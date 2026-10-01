#include "disco_match.h"
#include "title_match.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_set>

namespace tangotagger
{
	namespace
	{
		const std::size_t max_candidates = 8;

		bool contains(const std::vector<std::string> & v, const std::string & s)
		{
			return std::find(v.begin(), v.end(), s) != v.end();
		}

		std::string strip_brackets(const std::string & s, std::string * inside = nullptr)
		{
			std::string out;
			int depth = 0;
			for (char c : s)
			{
				if (c == '(' || c == '[') { depth++; out += ' '; continue; }
				if ((c == ')' || c == ']') && depth > 0) { depth--; out += ' '; continue; }
				if (depth == 0) out += c;
				else if (inside != nullptr) *inside += c;
			}
			return out;
		}

		//! Leaders' names come with these around them: "Orquesta Típica
		//! Víctor", "Juan D'Arienzo y su Orquesta Típica".
		bool is_filler(const std::string & w)
		{
			static const char * const fillers[] = {
				"orquesta", "orq", "orchestra", "orchestre", "orkestra", "y", "su", "and", "conjunto",
				"dir", "director", "con", "la", "el", "los", "las", "his", "band",
			};
			for (const char * f : fillers)
				if (w == f) return true;
			return false;
		}

		//! "Sexteto", "Quartet"...: the line-up, which tells a leader's sextet
		//! apart from his orchestra. Returned in one spelling.
		const char * qualifier(const std::string & w)
		{
			if (w == "sexteto" || w == "sextet" || w == "sextette") return "sexteto";
			if (w == "quinteto" || w == "quintet" || w == "quintette") return "quinteto";
			if (w == "cuarteto" || w == "quartet" || w == "quarteto" || w == "quartette") return "cuarteto";
			if (w == "trio") return "trio";
			if (w == "jazz") return "jazz";
			return nullptr;
		}

		bool is_particle(const std::string & w)
		{
			static const char * const particles[] = { "di", "de", "d", "da", "del", "della", "dalla", "van", "von" };
			for (const char * p : particles)
				if (w == p) return true;
			return false;
		}

		//! Not people: "-choir-", "(playback)", "supuestamente".
		bool is_vocal_note(const std::string & w)
		{
			static const char * const notes[] = {
				"choir", "coro", "coros", "playback", "supuestamente", "estribillo", "estribillista",
				"recitado", "recita", "y", "con", "and",
			};
			for (const char * n : notes)
				if (w == n) return true;
			return false;
		}

		//! The genres that, on a track, contradict one another.
		std::string main_genre(const std::string & genre)
		{
			const std::vector<std::string> words = fold_words(genre);
			for (const std::string & w : words)
			{
				if (w == "tango") return "tango";
				if (w == "vals" || w == "valse" || w == "valsecito" || w == "waltz") return "vals";
				if (w == "milonga" || w == "milongon" || w == "milongas") return "milonga";
				if (w == "arr" || w == "arreglo" || w == "en") continue;
				return std::string();
			}
			return std::string();
		}

		bool is_digit(char c) { return c >= '0' && c <= '9'; }

		void append_unique(std::vector<std::string> & to, const std::vector<std::string> & from)
		{
			for (const std::string & k : from)
				if (!contains(to, k)) to.push_back(k);
		}

		//! title_keys, of the whole title and of each side of "N.P. | No placé"
		//! or "Palais de glace / Palé de glas"; and the same with the numbers
		//! spelled out.
		std::vector<std::string> keys_with_numbers(const std::string & title)
		{
			std::vector<std::string> keys = title_keys(title);
			// title_keys drops numbers, but a whole title can be one: De
			// Caro's "1937".
			const std::string whole = fold_key(title);
			if (!whole.empty() && std::all_of(whole.begin(), whole.end(), is_digit)) append_unique(keys, { whole });
			for (const char * separator : { " | ", " / " })
			{
				std::size_t start = 0;
				for (std::size_t at; (at = title.find(separator, start)) != std::string::npos; start = at + 3)
					append_unique(keys, title_keys(title.substr(start, at - start)));
				if (start > 0) append_unique(keys, title_keys(title.substr(start)));
			}
			const std::string spelled = spell_numbers(title);
			if (!spelled.empty()) append_unique(keys, keys_with_numbers(spelled));
			return keys;
		}

