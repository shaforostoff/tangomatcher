// A stand-in for DeaDBeeF, for the tests and the previews. See fake_host.h.

#include "fake_host.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fake
{

std::recursive_mutex pl_mutex;
std::vector<fake_track *> playlist;
ddb_playlist_t * const the_playlist = reinterpret_cast<ddb_playlist_t *>(&playlist);
std::map<std::string, int> conf_ints;
int conf_saves = 0;
fake_track * playing = nullptr;
DB_plugin_t * ui_plugin = nullptr;
bool quiet = false;
std::vector<std::string> log_lines;

fake_track * T(DB_playItem_t * it) { return reinterpret_cast<fake_track *>(it); }

bool same_key(const std::string & a, const char * b)
{
	if (a.size() != std::strlen(b)) return false;
	for (std::size_t i = 0; i < a.size(); i++)
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	return true;
}

//! The list DeaDBeeF hands out, relinked after every change.
void relink(fake_track & t)
{
	for (std::size_t i = 0; i < t.meta.size(); i++)
	{
		fake_meta & m = *t.meta[i];
		m.info.key = m.key.c_str();
		m.info.value = m.value.c_str();
		m.info.valuesize = static_cast<int>(m.value.size());
		m.info.next = i + 1 < t.meta.size() ? &t.meta[i + 1]->info : nullptr;
	}
}

fake_meta * find(fake_track & t, const char * key)
{
	for (auto & m : t.meta)
		if (same_key(m->key, key)) return m.get();
	return nullptr;
}

void remove(fake_track & t, const char * key)
{
	t.meta.erase(std::remove_if(t.meta.begin(), t.meta.end(),
	                            [key](const std::unique_ptr<fake_meta> & m) { return same_key(m->key, key); }),
	             t.meta.end());
	relink(t);
}

void fake_track::set(const std::string & key, const std::vector<std::string> & values)
{
	remove(*this, key.c_str());
	if (values.empty()) return;
	std::unique_ptr<fake_meta> m(new fake_meta());
	m->key = key;
	for (const std::string & v : values)
	{
		m->value += v;
		m->value += '\0';
	}
	meta.push_back(std::move(m));
	relink(*this);
}

void fake_track::set(const std::string & key, const std::string & value)
{
	set(key, std::vector<std::string>(1, value));
}

std::vector<std::string> values(fake_track & t, const char * key)
{
	std::vector<std::string> out;
	fake_meta * m = find(t, key);
	if (m == nullptr) return out;
	std::size_t p = 0;
	while (p < m->value.size())
	{
		const std::size_t nul = m->value.find('\0', p);
		out.push_back(m->value.substr(p, nul - p));
		p = nul + 1;
	}
	return out;
}

std::string get(fake_track & t, const char * key)
{
	const std::vector<std::string> v = values(t, key);
	return v.empty() ? std::string() : v.front();
}

// --- a decoder -------------------------------------------------------------

struct fake_fileinfo
{
	DB_fileinfo_t base;
	fake_track * track;
	std::size_t frame = 0;
	std::size_t frames = 0;
};

DB_decoder_t fake_decoder;

DB_fileinfo_t * dec_open(uint32_t)
{
	fake_fileinfo * info = new fake_fileinfo();
	info->base.plugin = &fake_decoder;
	return &info->base;
}

int dec_init(DB_fileinfo_t * base, DB_playItem_t * it)
{
	fake_fileinfo * info = reinterpret_cast<fake_fileinfo *>(base);
	info->track = T(it);
	base->fmt.channels = 2;
	base->fmt.channelmask = 3;
	base->fmt.samplerate = 44100;
	base->fmt.bps = 16;
	base->fmt.is_float = 0;
	info->frames = static_cast<std::size_t>(info->track->seconds * 44100);
	return 0;
}

void dec_free(DB_fileinfo_t * base)
{
	delete reinterpret_cast<fake_fileinfo *>(base);
}

//! Clicks two a second over a held A minor chord: something with onsets and
//! pitch in it, which no fingerprint of a real recording will match.
float sample_at(std::size_t frame)
{
	const double pi = 3.14159265358979323846;
	const double s = static_cast<double>(frame) / 44100;
	const double into = std::fmod(s, 0.5);
	double v = 0;
	if (into < 0.08) v += 0.3 * std::exp(-30.0 * into) * std::sin(2.0 * pi * 900.0 * s);
	for (double f : { 220.0, 261.63, 329.63 }) v += 0.05 * std::sin(2.0 * pi * f * s);
	return static_cast<float>(v);
}

int dec_read(DB_fileinfo_t * base, char * buffer, int nbytes)
{
	fake_fileinfo * info = reinterpret_cast<fake_fileinfo *>(base);
	const int frame_bytes = base->fmt.channels * 2;
	int written = 0;
	while (written + frame_bytes <= nbytes && info->frame < info->frames)
	{
		const int16_t s = static_cast<int16_t>(std::lround(sample_at(info->frame++) * 32767.0));
		for (int c = 0; c < base->fmt.channels; c++)
		{
			std::memcpy(buffer + written, &s, sizeof(s));
			written += 2;
		}
	}
	return written;
}

int dec_write_metadata(DB_playItem_t * it)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	T(it)->writes++;
	return 0;
}

