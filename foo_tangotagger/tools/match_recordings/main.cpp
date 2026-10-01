// match_recordings: the discography matching from the command line, for
// trying it on a collection.
//
// Reads one track per line on stdin, tab separated:
//
//     path  title  artist  album artist  performers  album  dates  comment  genre
//
// and writes the same line followed by, tab separated: "confident" or
// "unsure" or "none", then each candidate as
// "orchestra | vocal | date | genre | title | evidence | score".

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "disco_match.h"

int main()
{
	using namespace tangotagger;
	const discography & d = embedded_discography();
	if (d.recordings.empty())
	{
		std::fprintf(stderr, "match_recordings: the embedded discographies are empty\n");
		return 1;
	}
	const recording_matcher m(d);

	std::string line;
	while (std::getline(std::cin, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		std::vector<std::string> f;
		std::size_t start = 0;
		for (;;)
		{
			const std::size_t tab = line.find('\t', start);
			f.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
			if (tab == std::string::npos) break;
			start = tab + 1;
		}
		f.resize(9);
		track_tags t;
		t.path = f[0]; t.title = f[1]; t.artist = f[2]; t.album_artist = f[3]; t.performers = f[4];
		t.album = f[5]; t.dates = f[6]; t.comment = f[7]; t.genre = f[8];

		const track_match r = m.find(t);
		std::string out = line;
		out += r.candidates.empty() ? "\tnone" : r.confident ? "\tconfident" : "\tunsure";
		for (const recording_match & c : r.candidates)
		{
			const recording & rec = d.recordings[c.recording];
			out += "\t" + d.orchestras[rec.orchestra] + " | " + rec.vocal + " | " + rec.date + " | " + rec.genre +
			       " | " + rec.name + " | " + recording_matcher::evidence_text(c) + " | " + std::to_string(c.score);
		}
		std::cout << out << "\n";
	}
	return 0;
}
