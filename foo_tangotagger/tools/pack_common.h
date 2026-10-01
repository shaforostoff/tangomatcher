#pragma once

// What the build-time packers (pack_lyrics, pack_discography) share: reading
// the machine-written XML by plain string scanning, and turning a payload
// into an LZMA-compressed C++ array.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "LzmaEnc.h"

#include "payload_format.h"

namespace pack
{
	namespace fs = std::filesystem;

	inline void * lzma_alloc(ISzAllocPtr, size_t size) { return std::malloc(size); }
	inline void lzma_free(ISzAllocPtr, void * address) { std::free(address); }
	const ISzAlloc g_alloc = { lzma_alloc, lzma_free };

	inline bool read_file(const fs::path & path, std::string & out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) return false;
		out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		return true;
	}

	inline void append_utf8(std::string & out, std::uint32_t cp)
	{
		if (cp < 0x80) out += static_cast<char>(cp);
		else if (cp < 0x800)
		{
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else if (cp < 0x10000)
		{
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xF0 | (cp >> 18));
			out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
	}

	inline std::string xml_unescape(const std::string & s)
	{
		std::string out;
		out.reserve(s.size());
		for (std::size_t i = 0; i < s.size(); i++)
		{
			if (s[i] != '&') { out += s[i]; continue; }
			const std::size_t semi = s.find(';', i);
			if (semi == std::string::npos || semi - i > 10) { out += s[i]; continue; }
			const std::string ent = s.substr(i + 1, semi - i - 1);
			if (ent == "amp") out += '&';
			else if (ent == "lt") out += '<';
			else if (ent == "gt") out += '>';
			else if (ent == "quot") out += '"';
			else if (ent == "apos") out += '\'';
			else if (!ent.empty() && ent[0] == '#')
			{
				const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
				append_utf8(out, static_cast<std::uint32_t>(
					std::strtoul(ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10)));
			}
			else { out += s[i]; continue; }
			i = semi;
		}
		return out;
	}

	//! The value of `name="..."` inside one start tag, unescaped.
	inline std::string attribute(const std::string & tag, const char * name)
	{
		const std::string needle = std::string(" ") + name + "=\"";
		const std::size_t at = tag.find(needle);
		if (at == std::string::npos) return std::string();
		const std::size_t start = at + needle.size();
		const std::size_t end = tag.find('"', start);
		if (end == std::string::npos) return std::string();
		return xml_unescape(tag.substr(start, end - start));
	}

	inline std::string trim(const std::string & s)
	{
		const char * ws = " \t\r\n";
		const std::size_t b = s.find_first_not_of(ws);
		if (b == std::string::npos) return std::string();
		return s.substr(b, s.find_last_not_of(ws) - b + 1);
	}

	inline void put_u32le(std::string & out, std::uint32_t v)
	{
		for (int i = 0; i < 4; i++) out += static_cast<char>((v >> (8 * i)) & 0xFF);
	}

	//! Every *.xml in `dir`, sorted: directory order differs between file
	//! systems, and the blob should not.
	inline std::vector<fs::path> xml_files(const fs::path & dir)
	{
		std::vector<fs::path> files;
		std::error_code ec;
		for (const fs::directory_entry & d : fs::directory_iterator(dir, ec))
			if (d.is_regular_file() && d.path().extension() == ".xml") files.push_back(d.path());
		std::sort(files.begin(), files.end());
		return files;
	}

	//! Compresses `payload` into a .lzma stream (see payload_format.h).
	//! Returns false if the encoder fails.
	inline bool compress(const std::string & payload, std::vector<unsigned char> & packed, const char * tool)
	{
		CLzmaEncProps props;
		LzmaEncProps_Init(&props);
		props.level = 9;
		props.dictSize = 1u << 24;   // larger than any payload in sight: one window over everything
		props.lc = 3; props.lp = 0; props.pb = 0;   // text: byte-aligned, no position bits
		props.fb = 273;
		props.numThreads = 1;
		LzmaEncProps_Normalize(&props);

		packed.assign(payload.size() + payload.size() / 2 + 1024, 0);
		SizeT packed_len = packed.size() - tangotagger::lzma_header_size;
		SizeT props_len = LZMA_PROPS_SIZE;
		const SRes res = LzmaEncode(packed.data() + tangotagger::lzma_header_size, &packed_len,
		                            reinterpret_cast<const Byte *>(payload.data()), payload.size(),
		                            &props, packed.data(), &props_len, 0, nullptr, &g_alloc, &g_alloc);
		if (res != SZ_OK || props_len != LZMA_PROPS_SIZE)
		{
			std::fprintf(stderr, "%s: LzmaEncode failed (%d)\n", tool, static_cast<int>(res));
			return false;
		}
		for (int i = 0; i < 8; i++)
			packed[LZMA_PROPS_SIZE + i] = static_cast<unsigned char>((static_cast<std::uint64_t>(payload.size()) >> (8 * i)) & 0xFF);
		packed.resize(tangotagger::lzma_header_size + packed_len);
		return true;
	}

	//! Writes `packed` out as `extern const unsigned char <symbol>[]` and
	//! `<symbol>_size`, under `comment`. The file is only rewritten when its
	//! contents change.
	inline bool write_blob(const fs::path & output, const char * symbol, const std::string & comment,
	                       const std::vector<unsigned char> & packed, const char * tool)
	{
		std::ostringstream cpp;
		cpp << comment
		    << "#include <cstddef>\n\n"
		    << "extern const unsigned char " << symbol << "[] = {\n";
		for (std::size_t i = 0; i < packed.size(); i++)
		{
			if (i % 20 == 0) cpp << "\t";
			cpp << static_cast<unsigned>(packed[i]) << ",";
			if (i % 20 == 19 || i + 1 == packed.size()) cpp << "\n";
		}
		cpp << "};\n\n"
		    << "extern const std::size_t " << symbol << "_size = sizeof(" << symbol << ");\n";

		const std::string text = cpp.str();
		std::string existing;
		if (!read_file(output, existing) || existing != text)
		{
			std::ofstream out(output, std::ios::binary | std::ios::trunc);
			out << text;
			if (!out)
			{
				std::fprintf(stderr, "%s: cannot write %s\n", tool, output.u8string().c_str());
				return false;
			}
		}
		return true;
	}
}
