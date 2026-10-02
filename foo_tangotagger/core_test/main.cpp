// core_test: the embedded lyrics and discographies and the matching, without
// a host.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "disco_fingerprint.h"
#include "disco_match.h"
#include "fingerprint.h"
#include "disco_tags.h"
#include "lyrics_db.h"
#include "text_links.h"
#include "title_match.h"

using namespace tangotagger;

namespace
{
	int g_failures = 0;
	int g_checks = 0;

	void check(bool ok, const std::string & what)
	{
		g_checks++;
		if (!ok)
		{
			g_failures++;
			std::printf("FAIL: %s\n", what.c_str());
		}
	}

	const char * kind_name(match_kind k)
	{
		switch (k)
		{
		case match_kind::exact:   return "exact";
		case match_kind::similar: return "similar";
		default:                  return "none";
		}
	}

	std::string describe(const matcher & m, const std::string & title)
	{
		const title_match r = m.find(title);
		std::string s = "\"" + title + "\" -> " + kind_name(r.kind);
		for (const match_candidate & c : r.candidates) s += " \"" + m.songs()[c.song].file_name + "\"";
		return s;
	}

	bool has_candidate(const title_match & r, const matcher & m, const std::string & file_name)
	{
		for (const match_candidate & c : r.candidates)
			if (m.songs()[c.song].file_name == file_name) return true;
		return false;
	}

	//! `title` matches the song whose file name is `file_name`, as `kind`.
	void expect(const matcher & m, const std::string & title, const std::string & file_name, match_kind kind)
	{
		const title_match r = m.find(title);
		const bool ok = r.kind == kind && has_candidate(r, m, file_name);
		check(ok, describe(m, title) + ", expected " + kind_name(kind) + " \"" + file_name + "\"");
	}

	void expect_none(const matcher & m, const std::string & title)
	{
		check(m.find(title).kind == match_kind::none, describe(m, title) + ", expected none");
	}

	std::vector<std::string> all_keys(const song & s)
	{
		std::vector<std::string> aliases;
		std::vector<std::string> keys = song_keys(s, &aliases);
		keys.insert(keys.end(), aliases.begin(), aliases.end());
		return keys;
	}

	song make_song(const std::string & name)
	{
		song s;
		s.name = name;
		s.file_name = name;
		s.text = "la la la";
		return s;
	}
}

namespace
{
	// --- fingerprints ---------------------------------------------------------------
	//
	// A made-up piece - a melody of chords and a performance's onsets - rendered
	// the way two transfers of it would differ: speed (and with it the tuning
	// offset bpmcore would measure), where the file starts, noise; and as a
	// second performance of the same arrangement, its rubato different.

