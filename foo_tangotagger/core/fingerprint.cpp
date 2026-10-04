#include "fingerprint.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <limits>
#include <numeric>

// The layout of an encoded fingerprint, all little endian:
//
//     1 byte     version, 2
//     2 bytes    duration, in 0.1s
//     2 bytes    music start, in 10ms
//     1 byte     tuning, cents, signed
//     2 bytes    bins
//     2 bytes    onsets
//     3 bytes    two bins: 12 pitch classes of 1 bit each, class 0 in the low
//                bit; the last half filled with zeros for an odd count
//     per onset  a varint of (time since the previous onset in 10ms) * 16 + strength
//
// The constants below are the ones fingerprint_lab measured (compact.py,
// then sweep.py for the sizes); a change to any of them changes what a
// stored fingerprint means, or how well the matching works, and should be
// measured there first. Measured, against version 1's 2 bits a pitch class
// and 4 onsets a second: the same 96% identified, 794 bytes a fingerprint
// after LZMA instead of 1,250. Coarser chroma (0.5s), fewer onsets (2.5 a
// second), coarser strengths (8 levels) or a shorter excerpt (60s) each cost
// identifications or let D'Arienzo's re-recordings through.

namespace tangotagger
{
	namespace
	{
		const double bin_seconds = 0.25;
		const double onsets_per_second = 3.0;
		const char format_version = 2;
		const double onset_unit = 0.01;          // seconds; the onset grid
		const double onset_sigma = 0.03;         // seconds an onset is rendered across

		const double coarse_hop = 0.2;
		const int coarse_max_lag = 150;          // frames: 30 seconds either way
		const int fft_size = 1024;
		const int coarse_frames = fft_size - coarse_max_lag - 1;
		const double fine_hop = 0.1;
		const int fine_lag = 10;                 // frames: a second either way of the coarse lag
		const int fine_scales = 13;              // 0.994 .. 1.006
		const int fine_min_overlap = 60;
		const double block_seconds = 8.0;
		const int block_slack = 8;               // onset units a block may slide
		const std::size_t prefilter_size = 300;
		const double duration_slack = 0.15;
		const std::size_t fine_candidates = 5;

		const double nan = std::numeric_limits<double>::quiet_NaN();

		// ---------------------------------------------------------------- making

		//! sqrt, centred and unit length: a frame's shape, not its level.
		void frame_shape(const float * in, double * out)
		{
			double mean = 0;
			for (int c = 0; c < 12; c++) mean += out[c] = std::sqrt(std::max(0.0f, in[c]));
			mean /= 12;
			double norm = 0;
			for (int c = 0; c < 12; c++) norm += (out[c] -= mean) * out[c];
			norm = std::sqrt(norm);
			for (int c = 0; c < 12; c++) out[c] = norm > 0 ? out[c] / norm : 0;
		}

		// ---------------------------------------------------------------- the FFT

		//! Radix-2 complex transform of fft_size points.
		class fft
		{
		public:
			fft()
			{
				m_twiddle.resize(fft_size / 2);
				for (int i = 0; i < fft_size / 2; i++)
					m_twiddle[i] = std::polar(1.0f, static_cast<float>(-2.0 * 3.14159265358979323846 * i / fft_size));
				m_reverse.resize(fft_size);
				int bits = 0;
				while ((1 << bits) < fft_size) bits++;
				for (int i = 0; i < fft_size; i++)
				{
					int r = 0;
					for (int b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b);
					m_reverse[i] = r;
				}
			}

			void run(std::complex<float> * x, bool inverse) const
			{
				for (int i = 0; i < fft_size; i++)
					if (i < m_reverse[i]) std::swap(x[i], x[m_reverse[i]]);
				for (int len = 2; len <= fft_size; len <<= 1)
				{
					const int step = fft_size / len;
					for (int i = 0; i < fft_size; i += len)
						for (int j = 0; j < len / 2; j++)
						{
							std::complex<float> w = m_twiddle[j * step];
							if (inverse) w = std::conj(w);
							const std::complex<float> u = x[i + j], v = x[i + j + len / 2] * w;
							x[i + j] = u + v;
							x[i + j + len / 2] = u - v;
						}
				}
				if (inverse)
					for (int i = 0; i < fft_size; i++) x[i] /= static_cast<float>(fft_size);
			}

