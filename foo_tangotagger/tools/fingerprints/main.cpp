// tango_fingerprints: makes the audio fingerprints the component embeds, and
// tries the component's matching on a folder.
//
//   tango_fingerprints build <out dir> <cache file> <collection dir>... [--jobs N]
//
//     Every audio file in the collections, best collection first, is matched
//     by its tags and file name against the embedded discographies, as Match
//     discographies does. Each recording matched confidently gets the
//     fingerprint of one of its transfers: the first, in collection order,
//     whose fingerprint identifies another transfer of the recording - two
//     files agreeing is what makes a mislabelled one unlikely - or the first
//     when there is only the one. A recording whose transfers do not agree is
//     reported and left out. Writes <out dir>/<orchestra>.xml, which
//     pack_discography embeds (core/fingerprint.h, core/discography.h).
//
//   tango_fingerprints identify <dir> [--jobs N]
//
//     What Match discographies would make of every audio file in <dir>: by
//     tags and file name, and by sound when that is not confident. One line a
//     file: the path, how it was matched, then the candidates.
//
// Audio is decoded and tags read by ffmpeg and ffprobe (TANGO_FFMPEG,
// TANGO_FFPROBE, or the ones on the PATH). Fingerprints are kept in the cache
// file between runs, by path, size and time.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "disco_fingerprint.h"
#include "disco_match.h"
#include "fingerprint.h"
#include "fingerprint_bpmcore.h"
#include "title_match.h"

namespace fs = std::filesystem;
using namespace tangotagger;

namespace
{
	const unsigned rate = 22050;
	const char * const audio_extensions[] = { ".flac", ".m4a", ".mp3", ".wav", ".aif", ".aiff", ".ogg", ".opus",
	                                          ".wv", ".ape", ".wma" };

	std::string tool_path(const char * env, const char * fallback)
	{
		const char * e = std::getenv(env);
		return e != nullptr && *e != '\0' ? std::string(e) : std::string(fallback);
	}

	//! Runs a tool on one file and returns what it writes to stdout.
	bool run(const std::string & exe, const std::string & args_before, const std::string & path,
	         const std::string & args_after, std::string & out)
	{
		out.clear();
		if (path.find('"') != std::string::npos) return false;
#if defined(_WIN32)
		const std::wstring wexe = fs::u8path(exe).wstring(), wpath = fs::u8path(path).wstring();
		const std::wstring before(args_before.begin(), args_before.end()), after(args_after.begin(), args_after.end());
		const std::wstring cmd = L"\"\"" + wexe + L"\" " + before + L" \"" + wpath + L"\" " + after + L"\"";
		std::FILE * pipe = _wpopen(cmd.c_str(), L"rb");
#else
		const std::string cmd = "\"" + exe + "\" " + args_before + " \"" + path + "\" " + args_after;
		std::FILE * pipe = popen(cmd.c_str(), "r");
#endif
		if (pipe == nullptr) return false;
		char buf[1 << 16];
		std::size_t n;
		while ((n = std::fread(buf, 1, sizeof buf, pipe)) > 0) out.append(buf, n);
#if defined(_WIN32)
		return _pclose(pipe) == 0;
#else
		return pclose(pipe) == 0;
#endif
	}

