// Fingerprint experiment: extracts the features a fingerprint would be made of.
//
//   fp_extract <list.tsv> <out_dir> [jobs]
//
// Each line of the list is "<out name>\t<audio path>", UTF-8. For every line
// whose <out_dir>/<out name> does not exist yet, the audio is decoded through
// ffmpeg (TANGO_FFMPEG, or ffmpeg on the PATH) to mono at 22050Hz and three
// things are written:
//
//   - the tuning offset bpmcore measures, which is what lets two transfers at
//     different turntable speeds be brought to the same one;
//   - the chroma of every pitch frame, with that offset already taken out;
//   - the onset novelty curve the tempo analysis runs on, which carries the
//     timing of the performance itself.
//
// Nothing is resampled for speed here. The chroma is already in tune, so all
// that a speed difference leaves is a stretched time axis, and stretching a
// feature sequence is the evaluation's job - where it can be tried several
// ways without decoding the collection again.
//
// File layout, little endian:
//
//     4 bytes   "TFP1"
//     f64       duration, seconds
//     f64       tuning_cents, in (-50, 50]
//     f64       tuning_r, 0..1
//     f64       chroma hop, seconds
//     f64       novelty frame rate, Hz
//     f32       silence gate on the frame rms
//     i32       chroma frames (C)
//     i32       novelty frames (N)
//     f32[C*12] chroma
//     f32[C]    frame rms
//     f32[N]    novelty

#include <bpmcore/bpmcore.h>
#include <bpmcore/internal.h>

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
	const unsigned rate = bpmcore::odf_model_rate;   // 22050

	std::string ffmpeg_path()
	{
		const char * e = std::getenv("TANGO_FFMPEG");
		return e != nullptr && *e != '\0' ? std::string(e) : std::string("ffmpeg");
	}

	//! Mono float at 22050Hz, through ffmpeg - the same invocation bpmcore_test
	//! uses, so features here match what the analysis sees there.
	bool decode(const std::string & path, std::vector<float> & out, std::string & error)
	{
		out.clear();
		if (path.find('"') != std::string::npos) { error = "path contains a quote"; return false; }

		const std::size_t max_samples = static_cast<std::size_t>(bpmcore::max_seconds() * rate);
		const std::string tail = " -ac 1 -ar " + std::to_string(rate) + " -f f32le -";
#if defined(_WIN32)
		const std::wstring wexe  = std::filesystem::u8path(ffmpeg_path()).wstring();
		const std::wstring wpath = std::filesystem::u8path(path).wstring();
		const std::wstring wtail(tail.begin(), tail.end());
		const std::wstring cmd = L"\"\"" + wexe + L"\" -v error -i \"" + wpath + L"\"" + wtail + L"\"";
		std::FILE * pipe = _wpopen(cmd.c_str(), L"rb");
#else
		const std::string cmd = "\"" + ffmpeg_path() + "\" -v error -i \"" + path + "\"" + tail;
		std::FILE * pipe = popen(cmd.c_str(), "r");
#endif
		if (pipe == nullptr) { error = "cannot start ffmpeg"; return false; }

		std::vector<char> bytes;
		char buf[1 << 16];
		std::size_t n;
		while ((n = std::fread(buf, 1, sizeof buf, pipe)) > 0)
			if (bytes.size() < max_samples * sizeof(float))
				bytes.insert(bytes.end(), buf, buf + n);
#if defined(_WIN32)
		const int rc = _pclose(pipe);
#else
		const int rc = pclose(pipe);
#endif
		if (rc != 0) { error = "ffmpeg exited " + std::to_string(rc); return false; }

		const std::size_t count = std::min(bytes.size() / sizeof(float), max_samples);
		if (count < 10 * rate) { error = "short or empty decode"; return false; }
		out.resize(count);
		std::memcpy(out.data(), bytes.data(), count * sizeof(float));
		for (float & v : out) if (!std::isfinite(v)) v = 0.0f;
		return true;
	}

	template <class T> void put(std::ofstream & f, T v)
	{
		f.write(reinterpret_cast<const char *>(&v), sizeof v);
	}

	void put(std::ofstream & f, const std::vector<float> & v)
	{
		f.write(reinterpret_cast<const char *>(v.data()),
		        static_cast<std::streamsize>(v.size() * sizeof(float)));
	}

	bool extract(const std::string & path, const std::filesystem::path & out, std::string & error)
	{
		std::vector<float> mono;
		if (!decode(path, mono, error)) return false;

		bpmcore::key_analysis key;
		bpmcore::key_frames frames;
		if (!bpmcore::compute_key(mono.data(), mono.size(), rate, key, nullptr, 1, &frames)
		    || frames.frames <= 0)
		{
			error = "no pitch analysis";
			return false;
		}

		bpmcore::odf o;
		if (!bpmcore::compute_odf(mono.data(), mono.size(), rate, o, nullptr, 1))
		{
			error = "no onset analysis";
			return false;
		}
		std::vector<float> novelty;
		bpmcore::mix_bands(o, novelty);
		bpmcore::make_novelty(novelty, o.frame_rate);

		// Written under a temporary name and renamed, so an interrupted run
		// never leaves a truncated file that a rerun would take as done.
		std::filesystem::path tmp = out;
		tmp += ".part";
		{
			std::ofstream f(tmp, std::ios::binary);
			if (!f) { error = "cannot write output"; return false; }
			f.write("TFP1", 4);
			put(f, static_cast<double>(mono.size()) / rate);
			put(f, key.tuning_cents);
			put(f, key.tuning_r);
			put(f, frames.hop_seconds);
			put(f, o.frame_rate);
			put(f, frames.gate);
			put(f, static_cast<std::int32_t>(frames.frames));
			put(f, static_cast<std::int32_t>(novelty.size()));
			put(f, frames.chroma);
			put(f, frames.rms);
			put(f, novelty);
			if (!f) { error = "write failed"; return false; }
		}
		// A virus scanner may still hold the file it has just seen written.
		std::error_code ec;
		for (int attempt = 0; attempt < 20; attempt++)
		{
			std::filesystem::rename(tmp, out, ec);
			if (!ec) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		if (ec) { error = "rename failed: " + ec.message(); return false; }
		return true;
	}

	struct job
	{
		std::string name, path;
	};
}

