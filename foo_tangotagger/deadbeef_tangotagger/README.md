Tango Tagger for DeaDBeeF
=========================

foo_tangotagger as a [DeaDBeeF](https://deadbeef.sourceforge.io/) plugin:
the same lyrics and discography matching (`../core`, shared, not copied),
the same embedded data, the same results windows, and the same fields
written under the same rules, so a library tagged in either player reads the
same in the other. See [../README.md](../README.md) for what the matching
does; this file is about what is different in DeaDBeeF.

It needs DeaDBeeF 1.8.0 or later (plugin API 1.10). It builds for Linux,
macOS and Windows, with the results windows in GTK 3 for DeaDBeeF's GTK 3
interface and in Cocoa for DeaDBeeF for Mac. The layout follows
`deadbeef_rubato` in foo_bpm, which has the longer notes on toolkits,
MinGW and signing.

Building
--------

A C++17 compiler and CMake 3.16 or later; for the GTK windows the GTK 3
development files (`libgtk-3-dev` on Debian and Ubuntu, `gtk3-devel` on
Fedora). DeaDBeeF's plugin headers are in `include/`. The first configure
fetches the LZMA SDK into `../external` (`../scripts/get_sdk.sh -o
lzma_sdk`; on Linux that wants `bsdtar` or 7-Zip to unpack the `.7z`), and
bpmcore comes from a foo_bpm checkout beside this repository
(`-DFOO_BPM_DIR=...`) or from GitHub. Run these from `foo_tangotagger/`.

### Linux

    cmake -S deadbeef_tangotagger -B build/ddb -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ddb
    ctest --test-dir build/ddb

writes `build/ddb/ddb_tangotagger.so`, with the GTK windows and the lyrics
panel. `-DTT_DDB_GTK=OFF` builds it without them.

### macOS

    cmake -S deadbeef_tangotagger -B build/ddb-mac -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ddb-mac
    ctest --test-dir build/ddb-mac

writes `build/ddb-mac/ddb_tangotagger.dylib`, universal, ad-hoc signed, with
the Cocoa windows; it runs on macOS 10.13 on Intel and 11 on Apple Silicon.

For a release, `scripts/build_release_deadbeef_macos.sh` builds it universal,
runs the tests, moves the debug information into a `.dSYM`, strips and signs
the library again, checks its architectures, exports, links and that it
loads, and makes an installer of it,
`dist/ddb_tangotagger-<version>-macos-universal[-personal].pkg`, which puts
the library in the folder below for the current user. `--public-domain` makes
the build to publish; `--sign`, `--codesign` and `--notarize` sign and
notarize the installer as deadbeef_rubato's script does; `--install` also
copies the library straight into the plugin folder to try it. `--help` lists
the options.

### Windows

From an MSYS2 MINGW64 shell, for the GTK windows:

    cmake -S deadbeef_tangotagger -B build/ddb-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build/ddb-mingw

or with Visual Studio, without windows (`-A x64`, `--config Release`).

### Options

`-DFOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY=ON` embeds only the lyrics marked
public domain - the build to publish - exactly as for the component; the
default embeds every lyrics file and is not to be distributed. The data
folders are the component's options too (`TANGOTAGGER_LYRICS_DIR` and so
on). `-DTT_DDB_BUILD_TESTS=OFF` leaves the tests out.

Installing
----------

Copy the library, under exactly this name, to

| | |
|---|---|
| Linux   | `~/.local/lib/deadbeef/ddb_tangotagger.so` |
| macOS   | `~/Library/Application Support/Deadbeef/Plugins/ddb_tangotagger.dylib` |
| Windows | `%APPDATA%\deadbeef\plugins\ddb_tangotagger.dll` |

and restart DeaDBeeF. DeaDBeeF finds the entry point from the file name, so
a renamed library is skipped without a word. *Tango Tagger* should then be
listed under **Preferences > Plugins**.

Using it
--------

Select tracks and open **Tango Tagger** in the playlist's context menu, or
in a playlist tab's for the whole playlist:

* **Find lyrics...** - the lyrics window: one row per song a title matches,
  the checked ones written; **Add links to translations** below the list.
* **Match discographies...** - the discographies window: one row per
  recording a track could be, the artist scheme and the fields to write
  below the list. A track the tags do not place is listened to first, with a
  progress window; **Stop** ends that and opens the window with what there
  is.

