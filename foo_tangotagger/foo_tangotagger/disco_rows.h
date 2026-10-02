#pragma once

// What the Match discographies window shows and what its Write button
// writes; both platforms' windows are built on this, like lyrics_rows.h.
//
// One row per recording a track could be, grouped under the track, at most
// one of a group checked. A track nothing matched gets one row, which cannot
// be checked.

#include <SDK/foobar2000.h>

#include <map>
#include <string>
#include <vector>

#include "disco_match.h"
#include "disco_tags.h"

//! The artist scheme and the fields to write. Kept between sessions.
tangotagger::tag_options disco_tag_options();
void set_disco_tag_options(const tangotagger::tag_options & options);

struct disco_row
{
	metadb_handle_ptr track;
	std::size_t group = 0;     //!< rows of one track share this
	int version = 1;           //!< 1-based position among the track's candidates
	int versions = 1;
	bool confident = false;    //!< the track's best candidate, and sure of it
	pfc::string8 title;        //!< TITLE, or the file name without one
	pfc::string8 artist;
	tangotagger::recording_match match;
	//! The track's values of every field a scheme can write.
	std::map<std::string, std::vector<std::string>> current;
	bool checked = false;

	bool matched() const { return match.recording >= 0; }
};

struct disco_matches
{
	std::vector<disco_row> rows;
	std::size_t tracks_examined = 0;
	std::size_t tracks_matched = 0;
	std::size_t tracks_confident = 0;
	std::size_t tracks_by_sound = 0;     //!< confident only because of their sound
};

//! A selected track and what matching it found, before it becomes rows.
struct disco_track
{
	disco_row base;                      //!< the track's own fields; no match yet
	tangotagger::track_match match;
	bool confident_by_tags = false;
};

//! Matches every track against the embedded discographies by its tags and
//! file name. Once each, in selection order.
std::vector<disco_track> match_disco_tracks(metadb_handle_list_cref tracks);

//! The rows: tracks in selection order, each with its candidates best first;
//! the tracks nothing matched after them. A confident match is pre-checked.
disco_matches disco_rows_of(std::vector<disco_track> && tracks);

//! match_disco_tracks, then disco_rows_of: by tags and file name alone.
disco_matches find_disco_matches(metadb_handle_list_cref tracks);

//! Match discographies: by tags and file name, then by sound for the tracks
//! that leaves unsure (disco_sound.cpp), in the background; shows the window
//! or says nothing matched.
void match_discographies(metadb_handle_list_cref tracks);

//! The recording a matched row offers.
const tangotagger::recording & row_recording(const disco_row & row);
const std::string & row_orchestra(const disco_row & row);

//! The fields writing would change on the row's file with the current
//! options, as they would be written.
std::vector<tangotagger::tag_value> row_changes(const disco_row & row);

//! "confident", "likely", "possible", "no match".
pfc::string8 disco_match_label(const disco_row & row);

//! The recording, what it matched on, and each field before and after.
pfc::string8 disco_preview_text(const disco_row & row, const char * newline);

//! "40 of 52 tracks matched, 31 of them confidently. ..."
pfc::string8 disco_status_text(const disco_matches & matches);

void set_disco_row_checked(std::vector<disco_row> & rows, std::size_t index, bool checked);
//! Checks the best row of every group that has none checked.
void check_all_disco_rows(std::vector<disco_row> & rows);
std::size_t count_checked(const std::vector<disco_row> & rows);

//! Hands the checked rows' changes to foobar2000's tag writer.
void write_checked_disco_tags(const std::vector<disco_row> & rows);
