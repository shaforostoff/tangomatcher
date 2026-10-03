#include "tt_disco.h"

#include <algorithm>

using tangotagger::artist_scheme;
using tangotagger::tag_options;
using tangotagger::tag_value;

namespace tt
{

namespace
{
	//! Where a track may say who plays and sings on it, besides ARTIST and
	//! ALBUM ARTIST.
	const std::vector<std::string> performer_fields = {
		"CONDUCTOR", "PERFORMER", "ORCHESTRA", "ENSEMBLE", "BAND", "VOCALS", "VOCALIST", "SINGER", "CANTOR",
	};
	//! Where it may say when it was recorded: foobar2000's names, and
	//! DeaDBeeF's for the same frames.
	const std::vector<std::string> date_fields = {
		"year", "DATE", "ORIGINAL DATE", "ORIGINALDATE", "ORIGINAL YEAR", "ORIGINALYEAR",
		"ORIGINAL RELEASE DATE", "RECORDING DATE", "RECORDINGDATE",
		"original_release_time", "original_release_year", "RECORDING_TIME", "RECORDING_DATES",
	};
	const std::vector<std::string> comment_fields = { "COMMENT", "DESCRIPTION" };
	//! Every field a scheme writes.
	const char * const written_fields[] = { "TITLE", "ARTIST", "ALBUM ARTIST", "CANTOR", "DATE", "GENRE" };

	std::string joined_values(const std::vector<std::string> & values)
	{
		std::string out;
		for (const std::string & v : values) out += v + " ; ";
		return out;
	}

	const std::vector<std::string> & current_of(const disco_row & row, const char * field)
	{
		static const std::vector<std::string> none;
		const auto it = row.current.find(field);
		return it == row.current.end() ? none : it->second;
	}

	std::string first_of(const disco_row & row, const char * field)
	{
		const std::vector<std::string> & v = current_of(row, field);
		return v.empty() ? std::string() : v.front();
	}

	std::string joined(const std::vector<std::string> & values)
	{
		if (values.empty()) return "(none)";
		std::string out;
		for (const std::string & v : values) out += (out.empty() ? "" : "; ") + v;
		return out;
	}

	const tangotagger::recording_matcher & matcher()
	{
		static const tangotagger::recording_matcher m(tangotagger::embedded_discography());
		return m;
	}

	enum disco_column { col_check, col_title, col_artist, col_recording, col_orchestra, col_vocal, col_date, col_match };

