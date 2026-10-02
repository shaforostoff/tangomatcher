#pragma once

// Which recording in the discographies a track is.
//
// Files come tagged every which way: "Carlos Di Sarli - Roberto Rufino" in
// ARTIST, or the orchestra alone with the singer in the title or not at all;
// "Di Sarli" or "Carlos di Sarli y su Orquesta Típica"; accents and commas
// missing; the date in DATE, or just the year, or the date in COMMENT; or
// nothing but a file name like "Di Sarli - Rufino - Al compas del corazon -
// 1942.mp3". So a track is not looked up field by field. Its title picks the
// recordings it could be - the same keys as the lyrics matching, exact or
// near - and every other scrap of text on it is evidence for or against each
// of them:
//
//  - orchestra: the leader's surname anywhere in the artist fields, the file
//    name or its folders ("Pugliese", "D'Arienzo", "Darienzo", one letter
//    off for longer names), and with it the first name, "Sexteto", the
//    director of the Orquesta Típica Víctor;
//  - singer: the recording's singers named, or "Instrumental", or - against
//    it - another of the orchestra's singers named instead;
//  - date: the full date, the month or the year, wherever on the track it
//    is; a year far off counts against;
//  - genre: tango, vals, milonga.
//
// When the track names an orchestra, only its recordings are candidates. When
// it names none, every recording of exactly the title is, the singer and the
// year deciding the order - offered, never confident.
//
// Standard C++ only, like title_match.h.

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "discography.h"

namespace tangotagger
{
	//! What a track carries that can identify it. Each string may hold
	//! several values of its fields, separated by anything.
	struct track_tags
	{
		std::string title;
		std::string artist;       //!< ARTIST
		std::string album_artist; //!< ALBUM ARTIST
		std::string performers;   //!< CONDUCTOR, PERFORMER, ORCHESTRA, VOCALS, SINGER, CANTOR...
		std::string album;
		std::string dates;        //!< DATE, YEAR, ORIGINAL DATE, RECORDING DATE...
		std::string comment;      //!< COMMENT, DESCRIPTION
		std::string genre;
		std::string path;         //!< the file's path: its name and folders are read too
	};

	struct date_parts
	{
		int year = 0, month = 0, day = 0;
	};

	//! "1941-10-09", "1941-10", "1941"; zeros for what is not there.
	date_parts parse_date(const std::string & date);

	//! Every date written anywhere in `text`: full dates in year-month-day or
	//! day-month-year order with any separators ("1941-10-09", "1941.10.09",
	//! "09/10/1941", "1941–10–09"), and years 1900-2029 standing alone.
	std::vector<date_parts> find_dates(const std::string & text);

	//! How a candidate stands on each kind of evidence. Positive for, negative
	//! against, 0 when the track says nothing either way.
	struct recording_match
	{
		int recording = -1;     //!< index into discography::recordings
		int title = 0;          //!< 10 exact; less for a near or partial title
		int orchestra = 0;      //!< 0 not named; 1 named weakly; 2+ named
		int vocal = 0;
		int date = 0;
		int genre = 0;
		int score = 0;          //!< the sum, orchestra weighted
		//! How well the track's sound agrees with a known transfer of the
		//! recording, in hundredths of the onset agreement (fingerprint.h);
		//! 0 when the sound was not compared or did not agree.
		int sound = 0;
		bool sound_identified = false;   //!< the sound alone says this is the recording

		bool title_exact() const { return title >= 10; }
	};

	struct track_match
	{
		std::vector<recording_match> candidates;   //!< best first
		//! The best candidate is right as far as the track can tell: its title
		//! and orchestra match, nothing on the track contradicts it, and no
		//! other candidate comes close.
		bool confident = false;
	};

	class recording_matcher
	{
	public:
		explicit recording_matcher(const discography & d);

		track_match find(const track_tags & tags) const;

		const discography & data() const { return m_data; }

		//! "title, orchestra, singer, date" - what a candidate matched on;
		//! "singer differs", "date differs" for what speaks against it.
		static std::string evidence_text(const recording_match & m);

	private:
		struct person
		{
			std::vector<std::string> surname;   //!< alternatives: "sarli", "disarli"
			std::vector<std::string> given;     //!< "carlos"
		};
		struct orchestra_model
		{
			std::vector<person> leaders;            //!< all must be named
			std::vector<std::string> qualifiers;    //!< "sexteto", "cuarteto"...
			std::vector<std::string> director;      //!< surname of "(dir. X)"
			std::vector<person> singers;            //!< everyone who sang with it
		};

		const discography & m_data;
		std::vector<orchestra_model> m_orchestras;
		std::vector<std::vector<person>> m_singers;   //!< per recording
		std::vector<std::vector<std::string>> m_keys; //!< per recording
		std::unordered_map<std::string, std::vector<int>> m_exact;
		std::vector<std::vector<int>> m_by_orchestra;
		//! Keys of every orchestra and singer name: a title that is one of
		//! these is a file name piece, not a title.
		std::unordered_set<std::string> m_names;
	};
}
