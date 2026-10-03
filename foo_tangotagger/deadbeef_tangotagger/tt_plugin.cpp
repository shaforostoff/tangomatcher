// Tango Tagger for DeaDBeeF.
//
// The same lyrics and discography matching as foo_tangotagger - core/,
// shared, with the embedded data - behind DeaDBeeF's plugin API instead of
// foobar2000's: Find lyrics... and Match discographies... on the selected
// tracks, a results window to choose what to write, and the same fields
// written under the same rules. See tt_fields.h for where DeaDBeeF's field
// names differ.
//
// This file is the plugin without its windows: the actions, the settings, the
// worker threads, decoding for the matching by sound, and writing. The
// windows are behind tt_ui.h, one implementation per toolkit, and what they
// show is in tt_lyrics.h and tt_disco.h. Built without a toolkit, or run
// under an interface the windows do not belong to, the plugin writes what it
// is sure of - the rows the window would have shown checked - and reports
// every track in the log.

#include "tt_plugin.h"
#include "tt_disco.h"
#include "tt_lyrics.h"
#include "tt_ui.h"
#include "tt_version.h"

#include "disco_fingerprint.h"
#include "fingerprint_bpmcore.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <bpmcore/bpmcore.h>
// Which transform bpmcore was built with, and so which licence goes in the
// about text; shared with foo_rubato and foo_tangotagger.
#include <foo_rubato/fft_license.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace
{

DB_functions_t * deadbeef = nullptr;
DB_misc_t plugin;

// --- logging ---------------------------------------------------------------
//
// What the plugin did at the info layer, one line per track where there is no
// window to show it in. Failures go to the default layer, which DeaDBeeF's
// interfaces answer by opening the log window.

void log_at(uint32_t layer, const char * fmt, ...)
{
	char text[4096];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	deadbeef->log_detailed(&plugin.plugin, layer, "tangotagger: %s\n", text);
}

#define TT_INFO(...)  log_at(DDB_LOG_LAYER_INFO, __VA_ARGS__)
#define TT_ERROR(...) log_at(DDB_LOG_LAYER_DEFAULT, __VA_ARGS__)

const char * const plugin_name = "Tango Tagger";

// --- settings --------------------------------------------------------------
//
// What the results windows keep between sessions, as foo_tangotagger does -
// the links to translations, the artist scheme and the fields to write - and
// on the settings page as well, where a build without windows can reach them.

const char * const conf_links = "tangotagger.translation_links";
const char * const conf_scheme = "tangotagger.artist_scheme";
const char * const conf_fields[] = {
	"tangotagger.write_title", "tangotagger.write_artist", "tangotagger.write_album_artist",
	"tangotagger.write_date", "tangotagger.write_genre",
};
bool tangotagger::tag_options::* const option_fields[] = {
	&tangotagger::tag_options::title, &tangotagger::tag_options::artist, &tangotagger::tag_options::album_artist,
	&tangotagger::tag_options::date, &tangotagger::tag_options::genre,
};

std::string settings_dialog;

void build_settings_dialog()
{
	settings_dialog =
		"property \"Add links to translations to the lyrics\" checkbox tangotagger.translation_links 0;\n"
		"property \"Artist scheme\" select[";
	const int schemes = static_cast<int>(tangotagger::artist_scheme::count);
	settings_dialog += std::to_string(schemes) + "] " + conf_scheme + " 0";
	for (int s = 0; s < schemes; s++)
		settings_dialog += std::string(" \"") + tangotagger::artist_scheme_name(static_cast<tangotagger::artist_scheme>(s)) + "\"";
	settings_dialog +=
		";\n"
		"property \"Write TITLE\" checkbox tangotagger.write_title 1;\n"
		"property \"Write ARTIST (and CANTOR, where the scheme has it)\" checkbox tangotagger.write_artist 1;\n"
		"property \"Write ALBUM ARTIST\" checkbox tangotagger.write_album_artist 1;\n"
		"property \"Write DATE\" checkbox tangotagger.write_date 1;\n"
		"property \"Write GENRE\" checkbox tangotagger.write_genre 1;\n";
}

bool read_links()
{
	return deadbeef->conf_get_int(conf_links, 0) != 0;
}

void save_links(bool links)
{
	deadbeef->conf_set_int(conf_links, links ? 1 : 0);
	deadbeef->conf_save();
}

tangotagger::tag_options read_tag_options()
{
	tangotagger::tag_options o;
	const int scheme = deadbeef->conf_get_int(conf_scheme, 0);
	if (scheme >= 0 && scheme < static_cast<int>(tangotagger::artist_scheme::count))
		o.scheme = static_cast<tangotagger::artist_scheme>(scheme);
	for (std::size_t f = 0; f < sizeof option_fields / sizeof option_fields[0]; f++)
		o.*(option_fields[f]) = deadbeef->conf_get_int(conf_fields[f], 1) != 0;
	return o;
}

void save_tag_options(const tangotagger::tag_options & o)
{
	deadbeef->conf_set_int(conf_scheme, static_cast<int>(o.scheme));
	for (std::size_t f = 0; f < sizeof option_fields / sizeof option_fields[0]; f++)
		deadbeef->conf_set_int(conf_fields[f], o.*(option_fields[f]) ? 1 : 0);
	deadbeef->conf_save();
}

// --- tracks ----------------------------------------------------------------

std::string file_name(const std::string & path)
{
	const std::size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

//! A field's values: DeaDBeeF keeps several in one entry, NUL-separated.
//! Called with the playlist lock held.
std::vector<std::string> split_values(const DB_metaInfo_t * m)
{
	std::vector<std::string> out;
	if (m == nullptr || m->value == nullptr) return out;
	const char * p = m->value;
	const char * end = m->value + (m->valuesize > 0 ? m->valuesize : std::strlen(m->value) + 1);
	while (p < end)
	{
		const std::size_t n = strnlen(p, static_cast<std::size_t>(end - p));
		if (n > 0) out.emplace_back(p, n);
		p += n + 1;
	}
	return out;
}

std::vector<std::string> meta_values(DB_playItem_t * track, const char * key)
{
	return split_values(deadbeef->pl_meta_for_key(track, key));
}

//! The tracks an action is about, each holding a reference, which the last
//! owner gives back.
struct held_tracks
{
	std::vector<DB_playItem_t *> items;

	held_tracks() = default;
	held_tracks(const held_tracks &) = delete;
	held_tracks & operator=(const held_tracks &) = delete;
	~held_tracks()
	{
		for (DB_playItem_t * t : items) deadbeef->pl_item_unref(t);
	}
};

using tracks_ptr = std::shared_ptr<held_tracks>;

//! The tracks an action applies to, each with a reference held.
tracks_ptr action_tracks(int ctx)
{
	tracks_ptr tracks = std::make_shared<held_tracks>();
	if (ctx != DDB_ACTION_CTX_SELECTION && ctx != DDB_ACTION_CTX_PLAYLIST) return tracks;

	ddb_playlist_t * plt = deadbeef->action_get_playlist();
	if (plt == nullptr) return tracks;
	deadbeef->pl_lock();
	DB_playItem_t * it = deadbeef->plt_get_first(plt, PL_MAIN);
	while (it != nullptr)
	{
		if (ctx == DDB_ACTION_CTX_PLAYLIST || deadbeef->pl_is_selected(it))
		{
			deadbeef->pl_item_ref(it);
			tracks->items.push_back(it);
		}
		DB_playItem_t * next = deadbeef->pl_get_next(it, PL_MAIN);
		deadbeef->pl_item_unref(it);
		it = next;
	}
	deadbeef->pl_unlock();
	deadbeef->plt_unref(plt);
	return tracks;
}

std::vector<tt::track_meta> snapshots(const held_tracks & tracks)
{
	std::vector<tt::track_meta> out;
	out.reserve(tracks.items.size());
	for (DB_playItem_t * t : tracks.items) out.push_back(tt::snapshot(t));
	return out;
}

// --- the work queue --------------------------------------------------------
//
// Matching, listening and writing all happen here, off the interface's
// thread: a few thousand tracks take a while to match, and listening decodes
// them. As many threads as foo_tangotagger listens with - two short of the
// machine, so playback and the interface have cores left - at below-normal
// priority.

std::mutex queue_lock;
std::condition_variable queue_changed;
std::deque<std::function<void()>> queue;
std::vector<std::thread> pool;
int busy = 0;
bool stopping = false;
std::atomic<bool> aborting(false);

int worker_count()
{
	const int cores = static_cast<int>(std::thread::hardware_concurrency());
	return cores <= 2 ? 1 : cores - 2;
}

//! Lower the calling thread's priority, so listening to a library cannot take
//! the core playback is decoding on.
void deprioritise_this_thread()
{
#if defined(_WIN32)
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#elif defined(__APPLE__)
	pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#elif defined(__linux__)
	// Linux schedules threads as processes, so a thread's nice value is its
	// own. Raising it needs no privilege.
	setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}

void worker()
{
	deprioritise_this_thread();
	for (;;)
	{
		std::function<void()> job;
		{
			std::unique_lock<std::mutex> guard(queue_lock);
			queue_changed.wait(guard, [] { return stopping || !queue.empty(); });
			if (stopping) return;
			job = std::move(queue.front());
			queue.pop_front();
			busy++;
		}
		try
		{
			job();
		}
		catch (const std::exception & e)
		{
			TT_ERROR("%s", e.what());
		}
		catch (...)
		{
			TT_ERROR("unexpected error.");
		}
		// Whatever the job held - tracks, a review - goes before it counts as
		// done, so wait_until_idle sees every reference given back.
		job = nullptr;
		{
			std::lock_guard<std::mutex> guard(queue_lock);
			busy--;
		}
		queue_changed.notify_all();
	}
}

bool post(std::function<void()> job)
{
	{
		std::lock_guard<std::mutex> guard(queue_lock);
		if (stopping) return false;
		if (pool.empty())
		{
			const int n = worker_count();
			for (int i = 0; i < n; i++) pool.emplace_back(worker);
		}
		queue.push_back(std::move(job));
	}
	queue_changed.notify_all();
	return true;
}

// --- writing ---------------------------------------------------------------

//! One writer at a time. The decoders' tag writers were written for the track
//! properties dialog, which writes from one thread, and nothing promises they
//! are safe side by side.
std::mutex write_lock;

//! The decoder that can write this track's tags, or null with the reason
//! already logged.
DB_decoder_t * writer_for(DB_playItem_t * track, const std::string & name)
{
	deadbeef->pl_lock();
	const bool subtrack = (deadbeef->pl_get_item_flags(track) & DDB_IS_SUBTRACK) != 0;
	const char * id = deadbeef->pl_find_meta_raw(track, ":DECODER");
	const std::string decoder_id = id != nullptr ? id : "";
	deadbeef->pl_unlock();

	if (subtrack)
	{
		TT_ERROR("%s is one track of a multi-track file (a cue sheet, most often), "
		         "which DeaDBeeF cannot write tags to; nothing written.", name.c_str());
		return nullptr;
	}
	DB_decoder_t * dec = decoder_id.empty() ? nullptr
		: reinterpret_cast<DB_decoder_t *>(deadbeef->plug_get_for_id(decoder_id.c_str()));
	if (dec == nullptr || dec->write_metadata == nullptr)
	{
		TT_ERROR("DeaDBeeF cannot write tags to %s (decoder \"%s\"); nothing written.",
		         name.c_str(), decoder_id.c_str());
		return nullptr;
	}
	return dec;
}

//! Sets one field: every value replaced, the aliases removed. False when it
//! already reads exactly so. Called with the playlist lock held.
bool apply(DB_playItem_t * track, const tt::field_write & f)
{
	bool aliases = false;
	for (const std::string & a : f.aliases)
		aliases = aliases || deadbeef->pl_meta_for_key(track, a.c_str()) != nullptr;
	std::vector<std::string> wanted;
	for (const std::string & v : f.values)
		if (!v.empty()) wanted.push_back(v);
	if (!aliases && meta_values(track, f.key.c_str()) == wanted) return false;

	for (const std::string & a : f.aliases) deadbeef->pl_delete_meta(track, a.c_str());
	deadbeef->pl_delete_meta(track, f.key.c_str());
	for (const std::string & v : wanted) deadbeef->pl_append_meta(track, f.key.c_str(), v.c_str());
	return true;
}

//! Hands the changed fields to the decoder, and tells the rest of the player
//! the track changed. Call without the playlist lock.
bool commit(DB_decoder_t * dec, DB_playItem_t * track, const std::string & name)
{
	int failed;
	{
		std::lock_guard<std::mutex> guard(write_lock);
		failed = dec->write_metadata(track);
	}
	if (failed)
	{
		TT_ERROR("could not write tags to %s.", name.c_str());
		return false;
	}

	ddb_event_track_t * ev = reinterpret_cast<ddb_event_track_t *>(deadbeef->event_alloc(DB_EV_TRACKINFOCHANGED));
	ev->track = track;
	deadbeef->pl_item_ref(track);   // the event releases it
	deadbeef->event_send(reinterpret_cast<ddb_event_t *>(ev), 0, 0);

	if (ddb_playlist_t * plt = deadbeef->pl_get_playlist(track))
	{
		deadbeef->plt_modified(plt);
		deadbeef->plt_unref(plt);
	}
	return true;
}

//! Writes each track's fields, in the background.
void write_tracks(tracks_ptr tracks, std::vector<tt::track_write> writes, const char * what)
{
	post([tracks, writes, what]()
	{
		std::size_t written = 0, unchanged = 0, failed = 0;
		for (const tt::track_write & w : writes)
		{
			if (aborting.load()) break;
			if (w.track >= tracks->items.size()) continue;
			DB_playItem_t * track = tracks->items[w.track];
			deadbeef->pl_lock();
			const char * uri = deadbeef->pl_find_meta(track, ":URI");
			const std::string name = file_name(uri != nullptr ? uri : "");
			deadbeef->pl_unlock();

			DB_decoder_t * dec = writer_for(track, name);
			if (dec == nullptr) { failed++; continue; }
			bool changed = false;
			deadbeef->pl_lock();
			for (const tt::field_write & f : w.fields) changed = apply(track, f) || changed;
			deadbeef->pl_unlock();
			if (!changed) { unchanged++; continue; }
			if (commit(dec, track, name)) written++;
			else failed++;
		}
		std::string line = std::to_string(written) + " of " + std::to_string(writes.size())
		                 + (writes.size() == 1 ? " file" : " files") + " written (" + what + ")";
		if (unchanged > 0) line += "; " + std::to_string(unchanged) + " already had it";
		if (failed > 0) line += "; see above for the rest";
		TT_INFO("%s.", line.c_str());
		// The playlists hold a copy of every field; save them so the new
		// values are there after a restart without the files being read again.
		if (written > 0) deadbeef->pl_save_all();
	});
}

//! Says nothing matched: in a window where there is one, in the log always.
void nothing_matched(const std::string & text)
{
	TT_INFO("%s", text.c_str());
	if (tt::ui::available()) tt::ui::show_message(plugin_name, text);
}

// --- Find lyrics -----------------------------------------------------------

void find_lyrics(tracks_ptr tracks, std::vector<tt::track_meta> metas, bool links)
{
	tt::lyrics_matches m = tt::find_lyrics_matches(metas);
	if (aborting.load()) return;
	if (m.tracks_matched == 0)
	{
		nothing_matched(m.tracks_examined == 1
			? "No lyrics match the title of the selected track."
			: "No lyrics match the titles of the " + std::to_string(m.tracks_examined) + " selected tracks.");
		return;
	}
	TT_INFO("Find lyrics: %s", tt::lyrics_status_text(m).c_str());

	if (tt::ui::available())
	{
		tt::ui::show_review(std::make_shared<tt::lyrics_review>(std::move(m), links,
			[tracks](std::vector<tt::track_write> w) { write_tracks(tracks, std::move(w), "lyrics"); },
			save_links));
		return;
	}

	// No window: what it would have shown checked is written, and every
	// track the user would have had to decide about is listed instead.
	for (std::size_t i = 0; i < m.rows.size(); i++)
	{
		const tt::lyrics_row & r = m.rows[i];
		if (r.version != 1) continue;
		const std::string name = file_name(metas[r.group].path);
		std::size_t checked = i;
		for (std::size_t j = i; j < m.rows.size() && m.rows[j].group == r.group; j++)
			if (m.rows[j].checked) checked = j;
		if (!r.matched())
			TT_INFO("%s: no lyrics match \"%s\".", name.c_str(), r.title.c_str());
		else if (!r.writable)
			TT_INFO("%s: the lyrics of \"%s\", but it is one track of a multi-track file, which "
			        "DeaDBeeF cannot write tags to; left alone.", name.c_str(), tt::row_song(r).name.c_str());
		else if (m.rows[checked].checked)
			TT_INFO("%s: the lyrics of \"%s\".", name.c_str(), tt::row_song(m.rows[checked]).name.c_str());
		else if (r.in_file != tt::file_lyrics::none)
			TT_INFO("%s: already has lyrics (%s); left alone.", name.c_str(),
			        tt::existing_label(tt::existing(r, links)));
		else if (r.match.kind == tangotagger::match_kind::similar)
			TT_INFO("%s: only a similar title, \"%s\"; left alone.", name.c_str(), tt::row_song(r).name.c_str());
		else
			TT_INFO("%s: %d songs share the title \"%s\"; left alone.", name.c_str(), r.versions, r.title.c_str());
	}
	std::vector<tt::track_write> writes = tt::lyrics_writes(m.rows, links);
	if (!writes.empty()) write_tracks(tracks, std::move(writes), "lyrics");
}

int action_find_lyrics(DB_plugin_action_t *, int ctx)
{
	tracks_ptr tracks = action_tracks(ctx);
	if (tracks->items.empty()) return 0;
	std::vector<tt::track_meta> metas = snapshots(*tracks);
	const bool links = read_links();
	post([tracks, metas, links]() { find_lyrics(tracks, metas, links); });
	return 0;
}

// --- Match discographies ---------------------------------------------------

void show_disco(tracks_ptr tracks, std::vector<tt::disco_track> && found, const tangotagger::tag_options & options)
{
	if (aborting.load()) return;
	tt::disco_matches m = tt::disco_rows_of(std::move(found));
	if (m.tracks_matched == 0)
	{
		nothing_matched(tt::disco_nothing_text(m.tracks_examined));
		return;
	}
	TT_INFO("Match discographies: %s", tt::disco_status_text(m).c_str());

	if (tt::ui::available())
	{
		tt::ui::show_review(std::make_shared<tt::disco_review>(std::move(m), options,
			[tracks](std::vector<tt::track_write> w) { write_tracks(tracks, std::move(w), "discographies"); },
			save_tag_options));
		return;
	}

	// No window: the confident matches are written, the rest listed.
	for (std::size_t i = 0; i < m.rows.size(); i++)
	{
		const tt::disco_row & r = m.rows[i];
		if (r.version != 1) continue;
		const std::string name = file_name(r.path);
		if (!r.matched())
			TT_INFO("%s: not found in the discographies.", name.c_str());
		else
		{
			const tangotagger::recording & rec = tt::row_recording(r);
			TT_INFO("%s: %s, %s, %s, %s (%s)%s", name.c_str(), tangotagger::main_title(rec.name).c_str(),
			        tt::row_orchestra(r).c_str(), rec.vocal.c_str(), rec.date.c_str(), tt::disco_match_label(r).c_str(),
			        r.checked ? "" : !r.writable ? "; one track of a multi-track file, which DeaDBeeF cannot write tags to - left alone"
			                 : r.versions > 1 ? "; not sure enough, of several - left alone"
			                                  : "; not sure enough - left alone");
		}
	}
	std::vector<tt::track_write> writes = tt::disco_writes(m.rows, options);
	if (!writes.empty()) write_tracks(tracks, std::move(writes), "discographies");
}

//! The tracks the tags do not place, listened to: decoded, fingerprinted and
//! compared with the embedded fingerprints, several at once. The results
//! window opens when the last is done - or stopped, with what there is.
struct sound_batch : public tt::scan_progress
{
	tracks_ptr tracks;
	std::vector<tt::disco_track> found;
	std::vector<std::size_t> pending;   //!< indices into found
	tangotagger::tag_options options;
	std::vector<std::vector<tangotagger::fingerprint_match>> heard;   //!< per pending
	std::atomic<std::size_t> done{0};
	std::atomic<bool> cancelled{false};

	mutable std::mutex lock;
	//! Tracks being listened to, by pending index: their names and how far through.
	std::map<std::size_t, std::pair<std::string, double>> in_flight;

	bool stopped() const { return cancelled.load() || aborting.load(); }

	std::string title() const override
	{
		return "Listening to " + std::to_string(pending.size()) + (pending.size() == 1 ? " track" : " tracks")
		     + " the tags do not place...";
	}

	snapshot read() const override
	{
		snapshot out;
		out.total = pending.size();
		out.done = done.load();
		out.finished = out.done >= out.total;
		std::lock_guard<std::mutex> guard(lock);
		double partial = 0;
		std::size_t named = 0;
		for (const auto & f : in_flight)
		{
			partial += f.second.second;
			if (named < 2)
			{
				if (named > 0) out.in_flight += in_flight.size() == 2 ? " and " : ", ";
				out.in_flight += f.second.first;
			}
			named++;
		}
		if (in_flight.size() > 2) out.in_flight += " and " + std::to_string(in_flight.size() - 2) + " more";
		out.fraction = out.total == 0 ? 1.0 : (static_cast<double>(out.done) + partial) / out.total;
		return out;
	}

	void cancel() override { cancelled = true; }

	void set_fraction(std::size_t index, double f)
	{
		std::lock_guard<std::mutex> guard(lock);
		auto it = in_flight.find(index);
		if (it != in_flight.end()) it->second.second = f;
	}
};

//! The track's whole fingerprint, as a query; empty if it could not be
//! decoded, has no music in it, or the listening was stopped.
tangotagger::fingerprint listen(DB_playItem_t * track, const std::string & name, sound_batch & b, std::size_t index)
{
	deadbeef->pl_lock();
	const char * id = deadbeef->pl_find_meta(track, ":DECODER");
	const std::string decoder_id = id != nullptr ? id : "";
	deadbeef->pl_unlock();

	DB_decoder_t * dec = decoder_id.empty() ? nullptr
		: reinterpret_cast<DB_decoder_t *>(deadbeef->plug_get_for_id(decoder_id.c_str()));
	if (dec == nullptr)
	{
		TT_ERROR("no decoder for %s.", name.c_str());
		return tangotagger::fingerprint();
	}

	// The signal as it is in the file: no ReplayGain, no DSP.
	DB_fileinfo_t * info = dec->open(DDB_DECODER_HINT_RAW_SIGNAL);
	if (info == nullptr || dec->init(info, track) != 0)
	{
		TT_ERROR("could not open %s to listen to it.", name.c_str());
		if (info != nullptr) dec->free(info);
		return tangotagger::fingerprint();
	}

	const ddb_waveformat_t in_fmt = info->fmt;
	const int channels = in_fmt.channels;
	const int frame_bytes = channels * (in_fmt.bps / 8);
	if (channels <= 0 || frame_bytes <= 0 || in_fmt.samplerate <= 0)
	{
		TT_ERROR("%s reports no audio format.", name.c_str());
		dec->free(info);
		return tangotagger::fingerprint();
	}

	// DeaDBeeF's decoders hand over whatever the file holds; pcm_convert
	// turns anything that is not already float into float.
	ddb_waveformat_t float_fmt = in_fmt;
	float_fmt.bps = 32;
	float_fmt.is_float = 1;

	const int block_frames = 4096;
	std::vector<char> raw(static_cast<std::size_t>(block_frames) * frame_bytes);
	std::vector<float> samples(static_cast<std::size_t>(block_frames) * channels);

	const double expected = deadbeef->pl_get_item_duration(track);
	const double expected_frames = std::min(expected, bpmcore::max_seconds()) * in_fmt.samplerate;
	double decoded_frames = 0;

	// Read once, front to back, up to what bpmcore keeps.
	bpmcore::collector collector(static_cast<unsigned>(in_fmt.samplerate), expected);
	for (;;)
	{
		if (b.stopped()) break;
		const int bytes = dec->read(info, raw.data(), static_cast<int>(raw.size()));
		if (bytes <= 0) break;
		const int frames = bytes / frame_bytes;

		if (in_fmt.is_float && in_fmt.bps == 32)
			std::memcpy(samples.data(), raw.data(), static_cast<std::size_t>(frames) * frame_bytes);
		else
			deadbeef->pcm_convert(&in_fmt, raw.data(), &float_fmt, reinterpret_cast<char *>(samples.data()),
			                      frames * frame_bytes);

		collector.add_interleaved(samples.data(), static_cast<std::size_t>(frames), static_cast<unsigned>(channels));
		decoded_frames += frames;
		if (expected_frames > 0) b.set_fraction(index, 0.9 * std::min(1.0, decoded_frames / expected_frames));
		if (collector.full() || bytes < static_cast<int>(raw.size())) break;
	}
	dec->free(info);

	if (b.stopped() || collector.size() == 0) return tangotagger::fingerprint();
	bpmcore::features f = collector.finish_features(nullptr, 1);
	if (!f.ok) return tangotagger::fingerprint();
	return tangotagger::make_fingerprint(tangotagger::audio_features_of(std::move(f)), 0);
}

void listen_one(std::shared_ptr<sound_batch> b, std::size_t i)
{
	if (!b->stopped())
	{
		tt::disco_track & t = b->found[b->pending[i]];
		DB_playItem_t * track = b->tracks->items[t.base.group];
		const std::string name = file_name(t.base.path);
		{
			std::lock_guard<std::mutex> guard(b->lock);
			b->in_flight[i] = std::make_pair(name, 0.0);
		}
		const tangotagger::fingerprint q = listen(track, name, *b, i);
		if (!q.empty())
			b->heard[i] = tangotagger::embedded_fingerprints().identify(q, tangotagger::sound_hints(t.match));
		std::lock_guard<std::mutex> guard(b->lock);
		b->in_flight.erase(i);
	}
	// The last to finish, stopped or not, opens the window: what was found
	// so far is worth showing.
	if (b->done.fetch_add(1) + 1 == b->pending.size())
	{
		for (std::size_t p = 0; p < b->pending.size(); p++)
			if (!b->heard[p].empty()) tangotagger::add_sound(b->found[b->pending[p]].match, b->heard[p]);
		show_disco(b->tracks, std::move(b->found), b->options);
	}
}

void match_discographies(tracks_ptr tracks, std::vector<tt::track_meta> metas, tangotagger::tag_options options)
{
	std::vector<tt::disco_track> found = tt::match_disco_tracks(metas);
	std::vector<std::size_t> pending;
	if (!tangotagger::embedded_discography().fingerprints.empty())
		for (std::size_t i = 0; i < found.size(); i++)
			if (tangotagger::needs_sound(found[i].match)) pending.push_back(i);
	if (pending.empty())
	{
		show_disco(tracks, std::move(found), options);
		return;
	}

	std::shared_ptr<sound_batch> b = std::make_shared<sound_batch>();
	b->tracks = tracks;
	b->found = std::move(found);
	b->pending = std::move(pending);
	b->options = options;
	b->heard.resize(b->pending.size());
	TT_INFO("%s", b->title().c_str());
	for (std::size_t i = 0; i < b->pending.size(); i++)
		post([b, i]() { listen_one(b, i); });
	if (tt::ui::available()) tt::ui::show_progress(b);
}

int action_match_discographies(DB_plugin_action_t *, int ctx)
{
	tracks_ptr tracks = action_tracks(ctx);
	if (tracks->items.empty()) return 0;
	std::vector<tt::track_meta> metas = snapshots(*tracks);
	const tangotagger::tag_options options = read_tag_options();
	post([tracks, metas, options]() { match_discographies(tracks, metas, options); });
	return 0;
}

// --- actions ---------------------------------------------------------------

DB_plugin_action_t act_lyrics;
DB_plugin_action_t act_disco;

void init_action(DB_plugin_action_t & a, const char * name, DB_plugin_action_callback2_t callback)
{
	std::memset(&a, 0, sizeof(a));
	a.name = name;
	a.flags = DB_ACTION_SINGLE_TRACK | DB_ACTION_MULTIPLE_TRACKS | DB_ACTION_ADD_MENU;
	a.callback2 = callback;
}

//! Asked again every time a menu is built, which lets the titles follow
//! whether there are windows: "..." where one will open to choose in.
DB_plugin_action_t * get_actions(DB_playItem_t *)
{
	const bool windows = tt::ui::available();
	act_lyrics.title = windows ? "Tango Tagger/Find lyrics..." : "Tango Tagger/Find lyrics";
	act_disco.title = windows ? "Tango Tagger/Match discographies..." : "Tango Tagger/Match discographies";
	act_lyrics.next = &act_disco;
	act_disco.next = nullptr;
	return &act_lyrics;
}

// --- lifetime --------------------------------------------------------------

int plugin_start()
{
	std::lock_guard<std::mutex> guard(queue_lock);
	stopping = false;
	aborting = false;
	return 0;
}

int plugin_connect()
{
	tt::ui::connect(deadbeef);
	return 0;
}

int plugin_disconnect()
{
	tt::ui::disconnect();
	return 0;
}

//! Abandons whatever is still queued - a track half listened to is not
//! matched, and nothing more is written - and waits for the threads, so none
//! of them outlives the player's API.
int plugin_stop()
{
	aborting = true;
	tt::ui::shutdown();
	std::vector<std::thread> threads;
	{
		std::lock_guard<std::mutex> guard(queue_lock);
		stopping = true;
		threads.swap(pool);
	}
	queue_changed.notify_all();
	for (std::thread & t : threads) t.join();

	// The jobs never run give back the tracks they hold as they go.
	std::deque<std::function<void()>> left;
	{
		std::lock_guard<std::mutex> guard(queue_lock);
		left.swap(queue);
	}
	left.clear();
	return 0;
}

#if TT_PUBLIC_DOMAIN_ONLY
#define TT_LYRICS_DATA \
	"The lyrics are in the public domain in Argentina: their authors died more " \
	"than 70 years ago. Texts from todotango.com; credits from tango.info and wikipedia.\n"
#else
#define TT_LYRICS_DATA \
	"This copy carries lyrics that are not in the public domain, contact me if you " \
	"want specific lyrics to be removed. It must not be distributed. Texts from " \
	"todotango.com; credits from tango.info and wikipedia.\n"
#endif

const char plugin_description[] =
	"Writes tango lyrics and discography data into your files.\n"
	"\n"
	"Select tracks and use Tango Tagger in the context menu.\n"
	"\n"
	"Find lyrics matches each track's title against the lyrics built into the "
	"plugin - ignoring accents, case, punctuation and spacing, and never matching "
	"a title by part of it, so \"Cafe\" is not \"Cafe Dominguez\" - and writes "
	"the chosen ones to LYRICS (UNSYNCED LYRICS, the ID3 USLT frame, in an mp3).\n"
	"\n"
	"Match discographies finds the tracks' recordings in the orchestra "
	"discographies built into the plugin - by the title, and by the orchestra, "
	"singer and date wherever the tags, the file name or its folder mention "
	"them - and fixes their title, artist, album artist, date and genre, with "
	"the singer where you want it. A track the tags cannot place is matched by "
	"its sound, against fingerprints of known transfers of the recordings.\n"
	"\n"
	"With the GTK interface, the Tango Lyrics widget (View > Design Mode) shows "
	"the selected track's lyrics: the file's own, or the built-in ones its title "
	"matches.\n"
	"\n"
	TT_LYRICS_DATA
	"Discographies from todotango.com, tango.info and tangoteca, and of Aníbal "
	"Troilo, Carlos di Sarli, Edgardo Donato, Horacio Salgán, Juan D'Arienzo, "
	"Lucio Demare, the Orquesta Típica Victor, Osvaldo Fresedo, Pedro Laurenz "
	"and Rodolfo Biagi from Tango Time Travel.";

const char plugin_copyright[] =
	"Tango Tagger " TT_VERSION "\n"
	"\n"
	"MIT License\n"
	"\n"
	"Copyright (c) 2026 Nick Shaforostov\n"
	"\n"
	"Permission is hereby granted, free of charge, to any person obtaining a copy "
	"of this software and associated documentation files (the \"Software\"), to deal "
	"in the Software without restriction, including without limitation the rights "
	"to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
	"copies of the Software, and to permit persons to whom the Software is "
	"furnished to do so, subject to the following conditions:\n"
	"\n"
	"The above copyright notice and this permission notice shall be included in all "
	"copies or substantial portions of the Software.\n"
	"\n"
	"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
	"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
	"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
	"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
	"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
	"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
	"SOFTWARE.\n"
	"\n"
	"The data the plugin embeds is not under the MIT License. The Tango Time "
	"Travel discographies are (c) Tango Time Travel / Moving Art Studio ASBL, "
	"https://tangotimetravel.be/category/release-notes/, licensed under Creative "
	"Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0), "
	"https://creativecommons.org/licenses/by-sa/4.0/. Changes: converted from the "
	"original spreadsheets, spellings normalised, and merged with the other "
	"discographies, which leave out the recordings these have. The match window "
	"names the discography, version and date of every recording taken from them. "
	"The discography data in this plugin, adapted from theirs, is shared under "
	"the same licence. Tango Time Travel does not endorse this plugin. The lyrics "
	"and the other discographies keep their own status.\n"
	"\n"
	"Used libraries:\n"
	"LZMA SDK by Igor Pavlov - public domain.\n"
	"bpmcore from foo_bpm (https://github.com/shaforostoff/foo_bpm) - MIT License.\n"
	FOO_RUBATO_FFT_LICENSE;

}   // namespace

// --- tt_plugin.h -------------------------------------------------------------

tt::track_meta tt::snapshot(DB_playItem_t * track)
{
	track_meta out;
	deadbeef->pl_lock();
	const char * uri = deadbeef->pl_find_meta(track, ":URI");
	out.path = uri != nullptr ? uri : "";
	out.subtrack = (deadbeef->pl_get_item_flags(track) & DDB_IS_SUBTRACK) != 0;
	for (DB_metaInfo_t * m = deadbeef->pl_get_metadata_head(track); m != nullptr; m = m->next)
	{
		// ":" is the player's own, "!" an override of it, "_" internal.
		if (m->key == nullptr || std::strchr(":!_", m->key[0]) != nullptr) continue;
		for (std::string & v : split_values(m)) out.add(m->key, v);
	}
	deadbeef->pl_unlock();
	return out;
}

void tt::wait_until_idle()
{
	std::unique_lock<std::mutex> guard(queue_lock);
	queue_changed.wait(guard, [] { return stopping || (queue.empty() && busy == 0); });
}

extern "C" TT_EXPORT DB_plugin_t * ddb_tangotagger_load(DB_functions_t * api)
{
	deadbeef = api;

	init_action(act_lyrics, "tangotagger_find_lyrics", action_find_lyrics);
	init_action(act_disco, "tangotagger_match_discographies", action_match_discographies);
	build_settings_dialog();

	std::memset(&plugin, 0, sizeof(plugin));
	plugin.plugin.type = DB_PLUGIN_MISC;
	plugin.plugin.api_vmajor = 1;
	plugin.plugin.api_vminor = DDB_API_LEVEL;
	plugin.plugin.version_major = TT_VERSION_MAJOR;
	plugin.plugin.version_minor = TT_VERSION_MINOR;
	plugin.plugin.flags = DDB_PLUGIN_FLAG_LOGGING;
	plugin.plugin.id = "tangotagger";
	plugin.plugin.name = plugin_name;
	plugin.plugin.descr = plugin_description;
	plugin.plugin.copyright = plugin_copyright;
	plugin.plugin.website = "https://github.com/shaforostoff/tangomatcher";
	plugin.plugin.start = plugin_start;
	plugin.plugin.stop = plugin_stop;
	plugin.plugin.connect = plugin_connect;
	plugin.plugin.disconnect = plugin_disconnect;
	plugin.plugin.get_actions = get_actions;
	plugin.plugin.configdialog = settings_dialog.c_str();
	return &plugin.plugin;
}
