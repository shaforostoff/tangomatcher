// The plugin driven through a stand-in for DeaDBeeF (fake_host.h), with a
// window system that records what it is asked to show (ui_recorder.h).
//
// Runs the real actions on the real worker threads, against the real embedded
// data: tracks matched, listened to where the tags do not place them, and
// written the way the player would see it. The first half runs with no
// windows, as a build without any does; the second with them, through the
// results windows' model, which is everything a window decides.
//
// What the data says - which song a title is, which recording - is read from
// the embedded data rather than written down here, so the test checks the
// plugin and not this month's discographies.

#include <cstdio>
#include <cstring>
#include <string>

#include "disco_match.h"
#include "fake_host.h"
#include "tt_disco.h"
#include "tt_lyrics.h"
#include "ui_recorder.h"

using namespace fake;

namespace
{

int failures = 0;

void check(bool ok, const std::string & what)
{
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok) failures++;
}

bool contains(const std::string & haystack, const std::string & needle)
{
	return haystack.find(needle) != std::string::npos;
}

bool logged(const std::string & needle)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	for (const std::string & l : log_lines)
		if (contains(l, needle)) return true;
	return false;
}

void run(DB_plugin_action_t * a)
{
	a->callback2(a, DDB_ACTION_CTX_SELECTION);
	tt::wait_until_idle();
}

//! The song a title matches, if exactly one does.
const tangotagger::song * only_song(const char * title)
{
	tt::track_meta t;
	t.path = "/x.flac";
	t.add("title", title);
	const tt::lyrics_matches m = tt::find_lyrics_matches({ t });
	if (m.rows.size() != 1 || !m.rows[0].matched()) return nullptr;
	return &tt::row_song(m.rows[0]);
}

//! The recording a track like this is confidently matched to, or null.
const tangotagger::recording * confident_recording(const char * title, const char * artist, const char * year)
{
	tt::track_meta t;
	t.path = "/x.flac";
	t.add("title", title);
	t.add("artist", artist);
	t.add("year", year);
	std::vector<tt::disco_track> found = tt::match_disco_tracks({ t });
	if (!found[0].match.confident) return nullptr;
	return &tangotagger::embedded_discography().recordings[found[0].match.candidates[0].recording];
}

bool all_refs_back(const std::vector<fake_track *> & tracks)
{
	std::lock_guard<std::recursive_mutex> guard(pl_mutex);
	for (fake_track * t : tracks)
		if (t->refs != 1) return false;
	return true;
}

}   // namespace

