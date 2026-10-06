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
#include "../src/PianoBoard.hpp"
#include "../src/PianoNote.hpp"
#include "../src/PianoString.hpp"

#include <algorithm>
#include <ctime>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
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

AND THE CURVE BETWEEN THE TWO RATES IS NOT A BEND BUT A MESS, which two earlier attempts at this
both foundered on, each in its own way.

	LOOKING FOR THE BEND lands in the hole. Where the in-phase part has decayed to the level of
	the out-of-step part, the two CANCEL and the note drops twenty decibels and comes back within
	half a second. A two-line fit that puts its knee wherever it fits best puts it in that null
	every time, and reported the second rate as the FASTER one on every note.

	FITTING OVER FIXED STRETCHES OF LEVEL drags across the beat. Between the fast fall and the
	null the detuned strings come back into step for a moment and the level stops falling — it
	rises by a decibel and sits there for a second — and a line fitted from three decibels below
	the peak to twenty-five below runs straight through that plateau and comes out three times too
	shallow. This went unnoticed until phase 4 made the three strings of a note share one design,
	as they should, since before that their small differences blurred the beat into something a
	straight line could survive.

SO EACH RATE IS READ WHERE IT PLAINLY OWNS THE CURVE. The first is fitted from the peak up to the
moment the level first STOPS FALLING, which is the beat arriving and is exactly the end of the
stretch the in-phase mode owns. The second is fitted over the last third of what is left, long past
the null and the beat, where nothing is going on but the out-of-step modes dying. Where the two
fitted lines cross is reported as the bend, which is better defined than any point on the curve. */
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
	// Everything from the peak down to where it is lost in the floor.
	std::vector<double> ts, ds;
	for (size_t i = from; i < db.size(); i++) {
		if (db[i] < peak - 80.0)
			break;
		ts.push_back(t[i]);
		ds.push_back(db[i]);
	}
	if (ts.size() < 8)
		return;

	auto lineOver = [&](size_t a, size_t b, double* slope, double* at) {
		const size_t n = b - a;
		double sx = 0, sy = 0, sxy = 0, sxx = 0;
		for (size_t i = a; i < b; i++) {
			sx += ts[i]; sy += ds[i]; sxy += ts[i] * ds[i]; sxx += ts[i] * ts[i];
		}
		const double den = (double) n * sxx - sx * sx;
		if (n < 3 || std::fabs(den) < 1e-18)
			return false;
		*slope = ((double) n * sxy - sx * sy) / den;
		*at = (sy - *slope * sx) / (double) n;
		return true;
	};

	// THE FIRST RATE IS HOW LONG THE FIRST FIFTEEN DECIBELS TAKE, and no fit at all.
	//
	// Looking for the moment the level stops falling and fitting up to there is the obvious thing
	// and it is not robust: the turn is a decibel in a trace that has its own ripple, so on some
	// notes it fires on the ripple and on others never fires, and the same measurement reported
	// ratios from 1.2 to 3.8 on notes that behave identically. Fifteen decibels is always inside
	// the stretch the in-phase mode owns — the beat arrives at about eighteen on the worst of
	// them — and where a trace crosses a level is a far steadier thing to measure than where its
	// slope changes sign.
	double s1 = 0, c1 = 0, s2 = 0, c2 = 0;
	bool gotFirst = false;
	for (size_t i = 1; i < ts.size(); i++) {
		if (ds[i] <= peak - 15.0) {
			// Between this frame and the one before it, by a straight line.
			const double span = ds[i - 1] - ds[i];
			const double part = (span > 1e-9) ? (ds[i - 1] - (peak - 15.0)) / span : 0.0;
			const double when = ts[i - 1] + part * (ts[i] - ts[i - 1]);
			if (when > ts[0]) {
				s1 = -15.0 / (when - ts[0]);
				c1 = peak - s1 * ts[0];
				gotFirst = true;
			}
			break;
		}
	}
	// AND THE SECOND RATE THE SAME WAY, between two levels well down the tail. Fitting a line
	// through the last third of the trace was tried and is at the mercy of whatever that third
	// happens to contain: on A2 it landed somewhere with a second null in it and reported the
	// aftersound as FASTER than the same note's single-string decay, which is impossible. Where a
	// trace last passes a level is steady even when the trace is lumpy, which this one is: it has
	// a deep null in it and the beat going on either side.
	//
	// THE LAST CROSSING AND NOT THE FIRST, because the null and the beat can carry it back and
	// forth across a level several times, and what is wanted is the decay underneath all that.
	auto lastBelow = [&](double level) {
		double when = -1.0;
		for (size_t i = 1; i < ts.size(); i++) {
			if (ds[i - 1] > level && ds[i] <= level) {
				const double span = ds[i - 1] - ds[i];
				const double part = (span > 1e-9) ? (ds[i - 1] - level) / span : 0.0;
				when = ts[i - 1] + part * (ts[i] - ts[i - 1]);
			}
		}
		return when;
	};
	const double atFifty = lastBelow(peak - 50.0), atSeventy = lastBelow(peak - 70.0);
	bool gotSecond = false;
	if (atFifty > 0.0 && atSeventy > atFifty + 1e-6) {
		s2 = -20.0 / (atSeventy - atFifty);
		c2 = (peak - 50.0) - s2 * atFifty;
		gotSecond = true;
	}
	else {
		// Not enough of a tail to see: fall back on a line through the last third of it.
		const size_t late = (ts.size() >= 12) ? (2 * ts.size()) / 3 : 0;
		gotSecond = lineOver(late, ts.size(), &s2, &c2);
	}
	if (gotFirst && s1 < -1e-9)
		*firstT60 = -60.0 / s1;
	if (gotSecond && s2 < -1e-9)
		*secondT60 = -60.0 / s2;
	if (gotFirst && gotSecond && std::fabs(s1 - s2) > 1e-9)
		*crossSeconds = (c2 - c1) / (s1 - s2);
}


