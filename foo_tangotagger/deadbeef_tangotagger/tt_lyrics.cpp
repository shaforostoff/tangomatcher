#include "tt_lyrics.h"

#include <algorithm>

using tangotagger::match_kind;

namespace tt
{

namespace
{
	//! More near misses than this for one title is noise, not a choice.
	const std::size_t max_similar_candidates = 5;

	//! The tags that credit the writers of a track.
	const char * const credit_fields[] = { "COMPOSER", "LYRICIST", "WRITER", "AUTHOR" };

	//! Line endings dropped and surrounding whitespace trimmed, so lyrics
	//! written on one platform compare equal to the same text read back on
	//! another.
	std::string comparable(const std::string & text)
	{
		std::string out;
		for (char c : text)
			if (c != '\r') out += c;
		const std::size_t b = out.find_first_not_of(" \t\n");
		if (b == std::string::npos) return std::string();
		return out.substr(b, out.find_last_not_of(" \t\n") - b + 1);
	}

	file_lyrics existing_in(const track_meta & t, const tangotagger::song & s)
	{
		const std::string text = comparable(s.text);
		const std::string linked = comparable(lyrics_text(s, true));
		file_lyrics result = file_lyrics::none;
		for (const std::string & field : lyrics_known_fields)
		{
			for (const std::string & value : t.values(field))
			{
				const std::string theirs = comparable(value);
				if (theirs.empty()) continue;
				if (theirs == text) return file_lyrics::text;
				if (theirs == linked) return file_lyrics::linked;
				result = file_lyrics::other;
			}
		}
		return result;
	}

	std::string credits_of(const track_meta & t)
	{
		std::string out;
		for (const char * field : credit_fields)
			for (const std::string & v : t.values(field)) out += v + ' ';
		return out;
	}

	std::string with_newlines(const std::string & text, const char * newline)
	{
		std::string out;
		for (char c : text)
		{
			if (c == '\n') out += newline;
			else out += c;
		}
		return out;
	}

	//! The one row of a group to pre-check, or -1 for none.
	int preferred_row(const std::vector<lyrics_row> & group)
	{
		if (group.empty() || group.front().match.kind != match_kind::exact) return -1;
		int pick = -1;
		if (group.size() == 1) pick = 0;
		else
		{
			// Several songs by this title: take the one the track's credits
			// name, if they name exactly one.
			int best = 0, count = 0;
			for (std::size_t i = 0; i < group.size(); i++)
			{
				if (group[i].credit_score > best) { best = group[i].credit_score; pick = static_cast<int>(i); count = 1; }
				else if (group[i].credit_score == best && best > 0) count++;
			}
			if (count != 1) pick = -1;
		}
		// A file that already has lyrics keeps them unless the user says
		// otherwise - including when they are these lyrics, which there is no
		// point in writing again.
		for (const lyrics_row & r : group)
			if (r.in_file != file_lyrics::none || !r.writable) return -1;
		return pick;
	}

	//! The matcher, built once: the panel matches the selected track on
	//! every selection change.
	const tangotagger::matcher & song_matcher()
	{
		static const tangotagger::matcher m(tangotagger::embedded_songs());
		return m;
	}

