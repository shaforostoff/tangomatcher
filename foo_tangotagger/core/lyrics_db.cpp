#include "lyrics_db.h"
#include "payload_format.h"
#include "title_match.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "LzmaDec.h"

// Generated at build time by tools/pack_lyrics, from ../publicdomain-lyrics.
extern const unsigned char tangotagger_lyrics_blob[];
extern const std::size_t tangotagger_lyrics_blob_size;

namespace tangotagger
{
	namespace
	{
		void * lzma_alloc(ISzAllocPtr, size_t size) { return std::malloc(size); }
		void lzma_free(ISzAllocPtr, void * address) { std::free(address); }
		const ISzAlloc g_lzma_alloc = { lzma_alloc, lzma_free };

		std::uint32_t read_u32le(const unsigned char * p)
		{
			return static_cast<std::uint32_t>(p[0])
			     | static_cast<std::uint32_t>(p[1]) << 8
			     | static_cast<std::uint32_t>(p[2]) << 16
			     | static_cast<std::uint32_t>(p[3]) << 24;
		}
	}

	bool lzma_decompress(const unsigned char * data, std::size_t size, std::string & out)
	{
		out.clear();
		if (data == nullptr || size < lzma_header_size) return false;

		std::uint64_t unpacked = 0;
		for (int i = 7; i >= 0; i--) unpacked = (unpacked << 8) | data[LZMA_PROPS_SIZE + i];
		// Written by our own packer, which always records the size; the
		// "unknown size" marker (all ones) is not something it produces. The
		// ceiling is a sanity check against a damaged header, far above what
		// a few hundred songs come to.
		if (unpacked == 0 || unpacked > (64u << 20)) return false;

		out.resize(static_cast<std::size_t>(unpacked));
		SizeT dest_len = static_cast<SizeT>(unpacked);
		SizeT src_len = static_cast<SizeT>(size - lzma_header_size);
		ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
		const SRes res = LzmaDecode(reinterpret_cast<Byte *>(&out[0]), &dest_len,
		                            data + lzma_header_size, &src_len,
		                            data, LZMA_PROPS_SIZE, LZMA_FINISH_END,
		                            &status, &g_lzma_alloc);
		if (res != SZ_OK || dest_len != unpacked)
		{
			out.clear();
			return false;
		}
		return true;
	}

	bool parse_payload(const std::string & payload, std::vector<song> & out)
	{
		out.clear();
		if (payload.size() < 8 || std::memcmp(payload.data(), payload_magic, 4) != 0) return false;

		const std::uint32_t count =
			read_u32le(reinterpret_cast<const unsigned char *>(payload.data()) + 4);
		std::size_t pos = 8;
		out.reserve(count);
		for (std::uint32_t i = 0; i < count; i++)
		{
			std::string fields[payload_field_count];
			for (std::string & field : fields)
			{
				const std::size_t end = payload.find('\0', pos);
				if (end == std::string::npos)
				{
					out.clear();
					return false;
				}
				field.assign(payload, pos, end - pos);
				pos = end + 1;
			}
			song s;
			s.name      = std::move(fields[payload_name]);
			s.file_name = std::move(fields[payload_file_name]);
			s.composer  = std::move(fields[payload_composer]);
			s.author    = std::move(fields[payload_author]);
			s.text      = std::move(fields[payload_text]);
			s.link      = std::move(fields[payload_link]);
			// lang TAB name TAB translator TAB link, a line each.
			const std::string & t = fields[payload_translations];
			for (std::size_t start = 0; start < t.size();)
			{
				std::size_t end = t.find('\n', start);
				if (end == std::string::npos) end = t.size();
				std::string parts[4];
				std::size_t at = start;
				for (int p = 0; p < 4; p++)
				{
					std::size_t tab = p < 3 ? t.find('\t', at) : end;
					if (tab == std::string::npos || tab > end) tab = end;
					parts[p].assign(t, at, tab - at);
					at = (std::min)(tab + 1, end);
				}
				if (!parts[3].empty())
					s.translations.push_back({ std::move(parts[0]), std::move(parts[1]), std::move(parts[2]), std::move(parts[3]) });
				start = end + 1;
			}
			out.push_back(std::move(s));
		}
		if (pos != payload.size())
		{
			out.clear();
			return false;
		}
		return true;
	}

	namespace
	{
		const char * language_name(const std::string & code)
		{
			static const char * const names[][2] = {
				{ "eng", "English" }, { "rus", "Russian" }, { "deu", "German" }, { "ger", "German" },
				{ "ita", "Italian" }, { "nld", "Dutch" }, { "dut", "Dutch" }, { "fra", "French" },
				{ "fre", "French" }, { "ukr", "Ukrainian" }, { "por", "Portuguese" }, { "pol", "Polish" },
				{ "jpn", "Japanese" }, { "fin", "Finnish" }, { "swe", "Swedish" }, { "tur", "Turkish" },
				{ "ell", "Greek" }, { "gre", "Greek" }, { "heb", "Hebrew" }, { "ces", "Czech" },
			};
			for (const auto & n : names)
				if (code == n[0]) return n[1];
			return nullptr;
		}
	}

	std::string translation_links_text(const song & s)
	{
		if (s.translations.empty()) return std::string();
		// English first, the other languages in alphabetical order of their
		// names; one language's translations in the order the data has them.
		auto display = [](const translation_link & t) -> std::string
		{
			const char * name = language_name(t.language);
			return name != nullptr ? name : t.language;
		};
		std::vector<const translation_link *> order;
		for (const translation_link & t : s.translations) order.push_back(&t);
		std::stable_sort(order.begin(), order.end(), [&](const translation_link * a, const translation_link * b)
		{
			const bool a_eng = a->language == "eng", b_eng = b->language == "eng";
			if (a_eng != b_eng) return a_eng;
			return display(*a) < display(*b);
		});

		std::string out = "Translations:";
		const std::string own = fold_key(s.name);
		for (const translation_link * p : order)
		{
			const translation_link & t = *p;
			out += "\n";
			out += display(t);
			if (!t.name.empty() && fold_key(t.name) != own) out += ", \"" + t.name + "\"";
			if (!t.translator.empty()) out += ", " + t.translator;
			out += ": " + t.link;
		}
		return out;
	}

	const std::vector<song> & embedded_songs()
	{
		// A function-local static: initialised once, thread-safely, on the
		// first call, which is the first time someone asks for lyrics rather
		// than when foobar2000 starts.
		static const std::vector<song> songs = []
		{
			std::vector<song> result;
			std::string payload;
			if (lzma_decompress(tangotagger_lyrics_blob, tangotagger_lyrics_blob_size, payload))
				parse_payload(payload, result);
			return result;
		}();
		return songs;
	}
}
