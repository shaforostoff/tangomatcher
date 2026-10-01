// pack_discography <output .cpp> <discography dir>...
//
// Reads every *.xml in the discography directories - one <discography
// orchestra="..."> of <track name vocal year genre .../> each - keeps the
// orchestra, title, singers, date and genre of every recording, compresses
// them with LZMA and writes them out as a C++ array for the component to
// embed. See core/discography.h for the layout.
//
// The directories come best first, and a recording a better one has is left
// out of the others: the same orchestra, the same title - spelled a little
// differently, numbers in digits, an article dropped: "Tangos y copas" and
// "Tango y copas", "Milonga del ochenta y tres" and "Milonga del 83" - and
// the same singers within a month; or the same title on the same day. Tango
// Time Travel's discographies, which come first, often date to the day what
// the others date to the month. Each recording of a better directory stands
// for at most one of each other file, so a session it has does not swallow
// a remake the other file dates close to it.
//
// A file whose root names a licence - CC BY-SA 4.0 - is a source the
// component credits: its title, version, author and links go into the blob,
// and its recordings point at it.
//
// Left out within a directory:
//  - X_tangoinfo.xml and X_bigwithmistakes.xml when the folder has X.xml:
//    older or less reliable versions of a discography it has a better file
//    for. Without one - Edgardo Donato, Julio de Caro - they are used;
//  - a recording listed twice - the same title (bracketed alternatives
//    aside), singers and date under the same orchestra, or, with a full date,
//    under a more specific name of it
//    ("Anibal Troilo (all)" and "Anibal Troilo 1938-1950"; "Orquesta Típica
//    Víctor" and "Orquesta Típica Víctor (dir. Adolfo Carabelli)", which
//    keeps the director).
//
// Runs at build time on the build machine. The output is only rewritten when
// its contents change.

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../pack_common.h"

#include "discography.h"
#include "title_match.h"

namespace fs = std::filesystem;

namespace
{
	const char * const tool = "pack_discography";

	//! The orchestra's file a lesser version is named after: "Carlos di
	//! Sarli" for "Carlos di Sarli_tangoinfo"; empty for a file that is not
	//! a lesser version.
	std::string better_file_stem(const std::string & stem)
	{
		for (const char * suffix : { "_tangoinfo", "_bigwithmistakes" })
		{
			const std::size_t n = std::strlen(suffix);
			if (stem.size() > n && stem.compare(stem.size() - n, n, suffix) == 0) return stem.substr(0, stem.size() - n);
		}
		return std::string();
	}

	bool is_digits(const std::string & s, std::size_t at, std::size_t n)
	{
		if (s.size() < at + n) return false;
		for (std::size_t i = at; i < at + n; i++)
			if (s[i] < '0' || s[i] > '9') return false;
		return true;
	}

	//! "1941-10-09", "1941-10" or "1941"; anything else - "1944--1971",
	//! "?" - is not a recording date and becomes empty.
	std::string clean_date(const std::string & raw)
	{
		const std::string s = pack::trim(raw);
		if (s.size() == 4 && is_digits(s, 0, 4)) return s;
		if (s.size() == 7 && is_digits(s, 0, 4) && s[4] == '-' && is_digits(s, 5, 2)) return s;
		if (s.size() == 10 && is_digits(s, 0, 4) && s[4] == '-' && is_digits(s, 5, 2) && s[7] == '-' &&
		    is_digits(s, 8, 2))
			return s;
		return std::string();
	}

	//! "tango" -> "Tango". The sources disagree on case; the tag should not.
	std::string clean_genre(const std::string & raw)
	{
		std::string s = pack::trim(raw);
		if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
		return s;
	}

	//! Trailing commas and spaces dropped: "Francisco Fiorentino -supuestamente-,".
	std::string clean_vocal(const std::string & raw)
	{
		std::string s = pack::trim(raw);
		while (!s.empty() && (s.back() == ',' || s.back() == ' ')) s.pop_back();
		return s.empty() ? "Instrumental" : s;
	}

	std::string without_brackets(const std::string & s)
	{
		std::string out;
		int depth = 0;
		for (char c : s)
		{
			if (c == '(') depth++;
			else if (c == ')') depth = std::max(0, depth - 1);
			else if (depth == 0) out += c;
		}
		return out;
	}

