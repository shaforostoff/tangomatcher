#include "stdafx.h"

#include "disco_rows.h"

#include "file_info_filter_fields.h"
#include "guid.h"

#include <unordered_set>

using tangotagger::artist_scheme;
using tangotagger::tag_options;
using tangotagger::tag_value;

namespace
{
	//! Where a track may say who plays and sings on it, besides ARTIST and
	//! ALBUM ARTIST.
	const char * const performer_fields[] = {
		"CONDUCTOR", "PERFORMER", "ORCHESTRA", "ENSEMBLE", "BAND", "VOCALS", "VOCALIST", "SINGER", "CANTOR",
	};
	//! Where it may say when it was recorded.
	const char * const date_fields[] = {
		"DATE", "YEAR", "ORIGINAL DATE", "ORIGINALDATE", "ORIGINAL YEAR", "ORIGINALYEAR",
		"ORIGINAL RELEASE DATE", "RECORDING DATE", "RECORDINGDATE",
	};
	const char * const comment_fields[] = { "COMMENT", "DESCRIPTION" };
	//! Every field a scheme writes.
	const char * const written_fields[] = { "TITLE", "ARTIST", "ALBUM ARTIST", "CANTOR", "DATE", "GENRE" };

	// The scheme, and the fields as bits: title, artist, album artist,
	// date, genre.
	cfg_int cfg_disco_scheme(guid_cfg_disco_scheme, 0);
	cfg_int cfg_disco_fields(guid_cfg_disco_fields, 0x1F);

	std::string all_values(const file_info & info, const char * const * fields, std::size_t count)
	{
		std::string out;
		for (std::size_t f = 0; f < count; f++)
		{
			const t_size n = info.meta_get_count_by_name(fields[f]);
			for (t_size i = 0; i < n; i++)
			{
				out += info.meta_get(fields[f], i);
				out += " ; ";
			}
		}
		return out;
	}

	template <std::size_t N>
	std::string all_values(const file_info & info, const char * const (&fields)[N])
	{
		return all_values(info, fields, N);
	}

	std::string all_values(const file_info & info, const char * field)
	{
		return all_values(info, &field, 1);
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
}

tag_options disco_tag_options()
{
	tag_options o;
	const int scheme = cfg_disco_scheme;
	if (scheme >= 0 && scheme < static_cast<int>(artist_scheme::count)) o.scheme = static_cast<artist_scheme>(scheme);
	const int bits = cfg_disco_fields;
	o.title = (bits & 1) != 0;
	o.artist = (bits & 2) != 0;
	o.album_artist = (bits & 4) != 0;
	o.date = (bits & 8) != 0;
	o.genre = (bits & 16) != 0;
	return o;
}

void set_disco_tag_options(const tag_options & o)
{
	cfg_disco_scheme = static_cast<int>(o.scheme);
	cfg_disco_fields = (o.title ? 1 : 0) | (o.artist ? 2 : 0) | (o.album_artist ? 4 : 0) | (o.date ? 8 : 0) |
	                   (o.genre ? 16 : 0);
}

std::vector<disco_track> match_disco_tracks(metadb_handle_list_cref tracks)
{
	std::vector<disco_track> result;
	const tangotagger::recording_matcher & m = matcher();

	std::unordered_set<const metadb_handle *> seen;
	for (t_size i = 0; i < tracks.get_count(); i++)
	{
		const metadb_handle_ptr & track = tracks[i];
		if (!seen.insert(track.get_ptr()).second) continue;

		disco_row base;
		base.track = track;
		base.group = result.size();

		tangotagger::track_tags tags;
		tags.path = track->get_path();
		metadb_info_container::ptr info;
		if (track->get_info_ref(info))
		{
			const file_info & fi = info->info();
			auto first = [&](const char * f) { return fi.meta_exists(f) ? std::string(fi.meta_get(f, 0)) : std::string(); };
			tags.title = first("TITLE");
			tags.artist = all_values(fi, "ARTIST");
			tags.album_artist = all_values(fi, "ALBUM ARTIST");
			tags.performers = all_values(fi, performer_fields);
			tags.album = first("ALBUM");
			tags.dates = all_values(fi, date_fields);
			tags.comment = all_values(fi, comment_fields);
			tags.genre = all_values(fi, "GENRE");
			for (const char * f : written_fields)
			{
				const t_size n = fi.meta_get_count_by_name(f);
				for (t_size v = 0; v < n; v++) base.current[f].push_back(fi.meta_get(f, v));
			}
			base.artist = first("ARTIST").c_str();
		}
		base.title = tags.title.empty() ? pfc::string8(pfc::string_filename(track->get_path())) : pfc::string8(tags.title.c_str());

		disco_track t;
		t.base = std::move(base);
		t.match = m.find(tags);
		t.confident_by_tags = t.match.confident;
		result.push_back(std::move(t));
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
			row.checked = row.confident;
			result.rows.push_back(std::move(row));
		}
	}
	for (disco_row & r : unmatched) result.rows.push_back(std::move(r));
	return result;
}