	//! A portable generator: the same numbers on every platform.
	struct lcg
	{
		std::uint32_t s;
		double next() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0; }
	};

	struct piece
	{
		std::vector<std::array<int, 3>> chords;   // one every half second
		std::vector<double> onsets;               // seconds
		std::vector<double> strengths;
		double length = 0;
	};

	piece make_piece(std::uint32_t seed, double length)
	{
		lcg r{ seed };
		piece p;
		p.length = length;
		for (double t = 0; t < length; t += 0.5)
			p.chords.push_back({ static_cast<int>(r.next() * 12), static_cast<int>(r.next() * 12),
			                     static_cast<int>(r.next() * 12) });
		for (double t = 0.3; t < length; t += 0.15 + r.next() * 0.45)
		{
			p.onsets.push_back(t);
			p.strengths.push_back(0.3 + r.next());
		}
		return p;
	}

	//! A second performance: the same notes, the timing drifting by up to
	//! `drift` seconds as rubato does.
	piece reperform(const piece & p, std::uint32_t seed, double drift)
	{
		lcg r{ seed };
		piece q = p;
		double walk = 0;
		for (double & t : q.onsets)
		{
			walk = std::max(-drift, std::min(drift, walk + (r.next() - 0.5) * drift * 0.5));
			t += walk;
		}
		return q;
	}

	//! The features of `p` as a transfer `speed` times too fast whose file
	//! starts `trim` seconds into the music (after a second of silence).
	audio_features render(const piece & p, double speed, double trim, std::uint32_t noise_seed)
	{
		lcg noise{ noise_seed };
		audio_features f;
		f.chroma_hop = 2048.0 / 22050.0;
		f.novelty_rate = 22050.0 / 256.0;
		// The offset a speed shows as, folded into +/-50 cents.
		double cents = 1200.0 * std::log2(speed);
		cents -= 100.0 * std::round(cents / 100.0);
		f.tuning_cents = cents;
		const int semis = static_cast<int>(std::lround(1200.0 * std::log2(speed) / 100.0 - cents / 100.0));
		f.silence = 0.5f;
		const double lead = 1.0;   // silence before the music, in file seconds
		const double file_length = lead + (p.length - trim) / speed;
		const std::size_t frames = static_cast<std::size_t>(file_length / f.chroma_hop);
		for (std::size_t i = 0; i < frames; i++)
		{
			const double t = i * f.chroma_hop;
			const double music = (t - lead) * speed + trim;   // where in the piece
			std::array<float, 12> c{};
			float loud = 0;
			if (t >= lead && music < p.length)
			{
				loud = 1;
				const std::array<int, 3> & chord = p.chords[std::min(p.chords.size() - 1, static_cast<std::size_t>(music / 0.5))];
				for (int k = 0; k < 3; k++) c[((chord[k] + semis) % 12 + 12) % 12] += 1.0f - 0.25f * k;
				for (float & v : c) v += static_cast<float>(noise.next() * 0.15);
			}
			f.chroma.insert(f.chroma.end(), c.begin(), c.end());
			f.loudness.push_back(loud);
		}
		f.novelty.assign(static_cast<std::size_t>(file_length * f.novelty_rate), 0.0f);
		for (float & v : f.novelty) v = static_cast<float>(noise.next() * 0.05);
		for (std::size_t i = 0; i < p.onsets.size(); i++)
		{
			if (p.onsets[i] < trim) continue;
			const double at = (lead + (p.onsets[i] - trim) / speed) * f.novelty_rate;
			for (int d = -2; d <= 2; d++)
			{
				const long j = static_cast<long>(std::lround(at)) + d;
				if (j >= 0 && j < static_cast<long>(f.novelty.size()))
					f.novelty[j] += static_cast<float>(p.strengths[i] * std::exp(-0.5 * d * d));
			}
		}
		return f;
	}

	void fingerprint_tests(const discography & disco)
	{
		for (std::size_t n = 0; n < 7; n++)
		{
			std::string bytes;
			for (std::size_t i = 0; i < n; i++) bytes += static_cast<char>(i * 97 + 3);
			std::string back;
			check(from_base64(to_base64(bytes), back) && back == bytes, "base64 round trip, " + std::to_string(n) + " bytes");
		}

		const piece a = make_piece(1, 170), b = make_piece(2, 165);
		const fingerprint ref = make_fingerprint(render(a, 1.0, 0, 10), fingerprint_excerpt_seconds);
		check(!ref.empty() && ref.bins() == static_cast<std::size_t>(fingerprint_excerpt_seconds / 0.25),
		      "a reference keeps the excerpt");
		check(ref.onsets.size() > 300 && ref.onsets.size() <= 360, "about four onsets a second");
		fingerprint decoded;
		const std::string enc = encode_fingerprint(ref);
		check(decode_fingerprint(enc, decoded) && encode_fingerprint(decoded) == enc && decoded.chroma == ref.chroma &&
		          decoded.onsets.size() == ref.onsets.size() && decoded.tuning == ref.tuning,
		      "fingerprint encode/decode round trip");
		check(enc.size() < 2200, "a reference is about 2KB: " + std::to_string(enc.size()));
		check(!decode_fingerprint(enc.substr(0, enc.size() - 1), decoded), "a truncated fingerprint is refused");

		fingerprint_index index;
		index.add(100, ref);
		index.add(200, make_fingerprint(render(b, 1.0, 0, 11), fingerprint_excerpt_seconds));

		auto best = [&](const audio_features & f) { return index.identify(make_fingerprint(f, 0)); };
		auto describe_fp = [](const std::vector<fingerprint_match> & m)
		{
			std::string s;
			for (const fingerprint_match & x : m)
				s += " [" + std::to_string(x.id) + " onset " + std::to_string(x.onset) + " wander " +
				     std::to_string(x.wander_ms) + " speed " + std::to_string(x.speed) + "]";
			return s;
		};
		{
			const std::vector<fingerprint_match> m = best(render(a, 1.0, 0, 20));
			check(!m.empty() && m[0].id == 100 && m[0].identified(), "the same transfer again:" + describe_fp(m));
		}
		{
			// 2.5% fast, the first 4 seconds cut, other noise.
			const std::vector<fingerprint_match> m = best(render(a, 1.025, 4, 21));
			check(!m.empty() && m[0].id == 100 && m[0].identified() && std::fabs(m[0].speed - 1.025) < 0.005,
			      "a faster, trimmed transfer:" + describe_fp(m));
		}
		{
			// 4% slow: past the semitone wrap from the reference.
			const std::vector<fingerprint_match> m = best(render(a, 0.96, 0, 22));
			check(!m.empty() && m[0].id == 100 && m[0].identified() && std::fabs(m[0].speed - 0.96) < 0.005,
			      "a slower transfer, across the semitone wrap:" + describe_fp(m));
		}
		{
			const std::vector<fingerprint_match> m = best(render(b, 1.01, 2, 23));
			check(!m.empty() && m[0].id == 200 && m[0].identified(), "the other piece:" + describe_fp(m));
		}
		{
			const std::vector<fingerprint_match> m = best(render(make_piece(3, 168), 1.0, 0, 24));
			check(m.empty() || !m[0].identified(), "a piece with no reference is not identified:" + describe_fp(m));
		}
		{
			const std::vector<fingerprint_match> m = best(render(reperform(a, 5, 0.12), 1.0, 0, 25));
			check(m.empty() || !m[0].identified(), "a second performance is not identified:" + describe_fp(m));
		}

		// Merging into a track's match: what the sound identifies goes first
		// and is confident; probable goes first among the rest, unchecked.
		if (disco.recordings.size() >= 3)
		{
			track_match tm;
			recording_match by_tags;
			by_tags.recording = 0;
			by_tags.title = 10;
			tm.candidates.push_back(by_tags);
			fingerprint_match sure;
			sure.id = 2;
			sure.onset = 0.95;
			sure.wander_ms = 5;
			fingerprint_match maybe = sure;
			maybe.id = 1;
			maybe.onset = 0.75;

			track_match t1 = tm;
			add_sound(t1, { sure });
			check(t1.confident && t1.candidates.size() == 2 && t1.candidates[0].recording == 2 &&
			          t1.candidates[0].sound == 95 && t1.candidates[0].sound_identified,
			      "identified by sound: first, confident");
			check(recording_matcher::evidence_text(t1.candidates[0]) == "sound 0.95", "evidence of the sound alone");

			track_match t2 = tm;
			add_sound(t2, { maybe });
			check(!t2.confident && t2.candidates[0].recording == 1 && !t2.candidates[0].sound_identified,
			      "probable by sound: first, not confident");

			track_match t3 = tm;
			fingerprint_match also_sure = sure;
			also_sure.id = 0;
			add_sound(t3, { sure, also_sure });
			check(!t3.confident, "two recordings identified by sound: the user's call");

			track_match t4 = tm;
			fingerprint_match wandering = sure;
			wandering.wander_ms = 40;
			add_sound(t4, { wandering });
			check(!t4.confident && t4.candidates[0].sound > 0, "agreeing onsets off a straight line: probable");
		}

		// The embedded fingerprints, when this build has them.
		if (!disco.fingerprints.empty())
		{
			std::size_t bad = 0;
			for (const recording_fingerprint & f : disco.fingerprints)
			{
				fingerprint fp;
				if (!decode_fingerprint(f.data, fp) || fp.empty()) bad++;
			}
			check(bad == 0, std::to_string(bad) + " embedded fingerprints do not decode");
			check(embedded_fingerprints().size() == disco.fingerprints.size(), "every embedded fingerprint indexed");
			std::printf("%zu fingerprints embedded\n", disco.fingerprints.size());
		}
	}
}