		//! A piece of a title or a file name that says what the recording is
		//! rather than what it is called.
		bool is_genre_key(const std::string & k)
		{
			static const char * const genres[] = {
				"tango", "vals", "valse", "milonga", "foxtrot", "ranchera", "candombe", "pasodoble", "polca",
				"instrumental", "tangomilonga", "milongacandombe",
			};
			for (const char * g : genres)
				if (k == g) return true;
			return false;
		}

		//! What the track says, as folded words and, for "Di Sarli" written
		//! "Disarli", adjacent pairs joined.
		struct words_of
		{
			std::unordered_set<std::string> tokens;
			std::vector<std::string> words;

			void add(const std::string & text)
			{
				const std::vector<std::string> w = fold_words(text);
				for (std::size_t i = 0; i < w.size(); i++)
				{
					tokens.insert(w[i]);
					words.push_back(w[i]);
					if (i + 1 < w.size()) tokens.insert(w[i] + w[i + 1]);
				}
			}

			//! Exactly, or - for six letters and more, where a slip is a slip
			//! and not another name - one edit off.
			bool has(const std::string & token) const
			{
				if (tokens.count(token) != 0) return true;
				if (token.size() < 6) return false;
				for (const std::string & w : tokens)
				{
					const std::size_t a = w.size(), b = token.size();
					if ((a > b ? a - b : b - a) > 1) continue;
					if (edit_distance(w, token, 1) <= 1) return true;
				}
				return false;
			}

			bool has_exact(const std::string & token) const { return tokens.count(token) != 0; }
		};

		struct track_context
		{
			words_of strong;   //!< artist fields, performers, the file name and its folders, the title
			words_of weak;     //!< album, comment
			std::vector<date_parts> dates;
			std::string genre;
			std::vector<std::string> title_keys;   //!< from TITLE
			std::vector<std::string> file_keys;    //!< from the file name
			bool says_instrumental = false;
			std::vector<std::string> qualifiers;
		};

		//! The file name without its extension, and the names of the two
		//! folders above it.
		void split_path(const std::string & path, std::string & stem, std::string & folders)
		{
			std::vector<std::string> parts;
			std::string cur;
			for (char c : path)
			{
				if (c == '/' || c == '\\') { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
				else cur += c;
			}
			if (!cur.empty()) parts.push_back(cur);
			if (parts.empty()) return;
			stem = parts.back();
			const std::size_t dot = stem.rfind('.');
			if (dot != std::string::npos && stem.size() - dot <= 6) stem.erase(dot);
			for (std::size_t i = parts.size() - 1, n = 0; i-- > 0 && n < 2; n++)
				folders += parts[i] + " / ";
		}
	}

	date_parts parse_date(const std::string & date)
	{
		date_parts d;
		std::size_t i = 0;
		auto number = [&](std::size_t max_digits) -> int
		{
			int v = 0;
			std::size_t n = 0;
			while (i < date.size() && is_digit(date[i]) && n < max_digits) { v = v * 10 + (date[i++] - '0'); n++; }
			return n == 0 ? -1 : v;
		};
		const int y = number(4);
		if (y < 1000) return d;
		d.year = y;
		if (i < date.size() && date[i] == '-')
		{
			i++;
			const int m = number(2);
			if (m >= 1 && m <= 12)
			{
				d.month = m;
				if (i < date.size() && date[i] == '-')
				{
					i++;
					const int dd = number(2);
					if (dd >= 1 && dd <= 31) d.day = dd;
				}
			}
		}
		return d;
	}

