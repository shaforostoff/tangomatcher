#pragma once

// What the results window shows and what its Write button writes. Both
// platforms' windows are built on this, so the matching, the default check
// state and the text that reaches the file are decided once.
//
// One row per song a track matched. Most tracks match one song; a title
// several songs share ("Sin amor" is three) gives a row for each, grouped
// under the track, and at most one row of a group can be checked. A track
// nothing matched gets one row too, so the list accounts for the whole
// selection; that row has no song and cannot be checked.

#include <SDK/foobar2000.h>

#include <string>
#include <vector>

#include "title_match.h"

enum class existing_lyrics
{
	none,       //!< no lyrics in the file
	same,       //!< the file already has exactly these lyrics
	different   //!< the file has other lyrics, which writing would replace
};

struct lyrics_row
{
	metadb_handle_ptr track;
	std::size_t group = 0;       //!< rows of one track share this
	int version = 1;             //!< 1-based position among the track's candidates
	int versions = 1;            //!< how many candidates the track has
	pfc::string8 title;          //!< TITLE, or the file name without one
	pfc::string8 artist;
	pfc::string8 field;          //!< the tag the lyrics go into for this file
	tangotagger::match_candidate match;
	//! Credits on the track that name this song's composer or lyricist.
	int credit_score = 0;
	existing_lyrics existing = existing_lyrics::none;
	bool checked = false;

	//! False for a track nothing matched: no song, never checked.
	bool matched() const { return match.kind != tangotagger::match_kind::none; }
};

struct lyrics_matches
{
	std::vector<lyrics_row> rows;
	std::size_t tracks_examined = 0;
	std::size_t tracks_matched = 0;
};

//! Matches every track against the embedded songs. Tracks come in selection
//! order, each with its candidates best first; the tracks nothing matched
//! follow, one unmatched row each, also in selection order.
//!
//! Pre-checked: an exact match onto a file without lyrics, where the track
//! has one candidate - or several, and its COMPOSER/LYRICIST tags point at
//! exactly one of them. A near miss, a file that already has lyrics, or a
//! choice the tags cannot settle is listed but left to the user.
lyrics_matches find_lyrics_matches(metadb_handle_list_cref tracks);

//! The lyrics as they go into the tag: the text alone, CRLF line endings.
pfc::string8 lyrics_tag_text(const tangotagger::song & s);

//! The preview for a row: song title, credits, how it matched, the lyrics.
//! `newline` is "\r\n" for a Win32 edit control and "\n" for Cocoa.
pfc::string8 lyrics_preview_text(const lyrics_row & row, const char * newline);

//! The embedded song a row offers. Matched rows only.
const tangotagger::song & row_song(const lyrics_row & row);

//! "exact", "exact 2/3", "similar", "no match".
pfc::string8 match_label(const lyrics_row & row);
const char * existing_label(existing_lyrics e);

//! "%LYRICS%, %UNSYNCED LYRICS%" - the fields the rows will write.
pfc::string8 lyrics_fields_summary(const std::vector<lyrics_row> & rows);

//! "12 of 40 tracks matched; 3 have several songs to choose from. ..."
pfc::string8 lyrics_status_text(const lyrics_matches & matches);

//! Checks or unchecks a row. Checking one unchecks the rest of its group;
//! an unmatched row stays unchecked.
void set_row_checked(std::vector<lyrics_row> & rows, std::size_t index, bool checked);

std::size_t count_checked(const std::vector<lyrics_row> & rows);

//! Hands the checked rows to foobar2000's tag writer.
void write_checked_lyrics(const std::vector<lyrics_row> & rows);