// --- DB_functions_t ----------------------------------------------------------

void f_log_detailed(DB_plugin_t *, uint32_t layers, const char * fmt, ...)
{
	char text[8192];
	va_list args;
	va_start(args, fmt);
	std::vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	{
		std::lock_guard<std::recursive_mutex> guard(pl_mutex);
		log_lines.push_back(text);
	}
	if (!quiet) std::printf("%s%s", layers == DDB_LOG_LAYER_INFO ? "[info]  " : "[error] ", text);
}

int f_conf_get_int(const char * key, int def)
{
	auto it = conf_ints.find(key);
	return it != conf_ints.end() ? it->second : def;
}

void f_conf_set_int(const char * key, int value) { conf_ints[key] = value; }
int f_conf_save() { conf_saves++; return 0; }

void f_pl_lock() { pl_mutex.lock(); }
void f_pl_unlock() { pl_mutex.unlock(); }

void f_pl_item_ref(DB_playItem_t * it)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	T(it)->refs++;
}

void f_pl_item_unref(DB_playItem_t * it)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	T(it)->refs--;
}

DB_metaInfo_t * f_pl_meta_for_key(DB_playItem_t * it, const char * key)
{
	fake_meta * m = find(*T(it), key);
	return m != nullptr ? &m->info : nullptr;
}

DB_metaInfo_t * f_pl_get_metadata_head(DB_playItem_t * it)
{
	return T(it)->meta.empty() ? nullptr : &T(it)->meta.front()->info;
}

const char * f_pl_find_meta(DB_playItem_t * it, const char * key)
{
	if (std::strcmp(key, ":DECODER") == 0) return "fake";
	fake_meta * m = find(*T(it), key);
	return m != nullptr ? m->value.c_str() : nullptr;
}

void f_pl_replace_meta(DB_playItem_t * it, const char * key, const char * value)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	T(it)->set(key, value);
}

//! Adds a value, unless the field has it already - as DeaDBeeF's
//! pl_append_meta combines values into a unique set.
void f_pl_append_meta(DB_playItem_t * it, const char * key, const char * value)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	if (value == nullptr || *value == '\0') return;
	std::vector<std::string> v = values(*T(it), key);
	if (std::find(v.begin(), v.end(), value) != v.end()) return;
	v.push_back(value);
	fake_meta * m = find(*T(it), key);
	T(it)->set(m != nullptr ? m->key : std::string(key), v);
}

void f_pl_delete_meta(DB_playItem_t * it, const char * key)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	remove(*T(it), key);
}

uint32_t f_pl_get_item_flags(DB_playItem_t * it) { return T(it)->subtrack ? DDB_IS_SUBTRACK : 0; }
float f_pl_get_item_duration(DB_playItem_t * it) { return static_cast<float>(T(it)->seconds); }
int f_pl_is_selected(DB_playItem_t * it) { return T(it)->selected ? 1 : 0; }

DB_plugin_t * f_plug_get_for_id(const char * id)
{
	if (std::strcmp(id, "fake") == 0) return &fake_decoder.plugin;
	if (ui_plugin != nullptr && ui_plugin->id != nullptr && std::strcmp(id, ui_plugin->id) == 0) return ui_plugin;
	return nullptr;
}

int plt_refs = 0;
int saves = 0;

ddb_playlist_t * f_action_get_playlist() { plt_refs++; return the_playlist; }
ddb_playlist_t * f_plt_get_curr() { plt_refs++; return the_playlist; }
ddb_playlist_t * f_pl_get_playlist(DB_playItem_t *) { plt_refs++; return the_playlist; }
void f_plt_unref(ddb_playlist_t *) { plt_refs--; }
void f_plt_modified(ddb_playlist_t *) {}
int f_pl_save_all() { saves++; return 0; }

DB_playItem_t * at(std::size_t i)
{
	if (i >= playlist.size()) return nullptr;
	f_pl_item_ref(&playlist[i]->base);
	return &playlist[i]->base;
}

DB_playItem_t * f_plt_get_first(ddb_playlist_t *, int) { return at(0); }

