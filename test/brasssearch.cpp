/** THE TROMBONE'S SETTINGS SEARCHED AGAINST A RECORDING: tuning by ear, phase 7 of docs/brass.md, done by the
machine while nobody is listening.

    make brasssearch ARGS="MIDI DYN [minutes=M] [threads=T] [seed=S] [air=SHARE] [NAME=LO:HI] [from=...] [try=...] [name=NAME]"

One note, as mpxTrombone's tuning plays it — the lips' tension, gap, damping and mass, the mouth's pressure,
the breath's rise, where it starts and the curve it rises by — each setting played through BrassPlayer for
1.6 s, the slide first put in tune for it (BrassPlayer::tuneFor with the mouth given), and compared with the
Iowa recording of the note at that dynamic (the Rack user folder's DreamerMPX/trombone-reference) from where
each is first heard:

- THE HELD TONE: each of the first 20 harmonics' level, against all 20 together, over 0.4 to 0.7 s.
- HOW IT GROWS: every 25 ms from the start to 375 ms, the level of four bands of harmonics — 1 to 3, 4 to
  8, 9 to 14, 15 to 20 — against the held tone's whole level: the loudness coming up and the tone opening.
- HOW ROUGH, as Chris heard the pilot's eighteen: the sound between the harmonics held, below the note
  too; how much the held tone's bands wander; and how far its first 150 ms are from repeating — each only
  past the recording's by a margin (see distance).
- HIGHS THE RECORDING DOES NOT HAVE, to 10 kHz.
- HOW LATE: how long after the tongue it is first heard, which the player can start ahead of.

Only what the recording holds over its own noise counts: the harmonics within 45 dB of its strongest and
under 10 kHz, and the bands that have one. The mean difference in decibels of the held tone and, a third as
much, of the growth, each difference held to 25 and 15 dB, with the roughness's points, a tenth for each
decibel of highs the recording lacks, a hundredth of a point for each millisecond late past 40, and a third for each cent out of tune past five, make the
distance; a note more than 25 cents out of tune, more than half a second late, or silent, is far away.
Random settings at first, or, from=, the setting of the note beside it and small changes of it; then, a
generation at a time, the best sixteen changed a little, less and less, with some random ones still. air=
sets the breath noise, nought for none, as the recordings have none to hear; pitch=recording plays the note
at the recording's own pitch, for listening to its tone beside it; NAME=LO:HI a range; try= scores
one setting, given as the best are printed, into build/Search MIDI DYN, tried.wav. Each trial is written to
build/Search MIDI DYN.tsv; the best five, at the end, to build/Search MIDI DYN, best N.wav, and printed with
the knobs' values. Shared over `threads` threads, the machine's less two. */
#include "../src/BrassInstrument.hpp"
#include "../src/BrassPlayer.hpp"
#include "../src/BrassStart.hpp"
#include "../src/BrassTable.hpp"
#include "../src/Crew.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace px;

static const double RATE = 48000.0;
/** WHERE THE TONGUE LETS GO in what playSetting plays, seconds; and, 50 ms before the note's end, where its
held parts are measured to, samples. */
static const double TONGUE_AT = 0.3;
static const size_t NOTE_ENDS = (size_t) ((0.3 + 1.6 - 0.05) * RATE);

