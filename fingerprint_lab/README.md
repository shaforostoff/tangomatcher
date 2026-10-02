# Fingerprint lab

An experiment to find out whether audio fingerprints can identify a tango
recording reliably across transfers: shellac against vinyl, remaster against
needle drop, and turntable speeds a few percent apart. If they can, the
tagger can identify `02 Track02.wav` with no tags at all.

TangoTunes (`C:\TangoTunes`) is the reference collection: it has the best
transfers, at the right speed and without added reverb. Queries come from
`C:\SortedTangoSpanishNamesFLAC` and `C:\SortedTangoSpanishNames`.

## What is measured

Per file, from bpmcore (`C:\Dev\foo_bpm`):

- the tuning offset against A=440, in cents. A wrong turntable speed shifts
  pitch and tempo together, so this offset is also a speed gauge: every
  transfer brought to tuning zero runs at the same speed, up to a whole
  number of semitones, since the tuning is known only modulo 100 cents;
- chroma per 93ms frame, with that offset already taken out;
- the onset novelty curve at 86Hz, which carries the timing of the
  performance itself.

The matching (in `evaluate.py`):

1. Brings both tracks to tuning zero, and tries the query at -1, 0 and +1
   semitones (chroma rotated, time scaled to match).
2. Coarse: chroma at 0.2s against every reference, every offset within 30s.
3. Fine, for the five best: a small residual speed searched, then the onset
   curves compared in 8 second blocks. A second transfer of one recording
   lines up block after block; a re-recording of the same tune by the same
   orchestra, with the same arrangement in the same key, does not, because
   the rubato is never the same twice.

## Running it

```
cmake -S . -B build -A x64
cmake --build build --config Release
py -3 index_sources.py      # tags of the three collections -> data/sources.tsv
py -3 make_sample.py        # references and queries -> data/sample.tsv, data/extract.tsv
build\Release\fp_extract.exe data\extract.tsv data\features
py -3 evaluate.py           # -> data/results.tsv and a summary
py -3 evaluate.py --mode raw  # baseline without the speed normalisation
py -3 compact.py             # the embedded format, in Python
py -3 cpp_eval.py            # the embedded format, foo_tangotagger's C++ (build fp_eval first)
py -3 identify.py ...        # untagged files against all three collections, in Python
```

Ground truth comes from the tags: the same orchestra, title and full
recording date in two collections is taken to be the same recording, and a
different date for the same orchestra and title is a re-recording. Tags are
sometimes wrong, so a confident fingerprint match that disagrees with them is
worth a look rather than counted as a failure without one.

`fp_extract` needs the `key_frames` output of `bpmcore::compute_key`
(`foo_bpm/bpmcore/internal.h`).

## Results (2026-10-01)

2,414 TangoTunes references, 2,954 queries: 1,789 files from the FLAC and
MP3 collections whose tags name a recording TangoTunes has, 841 second
TangoTunes transfers, 324 files whose tags name only a re-recording of a
title TangoTunes has.

The true recording ranked first in the coarse stage for 97% of the queries
(1,737 of 1,789; 831 of 841). The onset comparison separates the rest:

| onset agreement | same recording | best wrong candidate |
|---|---|---|
| median | 0.96 | 0.09 |
| 5th-95th percentile | 0.87-0.98 | 0.04-0.30* |

\* Most of the high wrong-candidate scores turned out to be tag problems, not
fingerprint errors: the same recording under "Juan D'Arienzo" and "Juan
D'Arienzo y su Orquesta Típica", untagged TangoTunes files, and dates that
differ by days or have swapped digits (Tinta verde 1945/1954).

Decision rule, on an 800-query subset (`report.py`), with speed from the
tuning offset and a direct speed search where that falls short (hybrid):

| | identified (>= 0.85) | probable (0.70-0.85) | none |
|---|---|---|---|
| same recording in TangoTunes | 97.5% | 2.2% | 0.3% |
| only a re-recording in TangoTunes | 0 wrong** | 6.4% wrong | 72% |

\*\* 3 contradict the tags, and all 3 are tag errors (the remaining 19% match a
recording whose tags agree once the spelling and date problems above are
allowed for).

Every wrong match in the probable band is a D'Arienzo instrumental re-recorded
in the 1950s with the 1940s arrangement: his tempo is steady enough that two
performances line up. So the probable band needs a person to confirm.

The tuning offset predicts a transfer's speed well but not always: some files
in the collections have been retuned or time-stretched digitally, which
separates pitch from tempo. Searching the speed directly finds those, at about
three times the matching cost, so it is the fallback rather than the default.

Transfers in the FLAC and MP3 collections run between 2.4% slower and 3%
faster than TangoTunes (5th-95th percentile, median +0.7%).

## The embedded format (2026-10-02)

The component cannot carry 150 KB of features per reference, nor hold every
reference's spectrum in memory. `compact.py` tested what it does carry,
and `fp_eval.cpp` / `cpp_eval.py` run foo_tangotagger's C++
(`core/fingerprint.cpp`) over the same 800 queries:

- chroma in 0.25 s bins, 2 bits per pitch class, the first 90 seconds of
  the music; the strongest onsets, 4 a second, at 10 ms with a 4-bit
  strength - about 1.8 KB a reference;
- onsets rendered 30 ms wide for comparison (15 ms lost 20 points of
  identification; more onsets did not help);
- a prefilter on duration (±15%) and overall chroma keeps 300 candidates
  for the alignment; the true recording survived it every time.

The C++ and Python agree. With the onsets alone, D'Arienzo's 1950s
re-recordings of his 1940s arrangements reached the probable band more often
than with the full features, and one ("Don Juan", 1950 against 1948) reached
0.86. What separates them is the straight line: the best shift of each 8
second block lies on a line for two transfers (median residual 3 ms, 95% under
19 ms) but wanders for two performances (8-67 ms). Identified therefore means
onset agreement >= 0.85 *and* a residual <= 25 ms:

| | identified | probable | none |
|---|---|---|---|
| same recording in TangoTunes (691) | 96.2% | 3.3% | 0.4% |
| only a re-recording in TangoTunes (109) | 0 wrong* | 16.5% wrong | 62% |

\* 3 contradict the tags, all tag errors; 18% match a recording whose tags
agree once the spelling and date problems are allowed for.

800 queries against 2,414 references take 35 seconds in C++ on 16 threads.

## Smaller fingerprints (2026-10-02)

`sweep.py` emulates smaller formats in `fp_eval` (`FP_VARIANT`) and scores
each on the same 800 queries; sizes are after LZMA, as the component ships
them (format 1: 1,249 bytes).

| change | identified | verdict |
|---|---|---|
| chroma 1 bit a pitch class (above the bin's mean) | 96.1% | free |
| 3 onsets a second instead of 4 | 96.2% | free |
| chroma in 0.5 s bins | 94.2% | -2 points |
| 2.5 / 2 onsets a second | 94.9% / 94.6% | -1 / -1.5 points |
| onset strength in 8 / 4 levels / none | 91.5% / 84.9% / 38.4% | the strength matters |
| 60 s excerpt | 96.2% | D'Arienzo's "Don Juan" re-recording identified again |

Format 2 takes the two free ones: 794 bytes after LZMA, 36% less, and
95.8% identified, 3.8% probable, 0.4% missed, no wrong identification
beyond the tag errors. An entropy coder would not help: with context models
the estimated floor is 749 bytes. Byte-aligned chroma compresses to 776 but
makes the XML 17% larger.
