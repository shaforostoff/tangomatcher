#include "stdafx.h"

#include "lyrics_rows.h"

#include "file_info_filter_lyrics.h"
#include "lyrics_db.h"
#include "lyrics_field.h"

#include "guid.h"

#include <unordered_set>

using tangotagger::match_kind;

namespace
{
	//! More near misses than this for one title is noise, not a choice.
	const std::size_t max_similar_candidates = 5;

	//! The tags that credit the writers of a track.
	const char * const credit_fields[] = { "COMPOSER", "LYRICIST", "WRITER", "AUTHOR" };

	//! Line endings dropped and surrounding whitespace trimmed, so lyrics
	//! written by this component on one platform compare equal to the same
	//! text read back on the other.
	std::string comparable(const char * text)
	{
		std::string out;
		for (const char * p = text; *p != '\0'; p++)
			if (*p != '\r') out += *p;
		const std::size_t b = out.find_first_not_of(" \t\n");
		if (b == std::string::npos) return std::string();
		return out.substr(b, out.find_last_not_of(" \t\n") - b + 1);
	}

	cfg_bool cfg_translation_links(guid_cfg_translation_links, false);

	file_lyrics existing_in(const file_info & info, const tangotagger::song & s)
	{
		const std::string text = comparable(s.text.c_str());
		const std::string linked = comparable(lyrics_text(s, true).c_str());
		file_lyrics result = file_lyrics::none;
		for (const char * field : lyrics_known_fields)
		{
			const t_size count = info.meta_get_count_by_name(field);
			for (t_size i = 0; i < count; i++)
			{
				const std::string theirs = comparable(info.meta_get(field, i));
				if (theirs.empty()) continue;
				if (theirs == text) return file_lyrics::text;
				if (theirs == linked) return file_lyrics::linked;
				result = file_lyrics::other;
			}
		}
		return result;
	}

	std::string credits_of(const file_info & info)
	{
		std::string out;
		for (const char * field : credit_fields)
		{
			const t_size count = info.meta_get_count_by_name(field);
			for (t_size i = 0; i < count; i++)
			{
				out += info.meta_get(field, i);
				out += ' ';
			}
		}
		return out;
	}

