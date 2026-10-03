#include "tt_fields.h"

namespace tt
{

namespace
{
	bool equals_nocase(const std::string & a, const char * b)
	{
		return lower_ascii(a) == lower_ascii(b);
	}
}

std::string lower_ascii(std::string s)
{
	for (char & c : s)
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	return s;
}

const std::vector<std::string> & track_meta::values(const std::string & key) const
{
	static const std::vector<std::string> none;
	const auto it = fields.find(lower_ascii(key));
	return it == fields.end() ? none : it->second;
}

std::string track_meta::first(const std::string & key) const
{
	const std::vector<std::string> & v = values(key);
	return v.empty() ? std::string() : v.front();
}

std::vector<std::string> track_meta::values_of(const std::vector<std::string> & keys) const
{
	std::vector<std::string> out;
	for (const std::string & k : keys)
		for (const std::string & v : values(k)) out.push_back(v);
	return out;
}

void track_meta::add(const std::string & key, const std::string & value)
{
	fields[lower_ascii(key)].push_back(value);
}

std::string file_stem(const std::string & path)
{
	const std::size_t slash = path.find_last_of("/\\");
	std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
	const std::size_t dot = name.find_last_of('.');
	if (dot != std::string::npos && dot > 0) name.erase(dot);
	return name;
}

container container_of(const std::string & path)
{
	std::size_t dot = std::string::npos;
	for (std::size_t i = 0; i < path.size(); i++)
	{
		if (path[i] == '.') dot = i;
		else if (path[i] == '/' || path[i] == '\\' || path[i] == '|') dot = std::string::npos;
	}
	if (dot == std::string::npos) return container::other;
	const std::string ext = lower_ascii(path.substr(dot + 1));
	for (const char * e : { "mp3", "mp2", "mp1" })
		if (ext == e) return container::id3;
	for (const char * e : { "m4a", "m4b", "m4r", "mp4", "m4v", "3gp", "3g2" })
		if (ext == e) return container::mp4;
	return container::other;
}

const char * lyrics_field(const std::string & path)
{
	return container_of(path) == container::id3 ? "UNSYNCED LYRICS" : "LYRICS";
}

const char * const multitrack_note =
	"This is one track of a multi-track file - a cue sheet, most often - and DeaDBeeF "
	"cannot write tags to it.\n\n";

const std::vector<std::string> lyrics_known_fields = { "LYRICS", "UNSYNCED LYRICS", "UNSYNCEDLYRICS" };

std::vector<std::string> read_keys(const std::string & field, const std::string & path)
{
	if (equals_nocase(field, "DATE")) return { "year", "date" };
	if (equals_nocase(field, "ALBUM ARTIST"))
	{
		// TPE2 and aART are "band" to DeaDBeeF; a foobar2000 ALBUM ARTIST in
		// a Vorbis comment, or ALBUMARTIST, is "ALBUM ARTIST".
		if (container_of(path) == container::other) return { "ALBUM ARTIST", "albumartist" };
		return { "band", "ALBUM ARTIST", "albumartist" };
	}
	return { field };
}

std::string write_key(const std::string & field, const std::string & path)
{
	return read_keys(field, path).front();
}

std::vector<std::string> field_values(const track_meta & t, const std::string & field)
{
	return t.values_of(read_keys(field, t.path));
}

field_write field_write_for(const std::string & field, const std::vector<std::string> & values,
                            const std::string & path)
{
	field_write w;
	std::vector<std::string> keys = read_keys(field, path);
	w.key = keys.front();
	w.values = values;
	w.aliases.assign(keys.begin() + 1, keys.end());
	return w;
}

}   // namespace tt
