// Runs foo_tangotagger's fingerprint matching (core/fingerprint.cpp) over the
// lab's extracted features, so the C++ can be checked against compact.py.
//
//   fp_eval <list.tsv> <features dir> [threads]
//
// The list has one line per track: "ref" or "query", an id, the feature file
// name, and for a query the id of a reference to leave out (or -1). References
// are cut to the component's excerpt and sent through encode/decode, as the
// component stores them; queries are fingerprinted whole. Prints, per query,
// its id and the best five: "ref_id onset chroma speed semitones".

#include "fingerprint.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace tangotagger;

namespace
{
	bool load(const std::filesystem::path & path, audio_features & f)
	{
		std::ifstream in(path, std::ios::binary);
		std::string b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (b.size() < 56 || b.compare(0, 4, "TFP1") != 0) return false;
		double head[5];
		std::memcpy(head, b.data() + 4, sizeof head);
		float gate;
		std::memcpy(&gate, b.data() + 44, 4);
		std::int32_t c, n;
		std::memcpy(&c, b.data() + 48, 4);
		std::memcpy(&n, b.data() + 52, 4);
		std::size_t at = 56;
		if (b.size() < at + (static_cast<std::size_t>(c) * 13 + n) * 4) return false;
		f.tuning_cents = head[1];
		f.chroma_hop = head[3];
		f.novelty_rate = head[4];
		f.silence = gate;
		f.chroma.resize(static_cast<std::size_t>(c) * 12);
		std::memcpy(f.chroma.data(), b.data() + at, f.chroma.size() * 4);
		at += f.chroma.size() * 4;
		f.loudness.resize(c);
		std::memcpy(f.loudness.data(), b.data() + at, f.loudness.size() * 4);
		at += f.loudness.size() * 4;
		f.novelty.resize(n);
		std::memcpy(f.novelty.data(), b.data() + at, f.novelty.size() * 4);
		return true;
	}

	struct item
	{
		bool ref;
		int id;
		std::string file;
		int exclude;
	};

	// --- smaller formats, emulated -------------------------------------------
	//
	// FP_VARIANT="cbits=1 cpair=1 slevels=4 excerpt=60 orate=3" degrades the
	// fingerprints the way a smaller encoding would lose information, and the
	// matcher runs on them unchanged; the size is what that encoding would
	// take. Unset, everything is as the component has it.
	struct variant
	{
		int cbits = 2;          // bits per pitch class: 2, or 1 (above the bin's mean)
		bool cpair = false;     // chroma in 0.5s bins instead of 0.25s
		int slevels = 16;       // onset strength levels: 16, 4 or 1
		double excerpt = 90;    // seconds a reference keeps
		double orate = 4;       // onsets kept a second
	};

	variant read_variant()
	{
		variant v;
		const char * e = std::getenv("FP_VARIANT");
		if (e == nullptr) return v;
		std::istringstream s(e);
		std::string kv;
		while (s >> kv)
		{
			const std::size_t eq = kv.find('=');
			if (eq == std::string::npos) continue;
			const std::string k = kv.substr(0, eq);
			const double x = std::atof(kv.c_str() + eq + 1);
			if (k == "cbits") v.cbits = static_cast<int>(x);
			else if (k == "cpair") v.cpair = x != 0;
			else if (k == "slevels") v.slevels = static_cast<int>(x);
			else if (k == "excerpt") v.excerpt = x;
			else if (k == "orate") v.orate = x;
		}
		return v;
	}

	void requantise(std::uint8_t * q, const double * m, int bits)
	{
		double mean = 0, hi = 0;
		for (int c = 0; c < 12; c++) { mean += m[c]; hi = std::max(hi, m[c]); }
		mean /= 12;
		for (int c = 0; c < 12; c++)
			q[c] = hi <= 0 ? 0 : bits == 1 ? (m[c] > mean ? 3 : 0) : static_cast<std::uint8_t>(std::lround(3 * m[c] / hi));
	}

