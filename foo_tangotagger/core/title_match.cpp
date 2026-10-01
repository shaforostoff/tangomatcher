#include "title_match.h"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cstdlib>

namespace tangotagger
{
	namespace
	{
		// --- folding -------------------------------------------------------

		//! Latin-1 Supplement, U+00C0..U+00FF. '_' is a word break (× and ÷),
		//! '*' a two-letter fold handled in fold_code_point.
		const char latin1_fold[] =
			"aaaaaa*ceeeeiiii"   // À..Ï   (Æ)
			"dnooooo_ouuuuy**"   // Ð..ß   (× Þ ß)
			"aaaaaa*ceeeeiiii"   // à..ï   (æ)
			"dnooooo_ouuuuy*y";  // ð..ÿ   (÷ þ)

		//! Latin Extended-A, U+0100..U+017F. '*' as above (Ĳ ĳ Œ œ).
		const char latin_ext_a_fold[] =
			"aaaaaaccccccccdd"   // Ā..ď
			"ddeeeeeeeeeegggg"   // Đ..ğ
			"gggghhhhiiiiiiii"   // Ġ..į
			"ii**jjkkklllllll"   // İ..Ŀ
			"lllnnnnnnnnnoooo"   // ŀ..ŏ
			"oo**rrrrrrssssss"   // Ő..ş
			"ssttttttuuuuuuuu"   // Š..ů
			"uuuuwwyyyzzzzzzs";  // Ű..ſ

		//! Appends the fold of one code point to `word`, or returns false when
		//! the code point is a word break.
		bool fold_code_point(std::uint32_t cp, std::string & word)
		{
			if (cp < 0x80)
			{
				if (cp >= 'A' && cp <= 'Z') { word += static_cast<char>(cp - 'A' + 'a'); return true; }
				if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) { word += static_cast<char>(cp); return true; }
				// Apostrophes join rather than break: "Pa' que" and "Pa que"
				// come to the same key either way, but "m'hijo" stays one word.
				if (cp == '\'') return true;
				return false;
			}
			if (cp >= 0xC0 && cp <= 0xFF)
			{
				const char c = latin1_fold[cp - 0xC0];
				if (c == '_') return false;
				if (c != '*') { word += c; return true; }
				switch (cp)
				{
				case 0xC6: case 0xE6: word += "ae"; break;
				case 0xDE: case 0xFE: word += "th"; break;
				case 0xDF:            word += "ss"; break;
				}
				return true;
			}
			if (cp >= 0x100 && cp <= 0x17F)
			{
				const char c = latin_ext_a_fold[cp - 0x100];
				if (c != '*') { word += c; return true; }
				word += (cp == 0x132 || cp == 0x133) ? "ij" : "oe";
				return true;
			}
			// Combining marks: an accent written as a separate code point, as
			// macOS writes file names. The base letter has already been added.
			if (cp >= 0x300 && cp <= 0x36F) return true;
			// Right single quotation mark, used as an apostrophe.
			if (cp == 0x2019) return true;
			// Cyrillic letters that look like Latin ones. The source data has
			// at least one title typed with a Cyrillic С in it.
			switch (cp)
			{
			case 0x410: case 0x430: word += 'a'; return true;
			case 0x412: case 0x432: word += 'b'; return true;
			case 0x415: case 0x435: word += 'e'; return true;
			case 0x41A: case 0x43A: word += 'k'; return true;
			case 0x41C: case 0x43C: word += 'm'; return true;
			case 0x41D: case 0x43D: word += 'h'; return true;
			case 0x41E: case 0x43E: word += 'o'; return true;
			case 0x420: case 0x440: word += 'p'; return true;
			case 0x421: case 0x441: word += 'c'; return true;
			case 0x422: case 0x442: word += 't'; return true;
			case 0x425: case 0x445: word += 'x'; return true;
			case 0x423: case 0x443: word += 'y'; return true;
			case 0x406: case 0x456: word += 'i'; return true;
			}
			// Punctuation and spaces outside ASCII: Latin-1 ¡¿«»·, the General
			// Punctuation block (dashes, quotes, …), CJK punctuation.
			if (cp < 0xC0) return false;
			if (cp >= 0x2000 && cp <= 0x206F) return false;
			if (cp >= 0x3000 && cp <= 0x303F) return false;
			if (cp == 0xFEFF) return false;
			// Anything else - a title in another script - is kept as it is.
			if (cp < 0x800)
			{
				word += static_cast<char>(0xC0 | (cp >> 6));
				word += static_cast<char>(0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000)
			{
				word += static_cast<char>(0xE0 | (cp >> 12));
				word += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				word += static_cast<char>(0x80 | (cp & 0x3F));
			}
			else
			{
				word += static_cast<char>(0xF0 | (cp >> 18));
				word += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
				word += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				word += static_cast<char>(0x80 | (cp & 0x3F));
			}
			return true;
		}