	pfc::string8 with_newlines(const std::string & text, const char * newline)
	{
		pfc::string8 out;
		for (char c : text)
		{
			if (c == '\n') out += newline;
			else out.add_byte(c);
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
			if (r.in_file != file_lyrics::none) return -1;
		return pick;
	}
}

lyrics_matches find_lyrics_matches(metadb_handle_list_cref tracks)
{
	lyrics_matches result;
	const std::vector<tangotagger::song> & songs = tangotagger::embedded_songs();
	// Built once: the lyrics panel matches the selected track on every
	// selection change.
	static const tangotagger::matcher m(songs);

	// Selection order, each track once; the tracks nothing matched after all
	// the others, where they do not break up the list of choices.
	std::vector<lyrics_row> unmatched;
	std::unordered_set<const metadb_handle *> seen;
	for (t_size i = 0; i < tracks.get_count(); i++)
	{
		const metadb_handle_ptr & track = tracks[i];
		if (!seen.insert(track.get_ptr()).second) continue;
		result.tracks_examined++;

		lyrics_row base;
		base.track = track;
		base.group = result.tracks_examined - 1;
		base.field = lyrics_field_for_path(track->get_path());

		metadb_info_container::ptr info;
		const bool have_info = track->get_info_ref(info);
		if (have_info && info->info().meta_exists("TITLE")) base.title = info->info().meta_get("TITLE", 0);
		else base.title = pfc::string_filename(track->get_path());
		if (have_info && info->info().meta_exists("ARTIST")) base.artist = info->info().meta_get("ARTIST", 0);
		const std::string credits = have_info ? credits_of(info->info()) : std::string();

		const tangotagger::title_match match = m.find(base.title.get_ptr());
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
			if (have_info) row.in_file = existing_in(info->info(), songs[c.song]);
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

bool translation_links_enabled()
{
	return cfg_translation_links;
}

void set_translation_links_enabled(bool enabled)
{
	cfg_translation_links = enabled;
}

existing_lyrics lyrics_row::existing() const
{
	switch (in_file)
	{
	case file_lyrics::none:  return existing_lyrics::none;
	case file_lyrics::other: return existing_lyrics::different;
	case file_lyrics::text:
		// A song without translations has no links to add.
		return translation_links_enabled() && !row_song(*this).translations.empty()
		       ? existing_lyrics::same_without_links : existing_lyrics::same;
	default:
		return translation_links_enabled() ? existing_lyrics::same : existing_lyrics::same_with_links;
	}
}

std::string lyrics_text(const tangotagger::song & s, bool with_links)
{
	const std::string links = with_links ? tangotagger::translation_links_text(s) : std::string();
	return links.empty() ? s.text : s.text + "\n\n" + links;
}

pfc::string8 lyrics_tag_text(const tangotagger::song & s)
{
	return with_newlines(lyrics_text(s, translation_links_enabled()), "\r\n");
}

pfc::string8 lyrics_preview_text(const lyrics_row & row, const char * newline)
{
	if (!row.matched())
	{
		pfc::string_formatter out;
		out << "No lyrics match \"" << row.title << "\"." << newline << newline
		    << "Titles are compared whole, ignoring accents, case, punctuation and bracketed notes; "
		    << "a title that is only part of a song's title, or the other way round, does not match.";
		return out;
	}
	const tangotagger::song & s = row_song(row);

	pfc::string_formatter out;
	out << s.name.c_str() << newline;
	const pfc::string8 credits = song_credits_text(s);
	if (!credits.is_empty()) out << credits << newline;
	if (row.versions > 1)
	{
		out << "One of " << row.versions << " songs this title matches";
		if (row.credit_score > 0) out << "; the track's composer/lyricist tags name this one";
		out << "." << newline;
	}
	if (row.match.kind == match_kind::similar)
		out << "Similar, not identical: the title reads \"" << row.match.track_key.c_str()
		    << "\", the song \"" << row.match.song_key.c_str() << "\"." << newline;
	switch (row.existing())
	{
	case existing_lyrics::different:
		out << "The file already has other lyrics; writing replaces them." << newline;
		break;
	case existing_lyrics::same:
		out << "The file already has these lyrics." << newline;
		break;
	case existing_lyrics::same_without_links:
		out << "The file already has these lyrics, without the links to translations; writing adds them." << newline;
		break;
	case existing_lyrics::same_with_links:
		out << "The file already has these lyrics, with links to translations; writing leaves the links out." << newline;
		break;
	default:
		break;
	}
	out << newline << with_newlines(lyrics_text(s, translation_links_enabled()), newline);
	return out;
}

pfc::string8 song_credits_text(const tangotagger::song & s)
{
	pfc::string_formatter out;
	if (!s.composer.empty()) out << "Music: " << s.composer.c_str();
	if (!s.composer.empty() && !s.author.empty()) out << "  \xC2\xB7  ";   // middle dot
	if (!s.author.empty()) out << "Lyrics: " << s.author.c_str();
	return out;
}

pfc::string8 match_label(const lyrics_row & row)
{
	if (!row.matched()) return "no match";
	pfc::string_formatter out;
	out << (row.match.kind == match_kind::exact ? "exact" : "similar");
	if (row.versions > 1) out << " " << row.version << "/" << row.versions;
	return out;
}

const char * existing_label(existing_lyrics e)
{
	switch (e)
	{
	case existing_lyrics::same:               return "same";
	case existing_lyrics::same_without_links: return "same, no links";
	case existing_lyrics::same_with_links:    return "same, with links";
	case existing_lyrics::different:          return "different";
	default:                         return "none";
	}
}

pfc::string8 lyrics_fields_summary(const std::vector<lyrics_row> & rows)
{
	std::vector<std::string> fields;
	for (const lyrics_row & r : rows)
		if (r.matched() && std::find(fields.begin(), fields.end(), r.field.get_ptr()) == fields.end())
			fields.push_back(r.field.get_ptr());
	pfc::string_formatter out;
	for (std::size_t i = 0; i < fields.size(); i++)
		out << (i ? ", " : "") << "%" << fields[i].c_str() << "%";
	return out;
}

pfc::string8 lyrics_status_text(const lyrics_matches & matches)
{
	std::size_t several = 0;
	for (const lyrics_row & r : matches.rows)
		if (r.version == 2) several++;

	pfc::string_formatter out;
	out << matches.tracks_matched << " of " << matches.tracks_examined
	    << (matches.tracks_examined == 1 ? " track" : " tracks") << " matched";
	if (several == 1) out << "; 1 has several songs to choose from";
	else if (several > 1) out << "; " << several << " have several songs to choose from";
	out << ". Checked tracks get the lyrics in " << lyrics_fields_summary(matches.rows) << ".";
	return out;
}

void set_row_checked(std::vector<lyrics_row> & rows, std::size_t index, bool checked)
{
	if (index >= rows.size() || !rows[index].matched()) return;
	if (checked)
		for (lyrics_row & r : rows)
			if (r.group == rows[index].group) r.checked = false;
	rows[index].checked = checked;
}

std::size_t count_checked(const std::vector<lyrics_row> & rows)
{
	return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(),
	                                              [](const lyrics_row & r) { return r.checked; }));
}

void write_checked_lyrics(const std::vector<lyrics_row> & rows)
{
	std::vector<file_info_filter_lyrics::item> items;
	metadb_handle_list tracks;
	for (const lyrics_row & r : rows)
	{
		if (!r.checked) continue;
		items.push_back({ r.track, r.field, lyrics_tag_text(row_song(r)) });
		tracks.add_item(r.track);
	}
	if (items.empty()) return;

	metadb_io_v2::get()->update_info_async(
		tracks,
		fb2k::service_new<file_info_filter_lyrics>(items),
		core_api::get_main_window(),
		metadb_io_v2::op_flag_background | metadb_io_v2::op_flag_delay_ui,
		nullptr);
}
