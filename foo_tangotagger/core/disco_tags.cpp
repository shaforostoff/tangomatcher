#include "disco_tags.h"
#include "disco_match.h"
#include "title_match.h"

namespace tangotagger
{
	const char * artist_scheme_name(artist_scheme s)
	{
		switch (s)
		{
		case artist_scheme::orchestra_dash_singer:       return "Orquesta - Cantor";
		case artist_scheme::orchestra_slash_singer:      return "Orquesta / Cantor";
		case artist_scheme::singer_only:                 return "Cantor (orquesta as album artist)";
		case artist_scheme::orchestra_and_cantor_field:  return "Orquesta, cantor in CANTOR";
		case artist_scheme::orchestra_and_singer_values: return "Orquesta; Cantor (two artist values)";
		default:                                         return "";
		}
	}

	std::string artist_scheme_example(artist_scheme s)
	{
		switch (s)
		{
		case artist_scheme::orchestra_dash_singer:       return "Carlos di Sarli - Roberto Rufino";
		case artist_scheme::orchestra_slash_singer:      return "Carlos di Sarli / Roberto Rufino";
		case artist_scheme::singer_only:                 return "Roberto Rufino";
		case artist_scheme::orchestra_and_cantor_field:  return "Carlos di Sarli; CANTOR: Roberto Rufino";
		case artist_scheme::orchestra_and_singer_values: return "Carlos di Sarli; Roberto Rufino";
		default:                                         return "";
		}
	}

	std::string title_with_notes(const std::string & recording_name, const std::string & current_title)
	{
		std::string out = main_title(recording_name);
		const std::string own = fold_key(recording_name);
		int depth = 0;
		std::string part;
		for (char c : current_title)
		{
			if (c == '(' || c == '[')
			{
				if (depth++ == 0) { part.clear(); continue; }
			}
			else if ((c == ')' || c == ']') && depth > 0)
			{
				if (--depth == 0)
				{
					bool lower = true;
					for (char p : part)
						if (p >= 'A' && p <= 'Z') lower = false;
					const std::string key = fold_key(part);
					if (lower && !key.empty() && own.find(key) == std::string::npos &&
					    out.find("(" + part + ")") == std::string::npos)
						out += " (" + part + ")";
					continue;
				}
			}
			if (depth > 0) part += c;
		}
		return out;
	}

	std::string date_for(const recording & r, const std::string & current_date)
	{
		if (r.date.empty()) return current_date;
		const date_parts ours = parse_date(r.date);
		const date_parts theirs = parse_date(current_date);
		const int years_apart = ours.year > theirs.year ? ours.year - theirs.year : theirs.year - ours.year;
		const bool theirs_finer = theirs.day != 0 ? ours.day == 0 : (theirs.month != 0 && ours.month == 0);
		if (theirs.year != 0 && theirs_finer && years_apart <= 1) return current_date;
		return r.date;
	}

	std::vector<tag_value> recording_tags(const discography & d, const recording & r, const tag_options & options,
	                                      const current_tags & current)
	{
		std::vector<tag_value> out;
		const std::string & orchestra = d.orchestras[r.orchestra];
		const bool instrumental = is_instrumental(r.vocal);
		const std::vector<std::string> singers = singers_of(r.vocal);
		std::string singer_list;
		for (const std::string & s : singers)
			singer_list += (singer_list.empty() ? "" : ", ") + s;

		if (options.title) out.push_back({ "TITLE", { title_with_notes(r.name, current.title) } });
		if (options.artist)
		{
			switch (options.scheme)
			{
			case artist_scheme::orchestra_slash_singer:
				out.push_back({ "ARTIST", { instrumental ? orchestra : orchestra + " / " + singer_list } });
				break;
			case artist_scheme::singer_only:
				out.push_back({ "ARTIST", { instrumental ? std::string("Instrumental") : singer_list } });
				break;
			case artist_scheme::orchestra_and_cantor_field:
				out.push_back({ "ARTIST", { orchestra } });
				out.push_back({ "CANTOR", { instrumental ? std::string("Instrumental") : singer_list } });
				break;
			case artist_scheme::orchestra_and_singer_values:
			{
				tag_value artist{ "ARTIST", { orchestra } };
				for (const std::string & s : singers) artist.values.push_back(s);
				out.push_back(artist);
				break;
			}
			default:
				out.push_back({ "ARTIST", { orchestra + " - " + (instrumental ? std::string("Instrumental") : singer_list) } });
				break;
			}
		}
		// The singer-only scheme has nowhere else for the orchestra.
		if (options.album_artist || (options.artist && options.scheme == artist_scheme::singer_only))
			out.push_back({ "ALBUM ARTIST", { orchestra } });
		if (options.date && !r.date.empty()) out.push_back({ "DATE", { date_for(r, current.date) } });
		if (options.genre && !r.genre.empty()) out.push_back({ "GENRE", { r.genre } });
		return out;
	}
}
