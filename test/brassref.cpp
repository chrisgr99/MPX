/** THE TROMBONE'S REFERENCE, from recordings: phase 1 of docs/brass.md, target B6.

The University of Iowa Musical Instrument Samples, tenor trombone, recorded before 2012: chromatic
scales from E2 to C5 at pp, mf and ff, one Neumann KM 84 in an anechoic room, 16 bits, 44.1 kHz, mono.
The page says they "may be downloaded and used for any projects, without restrictions". They are not
kept in the repository; to fetch them, into build/iowa-trombone:

    https://theremin.music.uiowa.edu/MIStenortrombone.html
    TenorTrombone.{pp,mf,ff}.{E2B2,C3B3,C4B4,C5}.aiff
    (the pp and mf E2B2 files are named with a space after the dynamic's full stop on the site)

For each note of each scale: its pitch, found and checked against the note the scale should have there;
its level over the steady part, in decibels of full scale; how long it takes to rise from 30 dB under
its steady level to within 3 dB of it; how long it takes to fall from within 3 dB of it to 30 dB under;
the levels of its harmonics up to 10 kHz; its spectral centroid, amplitude-weighted over the harmonics;
the slopes of its spectral envelope below and above 1 kHz, in decibels an octave; and how much of it
lies above 1 kHz, in decibels against the whole.

    make brassref                                    every scale in build/iowa-trombone
    make brassref ARGS="DIR OUT.tsv"                 another folder, another table

The levels are the recording's, not sound pressure: the same microphone and, as far as the collection
says, the same gain for every dynamic, so the differences between pp, mf and ff are meaningful and the
absolute figures are not. */
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/** A MONO AIFF, 16 bits, as floats from minus one to one. False where it is anything else. */
static bool readAiff(const std::string& path, std::vector<float>& out, double& rate) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::vector<uint8_t> b;
	uint8_t chunk[65536];
	size_t got;
	while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0)
		b.insert(b.end(), chunk, chunk + got);
	std::fclose(f);
	auto u32 = [&](size_t i) { return (uint32_t) b[i] << 24 | (uint32_t) b[i + 1] << 16 | (uint32_t) b[i + 2] << 8 | b[i + 3]; };
	auto u16 = [&](size_t i) { return (uint16_t) (b[i] << 8 | b[i + 1]); };
	if (b.size() < 12 || std::memcmp(&b[0], "FORM", 4) || (std::memcmp(&b[8], "AIFF", 4) && std::memcmp(&b[8], "AIFC", 4)))
		return false;
	int channels = 0, bits = 0;
	uint32_t frames = 0;
	size_t data = 0, dataEnd = 0;
	for (size_t i = 12; i + 8 <= b.size(); ) {
		const uint32_t size = u32(i + 4);
		if (!std::memcmp(&b[i], "COMM", 4)) {
			channels = (int16_t) u16(i + 8);
			frames = u32(i + 10);
			bits = (int16_t) u16(i + 14);
			// The rate as an 80-bit extended float: a sign and fifteen bits of exponent, then sixty-four of
			// mantissa with the integer bit showing.
			const int exponent = (u16(i + 16) & 0x7fff) - 16383 - 63;
			uint64_t mantissa = 0;
			for (int k = 0; k < 8; k++)
				mantissa = mantissa << 8 | b[i + 18 + k];
			rate = std::ldexp((double) mantissa, exponent);
		}
		else if (!std::memcmp(&b[i], "SSND", 4)) {
			data = i + 16 + u32(i + 8);
			dataEnd = i + 8 + size;
		}
		i += 8 + size + (size & 1);
	}
	if (channels != 1 || bits != 16 || !data)
		return false;
	out.clear();
	for (size_t i = data; i + 1 < dataEnd && out.size() < frames; i += 2)
		out.push_back((float) (int16_t) u16(i) / 32768.f);
	return true;
}