DB_playItem_t * f_pl_get_next(DB_playItem_t * it, int)
{
	for (std::size_t i = 0; i < playlist.size(); i++)
		if (&playlist[i]->base == it) return at(i + 1);
	return nullptr;
}

int f_plt_get_cursor(ddb_playlist_t *, int)
{
	for (std::size_t i = 0; i < playlist.size(); i++)
		if (playlist[i]->selected) return static_cast<int>(i);
	return -1;
}

DB_playItem_t * f_plt_get_item_for_idx(ddb_playlist_t *, int idx, int)
{
	return idx < 0 ? nullptr : at(static_cast<std::size_t>(idx));
}

int f_pcm_convert(const ddb_waveformat_t * in, const char * input, const ddb_waveformat_t * out, char * output,
                  int inputsize)
{
	if (in->bps != 16 || in->is_float || out->bps != 32 || !out->is_float) std::abort();
	const int n = inputsize / 2;
	for (int i = 0; i < n; i++)
	{
		int16_t s;
		std::memcpy(&s, input + 2 * i, 2);
		const float f = s / 32768.0f;
		std::memcpy(output + 4 * i, &f, 4);
	}
	return n * 4;
}

int events = 0;

ddb_event_t * f_event_alloc(uint32_t id)
{
	ddb_event_track_t * ev = static_cast<ddb_event_track_t *>(std::calloc(1, sizeof(ddb_event_track_t)));
	ev->ev.event = static_cast<int>(id);
	ev->ev.size = sizeof(ddb_event_track_t);
	return &ev->ev;
}

int f_event_send(ddb_event_t * ev, uint32_t, uint32_t)
{
	ddb_event_track_t * tev = reinterpret_cast<ddb_event_track_t *>(ev);
	if (ev->event == DB_EV_TRACKINFOCHANGED && tev->track != nullptr)
	{
		std::lock_guard<std::recursive_mutex> guard(pl_mutex);
		events++;
		f_pl_item_unref(tev->track);
	}
	std::free(ev);
	return 0;
}

DB_playItem_t * f_streamer_get_playing_track()
{
	if (playing == nullptr) return nullptr;
	f_pl_item_ref(&playing->base);
	return &playing->base;
}

DB_functions_t make_api()
{
	DB_functions_t api;
	std::memset(&api, 0, sizeof(api));
	api.log_detailed = f_log_detailed;
	api.conf_get_int = f_conf_get_int;
	api.conf_set_int = f_conf_set_int;
	api.conf_save = f_conf_save;
	api.pl_lock = f_pl_lock;
	api.pl_unlock = f_pl_unlock;
	api.pl_item_ref = f_pl_item_ref;
	api.pl_item_unref = f_pl_item_unref;
	api.pl_meta_for_key = f_pl_meta_for_key;
	api.pl_get_metadata_head = f_pl_get_metadata_head;
	api.pl_find_meta = f_pl_find_meta;
	api.pl_find_meta_raw = f_pl_find_meta;
	api.pl_replace_meta = f_pl_replace_meta;
	api.pl_append_meta = f_pl_append_meta;
	api.pl_delete_meta = f_pl_delete_meta;
	api.pl_get_item_flags = f_pl_get_item_flags;
	api.pl_get_item_duration = f_pl_get_item_duration;
	api.pl_is_selected = f_pl_is_selected;
	api.plug_get_for_id = f_plug_get_for_id;
	api.action_get_playlist = f_action_get_playlist;
	api.plt_get_curr = f_plt_get_curr;
	api.pl_get_playlist = f_pl_get_playlist;
	api.plt_unref = f_plt_unref;
	api.plt_modified = f_plt_modified;
	api.pl_save_all = f_pl_save_all;
	api.plt_get_first = f_plt_get_first;
	api.pl_get_next = f_pl_get_next;
	api.plt_get_cursor = f_plt_get_cursor;
	api.plt_get_item_for_idx = f_plt_get_item_for_idx;
	api.pcm_convert = f_pcm_convert;
	api.streamer_get_playing_track = f_streamer_get_playing_track;
	api.event_alloc = f_event_alloc;
	api.event_send = f_event_send;
	return api;
}

DB_plugin_action_t * find_action(DB_plugin_t * p, const char * name)
{
	for (DB_plugin_action_t * a = p->get_actions(nullptr); a != nullptr; a = a->next)
		if (std::strcmp(a->name, name) == 0) return a;
	return nullptr;
}

void init()
{
	fake_decoder.plugin.type = DB_PLUGIN_DECODER;
	fake_decoder.plugin.id = "fake";
	fake_decoder.open = dec_open;
	fake_decoder.init = dec_init;
	fake_decoder.free = dec_free;
	fake_decoder.read = dec_read;
	fake_decoder.write_metadata = dec_write_metadata;
}

}   // namespace fake
