/** THE MEASURING BENCH for the modelled piano. Phase 0 of docs/piano-model.md.

WHY THIS COMES FIRST. A fault in a string model is a sound that is merely odd, and odd cannot be
debugged by ear: a stretch coefficient half what it should be, a decay that is right at the
fundamental and wrong four partials up, a hammer whose contact does not shorten when it is hit
harder — all of those sound like "not quite a piano" and none of them points at itself. So the
means of measuring comes before the thing measured, and every later phase ends in a number from
here rather than an impression.

WHAT IT MEASURES

	PARTIALS AND STRETCH. A stiff string's partials are not whole multiples of the fundamental:
	the nth sits at n times the fundamental times the square root of one plus B n squared, where B
	is the inharmonicity coefficient. B is what makes a piano a piano rather than a harp, it is
	why pianos are tuned stretched, and it varies across the compass by more than an order of
	magnitude. The bench finds the partials and fits B to them.

	DECAY, PARTIAL BY PARTIAL. How long each takes to fall sixty decibels. A real string's upper
	partials die first, and a real PIANO note falls quickly at first and then hangs on — the
	double decay, which comes from two or three strings coupled at a bridge and is the measurement
	that will say whether phase three worked.

	BRIGHTNESS AGAINST FORCE. The spectral centroid of the attack. On a piano it rises with how
	hard the note is struck, because the felt stiffens as it compresses; on anything with an
	envelope instead of a hammer, it does not move. This is the measurement phase two lives or
	dies by.

	BEATING. The rate at which two mistuned unison strings go in and out of phase.

	make pianobench

It is driven here against GuitarString, which is the waveguide the piano's strings will be built
from, so that the bench is proven and the starting point is written down before anything changes.
*/
#include "../src/GuitarString.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using namespace px;


// ---- a small Fourier transform -----------------------------------------------------------------
//
// IN HERE RATHER THAN BORROWED, because the bench must not depend on anything the plugin links
// against: it has to build and run from a command line with two source files and no Rack.

static void fft(std::vector<std::complex<double> >& a) {
	const size_t n = a.size();
	if (n <= 1)
		return;
	for (size_t i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		const double ang = -2.0 * M_PI / (double) len;
		const std::complex<double> wl(std::cos(ang), std::sin(ang));
		for (size_t i = 0; i < n; i += len) {
			std::complex<double> w(1.0, 0.0);
			for (size_t k = 0; k < len / 2; k++) {
				const std::complex<double> u = a[i + k];
				const std::complex<double> v = a[i + k + len / 2] * w;
				a[i + k] = u + v;
				a[i + k + len / 2] = u - v;
				w *= wl;
			}
		}
	}
}

/** The magnitude spectrum of a stretch of signal, through a Hann window so that a partial sitting
between two bins does not smear across the whole of the rest. */
static std::vector<double> spectrum(const float* x, size_t n, size_t fftSize) {
	std::vector<std::complex<double> > a(fftSize, std::complex<double>(0.0, 0.0));
	const size_t take = std::min(n, fftSize);
	for (size_t i = 0; i < take; i++) {
		const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i / (double) (take - 1));
		a[i] = std::complex<double>((double) x[i] * w, 0.0);
	}
	fft(a);
	std::vector<double> mag(fftSize / 2);
	for (size_t i = 0; i < fftSize / 2; i++)
		mag[i] = std::abs(a[i]);
	return mag;
}

/** The true position of a peak, from the three bins around its top: a parabola through them,
which places a partial to a fraction of a bin and is what makes a stretch measurement possible at
all without an enormous transform. */
static double refine(const std::vector<double>& mag, size_t bin) {
	if (bin == 0 || bin + 1 >= mag.size())
		return (double) bin;
	const double a = mag[bin - 1], b = mag[bin], c = mag[bin + 1];
	const double d = a - 2.0 * b + c;
	if (std::fabs(d) < 1e-20)
		return (double) bin;
	return (double) bin + 0.5 * (a - c) / d;
}


