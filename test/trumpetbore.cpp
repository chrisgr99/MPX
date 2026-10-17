/** THE TRUMPET'S AIR COLUMN, from a command line: phase 1 of docs/trumpet.md.

    make trumpetbore

The calculation in frequency (BrassBore::trumpet) against the measured Smith-Watkins trumpet (Bilbao and
Chick 2013, Table I), open, as it is and tuned by its tuning slide to that trumpet's fourth peak; and
against Adachi and Sato's calculated trumpet (1995, Table II). Then the valves' loops, each found so that
pressed alone it lowers the open trumpet's resonances 2 to 8 — their geometric mean over their numbers — by
its interval, and the seven lengths with them, each against its interval in cents, as Moore's arithmetic
expects them (research/brass/trumpet-and-flugelhorn.md, 1.3). Then the delay lines with grids at the ends
(BrassWaveguide) against the calculation for each of the seven, at 44.1 and 48 kHz: a flow of one step
into the cup and the cup's pressure for three seconds, its spectrum's peaks against the calculation's, and
the seconds of computing a second of sound took.
*/
#include "../src/BrassBore.hpp"
#include "../src/BrassGrid.hpp"
#include "../src/BrassWaveguide.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

using namespace px;

/** The Smith-Watkins B flat trumpet with a Kelly Screamer, measured (Bilbao and Chick 2013, Table I). */
static const double SMITH_WATKINS[7] = {83.5, 234.2, 353.1, 469.1, 591.6, 702.7, 812.5};
/** Adachi and Sato's trumpet, calculated from its dimensions (1995, Table II), modes 1 to 8. */
static const double ADACHI_SATO[8] = {87.0, 232.0, 341.0, 457.0, 572.0, 686.0, 800.0, 913.0};

static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

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

/** THE PEAKS OF A BORE'S IMPEDANCE from 30 Hz to `top`, every `step` Hz. */
static void peaksOf(const BrassBore& b, const BrassAir& air, double top, std::vector<double>* hz, std::vector<double>* mag, double step = 0.05) {
	std::vector<double> f, m;
	for (double x = 30.0; x <= top; x += step) {
		f.push_back(x);
		m.push_back(std::abs(b.impedance(x, air)));
	}
	brassPeaks(f, m, hz, mag);
}

/** THE MEAN OF PEAKS 2 TO 8 over their numbers, geometric: where the series of a length lies. */
static double seriesOf(const BrassBore& b, const BrassAir& air) {
	std::vector<double> hz, mag;
	peaksOf(b, air, 1100.0, &hz, &mag, 0.1);
	double sum = 0.0;
	int count = 0;
	for (size_t i = 1; i < hz.size() && count < 7; i++, count++)
		sum += std::log(hz[i] / (double) (i + 1));
	return std::exp(sum / std::max(1, count));
}

/** THE PEAKS OF AN AIR COLUMN PLAYED IN TIME, as make brassbore finds them. */
static void timedPeaks(const std::function<double(double)>& step, double rate, double seconds, double top, std::vector<double>* hz,
	std::vector<double>* mag, double* cost) {
	const double q = 1e-6;
	const size_t n = (size_t) (seconds * rate);
	std::vector<std::complex<double> > x((size_t) 1 << 21, 0.0);
	const auto from = std::chrono::steady_clock::now();
	for (size_t i = 0; i < n; i++)
		x[i] = step(i == 0 ? q : 0.0) / q;
	*cost = std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count() / seconds;
	fft(x);
	std::vector<double> f, m;
	const double bin = rate / (double) x.size();
	for (size_t i = (size_t) (30.0 / bin); i < x.size() / 2 && (double) i * bin <= top; i++) {
		f.push_back((double) i * bin);
		m.push_back(std::abs(x[i]));
	}
	brassPeaks(f, m, hz, mag);
}

static const char* const FINGERINGS[8] = {"open", "1", "2", "1 2", "3", "1 3", "2 3", "1 2 3"};
/** Each fingering's interval below the open trumpet, semitones. */
static const int SEMITONES[8] = {0, 2, 1, 3, 3, 5, 4, 6};

