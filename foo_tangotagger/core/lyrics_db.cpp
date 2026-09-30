#include "lyrics_db.h"
#include "payload_format.h"

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
			out.push_back(std::move(s));
		}
		if (pos != payload.size())
		{
			out.clear();
			return false;
		}
		return true;
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