		private:
			std::vector<std::complex<float>> m_twiddle;
			std::vector<int> m_reverse;
		};

		const fft & transform()
		{
			static const fft f;
			return f;
		}

		// ---------------------------------------------------------------- matching

		//! Chroma on a grid of `hop` seconds in normalised time - the file's
		//! time multiplied by `scale` - read `k` pitch classes on. Rows are
		//! unit length, or zero where there is nothing to compare.
		struct sequence
		{
			std::vector<double> x;    // 12 a frame
			std::vector<char> valid;
			std::size_t frames() const { return valid.size(); }
		};

		sequence chroma_sequence(const fingerprint & f, double scale, double hop, int k, std::size_t max_frames)
		{
			sequence s;
			const std::size_t n_src = f.bins();
			if (n_src == 0) return s;
			std::vector<double> src(n_src * 12, 0.0);
			std::vector<char> ok(n_src, 0);
			for (std::size_t b = 0; b < n_src; b++)
			{
				const std::uint8_t * q = &f.chroma[b * 12];
				double mean = 0;
				for (int c = 0; c < 12; c++) mean += q[c];
				if (mean <= 0) continue;
				mean /= 12;
				double norm = 0;
				for (int c = 0; c < 12; c++) norm += (src[b * 12 + c] = q[c] - mean) * src[b * 12 + c];
				if (norm <= 0) continue;
				norm = std::sqrt(norm);
				for (int c = 0; c < 12; c++) src[b * 12 + c] /= norm;
				ok[b] = 1;
			}
			const double last = (f.music_start + (n_src - 1) * bin_seconds) * scale;
			std::size_t n = static_cast<std::size_t>(last / hop) + 1;
			n = std::min(n, max_frames);
			s.x.assign(n * 12, 0.0);
			s.valid.assign(n, 0);
			for (std::size_t j = 0; j < n; j++)
			{
				const double pos = (j * hop / scale - f.music_start) / bin_seconds;
				if (pos < 0 || pos > static_cast<double>(n_src - 1)) continue;
				const std::size_t i0 = static_cast<std::size_t>(pos);
				const std::size_t i1 = std::min(i0 + 1, n_src - 1);
				if (!ok[i0] || !ok[i1]) continue;
				const double w = pos - i0;
				double y[12], norm = 0;
				for (int c = 0; c < 12; c++)
				{
					const int from = ((c + k) % 12 + 12) % 12;
					y[c] = src[i0 * 12 + from] * (1 - w) + src[i1 * 12 + from] * w;
					norm += y[c] * y[c];
				}
				if (norm <= 1e-12) continue;
				norm = std::sqrt(norm);
				for (int c = 0; c < 12; c++) s.x[j * 12 + c] = y[c] / norm;
				s.valid[j] = 1;
			}
			return s;
		}

		//! The onsets rendered on a 10ms grid in normalised time from `t0`.
		std::vector<double> onset_curve(const fingerprint & f, double scale, double t0, std::size_t n)
		{
			std::vector<double> out(n, 0.0);
			const double reach = 4 * onset_sigma / onset_unit;
			for (const fingerprint::onset & o : f.onsets)
			{
				const double i = (o.time * onset_unit * scale - t0) / onset_unit;
				const long lo = std::max(0L, static_cast<long>(std::ceil(i - reach)));
				const long hi = std::min(static_cast<long>(n) - 1, static_cast<long>(std::floor(i + reach)));
				for (long j = lo; j <= hi; j++)
				{
					const double d = (j - i) * onset_unit / onset_sigma;
					out[j] += o.strength * std::exp(-0.5 * d * d);
				}
			}
			return out;
		}

		double correlation(const double * a, const double * b, std::size_t n)
		{
			double ma = 0, mb = 0;
			for (std::size_t i = 0; i < n; i++) { ma += a[i]; mb += b[i]; }
			ma /= n;
			mb /= n;
			double num = 0, da = 0, db = 0;
			for (std::size_t i = 0; i < n; i++)
			{
				const double x = a[i] - ma, y = b[i] - mb;
				num += x * y;
				da += x * x;
				db += y * y;
			}
			if (da <= 1e-18 || db <= 1e-18) return nan;
			return num / std::sqrt(da * db);
		}