	//! The words of an orchestra name outside brackets: what is compared to
	//! decide that one name is a more specific form of another.
	std::set<std::string> name_words(const std::string & name)
	{
		const std::vector<std::string> w = tangotagger::fold_words(without_brackets(name));
		return std::set<std::string>(w.begin(), w.end());
	}

	bool includes(const std::set<std::string> & big, const std::set<std::string> & small)
	{
		return std::includes(big.begin(), big.end(), small.begin(), small.end());
	}

	struct entry
	{
		int orchestra;
		std::string fields[4];   // name, vocal, date, genre
		std::string file;
		int rank = 0;            // the directory's place on the command line: 0 is the best
		int source = -1;
	};

	//! What follows `label` on its line of `text`: "Source: ".
	std::string line_after(const std::string & text, const char * label)
	{
		const std::size_t at = text.find(label);
		if (at == std::string::npos) return std::string();
		const std::size_t start = at + std::strlen(label);
		return pack::trim(text.substr(start, text.find('\n', start) - start));
	}

	//! "Tango Time Travel / Moving Art Studio ASBL, https://..." -> the name
	//! and the address.
	void split_link(const std::string & line, std::string & text, std::string & url)
	{
		const std::size_t at = line.find(", http");
		text = pack::trim(line.substr(0, at));
		url = at == std::string::npos ? std::string() : pack::trim(line.substr(at + 2));
	}

	//! The credit a licensed file asks for, from its root element and the
	//! comment before it:
	//!
	//!     Discography Carlos di Sarli 1939-1941, version 2.1 of 2026-09-29.
	//!     Source: Tango Time Travel / Moving Art Studio ASBL, https://...ods
	//!     Licence: CC BY-SA 4.0, https://creativecommons.org/licenses/by-sa/4.0/
	tangotagger::discography_source source_of(const std::string & xml, std::size_t root, const std::string & root_tag,
	                                          const std::string & stem)
	{
		tangotagger::discography_source s;
		const std::size_t open = xml.find("<!--");
		const std::string comment = open < root ? xml.substr(open, xml.find("-->", open) - open) : std::string();
		s.title = line_after(comment, "Discography ");
		s.title = s.title.substr(0, s.title.find(", version"));
		if (s.title.empty()) s.title = stem;
		s.version = pack::trim(pack::attribute(root_tag, "version"));
		s.date = pack::trim(pack::attribute(root_tag, "date"));
		std::string unused;
		split_link(line_after(comment, "Source: "), s.author, unused);
		if (s.author.empty()) s.author = pack::trim(pack::attribute(root_tag, "source"));
		s.url = pack::trim(pack::attribute(root_tag, "url"));
		std::string licence;
		split_link(line_after(comment, "Licence: "), licence, s.licence_url);
		s.licence = pack::trim(pack::attribute(root_tag, "licence"));
		if (s.licence.empty()) s.licence = licence;
		return s;
	}

	const int same_day = -1, too_far = 1000;

	//! How far apart two recording dates are, in months: same_day for the
	//! same full date; 0 when either is missing, or one is only a year and
	//! the other in it; too_far beyond a month.
	int months_apart(const std::string & a, const std::string & b)
	{
		if (a.empty() || b.empty()) return 0;
		if (a == b && a.size() == 10) return same_day;
		const int ya = std::atoi(a.substr(0, 4).c_str()), yb = std::atoi(b.substr(0, 4).c_str());
		if (a.size() < 7 || b.size() < 7) return ya == yb ? 0 : too_far;
		const int months = std::abs((ya * 12 + std::atoi(a.substr(5, 2).c_str())) -
		                            (yb * 12 + std::atoi(b.substr(5, 2).c_str())));
		return months <= 1 ? months : too_far;
	}

