#include "discography.h"
#include "lyrics_db.h"
#include "title_match.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

// Generated at build time by tools/pack_discography, from ../publicdomain-xmldata.
extern const unsigned char tangotagger_discography_blob[];
extern const std::size_t tangotagger_discography_blob_size;

namespace tangotagger
{
	namespace
	{
		std::uint32_t read_u32le(const std::string & s, std::size_t pos)
		{
			const unsigned char * p = reinterpret_cast<const unsigned char *>(s.data()) + pos;
			return static_cast<std::uint32_t>(p[0])
			     | static_cast<std::uint32_t>(p[1]) << 8
			     | static_cast<std::uint32_t>(p[2]) << 16
			     | static_cast<std::uint32_t>(p[3]) << 24;
		}

		bool next_string(const std::string & payload, std::size_t & pos, std::string & out)
		{
			const std::size_t end = payload.find('\0', pos);
			if (end == std::string::npos) return false;
			out.assign(payload, pos, end - pos);
			pos = end + 1;
			return true;
		}

		std::string trim(const std::string & s)
		{
			const std::size_t b = s.find_first_not_of(" \t");
			if (b == std::string::npos) return std::string();
			return s.substr(b, s.find_last_not_of(" \t") - b + 1);
		}
	}

	bool parse_discography(const std::string & payload, discography & out)
	{
		out = discography();
		if (payload.size() < 12 || std::memcmp(payload.data(), discography_magic, 4) != 0) return false;

		std::size_t pos = 4;
		const std::uint32_t orchestras = read_u32le(payload, pos);
		pos += 4;
		out.orchestras.resize(orchestras);
		for (std::string & o : out.orchestras)
			if (!next_string(payload, pos, o)) return false;

		if (payload.size() < pos + 4) return false;
		const std::uint32_t count = read_u32le(payload, pos);
		pos += 4;
		out.recordings.resize(count);
		for (recording & r : out.recordings)
		{
			std::string index;
			if (!next_string(payload, pos, index) || !next_string(payload, pos, r.name) ||
			    !next_string(payload, pos, r.vocal) || !next_string(payload, pos, r.date) ||
			    !next_string(payload, pos, r.genre))
				return false;
			r.orchestra = std::atoi(index.c_str());
			if (r.orchestra < 0 || static_cast<std::uint32_t>(r.orchestra) >= orchestras) return false;
		}
		return pos == payload.size();
	}

	const discography & embedded_discography()
	{
		static const discography d = []
		{
			discography result;
			std::string payload;
			if (lzma_decompress(tangotagger_discography_blob, tangotagger_discography_blob_size, payload))
				parse_discography(payload, result);
			return result;
		}();
		return d;
	}

	bool is_instrumental(const std::string & vocal)
	{
		const std::string key = fold_key(vocal);
		return key.empty() || key == "instrumental";
	}

	std::vector<std::string> singers_of(const std::string & vocal)
	{
		std::vector<std::string> out;
		if (is_instrumental(vocal)) return out;
		std::string rest = vocal;
		for (std::size_t y; (y = rest.find(" y ")) != std::string::npos;) rest.replace(y, 3, ",");
		std::size_t start = 0;
		while (start <= rest.size())
		{
			std::size_t end = rest.find(',', start);
			if (end == std::string::npos) end = rest.size();
			const std::string one = trim(rest.substr(start, end - start));
			if (!fold_key(one).empty()) out.push_back(one);
			start = end + 1;
		}
		return out;
	}

	std::string main_title(const std::string & name)
	{
		const std::size_t bar = name.find(" | ");
		return bar == std::string::npos ? name : trim(name.substr(0, bar));
	}
}