	std::vector<date_parts> find_dates(const std::string & text)
	{
		std::vector<date_parts> out;
		const std::size_t n = text.size();

		// Digit runs: [start, end).
		struct run { std::size_t start, end; int value; };
		std::vector<run> runs;
		for (std::size_t i = 0; i < n;)
		{
			if (!is_digit(text[i])) { i++; continue; }
			run r{ i, i, 0 };
			while (i < n && is_digit(text[i])) { if (i - r.start < 9) r.value = r.value * 10 + (text[i] - '0'); i++; }
			r.end = i;
			runs.push_back(r);
		}

		// A separator between the parts of a date: one to six bytes, none of
		// them a digit, a letter or a space - '-', '.', '/', an en dash, or an
		// en dash gone through the wrong code page.
		auto separator = [&](std::size_t from, std::size_t to)
		{
			if (to <= from || to - from > 6) return false;
			for (std::size_t i = from; i < to; i++)
			{
				const unsigned char c = static_cast<unsigned char>(text[i]);
				if (c == ' ' || c == '\t' || (c < 0x80 && std::isalnum(c))) return false;
			}
			return true;
		};
		auto add = [&](int y, int m, int d)
		{
			for (const date_parts & e : out)
				if (e.year == y && e.month == m && e.day == d) return;
			out.push_back({ y, m, d });
		};

		std::vector<bool> used(runs.size(), false);
		for (std::size_t k = 0; k < runs.size(); k++)
		{
			const run & r = runs[k];
			if (r.end - r.start != 4 || r.value < 1900 || r.value > 2029) continue;
			// year-month-day, year-month
			if (k + 1 < runs.size() && separator(r.end, runs[k + 1].start) && runs[k + 1].end - runs[k + 1].start <= 2 &&
			    runs[k + 1].value >= 1 && runs[k + 1].value <= 12)
			{
				const int m = runs[k + 1].value;
				if (k + 2 < runs.size() && separator(runs[k + 1].end, runs[k + 2].start) &&
				    runs[k + 2].end - runs[k + 2].start <= 2 && runs[k + 2].value >= 1 && runs[k + 2].value <= 31)
				{
					add(r.value, m, runs[k + 2].value);
					used[k] = used[k + 1] = used[k + 2] = true;
					continue;
				}
				add(r.value, m, 0);
				used[k] = used[k + 1] = true;
				continue;
			}
			// day-month-year
			if (k >= 2 && !used[k - 1] && !used[k - 2] && separator(runs[k - 1].end, r.start) &&
			    separator(runs[k - 2].end, runs[k - 1].start) && runs[k - 1].end - runs[k - 1].start <= 2 &&
			    runs[k - 2].end - runs[k - 2].start <= 2 && runs[k - 1].value >= 1 && runs[k - 1].value <= 12 &&
			    runs[k - 2].value >= 1 && runs[k - 2].value <= 31)
			{
				add(r.value, runs[k - 1].value, runs[k - 2].value);
				used[k] = true;
				continue;
			}
			add(r.value, 0, 0);
			used[k] = true;
		}
		return out;
	}

	namespace
	{
		//! The surname, with a particle joined to it ("di sarli" -> "disarli",
		//! which is also what "Disarli" and "Di Sarli" are looked up as), and
		//! the rest as given names.
		template <class Person>
		Person make_person(const std::vector<std::string> & words)
		{
			Person p;
			if (words.empty()) return p;
			std::size_t last = words.size() - 1;
			std::string surname = words[last];
			if (last > 0 && is_particle(words[last - 1]))
			{
				surname = words[last - 1] + surname;
				last--;
			}
			p.surname.push_back(surname);
			for (std::size_t i = 0; i < last; i++)
				if (words[i].size() >= 3 && !is_particle(words[i])) p.given.push_back(words[i]);
			return p;
		}
	}

