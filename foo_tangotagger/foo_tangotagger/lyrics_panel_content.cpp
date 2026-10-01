#include "stdafx.h"

#include "lyrics_panel_content.h"

#include "lyrics_db.h"
#include "lyrics_field.h"
#include "lyrics_rows.h"

namespace
{
	//! LF line endings, surrounding blank lines and whitespace trimmed.
	std::string normalised(const char * text)
	{
		std::string out;
		for (const char * p = text; *p != '\0'; p++)
		{
			if (*p == '\r')
			{
				out += '\n';
				if (p[1] == '\n') p++;
			}
			else out += *p;
		}
		const std::size_t b = out.find_first_not_of(" \t\n");
		if (b == std::string::npos) return std::string();
		return out.substr(b, out.find_last_not_of(" \t\n") - b + 1);
	}

	//! The lyrics the file carries, under whichever name it has them.
	std::string lyrics_in_file(const metadb_handle_ptr & track)
	{
		metadb_info_container::ptr info;
		if (!track->get_info_ref(info)) return std::string();
		for (const char * field : lyrics_known_fields)
		{
			std::string text;
			const t_size count = info->info().meta_get_count_by_name(field);
			for (t_size i = 0; i < count; i++)
			{
				const std::string value = normalised(info->info().meta_get(field, i));
				if (value.empty()) continue;
				if (!text.empty()) text += "\n\n";
				text += value;
			}
			if (!text.empty()) return text;
		}
		return std::string();
	}

	void add(lyrics_panel_content & out, lyrics_panel_style style, const std::string & text)
	{
		lyrics_panel_line line;
		line.style = style;
		line.text = text;
		line.links = tangotagger::find_links(text);
		out.lines.push_back(std::move(line));
	}

	void add_lines(lyrics_panel_content & out, lyrics_panel_style style, const std::string & text)
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

lyrics_panel_content lyrics_panel_content_for(const metadb_handle_ptr & track)
{
	lyrics_panel_content out;
	if (!track.is_valid())
	{
		add(out, lyrics_panel_style::note, "No track selected.");
		return out;
	}

	metadb_handle_list one;
	one.add_item(track);
	const lyrics_matches matches = find_lyrics_matches(one);
	// One row at least: a track nothing matched gets an unmatched row.
	const lyrics_row & best = matches.rows.front();

	std::string heading = best.title.get_ptr();
	std::string credits = best.artist.get_ptr();
	std::string text = lyrics_in_file(track);
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
		pfc::string_formatter note;
		if (best.match.kind == tangotagger::match_kind::similar)
			note << "Built-in lyrics of a song with a similar title; not in the file.";
		else
			note << "Built-in lyrics; not in the file. Tango Tagger > Find lyrics... writes them.";
		notes.push_back(note.get_ptr());
		if (best.versions > 1)
		{
			pfc::string_formatter several;
			several << "One of " << best.versions << " songs with this title";
			if (best.credit_score > 0) several << "; the track's composer/lyricist tags name this one";
			several << ".";
			notes.push_back(several.get_ptr());
		}
	}
	else
	{
		notes.push_back("No lyrics in the file, and none of the built-in songs has this title.");
	}

	if (song != nullptr)
	{
		heading = song->name;
		credits = song_credits_text(*song).get_ptr();
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

	add(out, lyrics_panel_style::heading, heading);
	if (!credits.empty()) add(out, lyrics_panel_style::credits, credits);
	if (!text.empty())
	{
		add(out, lyrics_panel_style::text, std::string());
		add_lines(out, lyrics_panel_style::text, text);

		out.copy_text = heading + "\n";
		if (!credits.empty()) out.copy_text += credits + "\n";
		out.copy_text += "\n" + text + "\n";
	}
	add(out, lyrics_panel_style::text, std::string());
	for (const std::string & n : notes) add(out, lyrics_panel_style::note, n);
	return out;
}