	//! The field checkboxes, in the order the options list them.
	bool tag_options::* const field_members[] = {
		&tag_options::title,
		&tag_options::artist,
		&tag_options::album_artist,
		&tag_options::date,
		&tag_options::genre,
	};
	const char * const field_titles[] = { "Title", "Artist", "Album artist", "Date", "Genre" };
	const std::size_t field_count = sizeof field_members / sizeof field_members[0];
}

std::vector<disco_track> match_disco_tracks(const std::vector<track_meta> & tracks)
{
	std::vector<disco_track> result;
	const tangotagger::recording_matcher & m = matcher();

	for (std::size_t i = 0; i < tracks.size(); i++)
	{
		const track_meta & t = tracks[i];
		disco_row base;
		base.group = i;
		base.path = t.path;

		tangotagger::track_tags tags;
		tags.path = t.path;
		tags.title = t.first("title");
		tags.artist = joined_values(field_values(t, "ARTIST"));
		tags.album_artist = joined_values(field_values(t, "ALBUM ARTIST"));
		tags.performers = joined_values(t.values_of(performer_fields));
		tags.album = t.first("album");
		tags.dates = joined_values(t.values_of(date_fields));
		tags.comment = joined_values(t.values_of(comment_fields));
		tags.genre = joined_values(t.values("genre"));
		for (const char * f : written_fields)
		{
			std::vector<std::string> v = field_values(t, f);
			if (!v.empty()) base.current[f] = std::move(v);
		}
		base.artist = t.first("artist");
		base.title = tags.title.empty() ? file_stem(t.path) : tags.title;
		base.writable = !t.subtrack;

		disco_track d;
		d.base = std::move(base);
		d.match = m.find(tags);
		d.confident_by_tags = d.match.confident;
		result.push_back(std::move(d));
	}
	return result;
}

disco_matches disco_rows_of(std::vector<disco_track> && tracks)
{
	disco_matches result;
	std::vector<disco_row> unmatched;
	for (disco_track & t : tracks)
	{
		result.tracks_examined++;
		const tangotagger::track_match & found = t.match;
		if (found.candidates.empty())
		{
			unmatched.push_back(std::move(t.base));
			continue;
		}
		result.tracks_matched++;
		if (found.confident) result.tracks_confident++;
		if (found.confident && !t.confident_by_tags) result.tracks_by_sound++;
		for (std::size_t v = 0; v < found.candidates.size(); v++)
		{
			disco_row row = t.base;
			row.match = found.candidates[v];
			row.version = static_cast<int>(v + 1);
			row.versions = static_cast<int>(found.candidates.size());
			row.confident = v == 0 && found.confident;
			row.checked = row.confident && row.writable;
			result.rows.push_back(std::move(row));
		}
	}
	for (disco_row & r : unmatched) result.rows.push_back(std::move(r));
	return result;
}

const tangotagger::recording & row_recording(const disco_row & row)
{
	return tangotagger::embedded_discography().recordings[row.match.recording];
}

const std::string & row_orchestra(const disco_row & row)
{
	return tangotagger::embedded_discography().orchestras[row_recording(row).orchestra];
}

std::vector<tag_value> row_changes(const disco_row & row, const tag_options & o)
{
	std::vector<tag_value> out;
	if (!row.matched()) return out;
	const tangotagger::current_tags current{ first_of(row, "TITLE"), first_of(row, "DATE") };
	for (tag_value & t : tangotagger::recording_tags(tangotagger::embedded_discography(), row_recording(row), o, current))
		if (current_of(row, t.field.c_str()) != t.values) out.push_back(std::move(t));
	return out;
}

std::string disco_match_label(const disco_row & row)
{
	if (!row.matched()) return "no match";
	if (row.confident && row.match.sound_identified) return "by sound";
	if (row.match.sound > 0 && !row.match.sound_identified) return "sound?";
	if (row.confident) return "confident";
	if (row.match.orchestra >= 2 && row.match.title >= 8 && row.match.vocal >= 0 && row.match.date >= 0) return "likely";
	return "possible";
}

std::string disco_preview_text(const disco_row & row, const tag_options & o)
{
	const tangotagger::discography & d = tangotagger::embedded_discography();
	std::string out;
	if (!row.matched())
	{
		out += "No recording in the discographies matches \"" + row.title + "\"";
		if (!row.artist.empty()) out += " by " + row.artist;
		out += ".\n\nA track is matched by its title, and the orchestra has to be named somewhere on it - "
		       "artist, album artist, conductor, the file name or its folder. The discographies cover "
		     + std::to_string(d.orchestras.size()) + " orchestras.";
		if (!d.fingerprints.empty())
			out += "\n\nIts sound was compared too, with known transfers of " + std::to_string(d.fingerprints.size())
			     + " of their recordings, and matched none of them.";
		return out;
	}
	const tangotagger::recording & r = row_recording(row);
	if (!row.writable) out += multitrack_note;
	out += row_orchestra(row) + "  \xC2\xB7  " + r.vocal;
	if (!r.date.empty()) out += "  \xC2\xB7  " + r.date;
	if (!r.genre.empty()) out += "  \xC2\xB7  " + r.genre;
	out += "\n" + r.name + "\n";
	out += "Matched on: " + tangotagger::recording_matcher::evidence_text(row.match) + ".\n";
	if (row.match.sound_identified)
		out += "The track sounds like a known transfer of this recording: the same performance, "
		       "whatever the speed, noise or trim.\n";
	else if (row.match.sound > 0)
		out += "The track probably sounds like this recording, but not surely enough: listen before "
		       "writing. An orchestra re-recording an arrangement can sound this close.\n";
	if (row.versions > 1)
		out += "One of " + std::to_string(row.versions) + " recordings this track could be"
		     + (row.confident ? "; the others fit it clearly worse." : ".") + "\n";
	out += "\n";

	const std::vector<tag_value> changes = row_changes(row, o);
	if (changes.empty()) out += "The file's tags already say all this; writing changes nothing.";
	else
	{
		out += "Writing changes:\n";
		for (const tag_value & t : changes)
			out += "    " + t.field + ":  " + joined(current_of(row, t.field.c_str())) + "  \xE2\x86\x92  "
			     + joined(t.values) + "\n";
	}
	if (r.source >= 0)
	{
		// The credit CC BY-SA asks for: title, version, author, licence, links.
		const tangotagger::discography_source & s = d.sources[r.source];
		if (changes.empty()) out += "\n";
		out += "\nSource: discography \"" + s.title + "\"";
		if (!s.version.empty()) out += ", version " + s.version;
		if (!s.date.empty()) out += " of " + s.date;
		out += ", by " + s.author + ", " + s.url + " - licensed under " + s.licence;
		if (!s.licence_url.empty()) out += ", " + s.licence_url;
		out += "; converted and merged with other discographies.";
	}
	return out;
}

std::string disco_status_text(const disco_matches & matches)
{
	std::string out = std::to_string(matches.tracks_matched) + " of " + std::to_string(matches.tracks_examined)
	                + (matches.tracks_examined == 1 ? " track" : " tracks") + " matched, "
	                + std::to_string(matches.tracks_confident) + " of them confidently";
	if (matches.tracks_by_sound > 0) out += " (" + std::to_string(matches.tracks_by_sound) + " by their sound)";
	out += "; those are checked. Pick the right recording for the rest.";
	return out;
}

std::string disco_nothing_text(std::size_t tracks_examined)
{
	std::string out = tracks_examined == 1
		? "The selected track was not found in the discographies."
		: "None of the " + std::to_string(tracks_examined) + " selected tracks was found in the discographies.";
	out += "\n\nA track is matched by its title, and its orchestra has to be named somewhere on it: "
	       "artist, album artist, conductor, the file name or its folder.";
	const tangotagger::discography & d = tangotagger::embedded_discography();
	if (!d.fingerprints.empty())
		out += " A track without them is matched by its sound, against known transfers of "
		     + std::to_string(d.fingerprints.size()) + " recordings.";
	return out;
}

void set_disco_row_checked(std::vector<disco_row> & rows, std::size_t index, bool checked)
{
	if (index >= rows.size() || !rows[index].checkable()) return;
	if (checked)
		for (disco_row & r : rows)
			if (r.group == rows[index].group) r.checked = false;
	rows[index].checked = checked;
}

void check_all_disco_rows(std::vector<disco_row> & rows)
{
	for (std::size_t i = 0; i < rows.size(); i++)
	{
		if (rows[i].version != 1 || !rows[i].checkable()) continue;
		bool any = false;
		for (std::size_t j = i; j < rows.size() && rows[j].group == rows[i].group; j++) any = any || rows[j].checked;
		if (!any) rows[i].checked = true;
	}
}

std::size_t count_checked(const std::vector<disco_row> & rows)
{
	return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [](const disco_row & r) { return r.checked; }));
}