/** THE BREATH NOISE, where nought or more (BrassInstrument::Design::breathNoise), else the trombone's own:
air=SHARE. */
static double AIR = -1.0;
/** THE PITCH PLAYED, hertz, where more than nought, in place of the note's own: pitch=recording plays it at
the recording's, which for the low Iowa notes can be 30 cents sharp, so that it can be heard beside it for its
tone alone. */
static double PITCH = -1.0;
/** THE WAVER: waver=BREATH,LIPS, the shares of BrassPlayer::Note::waverBreath and waverLips. */
static double WAVER_BREATH = 0.0, WAVER_LIPS = 0.0;
/** THE VOCAL TRACT: mouth=F1,S1,Q1,F2,S2,Q2,F3,S3,Q3, each formant's frequency, strength in MPa s/m^3 and
quality (BrassPlayer::Note::formantHz and the rest); none unless given. */
static double MOUTH[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

/** THE SETTINGS SEARCHED, each by its share of its range, nought to one. */
struct Range {
	const char* name;
	double lo, hi;
	bool logarithmic;
};
static Range RANGES[] = {
	{"tension", 0.8, 1.15, false},
	{"gap mm", -0.5, 0.5, false},
	{"damp", 0.25, 0.83, false},
	{"mass", 0.6, 2.5, true},
	{"mouth Pa", 300.0, 10000.0, true},
	{"rise s", 0.05, 1.0, true},
	{"floor", 0.8, 2.0, false},
	{"curve", 0.7, 4.0, true},
};
static const int DIMS = (int) (sizeof RANGES / sizeof RANGES[0]);

static double shareOf(int d, double v) {
	const Range& r = RANGES[d];
	return r.logarithmic ? std::log(v / r.lo) / std::log(r.hi / r.lo) : (v - r.lo) / (r.hi - r.lo);
}

static double valueOf(int d, double u) {
	const Range& r = RANGES[d];
	u = std::fmin(1.0, std::fmax(0.0, u));
	return r.logarithmic ? r.lo * std::pow(r.hi / r.lo, u) : r.lo + u * (r.hi - r.lo);
}

/** A WAV, mono or the first channel, 16 bits, as doubles; its rate into `rate`. */
static std::vector<double> readWav(const std::string& path, double& rate) {
	std::vector<double> x;
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return x;
	std::vector<uint8_t> b;
	uint8_t chunk[65536];
	size_t got;
	while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0)
		b.insert(b.end(), chunk, chunk + got);
	std::fclose(f);
	auto u16 = [&](size_t i) { return (uint32_t) b[i] | (uint32_t) b[i + 1] << 8; };
	auto u32 = [&](size_t i) { return u16(i) | u16(i + 2) << 16; };
	int channels = 1;
	for (size_t i = 12; i + 8 <= b.size();) {
		const uint32_t size = u32(i + 4);
		if (!std::memcmp(&b[i], "fmt ", 4)) {
			channels = (int) u16(i + 10);
			rate = (double) u32(i + 12);
		}
		if (!std::memcmp(&b[i], "data", 4))
			for (size_t j = i + 8; j + 1 < std::min(b.size(), (size_t) (i + 8 + size)); j += 2 * (size_t) channels)
				x.push_back((double) (int16_t) (b[j] | b[j + 1] << 8) / 32768.0);
		i += 8 + size + (size & 1);
	}
	return x;
}

static std::vector<double> resample(const std::vector<double>& x, double from, double to) {
	if (from == to)
		return x;
	const size_t n = (size_t) ((double) x.size() * to / from);
	const double cut = 0.95 * std::min(1.0, to / from);
	std::vector<double> y(n, 0.0);
	for (size_t i = 0; i < n; i++) {
		const double t = (double) i * from / to;
		const long c = (long) std::floor(t);
		double sum = 0.0;
		for (long k = c - 15; k <= c + 16; k++) {
			if (k < 0 || k >= (long) x.size())
				continue;
			const double d = t - (double) k;
			const double sinc = std::fabs(d) < 1e-9 ? cut : std::sin(M_PI * cut * d) / (M_PI * d);
			sum += x[(size_t) k] * sinc * (0.5 + 0.5 * std::cos(M_PI * d / 16.0));
		}
		y[i] = sum;
	}
	return y;
}

/** THE AMPLITUDE AT `hz` of `n` samples of `x` from `from`, Hann windowed. */
static double amplitudeAt(const std::vector<double>& x, size_t from, size_t n, double hz) {
	const double w = 2.0 * M_PI * hz / RATE;
	double re = 0.0, im = 0.0;
	const double cw = std::cos(w), sw = std::sin(w);
	double c = 1.0, s = 0.0;
	for (size_t i = 0; i < n && from + i < x.size(); i++) {
		const double win = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) n);
		const double v = x[from + i] * win;
		re += v * c;
		im -= v * s;
		const double c2 = c * cw - s * sw;
		s = s * cw + c * sw;
		c = c2;
	}
	return std::sqrt(re * re + im * im);
}

