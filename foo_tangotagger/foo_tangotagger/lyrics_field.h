#pragma once

#include <pfc/pfc.h>

// Which field puts the lyrics where players look for them, for this file.
//
// foobar2000 writes a field under the name it is given, except where it maps
// a name onto a native frame or atom:
//
//     MP3 (ID3v2)       "UNSYNCED LYRICS"  -> USLT frame
//     MP4 / M4A         "LYRICS"           -> ©lyr atom
//     FLAC, Ogg, Opus,  "LYRICS"           -> LYRICS field, as written by
//     APE, WavPack...                         most taggers
//
// which are the same frames and fields lyrmatcher wrote with TagLib.
inline const char * lyrics_field_for_path(const char * path)
{
	const char * ext = nullptr;
	for (const char * p = path; p != nullptr && *p != '\0'; p++)
	{
		if (*p == '.') ext = p + 1;
		else if (*p == '/' || *p == '\\' || *p == '|') ext = nullptr;
	}
	if (ext != nullptr)
	{
		static const char * const id3[] = { "mp3", "mp2", "mp1" };
		for (const char * e : id3)
			if (pfc::stricmp_ascii(ext, e) == 0) return "UNSYNCED LYRICS";
	}
	return "LYRICS";
}

//! Every name lyrics are commonly found under, for telling whether a track
//! already has some.
static const char * const lyrics_known_fields[] = { "LYRICS", "UNSYNCED LYRICS", "UNSYNCEDLYRICS" };
