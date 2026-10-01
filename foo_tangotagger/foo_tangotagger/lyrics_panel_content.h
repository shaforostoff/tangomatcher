#pragma once

// What the lyrics panel shows for a track. Both platforms' panels are built
// on this; only the drawing is written twice.
//
// The file's own lyrics come first. A track without any gets the built-in
// song its title matches, marked as not being in the file; the matching is
// the results window's, so the panel and Find lyrics... agree on the song.

#include <SDK/foobar2000.h>

#include <string>
#include <vector>

#include "text_links.h"

enum class lyrics_panel_style
{
	heading,    //!< the song's or the track's title
	credits,    //!< composer and lyricist, or the artist
	text,       //!< a line of the lyrics; empty between verses
	note        //!< where the lyrics come from, or why there are none
};

struct lyrics_panel_line
{
	lyrics_panel_style style = lyrics_panel_style::text;
	std::string text;                               //!< UTF-8, one line
	std::vector<tangotagger::text_link> links;      //!< the web addresses in it
};

struct lyrics_panel_content
{
	std::vector<lyrics_panel_line> lines;
	//! Title, credits and lyrics as plain text, for the clipboard; empty when
	//! there are no lyrics.
	std::string copy_text;
};

//! The panel's content for a track; a null track gives the "nothing
//! selected" note.
lyrics_panel_content lyrics_panel_content_for(const metadb_handle_ptr & track);