int main()
{
	init();
	quiet = std::getenv("TT_VERBOSE") == nullptr;

	DB_functions_t api = make_api();
	DB_plugin_t * p = ddb_tangotagger_load(&api);
	check(p != nullptr && p->type == DB_PLUGIN_MISC, "plugin loads as a misc plugin");
	check(p->api_vmajor == 1 && p->api_vminor == 10, "asks for API 1.10");
	check(p->configdialog != nullptr && contains(p->configdialog, "tangotagger.artist_scheme")
	      && contains(p->configdialog, "Orquesta - Cantor"), "settings dialog lists the artist schemes");
	check(p->start() == 0 && p->connect() == 0, "starts and connects");

	DB_plugin_action_t * lyrics = find_action(p, "tangotagger_find_lyrics");
	DB_plugin_action_t * disco = find_action(p, "tangotagger_match_discographies");
	check(lyrics != nullptr && disco != nullptr, "two actions");
	if (lyrics == nullptr || disco == nullptr) return 1;
	check(std::strcmp(lyrics->title, "Tango Tagger/Find lyrics") == 0, "without windows: no ellipsis");

	const tangotagger::song * yira = only_song("Yira, yira");
	const tangotagger::song * malena = only_song("Malena");
	const tangotagger::recording * podesta = confident_recording(
		"Al compás del corazón", "Carlos Di Sarli - Alberto Podestá", "1942");
	check(yira != nullptr && malena != nullptr, "the data has the test songs, once each");
	check(podesta != nullptr, "the data places the test recording confidently");
	if (yira == nullptr || malena == nullptr || podesta == nullptr) return 1;
	const std::string orchestra = tangotagger::embedded_discography().orchestras[podesta->orchestra];

	// --- Find lyrics, no windows -------------------------------------------
	{
		fake_track flac, mp3, has, none, cue, unselected;
		flac.set(":URI", "/music/Yira yira.flac");
		flac.set("title", "Yira, yira");
		mp3.set(":URI", "/music/malena.mp3");
		mp3.set("title", "Malena");
		has.set(":URI", "/music/has.flac");
		has.set("title", "Yira, yira");
		has.set("LYRICS", "Some other words");
		none.set(":URI", "/music/none.flac");
		none.set("title", "Zzz no such tango");
		cue.set(":URI", "/music/album.flac");
		cue.set("title", "Yira, yira");
		cue.subtrack = true;
		unselected.set(":URI", "/music/unselected.flac");
		unselected.set("title", "Yira, yira");
		unselected.selected = false;
		playlist = { &flac, &mp3, &has, &none, &cue, &unselected };

		run(lyrics);
		check(get(flac, "LYRICS") == tt::lyrics_tag_text(*yira, false) && flac.writes == 1,
		      "flac: LYRICS written, CRLF, no links");
		check(get(mp3, "UNSYNCED LYRICS") == tt::lyrics_tag_text(*malena, false) && get(mp3, "LYRICS").empty()
		      && mp3.writes == 1, "mp3: UNSYNCED LYRICS written");
		check(get(has, "LYRICS") == "Some other words" && has.writes == 0, "lyrics already in the file are kept");
		check(none.writes == 0 && logged("no lyrics match \"Zzz no such tango\""), "no match: logged, nothing written");
		check(cue.writes == 0 && get(cue, "LYRICS").empty() && logged("album.flac: the lyrics of")
		      && logged("multi-track file"), "cue sheet track: not written, logged");
		check(unselected.writes == 0 && get(unselected, "LYRICS").empty(), "unselected track untouched");
		check(saves >= 1 && events == 2, "playlists saved, two tracks announced changed");

		conf_ints["tangotagger.translation_links"] = 1;
		flac.set("LYRICS", tt::lyrics_tag_text(*yira, false));
		run(lyrics);
		check(get(flac, "LYRICS") == tt::lyrics_tag_text(*yira, false),
		      "lyrics already there without links are left alone, setting or not");
		conf_ints.erase("tangotagger.translation_links");
		check(all_refs_back(playlist), "every reference given back");
	}

	// --- Match discographies, no windows ------------------------------------
	{
		fake_track flac, mp3, unknown;
		flac.set(":URI", "/music/Al compas del corazon.flac");
		flac.set("title", "Al compás del corazón");
		flac.set("artist", "Carlos Di Sarli - Alberto Podestá");
		flac.set("year", "1942");
		mp3.set(":URI", "/music/al compas.mp3");
		mp3.set("title", "Al compas del corazon");
		mp3.set("artist", "Di Sarli");
		mp3.set("CANTOR", "Alberto Podestá");
		mp3.set("year", "1942");
		mp3.set("band", "Old band");
		mp3.set("ALBUM ARTIST", "Old album artist");
		unknown.set(":URI", "/music/02 Track02.flac");
		playlist = { &flac, &mp3, &unknown };

		run(disco);
		check(get(flac, "year") == podesta->date && flac.writes == 1, "flac: DATE goes to year");
		check(get(flac, "ALBUM ARTIST") == orchestra, "flac: ALBUM ARTIST under its own name");
		check(get(flac, "artist") == orchestra + " - " + podesta->vocal, "flac: ARTIST in the default scheme");
		check(!get(flac, "genre").empty(), "flac: GENRE written");
		check(get(mp3, "band") == orchestra && get(mp3, "ALBUM ARTIST").empty() && mp3.writes == 1,
		      "mp3: ALBUM ARTIST goes to band, the TXXX copy removed");
		check(unknown.writes == 0 && logged("Listening to 1 track") && logged("02 Track02.flac: not found"),
		      "untagged track: listened to, not found, nothing written");
		check(all_refs_back(playlist), "every reference given back");
	}

	// --- one track of a multi-track file, in the window's model -------------
	{
		tt::track_meta t;
		t.path = "/music/album.flac";
		t.subtrack = true;
		t.add("title", "Yira, yira");
		tt::lyrics_matches m = tt::find_lyrics_matches({ t });
		tt::set_row_checked(m.rows, 0, true);
		check(m.rows[0].matched() && !m.rows[0].checkable() && !m.rows[0].checked
		      && contains(tt::lyrics_preview_text(m.rows[0], false), "cannot write tags"),
		      "multi-track file: lyrics listed, not checkable, the preview says why");

		t.add("artist", "Carlos Di Sarli - Alberto Podestá");
		t.fields["title"] = { "Al compás del corazón" };
		t.add("year", "1942");
		tt::disco_matches d = tt::disco_rows_of(tt::match_disco_tracks({ t }));
		tt::check_all_disco_rows(d.rows);
		check(d.rows[0].confident && !d.rows[0].checked && !d.rows[0].checkable(),
		      "multi-track file: recording listed, confident, not checkable");
	}

	// --- with windows ----------------------------------------------------------
	recorder::available = true;
	check(std::strcmp(find_action(p, "tangotagger_find_lyrics")->title, "Tango Tagger/Find lyrics...") == 0,
	      "with windows: the ellipsis");
	{
		fake_track flac, none;
		flac.set(":URI", "/music/Yira yira.flac");
		flac.set("title", "Yira, yira");
		none.set(":URI", "/music/none.flac");
		none.set("title", "Zzz no such tango");
		playlist = { &flac, &none };

		run(lyrics);
		check(recorder::reviews.size() == 1 && flac.writes == 0, "lyrics: a window, nothing written yet");
		if (recorder::reviews.size() == 1)
		{
			tt::review & r = *recorder::reviews[0];
			check(r.columns().size() == 6 && r.size() == 2, "lyrics window: six columns, a row per track");
			check(r.cell(0, 1) == "Yira, yira" && r.cell(0, 3) == yira->name && r.cell(0, 4) == "exact"
			      && r.cell(0, 5) == "none", "lyrics window: the cells");
			check(r.checkable(0) && r.checked(0) && !r.checkable(1) && r.cell(1, 4) == "no match",
			      "lyrics window: the match checked, the miss not checkable");
			check(contains(r.preview(0), yira->text.substr(0, 20)), "lyrics window: the preview has the lyrics");
			check(contains(r.status(), "1 of 2 tracks matched"), "lyrics window: the status line");
			check(r.commit_label() == "Write 1 file", "lyrics window: the button says how many");
			r.set_option(0, 1);
			check(conf_ints["tangotagger.translation_links"] == 1 && conf_saves > 0, "lyrics window: links option kept");
			r.commit();
			tt::wait_until_idle();
			check(get(flac, "LYRICS") == tt::lyrics_tag_text(*yira, true) && flac.writes == 1,
			      "lyrics window: written with the links");
			r.commit();
			tt::wait_until_idle();
			check(flac.writes == 1, "lyrics window: writes once only");
		}
		recorder::reviews.clear();
		conf_ints.erase("tangotagger.translation_links");

		playlist = { &none };
		run(lyrics);
		check(recorder::reviews.empty() && recorder::messages.size() == 1
		      && contains(recorder::messages[0], "No lyrics match the title"), "lyrics: nothing matched, a message");
		recorder::messages.clear();
		playlist = { &flac, &none };
		check(all_refs_back(playlist), "every reference given back");
	}
	{
		fake_track flac, unknown;
		flac.set(":URI", "/music/Al compas del corazon.flac");
		flac.set("title", "Al compás del corazón");
		flac.set("artist", "Carlos Di Sarli - Alberto Podestá");
		flac.set("year", "1942-04");
		unknown.set(":URI", "/music/02 Track02.flac");
		playlist = { &flac, &unknown };

		run(disco);
		check(recorder::progress.size() == 1 && recorder::progress[0]->read().finished
		      && contains(recorder::progress[0]->title(), "Listening to 1 track"), "disco: listening had a progress window");
		check(recorder::reviews.size() == 1 && flac.writes == 0, "disco: a window, nothing written yet");
		if (recorder::reviews.size() == 1)
		{
			tt::review & r = *recorder::reviews[0];
			check(r.columns().size() == 8 && r.columns()[6].heading == "Date"
			      && r.columns()[6].kind == tt::review_column::fit, "disco window: eight columns, Date fits its dates");
			check(r.cell(0, 6) == podesta->date && r.cell(0, 7) == "confident" && r.checked(0),
			      "disco window: the match, checked");
			check(r.cell(r.size() - 1, 7) == "no match" && !r.checkable(r.size() - 1), "disco window: the miss last");
			check(r.block(0) != r.block(r.size() - 1), "disco window: tracks shaded apart");
			check(contains(r.preview(0), "Writing changes:") && contains(r.preview(0), "DATE:"),
			      "disco window: the preview lists the changes");
			const std::vector<tt::review_option> o = r.options();
			check(o.size() == 6 && o[0].kind == tt::review_option::choice
			      && o[0].choices.size() == static_cast<std::size_t>(tangotagger::artist_scheme::count),
			      "disco window: the scheme and five fields");
			r.set_option(0, static_cast<int>(tangotagger::artist_scheme::singer_only));
			r.set_option(5, 0);   // genre off
			check(conf_ints["tangotagger.artist_scheme"] == static_cast<int>(tangotagger::artist_scheme::singer_only)
			      && conf_ints["tangotagger.write_genre"] == 0, "disco window: options kept");
			check(!contains(r.preview(0), "GENRE:"), "disco window: the preview follows the options");
			r.commit();
			tt::wait_until_idle();
			check(get(flac, "artist") == podesta->vocal && get(flac, "ALBUM ARTIST") == orchestra
			      && get(flac, "genre").empty() && flac.writes == 1, "disco window: written in the chosen scheme");
		}
		recorder::reviews.clear();
		recorder::progress.clear();
		check(all_refs_back(playlist), "every reference given back");
	}

	check(p->disconnect() == 0 && p->stop() == 0, "disconnects and stops");
	check(plt_refs == 0, "every playlist reference given back");
	std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
