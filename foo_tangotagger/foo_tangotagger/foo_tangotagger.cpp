#include "stdafx.h"

#include "lyrics_db.h"
#include "version.h"

#ifdef _WIN32
#define FOO_TANGOTAGGER_PLATFORM \
	"Built against the foobar2000 SDK " FOO_TANGOTAGGER_SDK_VERSION "; runs on\n" \
	"foobar2000 for Windows 1.5 or newer, 32 and 64 bit.\n"
#else
#define FOO_TANGOTAGGER_PLATFORM \
	"Built against the foobar2000 SDK " FOO_TANGOTAGGER_SDK_VERSION "; runs on Intel and\n" \
	"Apple Silicon foobar2000 for Mac 2.6 or newer.\n"
#endif

#if FOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY
#define FOO_TANGOTAGGER_DATA \
	"The lyrics are in the public domain in Argentina: their authors died more\n" \
	"than 70 years ago. Texts from todotango.com; credits from tango.info and wikipedia.\n"
#else
#define FOO_TANGOTAGGER_DATA \
	"This copy carries lyrics that are not in the public domain, contact me if you want specific lyrics to be removed.\n" \
	"Texts from todotango.com; credits from tango.info and wikipedia.\n"
#endif

DECLARE_COMPONENT_VERSION(
	FOO_TANGOTAGGER_NAME,
	FOO_TANGOTAGGER_VERSION,
	"Writes tango lyrics into your files.\n"
	"\n"
	"Select tracks, right-click, Tango Tagger > Find lyrics. Each\n"
	"track's title is matched against the lyrics built into the component -\n"
	"ignoring accents, case, punctuation and spacing, and never matching a\n"
	"title by part of it, so \"Cafe\" is not \"Cafe Dominguez\". The matches are\n"
	"listed with checkboxes; the checked ones are written to LYRICS (UNSYNCED\n"
	"LYRICS, the ID3 USLT frame, for MP3).\n"
	"\n"
	FOO_TANGOTAGGER_DATA
	"\n"
	FOO_TANGOTAGGER_PLATFORM
	"\n"
	"(c) 2026 Nick Shaforostov\n"
	"\n"
	"Used libraries:\n"
	"LZMA SDK by Igor Pavlov - public domain.\n"
);

VALIDATE_COMPONENT_FILENAME("foo_tangotagger.dll");
