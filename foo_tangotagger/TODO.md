# TODO

## Speed

Measured on an i7-4750HQ (Haswell, 4 cores / 8 threads, 256KB L2 a core,
6MB L3, 128MB eDRAM, DDR3-1600), Release build of MSVC 2017, x64 SSE2.

### Fingerprint matching: where the time goes now

`fingerprint_index::identify` takes about 135ms on one thread through pffft
(FOO_TANGOTAGGER_FINGERPRINT_PFFFT, the default), 207ms a thread with the
component's six workers; 245ms and 380ms through core's own transform, 564ms
and 861ms before the half spectra. Not profiled again since pffft: measure
before choosing the next thing.

Check any change against the current results: they must stay the same on the
embedded references and on core_test's synthetic transfers (faster, slower,
trimmed, across the semitone wrap, re-performed); and against fingerprint_lab
(cpp_eval.py) for anything that changes the rounding.

### Fingerprint matching: the fine pass

`fine_pass` calls `chroma_sequence` for the query once per speed (13 per
candidate, 5 candidates, up to two modes), and each call normalises every
bin of the query again and allocates its vectors. The normalised bins could
be made once per query. The sequences are doubles; floats would halve what
the lag loop reads, but the scores could change, so measure first.

### Title matching: allocations in edit_distance

`edit_distance` (core/title_match.cpp) allocates three vectors a call. It is
called for every title key of a track against every key of every recording
of each orchestra the track names. A fixed buffer on the stack for titles
of ordinary length would avoid the heap. Not measured: that needs the
embedded discography and a few thousand tracks.

### Not worth doing

Aligning the spectrum rows to cache lines made no measurable difference
(243-248ms either way): the x64 build uses 16-byte SSE2 loads, which
Haswell does at full speed unaligned. Worth a look again only with an AVX
build.

Six workers against one thread cost the same proportion before and after
the half-spectra change, which halved the memory read; the slowdown is the
hyper-threads and the lower turbo with more cores busy, not memory. That
may differ on a DDR3 machine without this CPU's eDRAM.

## Other

### Credit pffft

The component links pffft - through bpmcore since before the fingerprint
matching used it - and pffft's FFTPACK licence asks binary distributions to
reproduce its notice (foo_bpm's pffft/COPYING). The about box credits the
discographies and the lyrics, not pffft.

### Build: tools/fingerprints with Visual Studio 2017

tools/fingerprints/main.cpp calls `std::tolower` without `<cctype>`, which
Visual Studio 2017's headers do not bring in by themselves. And the 2025
foobar2000 SDK uses `ERROR_NO_SUCH_DEVICE`, which Visual Studio 2017's
Windows SDK (10.0.17763) does not have: the component wants a newer one.
