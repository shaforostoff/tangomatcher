#pragma once

// The public domain lyrics compiled into the component.
//
// Standard C++ only - no foobar2000 SDK - so that core_test can check the
// data and the matching without a host.

#include <cstddef>
#include <string>
#include <vector>

namespace tangotagger
{
	struct song
	{
		std::string name;       //!< title as the source gives it
		std::string file_name;  //!< source file's base name; sometimes a variant spelling
		std::string composer;
		std::string author;     //!< lyricist
		std::string text;       //!< the lyrics, LF line endings
	};

	//! Decompress a .lzma stream (13-byte header, then the stream).
	//! Returns false on anything malformed.
	bool lzma_decompress(const unsigned char * data, std::size_t size, std::string & out);

	//! Split a decompressed payload (see payload_format.h) into songs.
	//! Returns false on anything malformed.
	bool parse_payload(const std::string & payload, std::vector<song> & out);

	//! The songs embedded in this build. Decompressed on first use and kept;
	//! safe to call from any thread. Empty only if the blob is damaged, which
	//! core_test would have caught.
	const std::vector<song> & embedded_songs();
}
