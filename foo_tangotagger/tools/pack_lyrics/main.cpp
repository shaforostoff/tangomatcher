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

#include <map>
#include <string>
#include <vector>

#include "../pack_common.h"

namespace fs = std::filesystem;
using pack::attribute;
using pack::trim;
using pack::xml_unescape;

namespace
{
	//! Trimmed, with tabs and line breaks - the separators of the
	//! translations field - as spaces.
	std::string one_line(const std::string & s)
	{
		std::string out = trim(s);
		for (char & c : out)
			if (c == '\t' || c == '\n' || c == '\r') c = ' ';
		return out;
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

		// The Spanish text, and a link to each translation that has one.
		// copy-publicdomain-lyrics.ps1 leaves the other languages' elements
		// empty - their attributes without their text - but the language is
		// checked rather than assumed.
		bool have_spanish = false;
		std::string & translations = e.fields[tangotagger::payload_translations];
		for (std::size_t pos = at;;)
		{
			const std::size_t t = xml.find("<translation", pos);
			if (t == std::string::npos) break;
			const std::size_t tag_end = xml.find('>', t);
			const std::size_t close = xml.find("</translation>", tag_end);
			if (tag_end == std::string::npos || close == std::string::npos)
			{
				error = "unterminated <translation>";
				return false;
			}
			const std::string tag = xml.substr(t, tag_end - t);
			pos = close;
			const std::string lang = attribute(tag, "lang");
			if (lang != "spa")
			{
				const std::string link = one_line(attribute(tag, "link"));
				if (link.empty() || !attribute(tag, "deadlink").empty()) continue;
				if (translations.find("\t" + link + "\n") != std::string::npos) continue;   // listed twice
				translations += one_line(lang) + "\t" + one_line(attribute(tag, "name")) + "\t" +
				                one_line(attribute(tag, "translator")) + "\t" + link + "\n";
				continue;
			}
			if (have_spanish) continue;
			have_spanish = true;

			e.fields[tangotagger::payload_name] = trim(attribute(tag, "name"));
			e.fields[tangotagger::payload_link] = trim(attribute(tag, "link"));
			e.fields[tangotagger::payload_text] =
				trim(to_lf(xml_unescape(xml.substr(tag_end + 1, close - tag_end - 1))));
		}
		if (!have_spanish) { error = "no Spanish <translation>"; return false; }
		e.fields[tangotagger::payload_file_name] = file_name;
		e.fields[tangotagger::payload_composer] = trim(attribute(lyrics_tag, "composer"));
		e.fields[tangotagger::payload_author] = trim(attribute(lyrics_tag, "author"));
		if (e.fields[tangotagger::payload_name].empty()) e.fields[tangotagger::payload_name] = file_name;
		if (e.fields[tangotagger::payload_text].empty()) { error = "empty lyrics"; return false; }
		return true;
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

	const std::vector<fs::path> files = pack::xml_files(dir);
	if (files.empty())
	{
		std::fprintf(stderr, "pack_lyrics: no .xml files in %s\n", argv[1]);
		return 1;
	}

	std::string body;
	std::uint32_t count = 0;
	int failures = 0, skipped = 0;
	std::map<std::string, int> skip_reasons;
	for (const fs::path & path : files)
	{
		const std::string file_name = path.stem().u8string();
		std::string xml, error;
		entry e;
		if (!pack::read_file(path, xml))
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
	pack::put_u32le(payload, count);
	payload += body;

	std::vector<unsigned char> packed;
	if (!pack::compress(payload, packed, "pack_lyrics")) return 1;

	std::ostringstream comment;
	comment << "// Generated by tools/pack_lyrics: " << count << " songs from " << files.size() << " files. Do not edit.\n"
	        << "// " << payload.size() << " bytes of payload, LZMA-compressed to " << packed.size() << ".\n\n";
	if (!pack::write_blob(output, "tangotagger_lyrics_blob", comment.str(), packed, "pack_lyrics")) return 1;

	std::printf("pack_lyrics: %u songs (%d files skipped), %zu bytes -> %zu bytes LZMA\n",
	            count, skipped, payload.size(), packed.size());
	return 0;
}
