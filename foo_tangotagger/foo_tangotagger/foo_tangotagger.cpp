#include "stdafx.h"

#include "lyrics_db.h"
#include "version.h"

// The notice of the transform bpmcore was built with, as foo_rubato carries it.
#include <foo_rubato/fft_license.h>

#ifdef _WIN32
#define FOO_TANGOTAGGER_PLATFORM \
	"Built against the foobar2000 SDK " FOO_TANGOTAGGER_SDK_VERSION "; runs on\n" \
	"foobar2000 for Windows 1.5 or newer, 32 and 64 bit.\n"
#else
#define FOO_TANGOTAGGER_PLATFORM \
	"Built against the foobar2000 SDK " FOO_TANGOTAGGER_SDK_VERSION "; runs on Intel and\n" \
	"Apple Silicon foobar2000 for Mac 2.6 or newer.\n"
#endif

#ifdef _WIN32
#define LYRICS_PANEL_HELP \
	"The Lyrics panel (Selection Information in the layout editor) shows the\n" \
	"selected track's lyrics - the file's own, or the built-in ones its title\n" \
	"matches. Ctrl+mouse wheel or the right-click menu sets the text size; links\n" \
	"open in the web browser.\n"
#else
#define LYRICS_PANEL_HELP \
	"The tango-lyrics layout element shows the selected track's lyrics - the\n" \
	"file's own, or the built-in ones its title matches. Cmd+Plus/Minus, a pinch\n" \
	"or the right-click menu sets the text size; links open in the web browser.\n"
#endif

#define DISCOGRAPHIES_HELP \
	"Tango Tagger > Match discographies finds the selected tracks' recordings in\n" \
	"the orchestra discographies built into the component - by the title, and by\n" \
	"the orchestra, singer and date wherever the tags, the file name or its\n" \
	"folder mention them - and fixes their title, artist, album artist, date and\n" \
	"genre, with the singer where you want it: \"Orquesta - Cantor\", \"Orquesta /\n" \
	"Cantor\", the singer alone, a CANTOR field, or two artist values. A track the\n" \
	"tags cannot place is matched by its sound, against fingerprints of known\n" \
	"transfers of the recordings.\n"

#define DISCOGRAPHIES_DATA \
	"Discographies from todotango.com, tango.info and tangoteca, and of Aníbal\n" \
	"Troilo, Carlos di Sarli, Edgardo Donato, Horacio Salgán, Juan D'Arienzo,\n" \
	"Lucio Demare, the Orquesta Típica Victor, Osvaldo Fresedo, Pedro Laurenz\n" \
	"and Rodolfo Biagi from Tango Time Travel.\n"

#if FOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY
#define FOO_TANGOTAGGER_DATA \
	"The lyrics are in the public domain in Argentina: their authors died more\n" \
	"than 70 years ago. Texts from todotango.com; credits from tango.info and wikipedia.\n" \
	DISCOGRAPHIES_DATA
#else
#define FOO_TANGOTAGGER_DATA \
	"This copy carries lyrics that are not in the public domain, contact me if you want specific lyrics to be removed.\n" \
	"Texts from todotango.com; credits from tango.info and wikipedia.\n" \
	DISCOGRAPHIES_DATA
#endif

DECLARE_COMPONENT_VERSION(
	FOO_TANGOTAGGER_NAME,
	FOO_TANGOTAGGER_VERSION,
	"Writes tango lyrics and discography data into your files.\n"
	"\n"
	"Select tracks, right-click, Tango Tagger > Find lyrics. Each\n"
	"track's title is matched against the lyrics built into the component -\n"
	"ignoring accents, case, punctuation and spacing, and never matching a\n"
	"title by part of it, so \"Cafe\" is not \"Cafe Dominguez\". The matches are\n"
	"listed with checkboxes; the checked ones are written to LYRICS (UNSYNCED\n"
	"LYRICS, the ID3 USLT frame, for MP3).\n"
	"\n"
	LYRICS_PANEL_HELP
	"\n"
	DISCOGRAPHIES_HELP
	"\n"
	FOO_TANGOTAGGER_DATA
	"\n"
	FOO_TANGOTAGGER_PLATFORM
	"\n"
	"(c) 2026 Nick Shaforostov. The component's code is released under the MIT\n"
	"License.\n"
	"\n"
	"The Tango Time Travel discographies are (c) Tango Time Travel / Moving Art\n"
	"Studio ASBL, https://tangotimetravel.be/category/release-notes/, licensed\n"
	"under Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA\n"
	"4.0), https://creativecommons.org/licenses/by-sa/4.0/. Changes: converted\n"
	"from the original spreadsheets, spellings normalised, and merged with the\n"
	"other discographies, which leave out the recordings these have. The match\n"
	"window names the discography, version and date of every recording taken\n"
	"from them. The discography data in this component, adapted from theirs, is\n"
	"shared under the same licence. Tango Time Travel does not endorse this\n"
	"component.\n"
	"\n"
	"Used libraries:\n"
	"LZMA SDK by Igor Pavlov - public domain.\n"
	"bpmcore from foo_bpm (https://github.com/shaforostoff/foo_bpm) - MIT License.\n"
	FOO_RUBATO_FFT_LICENSE
);

VALIDATE_COMPONENT_FILENAME("foo_tangotagger.dll");
