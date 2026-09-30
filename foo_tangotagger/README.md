# Tango Tagger for foobar2000

Writes tango lyrics into your files.

Select tracks, right-click, **Tango Tagger > Find lyrics...**.
Each track's title is matched against the lyrics built into the component, and
the matches are listed with checkboxes; the checked ones are written to the
file. The selected row's lyrics, with composer and lyricist, are shown below
the list.

## Matching

Titles are compared as *keys*: accents folded (Años = Anos), case, punctuation
and spaces dropped (`Yira, yira` = `Yira yira`, `¿Volverás? ¿Pero cuándo?` =
`Volveras pero cuando`). A match is always the whole key - never a prefix - so
`Cafe` does not match `Cafe Dominguez` and `Canto` does not match `Canto de amor`.

A title also answers to:

- the title without bracketed notes: `La lluvia y yo (vals)`, `[Remastered 2004]`;
- an alternative title in brackets: `Frou Frou (Fru Fru)` is found as `Fru Fru`;
- the pieces between dashes: `Carlos Di Sarli - Rie payaso - 1940`.

When nothing matches exactly, a near miss (1 edit on titles of 10+ letters, 2
on 16+) is listed as *similar* and left unchecked. A title several songs share
lists each of them, and the track's COMPOSER / LYRICIST tags pick one if they
name exactly one.

Pre-checked: exact matches onto files without lyrics. Files that already have
lyrics are listed (`same` / `different`) but left unchecked.

## Tags

| Format              | Field             | Stored as        |
|---------------------|-------------------|------------------|
| MP3                 | `UNSYNCED LYRICS` | ID3v2 USLT frame |
| MP4 / M4A           | `LYRICS`          | ©lyr atom        |
| FLAC, Ogg, Opus ... | `LYRICS`          | LYRICS field     |

## Data

The lyrics are read from `../publicdomain-lyrics` at build time by
`tools/pack_lyrics` and embedded LZMA-compressed (7-Zip's LZMA SDK) in the
component. Two builds:

- **personal** (default): every lyrics file in the folder. The archive is named
  `foo_tangotagger-<version>-personal.fb2k-component` and the about box says it
  must not be distributed.
- **public domain** (`build_release.ps1 -PublicDomain`,
  `build_release_macos.sh --public-domain`, or
  `-DFOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY=ON`): only files marked
  `pd_status="public domain"`. This is the one to publish.
`tools/match_titles` runs the same matching from the command line
(`title<TAB>credits` lines on stdin).

## Building

    powershell -ExecutionPolicy Bypass -File scripts\build_release.ps1

or on macOS `scripts/build_release_macos.sh`. The foobar2000 SDK, WTL and the
LZMA SDK are downloaded on the first configure (`scripts/get_sdk.ps1`).
