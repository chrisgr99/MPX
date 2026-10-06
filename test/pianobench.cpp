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
#include "../src/PianoNote.hpp"
#include "../src/PianoString.hpp"

#include <algorithm>
#include <ctime>
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

/** B FITTED THE WAY THE MODEL DEFINES IT: against the string's FIRST PARTIAL rather than against
the hypothetical B-less fundamental, since the first partial is what is tuned and what is heard.
The nth then sits at n times the first times the square root of (1 + B n squared) over (1 + B), so
what least squares can fit linearly is u = B / (1 + B) against n squared minus one. */
static double stretchRelative(const std::vector<Partial>& ps) {
	if (ps.size() < 2)
		return 0.0;
	const double first = ps[0].hz;
	double strongest = 0.0;
	for (const Partial& p : ps)
		strongest = std::max(strongest, p.level);
	double top = 0.0, bottom = 0.0;
	for (const Partial& p : ps) {
		if (p.n < 2 || p.level < strongest * 0.001)
			continue;
		const double r = p.hz / ((double) p.n * first);
		const double y = r * r - 1.0;
		const double x = (double) p.n * (double) p.n - 1.0;
		top += x * y;
		bottom += x * x;
	}
	const double u = (bottom > 0.0) ? top / bottom : 0.0;
	return (u < 0.999) ? u / (1.0 - u) : 0.0;
}