	enum lyrics_column { col_check, col_title, col_artist, col_song, col_match, col_existing };
}

lyrics_matches find_lyrics_matches(const std::vector<track_meta> & tracks)
{
	lyrics_matches result;
	const std::vector<tangotagger::song> & songs = tangotagger::embedded_songs();
	const tangotagger::matcher & m = song_matcher();

	// Selection order; the tracks nothing matched after all the others, where
	// they do not break up the list of choices.
	std::vector<lyrics_row> unmatched;
	for (std::size_t i = 0; i < tracks.size(); i++)
	{
		const track_meta & t = tracks[i];
		result.tracks_examined++;

		lyrics_row base;
		base.group = i;
		base.field = lyrics_field(t.path);
		base.title = t.first("title");
		if (base.title.empty()) base.title = file_stem(t.path);
		base.artist = t.first("artist");
		base.writable = !t.subtrack;
		const std::string credits = credits_of(t);

		const tangotagger::title_match match = m.find(base.title);
		if (match.kind == match_kind::none)
		{
			unmatched.push_back(std::move(base));
			continue;
		}

		std::vector<lyrics_row> group;
		for (const tangotagger::match_candidate & c : match.candidates)
		{
			if (c.kind == match_kind::similar && group.size() >= max_similar_candidates) break;
			lyrics_row row = base;
			row.match = c;
			row.credit_score = tangotagger::credit_overlap(credits, songs[c.song]);
			row.in_file = existing_in(t, songs[c.song]);
			group.push_back(std::move(row));
		}

		// Best supported by the credits first, so a tagged track's own song
		// heads its group; stable, so the matcher's order decides the rest.
		std::stable_sort(group.begin(), group.end(), [](const lyrics_row & a, const lyrics_row & b)
		{
			return a.credit_score > b.credit_score;
		});
		for (std::size_t v = 0; v < group.size(); v++)
		{
			group[v].version = static_cast<int>(v + 1);
			group[v].versions = static_cast<int>(group.size());
		}
		const int pick = preferred_row(group);
		if (pick >= 0) group[pick].checked = true;

		for (lyrics_row & r : group) result.rows.push_back(std::move(r));
		result.tracks_matched++;
	}
	for (lyrics_row & r : unmatched) result.rows.push_back(std::move(r));
	return result;
}

const tangotagger::song & row_song(const lyrics_row & row)
{
	return tangotagger::embedded_songs()[row.match.song];
}

existing_lyrics existing(const lyrics_row & row, bool links)
{
	switch (row.in_file)
	{
	case file_lyrics::none:  return existing_lyrics::none;
	case file_lyrics::other: return existing_lyrics::different;
	case file_lyrics::text:
		// A song without translations has no links to add.
		return links && !row_song(row).translations.empty() ? existing_lyrics::same_without_links
		                                                    : existing_lyrics::same;
	default:
		return links ? existing_lyrics::same : existing_lyrics::same_with_links;
	}
}

std::string lyrics_text(const tangotagger::song & s, bool links)
{
	const std::string text = links ? tangotagger::translation_links_text(s) : std::string();
	return text.empty() ? s.text : s.text + "\n\n" + text;
}

std::string lyrics_tag_text(const tangotagger::song & s, bool links)
{
	return with_newlines(lyrics_text(s, links), "\r\n");
}

std::string lyrics_preview_text(const lyrics_row & row, bool links)
{
	std::string out;
	if (!row.matched())
	{
		out += "No lyrics match \"" + row.title + "\".\n\n"
		       "Titles are compared whole, ignoring accents, case, punctuation and bracketed notes; "
		       "a title that is only part of a song's title, or the other way round, does not match.";
		return out;
	}
	const tangotagger::song & s = row_song(row);

	if (!row.writable) out += multitrack_note;
	out += s.name + "\n";
	const std::string credits = song_credits_text(s);
	if (!credits.empty()) out += credits + "\n";
	if (row.versions > 1)
	{
		out += "One of " + std::to_string(row.versions) + " songs this title matches";
		if (row.credit_score > 0) out += "; the track's composer/lyricist tags name this one";
		out += ".\n";
	}
	if (row.match.kind == match_kind::similar)
		out += "Similar, not identical: the title reads \"" + row.match.track_key + "\", the song \""
		     + row.match.song_key + "\".\n";
	switch (existing(row, links))
	{
	case existing_lyrics::different:
		out += "The file already has other lyrics; writing replaces them.\n";
		break;
	case existing_lyrics::same:
		out += "The file already has these lyrics.\n";
		break;
	case existing_lyrics::same_without_links:
		out += "The file already has these lyrics, without the links to translations; writing adds them.\n";
		break;
	case existing_lyrics::same_with_links:
		out += "The file already has these lyrics, with links to translations; writing leaves the links out.\n";
		break;
	default:
		break;
	}
	out += "\n" + lyrics_text(s, links);
	return out;
}

std::string song_credits_text(const tangotagger::song & s)
{
	std::string out;
	if (!s.composer.empty()) out += "Music: " + s.composer;
	if (!s.composer.empty() && !s.author.empty()) out += "  \xC2\xB7  ";   // middle dot
	if (!s.author.empty()) out += "Lyrics: " + s.author;
	return out;
}

std::string match_label(const lyrics_row & row)
{
	if (!row.matched()) return "no match";
	return row.match.kind == match_kind::exact ? "exact" : "similar";
}

const char * existing_label(existing_lyrics e)
{
	switch (e)
	{
	case existing_lyrics::same:               return "same";
	case existing_lyrics::same_without_links: return "same, no links";
	case existing_lyrics::same_with_links:    return "same, with links";
	case existing_lyrics::different:          return "different";
	default:                                  return "none";
	}
}

std::string lyrics_fields_summary(const std::vector<lyrics_row> & rows)
{
	std::vector<std::string> fields;
	for (const lyrics_row & r : rows)
		if (r.matched() && std::find(fields.begin(), fields.end(), r.field) == fields.end())
			fields.push_back(r.field);
	std::string out;
	for (std::size_t i = 0; i < fields.size(); i++)
		out += (i ? ", %" : "%") + fields[i] + "%";
	return out;
}

std::string lyrics_status_text(const lyrics_matches & matches)
{
	std::size_t several = 0;
	for (const lyrics_row & r : matches.rows)
		if (r.version == 2) several++;

	std::string out = std::to_string(matches.tracks_matched) + " of " + std::to_string(matches.tracks_examined)
	                + (matches.tracks_examined == 1 ? " track" : " tracks") + " matched";
	if (several == 1) out += "; 1 has several songs to choose from";
	else if (several > 1) out += "; " + std::to_string(several) + " have several songs to choose from";
	out += ". Checked tracks get the lyrics in " + lyrics_fields_summary(matches.rows) + ".";
	return out;
}

void set_row_checked(std::vector<lyrics_row> & rows, std::size_t index, bool checked)
{
	if (index >= rows.size() || !rows[index].checkable()) return;
	if (checked)
		for (lyrics_row & r : rows)
			if (r.group == rows[index].group) r.checked = false;
	rows[index].checked = checked;
}

void check_all(std::vector<lyrics_row> & rows)
{
	for (std::size_t i = 0; i < rows.size(); i++)
	{
		if (rows[i].version != 1) continue;
		bool any = false;
		for (std::size_t j = i; j < rows.size() && rows[j].group == rows[i].group; j++)
			any = any || rows[j].checked;
		if (!any) set_row_checked(rows, i, true);
	}
}

std::size_t count_checked(const std::vector<lyrics_row> & rows)
{
	return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(),
	                                              [](const lyrics_row & r) { return r.checked; }));
}