	//! The keys two listings of one recording may share: each side of "A |
	//! B", with and without its brackets, each bracketed alternative, all
	//! with the numbers spelled out too, and without a leading article.
	std::vector<std::string> recording_keys(const std::string & name)
	{
		std::vector<std::string> keys;
		auto add = [&](const std::string & piece)
		{
			std::vector<std::string> w = tangotagger::fold_words(piece);
			while (!w.empty())
			{
				std::string k;
				for (const std::string & x : w) k += x;
				if (k.size() >= 2 && std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
				if (w.size() < 2 || (w[0] != "el" && w[0] != "la" && w[0] != "los" && w[0] != "las")) break;
				w.erase(w.begin());
			}
		};
		std::vector<std::string> sides;
		std::string rest = name;
		for (std::size_t bar; (bar = rest.find(" | ")) != std::string::npos; rest = rest.substr(bar + 3))
			sides.push_back(rest.substr(0, bar));
		sides.push_back(rest);
		for (const std::string & side : sides)
			for (const std::string & v : { side, tangotagger::spell_numbers(side) })
			{
				if (v.empty()) continue;
				add(v);
				add(without_brackets(v));
				for (std::size_t open = v.find('('); open != std::string::npos; open = v.find('(', open + 1))
				{
					const std::size_t close = v.find(')', open);
					if (close != std::string::npos) add(v.substr(open + 1, close - open - 1));
				}
			}
		return keys;
	}

	//! A key in common; or one spelled a little differently - as near as
	//! the title matching allows, and on the same day one letter off on any
	//! title or a word more ("Lunes" and "Lunes 13").
	bool same_title(const std::vector<std::string> & a, const std::vector<std::string> & b, bool on_the_day)
	{
		for (const std::string & x : a)
			for (const std::string & y : b)
			{
				if (x == y) return true;
				const std::size_t shorter = std::min(x.size(), y.size());
				int limit = tangotagger::similar_limit(shorter);
				if (on_the_day)
				{
					if (shorter >= 4 && x.compare(0, shorter, y, 0, shorter) == 0) return true;
					limit = std::max(limit, 1);
				}
				if (limit > 0 && tangotagger::edit_distance(x, y, limit) <= limit) return true;
			}
		return false;
	}

	//! The singers' surnames, so "A. Marino, F. Fiorentino" is "Alberto
	//! Marino y Francisco Fiorentino". Empty for an instrumental.
	std::set<std::string> singer_surnames(const std::string & vocal)
	{
		std::set<std::string> out;
		std::string rest = vocal;
		for (std::size_t y; (y = rest.find(" y ")) != std::string::npos;) rest.replace(y, 3, ",");
		for (std::size_t start = 0; start <= rest.size();)
		{
			std::size_t end = rest.find(',', start);
			if (end == std::string::npos) end = rest.size();
			std::vector<std::string> w = tangotagger::fold_words(without_brackets(rest.substr(start, end - start)));
			while (!w.empty() && (w.back() == "coro" || w.back() == "choir" || w.back() == "estribillo" ||
			                      w.back() == "playback" || w.back() == "supuestamente"))
				w.pop_back();
			if (!w.empty() && w.back() != "instrumental") out.insert(w.back());
			start = end + 1;
		}
		return out;
	}

	bool same_singers(const std::string & a, const std::string & b)
	{
		const std::set<std::string> x = singer_surnames(a), y = singer_surnames(b);
		if (x.empty() || y.empty()) return x.empty() && y.empty();
		for (const std::string & s : x)
			if (y.count(s) != 0) return true;
		return false;
	}
}

int main(int argc, char ** argv)
{
	if (argc < 3)
	{
		std::fprintf(stderr, "usage: pack_discography <output .cpp> <discography dir>...\n");
		return 2;
	}
	const fs::path output = fs::u8path(argv[1]);

	// Orchestras by folded name: "Astor Piazzolla" and "Ástor Piazzolla" are
	// one, under the first spelling met.
	std::vector<std::string> orchestras;
	std::map<std::string, int> orchestra_index;
	std::vector<tangotagger::discography_source> sources;
	std::vector<entry> entries;
	int files_read = 0, files_skipped = 0;
	for (int rank = 0; rank + 2 < argc; rank++)
	{
		const std::vector<fs::path> files = pack::xml_files(fs::u8path(argv[rank + 2]));
		if (files.empty())
		{
			std::fprintf(stderr, "%s: no .xml files in %s\n", tool, argv[rank + 2]);
			return 1;
		}
		for (const fs::path & path : files)
		{
			const std::string better = better_file_stem(path.stem().u8string());
			if (!better.empty() && fs::exists(path.parent_path() / fs::u8path(better + ".xml")))
			{
				files_skipped++;
				continue;
			}
			std::string xml;
			if (!pack::read_file(path, xml))
			{
				std::fprintf(stderr, "%s: error: %s: cannot read\n", tool, path.u8string().c_str());
				return 1;
			}
			const std::size_t root = xml.find("<discography");
			if (root == std::string::npos)
			{
				std::fprintf(stderr, "%s: error: %s: no <discography> element\n", tool, path.u8string().c_str());
				return 1;
			}
			const std::string root_tag = xml.substr(root, xml.find('>', root) - root);
			std::string orchestra = pack::trim(pack::attribute(root_tag, "orchestra"));
			if (orchestra.empty()) orchestra = path.stem().u8string();
			const std::string key = tangotagger::fold_key(orchestra);
			auto found = orchestra_index.find(key);
			if (found == orchestra_index.end())
			{
				found = orchestra_index.emplace(key, static_cast<int>(orchestras.size())).first;
				orchestras.push_back(orchestra);
			}
			files_read++;

			int source = -1;
			if (!pack::trim(pack::attribute(root_tag, "licence")).empty())
			{
				const tangotagger::discography_source s = source_of(xml, root, root_tag, path.stem().u8string());
				for (std::size_t i = 0; i < sources.size() && source < 0; i++)
					if (sources[i].title == s.title && sources[i].version == s.version && sources[i].url == s.url)
						source = static_cast<int>(i);
				if (source < 0)
				{
					source = static_cast<int>(sources.size());
					sources.push_back(s);
				}
			}

			for (std::size_t pos = root;;)
			{
				const std::size_t t = xml.find("<track ", pos);
				if (t == std::string::npos) break;
				const std::size_t end = xml.find('>', t);
				if (end == std::string::npos) break;
				const std::string tag = xml.substr(t, end - t);
				pos = end;
				entry e;
				e.orchestra = found->second;
				e.fields[0] = pack::trim(pack::attribute(tag, "name"));
				e.fields[1] = clean_vocal(pack::attribute(tag, "vocal"));
				e.fields[2] = clean_date(pack::attribute(tag, "year"));
				e.fields[3] = clean_genre(pack::attribute(tag, "genre"));
				e.file = path.filename().u8string();
				e.rank = rank;
				e.source = source;
				if (!e.fields[0].empty()) entries.push_back(std::move(e));
			}
		}
	}

	std::vector<std::set<std::string>> words;
	for (const std::string & o : orchestras) words.push_back(name_words(o));

	// --- recordings a better directory has -------------------------------------
	// Looked for under the same orchestra name outside brackets. The better
	// directory's recording takes on a bracketed name: "Orquesta Típica
	// Víctor (dir. Adolfo Carabelli)" keeps the director.
	int known_better = 0;
	{
		std::vector<std::vector<std::string>> keys(entries.size());
		for (std::size_t i = 0; i < entries.size(); i++) keys[i] = recording_keys(entries[i].fields[0]);
		std::map<std::set<std::string>, std::vector<std::size_t>> better;   // orchestra words -> entries
		std::vector<bool> drop(entries.size(), false);
		std::vector<std::set<std::string>> stands_for(entries.size());   // the files whose recording it is
		for (std::size_t i = 0, first_of_rank = 0; i < entries.size(); i++)
		{
			const entry & e = entries[i];
			if (e.rank != entries[first_of_rank].rank)
			{
				for (std::size_t j = first_of_rank; j < i; j++)
					if (!drop[j]) better[words[entries[j].orchestra]].push_back(j);
				first_of_rank = i;
			}
			const auto candidates = better.find(words[e.orchestra]);
			if (candidates == better.end()) continue;
			int best = -1, best_months = too_far;
			for (std::size_t j : candidates->second)
			{
				const entry & b = entries[j];
				const int months = months_apart(e.fields[2], b.fields[2]);
				if (months >= best_months || stands_for[j].count(e.file) != 0) continue;
				if (!same_title(keys[i], keys[j], months == same_day)) continue;
				if (months != same_day && !same_singers(e.fields[1], b.fields[1])) continue;
				best = static_cast<int>(j);
				best_months = months;
			}
			if (best < 0) continue;
			drop[i] = true;
			known_better++;
			stands_for[best].insert(e.file);
			entry & kept = entries[best];
			if (orchestras[e.orchestra].find('(') != std::string::npos &&
			    orchestras[kept.orchestra].find('(') == std::string::npos)
				kept.orchestra = e.orchestra;
		}
		std::vector<entry> rest;
		for (std::size_t i = 0; i < entries.size(); i++)
			if (!drop[i]) rest.push_back(std::move(entries[i]));
		entries.swap(rest);
	}

	// --- duplicates ------------------------------------------------------------
	// The better directory first, then the more specific orchestra name, then
	// the longer title - "Los mareados (En mi pasado)" over "Los mareados" -
	// so they are what is kept.
	std::stable_sort(entries.begin(), entries.end(), [&](const entry & a, const entry & b)
	{
		if (a.rank != b.rank) return a.rank < b.rank;
		if (orchestras[a.orchestra].size() != orchestras[b.orchestra].size())
			return orchestras[a.orchestra].size() > orchestras[b.orchestra].size();
		return a.fields[0].size() > b.fields[0].size();
	});
	std::map<std::string, std::vector<int>> seen;   // title|vocal|date -> orchestras holding it
	std::vector<entry> kept;
	int duplicates = 0;
	for (entry & e : entries)
	{
		bool duplicate = false;
		// With a full date, the same recording under a related orchestra
		// name too; with only a year or a month, only under the same one -
		// "Malena", Raúl Berón, 1952, listed twice by one file.
		if (!e.fields[2].empty())
		{
			std::vector<int> & holders = seen[tangotagger::fold_key(without_brackets(e.fields[0])) + "|" +
			                                  tangotagger::fold_key(e.fields[1]) + "|" + e.fields[2]];
			for (int h : holders)
				if (h == e.orchestra || (e.fields[2].size() == 10 && includes(words[h], words[e.orchestra])))
					duplicate = true;
			if (!duplicate) holders.push_back(e.orchestra);
		}
		if (duplicate) { duplicates++; continue; }
		kept.push_back(std::move(e));
	}

	// By orchestra, then title, then date: the order the component lists
	// them in, and the one that compresses best.
	std::vector<std::string> sort_keys(kept.size());
	std::vector<std::size_t> order(kept.size());
	for (std::size_t i = 0; i < kept.size(); i++)
	{
		order[i] = i;
		sort_keys[i] = tangotagger::fold_key(kept[i].fields[0]);
	}
	std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
	{
		const entry & x = kept[a];
		const entry & y = kept[b];
		if (x.orchestra != y.orchestra) return orchestras[x.orchestra] < orchestras[y.orchestra];
		if (sort_keys[a] != sort_keys[b]) return sort_keys[a] < sort_keys[b];
		if (x.fields[2] != y.fields[2]) return x.fields[2] < y.fields[2];
		return x.fields[1] < y.fields[1];
	});

