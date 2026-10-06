/** WHAT SHAPE OF LOOP FILTER CAN ACTUALLY HOLD THE DECAY LAW.

	make lossprobe

A measurement and not a model. The piano string's partials are to die by a power law — the one at f
in the fundamental's time multiplied by f0 over f raised to about one — and for that the loop's loss
in decibels must grow very nearly in PROPORTION to the frequency, from the fundamental to the top of
the run. This asks of each candidate form the only question that matters: given the best
coefficients it can have, how far out is its worst partial's decay time?

What it found, and what therefore went into src/PianoString.cpp:

	ONE POLE, or TWO BARE POLES: nineteen to twenty-three per cent, and the second pole changes
	nothing at all. A pole's loss grows as the SQUARE of the frequency below its corner and stops
	growing above it, and those are the only two shapes it has; two curves that are each
	squared-then-flat still make squared-then-flat. The law wanted is between the two.

	A SYMMETRIC FIR, tried because its taps enter its magnitude linearly, so the best of them is a
	linear solve rather than a search — no sweeping, no local minima, and a constant delay that
	drops out of the tuning. UNUSABLE, and the reason is in the last column: fitted to the partials
	it is free to do as it likes between them and above them, and it peaks at gains in the
	thousands outside the fitted band. A loop that gains at any frequency finds that frequency and
	screams on it.

	THREE POLE-ZERO SHELVES: five per cent at the bottom A, ten to fourteen in the middle. Each
	shelf contributes its own squared-law rise over its own octave or two and then levels off, and
	three staggered follow a gentler power law across the whole run.

	AND THE SEARCH NEEDS RESTARTS. The last column starts each descent where the design says the
	shelves belong — corners staggered geometrically across the partials, each zero a little behind
	its pole — and it stalls at once, twenty to ninety-five per cent out: the surface is narrow
	valleys and one coefficient at a time cannot turn a corner in one. Two and thirty random starts
	come within a few points of what three hundred reach, and cost a few milliseconds. */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
static const double SR = 48000.0, PI = 3.14159265358979323846;

struct Target { std::vector<double> w, need; };

static Target targetsFor(double f0, double T1, double p) {
	Target t;
	for (int n = 1; n <= 24; n++) {
		const double f = f0 * n;
		if (f > 10000.0 || f > SR * 0.45) break;
		const double T = T1 * std::pow(f0 / f, p);
		t.w.push_back(2.0 * PI * f / SR);
		t.need.push_back(-3.0 / (f0 * T));
	}
	return t;
}

// N cascaded pole-zero shelves, parameters as decades short of one.
static double shelfGain(const double* u, int n, double w) {
	double g = 1.0;
	for (int i = 0; i < n; i++) {
		const double up = std::fmin(std::fmax(u[2*i], 0.0), 1.0);
		const double uq = std::fmin(std::fmax(std::fmin(u[2*i+1], u[2*i]), 0.0), 1.0);
		const double p = 1.0 - std::pow(10.0, -4.0 * up), q = 1.0 - std::pow(10.0, -4.0 * uq);
		const double k = (1.0 - p) / (1.0 - q);
		const double nr = 1.0 - q*std::cos(w), ni = q*std::sin(w);
		const double dr = 1.0 - p*std::cos(w), di = p*std::sin(w);
		g *= k * std::sqrt((nr*nr+ni*ni)/(dr*dr+di*di));
	}
	return g;
}

static double worstOfShelves(const Target& t, const double* u, int n) {
	const double anchor = t.need[0] - std::log10(std::fmax(shelfGain(u, n, t.w[0]), 1e-12));
	double worst = 0.0;
	for (size_t i = 1; i < t.w.size(); i++) {
		const double lg = anchor + std::log10(std::fmax(shelfGain(u, n, t.w[i]), 1e-12));
		worst = std::fmax(worst, std::fabs((lg - t.need[i]) / t.need[i]));
	}
	return worst;
}

// Plain coordinate descent from many random starts.
static double fitShelves(const Target& t, int n, int trials, int rounds) {
	unsigned seed = 12345u;
	auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (double)(seed >> 8) / 16777216.0; };
	double best = 1e30;
	for (int trial = 0; trial < trials; trial++) {
		std::vector<double> u(2 * n);
		for (int i = 0; i < 2 * n; i++) u[i] = rnd();
		double at = worstOfShelves(t, u.data(), n);
		double span = 0.3;
		for (int round = 0; round < rounds; round++) {
			bool moved = false;
			for (int i = 0; i < 2 * n; i++) {
				for (int side = -1; side <= 1; side += 2) {
					const double keep = u[i];
					u[i] = std::fmin(std::fmax(keep + side * span, 0.0), 1.0);
					const double e = worstOfShelves(t, u.data(), n);
					if (e < at) { at = e; moved = true; }
					else u[i] = keep;
				}
			}
			if (!moved) span *= 0.6;
			if (span < 1e-7) break;
		}
		best = std::fmin(best, at);
	}
	return best;
}