	recording_matcher::recording_matcher(const discography & d) : m_data(d)
	{
		m_orchestras.resize(d.orchestras.size());
		m_by_orchestra.resize(d.orchestras.size());
		for (std::size_t o = 0; o < d.orchestras.size(); o++)
		{
			orchestra_model & m = m_orchestras[o];
			std::string inside;
			const std::string outside = strip_brackets(d.orchestras[o], &inside);

			// "Quartet Troilo y Grela", "Enrique Francini, Armando Pontier":
			// each leader on his own.
			std::string list = outside;
			for (std::size_t y; (y = list.find(" y ")) != std::string::npos;) list.replace(y, 3, ",");
			std::size_t start = 0;
			while (start <= list.size())
			{
				std::size_t end = list.find(',', start);
				if (end == std::string::npos) end = list.size();
				std::vector<std::string> words;
				for (const std::string & w : fold_words(list.substr(start, end - start)))
				{
					if (const char * q = qualifier(w)) { if (!contains(m.qualifiers, q)) m.qualifiers.push_back(q); }
					else if (!is_filler(w)) words.push_back(w);
				}
				if (!words.empty())
				{
					person p = make_person<person>(words);
					// "Orquesta Típica Víctor", "Orquesta Típica Porteña",
					// "Orquesta Victor Popular": named after no one, and
					// "Victor" alone is a record label too. The two words
					// go together.
					const bool tipica = contains(words, "tipica"), victor = contains(words, "victor");
					if ((tipica || victor) && words.size() == 2)
					{
						p.surname = { words[0] + words[1] };
						p.given.clear();
						if (tipica && victor) p.surname.push_back("otv");
					}
					m.leaders.push_back(p);
				}
				start = end + 1;
			}
			std::vector<std::string> dir;
			for (const std::string & w : fold_words(inside))
				if (!is_filler(w)) dir.push_back(w);
			if (!dir.empty()) m.director = make_person<person>(dir).surname;
			m_names.insert(fold_key(d.orchestras[o]));
			m_names.insert(fold_key(outside));
		}

		m_singers.resize(d.recordings.size());
		m_keys.resize(d.recordings.size());
		for (std::size_t i = 0; i < d.recordings.size(); i++)
		{
			const recording & r = d.recordings[i];
			const int index = static_cast<int>(i);
			m_by_orchestra[r.orchestra].push_back(index);

			for (const std::string & s : singers_of(r.vocal))
			{
				std::vector<std::string> words;
				for (const std::string & w : fold_words(strip_brackets(s)))
					if (!is_vocal_note(w)) words.push_back(w);
				if (words.empty()) continue;
				person p = make_person<person>(words);
				m_singers[i].push_back(p);
				m_names.insert(fold_key(s));
				std::vector<person> & all = m_orchestras[r.orchestra].singers;
				if (std::none_of(all.begin(), all.end(), [&](const person & q) { return q.surname == p.surname; }))
					all.push_back(p);
			}

			// The title, and each side of "Canción de amor | La Chanson
			// d'Amour", with and without brackets.
			std::vector<std::string> & keys = m_keys[i];
			std::string rest = r.name;
			std::vector<std::string> names;
			for (std::size_t bar; (bar = rest.find(" | ")) != std::string::npos;)
			{
				names.push_back(rest.substr(0, bar));
				rest = rest.substr(bar + 3);
			}
			names.push_back(rest);
			for (const std::string & n : names)
			{
				const std::string spelled = spell_numbers(n);
				for (const std::string & k : std::initializer_list<std::string>{
				         fold_key(n), fold_key(strip_brackets(n)), fold_key(spelled), fold_key(strip_brackets(spelled)) })
					if (k.size() >= 2 && !contains(keys, k)) keys.push_back(k);
			}
			for (const std::string & k : keys)
			{
				std::vector<int> & list = m_exact[k];
				if (list.empty() || list.back() != index) list.push_back(index);
			}
		}
	}