/** A RADIX-TWO TRANSFORM, in place. */
static void fft(std::vector<std::complex<double>>& a) {
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

/** HOW FAR FROM REPEATING, from `a` to `b`: YIN's difference at the period `T`, samples, as a share of the
power, over windows of three periods a period apart, their mean. Nought repeats exactly; one, not at all. */
static double aperiodicOf(const std::vector<double>& x, size_t a, size_t b, double T) {
	const size_t p = (size_t) std::lround(T), w = 3 * p;
	double sum = 0.0;
	int count = 0;
	for (size_t s = a; s + w + p < b && s + w + p < x.size(); s += p) {
		double d = 0.0, e = 0.0;
		for (size_t i = 0; i < w; i++) {
			const double u = x[s + i], v = x[s + p + i];
			d += (u - v) * (u - v);
			e += u * u + v * v;
		}
		sum += d / (e + 1e-30);
		count++;
	}
	return count ? sum / count : 1.0;
}

/** THE SOUND BETWEEN THE HARMONICS, from `a`, `n` samples: the power more than 8 per cent of the pitch from
any harmonic, from 30 Hz to 5 kHz, below the note too, against the power near them, dB. */
static double inharmonicOf(const std::vector<double>& x, size_t a, size_t n, double f0) {
	size_t m = 1;
	while (m < n)
		m <<= 1;
	std::vector<std::complex<double>> s(m, 0.0);
	for (size_t i = 0; i < n && a + i < x.size(); i++)
		s[i] = x[a + i] * (0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) n));
	fft(s);
	double near = 0.0, away = 0.0;
	const double bin = RATE / (double) m, width = std::max(0.08 * f0, 3.0 * RATE / (double) n);
	for (size_t i = 1; i < m / 2; i++) {
		const double f = (double) i * bin;
		if (f < 30.0 || f > 5000.0)
			continue;
		const double k = std::round(f / f0), p = std::norm(s[i]);
		if (k >= 1.0 && std::fabs(f - k * f0) < width)
			near += p;
		else
			away += p;
	}
	return 10.0 * std::log10((away + 1e-30) / (near + 1e-30));
}

/** WHERE A NOTE IS FIRST HEARD: the first 2 ms within 30 dB of its loudest 50 ms. */
static size_t onsetOf(const std::vector<double>& x) {
	auto rms = [&](size_t a, size_t b) {
		double s = 0.0;
		for (size_t i = a; i < b && i < x.size(); i++)
			s += x[i] * x[i];
		return std::sqrt(s / (double) std::max<size_t>(1, b - a));
	};
	const size_t w50 = (size_t) (0.05 * RATE), w2 = (size_t) (0.002 * RATE);
	double loud = 0.0;
	for (size_t i = 0; i + w50 < x.size(); i += w2)
		loud = std::max(loud, rms(i, i + w50));
	for (size_t i = 0; i + w2 < x.size(); i += w2)
		if (rms(i, i + w2) > loud * 0.0316)
			return i;
	return 0;
}

/** THE PITCH over 0.4 to 0.7 s from the onset, by the strongest sum of the first ten harmonics near `near`. */
static double pitchOf(const std::vector<double>& x, size_t onset, double near) {
	const size_t from = onset + (size_t) (0.4 * RATE), n = (size_t) (0.3 * RATE);
	double best = 0.0, f0 = near;
	for (double c = near * 0.95; c < near * 1.05; c += near * 0.0005) {
		double s = 0.0;
		for (int k = 1; k <= 10; k++)
			s += amplitudeAt(x, from, n, k * c);
		if (s > best) {
			best = s;
			f0 = c;
		}
	}
	return f0;
}

static const int HARMONICS = 20, FRAMES = 16, BANDS = 4;

static const int BAND_FIRST[BANDS + 1] = {1, 4, 9, 15, 21};

struct Features {
	bool valid = false;
	double f0 = 0.0;
	double held[HARMONICS] = {};
	double grows[FRAMES][BANDS] = {};
	double between = 0.0;
	/** Where it is first heard, seconds from the start of what was played; the sound between the harmonics
	held, dB; how unsteady the held tone is, the spread over 0.35 to 0.95 s of the level of each of the first
	three bands, dB; and how far from repeating its start is, its first 10 to 150 ms. */
	double onset = 0.0, inharmonic = 0.0, unsteady = 0.0, startAperiodic = 0.0;
	/** A note played for the search: its start's blip and blat, as the player's start judge measures them
	(BrassStart), dB. */
	double blip = 0.0, blat = 0.0;
	/** THE HELD TONE TO 10 kHz: each harmonic's amplitude against the strongest, dB, at most 40 of them. */
	double upTo[40] = {};
	int upToCount = 0;
};

