#pragma once

// The orchestra discographies compiled into the component: who recorded
// what, with which singer, when. Read from ../xml-discographies-publicdomain at build
// time by tools/pack_discography.
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
	};

	struct discography
	{
		std::vector<std::string> orchestras;   //!< "Carlos di Sarli", "Orquesta Típica Víctor"...
		std::vector<recording> recordings;     //!< by orchestra, then title
	};

	//! The payload layout, shared with the packer:
	//!
	//!     4 bytes    "TTD1"
	//!     4 bytes    orchestra count, little endian
	//!     per orchestra its name, NUL-terminated
	//!     4 bytes    recording count, little endian
	//!     per recording five NUL-terminated strings: the orchestra's index
	//!                in decimal, name, vocal, date, genre
	const char discography_magic[4] = { 'T', 'T', 'D', '1' };

	//! Splits a decompressed payload into a discography. Returns false on
	//! anything malformed.
	bool parse_discography(const std::string & payload, discography & out);

	//! The discographies embedded in this build, decompressed on first use and
	//! kept; safe to call from any thread.
	const discography & embedded_discography();

	//! "Instrumental" (any case) or empty.
	bool is_instrumental(const std::string & vocal);

	//! The singers of a vocal field, split on commas and " y ".
	std::vector<std::string> singers_of(const std::string & vocal);

	//! The title without its " | alternative".
	std::string main_title(const std::string & name);
}
