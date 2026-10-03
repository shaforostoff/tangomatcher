#ifndef TT_PLUGIN_H
#define TT_PLUGIN_H

// The one symbol DeaDBeeF looks for, and what the windows and the test
// harness need from the plugin.
//
// DeaDBeeF finds a plugin's entry point from its file name: it strips the
// extension from ddb_tangotagger.so / .dll / .dylib and appends "_load".
// Renaming the library without renaming this function means it is silently
// skipped.

#ifndef DDB_API_LEVEL
#define DDB_API_LEVEL 10   // DeaDBeeF 1.8.0 and later
#endif
#include <deadbeef/deadbeef.h>

#include "tt_fields.h"

#if defined(_WIN32)
#define TT_EXPORT __declspec(dllexport)
#else
#define TT_EXPORT __attribute__((visibility("default")))
#endif

extern "C" TT_EXPORT DB_plugin_t * ddb_tangotagger_load(DB_functions_t * api);

namespace tt
{
	//! A copy of a track's metadata, taken under the playlist lock.
	track_meta snapshot(DB_playItem_t * track);

	//! Blocks until every queued job - matching, listening, writing - is
	//! done. The player never needs this; ddb_tangotagger_hosttest does.
	void wait_until_idle();
}

#endif // TT_PLUGIN_H