		double ref_scale(int tuning, bool by_tuning)
		{
			return by_tuning ? std::pow(2.0, tuning / 1200.0) : 1.0;
		}

		struct alignment
		{
			double score = -std::numeric_limits<double>::infinity();
			int k = 0;
			double scale = 1;   // the query's
			double lag = 0;     // seconds: reference time t sits at query time t + lag
		};

		struct spectrum
		{
			std::vector<std::complex<float>> x;   // 12 * fft_size, conjugated
			std::vector<std::complex<float>> mask;
			double valid = 0;
		};

		spectrum spectrum_of(const sequence & s)
		{
			const fft & t = transform();
			spectrum out;
			out.x.assign(12 * fft_size, 0.0f);
			out.mask.assign(fft_size, 0.0f);
			const std::size_t n = std::min<std::size_t>(s.frames(), coarse_frames);
			for (int c = 0; c < 12; c++)
			{
				std::complex<float> * row = &out.x[c * fft_size];
				for (std::size_t j = 0; j < n; j++) row[j] = static_cast<float>(s.x[j * 12 + c]);
				t.run(row, false);
			}
			for (std::size_t j = 0; j < n; j++)
				if (s.valid[j])
				{
					out.mask[j] = 1.0f;
					out.valid++;
				}
			t.run(out.mask.data(), false);
			return out;
		}

		//! One way of reading the query: at a speed, a pitch class or more on.
		struct query_reading
		{
			spectrum q;
			double scale;
			std::vector<int> rotations;
		};

		//! A candidate's best lag over every reading of the query. The
		//! candidate's spectrum is made here and dropped after: a spectrum is
		//! about 100KB, and the prefilter keeps a few hundred candidates.
		void coarse_pass(const spectrum & a, const std::vector<query_reading> & readings, alignment & best,
		                 std::vector<std::complex<float>> & den, std::vector<std::complex<float>> & num)
		{
			const fft & t = transform();
			for (const query_reading & reading : readings)
			{
				const spectrum & q = reading.q;
				const double scale = reading.scale;
				for (int i = 0; i < fft_size; i++) den[i] = std::conj(a.mask[i]) * q.mask[i];
				t.run(den.data(), true);
				const double need = std::max(0.5 * std::min(a.valid, q.valid), 40.0);
				for (int k : reading.rotations)
				{
					std::fill(num.begin(), num.end(), std::complex<float>(0.0f));
					for (int c = 0; c < 12; c++)
					{
						const std::complex<float> * x = &a.x[c * fft_size];
						const std::complex<float> * y = &q.x[(((c + k) % 12 + 12) % 12) * fft_size];
						for (int i = 0; i < fft_size; i++) num[i] += std::conj(x[i]) * y[i];
					}
					t.run(num.data(), true);
					for (int lag = -coarse_max_lag; lag <= coarse_max_lag; lag++)
					{
						const int i = lag >= 0 ? lag : fft_size + lag;
						const double d = den[i].real();
						if (d < need) continue;
						const double score = num[i].real() / std::max(d, 1.0);
						if (score > best.score)
						{
							best.score = score;
							best.k = k;
							best.scale = scale;
							best.lag = lag * coarse_hop;
						}
					}
				}
			}
		}

		struct verdict
		{
			double chroma = nan, onset = nan, extra = 1, lag = 0, wander = nan;
		};

