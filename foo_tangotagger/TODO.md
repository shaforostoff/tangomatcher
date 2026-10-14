# TODO

## Speed

Measured on an i7-4750HQ (Haswell, 4 cores / 8 threads, 256KB L2 a core,
6MB L3, 128MB eDRAM, DDR3-1600), Release build of MSVC 2017, x64 SSE2.

### Fingerprint matching: a faster FFT

`fingerprint_index::identify` takes about 245ms on one thread, 380ms a
thread with the component's six workers. Building each candidate's
spectrum is 41% of it, the coarse pass 44%, the fine pass 14%, the
prefilter under 1%. Most of the first two is `fft::run`, a plain radix-2
transform on `std::complex<float>` that does not vectorise. It would help
every machine, not only slow ones.

pffft is already in the component - bpmcore's transform, from foo_bpm - but
core/ does not use it: tangotagger_core is standard C++ only and does not
link bpmcore. Linking pffft into tangotagger_core (core_test and the tools
would need it too) gives SIMD real transforms of 1024 points, which would
replace both the transform and the two-real-rows-in-one-complex packing in
`two_real_spectra` / `two_real_signals`. Mind pffft's packed real layout -
DC and Nyquist share the first complex - and its 16-byte alignment, which
`pffft_aligned_malloc` gives. A radix-4 transform of our own is the
alternative that keeps core/ free of dependencies.

Either rounds differently from the current transform, so the results will
not stay byte-identical: check identifications against fingerprint_lab
(cpp_eval.py) and core_test's synthetic transfers (faster, slower, trimmed,
across the semitone wrap, re-performed), not just a diff of the output.

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