	void degrade(fingerprint & f, const variant & v, bool reference)
	{
		const double bin = 0.25;
		if (reference && v.excerpt < 90)
		{
			const std::size_t bins = std::min(f.bins(), static_cast<std::size_t>(v.excerpt / bin));
			f.chroma.resize(bins * 12);
			const std::uint32_t end = static_cast<std::uint32_t>(std::lround((f.music_start + v.excerpt) * 100));
			f.onsets.erase(std::remove_if(f.onsets.begin(), f.onsets.end(),
			                              [&](const fingerprint::onset & o) { return o.time >= end; }),
			               f.onsets.end());
		}
		if (v.cpair || v.cbits != 2)
		{
			const std::size_t n = f.bins();
			const std::size_t step = v.cpair ? 2 : 1;
			for (std::size_t b = 0; b < n; b += step)
			{
				double m[12] = { 0 };
				int count = 0;
				for (std::size_t j = b; j < std::min(n, b + step); j++)
				{
					int sum = 0;
					for (int c = 0; c < 12; c++) sum += f.chroma[j * 12 + c];
					if (sum == 0) continue;
					for (int c = 0; c < 12; c++) m[c] += f.chroma[j * 12 + c];
					count++;
				}
				std::uint8_t q[12] = { 0 };
				if (count > 0) requantise(q, m, v.cbits);
				for (std::size_t j = b; j < std::min(n, b + step); j++)
					for (int c = 0; c < 12; c++) f.chroma[j * 12 + c] = q[c];
			}
		}
		if (v.orate < 4)
		{
			const std::size_t keep = static_cast<std::size_t>(f.onsets.size() * v.orate / 4);
			std::vector<fingerprint::onset> o = f.onsets;
			std::stable_sort(o.begin(), o.end(), [](const fingerprint::onset & a, const fingerprint::onset & b)
			                 { return a.strength > b.strength; });
			o.resize(keep);
			std::sort(o.begin(), o.end(), [](const fingerprint::onset & a, const fingerprint::onset & b)
			          { return a.time < b.time; });
			f.onsets = o;
		}
		if (v.slevels < 16)
			for (fingerprint::onset & o : f.onsets)
			{
				const int level = std::max(1, static_cast<int>(std::ceil(o.strength * v.slevels / 15.0)));
				o.strength = static_cast<std::uint8_t>(std::lround(level * 15.0 / v.slevels));
			}
	}

	//! What the variant's encoding would take: chroma packed at its bits and
	//! bin width; an onset a byte when its gap fits beside the strength bits,
	//! two otherwise.
	std::size_t variant_size(const fingerprint & f, const variant & v)
	{
		const std::size_t bins = v.cpair ? (f.bins() + 1) / 2 : f.bins();
		const int sbits = v.slevels >= 16 ? 4 : v.slevels >= 8 ? 3 : v.slevels >= 4 ? 2 : 0;
		std::size_t bytes = 10 + (bins * 12 * v.cbits + 7) / 8;
		std::uint32_t previous = 0;
		for (const fingerprint::onset & o : f.onsets)
		{
			bytes += (o.time - previous) < (1u << (7 - sbits)) ? 1 : 2;   // a continuation bit
			previous = o.time;
		}
		return bytes;
	}

	//! A byte layout for LZMA rather than for size: chroma bins as 1 bit a
	//! pitch class in two bytes, each XORed with the bin before (held chords
	//! become zeros); onset gaps and strengths in separate streams, a byte
	//! each. Only the 1 bit, 0.25s chroma is laid out this way.
	std::string lzma_layout(const fingerprint & f)
	{
		std::string s(10, '\0');
		std::uint16_t previous = 0;
		for (std::size_t b = 0; b < f.bins(); b++)
		{
			std::uint16_t bits = 0;
			for (int c = 0; c < 12; c++)
				if (f.chroma[b * 12 + c] >= 2) bits |= static_cast<std::uint16_t>(1u << c);
			const std::uint16_t x = bits ^ previous;
			previous = bits;
			s += static_cast<char>(x & 0xFF);
			s += static_cast<char>(x >> 8);
		}
		std::uint32_t t = 0;
		for (const fingerprint::onset & o : f.onsets)
		{
			const std::uint32_t gap = o.time - t;
			t = o.time;
			if (gap < 255) s += static_cast<char>(gap);
			else
			{
				s += static_cast<char>(255);
				s += static_cast<char>(gap & 0xFF);
				s += static_cast<char>(gap >> 8);
			}
		}
		for (const fingerprint::onset & o : f.onsets) s += static_cast<char>(o.strength);
		return s;
	}
}