		verdict fine_pass(const fingerprint & ref, const fingerprint & q, const alignment & al, bool by_tuning)
		{
			verdict v;
			const double sr = ref_scale(ref.tuning, by_tuning);
			const sequence a = chroma_sequence(ref, sr, fine_hop, 0, std::numeric_limits<std::size_t>::max());
			double best = -std::numeric_limits<double>::infinity();
			for (int s = 0; s < fine_scales; s++)
			{
				const double e = 0.994 + 0.001 * s;
				const sequence b = chroma_sequence(q, al.scale * e, fine_hop, al.k, std::numeric_limits<std::size_t>::max());
				const long base = std::lround(al.lag / fine_hop);
				for (long d = -fine_lag; d <= fine_lag; d++)
				{
					const long l = base + d;
					const long j0 = std::max(0L, -l);
					const long j1 = std::min(static_cast<long>(a.frames()), static_cast<long>(b.frames()) - l);
					if (j1 - j0 < fine_min_overlap) continue;
					double sum = 0;
					long count = 0;
					for (long j = j0; j < j1; j++)
					{
						if (!a.valid[j] || !b.valid[j + l]) continue;
						const double * x = &a.x[j * 12];
						const double * y = &b.x[(j + l) * 12];
						for (int c = 0; c < 12; c++) sum += x[c] * y[c];
						count++;
					}
					if (count < fine_min_overlap) continue;
					const double score = sum / count;
					if (score > best)
					{
						best = score;
						v.extra = e;
						v.lag = l * fine_hop;
					}
				}
			}
			if (!std::isfinite(best)) return v;
			v.chroma = best;

			// The onsets along that alignment, block by block: each block of
			// the reference may slide a little against the query, as much as a
			// performance's timing is not perfectly linear in a transfer.
			const double sq = al.scale * v.extra;
			const double t0 = ref.music_start * sr;
			const double t1 = t0 + ref.bins() * bin_seconds * sr;
			const std::size_t n = static_cast<std::size_t>((t1 - t0) / onset_unit);
			const std::vector<double> ra = onset_curve(ref, sr, t0, n);
			const std::vector<double> qa = onset_curve(q, sq, t0 + v.lag - block_slack * onset_unit, n + 2 * block_slack);
			const std::size_t block = static_cast<std::size_t>(block_seconds / onset_unit);
			std::vector<double> scores, where, shift;
			for (std::size_t s0 = 0; s0 + block <= n; s0 += block)
			{
				double best_block = -1;
				int best_shift = 0;
				bool any = false;
				for (int sh = 0; sh <= 2 * block_slack; sh++)
				{
					const double c = correlation(&ra[s0], &qa[s0 + sh], block);
					if (std::isnan(c)) continue;
					any = true;
					if (c > best_block)
					{
						best_block = c;
						best_shift = sh - block_slack;
					}
				}
				// A block silent in the reference says nothing either way.
				bool ref_silent = true;
				for (std::size_t i = s0; i < s0 + block && ref_silent; i++) ref_silent = ra[i] <= 1e-9;
				if (ref_silent) continue;
				scores.push_back(any ? best_block : -1.0);
				if (any)
				{
					where.push_back(static_cast<double>(s0));
					shift.push_back(best_shift);
				}
			}
			if (scores.empty()) return v;
			std::vector<double> sorted = scores;
			std::sort(sorted.begin(), sorted.end());
			const std::size_t m = sorted.size() / 2;
			v.onset = sorted.size() % 2 ? sorted[m] : 0.5 * (sorted[m - 1] + sorted[m]);

			// How far the blocks' best shifts stray from a straight line: a
			// transfer differs from another only by its speed, which is a line;
			// a performance differs by its rubato, which is not.
			if (where.size() >= 3)
			{
				double mx = 0, my = 0;
				for (std::size_t i = 0; i < where.size(); i++) { mx += where[i]; my += shift[i]; }
				mx /= where.size();
				my /= where.size();
				double sxy = 0, sxx = 0;
				for (std::size_t i = 0; i < where.size(); i++)
				{
					sxy += (where[i] - mx) * (shift[i] - my);
					sxx += (where[i] - mx) * (where[i] - mx);
				}
				const double slope = sxx > 0 ? sxy / sxx : 0;
				std::vector<double> residual;
				for (std::size_t i = 0; i < where.size(); i++)
					residual.push_back(std::fabs(shift[i] - (my + slope * (where[i] - mx))));
				std::sort(residual.begin(), residual.end());
				v.wander = residual[residual.size() / 2] * onset_unit * 1000;
			}
			return v;
		}

		// ---------------------------------------------------------------- bytes