/** THE SPECTRAL CENTROID of a stretch:/** THE SPECTRAL CENTROID of a stretch: where the weight of the sound sits. Rises with force on a
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


/** WHICH PHASES TO RUN. The bench is five phases deep and takes minutes to go through all of
them, most of it spent building whole instruments and listening to them decay for twenty seconds.
Naming the phases on the command line runs only those:

	make pianobench            everything
	make pianobench ARGS=5     phase 5 alone
	make pianobench ARGS="1 2" phases 1 and 2

Phase 0's own measurements, of the plucked string this was all built from, come with phase 1, since
their only purpose now is to be the thing the piano is compared against. */
static bool runPhase[8] = {true, true, true, true, true, true, true, true};

static bool doing(int phase) {
	return runPhase[phase];
}

int main(int argc, char** argv) {
	const double SR = 48000.0;
	if (argc > 1) {
		for (int i = 0; i < 8; i++)
			runPhase[i] = false;
		for (int i = 1; i < argc; i++) {
			const int phase = std::atoi(argv[i]);
			if (phase >= 0 && phase < 8)
				runPhase[phase] = true;
		}
		runPhase[0] = runPhase[1];
	}

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


	if (doing(0)) {
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

	}

	if (doing(1)) {
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

	}

	if (doing(2)) {
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

	}

	if (doing(3)) {
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
		// LONG ENOUGH TO SEE THE SECOND RATE, which means long past the null where the two
		// components cancel AND far enough down the tail to measure the slow rate over a useful
		// span of it. Twenty seconds was not enough for A2, whose own strings ring for eighteen:
		// the tail had barely started and the aftersound read as faster than the same note's
		// single-string decay, which cannot happen.
		const double seconds = std::min(40.0, std::max(5.0,
			2.5 * pianoDecaySeconds((float) midi)));
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
	std::printf("  THE SECOND RATE IS THE SHAKIEST NUMBER IN THIS BENCH and is worth distrusting\n");
	std::printf("  by a factor of about two either way. It is read from a stretch of tail that\n");
	std::printf("  has a null in the middle of it and the beat going on either side, and the\n");
	std::printf("  out-of-step components sit at slightly different frequencies and beat against\n");
	std::printf("  each other as well, so reading the strongest bin of a few follows the slowest\n");
	std::printf("  of them rather than their sum. The control below is what says the method is\n");
	std::printf("  sound: forced to a single string, where there is only one rate to find, the\n");
	std::printf("  two measurements agree to the last digit on every note.\n");

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

	}

	if (doing(4)) {
	// ---- PHASE 4: the whole board ------------------------------------------------------------------
	std::printf("\n");
	std::printf("================================================================================\n");
	std::printf("PHASE 4 — the whole board: eighty-eight notes, one bridge\n");
	std::printf("================================================================================\n");

	{
		// BUILDING IT, timed, because every string's dispersion chain and loss filter is solved
		// for and there are two hundred and sixty-odd strings. This happens once and must never
		// happen on the audio thread.
		PianoBoard board;
		const clock_t began = std::clock();
		board.setSampleRate((float) SR);
		board.silence();
		const double built = (double) (std::clock() - began) / CLOCKS_PER_SEC;
		std::printf("\nBUILDING IT: %.3f seconds to solve every string in the instrument.\n",
			built);
		std::printf("  Once, when the sample rate is set. Phase 7 has to keep it off the audio\n");
		std::printf("  thread and out of the way of a module appearing in a rack.\n");
		std::printf("  The strings hang %.0f kg/s of impedance on the bridge between them, against\n",
			board.hangingImpedance());
		std::printf("  the soundboard's own two thousand: a quarter again as hard to move as the\n");
		std::printf("  soundboard alone, which is why a note's decay depends on the whole\n");
		std::printf("  instrument being there rather than only on its own three strings.\n");

		// ---- the gate: striking one note sets the others ringing ----
		//
		// NO RESONANCE CODE EXISTS. The strings were always there and the bridge was always
		// shared. What follows is what a piano does.
		std::printf("\nSYMPATHETIC RESONANCE. C3 is struck and nothing else is touched. Two\n");
		std::printf("seconds later, this is how much every note is moving, in decibels below what\n");
		std::printf("C3 itself was doing at the moment of the blow. The notes that answer should\n");
		std::printf("be the ones that share a partial with C3.\n");
		board.silence();
		board.hit(48, 3.f);
		// What the blow itself amounted to, so that everything can be measured against the thing
		// that was put in rather than against the struck note two seconds later — which has
		// decayed a long way by then and would make every answer look louder than it is.
		double blow = 0.0;
		for (int i = 0; i < (int) (0.3 * SR); i++) {
			board.process();
			blow = std::max(blow, (double) board.note(48).moving());
		}
		for (int i = 0; i < (int) (1.7 * SR); i++)
			board.process();
		struct Answer { int midi; double db; };
		std::vector<Answer> answers;
		for (int midi = 21; midi <= 108; midi++) {
			const double m = (double) board.note(midi).moving();
			if (m > 0.0)
				answers.push_back(Answer{midi, 20.0 * std::log10(m / blow)});
		}
		std::sort(answers.begin(), answers.end(),
			[](const Answer& a, const Answer& b) { return a.db > b.db; });
		std::printf("%-6s %8s %22s\n", "note", "dB", "why");
		for (size_t k = 0; k < answers.size() && k < 12; k++) {
			const int midi = answers[k].midi;
			std::string why;
			if (midi == 48)
				why = "struck";
			else {
				// WHERE THE TWO NOTES MEET, which is not always at one of them. Looking only for
				// a whole-number ratio between the two fundamentals explains C2 and C4 and
				// leaves F2, G2, F3 and G3 looking like noise — and they are not noise: F2's
				// third partial is C4, which is C3's second, and G2's fourth is G4, which is
				// C3's third. Two strings talk through ANY partial they have in common, so the
				// smallest pair of whole numbers that lines them up is what to look for.
				const double f = hzOf(midi), c = hzOf(48);
				int bestP = 0, bestQ = 0;
				for (int sum = 2; sum <= 14 && !bestP; sum++) {
					for (int p = 1; p < sum; p++) {
						const int q = sum - p;
						if (std::fabs(1200.0 * std::log2((f * p) / (c * q))) < 30.0) {
							bestP = p;
							bestQ = q;
							break;
						}
					}
				}
				if (!bestP)
					why = "-";
				else if (bestP == 1)
					why = "partial " + std::to_string(bestQ) + " of C3 is it";
				else if (bestQ == 1)
					why = "C3 is its partial " + std::to_string(bestP);
				else
					why = "its " + std::to_string(bestP) + " = C3's "
						+ std::to_string(bestQ);
			}
			std::printf("%-6s %8.1f %22s%s\n", noteName(midi), answers[k].db, why.c_str(),
				(midi == 67) ? "   <- the twelfth: the gate" : "");
		}
		std::printf("  C3 itself has fallen to %.1f dB by then, so the notes that answer are not\n",
			20.0 * std::log10(std::max((double) board.note(48).moving(), 1e-30) / blow));
		std::printf("  as close behind it as they look: two seconds in, the halo is a good deal\n");
		std::printf("  quieter than the note, and the note is a good deal quieter than the blow.\n");
		std::printf("  SOME ANSWER BECAUSE THEY SHARE A PARTIAL and some because they never stop.\n");
		std::printf("  The ones named above are being driven at a frequency they ring at — C3 is\n");
		std::printf("  the second partial of C2 as surely as C4 is C3's, which is the same\n");
		std::printf("  mechanism from either end. The unexplained ones are all in the top two\n");
		std::printf("  octaves, which have no dampers at all: they take whatever broadband\n");
		std::printf("  shaking the bridge gives them and nothing ever stops them again.\n");
		std::printf("  AND THE BASS ANSWERS LESS THAN IT DID BEFORE PHASE 6, which is the\n");
		std::printf("  soundboard's doing and is right. A board goes stiff below its own lowest\n");
		std::printf("  mode — there is nothing down there for a force to excite — so the bridge\n");
		std::printf("  will not be shifted at twenty-seven hertz however hard it is pushed, and a\n");
		std::printf("  string that cannot shift the bridge cannot hear through it either. With a\n");
		std::printf("  plain resistance in place of the board the bass hummed under everything,\n");
		std::printf("  because a plain resistance gives way just as readily at twenty-seven hertz\n");
		std::printf("  as at two thousand, which no piece of spruce does.\n");

		// AND THE CONTROL, which the measurement needs as much as the result: with the bridge
		// made rigid there is nothing for the strings to talk through, and nothing should answer.
		PianoBoard rigid;
		rigid.setSampleRate((float) SR);
		rigid.setBridgeImpedance(1e9f);
		rigid.silence();
		rigid.hit(48, 3.f);
		double rigidBlow = 0.0;
		for (int i = 0; i < (int) (0.3 * SR); i++) {
			rigid.process();
			rigidBlow = std::max(rigidBlow, (double) rigid.note(48).moving());
		}
		for (int i = 0; i < (int) (1.7 * SR); i++)
			rigid.process();
		double other = 0.0;
		for (int midi = 21; midi <= 108; midi++) {
			if (midi != 48)
				other = std::max(other, (double) rigid.note(midi).moving());
		}
		std::printf("  CONTROL, the same strike with the bridge made rigid: the loudest other\n");
		std::printf("  note reaches %.1f dB, against %.1f with a bridge that moves. Nothing\n",
			(other > 0.0) ? 20.0 * std::log10(other / rigidBlow) : -999.0,
			(answers.size() > 1) ? answers[1].db : 0.0);
		std::printf("  resonates through a bridge that cannot be shifted, and there is no\n");
		std::printf("  resonance code in the model for it to resonate with.\n");

		// ---- and the whole instrument changes every note's decay ----
		std::printf("\nAND A NOTE'S DECAY DEPENDS ON THE WHOLE INSTRUMENT BEING THERE. A3 by\n");
		std::printf("itself, and A3 on a board whose every other string is free to join in:\n");
		{
			const size_t N = windowFor(57, 0.03);
			std::vector<float> x((size_t) (30.0 * SR) + 2 * N);
			board.silence();
			// EVERY OTHER DAMPER OFF, which is the state phase 4 was written in — it had no
			// dampers, so nothing was holding any string still. Measured with them down instead,
			// the rest of the instrument can hardly absorb anything and the comparison says
			// nothing at all.
			board.setSustain(1.f);
			board.hit(57, 2.f);
			for (size_t i = 0; i < x.size(); i++)
				x[i] = board.process();
			double f1 = 0, t1 = 0, cross = 0;
			twoRateDecay(x, SR, board.note(57).string(0).frequency(), N, &f1, &t1, &cross);
			PianoNote alone;
			alone.setSampleRate((float) SR);
			alone.setNote(57.f);
			alone.hit(2.f);
			std::vector<float> y(x.size());
			for (size_t i = 0; i < y.size(); i++)
				y[i] = alone.process();
			double f2 = 0, t2 = 0, cross2 = 0;
			twoRateDecay(y, SR, alone.string(0).frequency(), N, &f2, &t2, &cross2);
			std::printf("  first T60 / then T60:  on the board %.2f / %.2f,  alone %.2f / %.2f\n",
				f1, t1, f2, t2);
			std::printf("  THE AFTERSOUND IS SEVERAL TIMES LONGER, and the first version of this\n");
			std::printf("  measurement said the opposite — that the board HALVED it, and that\n");
			std::printf("  sympathetic resonance was a cost the note paid. That was a fault in the\n");
			std::printf("  reading and not in the model: the old way fitted a straight line across\n");
			std::printf("  the beat plateau, which on a board full of ringing strings is a quite\n");
			std::printf("  different shape from the one a lone note makes.\n");
			std::printf("  WHAT ACTUALLY HAPPENS is that the instrument becomes a reservoir. A3\n");
			std::printf("  puts energy into every string that shares a partial with it, and those\n");
			std::printf("  strings put it back, so the note is fed from the room it is filling.\n");
			std::printf("  The initial fall is barely touched, because that is over before any of\n");
			std::printf("  this has had time to happen. It is what a pianist hears when the pedal\n");
			std::printf("  goes down, and phase 5 measures it against the pedal directly.\n");
		}

		// ---- what it costs to run ----
		std::printf("\nWHAT IT COSTS TO RUN, as a share of one processor.\n");
		struct Case { const char* what; int notes[10]; int n; };
		const Case cases[4] = {
			{"silence, nothing struck", {0}, 0},
			{"one note", {60}, 1},
			{"a four-note chord", {48, 55, 60, 64}, 4},
			{"every note at once", {0}, -1},
		};
		std::printf("%-26s %8s %14s %12s\n", "", "awake", "x real time", "headroom");
		for (int c = 0; c < 4; c++) {
			PianoBoard b;
			b.setSampleRate((float) SR);
			b.silence();
			if (cases[c].n < 0) {
				for (int midi = 21; midi <= 108; midi++)
					b.hit(midi, 3.f);
			}
			else {
				for (int k = 0; k < cases[c].n; k++)
					b.hit(cases[c].notes[k], 3.f);
			}
			// A moment to settle, so the timing is of the instrument ringing rather than of
			// eighty-eight hammers all in contact at once.
			for (int i = 0; i < (int) (0.3 * SR); i++)
				b.process();
			const int n = (int) (1.0 * SR);
			const clock_t began = std::clock();
			float sink = 0.f;
			for (int i = 0; i < n; i++)
				sink += b.process();
			const double took = (double) (std::clock() - began) / CLOCKS_PER_SEC;
			if (!std::isfinite(sink))
				std::printf("  that went to not-a-number.\n");
			const double share = took / ((double) n / SR);
			std::printf("%-26s %8d %14.4f %12.0f\n", cases[c].what, b.awake(), share,
				1.0 / std::max(share, 1e-9));
		}
		std::printf("  SILENCE IS FREE and nothing else is, which is worth being plain about.\n");
		std::printf("  Once anything at all is sounding the bridge is moving, so every string in\n");
		std::printf("  the instrument is being driven, and a string that is being driven has a\n");
		std::printf("  right to be heard — so they are all awake and one note costs what the whole\n");
		std::printf("  keyboard does. That is the resonance doing its job rather than the gating\n");
		std::printf("  failing to do its own. Phase 4's gate was that the instrument runs at a\n");
		std::printf("  sensible cost with every damper up, and half a processor for a piano is\n");
		std::printf("  sensible. If phase 7 wants it cheaper, the thing to do is not a higher\n");
		std::printf("  floor — the bridge is loud either way — but to check the sleeping notes a\n");
		std::printf("  few at a time on a rotation, so each is woken briefly and put back unless\n");
		std::printf("  it has something to say.\n");

		// ---- the board must not grow either ----
		std::printf("\nSTABILITY OF THE WHOLE BOARD: every note struck at twelve metres per\n");
		std::printf("second, run for twenty seconds. The last second must be quieter than the\n");
		std::printf("first, and with two hundred and sixty strings tied together there are far\n");
		std::printf("more ways for that to fail than with one.\n");
		PianoBoard loud;
		loud.setSampleRate((float) SR);
		loud.silence();
		for (int midi = 21; midi <= 108; midi++)
			loud.hit(midi, 12.f);
		double first = 0.0, last = 0.0;
		bool finite = true;
		const int n = (int) (20.0 * SR);
		for (int i = 0; i < n; i++) {
			const float y = loud.process();
			if (!std::isfinite(y)) {
				std::printf("  not a number at %.3f s\n", i / SR);
				finite = false;
				break;
			}
			if (i < (int) SR)
				first = std::max(first, (double) std::fabs(y));
			if (i > n - (int) SR)
				last = std::max(last, (double) std::fabs(y));
		}
		if (finite)
			std::printf("  peak %.1f in the first second, %.3e in the last: a ratio of %.2e.\n",
				first, last, last / std::max(first, 1e-30));
	}

	}

	if (doing(5)) {
	// ---- PHASE 5: dampers and pedals -----------------------------------------------------------
	std::printf("\n");
	std::printf("================================================================================\n");
	std::printf("PHASE 5 — dampers and pedals\n");
	std::printf("================================================================================\n");

	// How fast a note is dying over a stretch of time, as a time to fall sixty decibels.
	auto rateOver = [&](const std::vector<float>& x, double hz, size_t N, double from,
			double to) {
		std::vector<float> slice(x.begin() + (size_t) (from * SR),
			x.begin() + std::min((size_t) (to * SR), x.size()));
		return t60Of(slice, SR, hz, N);
	};

	// A RELEASED KEY AGAINST A HELD ONE, measured over the SAME stretch of the same note.
	//
	// AND THAT CONTROL IS THE WHOLE MEASUREMENT. Comparing the decay before the release with the
	// decay after it compares two different parts of a decay curve that already has two rates in
	// it, and reports the double decay of phase 3 as though the damper had done it: with the pedal
	// fully down, where nothing touches the string at all, it claimed the note died three times
	// SLOWER after the release. Run the same note twice, releasing it in one and holding it in the
	// other, and what is left between them is the damper and nothing else.
	std::printf("\nA RELEASED KEY STOPS THE NOTE. A4 struck and held a second; in one run the\n");
	std::printf("key is then let go and in the other it is not. Both are measured over the same\n");
	std::printf("stretch afterwards, as times to fall sixty decibels.\n");
	std::printf("%-10s %12s %12s %12s\n", "sustain", "held on", "released", "times faster");
	for (int j = 0; j <= 4; j++) {
		const double pedal = 0.25 * j;
		double got[2] = {0.0, 0.0};
		for (int pass = 0; pass < 2; pass++) {
			PianoBoard b;
			b.setSampleRate((float) SR);
			b.silence();
			b.setSustain((float) pedal);
			b.hit(69, 2.f);
			std::vector<float> x((size_t) (4.0 * SR));
			for (size_t i = 0; i < x.size(); i++) {
				if (pass == 1 && i == (size_t) (1.0 * SR))
					b.release(69);
				x[i] = b.process();
			}
			got[pass] = rateOver(x, b.note(69).string(0).frequency(), 1u << 11, 1.15, 3.9);
		}
		std::printf("%-10.2f %12.2f %12.2f %12.1f\n", pedal, got[0], got[1],
			(got[1] > 0.0) ? got[0] / got[1] : 0.0);
	}
	std::printf("  HALF A PEDAL IS A REAL POSITION AND NOT A BLEND. The damper rests with\n");
	std::printf("  whatever weight the pedal is not taking off it, so a note released under a\n");
	std::printf("  pedal half down dies at a rate in between — which is what pianists use it\n");
	std::printf("  for. With the pedal fully down the release changes nothing at all, because\n");
	std::printf("  nothing has touched the string.\n");

	// WHAT THE NEXT BLOW MEETS, which is the damper's travel measured directly.
	//
	// NOT THE PEAK OF THE SECOND NOTE, which was tried and measures nothing: the second blow is
	// the same blow however long the gap, so its peak is the same too, and the table came out flat
	// to a third of a decibel. What changes is what the hammer FINDS — a string still ringing, a
	// string half stopped, or a string at rest — and that is what makes a note repeated under the
	// fingers sound joined up or separate.
	std::printf("\nA DAMPER TAKES TIME TO LAND. A4 struck and released, and then left alone:\n");
	std::printf("where the damper has got to, and what is left of the string, after a delay.\n");
	std::printf("%-12s %12s %14s %16s\n", "after ms", "damper at", "its own, dB",
		"whole sound, dB");
	{
		// MEASURED FROM THE SOUND AND NOT FROM THE NOTE'S OWN ENVELOPE. That envelope is a falling
		// memory with a tenth of a second in it, which is fine for deciding that a note has
		// stopped and useless for watching one stop: it cannot fall faster than it forgets, so it
		// reported every damper in the instrument as taking the same seven tenths of a second
		// however hard the felt was pressing. The sound itself has no such floor.
		PianoBoard b;
		b.setSampleRate((float) SR);
		b.silence();
		b.hit(69, 2.f);
		std::vector<float> x((size_t) (0.2 * SR));
		for (int i = 0; i < (int) (0.5 * SR); i++)
			b.process();
		b.release(69);
		for (size_t i = 0; i < x.size(); i++)
			x[i] = b.process();
		// The loudness of a short window, which for a note at 440 Hz is several of its cycles.
		auto levelAt = [&](double when) {
			const size_t from = (size_t) (when * SR);
			const size_t take = (size_t) (0.008 * SR);
			if (from + take >= x.size())
				return 0.0;
			double sum = 0.0;
			for (size_t i = from; i < from + take; i++)
				sum += (double) x[i] * (double) x[i];
			return std::sqrt(sum / (double) take);
		};
		// AND THE NOTE'S OWN FUNDAMENTAL, apart from everything else that is sounding. The two
		// columns differ by a great deal and the difference is the point: what is left after the
		// damper has done its work is not this note at all.
		const double f1 = (double) b.note(69).string(0).frequency();
		auto toneAt = [&](double when) {
			const size_t from = (size_t) (when * SR);
			const size_t take = (size_t) 1024;
			if (from + take >= x.size())
				return 0.0;
			double re = 0.0, im = 0.0;
			for (size_t i = 0; i < take; i++) {
				const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i
					/ (double) (take - 1));
				const double a = 2.0 * M_PI * f1 * (double) i / SR;
				re += (double) x[from + i] * w * std::cos(a);
				im -= (double) x[from + i] * w * std::sin(a);
			}
			return std::sqrt(re * re + im * im) / (double) take;
		};
		const double before = levelAt(0.0);
		const double beforeTone = toneAt(0.0);
		double at = 0.0;
		for (int j = 0; j <= 8; j++) {
			const double when = 0.02 * j;
			while (at < when) {
				b.process();
				at += 1.0 / SR;
			}
			std::printf("%-12.0f %12.2f %14.1f %16.1f\n", 1000.0 * when,
				(float) std::fmin(1.0, when / 0.045),
				20.0 * std::log10(std::max(toneAt(when), 1e-30) / beforeTone),
				20.0 * std::log10(std::max(levelAt(when), 1e-30) / before));
		}
		std::printf("  The damper is a lever with a felt on the end being let down by its own\n");
		std::printf("  weight, so it travels at a steady rate and ARRIVES, rather than\n");
		std::printf("  approaching for ever: a note has to be able to stop. It is slower to land\n");
		std::printf("  than to lift, because lifting is driven by the key or the pedal.\n");
		std::printf("  AND WHAT IS LEFT IS NOT THE NOTE. Its own fundamental is forty decibels\n");
		std::printf("  down while the instrument as a whole is only twenty, because the top two\n");
		std::printf("  octaves have no dampers at all and are still ringing with what this note\n");
		std::printf("  put into them. That is not a leak in the model: it is why the top of a\n");
		std::printf("  piano hangs in the air after everything below it has been stopped, and it\n");
		std::printf("  is the reason those notes are left undamped in the first place.\n");
	}

	// ---- the gate ----
	std::printf("\nA CHORD HELD SILENTLY AND STRUCK ELSEWHERE, which is phase 5's gate and is a\n");
	std::printf("thing pianists really do: press C4, E4 and G4 slowly enough that no hammer\n");
	std::printf("reaches a string, then strike C2 hard and let it go. C4, E4 and G4 are C2's\n");
	std::printf("fourth, fifth and sixth partials, and should be left singing.\n");
	{
		const int chord[3] = {60, 64, 67};
		double withHeld[3] = {}, without[3] = {};
		for (int pass = 0; pass < 2; pass++) {
			PianoBoard b;
			b.setSampleRate((float) SR);
			b.silence();
			if (pass == 0) {
				for (int k = 0; k < 3; k++)
					b.hold(chord[k]);
			}
			b.hit(36, 5.f);
			for (int i = 0; i < (int) (0.6 * SR); i++)
				b.process();
			b.release(36);
			for (int i = 0; i < (int) (1.4 * SR); i++)
				b.process();
			for (int k = 0; k < 3; k++)
				(pass == 0 ? withHeld : without)[k] = (double) b.note(chord[k]).moving();
		}
		std::printf("%-8s %14s %14s %12s\n", "note", "held silently", "not held", "difference");
		for (int k = 0; k < 3; k++)
			std::printf("%-8s %14.2e %14.2e %10.1f dB\n", noteName(chord[k]), withHeld[k],
				without[k], 20.0 * std::log10(withHeld[k] / std::max(without[k], 1e-30)));
		std::printf("  The held strings are still sounding two seconds after the note that woke\n");
		std::printf("  them was itself stopped. Nothing was struck but C2, and there is still no\n");
		std::printf("  resonance code: the dampers were simply not on those three.\n");
	}

	std::printf("\nAND THE SUSTAIN PEDAL IS NOT A SUSTAIN CONTROL. What it does to a note that\n");
	std::printf("is already sounding — whose own damper is off either way, since its key is\n");
	std::printf("held — is let every other string in the instrument join in. A3, with the rest\n");
	std::printf("of the board damped and then free:\n");
	{
		const size_t N = windowFor(57, 0.03);
		for (int j = 0; j < 2; j++) {
			PianoBoard b;
			b.setSampleRate((float) SR);
			b.silence();
			b.setSustain(j == 0 ? 0.f : 1.f);
			b.hit(57, 2.f);
			// LONG ENOUGH FOR THE LONGER OF THE TWO. With the pedal down the note rings for the
			// better part of a minute, and reading that off fourteen seconds of it gives a number
			// that depends more on where the recording stopped than on the note.
			std::vector<float> x((size_t) (30.0 * SR) + 2 * N);
			for (size_t i = 0; i < x.size(); i++)
				x[i] = b.process();
			double first = 0, then = 0, cross = 0;
			twoRateDecay(x, SR, b.note(57).string(0).frequency(), N, &first, &then, &cross);
			std::printf("  dampers %s on everything else:  first T60 %.2f, then %.2f\n",
				(j == 0) ? "down" : "up  ", first, then);
		}
		std::printf("  A3 by itself, on a bridge with nothing else on it and no modes in it, is\n");
		std::printf("  2.79 and 20.63. A board full of DAMPED strings is not the same thing and no\n");
		std::printf("  longer comes to the same figures: the board's modes decide how readily the\n");
		std::printf("  bridge gives way at A3's own frequency, and at two hundred and twenty hertz\n");
		std::printf("  it gives way more than a featureless bridge would, so the note parts with\n");
		std::printf("  its energy faster. Lifting the dampers then lengthens the aftersound by\n");
		std::printf("  three times, because every string sharing a partial with A3 is holding some\n");
		std::printf("  of it and handing it back.\n");
		std::printf("  THE INITIAL FALL BARELY MOVES, which is the other half of the story: that\n");
		std::printf("  part of the note is over before any of this has had time to happen. The\n");
		std::printf("  pedal does nothing to the attack and everything to what follows it, which\n");
		std::printf("  is exactly what it is used for.\n");
	}

	std::printf("\nTHE SOFT PEDAL, which is not a volume control. The action slides sideways,\n");
	std::printf("so the outer string of a choir is caught by the edge of the felt or missed,\n");
	std::printf("and every string is struck by a softer part of the hammer.\n");
	std::printf("%-8s %10s %12s %12s %12s\n", "soft", "peak dB", "contact ms", "centroid",
		"peak force");
	for (int j = 0; j <= 2; j++) {
		const float amount = 0.5f * (float) j;
		PianoNote n;
		n.setSampleRate((float) SR);
		n.setNote(60.f);
		n.setSoftPedal(amount);
		n.hit(2.f);
		std::vector<float> x((size_t) (1.0 * SR));
		double peak = 0.0, force = 0.0;
		for (size_t i = 0; i < x.size(); i++) {
			x[i] = n.process();
			peak = std::max(peak, (double) std::fabs(x[i]));
			force = std::max(force, (double) n.hammerForce());
		}
		std::printf("%-8.1f %10.2f %12.3f %12.1f %12.2f\n", amount,
			20.0 * std::log10(peak / 1.0), n.contactMilliseconds(),
			centroidOf(x, SR, 0.01, 0.1), force);
	}
	std::printf("  Quieter AND darker AND longer in contact, which is three changes a gain\n");
	std::printf("  control cannot make. The missed string is not silent either — it is left to\n");
	std::printf("  be driven through the bridge by its neighbours, which is much of why the una\n");
	std::printf("  corda sounds veiled rather than merely soft.\n");

	std::printf("\nTHE SOSTENUTO PEDAL: what is held when it goes down stays held, and nothing\n");
	std::printf("played afterwards is caught. C4 is held and the pedal pressed; then C4 is\n");
	std::printf("released and E4 struck and released. After two seconds:\n");
	{
		PianoBoard b;
		b.setSampleRate((float) SR);
		b.silence();
		b.hit(60, 2.f);
		for (int i = 0; i < (int) (0.2 * SR); i++)
			b.process();
		b.setSostenuto(true);
		b.release(60);
		for (int i = 0; i < (int) (0.2 * SR); i++)
			b.process();
		b.hit(64, 2.f);
		for (int i = 0; i < (int) (0.3 * SR); i++)
			b.process();
		b.release(64);
		for (int i = 0; i < (int) (2.0 * SR); i++)
			b.process();
		std::printf("  C4, caught by the pedal:      damper at %.2f, still moving %.2e\n",
			b.note(60).damperAt(), b.note(60).moving());
		std::printf("  E4, played after the pedal:   damper at %.2f, still moving %.2e\n",
			b.note(64).damperAt(), b.note(64).moving());
	}

	}

	if (doing(1) || doing(2)) {
	// ---- PHASE 6: the soundboard -----------------------------------------------------------------
	}

	if (doing(6)) {
	std::printf("\n");
	std::printf("================================================================================\n");
	std::printf("PHASE 6 — the soundboard\n");
	std::printf("================================================================================\n");

	{
		PianoBoard withModes, plain;
		withModes.setSampleRate((float) SR);
		withModes.silence();
		plain.setSampleRate((float) SR);
		plain.setSoundboardModes(0);
		plain.silence();

		std::printf("\nWHAT THE STRINGS FEEL. How readily the bridge gives way, in millimetres\n");
		std::printf("per second per newton, against frequency. A plain resistance — which is what\n");
		std::printf("every phase before this one had — is the same figure everywhere.\n");
		std::printf("%-10s %14s %14s\n", "Hz", "with modes", "plain");
		for (double hz = 40.0; hz < 2600.0; hz *= 1.18) {
			std::printf("%-10.0f %14.3f %14.3f\n", hz,
				1000.0 * withModes.soundboard().admittanceAt((float) hz),
				1000.0 * plain.soundboard().admittanceAt((float) hz));
		}
		std::printf("  %d modes, the lowest at %.0f Hz and the highest at %.0f.\n",
			withModes.soundboard().modeCount(), withModes.soundboard().modeFrequency(0),
			withModes.soundboard().modeFrequency(withModes.soundboard().modeCount() - 1));
		std::printf("  Evenly spaced in HERTZ and not in octaves, which is the difference between\n");
		std::printf("  a plate and a string: a plate's modes are as crowded between four hundred\n");
		std::printf("  and five hundred as between fifty and a hundred and fifty. Modes an octave\n");
		std::printf("  apart would give a dense bass and an empty treble, which is backwards.\n");
		std::printf("  Scattered off a perfect grid as well, because an even spacing is a comb,\n");
		std::printf("  and notes played through a comb ring like a plate reverb.\n");

		// ---- the gate: the instrument becomes uneven, which is what makes it an instrument ----
		//
		// HOW FAST THE FIRST FIFTEEN DECIBELS GO, which is the rate the bridge decides. A plain
		// resistance gives every note the same bridge and so a smooth run of decays; a board with
		// modes gives each note whatever its own frequency happens to meet.
		auto initialRate = [&](PianoBoard& b, int midi) {
			b.silence();
			b.hit(midi, 2.f);
			const double f1 = b.note(midi).string(0).frequency();
			std::vector<float> x((size_t) (2.5 * SR));
			for (size_t i = 0; i < x.size(); i++)
				x[i] = b.process();
			auto tone = [&](double at) {
				const size_t from = (size_t) (at * SR), take = 4096;
				if (from + take >= x.size())
					return -300.0;
				double re = 0.0, im = 0.0;
				for (size_t i = 0; i < take; i++) {
					const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double) i
						/ (double) (take - 1));
					const double a = 2.0 * M_PI * f1 * (double) i / SR;
					re += (double) x[from + i] * w * std::cos(a);
					im -= (double) x[from + i] * w * std::sin(a);
				}
				return 20.0 * std::log10(std::sqrt(re * re + im * im) / (double) take + 1e-30);
			};
			const double a = tone(0.12), b2 = tone(1.6);
			return (a > b2 + 0.1) ? 60.0 * 1.48 / (a - b2) : 0.0;
		};

		std::printf("\nAND SO THE INSTRUMENT BECOMES UNEVEN, which is phase 6's whole point. An\n");
		std::printf("octave played chromatically, and how long each note's fundamental takes to\n");
		std::printf("fall sixty decibels at the rate it starts out at. THE RATIO IS THE\n");
		std::printf("MEASUREMENT: the same note on the same strings, with the board's modes and\n");
		std::printf("with a featureless bridge in their place.\n");
		std::printf("%-6s %9s %12s %12s %10s\n", "note", "Hz", "with modes", "featureless",
			"ratio");
		double widest = 1.0, narrowest = 1.0;
		for (int midi = 45; midi <= 57; midi++) {
			const double a = initialRate(withModes, midi);
			const double b2 = initialRate(plain, midi);
			const double ratio = (b2 > 0.0 && a > 0.0) ? a / b2 : 0.0;
			if (ratio > 0.0) {
				widest = std::max(widest, ratio);
				narrowest = std::min(narrowest, ratio);
			}
			std::printf("%-6s %9.1f %12.2f %12.2f %10.2f\n", noteName(midi), hzOf(midi), a, b2,
				ratio);
		}
		std::printf("  The board makes some of these notes die %.0f%% faster than a featureless\n",
			100.0 * (1.0 - narrowest));
		std::printf("  bridge would and others %.0f%% slower, which is a note-to-note unevenness\n",
			100.0 * (widest - 1.0));
		std::printf("  of about %.1f to one across a single octave.\n", widest / narrowest);
		std::printf("  A note whose fundamental lands on one of the board's modes finds a bridge\n");
		std::printf("  that gives way easily and loses its energy into it; a note that lands\n");
		std::printf("  between modes finds one that hardly moves and hangs on. Real pianos are\n");
		std::printf("  uneven in exactly this way, and it is not a defect to be smoothed out: it\n");
		std::printf("  is most of what makes an instrument sound like an instrument rather than\n");
		std::printf("  like a patch.\n");
		std::printf("  NEITHER COLUMN RUNS SMOOTHLY ON ITS OWN, and the first way of putting this\n");
		std::printf("  claimed the featureless one would. It does not: a note's decay depends on\n");
		std::printf("  its own strings too — its stretch, how many of them there are, how far\n");
		std::printf("  apart they are tuned, how well its loss filter was fitted — so the spread\n");
		std::printf("  of either column conflates the board with all of that. Only the ratio,\n");
		std::printf("  which is one note against itself, says what the board did.\n");

		// ---- what gets into the air ----
		std::printf("\nAND WHAT GETS INTO THE AIR. A plate is a poor radiator until its bending\n");
		std::printf("waves travel as fast as sound does: below that it pushes air back and forth\n");
		std::printf("between its own crests instead of away from itself. The same blow at every\n");
		std::printf("octave, and what comes out:\n");
		std::printf("%-6s %9s %14s\n", "note", "Hz", "peak out");
		for (int midi = 21; midi <= 105; midi += 12) {
			withModes.silence();
			withModes.hit(midi, 2.f);
			double peak = 0.0;
			for (int i = 0; i < (int) (1.5 * SR); i++)
				peak = std::max(peak, (double) std::fabs(withModes.process()));
			std::printf("%-6s %9.1f %14.4f\n", noteName(midi), hzOf(midi), peak);
		}
		std::printf("  Seven decibels down at the bottom and ten at the top, against the middle:\n");
		std::printf("  an instrument that is a little stronger in the tenor than at either end,\n");
		std::printf("  which is what a piano is. The bottom is held up by its partials rather\n");
		std::printf("  than its fundamentals — the board goes stiff below its lowest mode and\n");
		std::printf("  will not radiate twenty-seven hertz however hard it is pushed — and that\n");
		std::printf("  is why the bottom of a piano sounds as it does and why a soundboard has to\n");
		std::printf("  be as large as it is. The falling away at the very top is the strings and\n");
		std::printf("  not the board: there is hardly any string up there to drive it with.\n");
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

	}

	if (doing(3) || doing(4)) {
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
	std::printf("  These are notes on their own bridges, summed on paper; phase 4 measures the\n");
	std::printf("  real thing, where they share one. Most of the cost is the dispersion chain,\n");
	std::printf("  which is longest in the bass — where a note has one string rather than three.\n");

	}

	std::printf("\nDone. Phase 7 is playing it: the cable's notes taken as a pianist's hands,\n");
	std::printf("and a Bach prelude with its sixteenths separate.\n");
	return 0;
}