static std::vector<Partial> partialsOf(const std::vector<float>& x, double sr, double f0,
		int want, size_t N = (size_t) 1u << 16) {
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
/** THE WINDOW MATTERS AS MUCH AS THE ARITHMETIC, and getting it wrong reads as a fault in the
string. Too short and the partials of a bass note are one smeared blob: the bottom A's partials are
27 Hz apart, a window of an eighth of a second resolves to 6 Hz, and every partial below about
sixty hertz then decays at whatever the fundamental is doing — which is exactly what the first run
of this measured, and it reported the second partial of A0 dying in 29.9 seconds when a flat loop
gain could only give it 15. Too LONG and the window averages over the decay it is meant to be
following, which is what made the top octave read as nothing at all. So the caller says how long,
and the caller works it out from the note. */
static double t60Of(const std::vector<float>& x, double sr, double hz, size_t N = (size_t) 1u << 13) {
	const size_t hop = N / 4;
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

/** A DOUBLE DECAY, MEASURED AS TWO RATES.

A coupled note does not fall at one rate: it falls quickly while its strings are in step and
driving the bridge, then much more slowly once they have drifted out of step and are barely moving
it. A straight line through the decibels is the wrong model and would report some average that is
neither rate, hiding the thing phase 3 exists to produce.

MEASURED OVER TWO STRETCHES OF LEVEL AND NOT BY LOOKING FOR THE BEND. Fitting two lines and putting
the bend wherever it fits best was tried first and it reported the second rate as the FASTER one on
every note — the opposite of the truth — because of what sits between the two rates. Where the
in-phase part of the note has decayed to the level of the out-of-step part, the two CANCEL, and the
note passes through a deep momentary null: at A3 the fundamental drops twenty decibels in one window
and comes back up again. A knee-finding fit lands in that hole every time.

So the two rates are measured where each one plainly owns the sound. The first is fitted from just
below the peak down to twenty-five decibels below it, which on every note is before the null; the
second from forty-five decibels down to seventy-five, which is after it. Where the two fitted lines
cross is reported as the bend, which is better defined than any point on the curve itself. */
static void twoRateDecay(const std::vector<float>& x, double sr, double hz, size_t N,
		double* firstT60, double* secondT60, double* crossSeconds) {
	*firstT60 = *secondT60 = *crossSeconds = 0.0;
	const size_t hop = N / 4;
	std::vector<double> t, db;
	for (size_t at = 0; at + N < x.size(); at += hop) {
		const std::vector<double> mag = spectrum(x.data() + at, N, N);
		const size_t bin = (size_t) std::lround(hz * (double) N / sr);
		if (bin < 2 || bin + 2 >= mag.size())
			break;
		double m = 0.0;
		for (size_t b = bin - 2; b <= bin + 2; b++)
			m = std::max(m, mag[b]);
		if (m <= 0.0)
			continue;
		t.push_back(((double) at + N * 0.5) / sr);
		db.push_back(20.0 * std::log10(m));
	}
	if (db.size() < 8)
		return;
	double peak = -1e9;
	size_t from = 0;
	for (size_t i = 0; i < db.size(); i++) {
		if (db[i] > peak) {
			peak = db[i];
			from = i;
		}
	}
	// A straight line through whatever lies in a window of level, and its intercept with it.
	auto fitBetween = [&](double upper, double lower, double* slope, double* at) {
		double sx = 0, sy = 0, sxy = 0, sxx = 0;
		int n = 0;
		for (size_t i = from; i < db.size(); i++) {
			if (db[i] > peak - upper || db[i] < peak - lower)
				continue;
			sx += t[i]; sy += db[i]; sxy += t[i] * db[i]; sxx += t[i] * t[i];
			n++;
		}
		const double den = (double) n * sxx - sx * sx;
		if (n < 3 || std::fabs(den) < 1e-18)
			return false;
		*slope = ((double) n * sxy - sx * sy) / den;
		*at = (sy - *slope * sx) / (double) n;
		return true;
	};
	double s1 = 0, c1 = 0, s2 = 0, c2 = 0;
	const bool gotFirst = fitBetween(3.0, 25.0, &s1, &c1);
	const bool gotSecond = fitBetween(45.0, 75.0, &s2, &c2);
	if (gotFirst && s1 < -1e-9)
		*firstT60 = -60.0 / s1;
	if (gotSecond && s2 < -1e-9)
		*secondT60 = -60.0 / s2;
	if (gotFirst && gotSecond && std::fabs(s1 - s2) > 1e-9)
		*crossSeconds = (c2 - c1) / (s1 - s2);
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

	// ---- PHASE 1: the piano's own string ---------------------------------------------------------
	//
	// EVERY NUMBER HERE IS A VERDICT. The string is told nothing but which note it is, and the
	// bench asks it, by listening, whether it did what the compass says that note should do.
	std::printf("\n");
	std::printf("================================================================================\n");
	std::printf("PHASE 1 — PianoString, told nothing but its note\n");
	std::printf("================================================================================\n");

	// A LONG LOOK AND A FINE ONE. The bottom A is 27.5 Hz, and a transform short enough to be quick
	// puts three-quarters of a semitone in one bin down there, which cannot measure a stretch of a
	// hundredth of a per cent. Six seconds through a quarter-million-point transform puts the bins
	// a fifth of a hertz apart, and the parabola between them does the rest.
	const int notesOf[8] = {21, 33, 45, 57, 69, 81, 93, 108};

	// THE WINDOW, PER NOTE: long enough to put at least eight bins between one partial and the
	// next, short enough not to average over the decay being measured. The bottom A needs both
	// ends of that and the top needs neither, so it is worked out rather than fixed.
	auto windowFor = [&](int midi, double fraction) {
		const double f0 = hzOf(midi);
		const double t60 = pianoDecaySeconds((float) midi);
		size_t least = 1u << 10;
		while ((double) least < 8.0 * SR / f0 && least < (1u << 19))
			least <<= 1;
		size_t most = 1u << 10;
		while ((double) (most << 1) <= fraction * t60 * SR && most < (1u << 19))
			most <<= 1;
		return std::max(least, most);
	};

	std::printf("\nSTRETCH: what the compass asks of each note against what the string does.\n");
	std::printf("%-5s %9s %10s %10s %8s %7s %7s %8s\n", "note", "asked B", "measured B",
		"error %", "stages", "coeff", "worst c", "pitch c");
	for (int k = 0; k < 8; k++) {
		const int midi = notesOf[k];
		// ONE STRING AND A RIGID BRIDGE, because what phase 1 fitted is the string's own losses
		// and its own stretch. The bridge's share of the decay is phase 3's business and is
		// measured there; left in here it would be measuring two things at once and blaming
		// either for the other.
		PianoNote s;
		s.setSampleRate((float) SR);
		s.setNote((float) midi);
		s.setStringCount(1);
		s.setUnisonSpread(0.f);
		s.setBridgeImpedance(100000.f);
		s.hit(3.f);
		// THE PARTIALS ARE MEASURED FROM THE ATTACK, over a window that is a third of the decay
		// at most: beyond that the upper partials have gone and the transform is measuring
		// silence, which is why the top note read as having no stretch at all.
		const size_t N = windowFor(midi, 0.34);
		std::vector<float> x(N + (size_t) (0.05 * SR));
		for (size_t i = 0; i < x.size(); i++)
			x[i] = s.process();
		const std::vector<Partial> ps = partialsOf(x, SR, hzOf(midi), 24, N);
		const double asked = pianoInharmonicity((float) midi);
		const double got = stretchRelative(ps);
		const double pitch = ps.empty() ? 0.0
			: 1200.0 * std::log2(ps[0].hz / hzOf(midi));
		std::printf("%-5s %9.2e %10.2e %+10.1f %8d %7.3f %7.2f %+8.2f\n",
			noteName(midi), asked, got,
			(asked > 0.0) ? 100.0 * (got - asked) / asked : 0.0,
			s.string(0).stages(), s.string(0).dispersion(), s.string(0).fitErrorCents(), pitch);
	}
	std::printf("  measured B is fitted to the first twenty-four partials of the sound itself.\n");
	std::printf("  worst c is what the string's own fit predicted its worst partial would be out\n");
	std::printf("  by, in cents; the two should agree, and when they do not, the loop is doing\n");
	std::printf("  something the design arithmetic does not know about.\n");
	std::printf("  pitch c is the first partial against the note asked for: tuning, not stretch.\n");

	std::printf("\nWHERE THE PARTIALS LAND, for A3 and A6, in cents from where a stiff string\n");
	std::printf("says they should be. This is the measurement the fit exists to make small.\n");
	for (int k = 0; k < 2; k++) {
		const int midi = (k == 0) ? 57 : 93;
		// ONE STRING AND A RIGID BRIDGE, because what phase 1 fitted is the string's own losses
		// and its own stretch. The bridge's share of the decay is phase 3's business and is
		// measured there; left in here it would be measuring two things at once and blaming
		// either for the other.
		PianoNote s;
		s.setSampleRate((float) SR);
		s.setNote((float) midi);
		s.setStringCount(1);
		s.setUnisonSpread(0.f);
		s.setBridgeImpedance(100000.f);
		s.hit(3.f);
		const size_t N = windowFor(midi, 0.34);
		std::vector<float> x(N + (size_t) (0.05 * SR));
		for (size_t i = 0; i < x.size(); i++)
			x[i] = s.process();
		const std::vector<Partial> ps = partialsOf(x, SR, hzOf(midi), 24, N);
		std::printf("  %-4s ", noteName(midi));
		if (ps.empty()) {
			std::printf("nothing found\n");
			continue;
		}
		const double B = s.string(0).inharmonicity();
		for (const Partial& p : ps) {
			if (p.n < 2)
				continue;
			const double ideal = ps[0].hz * p.n
				* std::sqrt((1.0 + B * p.n * p.n) / (1.0 + B));
			std::printf("%+.1f ", 1200.0 * std::log2(p.hz / ideal));
		}
		std::printf("\n");
	}
	std::printf("  partials 2 upward, left to right.\n");

	// ---- decay, which is the other half of phase 1 ------------------------------------------------
	//
	// THE SLOPE, NOT THE WHOLE FALL. A bass note takes half a minute to lose sixty decibels and
	// there is no need to wait for it: the decibels fall in a straight line against time, so four
	// seconds of the line says where it would reach.
	std::printf("\nDECAY: seconds to fall sixty decibels, per partial, against what was asked.\n");
	std::printf("%-5s %8s %9s %9s %9s %9s %8s %8s %7s\n", "note", "asked", "partial 1",
		"partial 2", "partial 4", "partial 8", "power", "reached", "fit %");
	for (int k = 0; k < 8; k++) {
		const int midi = notesOf[k];
		// ONE STRING AND A RIGID BRIDGE, because what phase 1 fitted is the string's own losses
		// and its own stretch. The bridge's share of the decay is phase 3's business and is
		// measured there; left in here it would be measuring two things at once and blaming
		// either for the other.
		PianoNote s;
		s.setSampleRate((float) SR);
		s.setNote((float) midi);
		s.setStringCount(1);
		s.setUnisonSpread(0.f);
		s.setBridgeImpedance(100000.f);
		s.hit(3.f);
		// LONG ENOUGH TO SEE A SLOPE. The bass hangs on for half a minute and there is no need to
		// wait for all of it, but four seconds of a thirty-second decay is eight decibels, which
		// is a slope measured off the end of a short lever.
		const size_t N = windowFor(midi, 0.08);
		const double seconds = std::min(10.0, std::max(1.0, 0.4 * pianoDecaySeconds((float) midi)));
		std::vector<float> x((size_t) (seconds * SR) + 2 * N);
		for (size_t i = 0; i < x.size(); i++)
			x[i] = s.process();
		const double f1 = s.string(0).frequency();
		const double B = s.string(0).inharmonicity();
		auto partialHz = [&](int n) {
			return f1 * n * std::sqrt((1.0 + B * n * n) / (1.0 + B));
		};
		const double t1 = t60Of(x, SR, f1, N);
		const double t2 = (partialHz(2) < SR * 0.45) ? t60Of(x, SR, partialHz(2), N) : 0.0;
		const double t4 = (partialHz(4) < SR * 0.45) ? t60Of(x, SR, partialHz(4), N) : 0.0;
		const double t8 = (partialHz(8) < SR * 0.45) ? t60Of(x, SR, partialHz(8), N) : 0.0;
		// The power measured from the sound: the straight line through the logarithm of each
		// partial's decay time against the logarithm of its frequency.
		double sx = 0, sy = 0, sxy = 0, sxx = 0;
		int used = 0;
		const double ts[4] = {t1, t2, t4, t8};
		const int ns[4] = {1, 2, 4, 8};
		for (int i = 0; i < 4; i++) {
			if (ts[i] <= 0.0)
				continue;
			const double x = std::log(partialHz(ns[i]) / f1), y = std::log(t1 / ts[i]);
			sx += x; sy += y; sxy += x * y; sxx += x * x;
			used++;
		}
		const double power = (used > 1 && used * sxx - sx * sx > 1e-12)
			? (used * sxy - sx * sy) / (used * sxx - sx * sx) : 0.0;
		std::printf("%-5s %8.1f %9.2f %9.2f %9.2f %9.2f %8.2f %8.2f %7.1f\n",
			noteName(midi), pianoDecaySeconds((float) midi), t1, t2, t4, t8,
			power, s.string(0).decayExponentReached(), s.string(0).decayFitError());
	}
	std::printf("  power is measured from the sound, reached is what the string says its own loop\n");
	std::printf("  gains come to, and 1.15 is what was asked.\n");
	std::printf("  NOUGHT is what costs nothing, not one: everything in the loop comes round once\n");
	std::printf("  per period of the FUNDAMENTAL, the delay line being one of those long, so a\n");
	std::printf("  loop gain that treats every frequency alike gives every partial the same decay\n");
	std::printf("  and the whole of this power law is the loss filter's work. Measuring that was\n");
	std::printf("  what found it: the model claimed 1.18 while the sound said 0.04.\n");
	std::printf("  fit %% is the worst partial's decay time against the law, as a percentage. It is\n");
	std::printf("  what three first-order shelves can hold a power law to; the form is argued, with\n");
	std::printf("  the measurements of the three forms that failed, in src/PianoString.cpp.\n");

	// ---- PHASE 2: the hammer ----------------------------------------------------------------------
	std::printf("\n");
	std::printf("================================================================================\n");
	std::printf("PHASE 2 — the felt hammer\n");
	std::printf("================================================================================\n");

	// THE MEASUREMENT PHASE 2 LIVES OR DIES BY, and the one phase 0 set up to catch. A fixed
	// excitation scaled by how hard it was hit has a spectral centroid that does not move; a felt
	// hammer's must climb, because the felt stiffens as it compresses, so the contact shortens and
	// what goes into the string is a briefer and brighter pulse.
	std::printf("\nBRIGHTNESS AND CONTACT AGAINST BLOW SPEED. Hit harder and a real hammer stays\n");
	std::printf("on the string for LESS time, not more, and the sound comes out brighter.\n");
	std::printf("%-5s %8s %10s %9s %10s %9s\n", "note", "m/s", "contact ms", "peak N",
		"centroid", "peak out");
	const int hammerNotes[4] = {33, 57, 69, 93};
	const float speeds[5] = {0.2f, 0.6f, 1.5f, 3.f, 6.f};
	for (int k = 0; k < 4; k++) {
		for (int j = 0; j < 5; j++) {
			PianoNote s;
			s.setSampleRate((float) SR);
			s.setNote((float) hammerNotes[k]);
			s.hit(speeds[j]);
			std::vector<float> x((size_t) (0.6 * SR));
			double force = 0.0, peak = 0.0;
			for (size_t i = 0; i < x.size(); i++) {
				x[i] = s.process();
				force = std::max(force, (double) s.hammerForce());
				peak = std::max(peak, (double) std::fabs(x[i]));
			}
			std::printf("%-5s %8.2f %10.3f %9.2f %10.1f %9.4f\n",
				noteName(hammerNotes[k]), speeds[j], s.contactMilliseconds(), force,
				centroidOf(x, SR, 0.01, 0.1), peak);
		}
	}
	std::printf("  A real hammer is on the string for about four milliseconds in the bass and\n");
	std::printf("  about one at the top, falling by something like a factor of two from the\n");
	std::printf("  quietest blow to the loudest, with peak forces from a newton or two up to a\n");
	std::printf("  hundred. The felt stiffness anchors were set by this table and nothing else:\n");
	std::printf("  their units depend on the felt's power, so the numbers cannot be compared\n");
	std::printf("  with anything, while the contact times can.\n");
	std::printf("  THE CENTROID IS THE ONE THAT MATTERS. Compare it with the plucked string\n");
	std::printf("  measured above, whose centroid is 571 Hz at every level: a fixed excitation\n");
	std::printf("  scaled by how hard it was hit cannot brighten, and a felt hammer must.\n");
	std::printf("  In the bass the contact barely shortens with force, and that is not a\n");
	std::printf("  shortcoming: a single heavy hammer on a single slow string stays on until the\n");
	std::printf("  string's own reflection returns from the agraffe and throws it off, which for\n");
	std::printf("  the bottom A is four and a half milliseconds after the blow however hard it\n");
	std::printf("  was struck. The brightening is there regardless, from the felt.\n");

	// ---- the strike point, now a consequence rather than an arrangement --------------------------
	//
	// A MODE WITH A NODE AT THE STRIKE POINT CANNOT BE MOVED BY A FORCE APPLIED THERE. Phase 1's
	// placeholder faked this with the corner of a triangle; now the force enters the string at a
	// junction, and the nulls are simply what that junction cannot excite.
	std::printf("\nTHE STRIKE POINT. A3 is struck an eighth of the way along, so the eighth\n");
	std::printf("partial and the sixteenth have a node under the hammer and should be absent.\n");
	std::printf("Decibels below the strongest partial.\n");
	{
		PianoNote s;
		s.setSampleRate((float) SR);
		s.setNote(57.f);
		s.setStringCount(1);
		s.setBridgeImpedance(100000.f);
		s.hit(3.f);
		std::vector<float> x((size_t) (2.0 * SR));
		for (size_t i = 0; i < x.size(); i++)
			x[i] = s.process();
		const std::vector<Partial> ps = partialsOf(x, SR, hzOf(57), 18);
		double strongest = 0.0;
		for (const Partial& p : ps)
			strongest = std::max(strongest, p.level);
		for (const Partial& p : ps)
			std::printf("  %2d %7.1f%s\n", p.n, 20.0 * std::log10(p.level / strongest),
				(p.n == 8 || p.n == 16) ? "   <- node under the hammer" : "");
		std::printf("  the split is %d samples of %0.1f, so the node is where it should be.\n",
			s.string(0).strikeSplit(), SR / s.string(0).frequency());
	}

	// ---- PHASE 3: the choir ------------------------------------------------------------------------
	std::printf("\n");
	std::printf("================================================================================\n");
	std::printf("PHASE 3 — the choir: two or three strings a note, coupled at a bridge\n");
	std::printf("================================================================================\n");

	std::printf("\nTHE DOUBLE DECAY, which is the measurement phase 3 exists to produce. A coupled\n");
	std::printf("note falls quickly while its strings are in step and driving the bridge, then\n");
	std::printf("much more slowly once they have drifted apart and are barely moving it. The\n");
	std::printf("fundamental's decibels are fitted with two lines and the bend put where it fits.\n");
	std::printf("%-5s %3s %7s %10s %10s %8s %9s\n", "note", "n", "spread", "first T60",
		"then T60", "ratio", "cross s");
	const int choirNotes[5] = {33, 45, 57, 69, 81};
	for (int k = 0; k < 5; k++) {
		const int midi = choirNotes[k];
		PianoNote many;
		many.setSampleRate((float) SR);
		many.setNote((float) midi);
		many.hit(2.f);
		const size_t N = windowFor(midi, 0.03);
		// LONG ENOUGH TO SEE THE SECOND RATE, which means long enough to get well past the null
		// where the two components cancel.
		const double seconds = std::min(20.0, std::max(4.0,
			1.2 * pianoDecaySeconds((float) midi)));
		std::vector<float> x((size_t) (seconds * SR) + 2 * N);
		for (size_t i = 0; i < x.size(); i++)
			x[i] = many.process();
		double first = 0, then = 0, knee = 0;
		twoRateDecay(x, SR, many.string(0).frequency(), N, &first, &then, &knee);
		std::printf("%-5s %3d %7.2f %10.2f %10.2f %8.2f %9.3f\n", noteName(midi),
			many.stringCount(), pianoUnisonSpread((float) midi), first, then,
			(then > 0.0 && first > 0.0) ? then / first : 0.0, knee);
	}
	std::printf("  A real grand falls two to five times faster at first than afterwards. The\n");
	std::printf("  string's own losses set the second rate — that is the curve phase 1 was fitted\n");
	std::printf("  to — and the bridge sets how much faster the first one is, so the effect is\n");
	std::printf("  strongest in the three-strung middle and weakest in the single-strung bass.\n");
	std::printf("  Between the two rates the note passes through a deep momentary null, where\n");
	std::printf("  what is left of the in-step part cancels against the out-of-step part. It is\n");
	std::printf("  twenty decibels at A3 and it is not an artefact: it is why a piano note seems\n");
	std::printf("  to breathe rather than simply fade.\n");
	std::printf("  THE SECOND RATE READS 15 TO 30 PER CENT LONGER than the string's own, and that\n");
	std::printf("  is the measurement rather than the model. A note's out-of-step components sit\n");
	std::printf("  at slightly different frequencies and beat against each other, and taking the\n");
	std::printf("  strongest bin of a few is reading an upper envelope, which follows the slowest\n");
	std::printf("  of them rather than their sum.\n");

	std::printf("\nAND WITH ONE STRING THERE IS NO SECOND RATE, because there is no out-of-step\n");
	std::printf("mode to hold anything back. The same notes, forced to a single string:\n");
	std::printf("%-5s %10s %10s %8s\n", "note", "first T60", "then T60", "ratio");
	for (int k = 0; k < 5; k++) {
		const int midi = choirNotes[k];
		PianoNote one;
		one.setSampleRate((float) SR);
		one.setNote((float) midi);
		one.setStringCount(1);
		one.hit(2.f);
		const size_t N = windowFor(midi, 0.03);
		const double seconds = std::min(20.0, std::max(4.0,
			1.2 * pianoDecaySeconds((float) midi)));
		std::vector<float> x((size_t) (seconds * SR) + 2 * N);
		for (size_t i = 0; i < x.size(); i++)
			x[i] = one.process();
		double first = 0, then = 0, knee = 0;
		twoRateDecay(x, SR, one.string(0).frequency(), N, &first, &then, &knee);
		std::printf("%-5s %10.2f %10.2f %8.2f\n", noteName(midi), first, then,
			(then > 0.0 && first > 0.0) ? then / first : 0.0);
	}

	std::printf("\nBEATING: two strings a given spread apart, measured from the note's own\n");
	std::printf("output, against what the arithmetic says two frequencies that far apart do.\n");
	std::printf("%-8s %12s %12s\n", "cents", "measured Hz", "expected Hz");
	for (int j = 0; j < 4; j++) {
		const double cents = 0.5 + 0.5 * j;
		PianoNote n;
		n.setSampleRate((float) SR);
		n.setNote(57.f);
		n.setStringCount(2);
		n.setUnisonSpread((float) cents);
		// A MEASUREMENT AND NOT A SOUND: a rigid bridge and a long decay, so the beat can be
		// counted over many cycles rather than inferred from the first half of one.
		n.setBridgeImpedance(100000.f);
		n.string(0).setDecaySeconds(300.f);
		n.string(1).setDecaySeconds(300.f);
		n.hit(2.f);
		std::vector<float> x((size_t) (40.0 * SR));
		for (size_t i = 0; i < x.size(); i++)
			x[i] = n.process();
		const double f = n.string(0).frequency();
		const double expect = f * (std::pow(2.0, cents / 1200.0) - 1.0);
		std::printf("%-8.2f %12.3f %12.3f\n", cents, beatRate(x, SR), expect);
	}

	// ---- nothing anywhere may grow -------------------------------------------------------------
	//
	// A REGRESSION GUARD AND NOT A MEASUREMENT. Every note struck as hard as anyone could, run for
	// long enough that a loop gain a hair above one would show: a thing worth re-checking after
	// every change to the loop, since an unstable string is silent until it is deafening.
	std::printf("\nSTABILITY: all eighty-eight notes, struck at twelve metres per second, run for\n");
	std::printf("twelve seconds. The last second must be quieter than the first.\n");
	{
		int faults = 0;
		double worst = 0.0;
		int worstNote = 0;
		for (int midi = 21; midi <= 108; midi++) {
			PianoNote s;
			s.setSampleRate((float) SR);
			s.setNote((float) midi);
			s.hit(12.f);
			double first = 0.0, last = 0.0;
			const int n = (int) (12.0 * SR);
			bool finite = true;
			for (int i = 0; i < n; i++) {
				const float y = s.process();
				if (!std::isfinite(y)) {
					std::printf("  %s: not a number at %.3f s\n", noteName(midi), i / SR);
					finite = false;
					faults++;
					break;
				}
				if (i < (int) SR)
					first = std::max(first, (double) std::fabs(y));
				if (i > n - (int) SR)
					last = std::max(last, (double) std::fabs(y));
			}
			if (!finite)
				continue;
			const double ratio = last / std::max(first, 1e-30);
			if (ratio > worst) {
				worst = ratio;
				worstNote = midi;
			}
			if (ratio > 1.0) {
				std::printf("  %s GREW: first %.3e, last %.3e\n", noteName(midi), first, last);
				faults++;
			}
		}
		std::printf("  %d faults. The worst ratio of last second to first is %.2e, at %s.\n",
			faults, worst, noteName(worstNote));
	}

	// ---- what a note costs ----------------------------------------------------------------------
	//
	// PHASE 4'S WHOLE PROBLEM, measured now while there is one note to measure. Eighty-eight of
	// these have to run at once with every damper up, and most of the filtering is the dispersion
	// chain, which is longest in the bass — where a note has one string rather than three.
	std::printf("\nWHAT A NOTE COSTS, as a share of one processor doing nothing else. This is\n");
	std::printf("phase 4's whole problem, measured while there is still one note to measure.\n");
	std::printf("%-5s %3s %8s %12s %12s\n", "note", "n", "stages", "x real time", "notes at once");
	double wholeBoard = 0.0;
	for (int midi = 21; midi <= 108; midi++) {
		PianoNote p;
		p.setSampleRate((float) SR);
		p.setNote((float) midi);
		p.hit(3.f);
		const int n = (int) (2.0 * SR);
		const clock_t began = std::clock();
		float sink = 0.f;
		for (int i = 0; i < n; i++)
			sink += p.process();
		const double took = (double) (std::clock() - began) / CLOCKS_PER_SEC;
		// The sum is kept only so that the compiler cannot decide the loop was pointless and
		// delete the thing being timed.
		if (!std::isfinite(sink))
			std::printf("  %s went to not-a-number.\n", noteName(midi));
		const double share = took / ((double) n / SR);
		wholeBoard += share;
		if (midi == 21 || midi == 45 || midi == 69 || midi == 93 || midi == 108)
			std::printf("%-5s %3d %8d %12.5f %12.0f\n", noteName(midi), p.stringCount(),
				p.string(0).stages(), share, 1.0 / share);
	}
	std::printf("  all eighty-eight ringing at once: %.2f processors.\n", wholeBoard);
	std::printf("  A piano with every damper up is the worst case and a rare one, but phase 4\n");
	std::printf("  has to survive it: the way out is that a string nobody has struck costs\n");
	std::printf("  nothing, so the gating has to be cheap and must not click when a string wakes.\n");

	std::printf("\nDone. Phase 4 is the whole board: eighty-eight notes at once, coupled, with a\n");
	std::printf("string whose energy is below hearing skipped so that silence is free.\n");
	return 0;
}
