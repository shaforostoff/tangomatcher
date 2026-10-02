#pragma once

// Audio fingerprints: which recording a track is, from its sound alone.
//
// For the tracks the tags and the file name cannot place - "02 Track02.wav"
// - the track is decoded and compared with fingerprints of known transfers
// of the discographies' recordings. The difficulty is not the codec but the
// transfer: the same 78 dubbed from shellac and from a vinyl reissue, at
// turntable speeds a few percent apart, trimmed differently, with different
// noise and equalisation; and an orchestra recording the same arrangement
// again years later, which must not match.
//
// A fingerprint holds two things, measured by bpmcore (foo_bpm):
//
//  - chroma, the pitch content, in 0.25 second bins. bpmcore takes the
//    tuning offset out, so a transfer running 2% fast has the same chroma on
//    a time axis 2% shorter - up to a whole semitone, the offset being known
//    only modulo 100 cents. It finds the recording and the alignment;
//  - the strongest onsets, about four a second. Two transfers of one
//    recording have the same onsets along the whole alignment; two
//    performances of one arrangement drift apart within a phrase, because
//    the rubato is never the same twice. It tells the recording from its
//    re-recordings.
//
// Matching brings both tracks to the speed their tuning offsets imply, tries
// the query a semitone either way, and finds the best offset by chroma; then
// it refines the speed and compares the onsets in eight second blocks. The
// onset agreement, the median over the blocks, is the verdict, together with
// how far the blocks' best alignments stray from a straight line - a
// transfer differs only by its speed, a performance by its rubato. 0.85 or
// more, along a line, identifies the recording; 0.70 and more is probable -
// right more often than not, but wrong on D'Arienzo's 1950s re-recordings of
// his 1940s arrangements, so a person should confirm it. Where the tuning
// offset does not predict the speed - a file retuned or time-stretched
// digitally - the speed is searched over +/-5% instead.
//
// Measured on 2,414 TangoTunes transfers as references and 800 files of two
// other collections and second TangoTunes transfers as queries
// (fingerprint_lab/README.md in the repository): 96% identified, 3.3%
// probable, 0.4% missed; of the files whose recording was not among the
// references, none identified wrongly except where the tags were wrong.
//
// Standard C++ only, like the rest of core/.

#include <cstdint>
#include <string>
#include <vector>

namespace tangotagger
{
	//! What bpmcore::features measures, without depending on bpmcore.
	struct audio_features
	{
		double tuning_cents = 0;
		double chroma_hop = 0;           //!< seconds between chroma frames
		std::vector<float> chroma;       //!< 12 a frame, pitch class 0 = C
		std::vector<float> loudness;     //!< per chroma frame
		float silence = 0;               //!< frames quieter than this have no pitch
		double novelty_rate = 0;         //!< novelty frames per second
		std::vector<float> novelty;
	};

	struct fingerprint
	{
		struct onset
		{
			std::uint32_t time = 0;      //!< from the start of the file, in 10ms
			std::uint8_t strength = 0;   //!< 1-15, relative to the strongest
		};

		double duration = 0;             //!< seconds of music, first to last audible frame
		double music_start = 0;          //!< seconds into the file the music starts
		int tuning = 0;                  //!< cents from A=440, -50..50
		//! Per 0.25 second bin from music_start, 12 pitch classes of 0-3
		//! relative to the bin's strongest; all 0 for a silent bin.
		std::vector<std::uint8_t> chroma;
		std::vector<onset> onsets;

		std::size_t bins() const { return chroma.size() / 12; }
		bool empty() const { return chroma.empty(); }
	};

	//! Seconds of music a reference fingerprint keeps; a query keeps all of it.
	const double fingerprint_excerpt_seconds = 90.0;

	//! A fingerprint of `excerpt_seconds` of the music from its start; 0 for
	//! all of it. Empty when there is no music.
	fingerprint make_fingerprint(const audio_features & f, double excerpt_seconds);

	//! The compact form stored in xml-fingerprints and the component: about
	//! 1.8KB for a 90 second excerpt.
	std::string encode_fingerprint(const fingerprint & f);
	bool decode_fingerprint(const std::string & bytes, fingerprint & out);

	std::string to_base64(const std::string & bytes);
	bool from_base64(const std::string & text, std::string & bytes);

	//! Onset agreement at and above which a match identifies the recording...
	const double fingerprint_identified = 0.85;
	//! ...if the alignment strays no further than this from a straight line.
	//! D'Arienzo re-recorded his 1940s arrangements in the 1950s at a tempo
	//! steady enough to agree at 0.85, but not along a straight line.
	const double fingerprint_max_wander_ms = 25.0;
	//! Onset agreement at and above which a match is worth showing.
	const double fingerprint_probable = 0.70;

	struct fingerprint_match
	{
		int id = -1;              //!< what the reference was added with
		double onset = 0;         //!< agreement of the onsets, -1..1: the verdict
		double chroma = 0;        //!< agreement of the pitch content along the alignment
		//! How much faster the query runs than the reference: 1.02 is 2% fast.
		double speed = 1;
		int semitones = 0;        //!< the offset wrap between the two, -1, 0 or 1
		//! How far, in ms, the onsets' alignment strays from the straight
		//! line two transfers of one recording keep; -1 when unmeasured.
		double wander_ms = -1;

		//! The recording, as far as the sound can tell.
		bool identified() const
		{
			return onset >= fingerprint_identified && wander_ms >= 0 && wander_ms <= fingerprint_max_wander_ms;
		}
		//! Likely, but for a person to confirm.
		bool probable() const { return !identified() && onset >= fingerprint_probable; }
	};

	//! The references a query is compared against. Built once, then safe to
	//! query from several threads at once.
	class fingerprint_index
	{
	public:
		void add(int id, fingerprint f);
		std::size_t size() const { return m_refs.size(); }

		//! The best references for `query`, best first, each a different id.
		//! `also` names ids to compare whether or not the prefilter picks
		//! them - the recordings the track's tags point at.
		std::vector<fingerprint_match> identify(const fingerprint & query,
		                                        const std::vector<int> & also = std::vector<int>(),
		                                        std::size_t max_results = 5) const;

	private:
		struct reference
		{
			int id;
			fingerprint fp;
			double profile[12];   //!< overall chroma, unit length
		};
		std::vector<reference> m_refs;
	};
}