/** WHAT A NOTE SOUNDS LIKE, from `x`; its held parts no later than `until` samples where that is more than
nought — a note played for the search ends there. */
static Features featuresOf(const std::vector<double>& x, double near, size_t until = 0) {
	Features f;
	const size_t end = until > 0 ? std::min(until, x.size()) : x.size();
	const size_t onset = onsetOf(x);
	if (onset + (size_t) (0.75 * RATE) > x.size())
		return f;
	f.f0 = pitchOf(x, onset, near);
	// The held tone: 0.4 to 0.7 s, as power, the 20 harmonics against all of them.
	const size_t heldFrom = onset + (size_t) (0.4 * RATE), heldN = (size_t) (0.3 * RATE);
	double power[HARMONICS], total = 0.0;
	for (int k = 0; k < HARMONICS; k++) {
		const double a = amplitudeAt(x, heldFrom, heldN, (k + 1) * f.f0);
		power[k] = a * a / ((double) heldN * heldN);
		total += power[k];
	}
	if (!(total > 0.0))
		return f;
	for (int k = 0; k < HARMONICS; k++)
		f.held[k] = 10.0 * std::log10(power[k] / total + 1e-12);
	// How it grows: 50 ms windows every 25 ms, the bands against the held tone's whole power.
	const size_t win = (size_t) (0.05 * RATE);
	for (int t = 0; t < FRAMES; t++) {
		const size_t from = onset + (size_t) (0.025 * RATE * t);
		for (int b = 0; b < BANDS; b++) {
			double p = 0.0;
			for (int k = BAND_FIRST[b]; k < BAND_FIRST[b + 1]; k++) {
				const double a = amplitudeAt(x, from, win, k * f.f0);
				p += a * a / ((double) win * win);
			}
			f.grows[t][b] = 10.0 * std::log10(p / total + 1e-12);
		}
	}
	// How clean: between the first twelve harmonics against them.
	double on = 0.0, off = 0.0;
	for (int k = 1; k <= 12; k++) {
		on += amplitudeAt(x, heldFrom, heldN, k * f.f0);
		off += amplitudeAt(x, heldFrom, heldN, (k + 0.5) * f.f0);
	}
	f.between = 20.0 * std::log10(off / on + 1e-12);
	{
		double strongest = 0.0;
		for (int k = 0; k < 40 && (k + 1) * f.f0 < 10000.0; k++) {
			f.upTo[k] = amplitudeAt(x, heldFrom, heldN, (k + 1) * f.f0);
			strongest = std::max(strongest, f.upTo[k]);
			f.upToCount = k + 1;
		}
		for (int k = 0; k < f.upToCount; k++)
			f.upTo[k] = 20.0 * std::log10(f.upTo[k] / (strongest + 1e-30) + 1e-12);
	}
	f.onset = (double) onset / RATE;
	const size_t heldA = std::min(onset + (size_t) (0.35 * RATE), end - std::min(end, (size_t) (0.2 * RATE)));
	const size_t heldB = std::min(onset + (size_t) (0.95 * RATE), end);
	f.inharmonic = inharmonicOf(x, heldA, heldB - heldA, f.f0);
	f.startAperiodic = aperiodicOf(x, onset + (size_t) (0.01 * RATE), onset + (size_t) (0.15 * RATE), RATE / f.f0);
	{
		const size_t w = (size_t) (0.05 * RATE);
		std::vector<double> level[3];
		for (size_t from = heldA; from + w < heldB; from += (size_t) (0.025 * RATE))
			for (int b = 0; b < 3; b++) {
				double p = 0.0;
				for (int k = BAND_FIRST[b]; k < BAND_FIRST[b + 1]; k++) {
					const double a = amplitudeAt(x, from, w, k * f.f0);
					p += a * a;
				}
				level[b].push_back(10.0 * std::log10(p + 1e-30));
			}
		double spread = 0.0;
		for (int b = 0; b < 3; b++) {
			double mean = 0.0, sq = 0.0;
			for (double v : level[b])
				mean += v;
			mean /= std::max<size_t>(1, level[b].size());
			for (double v : level[b])
				sq += (v - mean) * (v - mean);
			spread += std::sqrt(sq / std::max<size_t>(1, level[b].size()));
		}
		f.unsteady = spread / 3.0;
	}
	if (until > 0) {
		const BrassStartShape shape = brassStartShape(x.data(), x.size(), (size_t) ((TONGUE_AT - 0.01) * RATE), RATE, near);
		f.blip = shape.blip;
		f.blat = shape.blat;
	}
	f.valid = true;
	return f;
}

struct Score {
	double total = 1e9, held = 0.0, grows = 0.0, clean = 0.0, cents = 0.0;
	/** Highs the recording does not have, dB, summed (see distance). */
	double highs = 0.0;
	/** Milliseconds after the tongue it is first heard; and what roughness costs: the sound between the
	harmonics held, an unsteady tone, a start that does not repeat. */
	double late = 0.0, rough = 0.0;
};