int main()
{
	// --- folding --------------------------------------------------------------
	check(fold_key("Fueron Tres Años") == fold_key("Fueron tres anos"), "Años folds to anos");
	check(fold_key("Fueron Tres Años") == "fuerontresanos", "fuerontresanos");
	check(fold_key("Volverás... ¿Pero cuándo?") == "volverasperocuando", "punctuation dropped");
	check(fold_key("Yira, yira") == fold_key("Yira yira"), "comma ignored");
	check(fold_key("Pa' que seguir") == fold_key("Pa que seguir"), "apostrophe ignored");
	check(fold_key("¡Qué noche!") == "quenoche", "¡ ! ignored");
	check(fold_key("Por qué") == fold_key("Porque"), "spaces ignored");
	check(fold_key("Сuatro palabras") == "cuatropalabras", "Cyrillic look-alike folded");
	// "Años" in NFD, as a macOS file name has it: n + U+0303 combining tilde.
	check(fold_key("An\xCC\x83os") == "anos", "combining mark dropped");
	check(fold_key("Frou-Frou") == "froufrou", "hyphen inside a word");
	check(fold_key("Straße Œuvre") == "strasseoeuvre", "two-letter folds");

	check(edit_distance("abcdef", "abcdef", 1) == 0, "distance 0");
	check(edit_distance("abcdef", "abdcef", 1) == 1, "transposition costs 1");
	check(edit_distance("abcdef", "abcxyz", 1) == 2, "distance capped at limit + 1");

	// --- the embedded data -----------------------------------------------------
	const std::vector<song> & songs = embedded_songs();
	std::printf("%zu songs embedded\n", songs.size());
	check(songs.size() >= 200, "at least 200 songs embedded");
	for (const song & s : songs)
	{
		check(!s.name.empty() && !s.file_name.empty(), "song has a name: " + s.file_name);
		check(s.text.size() > 50, "song has lyrics: " + s.file_name);
		check(s.text.find('\r') == std::string::npos, "LF line endings: " + s.file_name);
		if (!s.link.empty())
		{
			// The panel shows the source as a link; it has to be found as one.
			const std::vector<text_link> links = find_links(s.link);
			check(links.size() == 1 && links[0].url == s.link, "source link is a link: " + s.link);
		}
	}
	std::size_t with_link = 0, with_translations = 0, translation_links = 0;
	for (const song & s : songs)
	{
		with_link += s.link.empty() ? 0 : 1;
		with_translations += s.translations.empty() ? 0 : 1;
		for (const translation_link & t : s.translations)
		{
			translation_links++;
			check(!t.language.empty() && find_links(t.link).size() == 1,
			      "translation link is a link: " + s.file_name + ": " + t.link);
		}
		// Every translation in the text below the lyrics, each address
		// found as a link there.
		check(find_links(translation_links_text(s)).size() == s.translations.size(),
		      "translation links text: " + s.file_name);
	}
	std::printf("%zu songs with a source link, %zu with %zu translation links\n",
	            with_link, with_translations, translation_links);
	check(with_link > 0, "source links embedded");
	check(with_translations > 0, "translation links embedded");

	{
		song s = make_song("Amiga");
		check(translation_links_text(s).empty(), "no translations, no text");
		s.translations.push_back({ "rus", "", "tangoman", "http://r.com/" });
		s.translations.push_back({ "eng", "Amiga", "Lucas", "https://a.com/amiga" });
		s.translations.push_back({ "xyz", "", "Someone", "http://c.com/" });
		s.translations.push_back({ "deu", "", "", "https://d.com/" });
		s.translations.push_back({ "eng", "Let's Dance", "", "https://b.com/x" });
		check(translation_links_text(s) ==
		          "Translations:\n"
		          "English, Lucas: https://a.com/amiga\n"
		          "English, \"Let's Dance\": https://b.com/x\n"
		          "German: https://d.com/\n"
		          "Russian, tangoman: http://r.com/\n"
		          "xyz, Someone: http://c.com/",
		      "translation links text: format, English first, then by language");
	}

	const matcher m(songs);

	// Every song is found exactly by its own name and by its file name. Other
	// songs may be found alongside it - several songs share a title - but
	// then only songs whose title really is the same once brackets are
	// dropped; never one that merely shares a note like "(hissy)".
	std::size_t ambiguous = 0;
	for (std::size_t i = 0; i < songs.size(); i++)
	{
		for (const std::string & title : { songs[i].name, songs[i].file_name })
		{
			const title_match r = m.find(title);
			bool self = false;
			for (const match_candidate & c : r.candidates) self = self || c.song == static_cast<int>(i);
			check(r.kind == match_kind::exact && self, "self match: " + describe(m, title));
			if (r.candidates.size() > 1 && title == songs[i].name) ambiguous++;

			const std::vector<std::string> own = all_keys(songs[i]);
			for (const match_candidate & c : r.candidates)
			{
				const std::vector<std::string> theirs = all_keys(songs[c.song]);
				bool shared = false;
				for (const std::string & k : own)
					shared = shared || std::find(theirs.begin(), theirs.end(), k) != theirs.end();
				// "Echague - Me gusta bailar milonga" finds "Me gusta bailar
				// milonga" through the piece after the dash, which is right.
				const bool via_dash = title.find(" - ") != std::string::npos;
				check(shared || via_dash, "\"" + title + "\" matched \"" + songs[c.song].file_name +
				              "\" through key " + c.song_key + ", which is not a title of both");
			}
		}
	}
	std::printf("%zu song titles are shared by more than one song\n", ambiguous);

	// --- real titles, as tracks carry them ---------------------------------------
	expect(m, "Rie payaso", "Rie, Payaso", match_kind::exact);
	expect(m, "Rie, payaso!", "Rie, Payaso", match_kind::exact);
	expect(m, "Ríe payaso", "Rie, Payaso", match_kind::exact);
	expect(m, "Dios te salve m'hijo", "Dios te salve, m'hijo", match_kind::exact);
	expect(m, "Dios te salve mi hijo", "Dios te salve, m'hijo", match_kind::similar);
	expect(m, "Volveras, pero cuando?", "Volveras pero cuando", match_kind::exact);
	expect(m, "¿Volverás? ¿Pero cuándo?", "Volveras pero cuando", match_kind::exact);
	expect(m, "La lluvia y yo (vals)", "La lluvia y yo", match_kind::exact);
	expect(m, "La lluvia y yo [Remastered 2004]", "La lluvia y yo", match_kind::exact);
	expect(m, "Frou-Frou", "Frou Frou", match_kind::exact);
	expect(m, "Loca de amor", "La loca de amor", match_kind::exact);
	expect(m, "Loca", "Loca", match_kind::exact);
	expect(m, "Volver", "Volver", match_kind::exact);
	expect(m, "Mirame a la cara", "Mirame en la cara", match_kind::exact);
	expect(m, "Mirame en la cara", "Mirame en la cara", match_kind::exact);
	expect(m, "Mirame en la kara", "Mirame en la cara", match_kind::similar);
	expect(m, "No me pregunten porque", "No me pregunten por que", match_kind::exact);
	expect(m, "Cuatro palabras", "Cuatro palabras", match_kind::exact);
	expect(m, "Carlos Di Sarli - Rie payaso - 1940", "Rie, Payaso", match_kind::exact);
	expect(m, "- Rie payaso -", "Rie, Payaso", match_kind::exact);
	expect(m, "Rie payaso (con Alberto Podesta)", "Rie, Payaso", match_kind::exact);

	// A song is never found by part of its title, nor a longer title by a
	// song that is part of it.
	expect_none(m, "Volver a verte");
	expect_none(m, "Loca bohemia");
	expect_none(m, "Mi taza");
	expect_none(m, "Rie");
	expect_none(m, "La lluvia");
	expect_none(m, "Instrumental");
	expect_none(m, "");

	// Notes in brackets are not titles, even when several songs carry them.
	expect_none(m, "Hissy");
	expect_none(m, "Reverb");
	expect_none(m, "Caruso");
	expect_none(m, "Estribillo");

	check(credit_overlap("", songs.front()) == 0, "no credits, no overlap");

	// Short titles, and a title several songs share: every one is offered,
	// and the credits pick between them. On a list of its own, so the test
	// does not depend on which songs the data happens to hold.
	{
		std::vector<song> shared;
		for (const char * n : { "Yo", "Tú", "Sin amor", "Sin amor - 1946", "Sin amor (tango)" })
			shared.push_back(make_song(n));
		shared[2].composer = "Lalo Echegoncelay"; shared[2].author = "Héctor Sapelli";
		shared[3].name = "Sin amor";   // the file name carries the year, the title does not
		shared[3].composer = "Juan Rodriguez";    shared[3].author = "Carlos Olmedo";
		shared[4].composer = "Pedro Maffia";      shared[4].author = "Homero Manzi";
		const matcher sm(shared);
		expect(sm, "Yo", "Yo", match_kind::exact);
		expect(sm, "Tu", "Tú", match_kind::exact);

		const title_match r = sm.find("Sin amor");
		check(r.kind == match_kind::exact && r.candidates.size() == 3, describe(sm, "Sin amor") + ", expected three");
		check(credit_overlap("Echegoncelay, Lalo; Sapelli", shared[2]) == 3, "credits name the first");
		check(credit_overlap("Echegoncelay, Lalo; Sapelli", shared[3]) == 0, "credits do not name the second");
		check(credit_overlap("Hector Sapelli", shared[2]) == 2, "credits match without accents");
	}

	// Bracketed alternative titles are found; a bracketed name the song
	// credits is not a title.
	{
		std::vector<song> alt;
		for (const char * n : { "Frou Frou (Fru Fru)", "Nobleza de arrabal (Caruso)", "Mia (Juan Caruso)",
		                        "Olga (Villoldo)", "Desvelo (De flor en flor)", "El trece" })
			alt.push_back(make_song(n));
		alt[1].composer = "Francisco Canaro"; alt[1].author = "Juan Andrés Caruso";
		alt[2].composer = "José Bohr";        alt[2].author = "Juan Andrés Caruso";
		alt[3].composer = "Francisco Peña";   alt[3].author = "Francisco Peña";
		alt[4].composer = "Juan de Dios Filiberto"; alt[4].author = "Carlos Flor";
		alt[5].composer = "Alberico Spátola"; alt[5].author = "Ángel Villoldo";
		const matcher am(alt);
		expect(am, "Fru Fru", "Frou Frou (Fru Fru)", match_kind::exact);
		expect(am, "Frou Frou", "Frou Frou (Fru Fru)", match_kind::exact);
		expect(am, "Nobleza de arrabal", "Nobleza de arrabal (Caruso)", match_kind::exact);
		expect_none(am, "Caruso");
		expect_none(am, "Juan Caruso");
		// A surname another song credits is a person too.
		expect(am, "Olga", "Olga (Villoldo)", match_kind::exact);
		expect_none(am, "Villoldo");
		// Credit words make up the alias, but not as surnames: still a title.
		expect(am, "De flor en flor", "Desvelo (De flor en flor)", match_kind::exact);
	}

	// --- the examples from the brief, on a list of their own ---------------------
	std::vector<song> small;
	for (const char * n : { "Cafe", "Canto", "Fueron Tres Años", "Canto de amor", "Cafe Dominguez" })
		small.push_back(make_song(n));
	const matcher s(small);
	expect(s, "Fueron tres anos", "Fueron Tres Años", match_kind::exact);
	expect(s, "Fueron tres años!", "Fueron Tres Años", match_kind::exact);
	expect(s, "Café", "Cafe", match_kind::exact);
	expect(s, "Café Domínguez", "Cafe Dominguez", match_kind::exact);
	expect(s, "Canto", "Canto", match_kind::exact);
	expect(s, "Canto de amor", "Canto de amor", match_kind::exact);
	expect(s, "Canto, de amor", "Canto de amor", match_kind::exact);

	std::vector<song> short_only;
	for (const char * n : { "Cafe", "Canto" }) short_only.push_back(make_song(n));
	const matcher so(short_only);
	expect_none(so, "Cafe Dominguez");
	expect_none(so, "Canto de amor");

	std::vector<song> long_only;
	for (const char * n : { "Cafe Dominguez", "Canto de amor" }) long_only.push_back(make_song(n));
	const matcher lo(long_only);
	expect_none(lo, "Cafe");
	expect_none(lo, "Canto");
	expect_none(lo, "Canto de amores perdidos");

	// --- links in text ------------------------------------------------------------
	auto links_of = [](const std::string & text)
	{
		std::vector<std::string> out;
		for (const text_link & l : find_links(text)) out.push_back(l.url);
		return out;
	};
	using strings = std::vector<std::string>;
	check(links_of("tomado de: http://recitango.wm.com.ar/") == strings{ "http://recitango.wm.com.ar/" },
	      "link at the end of a line");
	check(links_of("(http://www.antoniotormo.com.ar) se puede") == strings{ "http://www.antoniotormo.com.ar" },
	      "closing bracket not part of a link");
	check(links_of("see https://en.wikipedia.org/wiki/Tango_(dance).") ==
	          strings{ "https://en.wikipedia.org/wiki/Tango_(dance)" },
	      "bracket the link opened kept, full stop dropped");
	check(links_of("en www.todotango.com, y") == strings{ "http://www.todotango.com" }, "bare www. gets http://");
	check(links_of("HTTPS://X.COM/a?b=1&c=2") == strings{ "HTTPS://X.COM/a?b=1&c=2" }, "case and query kept");
	check(links_of("\xE2\x80\x9Chttp://a.com/x\xE2\x80\x9D") == strings{ "http://a.com/x" },
	      "typographic quotes end a link");
	check(links_of("a http://one.com b https://two.com") == strings{ "http://one.com", "https://two.com" },
	      "two links");
	check(links_of("no www. here, nor http:// alone").empty(), "prefix alone is no link");
	check(links_of("xhttp://a.com wwwx.com file:///c:/x").empty(), "not mid-word, not other schemes");
	{
		const std::string t = "Fuente: http://a.com/b.";
		const std::vector<text_link> l = find_links(t);
		check(l.size() == 1 && t.substr(l[0].begin, l[0].end - l[0].begin) == "http://a.com/b", "link offsets");
	}

	// --- discographies --------------------------------------------------------------
	const discography & disco = embedded_discography();
	check(disco.orchestras.size() >= 40, "at least 40 orchestras embedded");
	check(disco.recordings.size() >= 8000, "at least 8000 recordings embedded");
	{
		int fresedo = 0, piazzolla = 0;
		for (const std::string & o : disco.orchestras)
		{
			if (fold_key(o) == "osvaldofresedo") fresedo++;
			if (fold_key(o) == "astorpiazzolla") piazzolla++;
		}
		check(fresedo == 1, "one Osvaldo Fresedo, from the file without mistakes");
		int donato = 0, de_caro = 0;
		for (const std::string & o : disco.orchestras)
		{
			if (fold_key(o) == "edgardodonato") donato++;
			if (fold_key(o) == "juliodecaro") de_caro++;
		}
		check(donato == 1 && de_caro == 1, "Donato and De Caro from tango.info, having nothing better");
		check(piazzolla == 1, "Astor and Ástor Piazzolla are one orchestra");
		int laurenz = 0;
		for (const std::string & o : disco.orchestras)
			if (fold_key(o) == "pedrolaurenz") laurenz++;
		check(laurenz == 1, "Pedro Laurenz and Pedro Láurenz are one orchestra");
		std::size_t credited = 0, troilo_milongueando = 0;
		for (const recording & r : disco.recordings)
		{
			if (r.source >= 0) credited++;
			if (fold_key(disco.orchestras[r.orchestra]) == "anibaltroilo" && r.date == "1941-06-17" &&
			    fold_key(r.name).compare(0, 12, "milongueando") == 0)
				troilo_milongueando++;
		}
		check(!disco.sources.empty() && disco.sources.front().licence == "CC BY-SA 4.0" &&
		          disco.sources.front().author.find("Tango Time Travel") != std::string::npos &&
		          !disco.sources.front().url.empty() && !disco.sources.front().licence_url.empty(),
		      "Tango Time Travel credited, with licence and links");
		check(credited >= 1500, "Tango Time Travel's recordings carry their source");
		check(troilo_milongueando == 1, "\"Milongueando en el 40\" and \"... en el cuarenta\" on one day are one recording");
		bool sorted = true, dates_ok = true;
		for (std::size_t i = 0; i < disco.recordings.size(); i++)
		{
			const recording & r = disco.recordings[i];
			const std::size_t n = r.date.size();
			if (n != 0 && n != 4 && n != 7 && n != 10) dates_ok = false;
			if (i > 0 && disco.recordings[i - 1].orchestra != r.orchestra &&
			    disco.orchestras[disco.recordings[i - 1].orchestra] > disco.orchestras[r.orchestra])
				sorted = false;
		}
		check(dates_ok, "dates are yyyy, yyyy-mm or yyyy-mm-dd");
		check(sorted, "recordings by orchestra");
	}

	check(singers_of("Instrumental").empty(), "instrumental has no singers");
	check(singers_of("Floreal Ruiz, Edmundo Rivero") == std::vector<std::string>{ "Floreal Ruiz", "Edmundo Rivero" },
	      "singers split on commas");
	check(singers_of("Lita Morales y Horacio Lagos") == std::vector<std::string>{ "Lita Morales", "Horacio Lagos" },
	      "singers split on y");
	check(main_title("Canción de amor | La Chanson d'Amour") == "Canción de amor", "main title before the bar");

	// Dates written anywhere, any way.
	auto dates_in = [](const std::string & text)
	{
		std::string out;
		for (const date_parts & d : find_dates(text))
			out += (out.empty() ? "" : " ") + std::to_string(d.year) + "/" + std::to_string(d.month) + "/" +
			       std::to_string(d.day);
		return out;
	};
	check(dates_in("1941-10-09") == "1941/10/9", "iso date");
	check(dates_in("1941\xE2\x80\x93" "02\xE2\x80\x93" "19") == "1941/2/19", "en dashes");
	check(dates_in("09/08/1934") == "1934/8/9", "day/month/year");
	check(dates_in("1938-06-22 \nGolden Ear+") == "1938/6/22", "date with a note");
	check(dates_in("Todos de Rodolfo Biagi 1927-1948") == "1927/0/0 1948/0/0", "a range is two years");
	check(dates_in("Di Sarli - Rie payaso - 1940.mp3") == "1940/0/0", "year in a file name");
	check(dates_in("BAVE 69617-1 - 39552 B, 2606, 1106").empty(), "catalogue numbers are not dates");
	check(parse_date("1952-10").year == 1952 && parse_date("1952-10").month == 10 && parse_date("1952-10").day == 0,
	      "parse year-month");

	const recording_matcher rm(disco);
	auto best_of = [&](const track_tags & t, bool * confident = nullptr) -> const recording *
	{
		const track_match r = rm.find(t);
		if (confident != nullptr) *confident = r.confident;
		return r.candidates.empty() ? nullptr : &disco.recordings[r.candidates.front().recording];
	};
	auto describe_track = [&](const track_tags & t)
	{
		const track_match r = rm.find(t);
		std::string s = "\"" + t.title + "\" / \"" + t.artist + "\" / \"" + t.path + "\" ->" +
		                (r.confident ? " confident" : "");
		for (const recording_match & c : r.candidates)
		{
			const recording & rec = disco.recordings[c.recording];
			s += " [" + disco.orchestras[rec.orchestra] + " | " + rec.vocal + " | " + rec.date + " | " + rec.name + "]";
		}
		return s;
	};
	auto expect_recording = [&](const track_tags & t, const std::string & vocal, const std::string & date,
	                            bool confident)
	{
		bool sure = false;
		const recording * r = best_of(t, &sure);
		check(r != nullptr && r->vocal == vocal && r->date == date && sure == confident,
		      describe_track(t) + ", expected " + vocal + " " + date + (confident ? " confidently" : ""));
	};
	auto tags = [](const char * title, const char * artist, const char * dates = "", const char * comment = "",
	               const char * path = "")
	{
		track_tags t;
		t.title = title; t.artist = artist; t.dates = dates; t.comment = comment; t.path = path;
		return t;
	};

	// The year picks the session; accents and commas do not matter.
	expect_recording(tags("Al compas del corazon", "Di Sarli", "1942"), "Alberto Podestá", "1942-04-09", true);
	// The singer in the artist field, the title or the file name.
	expect_recording(tags("Al compás del corazón", "Carlos Di Sarli / Oscar Serpa", "1953-12"), "Oscar Serpa", "1953-12-18", true);
	expect_recording(tags("Al compás del corazón (Podestá)", "Carlos Di Sarli"), "Alberto Podestá", "1942-04-09", true);
	expect_recording(tags("", "", "", "", "X:\\Music\\Di Sarli - Podesta - Al compas del corazon.mp3"),
	                 "Alberto Podestá", "1942-04-09", true);
	// The date in the comment.
	expect_recording(tags("Al compás del corazón", "Carlos Di Sarli", "", "1952-10-14"), "Oscar Serpa", "1952-10", true);
	// Neither: two sessions, the user's call.
	expect_recording(tags("Al compás del corazón", "Carlos Di Sarli"), "Alberto Podestá", "1942-04-09", false);
	// Numbers as digits.
	expect_recording(tags("Los 33 orientales", "Carlos Di Sarli - Instrumental", "1948-06-22"), "Instrumental",
	                 "1948-06-22", true);
	// The orchestra in the folder, the singer nowhere: by the date.
	expect_recording(tags("Recuerdo", "", "1944", "", "X:\\Pugliese\\Recuerdo.flac"), "Instrumental", "1944-03-31", true);
	// Another of the orchestra's singers named: not that recording.
	{
		const recording * r = best_of(tags("Recuerdo", "Osvaldo Pugliese - Jorge Maciel"));
		check(r != nullptr && r->vocal == "Jorge Maciel", "the named singer's Recuerdo");
	}
	expect_recording(tags("A media luz", "Edgardo Donato - Horacio Lagos"), "Horacio Lagos", "1941-10-13", true);
	expect_recording(tags("1937", "Julio De Caro"), "Luis Díaz", "1938-01-10", true);
	// Nothing names an orchestra: every recording of the title is offered,
	// none with confidence.
	{
		const track_match r = rm.find(tags("", "", "", "", "E:\\78rpm\\_elcorazonmeengano.flac"));
		bool darienzo = false;
		for (const recording_match & c : r.candidates)
			darienzo = darienzo || (disco.orchestras[disco.recordings[c.recording].orchestra] == "Juan D'Arienzo" &&
			                        disco.recordings[c.recording].vocal == "Alberto Reynal");
		check(darienzo && !r.confident, describe_track(tags("", "", "", "", "E:\\78rpm\\_elcorazonmeengano.flac")) +
		                                    ", expected D'Arienzo / Reynal offered, unchecked");
		bool sure = true;
		check(best_of(tags("La cumparsita", ""), &sure) != nullptr && !sure, "title alone: offered, never confident");
	}
	// An orchestra named: the others' recordings of the title are not offered.
	check(best_of(tags("Naipe", "Enrique Rodriguez")) == nullptr, "an orchestra without a discography matches nothing");
	check(best_of(tags("Cafe", "Carlos Di Sarli")) == nullptr || fold_key(best_of(tags("Cafe", "Carlos Di Sarli"))->name) != "cafedominguez",
	      "no title by part of it");

	// Every recording found from its own tags, written as a tagged file has
	// them.
	{
		int missed = 0;
		std::string misses;
		for (const recording & r : disco.recordings)
		{
			track_tags t;
			t.title = r.name;
			t.artist = disco.orchestras[r.orchestra] + " - " + r.vocal;
			t.dates = r.date;
			const track_match tm = rm.find(t);
			bool found = false;
			for (const recording_match & c : tm.candidates)
			{
				const recording & o = disco.recordings[c.recording];
				if (c.score != tm.candidates.front().score) break;
				if (o.orchestra == r.orchestra && o.name == r.name && o.vocal == r.vocal && o.date == r.date) found = true;
			}
			if (!found && missed++ < 10) misses += "\n    " + describe_track(t);
		}
		check(missed == 0, std::to_string(missed) + " recordings not found from their own tags:" + misses);
	}

	// --- tags from a recording --------------------------------------------------------
	check(title_with_notes("Que no sepan las estrellas", "Que no sepan las estrellas (decrackle)") ==
	          "Que no sepan las estrellas (decrackle)", "lower case note kept");
	check(title_with_notes("El internado", "El internado (2)") == "El internado (2)", "take number kept");
	check(title_with_notes("Pobre negrito (Flor de Montserrat)", "Flor de Monserrat (Pobre negrito)") ==
	          "Pobre negrito (Flor de Montserrat)", "alternative title dropped");
	check(title_with_notes("Fea", "Fea - Alfredo Rojas - 1945") == "Fea", "singer and year in the title dropped");
	check(title_with_notes("Canción de amor | La Chanson d'Amour", "") == "Canción de amor", "alternative after the bar dropped");
	{
		recording r;
		r.date = "1952-09";
		check(date_for(r, "1953-08-14") == "1953-08-14", "a finer date a year off kept");
		check(date_for(r, "1953") == "1952-09", "a coarser date replaced");
		check(date_for(r, "1960-01-01") == "1952-09", "a date years off replaced");
		r.date = "";
		check(date_for(r, "1953") == "1953", "nothing to replace it with");
	}
	{
		discography d;
		d.orchestras = { "Carlos di Sarli" };
		recording sung{ 0, "Al compás del corazón", "Alberto Podestá", "1942-04-09", "Tango" };
		recording duo{ 0, "Al compás del corazón", "Alberto Podestá, Oscar Serpa", "1942-04-09", "Tango" };
		recording inst{ 0, "El once", "Instrumental", "1942-04-09", "Tango" };
		auto artist = [&](const recording & r, artist_scheme s)
		{
			tag_options o;
			o.scheme = s;
			std::string out;
			for (const tag_value & t : recording_tags(d, r, o, current_tags{}))
				if (t.field == "ARTIST" || t.field == "CANTOR")
					for (const std::string & v : t.values) out += (out.empty() ? "" : " | ") + t.field + "=" + v;
			return out;
		};
		check(artist(sung, artist_scheme::orchestra_dash_singer) == "ARTIST=Carlos di Sarli - Alberto Podestá", "dash scheme");
		check(artist(inst, artist_scheme::orchestra_dash_singer) == "ARTIST=Carlos di Sarli - Instrumental", "dash scheme, instrumental");
		check(artist(duo, artist_scheme::orchestra_slash_singer) == "ARTIST=Carlos di Sarli / Alberto Podestá, Oscar Serpa", "slash scheme");
		check(artist(inst, artist_scheme::orchestra_slash_singer) == "ARTIST=Carlos di Sarli", "slash scheme, instrumental");
		check(artist(sung, artist_scheme::singer_only) == "ARTIST=Alberto Podestá", "singer scheme");
		check(artist(inst, artist_scheme::singer_only) == "ARTIST=Instrumental", "singer scheme, instrumental");
		check(artist(sung, artist_scheme::orchestra_and_cantor_field) == "ARTIST=Carlos di Sarli | CANTOR=Alberto Podestá", "CANTOR scheme");
		check(artist(duo, artist_scheme::orchestra_and_singer_values) ==
		          "ARTIST=Carlos di Sarli | ARTIST=Alberto Podestá | ARTIST=Oscar Serpa", "multi-value scheme");

		tag_options only_artist;
		only_artist.title = only_artist.album_artist = only_artist.date = only_artist.genre = false;
		only_artist.scheme = artist_scheme::singer_only;
		bool album_artist = false;
		for (const tag_value & t : recording_tags(d, sung, only_artist, current_tags{}))
			album_artist = album_artist || t.field == "ALBUM ARTIST";
		check(album_artist, "the singer scheme always names the orchestra in ALBUM ARTIST");
	}

	fingerprint_tests(disco);

	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
