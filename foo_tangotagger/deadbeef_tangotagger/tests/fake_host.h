#ifndef TT_FAKE_HOST_H
#define TT_FAKE_HOST_H

// A stand-in for DeaDBeeF: the part of DB_functions_t the plugin calls - a
// playlist, track metadata with DeaDBeeF's NUL-separated multiple values, a
// decoder that synthesises audio, the configuration, the log and what is
// playing - so the real plugin can be run with no player installed. Shared by
// ddb_tangotagger_hosttest and the two window previews. Built like
// deadbeef_rubato's.
//
// What it cannot stand in for is the player's own half: whether a decoder's
// write_metadata puts a field where the plugin expects, which tt_fields.h
// documents from DeaDBeeF's sources.

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "tt_plugin.h"

namespace fake
{

struct fake_meta
{
	DB_metaInfo_t info;
	std::string key;
	std::string value;   //!< values NUL-separated, with a NUL at the end
};

struct fake_track
{
	DB_playItem_t base;   // first, so a DB_playItem_t * is a fake_track *
	std::vector<std::unique_ptr<fake_meta>> meta;
	bool selected = true;
	bool subtrack = false;
	int refs = 1;         // the playlist's own
	int writes = 0;
	double seconds = 20;  //!< what the fake decoder produces

	fake_track() : base() {}
	fake_track(const fake_track &) = delete;
	fake_track & operator=(const fake_track &) = delete;

	//! Sets a field to one value, or several.
	void set(const std::string & key, const std::string & value);
	void set(const std::string & key, const std::vector<std::string> & values);
};

extern std::recursive_mutex pl_mutex;
extern std::vector<fake_track *> playlist;
extern std::map<std::string, int> conf_ints;
extern int conf_saves;
extern fake_track * playing;   //!< what streamer_get_playing_track returns
//! Returned for its own id by plug_get_for_id, to stand in for DeaDBeeF's
//! interface plugin; null for none.
extern DB_plugin_t * ui_plugin;
extern bool quiet;             //!< no log on stdout
extern std::vector<std::string> log_lines;

extern int plt_refs;
extern int saves;
extern int events;

//! Sets up the decoder; call once first.
void init();
DB_functions_t make_api();

//! A field's values, read without the plugin's help.
std::vector<std::string> values(fake_track & t, const char * key);
//! Its first value; blank for none.
std::string get(fake_track & t, const char * key);
DB_plugin_action_t * find_action(DB_plugin_t * p, const char * name);

}   // namespace fake

#endif // TT_FAKE_HOST_H
