#pragma once

// What a recording from the discographies becomes in a file's tags.
//
// Collections disagree on where the singer goes. The schemes offered are the
// ones found in the wild:
//
//     Orquesta - Cantor     ARTIST "Carlos di Sarli - Roberto Rufino"
//                           ("Carlos di Sarli - Instrumental"), as
//                           TangoTunes' renamed files have it
//     Orquesta / Cantor     ARTIST "Carlos di Sarli / Roberto Rufino"
//                           ("Carlos di Sarli"), as Tango Time Travel has it
//     Cantor                ARTIST "Roberto Rufino" ("Instrumental"), the
//                           orchestra in ALBUM ARTIST only, as TangoTunes'
//                           own files have it
//     Orquesta + CANTOR     ARTIST "Carlos di Sarli", CANTOR "Roberto Rufino"
//     Orquesta; Cantor      ARTIST "Carlos di Sarli" and "Roberto Rufino",
//                           two values
//
// and ALBUM ARTIST, when written, is the orchestra in all of them.
//
// Standard C++ only.

#include <string>
#include <vector>

#include "discography.h"

namespace tangotagger
{
	enum class artist_scheme
	{
		orchestra_dash_singer,
		orchestra_slash_singer,
		singer_only,
		orchestra_and_cantor_field,
		orchestra_and_singer_values,
		count
	};

	//! "Orquesta - Cantor", for the scheme list.
	const char * artist_scheme_name(artist_scheme s);
	//! The scheme applied to an example: "Carlos di Sarli - Roberto Rufino".
	std::string artist_scheme_example(artist_scheme s);

	struct tag_options
	{
		artist_scheme scheme = artist_scheme::orchestra_dash_singer;
		bool title = true;
		bool artist = true;        //!< ARTIST, and CANTOR where the scheme has it
		bool album_artist = true;
		bool date = true;
		bool genre = true;
	};

	//! One field as it is to be written: all its values. No values removes
	//! the field.
	struct tag_value
	{
		std::string field;
		std::vector<std::string> values;
	};

	//! What the track has now, for the parts of the new tags that keep some of
	//! it.
	struct current_tags
	{
		std::string title;
		std::string date;
	};

	//! The fields `options` asks for, for a recording.
	std::vector<tag_value> recording_tags(const discography & d, const recording & r, const tag_options & options,
	                                      const current_tags & current);

	//! The recording's title, with the notes on the track's own title kept:
	//! "(decrackle)", "(2)", "(retuned declicked)" - lower case bracketed
	//! parts that are not part of the recording's title.
	std::string title_with_notes(const std::string & recording_name, const std::string & current_title);

	//! The recording's date, unless the track has a more precise one within a
	//! year of it - the discographies sometimes have only the month of a
	//! session the label dates to the day.
	std::string date_for(const recording & r, const std::string & current_date);
}