int main() {
	const BrassAir air(20.0);
	BrassTrumpetSetting s;
	// THE OPEN TRUMPET AS IT IS, against the measured and the calculated.
	auto show = [&](const char* name, const BrassBore& b) {
		std::vector<double> hz, mag;
		peaksOf(b, air, 1100.0, &hz, &mag);
		std::printf("\n%s (%.4f m):\n  peak   model Hz   Smith-Watkins Hz   difference   Adachi-Sato Hz   difference   over its number\n", name, b.length());
		double worst = 0.0;
		for (size_t i = 0; i < hz.size() && i < 9; i++) {
			std::printf("  %4zu %10.1f", i + 1, hz[i]);
			if (i < 7) {
				std::printf(" %18.1f %+10.1f c", SMITH_WATKINS[i], cents(hz[i], SMITH_WATKINS[i]));
				if (i >= 1)
					worst = std::fmax(worst, std::fabs(cents(hz[i], SMITH_WATKINS[i])));
			}
			else
				std::printf(" %18s %12s", "", "");
			if (i < 8)
				std::printf(" %16.1f %+10.1f c", ADACHI_SATO[i], cents(hz[i], ADACHI_SATO[i]));
			std::printf(" %14.1f\n", hz[i] / (double) (i + 1));
		}
		std::printf("  peaks 2 to 7 against the Smith-Watkins: worst %.1f cents (2 per cent is 34)\n", worst);
	};
	s.tuningPull = 0.0;
	show("THE TRUMPET, OPEN, as OpenWInD's", BrassBore::trumpet(s));
	// TUNED by the tuning slide to the Smith-Watkins's fourth peak.
	{
		double lo = -0.03, hi = 0.06;
		for (int i = 0; i < 24; i++) {
			s.tuningPull = 0.5 * (lo + hi);
			std::vector<double> hz, mag, f, m;
			for (double x = 440.0; x <= 500.0; x += 0.02) {
				f.push_back(x);
				m.push_back(std::abs(BrassBore::trumpet(s).impedance(x, air)));
			}
			brassPeaks(f, m, &hz, &mag);
			if (!hz.empty() && hz[0] > SMITH_WATKINS[3])
				lo = s.tuningPull;
			else
				hi = s.tuningPull;
		}
		char name[120];
		std::snprintf(name, sizeof name, "TUNED TO ITS FOURTH PEAK, the tuning slide pulled %.1f mm", 1000.0 * s.tuningPull);
		show(name, BrassBore::trumpet(s));
	}
	// THE LOOPS: each pressed alone lowers the series by its interval.
	{
		BrassTrumpetSetting open = s;
		open.pressed = 0;
		const double series = seriesOf(BrassBore::trumpet(open), air);
		std::printf("\nTHE LOOPS (the open series %.2f Hz):\n", series);
		const int alone[3] = {2, 1, 3};
		for (int v = 0; v < 3; v++) {
			const double want = series * std::pow(2.0, -alone[v] / 12.0);
			double a = 0.0, b = 0.4;
			for (int i = 0; i < 20; i++) {
				BrassTrumpetSetting t = open;
				t.pressed = 1 << v;
				t.loops[v] = 0.5 * (a + b);
				if (seriesOf(BrassBore::trumpet(t), air) > want)
					a = t.loops[v];
				else
					b = t.loops[v];
			}
			s.loops[v] = 0.5 * (a + b);
			std::printf("  valve %d, %d semitone%s: the loop %.1f mm past the straight path\n", v + 1, alone[v], alone[v] > 1 ? "s" : "",
				1000.0 * s.loops[v]);
		}
		std::printf("\nTHE SEVEN LENGTHS (each loop in tune alone):\n  fingering   series Hz   below open, cents   its interval   error, cents\n");
		for (int f = 0; f < 8; f++) {
			BrassTrumpetSetting t = s;
			t.pressed = f;
			const double here = seriesOf(BrassBore::trumpet(t), air);
			const double below = cents(series, here);
			std::printf("  %-9s %11.2f %19.1f %14d %+14.1f\n", FINGERINGS[f], here, below, 100 * SEMITONES[f], 100.0 * SEMITONES[f] - below);
		}
	}
	// THE DELAY LINES WITH GRIDS AT THE ENDS against the calculation, each of the seven lengths.
	for (double rate : {44100.0, 48000.0}) {
		std::printf("\nTHE DELAY LINES at %.1f kHz against the calculation (worst of peaks 2 to 10, cents; seconds of computing a second):\n", rate / 1000.0);
		for (int f = 0; f < 8; f++) {
			BrassTrumpetSetting t = s;
			t.pressed = f;
			BrassWaveguide w;
			BrassWaveguide::Setup ws;
			ws.air = air;
			ws.rate = rate;
			ws.instrument = BrassWaveguide::Setup::TRUMPET;
			ws.trumpet = t;
			w.setup(ws);
			std::vector<double> hz, mag;
			double cost = 0.0;
			timedPeaks([&](double q) { return w.step(q); }, rate, 3.0, 1200.0, &hz, &mag, &cost);
			BrassBore reference = BrassBore::trumpetTimed(t);
			reference.radiation = BrassRadiation::PARALLEL_RL;
			std::vector<double> rh, rm;
			peaksOf(reference, air, 1200.0, &rh, &rm);
			double worst = 0.0;
			std::printf("  %-7s", FINGERINGS[f]);
			for (size_t i = 1; i < 10 && i < hz.size() && i < rh.size(); i++) {
				std::printf(" %+5.1f", cents(hz[i], rh[i]));
				worst = std::fmax(worst, std::fabs(cents(hz[i], rh[i])));
			}
			std::printf("   worst %.1f, %.3f s\n", worst, cost);
		}
	}
	// THE VALVES MOVING while the air column rings: each valve down and up over 15 ms, twenty times a second,
	// the cup fed with noise; nothing may grow without bound.
	{
		BrassWaveguide w;
		BrassWaveguide::Setup ws;
		ws.air = air;
		ws.rate = 48000.0;
		ws.instrument = BrassWaveguide::Setup::TRUMPET;
		ws.trumpet = s;
		ws.trumpet.pressed = 0;
		w.setup(ws);
		uint64_t seed = 12345;
		double loudest = 0.0, quiet = 0.0;
		const int n = 48000 * 3, travel = 720;
		for (int i = 0; i < n; i++) {
			seed = seed * 6364136223846793005ull + 1442695040888963407ull;
			const double noise = ((double) (seed >> 11) / 9007199254740992.0 - 0.5) * 1e-5;
			const int phase = i % 2400, valve = (i / 2400) % 3;
			const double down = phase < travel ? (double) phase / travel : phase < 1200 ? 1.0 : phase < 1200 + travel ? 1.0 - (double) (phase - 1200) / travel : 0.0;
			w.setValve(valve, down);
			const double p = w.step(noise);
			if (!std::isfinite(p)) {
				std::printf("\nTHE VALVES MOVING: not finite at %d\n", i);
				return 1;
			}
			loudest = std::fmax(loudest, std::fabs(p));
			if (i > n - 48000)
				quiet = std::fmax(quiet, std::fabs(p));
		}
		std::printf("\nTHE VALVES MOVING, three seconds: the cup's loudest %.1f Pa, over the last second %.1f Pa: finite\n", loudest, quiet);
	}
	return 0;
}
