#include "stdafx.h"

// Match discographies, by sound where the tags are not enough.
//
// Tags and file names first, for every track: that is instant and usually
// enough. The tracks it leaves unsure are then decoded and fingerprinted -
// several at once, below normal priority, behind foobar2000's progress
// dialog - and compared with the fingerprints of known transfers embedded
// with the discographies (core/fingerprint.h, core/disco_fingerprint.h). The
// window opens when that is done, or with what there is if it is stopped.

#include "disco_rows.h"
#include "tangotagger_ui.h"
#include "version.h"

#include "disco_fingerprint.h"
#include "fingerprint_bpmcore.h"

#include <helpers/input_helpers.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

#ifndef _WIN32
#include <pthread.h>
#endif

namespace
{
	void show_or_explain(std::vector<disco_track> && tracks)
	{
		disco_matches matches = disco_rows_of(std::move(tracks));
		if (matches.tracks_matched == 0)
		{
			pfc::string_formatter msg;
			if (matches.tracks_examined == 1) msg << "The selected track was not found in the discographies.";
			else msg << "None of the " << matches.tracks_examined << " selected tracks was found in the discographies.";
			msg << "\n\nA track is matched by its title, and its orchestra has to be named somewhere on it: "
			       "artist, album artist, conductor, the file name or its folder.";
			if (!tangotagger::embedded_discography().fingerprints.empty())
				msg << " A track without them is matched by its sound, against known transfers of "
				    << tangotagger::embedded_discography().fingerprints.size() << " recordings.";
			popup_message::g_show(msg, FOO_TANGOTAGGER_NAME);
			return;
		}
		show_disco_matches(std::move(matches));
	}

	void below_normal_priority()
	{
#ifdef _WIN32
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#else
		pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
	}

	//! The track's whole fingerprint, as a query; empty if it could not be
	//! decoded or has no music in it.
	tangotagger::fingerprint listen(const metadb_handle_ptr & track, abort_callback & abort)
	{
		input_helper input;
		service_ptr_t<file> nothing;
		// Read once, front to back: no seektable, no looping.
		input.open(nothing, track, input_flag_simpledecode, abort, false, false);
		if (!input.is_open()) return tangotagger::fingerprint();

		std::unique_ptr<bpmcore::collector> collector;
		unsigned sample_rate = 0;
		audio_chunk_impl chunk;
		while (input.run(chunk, abort))
		{
			const unsigned channels = chunk.get_channels(), srate = chunk.get_srate();
			if (channels == 0 || srate == 0) continue;
			if (collector == nullptr)
			{
				sample_rate = srate;
				collector.reset(new bpmcore::collector(srate, track->get_length()));
			}
			else if (srate != sample_rate)
				break;   // a second time base would make the onsets meaningless
			collector->add_interleaved(chunk.get_data(), chunk.get_sample_count(), channels);
			if (collector->full()) break;
		}
		if (collector == nullptr || collector->size() == 0) return tangotagger::fingerprint();
		bpmcore::features f = collector->finish_features(nullptr, 1);
		if (!f.ok) return tangotagger::fingerprint();
		return tangotagger::make_fingerprint(tangotagger::audio_features_of(std::move(f)), 0);
	}

	class sound_matching : public threaded_process_callback
	{
	public:
		sound_matching(std::vector<disco_track> && tracks, std::vector<std::size_t> && pending)
			: m_tracks(std::move(tracks)), m_pending(std::move(pending)) {}

		void run(threaded_process_status & status, abort_callback & abort) override
		{
			const tangotagger::fingerprint_index & index = tangotagger::embedded_fingerprints();
			const std::size_t total = m_pending.size();
			std::vector<std::vector<tangotagger::fingerprint_match>> found(total);

			// Two cores short of the machine, as foo_rubato does: every
			// worker holds a decoder flat out, and playback should not be
			// what waits.
			const int cores = static_cast<int>(std::thread::hardware_concurrency());
			int workers = cores > 2 ? cores - 2 : 1;
			if (static_cast<std::size_t>(workers) > total) workers = static_cast<int>(total);

			std::atomic<std::size_t> next(0), done(0);
			auto worker = [&]()
			{
				below_normal_priority();
				for (;;)
				{
					if (abort.is_aborting()) return;
					const std::size_t i = next.fetch_add(1);
					if (i >= total) return;
					disco_track & t = m_tracks[m_pending[i]];
					try
					{
						const tangotagger::fingerprint q = listen(t.base.track, abort);
						if (!q.empty()) found[i] = index.identify(q, tangotagger::sound_hints(t.match));
					}
					catch (const exception_aborted &)
					{
						return;
					}
					catch (const std::exception & e)
					{
						FB2K_console_formatter() << FOO_TANGOTAGGER_NAME << ": could not listen to "
						                         << t.base.track->get_path() << ": " << e;
					}
					catch (...)
					{
					}
					done.fetch_add(1);
				}
			};

			std::vector<std::thread> pool;
			for (int w = 0; w < workers; w++)
			{
				try
				{
					pool.emplace_back(worker);
				}
				catch (...)
				{
					break;   // carry on with the threads there are
				}
			}
			if (pool.empty()) worker();
			// This thread only reports: threaded_process_status is not to be
			// touched from several. Every track a worker takes is counted done
			// unless the user stops it all.
			for (;;)
			{
				const std::size_t d = done.load();
				status.set_progress(d, total);
				if (d >= total || abort.is_aborting()) break;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			for (std::thread & t : pool) t.join();

			// Merged here, after every worker is done with the tracks.
			for (std::size_t i = 0; i < total; i++)
				if (!found[i].empty()) tangotagger::add_sound(m_tracks[m_pending[i]].match, found[i]);
		}

		void on_done(ctx_t, bool) override
		{
			// Stopped or not: what was found so far is worth showing.
			show_or_explain(std::move(m_tracks));
		}

	private:
		std::vector<disco_track> m_tracks;
		std::vector<std::size_t> m_pending;
	};
}

void match_discographies(metadb_handle_list_cref tracks)
{
	std::vector<disco_track> matched = match_disco_tracks(tracks);
	std::vector<std::size_t> pending;
	if (!tangotagger::embedded_discography().fingerprints.empty())
		for (std::size_t i = 0; i < matched.size(); i++)
			if (tangotagger::needs_sound(matched[i].match)) pending.push_back(i);
	if (pending.empty())
	{
		show_or_explain(std::move(matched));
		return;
	}

	pfc::string_formatter title;
	title << "Listening to " << pending.size() << (pending.size() == 1 ? " track" : " tracks")
	      << " the tags do not place...";
	threaded_process::g_run_modeless(
		fb2k::service_new<sound_matching>(std::move(matched), std::move(pending)),
		threaded_process::flag_show_abort | threaded_process::flag_show_delayed | threaded_process::flag_show_progress |
			threaded_process::flag_show_minimize,
		core_api::get_main_window(), title);
}