// A symmetric FIR of given length, by weighted least squares, plus its peak over the whole band.
static void fitFir(const Target& t, int taps, double* worst, double* peak) {
	const int M = (taps - 1) / 2, n = M + 1;
	const int rows = (int) t.w.size();
	std::vector<double> eq((size_t) n * (n + 1), 0.0);
	for (int r = 0; r < rows; r++) {
		std::vector<double> b(n);
		b[0] = 1.0;
		for (int k = 1; k <= M; k++) b[k] = 2.0 * std::cos(k * t.w[r]);
		const double want = std::pow(10.0, t.need[r]);
		const double ww = 1.0 / (t.need[r] * t.need[r]);
		for (int j = 0; j < n; j++) {
			for (int k = 0; k < n; k++) eq[(size_t)j*(n+1)+k] += ww * b[j] * b[k];
			eq[(size_t)j*(n+1)+n] += ww * b[j] * want;
		}
	}
	for (int c = 0; c < n; c++) {
		int piv = c;
		for (int r = c+1; r < n; r++) if (std::fabs(eq[(size_t)r*(n+1)+c]) > std::fabs(eq[(size_t)piv*(n+1)+c])) piv = r;
		for (int k = 0; k <= n; k++) std::swap(eq[(size_t)c*(n+1)+k], eq[(size_t)piv*(n+1)+k]);
		for (int r = 0; r < n; r++) {
			if (r == c) continue;
			const double f = eq[(size_t)r*(n+1)+c] / eq[(size_t)c*(n+1)+c];
			for (int k = c; k <= n; k++) eq[(size_t)r*(n+1)+k] -= f * eq[(size_t)c*(n+1)+k];
		}
	}
	std::vector<double> h(n);
	for (int i = 0; i < n; i++) h[i] = eq[(size_t)i*(n+1)+n] / eq[(size_t)i*(n+1)+i];
	auto gainAt = [&](double w) {
		double g = h[0];
		for (int k = 1; k <= M; k++) g += 2.0 * h[k] * std::cos(k * w);
		return g;
	};
	const double scale = std::pow(10.0, t.need[0]) / gainAt(t.w[0]);
	*worst = 0.0;
	for (size_t i = 1; i < t.w.size(); i++) {
		const double lg = std::log10(std::fmax(scale * gainAt(t.w[i]), 1e-12));
		*worst = std::fmax(*worst, std::fabs((lg - t.need[i]) / t.need[i]));
	}
	*peak = 0.0;
	for (int i = 0; i <= 4096; i++)
		*peak = std::fmax(*peak, std::fabs(scale * gainAt(PI * (double) i / 4096.0)));
}

/** THE CHEAP VERSION: started where the design says the sections belong — corners staggered
geometrically across the run of partials, each zero a fixed way behind its pole — and then refined
by coordinate descent for a fixed small number of rounds. One start, no randomness. */
static double fitShelvesCheap(const Target& t, int n, int rounds) {
	const double fLo = t.w[0] * SR / (2 * PI), fHi = t.w.back() * SR / (2 * PI);
	std::vector<double> u(2 * n);
	for (int i = 0; i < n; i++) {
		// A corner at this fraction of the way up the partial range, in the logarithm.
		const double frac = (n == 1) ? 0.5 : (double) i / (double) (n - 1);
		const double fc = fLo * std::pow(fHi / fLo, frac);
		// A pole at corner f is 1 - 2 pi f / SR, near enough; expressed as decades short of one.
		const double shortBy = -std::log10(std::fmin(2.0 * PI * fc / SR, 0.5)) / 4.0;
		u[2*i] = std::fmin(std::fmax(shortBy, 0.0), 1.0);
		u[2*i+1] = std::fmin(u[2*i] + 0.08, 1.0);   // the zero a little behind the pole
	}
	double at = worstOfShelves(t, u.data(), n);
	double span = 0.25;
	for (int round = 0; round < rounds; round++) {
		bool moved = false;
		for (int i = 0; i < 2 * n; i++) {
			for (int side = -1; side <= 1; side += 2) {
				const double keep = u[i];
				u[i] = std::fmin(std::fmax(keep + side * span, 0.0), 1.0);
				const double e = worstOfShelves(t, u.data(), n);
				if (e < at) { at = e; moved = true; }
				else u[i] = keep;
			}
		}
		if (!moved) span *= 0.6;
	}
	return at;
}

int main() {
	struct { const char* name; double f0, t1; } notes[3] =
		{{"A0", 27.5, 30.0}, {"A3", 220.0, 11.0}, {"A5", 880.0, 3.0}};
	for (int k = 0; k < 3; k++) {
		const Target t = targetsFor(notes[k].f0, notes[k].t1, 1.15);
		std::printf("%s: %zu partials, %.1f..%.0f Hz\n", notes[k].name, t.w.size(),
			t.w[0]*SR/(2*PI), t.w.back()*SR/(2*PI));
		for (int n = 1; n <= 4; n++)
			std::printf("   %d pole-zero section(s): best %5.1f%%   32 starts %5.1f%%   "
				"12 starts %5.1f%%   designed start, no restarts %5.1f%%\n",
				n, 100.0*fitShelves(t, n, 300, 400), 100.0*fitShelves(t, n, 32, 120),
				100.0*fitShelves(t, n, 12, 120), 100.0*fitShelvesCheap(t, n, 120));
		for (int taps = 7; taps <= 63; taps = taps * 2 + 1) {
			double worst, peak;
			fitFir(t, taps, &worst, &peak);
			std::printf("   %2d-tap symmetric FIR:   worst %5.1f%%   but its gain peaks at %.0f\n",
				taps, 100.0*worst, peak);
		}
	}
	return 0;
}