static Score distance(const Features& m, const Features& r, double hz) {
	Score s;
	if (!m.valid)
		return s;
	s.cents = 1200.0 * std::log2(m.f0 / hz);
	// ONLY WHAT THE RECORDING HOLDS OVER ITS OWN NOISE: the harmonics within 45 dB of its strongest and under
	// 10 kHz, and the bands that have one.
	double strongest = -1e9;
	for (int k = 0; k < HARMONICS; k++)
		strongest = std::max(strongest, r.held[k]);
	bool heard[HARMONICS];
	int count = 0;
	for (int k = 0; k < HARMONICS; k++) {
		heard[k] = r.held[k] > strongest - 45.0 && (k + 1) * r.f0 < 10000.0;
		if (heard[k]) {
			s.held += std::fmin(25.0, std::fabs(m.held[k] - r.held[k]));
			count++;
		}
	}
	s.held /= std::max(1, count);
	// THE HELD TONE ALONE, 0.4 to 0.7 s, keeps Chris's verdicts apart: his four keepers 1.4 to 1.7 dB, the
	// three he heard as nasal, hollow and thin 2.1 to 4.0. Compared stretch by stretch through the whole note
	// instead, two keepers fell further from their recordings than the hollow note, whose recordings change
	// tone as they go on.
	int cells = 0;
	for (int b = 0; b < BANDS; b++) {
		bool any = false;
		for (int k = BAND_FIRST[b]; k < BAND_FIRST[b + 1]; k++)
			any = any || heard[k - 1];
		if (!any)
			continue;
		for (int t = 0; t < FRAMES; t++) {
			s.grows += std::fmin(15.0, std::fabs(m.grows[t][b] - r.grows[t][b]));
			cells++;
		}
	}
	s.grows /= std::max(1, cells);
	// ROUGH, as Chris heard it in the pilot's eighteen (make brasssearch ARGS="... try=..." against his verdicts):
	// sound between the harmonics held more than 3 dB over the recording's, half a point a decibel; the held
	// tone's bands wandering more than a decibel more than the recording's, a point a decibel; and a start
	// further from repeating than the recording's by more than 0.3 of its power, fifteen points for each whole
	// share. The three he called rough each lost five to ten points; none of the thirteen he called excellent
	// lost more than 0.8. The slight break at a start and the wah partway through, both in notes heard more
	// than 300 ms after the tongue, it does not see.
	s.clean = std::fmax(0.0, m.inharmonic - r.inharmonic - 3.0);
	// And a blip or a blat at the start past what nine in ten of the recordings' starts have (BrassStart: 1.3 and
	// 5.3 dB), two points and one a decibel: Chris heard a small blat on B flat 3 and F4 at mf that nothing
	// else here saw.
	s.rough = 0.5 * s.clean + std::fmax(0.0, m.unsteady - r.unsteady - 1.0) + 15.0 * std::fmax(0.0, m.startAperiodic - r.startAperiodic - 0.3)
		+ 2.0 * std::fmax(0.0, m.blip - 1.3) + std::fmax(0.0, m.blat - 5.3);
	// HIGHS THE RECORDING DOES NOT HAVE: each harmonic to 10 kHz louder than 55 dB under the strongest and more
	// than 3 dB over the recording's, by how much, summed; a tenth of a point a decibel. The held tone above
	// leaves out what the recording holds under its noise, and Chris heard an F4 with more highs than its
	// recording, 5 to 7 kHz, as hollow. One keeper has such highs too (B flat 2 at mf, 16 points); he judged
	// one note not to make them right on others.
	for (int k = 0; k < std::min(m.upToCount, r.upToCount); k++)
		s.highs += std::fmax(0.0, m.upTo[k] - std::fmax(r.upTo[k] + 3.0, -55.0));
	// LATE: how long after the tongue it is first heard. A note that speaks late but well is not a fault — low
	// brass speaks late, and its players play ahead of the beat; the player reads ahead and can start it as
	// much sooner, so the table keeps it — but a hundredth of a point a millisecond past 40 prefers the prompter
	// of two alike, and past half a second is too far to start ahead.
	s.late = 1000.0 * (m.onset - TONGUE_AT);
	const double late = 0.01 * std::fmax(0.0, s.late - 40.0) + (s.late > 500.0 ? 20.0 : 0.0);
	// In tune: a third of a point for each cent past five, and far away past 25. The build-up counts a third as
	// much as the held tone: Chris heard soft notes as excellent that it put far from their recordings.
	s.total = s.held + 0.35 * s.grows + s.rough + 0.1 * s.highs + late + 0.3 * std::fmax(0.0, std::fabs(s.cents) - 5.0) + (std::fabs(s.cents) > 25.0 ? 20.0 : 0.0);
	return s;
}

struct Trial {
	double u[DIMS] = {};
	Score score;
	double slide = 0.0;
};

