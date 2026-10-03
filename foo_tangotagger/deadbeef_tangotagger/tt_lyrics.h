#ifndef TT_LYRICS_H
#define TT_LYRICS_H

// Find lyrics...: which built-in song each track's title names, what the
// results window shows of it, and what writing it puts in the file.
//
// A port of foo_tangotagger/lyrics_rows.cpp and lyrics_panel_content.cpp
// onto std::string and track_meta, so the DeaDBeeF plugin decides everything
// the foobar2000 component does - the rows, which start checked, the preview,
// the text that reaches the file - in the same words. Keep the two in step.
//
// One row per song a track matched, grouped under the track; at most one row
// of a group checked. A track nothing matched gets one row too, so the list
// accounts for the whole selection; that row has no song and cannot be
// checked.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lyrics_db.h"
#include "text_links.h"
#include "title_match.h"
#include "tt_fields.h"
#include "tt_review.h"

namespace tt
{

enum class existing_lyrics
{
	none,               //!< no lyrics in the file
	same,               //!< the file already has exactly what would be written
	same_without_links, //!< these lyrics, without the translation links writing would add
	same_with_links,    //!< these lyrics, with translation links writing would leave out
	different           //!< the file has other lyrics, which writing would replace
};

//! What a file's lyrics are, compared with a song's.
enum class file_lyrics
{
	none,       //!< the file has no lyrics
	text,       //!< the song's text
	linked,     //!< the song's text and its translation links
	other       //!< anything else
};

struct lyrics_row
{
	std::size_t group = 0;       //!< the track: its index in the selection
	int version = 1;             //!< 1-based position among the track's candidates
	int versions = 1;            //!< how many candidates the track has
	std::string title;           //!< TITLE, or the file name without one
	std::string artist;
	std::string field;           //!< the tag the lyrics go into for this file
	tangotagger::match_candidate match;
	//! Credits on the track that name this song's composer or lyricist.
	int credit_score = 0;
	file_lyrics in_file = file_lyrics::none;
	//! False for one track of a multi-track file, which DeaDBeeF cannot
	//! write tags to: listed, never checked.
	bool writable = true;
	bool checked = false;

	bool matched() const { return match.kind != tangotagger::match_kind::none; }
	bool checkable() const { return matched() && writable; }
};

struct lyrics_matches
{
	std::vector<lyrics_row> rows;
	std::size_t tracks_examined = 0;
	std::size_t tracks_matched = 0;
};

//! Every track against the embedded songs, as foo_tangotagger's
//! find_lyrics_matches: tracks in selection order, each with its candidates
//! best first, the tracks nothing matched after them. Pre-checked: an exact
//! match onto a file without lyrics, where the track has one candidate - or
//! several, and its COMPOSER/LYRICIST tags point at exactly one.
lyrics_matches find_lyrics_matches(const std::vector<track_meta> & tracks);

//! The embedded song a row offers. Matched rows only.
const tangotagger::song & row_song(const lyrics_row & row);

//! The file's lyrics against what writing would put there, with or without
//! the links to translations.
existing_lyrics existing(const lyrics_row & row, bool links);

//! The lyrics with LF line endings, with or without the links.
std::string lyrics_text(const tangotagger::song & s, bool links);
//! The lyrics as they go into the tag: CRLF line endings, as foo_tangotagger
//! writes them, so a file reads the same whichever player tagged it.
std::string lyrics_tag_text(const tangotagger::song & s, bool links);

//! The preview: song title, credits, how it matched, the lyrics.
std::string lyrics_preview_text(const lyrics_row & row, bool links);
//! "Music: X · Lyrics: Y", either half left out when unknown.
std::string song_credits_text(const tangotagger::song & s);
//! "exact", "similar", "no match".
std::string match_label(const lyrics_row & row);
const char * existing_label(existing_lyrics e);
//! "%LYRICS%, %UNSYNCED LYRICS%" - the fields the rows will write.
std::string lyrics_fields_summary(const std::vector<lyrics_row> & rows);
//! "12 of 40 tracks matched; 3 have several songs to choose from. ..."
std::string lyrics_status_text(const lyrics_matches & matches);

//! Checking one row unchecks the rest of its group; a row that cannot be
//! checked stays unchecked.
void set_row_checked(std::vector<lyrics_row> & rows, std::size_t index, bool checked);
//! Every track gets a song: the one already checked, or else its first.
void check_all(std::vector<lyrics_row> & rows);
std::size_t count_checked(const std::vector<lyrics_row> & rows);

//! The checked rows as the fields to write.
std::vector<track_write> lyrics_writes(const std::vector<lyrics_row> & rows, bool links);

//! The Find lyrics... results window.
class lyrics_review : public review
{
public:
	using writer = std::function<void(std::vector<track_write>)>;
	using saver = std::function<void(bool links)>;

	//! `write` takes the checked rows when the user writes them; `save`
	//! keeps the links option when it changes. Either may hold on to the
	//! tracks, which lets go of them when the review is gone.
	lyrics_review(lyrics_matches matches, bool links, writer write, saver save);

	std::string window_title() const override { return "Tango Tagger - Lyrics"; }
	const std::vector<review_column> & columns() const override { return m_columns; }
	std::size_t size() const override { return m_matches.rows.size(); }
	std::string cell(std::size_t row, std::size_t column) const override;
	bool checkable(std::size_t row) const override { return m_matches.rows[row].checkable(); }
	bool checked(std::size_t row) const override { return m_matches.rows[row].checked; }
	void set_checked(std::size_t row, bool checked) override { set_row_checked(m_matches.rows, row, checked); }
	void check_all() override { tt::check_all(m_matches.rows); }
	void check_none() override;
	std::size_t count_checked() const override { return tt::count_checked(m_matches.rows); }
	std::size_t block(std::size_t row) const override { return m_blocks[row]; }
	std::string preview(std::size_t row) const override;
	std::string status() const override { return lyrics_status_text(m_matches); }
	std::vector<review_option> options() const override;
	void set_option(std::size_t index, int value) override;
	void commit() override;

	const lyrics_matches & matches() const { return m_matches; }
	bool links() const { return m_links; }

private:
	lyrics_matches m_matches;
	bool m_links;
	writer m_write;
	saver m_save;
	bool m_committed = false;
	std::vector<review_column> m_columns;
	std::vector<std::size_t> m_blocks;
};

// --- the lyrics panel --------------------------------------------------------

enum class panel_style
{
	heading,    //!< the song's or the track's title
	credits,    //!< composer and lyricist, or the artist
	text,       //!< a line of the lyrics; empty between verses
	note        //!< where the lyrics come from, or why there are none
};

struct panel_line
{
	panel_style style = panel_style::text;
	std::string text;                               //!< UTF-8, one line
	std::vector<tangotagger::text_link> links;      //!< the web addresses in it
};

struct panel_content
{
	std::vector<panel_line> lines;
	//! Title, credits and lyrics as plain text, for the clipboard; empty when
	//! there are no lyrics.
	std::string copy_text;
};

//! What the lyrics panel shows for a track: the file's own lyrics, or else
//! the built-in song its title matches, marked as not in the file. Null for
//! the "nothing selected" note.
panel_content panel_content_for(const track_meta * track);

}   // namespace tt

#endif // TT_LYRICS_H
