# LyrMatcher for macOS

A native Swift/SwiftUI rewrite of the Qt [`lyrmatcher`](../../lyrmatcher) app: it matches the
XML lyrics files in [`lyrics-xmldata`](../../lyrics-xmldata) against a music library by filename
and embeds the lyrics into the matching MP3, M4A and FLAC files.

No Qt, no TagLib, no other dependency — the ID3v2, MP4 and FLAC tag writers are part of this
package.

## Building

```bash
make app
```

That produces `LyrMatcher.app` next to this README (SwiftPM builds the binary, the Makefile wraps
it in a bundle and ad-hoc signs it so macOS remembers the folder access it grants). `make run`
builds and launches it; `make test` runs the suite.

Requires macOS 13 or later and Swift 5.9 (Xcode 15). `open Package.swift` also works if you would
rather build from Xcode.

### Release packaging

```bash
./package.sh
```

Runs the tests, builds a universal (arm64 + x86_64) binary, stamps the version into the bundle,
ad-hoc signs it, and writes `dist/LyrMatcher-<version>-macos-universal.tar.xz` plus a `.sha256`
alongside it. The archive holds the app, `README.md`, `LICENSE` and an `INSTALL.txt`; it is
unpacked and re-verified before the script reports success.

The version comes from `git describe` when a `v*` tag exists, otherwise from `Info.plist` with
the short commit appended. `--version`, `--arch native`, `--skip-tests` and `--output` override
the defaults — see `./package.sh --help`.

The build is ad-hoc signed, not Developer ID signed or notarised, so Gatekeeper blocks the first
launch on another machine. `INSTALL.txt` in the archive tells the recipient how to get past it.

## Using it

1. Pick the folder holding the `*.xml` lyrics files (bottom left).
2. Pick the root of the music library (top right). It is indexed recursively once; press ⌘R or
   the refresh button after adding files.
3. Select a lyrics file. The pane shows every translation it contains; the list underneath shows
   the music files it matched.
4. **Write tags** (⌘↩) embeds the lyrics into every listed file.

| Shortcut | Action |
| --- | --- |
| ⌘L | Focus the lyrics-file filter |
| ⌘G | Jump to the next lyrics file that has matches |
| ⌘↩ | Write lyrics to the matched files |
| ⇧⌘↩ | Write, then jump to the next file with matches |
| ⌘R | Re-index the music folder |
| ⌘. | Cancel a running write |

Double-click a matched file to play it; right-click for **Reveal in Finder**.

Writing runs off the main thread. The status bar shows a determinate progress bar, the file
currently being tagged, and a Cancel button — tagging 80 FLACs means rewriting close to a
gigabyte, which takes around 15 seconds. Cancelling stops before the next file; files already
written stay written.

### Toggles

- **Add minuses** — on by default. Applies both boundary constraints to a title whose filename
  carries no markers of its own, so it only matches a whole ` - ` separated field. Without it,
  `Nada.xml` also matches `Nada mas que un corazon`. On a `NNN - Title.ext` library this costs
  almost no coverage and removes most false hits, which is why it defaults on.
- **Fuzzy** — off by default. When nothing matches literally, falls back to edit distance against
  each ` - ` separated part of the filename, accepting anything under 20 % different. Fuzzy hits
  are badged in the results list. This will occasionally pair genuinely different titles
  (`Maleza` / `Malena`), so review before writing.
- **Overwrite** — off by default; files that already carry lyrics are skipped. Turn it on to
  replace them.
- **Spanish only (Exclude translations)** — off by default. Most XML files carry the Spanish
  original plus English and/or Russian translations, and all of them are written. Turn this on to
  embed only the `spa` version. The preview pane follows the setting, so you always see what will
  be written. Titles with no Spanish version are then skipped, and the status line reports how
  many.

  A `<translation>` with no `lang` attribute (or an empty one) counts as Spanish: it is kept by
  this filter and its `USLT` frame is tagged `spa`. Every file currently in `lyrics-xmldata`
  labels its languages, so this only matters for hand-edited additions.

## How matching works

The XML filenames encode the match themselves, and that convention is preserved:

