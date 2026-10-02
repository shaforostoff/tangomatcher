#pragma once

// The discographies' fingerprints, and what they add to a track's match.
//
// Matching by tags and file name comes first and is usually enough. A track
// it is not confident about - nothing on it names the recording, or two
// sessions of the same tune fit it equally - is compared by its sound with
// the known transfers of the recordings (fingerprint.h). What the sound
// identifies goes first and is confident; what it finds probable goes first
// among the rest, unchecked; recordings the tags offered stay below.
//
// Standard C++ only.

#include <vector>

#include "disco_match.h"
#include "fingerprint.h"

namespace tangotagger
{
	//! The embedded fingerprints, indexed by recording; built on first use
	//! and kept. Safe to call from any thread.
	const fingerprint_index & embedded_fingerprints();

	//! Whether a track's match by tags leaves it to the sound.
	inline bool needs_sound(const track_match & m) { return !m.confident; }

	//! The recordings the tags offered, for fingerprint_index::identify to
	//! compare whatever its prefilter picks.
	std::vector<int> sound_hints(const track_match & m);

	//! `found` - fingerprint_index::identify's answer, best first - merged
	//! into the tags' candidates.
	void add_sound(track_match & m, const std::vector<fingerprint_match> & found);
}