/** ONE SETTING PLAYED: the slide put in tune for it, then the note, 1.6 s, from 0.3 s in. */
static std::vector<double> playSetting(const Trial& t, int midi, double level, double* slideOut) {
	BrassInstrument::Design design;
	design.rate = RATE;
	BrassPlayer::Note n;
	n.hz = PITCH > 0.0 ? PITCH : 440.0 * std::pow(2.0, (midi - 69) / 12.0);
	n.level = level;
	n.seconds = 1.6;
	n.tunedLips = valueOf(0, t.u[0]);
	n.hasGap = true;
	n.gap = 1e-3 * valueOf(1, t.u[1]);
	n.quality = 1.0 / valueOf(2, t.u[2]);
	n.mass = valueOf(3, t.u[3]);
	n.tunedMouth = valueOf(4, t.u[4]);
	n.rise = valueOf(5, t.u[5]);
	n.startFloor = valueOf(6, t.u[6]);
	n.riseCurve = valueOf(7, t.u[7]);
	n.air = AIR;
	n.waverBreath = WAVER_BREATH;
	n.waverLips = WAVER_LIPS;
	for (int k = 0; k < 3; k++)
		if (MOUTH[3 * k + 1] > 0.0) {
			n.formantHz[k] = MOUTH[3 * k];
			n.formantStrength[k] = 1e6 * MOUTH[3 * k + 1];
			n.formantQuality[k] = MOUTH[3 * k + 2];
		}
	const BrassPlayer::Tuned tuned = BrassPlayer::tuneFor(design, tromboneTable(), BrassPlayer::Habits(), n, nullptr);
	if (tuned.mouth <= 0.0)
		return {};
	n.slideCents = tuned.slideCents;
	if (slideOut)
		*slideOut = tuned.slideCents;
	BrassPlayer p;
	p.setup(design, tromboneTable());
	p.add(n, (int64_t) (0.3 * RATE), false);
	std::vector<double> out((size_t) (2.1 * RATE));
	for (double& v : out)
		v = p.process();
	return out;
}

