// core_test: the embedded lyrics and the title matching, without a host.

#include <cstdio>
#include <string>
#include <vector>

#include "lyrics_db.h"
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

	std::printf("%d checks, %d failed\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