std::vector<track_write> lyrics_writes(const std::vector<lyrics_row> & rows, bool links)
{
	std::vector<track_write> out;
	for (const lyrics_row & r : rows)
	{
		if (!r.checked) continue;
		track_write w;
		w.track = r.group;
		field_write f;
		f.key = r.field;
		f.values.push_back(lyrics_tag_text(row_song(r), links));
		w.fields.push_back(std::move(f));
		out.push_back(std::move(w));
	}
	return out;
}

// --- the results window ------------------------------------------------------

lyrics_review::lyrics_review(lyrics_matches matches, bool links, writer write, saver save)
	: m_matches(std::move(matches)), m_links(links), m_write(std::move(write)), m_save(std::move(save))
{
	m_columns = {
		{ "", review_column::check, 0 },
		{ "Track title", review_column::fill, 220 },
		{ "Artist", review_column::fill, 140 },
		{ "Lyrics of", review_column::fill, 200 },
		{ "Match", review_column::fit, 0 },
		{ "Existing lyrics", review_column::fit, 0 },
	};
	const std::vector<lyrics_row> & rows = m_matches.rows;
	for (std::size_t i = 0; i < rows.size(); i++)
		m_blocks.push_back(i == 0 ? 0 : m_blocks.back() + (rows[i].group != rows[i - 1].group ? 1 : 0));
}

std::string lyrics_review::cell(std::size_t row, std::size_t column) const
{
	const lyrics_row & r = m_matches.rows[row];
	switch (column)
	{
	case col_check:
		return r.versions > 1 ? std::to_string(r.version) : std::string();
	case col_title:
		// A track's further candidates sit under its first, without repeating it.
		return r.version > 1 ? std::string() : r.title;
	case col_artist:
		return r.version > 1 ? std::string() : r.artist;
	case col_song:
		return r.matched() ? row_song(r).name : std::string();
	case col_match:
		return match_label(r);
	case col_existing:
		return r.matched() ? existing_label(existing(r, m_links)) : std::string();
	default:
		return std::string();
	}
}

void lyrics_review::check_none()
{
	for (lyrics_row & r : m_matches.rows) r.checked = false;
}

std::string lyrics_review::preview(std::size_t row) const
{
	return row < m_matches.rows.size() ? lyrics_preview_text(m_matches.rows[row], m_links) : std::string();
}

std::vector<review_option> lyrics_review::options() const
{
	review_option links;
	links.kind = review_option::checkbox;
	links.label = "Add links to translations";
	links.value = m_links ? 1 : 0;
	return { links };
}

void lyrics_review::set_option(std::size_t index, int value)
{
	if (index != 0) return;
	m_links = value != 0;
	if (m_save) m_save(m_links);
}

void lyrics_review::commit()
{
	if (m_committed) return;
	m_committed = true;
	std::vector<track_write> writes = lyrics_writes(m_matches.rows, m_links);
	if (!writes.empty() && m_write) m_write(std::move(writes));
}