	std::string payload(tangotagger::discography_magic, 4);
	pack::put_u32le(payload, static_cast<std::uint32_t>(sources.size()));
	for (const tangotagger::discography_source & s : sources)
		for (const std::string * f : { &s.title, &s.version, &s.date, &s.author, &s.url, &s.licence, &s.licence_url })
		{
			payload += *f;
			payload += '\0';
		}
	pack::put_u32le(payload, static_cast<std::uint32_t>(orchestras.size()));
	for (const std::string & o : orchestras)
	{
		payload += o;
		payload += '\0';
	}
	pack::put_u32le(payload, static_cast<std::uint32_t>(kept.size()));
	std::size_t credited = 0;
	for (std::size_t i : order)
	{
		const entry & e = kept[i];
		payload += std::to_string(e.orchestra);
		payload += '\0';
		for (const std::string & f : e.fields)
		{
			payload += f;
			payload += '\0';
		}
		if (e.source >= 0)
		{
			payload += std::to_string(e.source);
			credited++;
		}
		payload += '\0';
	}

	std::vector<unsigned char> packed;
	if (!pack::compress(payload, packed, tool)) return 1;

	const std::string comment =
		"// Generated by tools/pack_discography: " + std::to_string(kept.size()) + " recordings of " +
		std::to_string(orchestras.size()) + " orchestras from " + std::to_string(files_read) +
		" files. Do not edit.\n// " + std::to_string(payload.size()) + " bytes of payload, LZMA-compressed to " +
		std::to_string(packed.size()) + ".\n\n";
	if (!pack::write_blob(output, "tangotagger_discography_blob", comment, packed, tool)) return 1;

	std::printf("%s: %zu recordings of %zu orchestras from %d files, %zu of them from %zu credited sources "
	            "(%d files, %d recordings a better file has and %d duplicate recordings left out), "
	            "%zu bytes -> %zu bytes LZMA\n",
	            tool, kept.size(), orchestras.size(), files_read, credited, sources.size(), files_skipped,
	            known_better, duplicates, payload.size(), packed.size());
	return 0;
}