		//! Decodes one UTF-8 sequence at `p`, advancing it. Malformed bytes
		//! come back as U+FFFD one at a time.
		std::uint32_t next_code_point(const unsigned char *& p, const unsigned char * end)
		{
			const unsigned char c = *p++;
			if (c < 0x80) return c;
			int extra;
			std::uint32_t cp;
			if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
			else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
			else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
			else return 0xFFFD;
			if (end - p < extra) return 0xFFFD;
			for (int i = 0; i < extra; i++)
			{
				if ((p[i] & 0xC0) != 0x80) return 0xFFFD;
				cp = (cp << 6) | (p[i] & 0x3F);
			}
			p += extra;
			return cp;
		}

		// --- title structure -----------------------------------------------

		struct bracket_split
		{
			std::string outside;              //!< the title with bracketed parts blanked
			std::vector<std::string> inside;  //!< each top-level bracketed part
		};

		bracket_split split_brackets(const std::string & title)
		{
			bracket_split r;
			int depth = 0;
			std::string current;
			for (char c : title)
			{
				if (c == '(' || c == '[' || c == '{')
				{
					if (depth++ == 0) { r.outside += ' '; current.clear(); continue; }
				}
				else if (c == ')' || c == ']' || c == '}')
				{
					if (depth == 0) { r.outside += ' '; continue; }
					if (--depth == 0) { r.inside.push_back(current); continue; }
				}
				(depth == 0 ? r.outside : current) += c;
			}
			// An unclosed bracket runs to the end of the title.
			if (depth > 0) r.inside.push_back(current);
			return r;
		}

		bool is_dash_at(const std::string & s, std::size_t i, std::size_t & len)
		{
			if (s[i] == '-') { len = 1; return true; }
			// – U+2013 and — U+2014, E2 80 93 / E2 80 94
			if (i + 2 < s.size() && static_cast<unsigned char>(s[i]) == 0xE2 &&
			    static_cast<unsigned char>(s[i + 1]) == 0x80 &&
			    (static_cast<unsigned char>(s[i + 2]) == 0x93 || static_cast<unsigned char>(s[i + 2]) == 0x94))
			{
				len = 3;
				return true;
			}
			return false;
		}

		bool is_space(char c) { return c == ' ' || c == '\t' || c == '_'; }

		//! Splits on dashes that stand apart from a word on at least one side
		//! ("Di Sarli - Rie payaso", "Rie payaso -1940"), but not on a hyphen
		//! inside one ("Frou-Frou").
		std::vector<std::string> split_dashes(const std::string & s)
		{
			std::vector<std::string> pieces;
			std::string current;
			for (std::size_t i = 0; i < s.size();)
			{
				std::size_t len = 0;
				if (is_dash_at(s, i, len))
				{
					const bool space_before = i == 0 || is_space(s[i - 1]);
					const bool space_after = i + len >= s.size() || is_space(s[i + len]);
					if (space_before || space_after)
					{
						pieces.push_back(current);
						current.clear();
						i += len;
						continue;
					}
				}
				current += s[i++];
			}
			pieces.push_back(current);
			return pieces;
		}

		bool is_number(const std::string & w)
		{
			return !w.empty() && std::all_of(w.begin(), w.end(), [](char c) { return c >= '0' && c <= '9'; });
		}

		bool is_roman(const std::string & w)
		{
			static const char * const roman[] = { "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x" };
			for (const char * r : roman)
				if (w == r) return true;
			return false;
		}

