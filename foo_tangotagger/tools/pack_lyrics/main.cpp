// pack_lyrics <lyrics dir> <output .cpp> [--public-domain-only]
//
// Reads every *.xml in the lyrics directory, keeps what the component needs
// (title, file name, composer, lyricist and the Spanish text), compresses it
// with LZMA and writes it out as a C++ array for the component to embed. See
// core/payload_format.h for the layout.
//
// Runs at build time on the build machine. The XML files are machine-written
// by copy-publicdomain-lyrics.ps1, one <lyrics> with one Spanish
// <translation>, so they are read by plain string scanning rather than with
// an XML library.
//
// The output is only rewritten when its contents change.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "LzmaEnc.h"

#include "payload_format.h"

namespace fs = std::filesystem;

namespace
{
	void * lzma_alloc(ISzAllocPtr, size_t size) { return std::malloc(size); }
	void lzma_free(ISzAllocPtr, void * address) { std::free(address); }
	const ISzAlloc g_alloc = { lzma_alloc, lzma_free };

	bool read_file(const fs::path & path, std::string & out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) return false;
		out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		return true;
	}

	void append_utf8(std::string & out, std::uint32_t cp)
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

	std::string xml_unescape(const std::string & s)
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
	std::string attribute(const std::string & tag, const char * name)
	{
		const std::string needle = std::string(" ") + name + "=\"";
		const std::size_t at = tag.find(needle);
		if (at == std::string::npos) return std::string();
		const std::size_t start = at + needle.size();
		const std::size_t end = tag.find('"', start);
		if (end == std::string::npos) return std::string();
		return xml_unescape(tag.substr(start, end - start));
	}

	std::string trim(const std::string & s)
	{
		const char * ws = " \t\r\n";
		const std::size_t b = s.find_first_not_of(ws);
		if (b == std::string::npos) return std::string();
		return s.substr(b, s.find_last_not_of(ws) - b + 1);
	}

	std::string to_lf(const std::string & s)
	{
		std::string out;
		out.reserve(s.size());
		for (std::size_t i = 0; i < s.size(); i++)
		{
			if (s[i] == '\r')
			{
				out += '\n';
				if (i + 1 < s.size() && s[i + 1] == '\n') i++;
			}
			else out += s[i];
		}
		return out;
	}

	//! Whether a UTF-8 string contains Cyrillic, which in a Spanish title is a
	//! look-alike letter typed by mistake.
	bool has_cyrillic(const std::string & s)
	{
		for (std::size_t i = 0; i + 1 < s.size(); i++)
		{
			const unsigned char c = static_cast<unsigned char>(s[i]);
			if (c == 0xD0 || c == 0xD1) return true;
		}
		return false;
	}

	struct entry
	{
		std::string fields[tangotagger::payload_field_count];
	};

	bool parse(const std::string & xml, const std::string & file_name, bool public_domain_only,
	           entry & e, std::string & error)
	{
		std::size_t at = xml.find("<lyrics");
		if (at == std::string::npos) { error = "no <lyrics> element"; return false; }
		const std::string lyrics_tag = xml.substr(at, xml.find('>', at) - at);

		// For a build that is going to be published, only what the source
		// marks as public domain goes in. Anything else - "in copyright",
		// "unknown", or no status at all - stays out, whatever folder it was
		// found in.
		const std::string status = attribute(lyrics_tag, "pd_status");
		if (public_domain_only && status != "public domain")
		{
			error = status.empty() ? "no pd_status" : "pd_status \"" + status + "\"";
			return false;
		}

		// The Spanish translation; copy-publicdomain-lyrics.ps1 keeps nothing else,
		// but the language is checked rather than assumed.
		for (std::size_t pos = at;;)
		{
			const std::size_t t = xml.find("<translation", pos);
			if (t == std::string::npos) { error = "no Spanish <translation>"; return false; }
			const std::size_t tag_end = xml.find('>', t);
			const std::size_t close = xml.find("</translation>", tag_end);
			if (tag_end == std::string::npos || close == std::string::npos)
			{
				error = "unterminated <translation>";
				return false;
			}
			const std::string tag = xml.substr(t, tag_end - t);
			pos = close;
			if (attribute(tag, "lang") != "spa") continue;

			e.fields[tangotagger::payload_name] = trim(attribute(tag, "name"));
			e.fields[tangotagger::payload_text] =
				trim(to_lf(xml_unescape(xml.substr(tag_end + 1, close - tag_end - 1))));
			break;
		}
		e.fields[tangotagger::payload_file_name] = file_name;
		e.fields[tangotagger::payload_composer] = trim(attribute(lyrics_tag, "composer"));
		e.fields[tangotagger::payload_author] = trim(attribute(lyrics_tag, "author"));
		if (e.fields[tangotagger::payload_name].empty()) e.fields[tangotagger::payload_name] = file_name;
		if (e.fields[tangotagger::payload_text].empty()) { error = "empty lyrics"; return false; }
		return true;
	}

	void put_u32le(std::string & out, std::uint32_t v)
	{
		for (int i = 0; i < 4; i++) out += static_cast<char>((v >> (8 * i)) & 0xFF);
	}
}