static void writeWav(const std::string& path, const std::vector<double>& x) {
	double peak = 1e-12;
	for (double v : x)
		peak = std::max(peak, std::fabs(v));
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	const uint32_t n = (uint32_t) x.size(), bytes = n * 2, r = (uint32_t) RATE;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(1); u32(r); u32(r * 2); u16(2); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (double v : x) {
		const int16_t s = (int16_t) std::lround(v * 0.891 * 32767.0 / peak);
		std::fwrite(&s, 2, 1, f);
	}
	std::fclose(f);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		std::printf("make brasssearch ARGS=\"MIDI DYN [minutes=M] [threads=T] [seed=S]\"\n");
		return 1;
	}
	const int midi = std::atoi(argv[1]);
	const std::string dyn = argv[2];
	const double level = dyn == "pp" ? 1.0 / 7.0 : dyn == "ff" ? 6.0 / 7.0 : 4.0 / 7.0;
	double minutes = 10.0;
	// MPX_THREADS, where it says, so that a run in the background leaves the computer usable.
	int threads = std::getenv("MPX_THREADS") ? std::max(1, std::atoi(std::getenv("MPX_THREADS"))) : std::max(1, (int) std::thread::hardware_concurrency() - 2);
	unsigned seed = 1;
	for (int a = 3; a < argc; a++) {
		if (!std::strncmp(argv[a], "minutes=", 8)) minutes = std::atof(argv[a] + 8);
		if (!std::strncmp(argv[a], "threads=", 8)) threads = std::atoi(argv[a] + 8);
		if (!std::strncmp(argv[a], "seed=", 5)) seed = (unsigned) std::atoi(argv[a] + 5);
		if (!std::strncmp(argv[a], "air=", 4)) AIR = std::atof(argv[a] + 4);
		if (!std::strncmp(argv[a], "mouth=", 6))
			std::sscanf(argv[a] + 6, "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", &MOUTH[0], &MOUTH[1], &MOUTH[2], &MOUTH[3], &MOUTH[4], &MOUTH[5], &MOUTH[6], &MOUTH[7], &MOUTH[8]);
		if (!std::strncmp(argv[a], "waver=", 6)) {
			WAVER_BREATH = std::atof(argv[a] + 6);
			const char* comma = std::strchr(argv[a], ',');
			WAVER_LIPS = comma ? std::atof(comma + 1) : 0.0;
		}
		// A RANGE WIDENED OR NARROWED: NAME=LO:HI, the name's first word.
		for (int d = 0; d < DIMS; d++) {
			const size_t k = std::strcspn(RANGES[d].name, " ");
			if (!std::strncmp(argv[a], RANGES[d].name, k) && argv[a][k] == '=' && std::strchr(argv[a], ':')) {
				RANGES[d].lo = std::atof(argv[a] + k + 1);
				RANGES[d].hi = std::atof(std::strchr(argv[a], ':') + 1);
			}
		}
	}
	double hz = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
	// THE RECORDING.
	const char* home = std::getenv("HOME");
	const std::string refPath = std::string(home ? home : "") + "/Library/Application Support/Rack2/DreamerMPX/trombone-reference/" + std::to_string(midi) + " " + dyn + ".wav";
	double refRate = 44100.0;
	std::vector<double> ref = readWav(refPath, refRate);
	if (ref.empty()) {
		std::printf("no recording at %s\n", refPath.c_str());
		return 1;
	}
	ref = resample(ref, refRate, RATE);
	const Features target = featuresOf(ref, hz);
	std::printf("the recording: %.2f Hz, %+.0f cents, between the harmonics %.1f dB\n", target.f0, 1200.0 * std::log2(target.f0 / hz), target.between);
	for (int a = 3; a < argc; a++)
		if (!std::strcmp(argv[a], "pitch=recording")) {
			PITCH = target.f0;
			hz = target.f0;
		}

	// ONE SETTING SCORED: try=TENSION,GAP,DAMP,MASS,MOUTH,RISE,FLOOR,CURVE, written as build/Search MIDI DYN,
	// tried.wav, or with name=NAME, tried NAME.wav.
	std::string triedName;
	for (int a = 3; a < argc; a++)
		if (!std::strncmp(argv[a], "name=", 5))
			triedName = std::string(" ") + (argv[a] + 5);
	for (int a = 3; a < argc; a++)
		if (!std::strncmp(argv[a], "try=", 4)) {
			Trial t;
			const char* at = argv[a] + 4;
			for (int d = 0; d < DIMS && at; d++) {
				t.u[d] = shareOf(d, std::atof(at));
				at = std::strchr(at, ',');
				at = at ? at + 1 : nullptr;
			}
			double slide = 0.0;
			const std::vector<double> out = playSetting(t, midi, level, &slide);
			const Score sc = out.empty() ? Score() : distance(featuresOf(out, hz, NOTE_ENDS), target, hz);
			const Features mf = out.empty() ? Features() : featuresOf(out, hz, NOTE_ENDS);
			std::printf("distance %.2f (held %.2f, grows %.2f, rough %.2f, highs %.1f, late %.0f ms, %+.0f cents; blip %.1f, blat %.1f dB), slide %+.0f cents\n",
				sc.total, sc.held, sc.grows, sc.rough, sc.highs, sc.late, sc.cents, mf.blip, mf.blat, slide);
			if (!out.empty())
				writeWav("build/Search " + std::to_string(midi) + " " + dyn + ", tried" + triedName + (AIR >= 0.0 ? " air " + std::to_string(AIR).substr(0, 4) : std::string())
					+ (WAVER_BREATH > 0.0 || WAVER_LIPS > 0.0 ? " waver " + std::to_string(WAVER_BREATH).substr(0, 5) + "," + std::to_string(WAVER_LIPS).substr(0, 6) : std::string()) + ".wav", out);
			return 0;
		}
	// FROM A NEIGHBOUR: from=TENSION,GAP,...,CURVE, as try= gives them — the first trials that setting and
	// small changes of it, so that the search starts where the note beside it ended.
	bool seeded = false;
	Trial seed0;
	for (int a = 3; a < argc; a++)
		if (!std::strncmp(argv[a], "from=", 5)) {
			const char* at = argv[a] + 5;
			for (int d = 0; d < DIMS && at; d++) {
				seed0.u[d] = std::fmin(1.0, std::fmax(0.0, shareOf(d, std::atof(at))));
				at = std::strchr(at, ',');
				at = at ? at + 1 : nullptr;
			}
			seeded = true;
		}
	// THE SEARCH.
	std::mutex lock;
	std::vector<Trial> all, best;
	std::mt19937 rng(seed);
	std::atomic<bool> stop{false};
	int generation = 0;
	auto propose = [&]() {
		// Main lock held. Random at first and one in five after; else one of the best sixteen, changed a little.
		Trial t;
		std::uniform_real_distribution<double> uni(0.0, 1.0);
		if (seeded && all.size() < 48) {
			std::normal_distribution<double> near(0.0, all.empty() ? 0.0 : 0.05);
			for (int d = 0; d < DIMS; d++)
				t.u[d] = std::fmin(1.0, std::fmax(0.0, seed0.u[d] + near(rng)));
			return t;
		}
		if (best.size() < 16 || all.size() < 120 || uni(rng) < 0.2) {
			for (int d = 0; d < DIMS; d++)
				t.u[d] = uni(rng);
			return t;
		}
		const Trial& from = best[(size_t) std::min<double>(15, std::floor(std::pow(uni(rng), 2.0) * 16.0))];
		const double sigma = std::fmax(0.02, 0.15 * std::pow(0.97, (double) generation));
		std::normal_distribution<double> step(0.0, sigma);
		for (int d = 0; d < DIMS; d++)
			t.u[d] = std::fmin(1.0, std::fmax(0.0, from.u[d] + step(rng)));
		return t;
	};
	// THE FILES' NAMES: every setting given after the note's, or, name=NAME, that alone.
	std::string tag;
	for (int a = 3; a < argc; a++)
		if (std::strncmp(argv[a], "minutes=", 8) && std::strncmp(argv[a], "threads=", 8))
			tag += std::string(" ") + argv[a];
	for (int a = 3; a < argc; a++)
		if (!std::strncmp(argv[a], "name=", 5))
			tag = std::string(" ") + (argv[a] + 5);
	const std::string tsv = "build/Search " + std::to_string(midi) + " " + dyn + tag + ".tsv";
	FILE* log = std::fopen(tsv.c_str(), "w");
	for (int d = 0; d < DIMS; d++)
		std::fprintf(log, "%s\t", RANGES[d].name);
	std::fprintf(log, "slide cents\tcents off\theld dB\tgrows dB\trough\tlate ms\tdistance\n");
	const auto t0 = std::chrono::steady_clock::now();
	auto elapsed = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
	auto work = [&]() {
		while (!stop.load()) {
			Trial t;
			{
				std::lock_guard<std::mutex> hold(lock);
				t = propose();
			}
			double slide = 0.0;
			const std::vector<double> out = playSetting(t, midi, level, &slide);
			t.slide = slide;
			t.score = out.empty() ? Score() : distance(featuresOf(out, hz, NOTE_ENDS), target, hz);
			std::lock_guard<std::mutex> hold(lock);
			all.push_back(t);
			for (int d = 0; d < DIMS; d++)
				std::fprintf(log, "%.4g\t", valueOf(d, t.u[d]));
			std::fprintf(log, "%.0f\t%.1f\t%.2f\t%.2f\t%.2f\t%.0f\t%.3f\n", t.slide, t.score.cents, t.score.held, t.score.grows, t.score.rough, t.score.late, t.score.total);
			best.push_back(t);
			std::sort(best.begin(), best.end(), [](const Trial& a, const Trial& b) { return a.score.total < b.score.total; });
			if (best.size() > 16)
				best.resize(16);
			if (all.size() % 64 == 0)
				generation++;
			if (elapsed() > 60.0 * minutes)
				stop.store(true);
		}
	};
	std::vector<std::thread> pool;
	for (int i = 0; i < threads; i++)
		pool.emplace_back(work);
	double reported = 0.0;
	while (!stop.load()) {
		std::this_thread::sleep_for(std::chrono::seconds(5));
		if (elapsed() - reported < 60.0)
			continue;
		reported = elapsed();
		std::lock_guard<std::mutex> hold(lock);
		std::fflush(log);
		if (!best.empty())
			std::printf("%.0f min, %zu trials, best %.2f (held %.2f, grows %.2f, rough %.2f, late %.0f ms)\n", reported / 60.0, all.size(), best[0].score.total,
				best[0].score.held, best[0].score.grows, best[0].score.rough, best[0].score.late);
		std::fflush(stdout);
	}
	for (std::thread& t : pool)
		t.join();
	std::fclose(log);

	// THE BEST FIVE, printed and written.
	std::printf("\n%zu trials in %.1f minutes. The best:\n", all.size(), elapsed() / 60.0);
	for (size_t i = 0; i < best.size() && i < 5; i++) {
		const Trial& t = best[i];
		std::printf("  %zu: distance %.2f (held %.2f, grows %.2f, rough %.2f, highs %.1f, late %.0f ms, %+.0f cents):", i + 1, t.score.total,
			t.score.held, t.score.grows, t.score.rough, t.score.highs, t.score.late, t.score.cents);
		for (int d = 0; d < DIMS; d++)
			std::printf(" %s %.3g", RANGES[d].name, valueOf(d, t.u[d]));
		std::printf(", slide %+.0f cents\n", t.slide);
		const std::vector<double> out = playSetting(t, midi, level, nullptr);
		if (!out.empty())
			writeWav("build/Search " + std::to_string(midi) + " " + dyn + tag + ", best " + std::to_string(i + 1) + ".wav", out);
	}
	return 0;
}