Both windows are foo_tangotagger's, row for row and word for word: the same
rows pre-checked, the same preview, the same status line. They can be
resized, and several can be open at once. The settings they keep - the links
to translations, the artist scheme, the fields - are also on **Preferences >
Plugins > Tango Tagger > Configure**.

Without windows - a build with neither toolkit, or DeaDBeeF's GTK 2
interface - both actions write what the window would have shown checked, and
list every track in the log (**View > Log**): what was written, and why the
rest was left alone.

### The lyrics panel

Under the GTK interface, **View > Design Mode** offers **Tango Lyrics**: the
lyrics of the track under the cursor, or of the one playing - the file's
own, or else the built-in song its title matches, marked as not in the file,
with composer, lyricist, source and the links to translations, which open in
the web browser. DeaDBeeF for Mac has no way for a plugin to add a widget to
its layout, so there it is not offered. DeaDBeeF's own Lyrics widget shows
what Find lyrics... has written - except in an mp3: it reads only the
`lyrics` field, and an mp3's lyrics are in USLT, which DeaDBeeF calls
`UNSYNCED LYRICS`. They go there all the same, because USLT is where every
other player and tagger looks.

The fields
----------

DeaDBeeF names some fields after its own table rather than foobar2000's,
read off its sources (`src/junklib.c`, `shared/mp4tagutil.c`,
`plugins/flac/flac.c`):

| | mp3 (ID3v2) | m4a | FLAC, Ogg, APE... |
|---|---|---|---|
| lyrics | `UNSYNCED LYRICS` (USLT) | `LYRICS` (©lyr) | `LYRICS` |
| DATE | `year` (TDRC, or TYER and TDAT) | `year` (©day) | `year` (DATE) |
| ALBUM ARTIST | `band` (TPE2) | `band` (aART) | `ALBUM ARTIST` (ALBUMARTIST) |

which land in the same frames and atoms foobar2000 writes. Writing ALBUM
ARTIST removes the field's other spellings (a `TXXX:ALBUM ARTIST` beside
TPE2), so a file does not carry two answers. Lyrics are written with CRLF
line endings, as foo_tangotagger writes them.

A track inside a cue sheet or other multi-track file is listed with its
match but cannot be checked: DeaDBeeF does not write tags into one track of
a file, and its own track properties dialog does not either.

Tests
-----

* `ddb_tangotagger_hosttest` loads the plugin into a stand-in for DeaDBeeF
  (`tests/fake_host.h`) and runs both actions on the worker threads against
  the real embedded data: the fields written per container, CRLF and the
  links, files that already have lyrics, cue sheet tracks, the unselected
  track left alone, listening to a track with no tags, and every reference
  given back. It runs once without windows and once with a window system
  that records what it is asked to show (`tests/ui_recorder.cpp`), which
  covers the results windows' model - columns, cells, previews, options and
  writing once. It needs no DeaDBeeF and no display.
* `tt_cocoa_preview` and `tt_gtk_preview`, in a Cocoa or GTK build, put the
  real windows up over the same stand-in with made-up tracks
  (`tests/preview_tracks.h`); given a directory, they save each window to a
  PNG there and exit. Not run by `ctest`.

Files
-----

* `tt_plugin.cpp` - the plugin: actions, settings, the worker threads,
  decoding for the matching by sound, and writing.
* `tt_fields.h` - a track's tags as copied out of the player, and
  DeaDBeeF's field names.
* `tt_lyrics.h`, `tt_disco.h` - what the results windows show and write; a
  port of `../foo_tangotagger/lyrics_rows.cpp`, `lyrics_panel_content.cpp`
  and `disco_rows.cpp` onto `std::string`, to be kept in step with them.
* `tt_review.h` - the one results window both actions show, as the
  toolkits see it.
* `tt_ui.h`, `ui_none.cpp`, `gtk/ui_gtk.cpp`, `cocoa/ui_cocoa.mm` - the
  windows.
* `include/deadbeef/` - DeaDBeeF's plugin header and its GTK interface's,
  from the 1.10.3 release tag, unmodified (zlib licence, in each file).
* `compat/msvc/` - the two POSIX headers `deadbeef.h` wants, for MSVC.