int main(int argc, char ** argv)
{
	if (argc < 3)
	{
		std::fprintf(stderr, "usage: fp_eval <list.tsv> <features dir> [threads]\n");
		return 2;
	}
	const std::filesystem::path dir = std::filesystem::u8path(argv[2]);
	const int threads = argc > 3 ? std::atoi(argv[3]) : static_cast<int>(std::thread::hardware_concurrency());

	std::vector<item> items;
	std::ifstream list(std::filesystem::u8path(argv[1]));
	std::string line;
	while (std::getline(list, line))
	{
		std::istringstream s(line);
		std::string kind, file;
		int id = 0, exclude = -1;
		if (!(s >> kind >> id >> file)) continue;
		s >> exclude;
		items.push_back({ kind == "ref", id, file, exclude });
	}

	const variant v = read_variant();
	fingerprint_index index;
	std::size_t bytes = 0, refs = 0;
	for (const item & it : items)
	{
		if (!it.ref) continue;
		audio_features f;
		if (!load(dir / it.file, f)) continue;
		const std::string enc = encode_fingerprint(make_fingerprint(f, fingerprint_excerpt_seconds));
		fingerprint back;
		if (!decode_fingerprint(enc, back)) { std::fprintf(stderr, "decode failed: %s\n", it.file.c_str()); return 1; }
		degrade(back, v, true);
		bytes += variant_size(back, v);
		if (const char * dump = std::getenv("FP_DUMP"))
		{
			// Each reference as the component would store it, length first, so
			// the blob's real, compressed size can be measured.
			static std::ofstream out(std::filesystem::u8path(dump), std::ios::binary | std::ios::trunc);
			const std::string data = std::getenv("FP_LAYOUT") != nullptr ? lzma_layout(back) : encode_fingerprint(back);
			const std::uint32_t n = static_cast<std::uint32_t>(data.size());
			out.write(reinterpret_cast<const char *>(&n), 4);
			out.write(data.data(), n);
			out.flush();
		}
		refs++;
		index.add(it.id, std::move(back));
	}
	std::fprintf(stderr, "%zu references, %zu bytes on average\n", refs, refs ? bytes / refs : 0);

	std::vector<const item *> queries;
	for (const item & it : items)
		if (!it.ref) queries.push_back(&it);
	std::vector<std::string> out(queries.size());
	std::atomic<std::size_t> next(0), done(0);
	auto worker = [&]()
	{
		for (;;)
		{
			const std::size_t i = next.fetch_add(1);
			if (i >= queries.size()) return;
			audio_features f;
			std::string row = std::to_string(queries[i]->id);
			if (load(dir / queries[i]->file, f))
			{
				fingerprint q = make_fingerprint(f, 0);
				degrade(q, v, false);
				std::vector<fingerprint_match> m = index.identify(q, {}, 6);
				int written = 0;
				for (const fingerprint_match & x : m)
				{
					if (x.id == queries[i]->exclude || written == 5) continue;
					char buf[128];
					std::snprintf(buf, sizeof buf, "\t%d %.4f %.4f %.4f %d %.1f", x.id, x.onset, x.chroma, x.speed, x.semitones,
					              x.wander_ms);
					row += buf;
					written++;
				}
			}
			out[i] = row;
			const std::size_t d = done.fetch_add(1) + 1;
			if (d % 100 == 0) std::fprintf(stderr, "%zu/%zu\n", d, queries.size());
		}
	};
	std::vector<std::thread> pool;
	for (int t = 0; t < threads; t++) pool.emplace_back(worker);
	for (auto & t : pool) t.join();
	for (const std::string & r : out) std::printf("%s\n", r.c_str());
	return 0;
}
