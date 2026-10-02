#pragma once

// bpmcore's features as fingerprint.h takes them. For the targets that link
// bpmcore - the component and tools/make_fingerprints; core/ itself does not.

#include <bpmcore/bpmcore.h>

#include "fingerprint.h"

namespace tangotagger
{
	inline audio_features audio_features_of(bpmcore::features && f)
	{
		audio_features out;
		out.tuning_cents = f.tuning_cents;
		out.chroma_hop = f.chroma_hop;
		out.chroma = std::move(f.chroma);
		out.loudness = std::move(f.loudness);
		out.silence = f.silence;
		out.novelty_rate = f.novelty_rate;
		out.novelty = std::move(f.novelty);
		return out;
	}
}