// --- the lyrics panel --------------------------------------------------------

namespace
{
	//! LF line endings, surrounding blank lines and whitespace trimmed.
	std::string normalised(const std::string & text)
	{
		std::string out;
		for (std::size_t i = 0; i < text.size(); i++)
		{
			if (text[i] == '\r')
			{
				out += '\n';
				if (i + 1 < text.size() && text[i + 1] == '\n') i++;
			}
			else out += text[i];
		}
		const std::size_t b = out.find_first_not_of(" \t\n");
		if (b == std::string::npos) return std::string();
		return out.substr(b, out.find_last_not_of(" \t\n") - b + 1);
	}

	//! The lyrics the file carries, under whichever name it has them.
	std::string lyrics_in_file(const track_meta & t)
	{
		for (const std::string & field : lyrics_known_fields)
		{
			std::string text;
			for (const std::string & v : t.values(field))
			{
				const std::string value = normalised(v);
				if (value.empty()) continue;
				if (!text.empty()) text += "\n\n";
				text += value;
			}
			if (!text.empty()) return text;
		}
		return std::string();
	}

	void add(panel_content & out, panel_style style, const std::string & text)
	{
		panel_line line;
		line.style = style;
		line.text = text;
		line.links = tangotagger::find_links(text);
		out.lines.push_back(std::move(line));
	}

	void add_lines(panel_content & out, panel_style style, const std::string & text)
	{
		std::size_t start = 0;
		for (;;)
		{
			const std::size_t nl = text.find('\n', start);
			add(out, style, text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
			if (nl == std::string::npos) break;
			start = nl + 1;
		}
	}
}

panel_content panel_content_for(const track_meta * track)
{
	panel_content out;
	if (track == nullptr)
	{
		add(out, panel_style::note, "No track selected.");
		return out;
	}

	const lyrics_matches matches = find_lyrics_matches(std::vector<track_meta>(1, *track));
	// One row at least: a track nothing matched gets an unmatched row.
	const lyrics_row & best = matches.rows.front();

	std::string heading = best.title;
	std::string credits = best.artist;
	std::string text = lyrics_in_file(*track);
	bool text_has_links = false;
	std::vector<std::string> notes;
	const tangotagger::song * song = nullptr;

	if (!text.empty())
	{
		// The file's own lyrics. If they are a built-in song's, word for word
		// - as Find lyrics... writes them, with or without the links to
		// translations - that song's credits and source go with them.
		for (const lyrics_row & r : matches.rows)
		{
			if (!r.matched() || (r.in_file != file_lyrics::text && r.in_file != file_lyrics::linked)) continue;
			song = &row_song(r);
			text_has_links = r.in_file == file_lyrics::linked;
			break;
		}
		notes.push_back("From the file's tags.");
	}
	else if (best.matched())
	{
		song = &row_song(best);
		text = song->text;
		if (best.match.kind == match_kind::similar)
			notes.push_back("Built-in lyrics of a song with a similar title; not in the file.");
		else
			notes.push_back("Built-in lyrics; not in the file. Tango Tagger > Find lyrics... writes them.");
		if (best.versions > 1)
		{
			std::string several = "One of " + std::to_string(best.versions) + " songs with this title";
			if (best.credit_score > 0) several += "; the track's composer/lyricist tags name this one";
			notes.push_back(several + ".");
		}
	}
	else
	{
		notes.push_back("No lyrics in the file, and none of the built-in songs has this title.");
	}

	if (song != nullptr)
	{
		heading = song->name;
		credits = song_credits_text(*song);
		if (!song->link.empty()) notes.push_back("Source: " + song->link);
		// The translations, unless the file's lyrics end with them already.
		if (!text_has_links)
		{
			const std::string links = tangotagger::translation_links_text(*song);
			std::size_t start = 0;
			while (start < links.size())
			{
				std::size_t nl = links.find('\n', start);
				if (nl == std::string::npos) nl = links.size();
				notes.push_back(links.substr(start, nl - start));
				start = nl + 1;
			}
		}
	}

	add(out, panel_style::heading, heading);
	if (!credits.empty()) add(out, panel_style::credits, credits);
	if (!text.empty())
	{
		add(out, panel_style::text, std::string());
		add_lines(out, panel_style::text, text);

		out.copy_text = heading + "\n";
		if (!credits.empty()) out.copy_text += credits + "\n";
		out.copy_text += "\n" + text + "\n";
	}
	add(out, panel_style::text, std::string());
	for (const std::string & n : notes) add(out, panel_style::note, n);
	return out;
}

}   // namespace tt