int main(int argc, char ** argv)
{
	if (argc < 3)
	{
		std::fprintf(stderr, "usage: fp_extract <list.tsv> <out_dir> [jobs]\n");
		return 2;
	}
	const std::filesystem::path out_dir = std::filesystem::u8path(argv[2]);
	std::filesystem::create_directories(out_dir);
	int jobs = argc > 3 ? std::atoi(argv[3]) : 0;
	if (jobs <= 0) jobs = std::max(1u, std::thread::hardware_concurrency());

	std::ifstream list(std::filesystem::u8path(argv[1]));
	if (!list) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
	std::vector<job> todo;
	std::string line;
	while (std::getline(list, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const std::size_t tab = line.find('\t');
		if (tab == std::string::npos) continue;
		job j{ line.substr(0, tab), line.substr(tab + 1) };
		if (!std::filesystem::exists(out_dir / std::filesystem::u8path(j.name)))
			todo.push_back(j);
	}
	std::fprintf(stderr, "%d files to extract, %d jobs\n", static_cast<int>(todo.size()), jobs);

	std::atomic<std::size_t> next(0), done(0), failed(0);
	std::mutex io;
	auto worker = [&]()
	{
		for (;;)
		{
			const std::size_t i = next.fetch_add(1);
			if (i >= todo.size()) return;
			std::string error;
			const bool ok = extract(todo[i].path, out_dir / std::filesystem::u8path(todo[i].name), error);
			const std::size_t d = done.fetch_add(1) + 1;
			std::lock_guard<std::mutex> lock(io);
			if (!ok)
			{
				failed.fetch_add(1);
				std::fprintf(stderr, "FAIL %s: %s\n", todo[i].path.c_str(), error.c_str());
			}
			if (d % 50 == 0 || d == todo.size())
				std::fprintf(stderr, "%d/%d\n", static_cast<int>(d), static_cast<int>(todo.size()));
		}
	};
	std::vector<std::thread> pool;
	for (int t = 0; t < jobs; t++) pool.emplace_back(worker);
	for (auto & t : pool) t.join();
	std::fprintf(stderr, "done, %d failed\n", static_cast<int>(failed.load()));
	return failed.load() == 0 ? 0 : 1;
}
