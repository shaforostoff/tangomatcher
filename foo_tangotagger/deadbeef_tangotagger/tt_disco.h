#ifndef TT_DISCO_H
#define TT_DISCO_H

// Match discographies...: which recording each track is, what the results
// window shows of it, and what writing it changes.
//
// A port of foo_tangotagger/disco_rows.cpp onto std::string and track_meta.
// Keep the two in step. The matching itself - core/disco_match.h, and by
// sound core/disco_fingerprint.h - is shared, not ported.
//
// One row per recording a track could be, grouped under the track, at most
// one of a group checked. A track nothing matched gets one row, which cannot
// be checked.

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "disco_match.h"
#include "disco_tags.h"
#include "tt_fields.h"
#include "tt_review.h"

namespace tt
{

struct disco_row
{
	std::size_t group = 0;     //!< the track: its index in the selection
	int version = 1;           //!< 1-based position among the track's candidates
	int versions = 1;
	bool confident = false;    //!< the track's best candidate, and sure of it
	std::string path;
	std::string title;         //!< TITLE, or the file name without one
	std::string artist;
	tangotagger::recording_match match;
	//! The track's values of every field a scheme can write, by
	//! foobar2000-style name.
	std::map<std::string, std::vector<std::string>> current;
	//! False for one track of a multi-track file, which DeaDBeeF cannot
	//! write tags to: listed, never checked.
	bool writable = true;
	bool checked = false;

	bool matched() const { return match.recording >= 0; }
	bool checkable() const { return matched() && writable; }
};

struct disco_matches
{
	std::vector<disco_row> rows;
	std::size_t tracks_examined = 0;
	std::size_t tracks_matched = 0;
	std::size_t tracks_confident = 0;
	std::size_t tracks_by_sound = 0;     //!< confident only because of their sound
};

//! A track and what matching it found, before it becomes rows.
struct disco_track
{
	disco_row base;                      //!< the track's own fields; no match yet
	tangotagger::track_match match;
	bool confident_by_tags = false;
};

//! Every track against the embedded discographies by its tags and file name.
std::vector<disco_track> match_disco_tracks(const std::vector<track_meta> & tracks);

//! The rows: tracks in selection order, each with its candidates best first;
//! the tracks nothing matched after them. A confident match is pre-checked.
disco_matches disco_rows_of(std::vector<disco_track> && tracks);

const tangotagger::recording & row_recording(const disco_row & row);
const std::string & row_orchestra(const disco_row & row);

//! The fields writing would change on the row's file with these options, by
//! foobar2000-style name, as they would be written.
std::vector<tangotagger::tag_value> row_changes(const disco_row & row, const tangotagger::tag_options & o);

//! "by sound", "sound?", "confident", "likely", "possible", "no match".
std::string disco_match_label(const disco_row & row);
//! The recording, what it matched on, and each field before and after.
std::string disco_preview_text(const disco_row & row, const tangotagger::tag_options & o);
//! "40 of 52 tracks matched, 31 of them confidently. ..."
std::string disco_status_text(const disco_matches & matches);
//! "None of the 12 selected tracks was found in the discographies. ..."
std::string disco_nothing_text(std::size_t tracks_examined);

void set_disco_row_checked(std::vector<disco_row> & rows, std::size_t index, bool checked);
//! Checks the best row of every group that has none checked.
void check_all_disco_rows(std::vector<disco_row> & rows);
std::size_t count_checked(const std::vector<disco_row> & rows);

//! The checked rows' changes as the fields to write, in DeaDBeeF's names.
std::vector<track_write> disco_writes(const std::vector<disco_row> & rows, const tangotagger::tag_options & o);

//! The Match discographies... results window.
class disco_review : public review
{
public:
	using writer = std::function<void(std::vector<track_write>)>;
	using saver = std::function<void(const tangotagger::tag_options &)>;

	disco_review(disco_matches matches, const tangotagger::tag_options & options, writer write, saver save);

	std::string window_title() const override { return "Tango Tagger - Match Discographies"; }
	const std::vector<review_column> & columns() const override { return m_columns; }
	std::size_t size() const override { return m_matches.rows.size(); }
	std::string cell(std::size_t row, std::size_t column) const override;
	bool checkable(std::size_t row) const override { return m_matches.rows[row].checkable(); }
	bool checked(std::size_t row) const override { return m_matches.rows[row].checked; }
	void set_checked(std::size_t row, bool checked) override { set_disco_row_checked(m_matches.rows, row, checked); }
	void check_all() override { check_all_disco_rows(m_matches.rows); }
	void check_none() override;
	std::size_t count_checked() const override { return tt::count_checked(m_matches.rows); }
	std::size_t block(std::size_t row) const override { return m_blocks[row]; }
	std::string preview(std::size_t row) const override;
	std::string status() const override { return disco_status_text(m_matches); }
	std::vector<review_option> options() const override;
	void set_option(std::size_t index, int value) override;
	void commit() override;

	const disco_matches & matches() const { return m_matches; }
	const tangotagger::tag_options & write_options() const { return m_options; }

private:
	disco_matches m_matches;
	tangotagger::tag_options m_options;
	writer m_write;
	saver m_save;
	bool m_committed = false;
	std::vector<review_column> m_columns;
	std::vector<std::size_t> m_blocks;
};

}   // namespace tt

#endif // TT_DISCO_H