	track_match recording_matcher::find(const track_tags & tags) const
	{
		track_context ctx;
		std::string stem, folders;
		split_path(tags.path, stem, folders);
		for (const std::string * s : std::initializer_list<const std::string *>{ &tags.artist, &tags.album_artist, &tags.performers, &stem, &folders, &tags.title })
			ctx.strong.add(*s);
		ctx.weak.add(tags.album);
		ctx.weak.add(tags.comment);
		for (const std::string * s : std::initializer_list<const std::string *>{ &tags.dates, &tags.comment, &tags.title, &stem, &folders })
			for (const date_parts & dp : find_dates(*s))
				if (std::none_of(ctx.dates.begin(), ctx.dates.end(), [&](const date_parts & e)
				                 { return e.year == dp.year && e.month == dp.month && e.day == dp.day; }))
					ctx.dates.push_back(dp);
		ctx.genre = main_genre(tags.genre);
		ctx.says_instrumental = ctx.strong.has_exact("instrumental") || ctx.weak.has_exact("instrumental") ||
		                        ctx.strong.has_exact("instr");
		for (const std::string & w : ctx.strong.words)
			if (const char * q = qualifier(w))
				if (!contains(ctx.qualifiers, q)) ctx.qualifiers.push_back(q);
		ctx.title_keys = keys_with_numbers(tags.title);
		ctx.file_keys = keys_with_numbers(stem);
		// "Tango", "Instrumental", "Osvaldo Fresedo": pieces of a file name
		// or a title that say what it is or who plays it, not what it is
		// called. Not the whole title, though: "Valsecito" and "Julián
		// Centeya" are songs.
		using keyed_text = std::pair<std::vector<std::string> *, const std::string *>;
		for (const keyed_text & keys : { keyed_text(&ctx.title_keys, &tags.title), keyed_text(&ctx.file_keys, &stem) })
		{
			const std::string whole = fold_key(*keys.second);
			const std::string whole_outside = fold_key(strip_brackets(*keys.second));
			keys.first->erase(std::remove_if(keys.first->begin(), keys.first->end(), [&](const std::string & k)
			{
				if (k == whole || k == whole_outside) return false;
				return is_genre_key(k) || m_names.count(k) != 0;
			}), keys.first->end());
		}

		// --- orchestras named on the track ---------------------------------
		auto named = [&](const person & p, const words_of & w)
		{
			for (const std::string & s : p.surname)
				if (w.has(s)) return true;
			return false;
		};
		auto given_named = [&](const person & p)
		{
			for (const std::string & g : p.given)
				if (ctx.strong.has_exact(g) || ctx.weak.has_exact(g)) return true;
			return false;
		};
		std::vector<int> orchestra_evidence(m_orchestras.size(), 0);
		bool any_orchestra = false;
		for (std::size_t o = 0; o < m_orchestras.size(); o++)
		{
			const orchestra_model & m = m_orchestras[o];
			if (m.leaders.empty()) continue;
			std::size_t strong = 0, weak = 0;
			bool given = false;
			for (const person & p : m.leaders)
			{
				if (named(p, ctx.strong)) strong++;
				else if (named(p, ctx.weak)) weak++;
				given = given || given_named(p);
			}
			int e = 0;
			if (strong == m.leaders.size()) e = 2;
			else if (strong + weak == m.leaders.size() || strong > 0) e = 1;
			if (e == 0) continue;
			if (given) e++;
			// A sextet against an orchestra of the same leader.
			bool qualified = false;
			for (const std::string & q : m.qualifiers) qualified = qualified || contains(ctx.qualifiers, q);
			if (qualified) e++;
			else if (m.qualifiers.empty() && !ctx.qualifiers.empty()) e--;
			for (const std::string & dsur : m.director)
				if (ctx.strong.has(dsur) || ctx.weak.has(dsur)) { e++; break; }
			orchestra_evidence[o] = std::max(e, 1);
			any_orchestra = true;
		}
		// The Orquesta Típica Víctor under Adolfo Carabelli recorded much of
		// what Carabelli's own orchestra did, and labels mix them up: naming
		// one is a hint at the other.
		for (std::size_t o = 0; o < m_orchestras.size(); o++)
		{
			if (orchestra_evidence[o] < 2 || m_orchestras[o].director.empty()) continue;
			for (std::size_t other = 0; other < m_orchestras.size(); other++)
				for (const person & l : m_orchestras[other].leaders)
					if (l.surname == m_orchestras[o].director && orchestra_evidence[other] == 0)
						orchestra_evidence[other] = 1;
		}

		// --- recordings by title -------------------------------------------
		std::unordered_map<int, int> title_score;   // recording -> best title score
		auto note = [&](int r, int score)
		{
			int & s = title_score[r];
			s = std::max(s, score);
		};
		auto exact = [&](const std::vector<std::string> & keys, int score)
		{
			for (const std::string & k : keys)
			{
				const auto it = m_exact.find(k);
				if (it == m_exact.end()) continue;
				for (int r : it->second) note(r, score);
			}
		};
		exact(ctx.title_keys, 10);
		exact(ctx.file_keys, 9);

		// Near titles, only among the recordings of the orchestras the track
		// names: there the misspellings are worth catching and the songs few
		// enough that a near title is not someone else's.
		auto near_limit = [](std::size_t len) { return len >= 18 ? 3 : len >= 10 ? 2 : len >= 6 ? 1 : 0; };
		for (std::size_t o = 0; o < m_orchestras.size(); o++)
		{
			if (orchestra_evidence[o] < 2) continue;
			for (int r : m_by_orchestra[o])
			{
				int best = 0;
				for (const std::vector<std::string> * keys : std::initializer_list<const std::vector<std::string> *>{ &ctx.title_keys, &ctx.file_keys })
					for (const std::string & tk : *keys)
						for (const std::string & rk : m_keys[r])
						{
							const std::size_t shorter = std::min(tk.size(), rk.size());
							const int limit = near_limit(shorter);
							if (limit > 0)
							{
								const int dist = edit_distance(tk, rk, limit);
								if (dist <= limit) best = std::max(best, 10 - 2 * dist - (keys == &ctx.file_keys ? 1 : 0));
							}
							// "Que no sepan las estrellas" for "No sepan las
							// estrellas": one title inside the other. Not
							// from the file name, whose pieces are names too
							// ("Palabras de Osvaldo Fresedo").
							const std::size_t longer = std::max(tk.size(), rk.size());
							if (keys == &ctx.title_keys && shorter >= 8 && shorter * 2 >= longer &&
							    (tk.find(rk) != std::string::npos || rk.find(tk) != std::string::npos))
								best = std::max(best, 5);
						}
				if (best > 0) note(r, best);
			}
		}

		// --- evidence for each -----------------------------------------------
		track_match result;
		for (const auto & ts : title_score)
		{
			const int r = ts.first;
			const recording & rec = m_data.recordings[r];
			recording_match m;
			m.recording = r;
			m.title = ts.second;
			m.orchestra = orchestra_evidence[rec.orchestra];

			// Singers. A surname that is also a word of the title - Alberto
			// Amor singing "Amor y celos" - only counts with the first name.
			const std::vector<std::string> title_words = fold_words(rec.name);
			auto singer_named = [&](const person & p)
			{
				for (const std::string & s : p.surname)
				{
					if (!ctx.strong.has(s) && !ctx.weak.has(s)) continue;
					if (!contains(title_words, s)) return true;
					if (given_named(p)) return true;
				}
				return false;
			};
			const orchestra_model & om = m_orchestras[rec.orchestra];
			auto other_singer_named = [&](const std::vector<person> & own)
			{
				for (const person & p : om.singers)
				{
					if (std::any_of(own.begin(), own.end(), [&](const person & q) { return q.surname == p.surname; }))
						continue;
					// The leader's own name is not another singer.
					if (std::any_of(om.leaders.begin(), om.leaders.end(),
					                [&](const person & l) { return l.surname == p.surname; }))
						continue;
					if (singer_named(p)) return true;
				}
				return false;
			};
			const std::vector<person> & singers = m_singers[r];
			if (singers.empty())
			{
				if (ctx.says_instrumental) m.vocal = 2;
				else if (other_singer_named(singers)) m.vocal = -3;
			}
			else
			{
				std::size_t found = 0;
				bool given = false;
				for (const person & p : singers)
					if (singer_named(p)) { found++; given = given || given_named(p); }
				if (found == singers.size()) m.vocal = given ? 4 : 3;
				else if (found > 0) m.vocal = 1;
				else if (ctx.says_instrumental || other_singer_named(singers)) m.vocal = -3;
			}

			// Date.
			const date_parts rd = parse_date(rec.date);
			if (rd.year != 0 && !ctx.dates.empty())
			{
				int best = -100;
				for (const date_parts & td : ctx.dates)
				{
					int e;
					if (td.year == rd.year)
					{
						if (td.month != 0 && rd.month != 0 && td.month != rd.month) e = 1;
						else if (td.day != 0 && rd.day != 0) e = td.month == rd.month && td.day == rd.day ? 5 : 1;
						else if (td.month != 0 && rd.month != 0) e = 3;
						else e = 2;
					}
					else if (td.year - rd.year == 1 || rd.year - td.year == 1) e = 0;
					// A year after 1990 on an old recording is the reissue's.
					else if (td.year > 1990 && rd.year <= 1990) e = 0;
					else e = -2;
					best = std::max(best, e);
				}
				m.date = best;
			}

			// Genre.
			const std::string rg = main_genre(rec.genre);
			if (!ctx.genre.empty() && !rg.empty()) m.genre = ctx.genre == rg ? 1 : -1;

			m.score = m.title + 3 * m.orchestra + m.vocal + m.date + m.genre;
			result.candidates.push_back(m);
		}

		// Without the orchestra a title is just a song, recorded by dozens.
		// When the track names one, recordings of the others go. When it
		// names none - "_elcorazonmeengano.flac" in a folder "78rpm" - every
		// recording of exactly that title is offered, the singer and the year
		// putting the likeliest first; confident it never is (see below). Not
		// when the artist fields name someone the discographies do not know -
		// Francisco Canaro's "Naipe" is not D'Arienzo's.
		const bool artist_unknown = fold_key(tags.artist + tags.album_artist + tags.performers).empty();
		result.candidates.erase(std::remove_if(result.candidates.begin(), result.candidates.end(),
			[&](const recording_match & m)
			{
				if (m.orchestra > 0) return false;
				return any_orchestra || !artist_unknown || m.title < 9;
			}), result.candidates.end());

		std::sort(result.candidates.begin(), result.candidates.end(), [](const recording_match & a, const recording_match & b)
		{
			if (a.score != b.score) return a.score > b.score;
			return a.recording < b.recording;
		});
		if (result.candidates.size() > max_candidates) result.candidates.resize(max_candidates);

		if (!result.candidates.empty())
		{
			const recording_match & best = result.candidates.front();
			bool clear_lead = result.candidates.size() == 1 || result.candidates[1].score <= best.score - 3;
			// Another candidate with the track's date or singers where the
			// best has not: the track contradicts itself, or the
			// discographies do. Either way it is the user's call.
			for (std::size_t i = 1; i < result.candidates.size() && clear_lead; i++)
			{
				const recording_match & c = result.candidates[i];
				if ((c.date > best.date && c.date >= 2) || (c.vocal > best.vocal && c.vocal >= 3)) clear_lead = false;
			}
			// The genre is the weakest evidence: tracks say "Tango" of a
			// milonga often enough. It only casts doubt without the date.
			result.confident = best.title >= 8 && best.orchestra >= 2 && best.vocal >= 0 && best.date >= 0 &&
			                   (best.genre >= 0 || best.date >= 5) && clear_lead;
		}
		return result;
	}

	std::string recording_matcher::evidence_text(const recording_match & m)
	{
		std::vector<std::string> parts;
		parts.push_back(m.title >= 9 ? "title" : m.title >= 6 ? "similar title" : "part of title");
		if (m.orchestra >= 2) parts.push_back("orchestra");
		else if (m.orchestra == 1) parts.push_back("orchestra?");
		else parts.push_back("no orchestra");
		if (m.vocal >= 3) parts.push_back("singer");
		else if (m.vocal == 2) parts.push_back("instrumental");
		else if (m.vocal == 1) parts.push_back("a singer");
		else if (m.vocal < 0) parts.push_back("singer differs");
		if (m.date >= 5) parts.push_back("date");
		else if (m.date >= 3) parts.push_back("month");
		else if (m.date >= 1) parts.push_back("year");
		else if (m.date < 0) parts.push_back("year differs");
		if (m.genre > 0) parts.push_back("genre");
		else if (m.genre < 0) parts.push_back("genre differs");
		std::string out;
		for (const std::string & p : parts)
		{
			if (!out.empty()) out += ", ";
			out += p;
		}
		return out;
	}
}