	bool is_audio(const fs::path & p)
	{
		std::string ext = p.extension().u8string();
		for (char & c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (p.filename().u8string().rfind("._", 0) == 0) return false;   // macOS resource forks
		for (const char * e : audio_extensions)
			if (ext == e) return true;
		return false;
	}

	std::vector<std::string> audio_files(const std::string & root)
	{
		std::vector<std::string> out;
		std::error_code ec;
		for (fs::recursive_directory_iterator it(fs::u8path(root), ec), end; it != end; it.increment(ec))
			if (!ec && it->is_regular_file(ec) && is_audio(it->path())) out.push_back(it->path().u8string());
		std::sort(out.begin(), out.end());
		return out;
	}

	// ------------------------------------------------------------------ tags

	//! The track's tags as Match discographies reads them, from ffprobe.
	track_tags read_tags(const std::string & path)
	{
		track_tags t;
		t.path = path;
		std::string out;
		if (!run(tool_path("TANGO_FFPROBE", "ffprobe"), "-v quiet -show_entries format_tags -of default=nw=1", path,
		         "", out))
			return t;
		std::istringstream lines(out);
		std::string line;
		while (std::getline(lines, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.rfind("TAG:", 0) != 0) continue;
			const std::size_t eq = line.find('=');
			if (eq == std::string::npos) continue;
			std::string key = line.substr(4, eq - 4);
			for (char & c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			const std::string value = line.substr(eq + 1);
			auto add = [&](std::string & field) { field += (field.empty() ? "" : " ; ") + value; };
			if (key == "title") t.title = value;
			else if (key == "artist") add(t.artist);
			else if (key == "album_artist" || key == "albumartist" || key == "album artist") add(t.album_artist);
			else if (key == "album") t.album = value;
			else if (key == "genre") add(t.genre);
			else if (key == "comment" || key == "description") add(t.comment);
			else if (key == "date" || key == "year" || key == "originaldate" || key == "original date" ||
			         key == "originalyear" || key == "recording date" || key == "recordingdate")
				add(t.dates);
			else if (key == "conductor" || key == "performer" || key == "orchestra" || key == "ensemble" ||
			         key == "band" || key == "vocals" || key == "vocalist" || key == "singer" || key == "cantor")
				add(t.performers);
		}
		return t;
	}

	// ------------------------------------------------------------------ audio

	struct prints
	{
		std::string excerpt;   //!< encoded, as a reference
		std::string whole;     //!< encoded, as a query
		bool ok() const { return !whole.empty(); }
	};

	prints fingerprint_file(const std::string & path)
	{
		prints p;
		std::string bytes;
		const std::string tail = "-ac 1 -ar " + std::to_string(rate) + " -f f32le -";
		if (!run(tool_path("TANGO_FFMPEG", "ffmpeg"), "-v error -i", path, tail, bytes)) return p;
		const std::size_t max = static_cast<std::size_t>(bpmcore::max_seconds() * rate);
		std::vector<float> mono(std::min(bytes.size() / sizeof(float), max));
		if (mono.size() < 10 * rate) return p;
		std::memcpy(mono.data(), bytes.data(), mono.size() * sizeof(float));
		for (float & v : mono)
			if (!std::isfinite(v)) v = 0.0f;
		bpmcore::features f = bpmcore::extract_features(mono.data(), mono.size(), rate, nullptr, 1);
		if (!f.ok) return p;
		const audio_features a = audio_features_of(std::move(f));
		const fingerprint excerpt = make_fingerprint(a, fingerprint_excerpt_seconds);
		const fingerprint whole = make_fingerprint(a, 0);
		if (whole.empty()) return p;
		p.excerpt = encode_fingerprint(excerpt);
		p.whole = encode_fingerprint(whole);
		return p;
	}

	//! Fingerprints by path, size and modification time, kept between runs.
	class cache
	{
	public:
		explicit cache(const std::string & file) : m_file(fs::u8path(file))
		{
			std::ifstream in(m_file, std::ios::binary);
			std::string line;
			while (std::getline(in, line))
			{
				std::vector<std::string> f;
				std::size_t start = 0;
				for (;;)
				{
					const std::size_t tab = line.find('\t', start);
					f.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
					if (tab == std::string::npos) break;
					start = tab + 1;
				}
				if (f.size() != 4) continue;
				prints p;
				from_base64(f[2], p.excerpt);
				from_base64(f[3], p.whole);
				m_entries[f[0] + "\t" + f[1]] = p;
			}
			m_out.open(m_file, std::ios::binary | std::ios::app);
		}

		bool find(const std::string & path, prints & p)
		{
			std::lock_guard<std::mutex> lock(m_lock);
			const auto it = m_entries.find(path + "\t" + stamp(path));
			if (it == m_entries.end()) return false;
			p = it->second;
			return true;
		}

		void put(const std::string & path, const prints & p)
		{
			std::lock_guard<std::mutex> lock(m_lock);
			const std::string key = path + "\t" + stamp(path);
			m_entries[key] = p;
			m_out << key << "\t" << to_base64(p.excerpt) << "\t" << to_base64(p.whole) << "\n";
			m_out.flush();
		}

	private:
		static std::string stamp(const std::string & path)
		{
			std::error_code ec;
			const auto size = fs::file_size(fs::u8path(path), ec);
			const auto time = fs::last_write_time(fs::u8path(path), ec).time_since_epoch().count();
			return std::to_string(size) + ":" + std::to_string(static_cast<long long>(time));
		}

		fs::path m_file;
		std::map<std::string, prints> m_entries;
		std::ofstream m_out;
		std::mutex m_lock;
	};

	//! Runs `job(i)` for i in [0, n) on `jobs` threads, reporting progress.
	template <class F> void parallel(std::size_t n, int jobs, const char * what, F job)
	{
		std::atomic<std::size_t> next(0), done(0);
		std::mutex io;
		auto worker = [&]()
		{
			for (;;)
			{
				const std::size_t i = next.fetch_add(1);
				if (i >= n) return;
				job(i);
				const std::size_t d = done.fetch_add(1) + 1;
				if (d % 200 == 0 || d == n)
				{
					std::lock_guard<std::mutex> lock(io);
					std::fprintf(stderr, "%s: %zu/%zu\n", what, d, n);
				}
			}
		};
		std::vector<std::thread> pool;
		for (int t = 0; t < std::max(1, jobs); t++) pool.emplace_back(worker);
		for (auto & t : pool) t.join();
	}

	std::string xml_escape(const std::string & s)
	{
		std::string out;
		for (char c : s)
		{
			switch (c)
			{
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			default: out += c;
			}
		}
		return out;
	}

	std::string file_name_for(const std::string & orchestra)
	{
		std::string out;
		for (char c : orchestra) out += std::strchr("<>:\"/\\|?*", c) != nullptr ? '_' : c;
		return out + ".xml";
	}

	// ------------------------------------------------------------------ build

	int build(const std::string & out_dir, const std::string & cache_file, const std::vector<std::string> & roots,
	          int jobs)
	{
		const discography & d = embedded_discography();
		const recording_matcher matcher(d);

		std::vector<std::string> files;
		std::vector<int> rank;
		for (std::size_t r = 0; r < roots.size(); r++)
			for (const std::string & f : audio_files(roots[r]))
			{
				files.push_back(f);
				rank.push_back(static_cast<int>(r));
			}
		std::fprintf(stderr, "%zu audio files\n", files.size());

		// Which recording each file is, by its tags.
		std::vector<int> recording_of(files.size(), -1);
		parallel(files.size(), jobs, "tags", [&](std::size_t i)
		{
			const track_match m = matcher.find(read_tags(files[i]));
			if (m.confident && !m.candidates.empty()) recording_of[i] = m.candidates.front().recording;
		});
		std::map<int, std::vector<std::size_t>> transfers;   // recording -> files, best collection first
		for (std::size_t i = 0; i < files.size(); i++)
			if (recording_of[i] >= 0) transfers[recording_of[i]].push_back(i);
		std::fprintf(stderr, "%zu files place %zu recordings\n",
		             static_cast<std::size_t>(std::count_if(recording_of.begin(), recording_of.end(),
		                                                    [](int r) { return r >= 0; })),
		             transfers.size());

		// Their fingerprints.
		std::vector<std::size_t> wanted;
		for (const auto & t : transfers) wanted.insert(wanted.end(), t.second.begin(), t.second.end());
		cache c(cache_file);
		std::vector<prints> fp(files.size());
		parallel(wanted.size(), jobs, "fingerprints", [&](std::size_t w)
		{
			const std::size_t i = wanted[w];
			if (c.find(files[i], fp[i])) return;
			fp[i] = fingerprint_file(files[i]);
			c.put(files[i], fp[i]);
		});

		// One transfer per recording, cross-checked against the others.
		struct chosen
		{
			int recording;
			std::string data;
			int agreeing;   // other transfers it identifies
		};
		std::vector<chosen> kept(transfers.size());
		std::vector<std::pair<int, std::vector<std::size_t>>> list(transfers.begin(), transfers.end());
		std::atomic<std::size_t> disagreeing(0), single(0);
		std::mutex io;
		parallel(list.size(), jobs, "cross-check", [&](std::size_t n)
		{
			const int rec = list[n].first;
			std::vector<std::size_t> ok;
			for (std::size_t i : list[n].second)
				if (fp[i].ok()) ok.push_back(i);
			kept[n].recording = -1;
			if (ok.empty()) return;
			if (ok.size() == 1)
			{
				kept[n] = { rec, fp[ok[0]].excerpt, 0 };
				single++;
				return;
			}
			// Each candidate reference against the other transfers.
			for (std::size_t a : ok)
			{
				fingerprint ref;
				if (!decode_fingerprint(fp[a].excerpt, ref)) continue;
				fingerprint_index one;
				one.add(rec, std::move(ref));
				int agree = 0;
				for (std::size_t b : ok)
				{
					if (b == a) continue;
					fingerprint q;
					if (!decode_fingerprint(fp[b].whole, q)) continue;
					const std::vector<fingerprint_match> m = one.identify(q, { rec }, 1);
					if (!m.empty() && m.front().identified()) agree++;
				}
				if (agree > 0)
				{
					kept[n] = { rec, fp[a].excerpt, agree };
					return;
				}
			}
			disagreeing++;
			const recording & r = d.recordings[rec];
			std::lock_guard<std::mutex> lock(io);
			std::fprintf(stderr, "transfers disagree, left out: %s - %s - %s - %s:\n", d.orchestras[r.orchestra].c_str(),
			             r.vocal.c_str(), r.name.c_str(), r.date.c_str());
			for (std::size_t i : ok) std::fprintf(stderr, "    %s\n", files[i].c_str());
		});

		// By orchestra, as the discographies are.
		std::map<int, std::vector<const chosen *>> by_orchestra;
		for (const chosen & k : kept)
			if (k.recording >= 0) by_orchestra[d.recordings[k.recording].orchestra].push_back(&k);
		fs::create_directories(fs::u8path(out_dir));
		std::set<std::string> written;
		std::size_t total = 0;
		for (auto & o : by_orchestra)
		{
			std::sort(o.second.begin(), o.second.end(), [&](const chosen * a, const chosen * b)
			{
				const recording & x = d.recordings[a->recording];
				const recording & y = d.recordings[b->recording];
				const std::string kx = fold_key(x.name), ky = fold_key(y.name);
				if (kx != ky) return kx < ky;
				return x.date < y.date;
			});
			const std::string & orchestra = d.orchestras[o.first];
			const std::string name = file_name_for(orchestra);
			std::ostringstream xml;
			xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			    << "<!--\n"
			    << "  Audio fingerprints of recordings of " << orchestra << ", for foo_tangotagger.\n"
			    << "  Made by foo_tangotagger/tools/fingerprints from transfers the tags and file\n"
			    << "  names place confidently; \"agreeing\" counts the other transfers of the same\n"
			    << "  recording the fingerprint identifies. A fingerprint is a description of the\n"
			    << "  sound - chroma and onsets, see foo_tangotagger/core/fingerprint.h - not\n"
			    << "  audio. Regenerated, not edited by hand.\n"
			    << "-->\n"
			    << "<fingerprints orchestra=\"" << xml_escape(orchestra) << "\" format=\"1\">\n";
			for (const chosen * k : o.second)
			{
				const recording & r = d.recordings[k->recording];
				xml << "  <recording name=\"" << xml_escape(r.name) << "\" vocal=\"" << xml_escape(r.vocal)
				    << "\" date=\"" << xml_escape(r.date) << "\" agreeing=\"" << k->agreeing << "\" fp=\""
				    << to_base64(k->data) << "\"/>\n";
				total++;
			}
			xml << "</fingerprints>\n";
			std::ofstream out(fs::u8path(out_dir) / fs::u8path(name), std::ios::binary | std::ios::trunc);
			out << xml.str();
			written.insert(name);
		}
		// Orchestras left without fingerprints lose their stale files.
		std::error_code ec;
		for (const fs::directory_entry & e : fs::directory_iterator(fs::u8path(out_dir), ec))
			if (e.path().extension() == ".xml" && written.count(e.path().filename().u8string()) == 0)
				fs::remove(e.path(), ec);

		std::fprintf(stderr, "%zu fingerprints of %zu orchestras; %zu from a single transfer, %zu recordings "
		             "left out because their transfers disagree\n",
		             total, by_orchestra.size(), single.load(), disagreeing.load());
		return 0;
	}

	// ------------------------------------------------------------------ identify

	int identify(const std::string & root, int jobs)
	{
		const discography & d = embedded_discography();
		const recording_matcher matcher(d);
		const fingerprint_index & index = embedded_fingerprints();
		std::fprintf(stderr, "%zu recordings, %zu of them fingerprinted\n", d.recordings.size(), index.size());

		const std::vector<std::string> files = audio_files(root);
		std::vector<std::string> lines(files.size());
		parallel(files.size(), jobs, "identify", [&](std::size_t i)
		{
			track_match m = matcher.find(read_tags(files[i]));
			std::string how = m.confident ? "tags" : "tags?";
			if (needs_sound(m) && index.size() > 0)
			{
				const prints p = fingerprint_file(files[i]);
				fingerprint q;
				if (p.ok() && decode_fingerprint(p.whole, q))
				{
					const bool before = m.confident;
					add_sound(m, index.identify(q, sound_hints(m)));
					how = m.confident && !before ? "sound" : !m.candidates.empty() && m.candidates.front().sound > 0
					                                           ? "sound?" : how;
				}
			}
			if (m.candidates.empty()) how = "none";
			std::string line = files[i] + "\t" + how;
			for (std::size_t c = 0; c < m.candidates.size() && c < 3; c++)
			{
				const recording_match & x = m.candidates[c];
				const recording & r = d.recordings[x.recording];
				line += "\t" + d.orchestras[r.orchestra] + " | " + r.vocal + " | " + r.name + " | " + r.date + " | " +
				        recording_matcher::evidence_text(x);
			}
			lines[i] = line;
		});
		for (const std::string & l : lines) std::printf("%s\n", l.c_str());
		return 0;
	}
}

int main(int argc, char ** argv)
{
	std::vector<std::string> args;
	int jobs = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 2);
	for (int i = 1; i < argc; i++)
	{
		if (std::strcmp(argv[i], "--jobs") == 0 && i + 1 < argc) jobs = std::atoi(argv[++i]);
		else args.push_back(argv[i]);
	}
	if (args.size() >= 4 && args[0] == "build")
		return build(args[1], args[2], std::vector<std::string>(args.begin() + 3, args.end()), jobs);
	if (args.size() == 2 && args[0] == "identify") return identify(args[1], jobs);
	std::fprintf(stderr,
	             "usage: tango_fingerprints build <out dir> <cache file> <collection dir>... [--jobs N]\n"
	             "       tango_fingerprints identify <dir> [--jobs N]\n");
	return 2;
}