		//! Whether a bracketed part describes the recording rather than naming
		//! the song: "(tango)", "(vals peruano)", "(estilo)", "[Remastered 2004]",
		//! "(version de julio sosa)", "(1938)", "(ii)", "(b)", "(hissy)".
		bool is_descriptor(const std::vector<std::string> & words)
		{
			if (words.empty()) return true;
			// Any of these marks the whole part as a note about the recording
			// or the text.
			static const char * const markers[] = {
				"version", "versionoriginal", "remaster", "remastered", "remasterizado",
				"remasterizada", "remasterised", "remasterd", "live", "vivo", "bonus",
				"take", "toma", "edit", "mix", "grabacion", "instrumental", "canta",
				"cantado", "feat", "featuring", "con", "with",
				"hissy", "reverb", "cut", "quality", "noise", "noisy", "fragment",
				"fragmento", "incomplete", "incompleto", "estribillo", "recitado",
				"recitativo", "glosa",
			};
			// A part made only of these is a genre, a form, a year or a
			// variant letter.
			static const char * const plain[] = {
				"tango", "tangos", "vals", "valse", "valsecito", "waltz", "milonga", "milongon",
				"candombe", "estilo", "cancion", "criollo", "criolla", "foxtrot", "fox", "trot",
				"ranchera", "pasodoble", "corrido", "polca", "habanera", "zamba", "chacarera",
				"campero", "campera", "peruano", "orillero", "maxixe", "shimmy", "one", "step",
				"mono", "stereo", "bad", "en", "de", "del", "la", "el", "y", "a",
			};
			bool all_plain = true;
			for (const std::string & w : words)
			{
				for (const char * m : markers)
					if (w == m) return true;
				if (is_number(w) || w.size() == 1 || is_roman(w)) continue;
				bool found = false;
				for (const char * p : plain)
					if (w == p) { found = true; break; }
				if (!found) all_plain = false;
			}
			return all_plain;
		}

		std::string join(const std::vector<std::string> & words)
		{
			std::string key;
			for (const std::string & w : words) key += w;
			return key;
		}

		//! Two letters is enough: "Yo" and "Tú" are tangos.
		bool usable_key(const std::string & key)
		{
			return key.size() >= 2 && !is_number(key);
		}

		void add_unique(std::vector<std::string> & keys, const std::string & key)
		{
			if (usable_key(key) && std::find(keys.begin(), keys.end(), key) == keys.end())
				keys.push_back(key);
		}

		//! Keys of one title string: whole and without brackets into `keys`,
		//! each bracketed part that is an alternative title rather than a note
		//! into `aliases`.
		void add_title_variants(std::vector<std::string> & keys, std::vector<std::string> & aliases,
		                        const std::string & title)
		{
			add_unique(keys, fold_key(title));
			const bracket_split b = split_brackets(title);
			add_unique(keys, fold_key(b.outside));
			for (const std::string & part : b.inside)
			{
				const std::vector<std::string> words = fold_words(part);
				if (!is_descriptor(words)) add_unique(aliases, join(words));
			}
		}

		bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }

		//! 0 to 99 in Spanish words, as titles write them: "treinta y tres".
		std::string spanish_number(int n)
		{
			static const char * const small[] = {
				"cero", "uno", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve",
				"diez", "once", "doce", "trece", "catorce", "quince", "dieciseis", "diecisiete",
				"dieciocho", "diecinueve", "veinte", "veintiuno", "veintidos", "veintitres",
				"veinticuatro", "veinticinco", "veintiseis", "veintisiete", "veintiocho", "veintinueve",
			};
			static const char * const tens[] = { "treinta", "cuarenta", "cincuenta", "sesenta", "setenta", "ochenta", "noventa" };
			if (n < 30) return small[n];
			std::string out = tens[n / 10 - 3];
			if (n % 10 != 0) out = out + " y " + small[n % 10];
			return out;
		}
	}

	std::vector<std::string> fold_words(const std::string & utf8)
	{
		std::vector<std::string> words;
		std::string word;
		const unsigned char * p = reinterpret_cast<const unsigned char *>(utf8.data());
		const unsigned char * const end = p + utf8.size();
		while (p < end)
		{
			const std::uint32_t cp = next_code_point(p, end);
			if (!fold_code_point(cp, word) && !word.empty())
			{
				words.push_back(word);
				word.clear();
			}
		}
		if (!word.empty()) words.push_back(word);
		return words;
	}

	std::string fold_key(const std::string & utf8)
	{
		return join(fold_words(utf8));
	}

	namespace
	{
		std::vector<std::string> credit_words(const song & s)
		{
			std::vector<std::string> words = fold_words(s.composer);
			const std::vector<std::string> more = fold_words(s.author);
			words.insert(words.end(), more.begin(), more.end());
			return words;
		}

		//! Whether `key` is made up entirely of credit words: "caruso" and
		//! "juancaruso" given the words of Juan Andrés Caruso.
		bool is_credit(const std::string & key, const std::vector<std::string> & words)
		{
			if (words.empty() || key.empty()) return false;

			// reachable[i]: key[0, i) splits into credit words.
			std::vector<bool> reachable(key.size() + 1, false);
			reachable[0] = true;
			for (std::size_t i = 0; i < key.size(); i++)
			{
				if (!reachable[i]) continue;
				for (const std::string & w : words)
					if (key.compare(i, w.size(), w) == 0) reachable[i + w.size()] = true;
			}
			return reachable[key.size()];
		}
	}

	std::vector<std::string> song_keys(const song & s, std::vector<std::string> * aliases)
	{
		std::vector<std::string> keys, alias_keys;
		add_title_variants(keys, alias_keys, s.name);
		add_title_variants(keys, alias_keys, s.file_name);
		if (aliases != nullptr)
		{
			// "Nobleza de arrabal (Caruso)": a bracketed name the song credits
			// tells versions apart; it is not another title of the song.
			const std::vector<std::string> credits = credit_words(s);
			aliases->clear();
			for (const std::string & a : alias_keys)
				if (std::find(keys.begin(), keys.end(), a) == keys.end() && !is_credit(a, credits))
					aliases->push_back(a);
		}
		return keys;
	}

	std::vector<std::string> title_keys(const std::string & title)
	{
		std::vector<std::string> keys, aliases;
		add_title_variants(keys, aliases, title);
		// "Orquesta - Title - Singer", "Title - 1940", "- Title -": the pieces
		// between dashes, from the title with its brackets already taken out.
		const std::vector<std::string> pieces = split_dashes(split_brackets(title).outside);
		if (pieces.size() > 1)
			for (const std::string & piece : pieces)
				add_title_variants(keys, aliases, piece);
		// A bracketed alternative title on the track is tried after all of
		// the title proper.
		for (const std::string & a : aliases) add_unique(keys, a);
		return keys;
	}

	std::string spell_numbers(const std::string & s)
	{
		std::string out;
		bool changed = false;
		for (std::size_t i = 0; i < s.size();)
		{
			if (!is_ascii_digit(s[i])) { out += s[i++]; continue; }
			std::size_t j = i;
			while (j < s.size() && is_ascii_digit(s[j])) j++;
			const bool alone = (i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1]))) &&
			                   (j == s.size() || !std::isalnum(static_cast<unsigned char>(s[j])));
			if (alone && j - i <= 2)
			{
				out += spanish_number(std::atoi(s.substr(i, j - i).c_str()));
				changed = true;
			}
			else out.append(s, i, j - i);
			i = j;
		}
		return changed ? out : std::string();
	}

	int similar_limit(std::size_t shorter_length)
	{
		if (shorter_length >= 16) return 2;
		if (shorter_length >= 10) return 1;
		return 0;
	}

	int edit_distance(const std::string & a, const std::string & b, int limit)
	{
		const std::size_t n = a.size(), m = b.size();
		if (static_cast<int>(n > m ? n - m : m - n) > limit) return limit + 1;

		// Three rows of the optimal string alignment table (Levenshtein plus
		// adjacent transposition), so a swapped pair of letters costs one.
		std::vector<int> prev2(m + 1), prev(m + 1), cur(m + 1);
		for (std::size_t j = 0; j <= m; j++) prev[j] = static_cast<int>(j);
		for (std::size_t i = 1; i <= n; i++)
		{
			cur[0] = static_cast<int>(i);
			int row_min = cur[0];
			for (std::size_t j = 1; j <= m; j++)
			{
				const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
				int v = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
				if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
					v = std::min(v, prev2[j - 2] + 1);
				cur[j] = v;
				row_min = std::min(row_min, v);
			}
			if (row_min > limit) return limit + 1;
			std::swap(prev2, prev);
			std::swap(prev, cur);
		}
		return std::min(prev[m], limit + 1);
	}

	matcher::matcher(const std::vector<song> & songs) : m_songs(songs)
	{
		auto add = [this](const std::string & key, int s)
		{
			std::vector<int> & list = m_exact[key];
			if (std::find(list.begin(), list.end(), s) == list.end())
			{
				list.push_back(s);
				m_all.push_back({ key, s });
			}
		};

		// Aliases - "(Fru Fru)" in "Frou Frou (Fru Fru)" - are only indexed
		// when they belong to one song. One that several songs carry is a
		// note after all ("(Caruso)", a singer), and would match a track
		// called that to every one of them.
		std::unordered_map<std::string, std::vector<int>> alias_owners;
		std::vector<std::string> aliases;
		for (std::size_t i = 0; i < songs.size(); i++)
		{
			const int s = static_cast<int>(i);
			for (const std::string & key : song_keys(songs[i], &aliases)) add(key, s);
			for (const std::string & a : aliases)
			{
				std::vector<int> & owners = alias_owners[a];
				if (std::find(owners.begin(), owners.end(), s) == owners.end()) owners.push_back(s);
			}
		}
		// "Olga (Caruso)" is credited to Francisco Peña: todotango tells songs
		// of one title apart by a surname in brackets, not always one of the
		// song's own credits. A bracket made only of words some song credits
		// names a person, not a title.
		// Surnames only - the last word of each person, three letters or more
		// - so that "de", "en" and "flor" cannot spell "(De flor en flor)".
		std::vector<std::string> surnames;
		for (const song & s : songs)
			for (const std::string * credits : { &s.composer, &s.author })
			{
				std::size_t start = 0;
				while (start < credits->size())
				{
					std::size_t end = credits->find_first_of(",;&/", start);
					if (end == std::string::npos) end = credits->size();
					const std::vector<std::string> words = fold_words(credits->substr(start, end - start));
					if (!words.empty() && words.back().size() >= 3 &&
					    std::find(surnames.begin(), surnames.end(), words.back()) == surnames.end())
						surnames.push_back(words.back());
					start = end + 1;
				}
			}
		for (const auto & a : alias_owners)
			if (a.second.size() == 1 && !is_credit(a.first, surnames)) add(a.first, a.second.front());
	}

	title_match matcher::find(const std::string & title) const
	{
		struct found_key
		{
			match_candidate c;
			std::size_t order;   //!< position of the track key in title_keys
		};
		std::vector<found_key> found;

		const std::vector<std::string> keys = title_keys(title);
		for (std::size_t k = 0; k < keys.size(); k++)
		{
			const auto it = m_exact.find(keys[k]);
			if (it == m_exact.end()) continue;
			for (int s : it->second)
				found.push_back({ { s, match_kind::exact, 0, keys[k], keys[k] }, k });
		}

		if (found.empty())
		{
			for (std::size_t k = 0; k < keys.size(); k++)
			{
				for (const keyed & e : m_all)
				{
					const int limit = similar_limit(std::min(keys[k].size(), e.key.size()));
					if (limit == 0) continue;
					const int d = edit_distance(keys[k], e.key, limit);
					if (d <= limit)
						found.push_back({ { e.song, match_kind::similar, d, keys[k], e.key }, k });
				}
			}
		}

		// Closest first; then the key that came earliest in title_keys (the
		// whole title before its pieces); then the longest, which is the most
		// specific; then the source order, so the result is stable.
		std::stable_sort(found.begin(), found.end(), [](const found_key & x, const found_key & y)
		{
			if (x.c.distance != y.c.distance) return x.c.distance < y.c.distance;
			if (x.order != y.order) return x.order < y.order;
			return x.c.track_key.size() > y.c.track_key.size();
		});

		title_match result;
		for (const found_key & f : found)
		{
			const bool seen = std::any_of(result.candidates.begin(), result.candidates.end(),
			                              [&](const match_candidate & c) { return c.song == f.c.song; });
			if (!seen) result.candidates.push_back(f.c);
		}
		if (!result.candidates.empty()) result.kind = result.candidates.front().kind;
		return result;
	}
}

namespace tangotagger
{
	int credit_overlap(const std::string & track_credits, const song & s)
	{
		std::vector<std::string> theirs = fold_words(track_credits);
		const std::vector<std::string> ours = fold_words(s.composer + " " + s.author);
		std::sort(theirs.begin(), theirs.end());
		theirs.erase(std::unique(theirs.begin(), theirs.end()), theirs.end());
		int n = 0;
		for (const std::string & w : theirs)
			if (w.size() >= 4 && std::find(ours.begin(), ours.end(), w) != ours.end()) n++;
		return n;
	}
}