// ---- what the bench reports ---------------------------------------------------------------------

struct Partial {
	int n = 0;
	double hz = 0.0;
	double level = 0.0;
};

/** THE PARTIALS, FOUND WHERE A STIFF STRING PUTS THEM.

A partial sits at n times the fundamental times the square root of one plus B n squared — never
flat of its whole multiple, only sharp of it, and increasingly so.

IN TWO PASSES, AND IT HAS TO BE. The window wide enough to catch the sixteenth partial of a stiff
string is wider than the gap between the second and third of a slack one, so a single pass locks
onto a neighbouring peak and reads the stretch as SMALLER the stiffer the string is. The low
partials barely move, so they are found first and a rough stretch fitted to them; that stretch
then says where the high ones are and they are found with a tight window. */
static bool peakNear(const std::vector<double>& mag, double binHz, double lo, double hi,
		Partial& out, int n) {
	size_t best = 0;
	double bestMag = 0.0;
	for (size_t b = (size_t) (lo / binHz); b <= (size_t) (hi / binHz) && b < mag.size(); b++) {
		if (mag[b] > bestMag) {
			bestMag = mag[b];
			best = b;
		}
	}
	if (bestMag <= 0.0)
		return false;
	out.n = n;
	out.hz = refine(mag, best) * binHz;
	out.level = bestMag;
	return true;
}

/** A stretch fitted to whatever partials are in hand, against the first of them. */
static double stretchOf(const std::vector<Partial>& ps) {
	if (ps.size() < 2)
		return 0.0;
	const double base = ps[0].hz;
	double top = 0.0, bottom = 0.0;
	double strongest = 0.0;
	for (const Partial& p : ps)
		strongest = std::max(strongest, p.level);
	for (const Partial& p : ps) {
		if (p.n < 2 || p.level < strongest * 0.001)
			continue;
		const double r = p.hz / ((double) p.n * base);
		const double y = r * r - 1.0;
		const double x = (double) p.n * (double) p.n;
		top += x * y;
		bottom += x * x;
	}
	return (bottom > 0.0) ? top / bottom : 0.0;
}

static std::vector<Partial> partialsOf(const std::vector<float>& x, double sr, double f0,
		int want) {
	const size_t N = 1u << 16;
	const std::vector<double> mag = spectrum(x.data(), x.size(), N);
	const double binHz = sr / (double) N;

	// PASS ONE: the first six, where even a very stiff string is stretched by under a per cent.
	std::vector<Partial> low;
	for (int n = 1; n <= std::min(6, want); n++) {
		const double nominal = f0 * n;
		if (nominal > sr * 0.45)
			break;
		Partial p;
		if (peakNear(mag, binHz, nominal * 0.990, nominal * 1.012, p, n))
			low.push_back(p);
	}
	if (low.empty())
		return low;

	// PASS TWO: everything, looked for where that stretch says it is.
	const double rough = std::max(0.0, stretchOf(low));
	const double base = low[0].hz;
	std::vector<Partial> out;
	for (int n = 1; n <= want; n++) {
		const double at = base * n * std::sqrt(1.0 + rough * (double) n * (double) n);
		if (at > sr * 0.45)
			break;
		Partial p;
		if (peakNear(mag, binHz, at * 0.994, at * 1.006, p, n))
			out.push_back(p);
	}
	return out;
}


/** THE INHARMONICITY COEFFICIENT, fitted to the partials found, against the string's OWN first
partial rather than the note it was asked for — a string a fiftieth of a per cent flat is still a
string whose partials are stretched by B, and measuring against the pitch it was ASKED for folds
that error into every partial and grows it with n. */
static double fitB(const std::vector<Partial>& ps, double, double* fittedF0) {
	if (ps.empty())
		return 0.0;
	if (fittedF0)
		*fittedF0 = ps[0].hz;
	return stretchOf(ps);
}


