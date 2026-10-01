#pragma once

// The layout of the embedded lyrics, shared by the build-time packer
// (tools/pack_lyrics) and the reader (lyrics_db.cpp).
//
// The blob in the component is a .lzma stream - the format `7z a -tlzma`
// writes and LzmaDecode reads:
//
//     5 bytes    LZMA properties (lc/lp/pb and the dictionary size)
//     8 bytes    uncompressed size, little endian
//     ...        the compressed stream
//
// and what it decompresses to is:
//
//     4 bytes    "TTL3"
//     4 bytes    song count, little endian
//     per song   seven NUL-terminated UTF-8 strings, in field order below
//
// Line endings inside the lyrics are bare LF; the tag writer turns them into
// whatever the file wants.

#include <cstddef>

namespace tangotagger
{
	const char payload_magic[4] = { 'T', 'T', 'L', '3' };

	//! The fields of one song, in the order they are stored.
	enum payload_field
	{
		payload_name,       //!< the title as the source gives it: <translation name="...">
		payload_file_name,  //!< the XML file's base name, which is sometimes a variant spelling
		payload_composer,
		payload_author,     //!< the lyricist
		payload_text,       //!< the lyrics
		payload_link,       //!< the page the lyrics come from: <translation link="...">; may be empty
		//! Links to translations, one per line: language code, translated
		//! title, translator and address, separated by tabs. May be empty.
		payload_translations,
		payload_field_count
	};

	const std::size_t lzma_header_size = 13;
}
