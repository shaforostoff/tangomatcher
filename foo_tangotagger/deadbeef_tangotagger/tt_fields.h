#ifndef TT_FIELDS_H
#define TT_FIELDS_H

// A track's tags as the matching reads them, and the names DeaDBeeF gives the
// fields Tango Tagger writes. No DeaDBeeF in it: the plugin copies a track's
// metadata into a track_meta under the playlist lock, and everything after
// that - the matching, the rows, what is written - works on the copy, which is
// what lets the tests run it without a player.
//
// foo_tangotagger names fields as foobar2000 does, and so do the rows here -
// TITLE, ARTIST, ALBUM ARTIST, CANTOR, DATE, GENRE - because that is what
// core/disco_tags.h produces and what the preview shows. DeaDBeeF's own names
// differ where it maps a field onto a native frame or atom, read off its
// sources (src/junklib.c, shared/mp4tagutil.c, plugins/flac/flac.c,
// plugins/liboggedit/oggedit_utils.c):
//
//                 mp3 (ID3v2)                 m4a                 FLAC, Ogg, APE...
//   DATE          year (TYER/TDAT or TDRC)    year (©day)         year (DATE)
//   ALBUM ARTIST  band (TPE2)                 band (aART)         ALBUM ARTIST (ALBUMARTIST)
//   lyrics        UNSYNCED LYRICS (USLT)      LYRICS (©lyr)       LYRICS
//
// which are the same frames and atoms foobar2000 writes. Meta keys are
// matched without regard to case, by DeaDBeeF and here alike.

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace tt
{

//! One track's metadata, copied out of the player.
struct track_meta
{
	std::string path;   //!< ":URI"
	//! Every field, keys in lower case, each with all its values: DeaDBeeF
	//! keeps a field's several values in one entry, NUL-separated.
	std::map<std::string, std::vector<std::string>> fields;
	//! One track of a multi-track file - a cue sheet, most often - which
	//! DeaDBeeF cannot write tags to.
	bool subtrack = false;

	//! The values of `key`, any case; none when there is no such field.
	const std::vector<std::string> & values(const std::string & key) const;
	//! The first value of `key`, or blank.
	std::string first(const std::string & key) const;
	//! Every value of every key in `keys`, in that order.
	std::vector<std::string> values_of(const std::vector<std::string> & keys) const;

	//! Adds a value; for the plugin's copying and for the tests.
	void add(const std::string & key, const std::string & value);
};

std::string lower_ascii(std::string s);

//! The file name without its folders or extension: what stands in for a
//! missing title.
std::string file_stem(const std::string & path);

enum class container { id3, mp4, other };

//! By the file's extension, as foo_tangotagger's lyrics_field_for_path and
//! rubato_format.h decide it.
container container_of(const std::string & path);

//! Where lyrics go in this file: "UNSYNCED LYRICS" in an mp3, "LYRICS"
//! elsewhere.
const char * lyrics_field(const std::string & path);

//! Every name lyrics are found under, for telling whether a track already
//! has some.
extern const std::vector<std::string> lyrics_known_fields;

//! A field as it is written: the values replace every value of `key`, and the
//! aliases - the other names the same field is found under - are removed, so
//! the file does not carry two answers. No values removes the field.
struct field_write
{
	std::string key;
	std::vector<std::string> values;
	std::vector<std::string> aliases;
};

//! What writing one track changes.
struct track_write
{
	std::size_t track = 0;   //!< its index in the selection
	std::vector<field_write> fields;
};

//! The DeaDBeeF key a foobar2000-style field name is written under in this
//! file, and the other keys it is read from. Names this file does not map -
//! CANTOR, or anything else - are their own key.
std::string write_key(const std::string & field, const std::string & path);
std::vector<std::string> read_keys(const std::string & field, const std::string & path);

//! The values a foobar2000-style field has on the track, whichever of its
//! keys they are under.
std::vector<std::string> field_values(const track_meta & t, const std::string & field);

//! What the preview of a row for one track of a multi-track file says first.
extern const char * const multitrack_note;

//! A foobar2000-style field set to `values` on this file.
field_write field_write_for(const std::string & field, const std::vector<std::string> & values,
                            const std::string & path);

}   // namespace tt

#endif // TT_FIELDS_H
