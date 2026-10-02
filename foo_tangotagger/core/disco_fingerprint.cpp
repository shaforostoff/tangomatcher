#include "disco_fingerprint.h"

#include <algorithm>
#include <cmath>

namespace tangotagger
{
	namespace
	{
		//! Probable matches kept beside an identified one or each other: more
		//! is a list of alternatives the sound cannot choose between.
		const std::size_t max_probable = 3;
	}

	const fingerprint_index & embedded_fingerprints()
	{
		static const fingerprint_index index = []
		{
			fingerprint_index result;
			for (const recording_fingerprint & f : embedded_discography().fingerprints)
			{
				fingerprint fp;
				if (decode_fingerprint(f.data, fp)) result.add(f.recording, std::move(fp));
			}
			return result;
		}();
		return index;
	}

	std::vector<int> sound_hints(const track_match & m)
	{
		std::vector<int> out;
		for (const recording_match & c : m.candidates) out.push_back(c.recording);
		return out;
	}

	void add_sound(track_match & m, const std::vector<fingerprint_match> & found)
	{
		std::size_t probable = 0;
		for (const fingerprint_match & f : found)
		{
			if (!f.identified() && !f.probable()) continue;
			if (!f.identified() && probable >= max_probable) continue;
			if (!f.identified()) probable++;
			auto it = std::find_if(m.candidates.begin(), m.candidates.end(),
			                       [&](const recording_match & c) { return c.recording == f.id; });
			if (it == m.candidates.end())
			{
				recording_match c;
				c.recording = f.id;
				m.candidates.push_back(c);
				it = m.candidates.end() - 1;
			}
			it->sound = std::max(1, static_cast<int>(std::lround(std::min(f.onset, 1.0) * 100)));
			it->sound_identified = it->sound_identified || f.identified();
		}

		// What the sound identifies, then what it finds probable, each best
		// first; then the tags' candidates in their own order.
		std::stable_sort(m.candidates.begin(), m.candidates.end(), [](const recording_match & a, const recording_match & b)
		{
			if (a.sound_identified != b.sound_identified) return a.sound_identified;
			if ((a.sound > 0) != (b.sound > 0)) return a.sound > 0;
			return a.sound > b.sound;
		});
		// Two recordings the sound both identifies are one recording the
		// discographies list twice, or a reference filed under the wrong one:
		// the user's call either way.
		const std::size_t identified = static_cast<std::size_t>(std::count_if(
			m.candidates.begin(), m.candidates.end(), [](const recording_match & c) { return c.sound_identified; }));
		if (identified == 1) m.confident = true;
		else if (identified > 1) m.confident = false;
	}
}