/** HOW LONG A PARTIAL TAKES TO FALL SIXTY DECIBELS, by following its level through the note in
short windows and fitting a straight line to the decibels against time. */
static double t60Of(const std::vector<float>& x, double sr, double hz) {
	const size_t N = 1u << 13;
	const size_t hop = N / 2;
	std::vector<double> t, db;
	for (size_t at = 0; at + N < x.size(); at += hop) {
		const std::vector<double> mag = spectrum(x.data() + at, N, N);
		const size_t bin = (size_t) std::lround(hz * (double) N / sr);
		if (bin == 0 || bin + 1 >= mag.size())
			break;
		const double m = std::max(mag[bin - 1], std::max(mag[bin], mag[bin + 1]));
		if (m <= 0.0)
			continue;
		t.push_back(((double) at + N * 0.5) / sr);
		db.push_back(20.0 * std::log10(m));
	}
	// FROM THE PEAK DOWNWARD, and only while it is still above the noise, so the attack and the
	// floor do not drag the line about.
	size_t from = 0;
	double peak = -1e9;
	for (size_t i = 0; i < db.size(); i++) {
		if (db[i] > peak) {
			peak = db[i];
			from = i;
		}
	}
	double sx = 0, sy = 0, sxy = 0, sxx = 0;
	int n = 0;
	for (size_t i = from; i < db.size(); i++) {
		if (db[i] < peak - 55.0)
			break;
		sx += t[i]; sy += db[i]; sxy += t[i] * db[i]; sxx += t[i] * t[i];
		n++;
	}
	if (n < 3)
		return 0.0;
	const double slope = ((double) n * sxy - sx * sy) / ((double) n * sxx - sx * sx);
	return (slope < -1e-9) ? -60.0 / slope : 0.0;
}

/** THE SPECTRAL CENTROID of a stretch: where the weight of the sound sits. Rises with force on a
real piano and does not on anything with an envelope instead of a hammer. */
static double centroidOf(const std::vector<float>& x, double sr, double from, double length) {
	const size_t N = 1u << 13;
	const size_t at = (size_t) (from * sr);
	if (at + N >= x.size())
		return 0.0;
	(void) length;
	const std::vector<double> mag = spectrum(x.data() + at, N, N);
	double num = 0.0, den = 0.0;
	for (size_t b = 1; b < mag.size(); b++) {
		const double f = (double) b * sr / (double) N;
		if (f > sr * 0.45)
			break;
		num += f * mag[b];
		den += mag[b];
	}
	return (den > 0.0) ? num / den : 0.0;
}

/** HOW FAST TWO MISTUNED STRINGS BEAT, from the envelope of their sum. */
static double beatRate(const std::vector<float>& x, double sr) {
	// The envelope, rectified and smoothed, then its own strongest slow frequency.
	std::vector<float> env(x.size());
	float low = 0.f;
	for (size_t i = 0; i < x.size(); i++) {
		const float a = std::fabs(x[i]);
		low += (a - low) * 0.002f;
		env[i] = low;
	}
	// Take the middle of the note, so the attack does not count as a beat. And take a LONG look:
	// a unison a cent apart at A3 beats about once every eight seconds, so four seconds of signal
	// cannot show even half a cycle of it and the answer came back as nothing.
	const size_t from = env.size() / 8;
	const size_t take = env.size() - from;
	double mean = 0.0;
	for (size_t i = 0; i < take; i++)
		mean += env[from + i];
	mean /= (double) take;
	std::vector<float> ac(take);
	for (size_t i = 0; i < take; i++)
		ac[i] = (float) (env[from + i] - mean);
	size_t N = 1u << 15;
	while (N < take)
		N <<= 1;
	const std::vector<double> mag = spectrum(ac.data(), take, N);
	size_t best = 0;
	double bestMag = 0.0;
	// FROM A FIFTIETH OF A HERTZ, since that is the range a piano's unisons live in.
	for (size_t b = (size_t) (0.02 * N / sr) + 1; b < (size_t) (8.0 * N / sr) && b < mag.size();
			b++) {
		if (mag[b] > bestMag) {
			bestMag = mag[b];
			best = b;
		}
	}
	return refine(mag, best) * sr / (double) N;
}