disco_matches find_disco_matches(metadb_handle_list_cref tracks)
{
	return disco_rows_of(match_disco_tracks(tracks));
}

const tangotagger::recording & row_recording(const disco_row & row)
{
	return tangotagger::embedded_discography().recordings[row.match.recording];
}

const std::string & row_orchestra(const disco_row & row)
{
	return tangotagger::embedded_discography().orchestras[row_recording(row).orchestra];
}

std::vector<tag_value> row_changes(const disco_row & row)
{
	std::vector<tag_value> out;
	if (!row.matched()) return out;
	const tangotagger::current_tags current{ first_of(row, "TITLE"), first_of(row, "DATE") };
	for (tag_value & t : tangotagger::recording_tags(tangotagger::embedded_discography(), row_recording(row),
	                                                  disco_tag_options(), current))
		if (current_of(row, t.field.c_str()) != t.values) out.push_back(std::move(t));
	return out;
}

pfc::string8 disco_match_label(const disco_row & row)
{
	if (!row.matched()) return "no match";
	pfc::string_formatter out;
	if (row.confident && row.match.sound_identified) out << "by sound";
	else if (row.match.sound > 0 && !row.match.sound_identified) out << "sound?";
	else if (row.confident) out << "confident";
	else if (row.match.orchestra >= 2 && row.match.title >= 8 && row.match.vocal >= 0 && row.match.date >= 0) out << "likely";
	else out << "possible";
	return out;
}