		void put_u16(std::string & s, unsigned v)
		{
			s += static_cast<char>(v & 0xFF);
			s += static_cast<char>((v >> 8) & 0xFF);
		}

		unsigned get_u16(const std::string & s, std::size_t at)
		{
			return static_cast<unsigned char>(s[at]) | static_cast<unsigned>(static_cast<unsigned char>(s[at + 1])) << 8;
		}

		unsigned clamp_u16(double v)
		{
			return static_cast<unsigned>(std::min(65535.0, std::max(0.0, std::round(v))));
		}
	}

	// ---------------------------------------------------------------- making

	fingerprint make_fingerprint(const audio_features & f, double excerpt_seconds)
	{
		fingerprint out;
		const std::size_t frames = f.chroma.size() / 12;
		if (frames == 0 || f.chroma_hop <= 0 || f.loudness.size() < frames) return out;

		std::vector<double> shape(frames * 12, 0.0);
		std::vector<char> valid(frames, 0);
		std::size_t first = frames, last = 0;
		for (std::size_t i = 0; i < frames; i++)
		{
			const float * c = &f.chroma[i * 12];
			float sum = 0;
			for (int k = 0; k < 12; k++) sum += c[k];
			if (f.loudness[i] < f.silence || sum <= 0) continue;
			frame_shape(c, &shape[i * 12]);
			valid[i] = 1;
			first = std::min(first, i);
			last = i;
		}
		if (first >= frames) return out;

		const double start = first * f.chroma_hop;
		double end = last * f.chroma_hop;
		out.duration = end - start;
		out.music_start = std::round(start / onset_unit) * onset_unit;
		if (excerpt_seconds > 0) end = std::min(end, start + excerpt_seconds);
		out.tuning = static_cast<int>(std::lround(f.tuning_cents));

		const std::size_t bins = static_cast<std::size_t>((end - start) / bin_seconds);
		out.chroma.assign(bins * 12, 0);
		for (std::size_t b = 0; b < bins; b++)
		{
			const std::size_t i0 = static_cast<std::size_t>((start + b * bin_seconds) / f.chroma_hop);
			const std::size_t i1 = std::min(frames, std::max(i0 + 1,
				static_cast<std::size_t>((start + (b + 1) * bin_seconds) / f.chroma_hop)));
			double m[12] = { 0 };
			int count = 0;
			for (std::size_t i = i0; i < i1; i++)
			{
				if (!valid[i]) continue;
				for (int k = 0; k < 12; k++) m[k] += shape[i * 12 + k];
				count++;
			}
			if (count == 0) continue;
			const double lo = *std::min_element(m, m + 12);
			double hi = 0;
			for (double & v : m) hi = std::max(hi, v = std::max(0.0, v - lo));
			if (hi <= 0) continue;
			// Each pitch class to four levels of the strongest, then to whether
			// it stands above the bin's mean level: the levels as
			// fingerprint_lab measured them, the bit as it stores them.
			long q[12], sum = 0;
			for (int k = 0; k < 12; k++) sum += q[k] = std::lround(3 * m[k] / hi);
			for (int k = 0; k < 12; k++) out.chroma[b * 12 + k] = q[k] * 12 > sum ? 1 : 0;
		}

		// The strongest local maxima of the novelty.
		const double r = f.novelty_rate;
		if (r > 0 && !f.novelty.empty())
		{
			const std::size_t a = static_cast<std::size_t>(start * r);
			const std::size_t z = std::min(f.novelty.size(), static_cast<std::size_t>(end * r));
			std::vector<std::size_t> peaks;
			for (std::size_t i = a + 1; i + 1 < z; i++)
			{
				const float v = f.novelty[i];
				if (v > 0 && v > f.novelty[i - 1] && v >= f.novelty[i + 1]) peaks.push_back(i);
			}
			const std::size_t keep = static_cast<std::size_t>(onsets_per_second * (end - start));
			if (peaks.size() > keep)
			{
				std::stable_sort(peaks.begin(), peaks.end(),
				                 [&](std::size_t x, std::size_t y) { return f.novelty[x] > f.novelty[y]; });
				peaks.resize(keep);
				std::sort(peaks.begin(), peaks.end());
			}
			float strongest = 0;
			for (std::size_t p : peaks) strongest = std::max(strongest, f.novelty[p]);
			for (std::size_t p : peaks)
			{
				fingerprint::onset o;
				o.time = static_cast<std::uint32_t>(std::lround(p / r / onset_unit));
				o.strength = static_cast<std::uint8_t>(
					std::max(1L, std::min(15L, std::lround(15.0 * f.novelty[p] / strongest))));
				out.onsets.push_back(o);
			}
		}
		return out;
	}

