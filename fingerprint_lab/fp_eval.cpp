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

#include <atomic>
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
		bytes += enc.size();
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
				const fingerprint q = make_fingerprint(f, 0);
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
