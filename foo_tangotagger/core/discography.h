#pragma once

// The orchestra discographies compiled into the component: who recorded
// what, with which singer, when. Read from ../xml-discographies-cc-by-sa-4.0
// and ../xml-discographies-publicdomain at build time by
// tools/pack_discography.
//
// Standard C++ only, like lyrics_db.h.

#include <cstddef>
#include <string>
#include <vector>

namespace tangotagger
{
	struct recording
	{
		int orchestra = 0;      //!< index into discography::orchestras
		std::string name;       //!< the title; " | " separates an alternative one
		std::string vocal;      //!< "Instrumental", or the singers, comma separated
		std::string date;       //!< "1941-10-09", "1941-10", "1941" or empty
		std::string genre;      //!< "Tango", "Vals", "Milonga", "Foxtrot"...; may be empty
		int source = -1;        //!< index into discography::sources; -1 for public domain data
	};

	//! A discography under a licence that asks for credit: Tango Time
	//! Travel's, CC BY-SA 4.0.
	struct discography_source
	{
		std::string title;        //!< "Carlos di Sarli 1939-1941"
		std::string version;      //!< "2.1"
		std::string date;         //!< of that version: "2026-09-29"
		std::string author;       //!< "Tango Time Travel / Moving Art Studio ASBL"
		std::string url;          //!< the original
		std::string licence;      //!< "CC BY-SA 4.0"
		std::string licence_url;
	};

	//! A known transfer of a recording, fingerprinted: see fingerprint.h.
	struct recording_fingerprint
	{
		int recording = 0;      //!< index into discography::recordings
		//! encode_fingerprint's bytes. In embedded_discography() empty once
		//! embedded_fingerprints() has been built: the index keeps them.
		std::string data;
	};

	struct discography
	{
		std::vector<std::string> orchestras;   //!< "Carlos di Sarli", "Orquesta Típica Víctor"...
		std::vector<recording> recordings;     //!< by orchestra, then title
		std::vector<discography_source> sources;
		//! From ../xml-fingerprints; by recording.
		std::vector<recording_fingerprint> fingerprints;
	};

	//! The payload layout, shared with the packer:
	//!
	//!     4 bytes    "TTD3"
	//!     4 bytes    source count, little endian
	//!     per source seven NUL-terminated strings, in the order of
	//!                discography_source's fields
	//!     4 bytes    orchestra count, little endian
	//!     per orchestra its name, NUL-terminated
	//!     4 bytes    recording count, little endian
	//!     per recording six NUL-terminated strings: the orchestra's index
	//!                in decimal, name, vocal, date, genre, the source's
	//!                index in decimal or empty
	//!     4 bytes    fingerprint count, little endian
	//!     per fingerprint the recording's index and the data's length, 4
	//!                bytes each, little endian, then the data
	const char discography_magic[4] = { 'T', 'T', 'D', '3' };

	//! Splits a decompressed payload into a discography. Returns false on
	//! anything malformed.
	bool parse_discography(const std::string & payload, discography & out);

	//! The discographies embedded in this build, decompressed on first use and
	//! kept; safe to call from any thread.
	const discography & embedded_discography();

	//! The embedded discography's fingerprints, for embedded_fingerprints()
	//! to move the bytes out of, once: the entries stay, so their count does
	//! not change, but their data is the index's from then on. Nothing else
	//! is to call this.
	std::vector<recording_fingerprint> & take_embedded_fingerprint_data();

	//! "Instrumental" (any case) or empty.
	bool is_instrumental(const std::string & vocal);

	//! The singers of a vocal field, split on commas and " y ".
	std::vector<std::string> singers_of(const std::string & vocal);

	//! The title without its " | alternative".
	std::string main_title(const std::string & name);
}