static void fft(std::vector<std::complex<double> >& a) {
	const size_t n = a.size();
	for (size_t i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		const std::complex<double> w = std::polar(1.0, -2.0 * M_PI / (double) len);
		for (size_t i = 0; i < n; i += len) {
			std::complex<double> wk = 1.0;
			for (size_t k = 0; k < len / 2; k++) {
				const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * wk;
				a[i + k] = u + v;
				a[i + k + len / 2] = u - v;
				wk *= w;
			}
		}
	}
}

static double db(double x) { return 20.0 * std::log10(x + 1e-12); }

static double rmsOver(const std::vector<float>& x, size_t from, size_t to) {
	double s = 0.0;
	for (size_t i = from; i < to && i < x.size(); i++)
		s += (double) x[i] * x[i];
	return std::sqrt(s / (double) std::max<size_t>(1, to - from));
}

/** THE PITCH OF `x` from `from`, over `n` samples: YIN's cumulative mean normalised difference, its first
dip under 0.15 between 50 and 1000 Hz, refined by a parabola. Nought where there is none. */
static double yin(const std::vector<float>& x, size_t from, size_t n, double rate) {
	const int least = (int) (rate / 1000.0), most = (int) (rate / 50.0);
	if (from + n + (size_t) most >= x.size())
		return 0.0;
	std::vector<double> d((size_t) most + 2, 0.0);
	for (int lag = 1; lag <= most + 1; lag++) {
		double s = 0.0;
		for (size_t i = 0; i < n; i++) {
			const double e = (double) x[from + i] - x[from + i + (size_t) lag];
			s += e * e;
		}
		d[(size_t) lag] = s;
	}
	double running = 0.0;
	std::vector<double> c(d.size(), 1.0);
	for (size_t lag = 1; lag < d.size(); lag++) {
		running += d[lag];
		c[lag] = running > 0.0 ? d[lag] * (double) lag / running : 1.0;
	}
	for (int lag = least; lag <= most; lag++) {
		if (c[(size_t) lag] < 0.15 && c[(size_t) lag] <= c[(size_t) lag + 1]) {
			const double a = c[(size_t) lag - 1], b = c[(size_t) lag], e = c[(size_t) lag + 1];
			const double shift = (a - 2.0 * b + e) != 0.0 ? 0.5 * (a - e) / (a - 2.0 * b + e) : 0.0;
			return rate / ((double) lag + shift);
		}
	}
	return 0.0;
}

static const char* NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static std::string noteName(int midi) {
	return std::string(NAMES[midi % 12]) + std::to_string(midi / 12 - 1);
}

static int midiOf(const std::string& name) {
	static const int SEMITONE[7] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
	const int letter = std::toupper((unsigned char) name[0]) - 'A';
	if (letter < 0 || letter > 6)
		return -1;
	size_t i = 1;
	int s = SEMITONE[letter];
	if (i < name.size() && name[i] == '#') { s++; i++; }
	else if (i < name.size() && name[i] == 'b') { s--; i++; }
	return (std::atoi(name.c_str() + i) + 1) * 12 + s;
}

struct NoteRow {
	std::string dynamic;
	int midi = 0;
	double f0 = 0.0, cents = 0.0, level = 0.0, onsetMs = 0.0, releaseMs = 0.0, centroid = 0.0;
	double slopeBelow = 0.0, slopeAbove = 0.0, aboveDb = 0.0;
	std::vector<double> harmonics;   // dB under the strongest
};