| Filename | Means |
| --- | --- |
| `Poema.xml` | `Poema` anywhere in the music filename |
| `- Nada.xml` | `Nada` must start the filename or a ` - ` separated field |
| `Intima -.xml` | `Intima` must end one — also matches `Intima (instrumental)` |
| `- Volver -.xml` | both ends |
| `Malena_.xml` | a parked alternate version of `Malena.xml` |

The markers are boundary constraints, not literal text. Qt spliced them into a `*stem*` glob,
which quietly required something to follow the title, so `Una carta -.xml` could never match
`014 - Una carta.flac` — a library named `NNN - Title.ext` puts the title last. Reading them as
constraints keeps what they are for (`- Nada.xml` still does not match `Nada mas`) and works at
either end of the name.

The other difference from the Qt version is that both sides are normalised before comparing, so the
usual mismatches disappear: accents (`ñ`→`n`, `Adiós`/`Adios`), underscores standing in for
spaces (`Di_Sarli_-_Que_vas_buscando_muñeca`), missing commas and exclamation/question marks
(`¡Rie, payaso!` / `Rie payaso`), typographic quotes and dashes, and casing. Hyphens and
parentheses survive normalisation because the boundary markers above depend on them.

See [`TextNormalization.swift`](Sources/LyrMatcherCore/TextNormalization.swift) and
[`TitleMatcher.swift`](Sources/LyrMatcherCore/TitleMatcher.swift).

## What gets written

Same payloads as the Qt/TagLib version, CRLF line endings included, so files tagged by either app
look identical to a player.

"Translation" below means each `<translation>` element of the XML file that survives the
**Spanish only** filter.

- **MP3** — one `USLT` frame per translation, each carrying its own ISO-639-2 language code and
  the translation's title as the content descriptor. A file's existing tag version is kept: v2.3
  tags get UTF-16 lyrics (the only Unicode encoding v2.3 allows), v2.4 and new tags get UTF-8.
  All other frames, artwork included, are preserved byte for byte.
- **FLAC** — a single `LYRICS` Vorbis comment with all translations concatenated. Other fields
  and metadata blocks are untouched.
- **M4A / MP4** — a single `©lyr` atom under `moov/udta/meta/ilst`, created along with the
  containers if the file has none. Resizing `moov` re-bases the `stco` / `co64` chunk offsets so
  the file still plays.

Every write goes to a temporary file next to the original and is swapped in atomically, so an
interrupted write cannot leave a half-tagged file.

## Layout

```
Sources/LyrMatcherCore/     matching + tag writing, no UI (unit tested)
  TextNormalization.swift   accent/punctuation folding
  TitleMatcher.swift        search stems, boundary markers, fuzzy fallback
  MusicIndex.swift          recursive library scan, cached normalised names
  LyricsDocument.swift      <translation> XML parsing
  LyricsPayload.swift       the exact strings written into tags
  TagWriting/               ID3v2, FLAC and MP4 writers
Sources/LyrMatcher/         SwiftUI app
Tests/LyrMatcherCoreTests/  44 tests, including a real AAC round trip via afconvert/AVFoundation
```

## Differences from the Qt version

- The music library is indexed once instead of re-walked for every selection.
- The `- ` / ` -` filename markers are boundary constraints rather than literal glob text, so a
  title also matches at the start or end of a filename. See "How matching works" above.
- The `Title (` fallback glob is always tried; Qt only tried it when the primary glob had already
  hit, which looked accidental.
- ⌘G ("Find next with matches") searches with the current **Add minuses** setting. Qt's F3 forced
  the markers on for the scan, so it walked past titles the results list would have shown.
- "Write all" de-duplicates lyrics files whose titles normalise identically (`Malena.xml` and
  `Malena_.xml`), which would otherwise overwrite each other, and asks for confirmation first.
- Writing a file that already has lyrics skips just that file; the Qt code returned out of the
  whole loop.
- The "Generate xhtml" export was not carried over.
- FLAC files get only the Vorbis comment. Qt also poked at an ID3v2 tag inside FLAC, but only if
  one already existed, which is not the normal case and is not a standard place to look.