// ---- driving a string ---------------------------------------------------------------------------

static std::vector<float> play(GuitarString& s, double sr, double seconds, float level,
		float hardness, float position) {
	s.silence();
	s.pluck(level, hardness, position);
	std::vector<float> out((size_t) (seconds * sr));
	for (size_t i = 0; i < out.size(); i++)
		out[i] = s.process();
	return out;
}

static const char* noteName(int midi) {
	static const char* N[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
	static std::string s;
	s = std::string(N[((midi % 12) + 12) % 12]) + std::to_string(midi / 12 - 1);
	return s.c_str();
}

static double hzOf(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }


int main() {
	const double SR = 48000.0;

	std::printf("THE PIANO BENCH — phase 0 of docs/piano-model.md\n");
	std::printf("Driving GuitarString, which is the waveguide the piano's strings will be built\n");
	std::printf("from, so the starting point is written down before anything changes.\n\n");

	// ---- what the bench is aiming at ---------------------------------------------------------
	std::printf("WHAT A REAL GRAND DOES, as the targets each later phase is measured against.\n");
	std::printf("  Inharmonicity B: least near the middle and rising toward both ends — steeply\n");
	std::printf("    in the wound bass and again at the top, where the strings are very short.\n");
	std::printf("    Order of 1e-5 to 1e-4 in the midrange, approaching 1e-3 at the extremes.\n");
	std::printf("  Decay: the fundamental of a midrange note falling 60 dB in several seconds,\n");
	std::printf("    the upper partials in a fraction of that, and a double decay — a quick fall\n");
	std::printf("    into a long aftersound — once two or three strings are coupled.\n");
	std::printf("  Hammer contact: a few milliseconds at a whisper falling toward one at force,\n");
	std::printf("    and shorter in the treble than the bass.\n");
	std::printf("  Unison detuning: a fraction of a cent to about a cent, so the beating is slow.\n");
	std::printf("  THESE ARE SHAPES, NOT YET FIGURES. Phase 1 replaces each with a measured\n");
	std::printf("  number from the literature, per register, written into the design note.\n\n");

	// ---- partials and stretch across the compass -----------------------------------------------
	std::printf("PARTIALS AND STRETCH, at four stiffness settings, for A1, A3 and A5.\n");
	std::printf("%-6s %-8s %10s %10s %10s %10s\n", "note", "stiff", "f1 Hz", "f2/f1", "f8/f1",
		"fitted B");
	const int notes[3] = {33, 57, 81};
	const float stiffs[4] = {0.f, 0.25f, 0.6f, 1.f};
	for (int k = 0; k < 3; k++) {
		for (int j = 0; j < 4; j++) {
			GuitarString s;
			s.setSampleRate((float) SR);
			s.setFrequency((float) hzOf(notes[k]));
			s.setDecay(6.f);
			s.setDamping(0.25f);
			s.setStiffness(stiffs[j]);
			const std::vector<float> x = play(s, SR, 2.0, 0.9f, 0.5f, 0.12f);
			const std::vector<Partial> ps = partialsOf(x, SR, hzOf(notes[k]), 16);
			double f0 = 0.0;
			const double B = fitB(ps, hzOf(notes[k]), &f0);
			double r2 = 0.0, r8 = 0.0;
			for (const Partial& p : ps) {
				if (p.n == 2) r2 = p.hz / ps[0].hz;
				if (p.n == 8) r8 = p.hz / ps[0].hz;
			}
			std::printf("%-6s %-8.2f %10.2f %10.4f %10.4f %10.6f\n",
				noteName(notes[k]), stiffs[j], f0, r2, r8, B);
		}
	}

	// ---- decay, partial by partial --------------------------------------------------------------
	std::printf("\nDECAY, in seconds to fall 60 dB, for A3 at two damping settings.\n");
	std::printf("%-8s %10s %10s %10s %10s\n", "damping", "partial 1", "partial 2", "partial 4",
		"partial 8");
	for (int j = 0; j < 2; j++) {
		GuitarString s;
		s.setSampleRate((float) SR);
		s.setFrequency((float) hzOf(57));
		s.setDecay(6.f);
		s.setDamping(j == 0 ? 0.1f : 0.6f);
		s.setStiffness(0.3f);
		const std::vector<float> x = play(s, SR, 6.0, 0.9f, 0.5f, 0.12f);
		const double f0 = hzOf(57);
		std::printf("%-8.2f %10.2f %10.2f %10.2f %10.2f\n", j == 0 ? 0.1 : 0.6,
			t60Of(x, SR, f0), t60Of(x, SR, f0 * 2), t60Of(x, SR, f0 * 4), t60Of(x, SR, f0 * 8));
	}

	// ---- brightness against force ----------------------------------------------------------------
	//
	// THE MEASUREMENT PHASE TWO LIVES OR DIES BY. A plucked string is a fixed excitation scaled by
	// how hard it was hit, so its centroid should barely move; a felt hammer's should climb.
	std::printf("\nBRIGHTNESS AGAINST FORCE: the centroid of the attack, in hertz.\n");
	std::printf("%-8s %12s\n", "level", "centroid");
	for (int j = 0; j < 4; j++) {
		const float level = 0.15f + 0.28f * (float) j;
		GuitarString s;
		s.setSampleRate((float) SR);
		s.setFrequency((float) hzOf(57));
		s.setDecay(6.f);
		s.setDamping(0.25f);
		s.setStiffness(0.3f);
		const std::vector<float> x = play(s, SR, 1.0, level, 0.5f, 0.12f);
		std::printf("%-8.2f %12.1f\n", level, centroidOf(x, SR, 0.01, 0.1));
	}
	std::printf("  A plucked string's centroid hardly moves with level, which is the point: a\n");
	std::printf("  hammer's must, and this is the number that will say whether it does.\n");

	// ---- beating ------------------------------------------------------------------------------
	std::printf("\nBEATING between two strings a given number of cents apart.\n");
	std::printf("%-8s %12s %12s\n", "cents", "measured Hz", "expected Hz");
	for (int j = 0; j < 4; j++) {
		const double cents = 0.5 + 0.5 * j;
		GuitarString a, b;
		for (GuitarString* s : {&a, &b}) {
			s->setSampleRate((float) SR);
			// A MEASUREMENT, NOT A SOUND: the strings are given a decay far longer than a piano's
			// so that the beat can be counted rather than inferred from its first half-cycle.
			s->setDecay(300.f);
			s->setDamping(0.2f);
			s->setStiffness(0.3f);
		}
		const double f = hzOf(57);
		a.setFrequency((float) f);
		b.setFrequency((float) (f * std::pow(2.0, cents / 1200.0)));
		a.silence(); b.silence();
		a.pluck(0.9f, 0.5f, 0.12f);
		b.pluck(0.9f, 0.5f, 0.12f);
		std::vector<float> x((size_t) (40.0 * SR));
		for (size_t i = 0; i < x.size(); i++)
			x[i] = a.process() + b.process();
		const double expect = f * (std::pow(2.0, cents / 1200.0) - 1.0);
		std::printf("%-8.2f %12.3f %12.3f\n", cents, beatRate(x, SR), expect);
	}

	std::printf("\nThe bench works. Phase 1 replaces the string.\n");
	return 0;
}