	// ---------------------------------------------------------------- bytes

	std::string encode_fingerprint(const fingerprint & f)
	{
		std::string s;
		s += format_version;
		put_u16(s, clamp_u16(f.duration * 10));
		put_u16(s, clamp_u16(f.music_start / onset_unit));
		s += static_cast<char>(static_cast<signed char>(std::max(-127, std::min(127, f.tuning))));
		const std::size_t bins = std::min<std::size_t>(f.bins(), 65535);
		const std::size_t onsets = std::min<std::size_t>(f.onsets.size(), 65535);
		put_u16(s, static_cast<unsigned>(bins));
		put_u16(s, static_cast<unsigned>(onsets));
		for (std::size_t b = 0; b < bins; b += 2)
		{
			std::uint32_t word = 0;
			for (std::size_t j = b; j < std::min(bins, b + 2); j++)
				for (int k = 0; k < 12; k++)
					if (f.chroma[j * 12 + k] != 0) word |= 1u << ((j - b) * 12 + k);
			s += static_cast<char>(word & 0xFF);
			s += static_cast<char>((word >> 8) & 0xFF);
			s += static_cast<char>((word >> 16) & 0xFF);
		}
		std::uint32_t previous = 0;
		for (std::size_t i = 0; i < onsets; i++)
		{
			const fingerprint::onset & o = f.onsets[i];
			std::uint64_t v = static_cast<std::uint64_t>(o.time - std::min(previous, o.time)) * 16 + (o.strength & 15);
			previous = o.time;
			do
			{
				unsigned char byte = v & 0x7F;
				v >>= 7;
				if (v) byte |= 0x80;
				s += static_cast<char>(byte);
			} while (v);
		}
		return s;
	}

