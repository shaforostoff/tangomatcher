Storing the decoded fingerprints compactly
==========================================

Option B below is done (`fingerprint_index::add_encoded`); option A is
still a proposal. Measured on the personal macOS build, October 2026: 6,254
embedded fingerprints.

Where the memory goes
---------------------

`embedded_fingerprints()` decodes every embedded fingerprint once, on the
first track that is listened to, and keeps them for the life of the player.
A decoded fingerprint (`core/fingerprint.h`) is, on average:

| Part | Layout | Per fingerprint | All 6,254 |
|---|---|---|---|
| chroma | 360 bins x 12 `uint8_t`, each 0 or 1 | 4.3 KB | 25.8 MB |
| onsets | 270 x `{uint32_t time; uint8_t strength}`, 8 bytes with padding | 2.2 KB | 12.9 MB |
| the rest | `fingerprint` (72 bytes), id, `profile[12]` as doubles | 0.2 KB | 1.1 MB |
| | | | **about 40 MB** |

The same data encoded (`encode_fingerprint`) is 6.2 MB: chroma at 12 bits a
bin, onsets as varint deltas. The decoded form spends 12 bytes where 12 bits
would do, and 8 bytes where 4 would do.

That is the largest part of what the plugin, and foo_tangotagger, keep after
the first discography match by sound: about 74 MB resident in all, with the
lyrics and the discographies loaded too. The heap in use grows by about
35 MB when the index is built - a little less than the table, which counts
capacity.

Option A: a compact decoded form
--------------------------------

Change the in-memory layout only; the encoded format, the embedded data and
`xml-fingerprints` stay as they are.

**Chroma: one `uint16_t` a bin**, the 12 pitch classes as bits, bit `c` for
pitch class `c` - the encoder's layout, without the packing of two bins into
three bytes:

```cpp
std::vector<std::uint16_t> chroma;   // per 0.25 s bin, bit c = pitch class c
std::size_t bins() const { return chroma.size(); }
```

25.8 MB becomes 4.3 MB. The readers, all in `core/fingerprint.cpp`:

- `chroma_sequence` reads a bin as twelve 0/1 values, centres them and
  scales them to unit length. For a bit pattern with `p` bits set, that is
  `(1 - p/12) / n` for a set bit and `-(p/12) / n` for a clear one, with
  `n = sqrt(p * (1 - p/12))`; zero for `p == 0` or `p == 12`. A table of 13
  pairs indexed by `__builtin_popcount` (or a 12-step loop where there is no
  builtin) gives the same values: equal for `p` and `12 - p` up to
  floating-point rounding, so the results need comparing with the current
  ones bit for bit, as below.
- `fingerprint_index::add` and `identify` sum each pitch class over the bins
  for the overall profile: `(chroma[b] >> c) & 1`.
- `make_fingerprint` writes bins; `encode_fingerprint` and
  `decode_fingerprint` copy the 12 bits instead of splitting them out.
- `core_test` and `tools/fingerprints` check `bins()` and compare decoded
  with original fingerprints; `fingerprint_lab/fp_eval.cpp` builds its own
  fingerprints and would need the same change.

**Onsets: 4 bytes each**, with bit fields:

```cpp
struct onset
{
    std::uint32_t time : 28;      // from the start of the file, in 10ms
    std::uint32_t strength : 4;   // 1-15
};
```

28 bits of 10 ms is 31 days; `strength` already only ever holds 1-15, as
the encoder's `& 15` assumes. `o.time` and `o.strength` read as before, so
the only changes are where the code takes their address or relies on
`sizeof(onset)`, which nothing does. 12.9 MB becomes 6.5 MB.

`profile[12]` could become `float`: 0.3 MB, not worth the change in rounding.

Altogether about 40 MB becomes about 12 MB. The time per `identify` should
not get worse: the chroma of a candidate is read once per reading of the
query, from a smaller array.

Option B: keep the fingerprints encoded (done)
----------------------------------------------

The index keeps each reference's encoded bytes and decodes the candidates
inside `identify`, after the prefilter:

```cpp
struct reference
{
    int id;
    std::string encoded;   // encode_fingerprint's bytes
    float duration;        // what the prefilter reads, decoded up front
    int tuning;
    double profile[12];
};
```

- The prefilter needs only `duration` and `profile`, filled in by `add`.
- The survivors - at most 300, plus the recordings the tags point at - are
  decoded where `spectrum_of(chroma_sequence(...))` is computed for them,
  one at a time, and dropped with the spectrum (see the coarse pass, which
  already handles one candidate at a time). The fine pass decodes its five
  again, or keeps them.
- `embedded_fingerprints()` moves the strings out of the discography
  (`take_embedded_fingerprint_data()`) instead of copying them, so there is
  one copy of the bytes, not two.
- `add(id, fingerprint)` still keeps a reference decoded, for the tools and
  tests that build an index from fingerprints that were never encoded.

Measured once done: the heap grows by 1.7 MB when the index is built, not
about 35 MB - the bytes were already in memory, in the discography - and
the process holds 26 MB resident after loading the discographies and the
index, not 69 MB. Building the index takes 79 ms instead of 227 ms. A
decode takes 6.4 µs, so the 300-odd candidates of an `identify` cost about
2 ms, twice when the speed search runs, against 400-600 ms for the call:
the time for the 24 test queries is the same within the noise. The results
are identical, field for field, to the decoded index's.

The encoded format is then a format the index reads while matching, not
only while loading, which is a reason to keep `decode_fingerprint` fast.

Both
----

A and B combine: B's decoding into A's layout is cheaper (no splitting the
chroma out into bytes), and queries - which are never encoded - get A's
smaller layout too. Queries are built once per track, so that matters less.

Recommendation
--------------

B, for the larger saving with the smaller change: the matching code reads
the same `fingerprint` it reads now, and only `fingerprint_index` changes.
Done. A would now save only the queries' memory - one fingerprint per track
being listened to - and the candidate being compared: not worth it alone.

Checking it
-----------

- The scratch comparison used for the coarse-pass change: identify 24
  queries - embedded references as they are, with a wrong tuning offset
  (which forces the speed search), and cut short - with the old and the new
  code, and compare every field of every result exactly. Option B must give
  identical output; option A as well, unless the table in `chroma_sequence`
  rounds differently, in which case the scores may differ in the last
  digits and the ids and order must still match.
- `core_test` (the fingerprint checks, decoding every embedded
  fingerprint) and `ddb_tangotagger_hosttest`.
- For A: `fingerprint_lab`'s evaluation, if the table changes the scores at
  all.
- Heap in use before and after `embedded_fingerprints()`
  (`malloc_zone_statistics` on macOS): resident size alone does not show
  memory the allocator keeps for reuse.
