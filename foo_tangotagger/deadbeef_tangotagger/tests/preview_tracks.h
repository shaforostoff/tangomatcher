#ifndef TT_PREVIEW_TRACKS_H
#define TT_PREVIEW_TRACKS_H

// The made-up playlist tt_gtk_preview and tt_cocoa_preview match, so the two
// toolkits' windows are shown the same rows: an exact title, one several
// songs share, a near miss, a file that already has lyrics and one nothing
// matches; recordings placed confidently, one of several sessions, and a file
// with nothing on it but its name, which is listened to.

#include <string>
#include <vector>

#include "fake_host.h"

namespace fake
{

inline void add_preview_tracks()
{
	struct made_up
	{
		const char * path;
		const char * title;
		const char * artist;
		const char * year;
		const char * lyrics;
	};
	static const made_up tracks[] = {
		{ "/music/Di Sarli/01 Al compas del corazon.flac", "Al compás del corazón",
		  "Carlos Di Sarli - Alberto Podestá", "1942", nullptr },
		{ "/music/Canaro/02 Yira yira.mp3", "Yira, yira", "Francisco Canaro", "", nullptr },
		{ "/music/Various/03 Sin amor.flac", "Sin amor", "", "", nullptr },
		{ "/music/Troilo/04 Malena.m4a", "Malena", "Aníbal Troilo - Francisco Fiorentino", "1942",
		  "Malena canta el tango como ninguna..." },
		{ "/music/Pugliese/05 La yumba.flac", "La yumba", "Osvaldo Pugliese", "1946", nullptr },
		{ "/music/Unknown/06 Track06.wav", nullptr, nullptr, nullptr, nullptr },
		{ "/music/Di Sarli/07 Bahia blanca.flac", "Bahía Blanca", "Carlos Di Sarli", "", nullptr },
	};
	for (const made_up & m : tracks)
	{
		fake_track * t = new fake_track();
		t->set(":URI", m.path);
		if (m.title != nullptr) t->set("title", m.title);
		if (m.artist != nullptr && *m.artist != '\0') t->set("artist", m.artist);
		if (m.year != nullptr && *m.year != '\0') t->set("year", m.year);
		if (m.lyrics != nullptr) t->set("LYRICS", m.lyrics);
		// Long enough to listen to that the progress window shows.
		if (m.title == nullptr) t->seconds = 240;
		playlist.push_back(t);
	}
}

}   // namespace fake

#endif // TT_PREVIEW_TRACKS_H