/** THE SLOPE of level against octave, decibels an octave, by least squares over the harmonics whose
frequency is in [lo, hi); nought where fewer than three are. */
static double slope(const NoteRow& r, double lo, double hi) {
	double sx = 0, sy = 0, sxx = 0, sxy = 0;
	int n = 0;
	for (size_t k = 0; k < r.harmonics.size(); k++) {
		const double f = r.f0 * (double) (k + 1);
		if (f < lo || f >= hi)
			continue;
		const double x = std::log2(f), y = r.harmonics[k];
		sx += x; sy += y; sxx += x * x; sxy += x * y;
		n++;
	}
	if (n < 3)
		return NAN;
	return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

int main(int argc, char** argv) {
	const std::string dir = argc > 1 ? argv[1] : "build/iowa-trombone";
	const std::string out = argc > 2 ? argv[2] : "research/brass/iowa-tenor-trombone.tsv";
	const char* dynamics[3] = {"pp", "mf", "ff"};
	const char* ranges[4] = {"E2B2", "C3B3", "C4B4", "C5"};
	std::vector<NoteRow> rows;
	for (const char* dyn : dynamics)
		for (const char* range : ranges) {
			const std::string path = dir + "/TenorTrombone." + dyn + "." + range + ".aiff";
			std::vector<float> x;
			double rate = 44100.0;
			if (!readAiff(path, x, rate)) {
				std::printf("could not read %s\n", path.c_str());
				continue;
			}
			// THE NOTES THE SCALE SHOULD HOLD, from its name: E2B2 is E2 to B2.
			const std::string r = range;
			const int first = midiOf(r.substr(0, 2));
			const int last = r.size() > 2 ? midiOf(r.substr(2)) : first;
			// THE NOTES IT DOES HOLD: 10 ms windows under a threshold are silence; a sound is a run of the rest
			// at least 0.3 s long, gaps under 0.1 s bridged. Some scales have digital silence between their
			// notes and some the room's noise, so no one threshold suits all of them; the scale's name says how
			// many notes it holds, and the threshold is the middle of those, in steps of a decibel from 60 dB
			// under the loudest window to 5 dB under it, that find exactly that many.
			const size_t hop = (size_t) (0.01 * rate);
			std::vector<double> env;
			double loudest = -1e9;
			for (size_t i = 0; i + hop <= x.size(); i += hop) {
				env.push_back(db(rmsOver(x, i, i + hop)));
				loudest = std::max(loudest, env.back());
			}
			auto spansAt = [&](double threshold) {
				std::vector<std::pair<size_t, size_t> > found;
				for (size_t w = 0; w < env.size(); ) {
					if (env[w] < threshold) { w++; continue; }
					size_t e = w, quiet = 0;
					while (e < env.size() && quiet < 10) {
						quiet = env[e] < threshold ? quiet + 1 : 0;
						e++;
					}
					e -= quiet;
					if (e - w >= 30)
						found.push_back(std::make_pair(w * hop, e * hop));
					w = e + quiet;
				}
				return found;
			};
			const int expected = last - first + 1;
			std::vector<double> fits;
			double nearest = loudest - 30.0;
			int nearestMiss = 1 << 30;
			for (double t = loudest - 60.0; t <= loudest - 5.0; t += 1.0) {
				const int got = (int) spansAt(t).size();
				if (got == expected)
					fits.push_back(t);
				if (std::abs(got - expected) < nearestMiss) {
					nearestMiss = std::abs(got - expected);
					nearest = t;
				}
			}
			const double threshold = fits.empty() ? nearest : fits[fits.size() / 2];
			const std::vector<std::pair<size_t, size_t> > spans = spansAt(threshold);
			std::printf("%s %s: %zu notes found, %d expected\n", dyn, range, spans.size(), expected);
			for (size_t s = 0; s < spans.size(); s++) {
				NoteRow row;
				row.dynamic = dyn;
				const size_t a = spans[s].first, b = spans[s].second, len = b - a;
				// THE STEADY PART: the middle half.
				const size_t mid0 = a + len / 4, mid1 = b - len / 4;
				row.level = db(rmsOver(x, mid0, mid1));
				// The pitch, from the middle, and the note it is nearest.
				row.f0 = yin(x, mid0, std::min<size_t>(mid1 - mid0, (size_t) (0.1 * rate)), rate);
				if (row.f0 <= 0.0)
					continue;
				const double m = 69.0 + 12.0 * std::log2(row.f0 / 440.0);
				row.midi = (int) std::lround(m);
				row.cents = 100.0 * (m - row.midi);
				const int should = (int) s < expected && spans.size() == (size_t) expected ? first + (int) s : row.midi;
				if (should != row.midi)
					std::printf("  note %zu: heard %s, the scale has %s there\n", s, noteName(row.midi).c_str(),
						noteName(should).c_str());
				// THE ONSET: 2 ms windows from 30 dB under the steady level to within 3, looked for from the end
				// of the note before, or half a second before this one, whichever is later.
				const size_t h2 = (size_t) (0.002 * rate);
				const size_t before = s > 0 ? spans[s - 1].second : 0;
				size_t from = std::max(before, a > (size_t) (0.5 * rate) ? a - (size_t) (0.5 * rate) : 0), rise0 = 0, rise1 = 0;
				// Only once the level has been 30 dB under the note: where the room's noise is nearer than that,
				// the start cannot be seen, and none is given.
				bool quietSeen = false;
				for (size_t i = from; i + h2 < mid0; i += h2) {
					const double l = db(rmsOver(x, i, i + h2));
					if (l <= row.level - 30.0)
						quietSeen = true;
					if (!rise0 && quietSeen && l > row.level - 30.0)
						rise0 = i;
					if (rise0 && l > row.level - 3.0) {
						rise1 = i;
						break;
					}
				}
				row.onsetMs = rise1 > rise0 ? 1000.0 * (double) (rise1 - rise0) / rate : 0.0;
				// THE RELEASE: from the last 2 ms window within 3 dB of the steady level to 30 dB under it, looked
				// for up to the next note, or a second after this one, whichever is sooner.
				const size_t after = std::min(s + 1 < spans.size() ? spans[s + 1].first : x.size(), b + (size_t) rate);
				size_t fall0 = 0, fall1 = 0;
				for (size_t i = mid0; i + h2 < std::min(x.size(), after); i += h2) {
					const double l = db(rmsOver(x, i, i + h2));
					if (l > row.level - 3.0) {
						fall0 = i;
						fall1 = 0;
					}
					else if (fall0 && !fall1 && l < row.level - 30.0)
						fall1 = i;
				}
				row.releaseMs = fall1 > fall0 ? 1000.0 * (double) (fall1 - fall0) / rate : 0.0;
				// THE HARMONICS, from a Hann-windowed spectrum of the steady part, each the strongest bin within
				// 3 per cent of its multiple of the pitch, interpolated on a parabola in decibels.
				size_t n = 1;
				while (n * 2 <= mid1 - mid0)
					n *= 2;
				std::vector<std::complex<double> > spec(n * 2, 0.0);
				for (size_t i = 0; i < n; i++)
					spec[i] = (double) x[mid0 + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) n));
				fft(spec);
				const double binHz = rate / (double) spec.size();
				double strongest = -1e9;
				for (int k = 1; (double) k * row.f0 < 10000.0; k++) {
					const double want = (double) k * row.f0;
					const size_t lo = (size_t) std::floor(want * 0.97 / binHz), hi = (size_t) std::ceil(want * 1.03 / binHz);
					size_t best = lo;
					for (size_t i = lo; i <= hi && i + 1 < spec.size() / 2; i++)
						if (std::abs(spec[i]) > std::abs(spec[best]))
							best = i;
					const double l = db(std::abs(spec[best]));
					row.harmonics.push_back(l);
					strongest = std::max(strongest, l);
				}
				double sumA = 0.0, sumFA = 0.0, all = 0.0, above = 0.0;
				for (size_t k = 0; k < row.harmonics.size(); k++) {
					row.harmonics[k] -= strongest;
					const double amp = std::pow(10.0, row.harmonics[k] / 20.0), f = row.f0 * (double) (k + 1);
					sumA += amp;
					sumFA += f * amp;
					all += amp * amp;
					if (f >= 1000.0)
						above += amp * amp;
				}
				row.centroid = sumA > 0.0 ? sumFA / sumA : 0.0;
				row.aboveDb = 10.0 * std::log10((above + 1e-12) / (all + 1e-12));
				row.slopeBelow = slope(row, 0.0, 1000.0);
				row.slopeAbove = slope(row, 1000.0, 5000.0);
				rows.push_back(row);
			}
		}

	FILE* f = std::fopen(out.c_str(), "w");
	if (!f) {
		std::printf("could not write %s\n", out.c_str());
		return 1;
	}
	std::fprintf(f, "note\tmidi\tdynamic\tf0_hz\tcents\tlevel_dbfs\tonset_ms\trelease_ms\tcentroid_hz\tcentroid_over_f0"
		"\tslope_below_1k_db_per_octave\tslope_above_1k_db_per_octave\tabove_1k_db");
	for (int k = 1; k <= 20; k++)
		std::fprintf(f, "\th%d_db", k);
	std::fprintf(f, "\n");
	for (const NoteRow& r : rows) {
		auto cell = [](double v, const char* form) {
			static char buf[32];
			if (std::isnan(v) || v <= 0.0)
				buf[0] = 0;
			else
				std::snprintf(buf, sizeof buf, form, v);
			return std::string(buf);
		};
		auto slopeCell = [](double v) {
			char buf[32];
			if (std::isnan(v))
				return std::string();
			std::snprintf(buf, sizeof buf, "%+.1f", v);
			return std::string(buf);
		};
		std::fprintf(f, "%s\t%d\t%s\t%.2f\t%+.1f\t%.1f\t%s\t%s\t%.0f\t%.2f\t%s\t%s\t%.1f", noteName(r.midi).c_str(), r.midi,
			r.dynamic.c_str(), r.f0, r.cents, r.level, cell(r.onsetMs, "%.0f").c_str(), cell(r.releaseMs, "%.0f").c_str(),
			r.centroid, r.centroid / r.f0, slopeCell(r.slopeBelow).c_str(), slopeCell(r.slopeAbove).c_str(), r.aboveDb);
		for (int k = 0; k < 20; k++)
			if (k < (int) r.harmonics.size())
				std::fprintf(f, "\t%.1f", r.harmonics[(size_t) k]);
			else
				std::fprintf(f, "\t");
		std::fprintf(f, "\n");
	}
	std::fclose(f);
	std::printf("%zu notes written to %s\n", rows.size(), out.c_str());

	// THE DYNAMICS SET SIDE BY SIDE: for each note recorded at all three, the medians of the differences.
	auto find = [&](int midi, const char* dyn) -> const NoteRow* {
		for (const NoteRow& r : rows)
			if (r.midi == midi && r.dynamic == dyn)
				return &r;
		return nullptr;
	};
	std::vector<double> levelMf, levelFf, centMf, centFf, aboveMf, aboveFf;
	for (int midi = 40; midi <= 72; midi++) {
		const NoteRow* p = find(midi, "pp");
		const NoteRow* m = find(midi, "mf");
		const NoteRow* ff = find(midi, "ff");
		if (!p || !m || !ff)
			continue;
		levelMf.push_back(m->level - p->level);
		levelFf.push_back(ff->level - m->level);
		centMf.push_back(m->centroid / p->centroid);
		centFf.push_back(ff->centroid / m->centroid);
		aboveMf.push_back(m->aboveDb - p->aboveDb);
		aboveFf.push_back(ff->aboveDb - m->aboveDb);
	}
	auto median = [](std::vector<double> v) {
		if (v.empty())
			return 0.0;
		std::sort(v.begin(), v.end());
		return v[v.size() / 2];
	};
	std::printf("over %zu notes recorded at all three dynamics, medians:\n", levelMf.size());
	std::printf("  level        pp to mf %+.1f dB, mf to ff %+.1f dB\n", median(levelMf), median(levelFf));
	std::printf("  centroid     pp to mf x%.2f, mf to ff x%.2f\n", median(centMf), median(centFf));
	std::printf("  above 1 kHz  pp to mf %+.1f dB, mf to ff %+.1f dB of the whole\n", median(aboveMf), median(aboveFf));
	return 0;
}