pfc::string8 disco_preview_text(const disco_row & row, const char * newline)
{
	pfc::string_formatter out;
	if (!row.matched())
	{
		out << "No recording in the discographies matches \"" << row.title << "\"";
		if (!row.artist.is_empty()) out << " by " << row.artist;
		out << "." << newline << newline
		    << "A track is matched by its title, and the orchestra has to be named somewhere on it - "
		    << "artist, album artist, conductor, the file name or its folder. The discographies cover "
		    << tangotagger::embedded_discography().orchestras.size() << " orchestras.";
		if (!tangotagger::embedded_discography().fingerprints.empty())
			out << newline << newline << "Its sound was compared too, with known transfers of "
			    << tangotagger::embedded_discography().fingerprints.size()
			    << " of their recordings, and matched none of them.";
		return out;
	}
	const tangotagger::recording & r = row_recording(row);
	out << row_orchestra(row).c_str() << "  \xC2\xB7  " << r.vocal.c_str();
	if (!r.date.empty()) out << "  \xC2\xB7  " << r.date.c_str();
	if (!r.genre.empty()) out << "  \xC2\xB7  " << r.genre.c_str();
	out << newline << r.name.c_str() << newline;
	out << "Matched on: " << tangotagger::recording_matcher::evidence_text(row.match).c_str() << "." << newline;
	if (row.match.sound_identified)
		out << "The track sounds like a known transfer of this recording: the same performance, "
		    << "whatever the speed, noise or trim." << newline;
	else if (row.match.sound > 0)
		out << "The track probably sounds like this recording, but not surely enough: listen before "
		    << "writing. An orchestra re-recording an arrangement can sound this close." << newline;
	if (row.versions > 1)
		out << "One of " << row.versions << " recordings this track could be"
		    << (row.confident ? "; the others fit it clearly worse." : ".") << newline;
	out << newline;

	const std::vector<tag_value> changes = row_changes(row);
	if (changes.empty()) out << "The file's tags already say all this; writing changes nothing.";
	else
	{
		out << "Writing changes:" << newline;
		for (const tag_value & t : changes)
			out << "    " << t.field.c_str() << ":  " << joined(current_of(row, t.field.c_str())).c_str()
			    << "  \xE2\x86\x92  " << joined(t.values).c_str() << newline;
	}
	if (r.source >= 0)
	{
		// The credit CC BY-SA asks for: title, version, author, licence, links.
		const tangotagger::discography_source & s = tangotagger::embedded_discography().sources[r.source];
		if (changes.empty()) out << newline;
		out << newline << "Source: discography \"" << s.title.c_str() << "\"";
		if (!s.version.empty()) out << ", version " << s.version.c_str();
		if (!s.date.empty()) out << " of " << s.date.c_str();
		out << ", by " << s.author.c_str() << ", " << s.url.c_str() << " - licensed under " << s.licence.c_str();
		if (!s.licence_url.empty()) out << ", " << s.licence_url.c_str();
		out << "; converted and merged with other discographies.";
	}
	return out;
}

pfc::string8 disco_status_text(const disco_matches & matches)
{
	pfc::string_formatter out;
	out << matches.tracks_matched << " of " << matches.tracks_examined
	    << (matches.tracks_examined == 1 ? " track" : " tracks") << " matched, " << matches.tracks_confident
	    << " of them confidently";
	if (matches.tracks_by_sound > 0) out << " (" << matches.tracks_by_sound << " by their sound)";
	out << "; those are checked. Pick the right recording for the rest.";
	return out;
}

void set_disco_row_checked(std::vector<disco_row> & rows, std::size_t index, bool checked)
{
	if (index >= rows.size() || !rows[index].matched()) return;
	if (checked)
		for (disco_row & r : rows)
			if (r.group == rows[index].group) r.checked = false;
	rows[index].checked = checked;
}

void check_all_disco_rows(std::vector<disco_row> & rows)
{
	for (std::size_t i = 0; i < rows.size(); i++)
	{
		if (rows[i].version != 1 || !rows[i].matched()) continue;
		bool any = false;
		for (std::size_t j = i; j < rows.size() && rows[j].group == rows[i].group; j++) any = any || rows[j].checked;
		if (!any) rows[i].checked = true;
	}
}

std::size_t count_checked(const std::vector<disco_row> & rows)
{
	return static_cast<std::size_t>(std::count_if(rows.begin(), rows.end(), [](const disco_row & r) { return r.checked; }));
}

void write_checked_disco_tags(const std::vector<disco_row> & rows)
{
	std::vector<file_info_filter_fields::item> items;
	metadb_handle_list tracks;
	for (const disco_row & r : rows)
	{
		if (!r.checked) continue;
		std::vector<tag_value> changes = row_changes(r);
		if (changes.empty()) continue;
		items.push_back({ r.track, std::move(changes) });
		tracks.add_item(r.track);
	}
	if (items.empty()) return;

	metadb_io_v2::get()->update_info_async(
		tracks,
		fb2k::service_new<file_info_filter_fields>(items),
		core_api::get_main_window(),
		metadb_io_v2::op_flag_background | metadb_io_v2::op_flag_delay_ui,
		nullptr);
}
