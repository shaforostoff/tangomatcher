// match_titles: runs the component's title matching outside foobar2000.
//
// Reads UTF-8 lines "title<TAB>credits" from stdin (credits optional: the
// track's composer and lyricist tags) and writes one line per title:
//
//     exact|similar|none <TAB> title <TAB> song file names, "|"-separated
//
// For checking how a collection would fare before running the component on
// it, e.g. with a script that reads the tags.

#include <cstdio>
#include <iostream>
#include <string>

#include "lyrics_db.h"
#include "title_match.h"

using namespace tangotagger;

int main()
{
	const std::vector<song> & songs = embedded_songs();
	const matcher m(songs);

	std::string line;
	while (std::getline(std::cin, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const std::size_t tab = line.find('\t');
		const std::string title = line.substr(0, tab);
		const std::string credits = tab == std::string::npos ? std::string() : line.substr(tab + 1);

		const title_match r = m.find(title);
		const char * kind = r.kind == match_kind::exact ? "exact" : r.kind == match_kind::similar ? "similar" : "none";
		std::string out = std::string(kind) + "\t" + title + "\t";
		for (std::size_t i = 0; i < r.candidates.size(); i++)
		{
			const song & s = songs[r.candidates[i].song];
			if (i) out += "|";
			out += s.file_name;
			const int score = credit_overlap(credits, s);
			if (score > 0) out += " [credits " + std::to_string(score) + "]";
		}
		std::fwrite(out.data(), 1, out.size(), stdout);
		std::fputc('\n', stdout);
	}
	return 0;
}
