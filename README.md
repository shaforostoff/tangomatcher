# tangomatcher

## Tango Tagger for foobar2000

[foo_tangotagger](foo_tangotagger) is a foobar2000 component (Windows and
macOS) that writes tango lyrics and discography data into your files. A
[DeaDBeeF variant](foo_tangotagger/deadbeef_tangotagger) runs on Linux, macOS
and Windows.

![Tango Tagger: the Match discographies window and the Lyrics panel](foo_tangotagger.png)

- **Find lyrics**: matches track titles against the built-in lyrics, with
  composer and lyricist, and writes the ones you check. Links to
  translations can be added as well.
- **Lyrics panel**: shows the selected or playing track's lyrics, from the
  file or from the built-in collection.
- **Match discographies**: finds each track's recording among about 12,500
  recordings of 59 orchestras, using its title, orchestra, singer, date and
  genre wherever they appear in the tags or the file name. It then fixes
  TITLE, ARTIST, ALBUM ARTIST, DATE and GENRE, and you choose how ARTIST
  names the singer.
- **By sound**: a track the tags can't place, like `02 Track02.wav`, is
  identified by an audio fingerprint of its first 90 seconds. This works
  regardless of codec, noise, EQ or transfer speed.

Everything is embedded in the component, so it needs no network access.
Select tracks, right-click, **Tango Tagger**. See the
[full README](foo_tangotagger/README.md) for how matching works, the data
sources and building.

The code is under the [MIT License](foo_tangotagger/LICENSE). The
discography data adapted from Tango Time Travel is under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).

## tangomatcher app

A small app to classify tango mp3/m4a/flac files according to orchestra
discographies (parsed from todotango, tango.info).

The matching is done based on filenames and tags. After matching finishes, it
is possible to rename files in a standard way and write discography info to
tags, including exact dates, composer and lyricist info.
