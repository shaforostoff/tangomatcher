#pragma once

// Title matching: which song, if any, a track title names.
//
// Everything works on keys. A key is a title folded to lower case ASCII
// letters and digits with accents, punctuation and spaces dropped, so
//
//     "Fueron Tres Años"  -> "fuerontresanos"  <- "Fueron tres anos"
//     "Yira, yira"        -> "yirayira"        <- "Yira yira"
//     "Volverás... ¿Pero cuándo?" -> "volverasperocuando"
//
// and two titles match when a key of one equals a key of the other - the
// whole key, never a prefix, so "Cafe" does not match "Cafe Dominguez" and
// "Canto" does not match "Canto de amor". A title has several keys: with
// and without its parenthesised parts, each parenthesised alternative title,
// and each piece between " - " separators.
//
// When nothing matches exactly, a near miss (edit distance 1 on keys of 10+
// characters, 2 on 16+) is offered as "similar" - listed but not pre-checked.
//
// Standard C++ only, like lyrics_db.h.

#include <string>
#include <unordered_map>
#include <vector>

#include "lyrics_db.h"

namespace tangotagger
{
	//! Lower case ASCII words, accents folded (á->a, ñ->n, ü->u, ß->ss...),
	//! Cyrillic look-alikes folded to Latin, punctuation as word breaks.
	//! Combining marks are dropped, so NFD input (macOS file names) folds the
	//! same as NFC. Letters with no Latin fold are kept as UTF-8.
	std::vector<std::string> fold_words(const std::string & utf8);

	//! fold_words, concatenated.
	std::string fold_key(const std::string & utf8);

	//! Keys a song answers to: its name and file name, each with and without
	//! parenthesised parts. When `aliases` is given it receives each
	//! parenthesised alternative title that is not just a descriptor such as
	//! "(tango)", "(estilo)" or "(hissy)" - which the matcher indexes only if
	//! no other song carries the same one.
	std::vector<std::string> song_keys(const song & s, std::vector<std::string> * aliases = nullptr);

	//! Keys to look a track title up under, most specific first: the whole
	//! title, the title without brackets, the pieces between dash separators
	//! ("Di Sarli - Rie payaso - 1940"), then each bracketed alternative title.
	std::vector<std::string> title_keys(const std::string & title);

	//! Optimal string alignment distance, giving up above `limit` (the return
	//! value is then limit + 1).
	int edit_distance(const std::string & a, const std::string & b, int limit);

	//! The largest edit distance allowed between two keys for a "similar"
	//! match, from the shorter key's length: 0 below 10 characters, so
	//! "Muchacha" and "Muchacho" - two different songs - stay apart.
	int similar_limit(std::size_t shorter_length);

	enum class match_kind { none, exact, similar };

	struct match_candidate
	{
		int song = -1;              //!< index into the song list
		match_kind kind = match_kind::none;
		int distance = 0;           //!< 0 for exact
		std::string track_key;      //!< the track key that matched
		std::string song_key;       //!< the song key it matched
	};

	//! Every song a title matched, best first, one entry per song. All of one
	//! kind: similar candidates are only looked for when nothing matched
	//! exactly. Several exact candidates are common - "Sin amor" is three
	//! different songs - and telling them apart is left to the caller, which
	//! has the track's other tags.
	struct title_match
	{
		match_kind kind = match_kind::none;
		std::vector<match_candidate> candidates;
	};

	class matcher
	{
	public:
		explicit matcher(const std::vector<song> & songs);

		title_match find(const std::string & title) const;

		const std::vector<song> & songs() const { return m_songs; }

	private:
		const std::vector<song> & m_songs;
		std::unordered_map<std::string, std::vector<int>> m_exact;
		struct keyed { std::string key; int song; };
		std::vector<keyed> m_all;
	};

	//! How strongly a track's composer and lyricist tags point at a song: the
	//! number of name words (4+ letters, folded) they share with the song's
	//! composer and author. 0 when either side has nothing to compare.
	int credit_overlap(const std::string & track_credits, const song & s);
}
