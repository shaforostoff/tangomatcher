#ifndef TT_REVIEW_H
#define TT_REVIEW_H

// What a results window shows and acts on, with no window system in it.
//
// Find lyrics... and Match discographies... both end in the same kind of
// window: one row per candidate, grouped under the track it is a candidate
// for, a checkbox on each row that can be written and at most one checked per
// track; a preview of the highlighted row below; a line saying how the
// matching went; a few options; and a button that writes the checked rows.
// foo_tangotagger has two windows for them because foobar2000 hands it two
// dialog templates. Here they are one `review`, so each toolkit has one
// window to draw - gtk/ui_gtk.cpp and cocoa/ui_cocoa.mm - and what it shows
// is decided once, in tt_lyrics.cpp and tt_disco.cpp.
//
// A review is owned by the window showing it, through a shared_ptr, and is
// not thread-safe: everything here is called on the window system's thread.

#include <cstddef>
#include <string>
#include <vector>

namespace tt
{

struct review_column
{
	enum kind_t
	{
		check,   //!< the checkbox, with the candidate's number beside it when there are several
		fill,    //!< takes a share of the width the others leave
		fit      //!< as wide as its widest cell and its heading, and no wider
	};

	std::string heading;
	kind_t kind = fill;
	//! A starting width, in points, for a fill column.
	int width = 120;
};

//! A setting shown below the list. Changing one can change what every row
//! would write, so the window redraws the cells and the preview after it.
struct review_option
{
	enum kind_t { checkbox, choice };

	kind_t kind = checkbox;
	std::string label;
	std::vector<std::string> choices;   //!< for a choice
	int value = 0;                      //!< 0 or 1 for a checkbox; the index for a choice
};

class review
{
public:
	virtual ~review() {}

	virtual std::string window_title() const = 0;
	virtual const std::vector<review_column> & columns() const = 0;

	virtual std::size_t size() const = 0;
	//! The text of a cell. For the check column, the candidate's number when
	//! its track has several ("2"), else blank.
	virtual std::string cell(std::size_t row, std::size_t column) const = 0;
	//! False for a track nothing matched: no checkbox, drawn greyed.
	virtual bool checkable(std::size_t row) const = 0;
	virtual bool checked(std::size_t row) const = 0;
	//! Checking a row unchecks the rest of its track's.
	virtual void set_checked(std::size_t row, bool checked) = 0;
	//! Every track gets a candidate: the one already checked, or its best.
	virtual void check_all() = 0;
	virtual void check_none() = 0;
	virtual std::size_t count_checked() const = 0;
	//! Which track's block a row is in, counted from the top: rows of one
	//! track are shaded alike, the next track's in the other shade.
	virtual std::size_t block(std::size_t row) const = 0;

	//! The highlighted row's preview: what it is, how it matched, what
	//! writing it changes. LF line endings.
	virtual std::string preview(std::size_t row) const = 0;
	//! "12 of 40 tracks matched; ..." - said once, under the preview.
	virtual std::string status() const = 0;

	virtual std::vector<review_option> options() const = 0;
	//! Kept for next time, as foo_tangotagger keeps them.
	virtual void set_option(std::size_t index, int value) = 0;

	//! "Write 12 files", from count_checked().
	std::string commit_label() const
	{
		const std::size_t n = count_checked();
		return n == 1 ? "Write 1 file" : "Write " + std::to_string(n) + " files";
	}

	//! Writes the checked rows, in the background. Once only: the window
	//! closes.
	virtual void commit() = 0;
};

//! How the listening to tracks by sound is getting on, for a progress window
//! to poll. Thread-safe.
class scan_progress
{
public:
	struct snapshot
	{
		std::size_t done = 0;
		std::size_t total = 0;
		//! Over the whole scan, with the tracks in flight counted by how far
		//! through each one is.
		double fraction = 0;
		//! "a.flac, b.flac and 4 more": the tracks being read right now.
		std::string in_flight;
		bool finished = false;
	};

	virtual ~scan_progress() {}
	//! "Listening to 3 tracks the tags do not place..."
	virtual std::string title() const = 0;
	virtual snapshot read() const = 0;
	//! Stops listening. The results window still opens, with what the tags
	//! and the tracks listened to so far found.
	virtual void cancel() = 0;
};

}   // namespace tt

#endif // TT_REVIEW_H
