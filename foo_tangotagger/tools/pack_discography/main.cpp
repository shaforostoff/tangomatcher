// pack_discography <discography dir> <output .cpp>
//
// Reads every *.xml in the discography directory - one <discography
// orchestra="..."> of <track name vocal year genre .../> each - keeps the
// orchestra, title, singers, date and genre of every recording, compresses
// them with LZMA and writes them out as a C++ array for the component to
// embed. See core/discography.h for the layout.
//
// Left out:
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

	//! The words of an orchestra name outside brackets: what is compared to
	//! decide that one name is a more specific form of another.
	std::set<std::string> name_words(const std::string & name)
	{
		std::string outside;
		int depth = 0;
		for (char c : name)
		{
			if (c == '(') depth++;
			else if (c == ')') depth = std::max(0, depth - 1);
			else if (depth == 0) outside += c;
		}
		const std::vector<std::string> w = tangotagger::fold_words(outside);
		return std::set<std::string>(w.begin(), w.end());
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

	bool includes(const std::set<std::string> & big, const std::set<std::string> & small)
	{
		return std::includes(big.begin(), big.end(), small.begin(), small.end());
	}

	struct entry
	{
		int orchestra;
		std::string fields[4];   // name, vocal, date, genre
		std::string file;
	};
}

int main(int argc, char ** argv)
{
	if (argc != 3)
	{
		std::fprintf(stderr, "usage: pack_discography <discography dir> <output .cpp>\n");
		return 2;
	}
	const fs::path dir = fs::u8path(argv[1]);
	const fs::path output = fs::u8path(argv[2]);

	const std::vector<fs::path> files = pack::xml_files(dir);
	if (files.empty())
	{
		std::fprintf(stderr, "%s: no .xml files in %s\n", tool, argv[1]);
		return 1;
	}

	// Orchestras by folded name: "Astor Piazzolla" and "Ástor Piazzolla" are
	// one, under the first spelling met.
	std::vector<std::string> orchestras;
	std::map<std::string, int> orchestra_index;
	std::vector<entry> entries;
	int files_read = 0, files_skipped = 0;
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
			if (!e.fields[0].empty()) entries.push_back(std::move(e));
		}
	}

	// --- duplicates ------------------------------------------------------------
	std::vector<std::set<std::string>> words;
	for (const std::string & o : orchestras) words.push_back(name_words(o));
	// The more specific orchestra name first, then the longer title - "Los
	// mareados (En mi pasado)" over "Los mareados" - so they are what is kept.
	std::stable_sort(entries.begin(), entries.end(), [&](const entry & a, const entry & b)
	{
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
	pack::put_u32le(payload, static_cast<std::uint32_t>(orchestras.size()));
	for (const std::string & o : orchestras)
	{
		payload += o;
		payload += '\0';
	}
	pack::put_u32le(payload, static_cast<std::uint32_t>(kept.size()));
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
	}

	std::vector<unsigned char> packed;
	if (!pack::compress(payload, packed, tool)) return 1;

	const std::string comment =
		"// Generated by tools/pack_discography: " + std::to_string(kept.size()) + " recordings of " +
		std::to_string(orchestras.size()) + " orchestras from " + std::to_string(files_read) +
		" files. Do not edit.\n// " + std::to_string(payload.size()) + " bytes of payload, LZMA-compressed to " +
		std::to_string(packed.size()) + ".\n\n";
	if (!pack::write_blob(output, "tangotagger_discography_blob", comment, packed, tool)) return 1;

	std::printf("%s: %zu recordings of %zu orchestras from %d files (%d files and %d duplicate recordings left out), "
	            "%zu bytes -> %zu bytes LZMA\n",
	            tool, kept.size(), orchestras.size(), files_read, files_skipped, duplicates,
	            payload.size(), packed.size());
	return 0;
}