	bool decode_fingerprint(const std::string & s, fingerprint & out)
	{
		out = fingerprint();
		if (s.size() < 10 || s[0] != format_version) return false;
		out.duration = get_u16(s, 1) / 10.0;
		out.music_start = get_u16(s, 3) * onset_unit;
		out.tuning = static_cast<signed char>(s[5]);
		const std::size_t bins = get_u16(s, 6), onsets = get_u16(s, 8);
		std::size_t at = 10;
		if (s.size() < at + (bins + 1) / 2 * 3) return false;
		out.chroma.resize(bins * 12);
		for (std::size_t b = 0; b < bins; b += 2, at += 3)
		{
			const std::uint32_t word = static_cast<unsigned char>(s[at]) |
			                           static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 1])) << 8 |
			                           static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 2])) << 16;
			for (std::size_t j = b; j < std::min(bins, b + 2); j++)
				for (int k = 0; k < 12; k++)
					out.chroma[j * 12 + k] = static_cast<std::uint8_t>((word >> ((j - b) * 12 + k)) & 1);
		}
		std::uint32_t time = 0;
		out.onsets.reserve(onsets);
		for (std::size_t i = 0; i < onsets; i++)
		{
			std::uint64_t v = 0;
			for (int shift = 0;; shift += 7)
			{
				if (at >= s.size() || shift > 35) return false;
				const unsigned char byte = static_cast<unsigned char>(s[at++]);
				v |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
				if (!(byte & 0x80)) break;
			}
			time += static_cast<std::uint32_t>(v / 16);
			fingerprint::onset o;
			o.time = time;
			o.strength = static_cast<std::uint8_t>(v % 16);
			out.onsets.push_back(o);
		}
		return at == s.size();
	}

	std::string to_base64(const std::string & bytes)
	{
		static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		out.reserve((bytes.size() + 2) / 3 * 4);
		for (std::size_t i = 0; i < bytes.size(); i += 3)
		{
			std::uint32_t v = static_cast<unsigned char>(bytes[i]) << 16;
			if (i + 1 < bytes.size()) v |= static_cast<unsigned char>(bytes[i + 1]) << 8;
			if (i + 2 < bytes.size()) v |= static_cast<unsigned char>(bytes[i + 2]);
			out += table[(v >> 18) & 63];
			out += table[(v >> 12) & 63];
			out += i + 1 < bytes.size() ? table[(v >> 6) & 63] : '=';
			out += i + 2 < bytes.size() ? table[v & 63] : '=';
		}
		return out;
	}

	bool from_base64(const std::string & text, std::string & bytes)
	{
		bytes.clear();
		std::uint32_t v = 0;
		int bits = 0;
		for (char c : text)
		{
			int d;
			if (c >= 'A' && c <= 'Z') d = c - 'A';
			else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
			else if (c >= '0' && c <= '9') d = c - '0' + 52;
			else if (c == '+') d = 62;
			else if (c == '/') d = 63;
			else if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
			else return false;
			v = (v << 6) | static_cast<std::uint32_t>(d);
			bits += 6;
			if (bits >= 8)
			{
				bits -= 8;
				bytes += static_cast<char>((v >> bits) & 0xFF);
			}
		}
		return true;
	}

	// ---------------------------------------------------------------- the index

	fingerprint_index::reference fingerprint_index::make_reference(int id, const fingerprint & f)
	{
		reference r;
		r.id = id;
		r.duration = f.duration;
		r.tuning = f.tuning;
		double norm = 0;
		for (int c = 0; c < 12; c++)
		{
			double sum = 0;
			for (std::size_t b = 0; b < f.bins(); b++) sum += f.chroma[b * 12 + c];
			r.profile[c] = sum;
			norm += sum * sum;
		}
		norm = std::sqrt(norm);
		for (double & p : r.profile) p = norm > 0 ? p / norm : 0;
		return r;
	}

	void fingerprint_index::add(int id, fingerprint f)
	{
		reference r = make_reference(id, f);
		r.fp = std::move(f);
		m_refs.push_back(std::move(r));
	}

	bool fingerprint_index::add_encoded(int id, std::string bytes)
	{
		fingerprint f;
		if (!decode_fingerprint(bytes, f)) return false;
		reference r = make_reference(id, f);
		r.encoded = std::move(bytes);
		m_refs.push_back(std::move(r));
		return true;
	}

	const fingerprint & fingerprint_index::reference_fp(const reference & r, fingerprint & holder)
	{
		if (r.encoded.empty()) return r.fp;
		decode_fingerprint(r.encoded, holder);   // decoded once already, in add_encoded
		return holder;
	}

	std::vector<fingerprint_match> fingerprint_index::identify(const fingerprint & query, const std::vector<int> & also,
	                                                           std::size_t max_results) const
	{
		std::vector<fingerprint_match> out;
		if (query.empty() || m_refs.empty()) return out;

		// --- prefilter: a similar duration and a similar overall chroma ------
		double profile[12] = { 0 }, norm = 0;
		for (int c = 0; c < 12; c++)
		{
			for (std::size_t b = 0; b < query.bins(); b++) profile[c] += query.chroma[b * 12 + c];
			norm += profile[c] * profile[c];
		}
		norm = std::sqrt(norm);
		if (norm <= 0) return out;
		for (double & p : profile) p /= norm;

		std::vector<std::pair<double, std::size_t>> ranked;
		for (std::size_t i = 0; i < m_refs.size(); i++)
		{
			const reference & r = m_refs[i];
			if (query.duration <= 0 || std::fabs(r.duration / query.duration - 1) > duration_slack) continue;
			double sim = -1;
			for (int k = -1; k <= 1; k++)
			{
				double s = 0;
				for (int c = 0; c < 12; c++) s += r.profile[c] * profile[((c + k) % 12 + 12) % 12];
				sim = std::max(sim, s);
			}
			ranked.emplace_back(sim, i);
		}
		std::sort(ranked.begin(), ranked.end(), [](const auto & a, const auto & b) { return a.first > b.first; });
		if (ranked.size() > prefilter_size) ranked.resize(prefilter_size);
		std::vector<std::size_t> cand;
		for (const auto & r : ranked) cand.push_back(r.second);
		for (int id : also)
			for (std::size_t i = 0; i < m_refs.size(); i++)
				if (m_refs[i].id == id && std::find(cand.begin(), cand.end(), i) == cand.end()) cand.push_back(i);
		if (cand.empty()) return out;

		// --- by the tuning offsets first, by a speed search if that fails ----
		struct result
		{
			std::size_t ref;
			verdict v;
			alignment al;
			bool by_tuning;
		};
		std::vector<result> best_results;
		double best_onset = -2;
		for (int mode = 0; mode < 2; mode++)
		{
			const bool by_tuning = mode == 0;
			std::vector<query_reading> readings;
			if (by_tuning)
			{
				for (int k = -1; k <= 1; k++)
				{
					const double s = std::pow(2.0, query.tuning / 1200.0) * std::pow(2.0, k / 12.0);
					readings.push_back({ spectrum_of(chroma_sequence(query, s, coarse_hop, 0, coarse_frames)), s, { k } });
				}
			}
			else
			{
				for (int i = 0; i <= 10; i++)
				{
					const double s = 0.95 + 0.01 * i;
					readings.push_back({ spectrum_of(chroma_sequence(query, s, coarse_hop, 0, coarse_frames)), s, { -1, 0, 1 } });
				}
			}
			std::vector<alignment> al(cand.size());
			std::vector<std::complex<float>> den(fft_size), num(fft_size);
			fingerprint holder;
			for (std::size_t c = 0; c < cand.size(); c++)
			{
				const fingerprint & ref = reference_fp(m_refs[cand[c]], holder);
				coarse_pass(spectrum_of(chroma_sequence(ref, ref_scale(ref.tuning, by_tuning), coarse_hop, 0, coarse_frames)),
				            readings, al[c], den, num);
			}

			std::vector<std::size_t> order(cand.size());
			std::iota(order.begin(), order.end(), 0);
			std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return al[a].score > al[b].score; });
			std::vector<result> results;
			for (std::size_t j = 0; j < order.size() && results.size() < fine_candidates; j++)
			{
				const std::size_t c = order[j];
				if (!std::isfinite(al[c].score)) break;
				results.push_back({ cand[c], fine_pass(reference_fp(m_refs[cand[c]], holder), query, al[c], by_tuning),
				                    al[c], by_tuning });
			}
			double top = -2;
			for (const result & r : results)
				if (!std::isnan(r.v.onset)) top = std::max(top, r.v.onset);
			if (top > best_onset)
			{
				best_onset = top;
				best_results = std::move(results);
			}
			// The speed search only for what the tuning offsets could not
			// place; whichever found the better onsets is kept.
			if (best_onset >= fingerprint_identified) break;
		}

		std::sort(best_results.begin(), best_results.end(), [](const result & a, const result & b)
		{
			const double x = std::isnan(a.v.onset) ? -2 : a.v.onset, y = std::isnan(b.v.onset) ? -2 : b.v.onset;
			if (x != y) return x > y;
			return a.v.chroma > b.v.chroma;
		});
		for (const result & r : best_results)
		{
			const reference & ref = m_refs[r.ref];
			bool seen = false;
			for (const fingerprint_match & m : out) seen = seen || m.id == ref.id;
			if (seen) continue;
			fingerprint_match m;
			m.id = ref.id;
			m.onset = std::isnan(r.v.onset) ? -1 : r.v.onset;
			m.chroma = std::isnan(r.v.chroma) ? -1 : r.v.chroma;
			// Both were brought to one time axis, the reference by its scale and
			// the query by the alignment's; the ratio is how much faster the
			// query runs.
			m.speed = r.al.scale * r.v.extra / ref_scale(ref.tuning, r.by_tuning);
			m.semitones = r.al.k;
			m.wander_ms = std::isnan(r.v.wander) ? -1 : r.v.wander;
			out.push_back(m);
			if (out.size() >= max_results) break;
		}
		return out;
	}
}