int main(int argc, char ** argv)
{
	const bool public_domain_only = argc == 4 && std::string(argv[3]) == "--public-domain-only";
	if (argc != 3 && !public_domain_only)
	{
		std::fprintf(stderr, "usage: pack_lyrics <lyrics dir> <output .cpp> [--public-domain-only]\n");
		return 2;
	}
	const fs::path dir = fs::u8path(argv[1]);
	const fs::path output = fs::u8path(argv[2]);

	std::vector<fs::path> files;
	std::error_code ec;
	for (const fs::directory_entry & d : fs::directory_iterator(dir, ec))
	{
		if (d.is_regular_file() && d.path().extension() == ".xml") files.push_back(d.path());
	}
	if (ec || files.empty())
	{
		std::fprintf(stderr, "pack_lyrics: no .xml files in %s\n", argv[1]);
		return 1;
	}
	// Directory order differs between file systems; the blob should not.
	std::sort(files.begin(), files.end());

	std::string body;
	std::uint32_t count = 0;
	int failures = 0, skipped = 0;
	std::map<std::string, int> skip_reasons;
	for (const fs::path & path : files)
	{
		const std::string file_name = path.stem().u8string();
		std::string xml, error;
		entry e;
		if (!read_file(path, xml))
		{
			std::fprintf(stderr, "pack_lyrics: error: %s: cannot read\n", path.u8string().c_str());
			failures++;
			continue;
		}
		if (!parse(xml, file_name, public_domain_only, e, error))
		{
			// Left out rather than failing the build: not public domain, or
			// nothing usable in it. Counted by reason rather than listed, as
			// there can be thousands.
			skip_reasons[error]++;
			skipped++;
			continue;
		}
		if (has_cyrillic(e.fields[tangotagger::payload_name]))
			std::printf("pack_lyrics: warning: %s: title \"%s\" contains Cyrillic letters\n",
			            path.filename().u8string().c_str(), e.fields[tangotagger::payload_name].c_str());
		for (const std::string & f : e.fields)
		{
			body += f;
			body += '\0';
		}
		count++;
	}
	for (const auto & r : skip_reasons)
		std::printf("pack_lyrics: skipped %d file%s: %s\n", r.second, r.second == 1 ? "" : "s", r.first.c_str());
	if (failures || count == 0) return 1;

	std::string payload(tangotagger::payload_magic, 4);
	put_u32le(payload, count);
	payload += body;

	// --- compress ------------------------------------------------------------
	CLzmaEncProps props;
	LzmaEncProps_Init(&props);
	props.level = 9;
	props.dictSize = 1u << 24;   // larger than any payload in sight: one window over everything
	props.lc = 3; props.lp = 0; props.pb = 0;   // text: byte-aligned, no position bits
	props.fb = 273;
	props.numThreads = 1;
	LzmaEncProps_Normalize(&props);

	std::vector<unsigned char> packed(payload.size() + payload.size() / 2 + 1024);
	SizeT packed_len = packed.size() - tangotagger::lzma_header_size;
	SizeT props_len = LZMA_PROPS_SIZE;
	const SRes res = LzmaEncode(packed.data() + tangotagger::lzma_header_size, &packed_len,
	                            reinterpret_cast<const Byte *>(payload.data()), payload.size(),
	                            &props, packed.data(), &props_len, 0, nullptr, &g_alloc, &g_alloc);
	if (res != SZ_OK || props_len != LZMA_PROPS_SIZE)
	{
		std::fprintf(stderr, "pack_lyrics: LzmaEncode failed (%d)\n", static_cast<int>(res));
		return 1;
	}
	for (int i = 0; i < 8; i++)
		packed[LZMA_PROPS_SIZE + i] = static_cast<unsigned char>((static_cast<std::uint64_t>(payload.size()) >> (8 * i)) & 0xFF);
	packed.resize(tangotagger::lzma_header_size + packed_len);

	// --- emit ------------------------------------------------------------------
	std::ostringstream cpp;
	cpp << "// Generated by tools/pack_lyrics: " << count << " songs from " << files.size() << " files. Do not edit.\n"
	    << "// " << payload.size() << " bytes of payload, LZMA-compressed to " << packed.size() << ".\n\n"
	    << "#include <cstddef>\n\n"
	    << "extern const unsigned char tangotagger_lyrics_blob[] = {\n";
	for (std::size_t i = 0; i < packed.size(); i++)
	{
		if (i % 20 == 0) cpp << "\t";
		cpp << static_cast<unsigned>(packed[i]) << ",";
		if (i % 20 == 19 || i + 1 == packed.size()) cpp << "\n";
	}
	cpp << "};\n\n"
	    << "extern const std::size_t tangotagger_lyrics_blob_size = sizeof(tangotagger_lyrics_blob);\n";

	const std::string text = cpp.str();
	std::string existing;
	if (!read_file(output, existing) || existing != text)
	{
		std::ofstream out(output, std::ios::binary | std::ios::trunc);
		out << text;
		if (!out)
		{
			std::fprintf(stderr, "pack_lyrics: cannot write %s\n", argv[2]);
			return 1;
		}
	}

	std::printf("pack_lyrics: %u songs (%d files skipped), %zu bytes -> %zu bytes LZMA\n",
	            count, skipped, payload.size(), packed.size());
	return 0;
}