std::vector<track_write> disco_writes(const std::vector<disco_row> & rows, const tag_options & o)
{
	std::vector<track_write> out;
	for (const disco_row & r : rows)
	{
		if (!r.checked) continue;
		track_write w;
		w.track = r.group;
		for (const tag_value & t : row_changes(r, o)) w.fields.push_back(field_write_for(t.field, t.values, r.path));
		if (!w.fields.empty()) out.push_back(std::move(w));
	}
	return out;
}

// --- the results window ------------------------------------------------------

disco_review::disco_review(disco_matches matches, const tag_options & options, writer write, saver save)
	: m_matches(std::move(matches)), m_options(options), m_write(std::move(write)), m_save(std::move(save))
{
	m_columns = {
		{ "", review_column::check, 0 },
		{ "Track title", review_column::fill, 170 },
		{ "Artist", review_column::fill, 130 },
		{ "Recording", review_column::fill, 170 },
		{ "Orchestra", review_column::fill, 140 },
		{ "Singer", review_column::fill, 130 },
		{ "Date", review_column::fit, 0 },
		{ "Match", review_column::fit, 0 },
	};
	const std::vector<disco_row> & rows = m_matches.rows;
	for (std::size_t i = 0; i < rows.size(); i++)
		m_blocks.push_back(i == 0 ? 0 : m_blocks.back() + (rows[i].group != rows[i - 1].group ? 1 : 0));
}

std::string disco_review::cell(std::size_t row, std::size_t column) const
{
	const disco_row & r = m_matches.rows[row];
	switch (column)
	{
	case col_check:  return r.versions > 1 ? std::to_string(r.version) : std::string();
	// A track's further candidates sit under its first, without repeating it.
	case col_title:  return r.version > 1 ? std::string() : r.title;
	case col_artist: return r.version > 1 ? std::string() : r.artist;
	case col_match:  return disco_match_label(r);
	default:         break;
	}
	if (!r.matched()) return std::string();
	const tangotagger::recording & rec = row_recording(r);
	switch (column)
	{
	case col_recording: return tangotagger::main_title(rec.name);
	case col_orchestra: return row_orchestra(r);
	case col_vocal:     return rec.vocal;
	case col_date:      return rec.date;
	default:            return std::string();
	}
}

void disco_review::check_none()
{
	for (disco_row & r : m_matches.rows) r.checked = false;
}

std::string disco_review::preview(std::size_t row) const
{
	return row < m_matches.rows.size() ? disco_preview_text(m_matches.rows[row], m_options) : std::string();
}

std::vector<review_option> disco_review::options() const
{
	std::vector<review_option> out;
	review_option scheme;
	scheme.kind = review_option::choice;
	scheme.label = "Artist scheme:";
	for (int s = 0; s < static_cast<int>(artist_scheme::count); s++)
	{
		const auto a = static_cast<artist_scheme>(s);
		scheme.choices.push_back(std::string(tangotagger::artist_scheme_name(a)) + "  \xE2\x80\x94  "
		                         + tangotagger::artist_scheme_example(a));
	}
	scheme.value = static_cast<int>(m_options.scheme);
	out.push_back(scheme);
	for (std::size_t f = 0; f < field_count; f++)
	{
		review_option box;
		box.kind = review_option::checkbox;
		box.label = field_titles[f];
		box.value = m_options.*(field_members[f]) ? 1 : 0;
		out.push_back(box);
	}
	return out;
}

void disco_review::set_option(std::size_t index, int value)
{
	if (index == 0)
	{
		if (value >= 0 && value < static_cast<int>(artist_scheme::count))
			m_options.scheme = static_cast<artist_scheme>(value);
	}
	else if (index - 1 < field_count)
		m_options.*(field_members[index - 1]) = value != 0;
	else
		return;
	if (m_save) m_save(m_options);
}

void disco_review::commit()
{
	if (m_committed) return;
	m_committed = true;
	std::vector<track_write> writes = disco_writes(m_matches.rows, m_options);
	if (!writes.empty() && m_write) m_write(std::move(writes));
}

}   // namespace tt
