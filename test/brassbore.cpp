/** THE AIR COLUMN, from a command line: phase 2 of docs/brass.md.

    make brassbore

The calculation in frequency (BrassBore) against the measured Conn 8H (B1): Smyth and Scott's table as it
is, with the radiation the grid carries, with a tenor mouthpiece and leadpipe as tubes, and tuned by the
tuning slide to the Conn's fourth peak; the same with the cup and throat lumped for playing in time. Then
the grid (BrassGrid) against the calculation at 44.1, 48 and 88.2 kHz, with four, three and two memory
variables for the wall's losses; the seven positions of the slide; and the delay lines with grids at the
ends (BrassWaveguide) against the calculation with the slide closed, 25 cm out and 53 cm out. Each timed
column is a flow of one step into the cup and the cup's pressure for three seconds, its spectrum's peaks
against the calculation's, and the seconds of computing a second of sound took.
*/
#include "../src/BrassBore.hpp"
#include "../src/BrassGrid.hpp"
#include "../src/BrassWaveguide.hpp"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

using namespace px;

/** The Conn 8H in first position, measured (Bilbao and Chick 2013, Table I). */
static const double CONN_8H[7] = {38.3, 113.2, 174.0, 233.2, 296.0, 350.9, 410.1};

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

/** THE PEAKS OF AN AIR COLUMN PLAYED IN TIME: a flow of `q` for one step into the cup, the cup's pressure
for `seconds`, its spectrum over the flow's (zero-padded to 2^21 points), and the peaks of that from 15 Hz
to `top`. `step` takes the flow and returns the cup's pressure. Also the seconds of computing a second of
sound took. */
static void timedPeaks(const std::function<double(double)>& step, double rate, double seconds, double top,
                       std::vector<double>* hz, std::vector<double>* mag, double* cost) {
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
	for (size_t i = (size_t) (15.0 / bin); i < x.size() / 2 && (double) i * bin <= top; i++) {
		f.push_back((double) i * bin);
		m.push_back(std::abs(x[i]));
	}
	brassPeaks(f, m, hz, mag);
}


/** THE PEAKS OF A BORE'S IMPEDANCE from 15 Hz to `top`, every 0.05 Hz. */
static void peaksOf(const BrassBore& b, const BrassAir& air, double top, std::vector<double>* hz, std::vector<double>* mag) {
	std::vector<double> f, m;
	for (double x = 15.0; x <= top; x += 0.05) {
		f.push_back(x);
		m.push_back(std::abs(b.impedance(x, air)));
	}
	brassPeaks(f, m, hz, mag);
}

/** THE TIMED COLUMN'S PEAKS AGAINST THE CALCULATION'S for the same bore, first ten. */
static void againstCalculation(const char* name, const BrassBore& b, const BrassAir& air,
                               const std::vector<double>& hz, const std::vector<double>& mag, double cost) {
	std::vector<double> rh, rm;
	peaksOf(b, air, 1200.0, &rh, &rm);
	std::printf("\n%s: %.3f s of computing a second of sound\n  peak   timed Hz   calculated Hz   difference   |Z| timed / calculated\n", name, cost);
	double worst = 0.0;
	for (size_t i = 0; i < 10 && i < hz.size() && i < rh.size(); i++) {
		std::printf("  %4zu %10.2f %15.2f %+10.1f c %10.2f\n", i + 1, hz[i], rh[i], cents(hz[i], rh[i]), mag[i] / rm[i]);
		if (i >= 1)
			worst = std::fmax(worst, std::fabs(cents(hz[i], rh[i])));
	}
	std::printf("  worst of peaks 2 to 10: %.1f cents\n", worst);
}

static void againstConn(const char* name, const BrassBore& b, const BrassAir& air) {
	std::vector<double> hz, mag;
	peaksOf(b, air, 1200.0, &hz, &mag);
	std::printf("\n%s, %.1f C:\n  peak   model Hz   Conn 8H Hz   difference   over its number   |Z| MPa s/m3\n", name, air.celsius);
	for (size_t i = 0; i < hz.size() && i < 12; i++) {
		if (i < 7)
			std::printf("  %4zu %10.1f %12.1f %+10.1f c %14.1f %14.1f\n", i + 1, hz[i], CONN_8H[i], cents(hz[i], CONN_8H[i]),
				hz[i] / (double) (i + 1), mag[i] / 1e6);
		else
			std::printf("  %4zu %10.1f %12s %12s %14.1f %14.1f\n", i + 1, hz[i], "", "", hz[i] / (double) (i + 1), mag[i] / 1e6);
	}
}

int main(int argc, char** argv) {
	(void) argc;
	(void) argv;
	const BrassAir air(20.0);
	againstConn("SMYTH AND SCOTT'S TROMBONE, FIRST POSITION, Silva radiation", BrassBore::trombone(0.0), air);
	{
		BrassBore b = BrassBore::trombone(0.0);
		b.radiation = BrassRadiation::PARALLEL_RL;
		againstConn("THE SAME, with the parallel R-L radiation the grid carries", b, air);
	}
	{
		const BrassBore b = BrassBore::tromboneWhole(0.0);
		std::printf("\n(whole: %.3f m from the cup to the bell's mouth)", b.length());
		againstConn("WITH ITS MOUTHPIECE AND LEADPIPE AS TUBES", b, air);
	}
	// TUNED AS THE CONN: the tuning slide pulled until the fourth peak is the Conn's, by bisection.
	{
		double lo = 0.0, hi = 0.08;
		for (int i = 0; i < 24; i++) {
			const double mid = 0.5 * (lo + hi);
			std::vector<double> hz, mag;
			const BrassBore b = BrassBore::tromboneWhole(0.0, mid);
			std::vector<double> f, m;
			for (double x = 225.0; x <= 250.0; x += 0.02) {
				f.push_back(x);
				m.push_back(std::abs(b.impedance(x, air)));
			}
			brassPeaks(f, m, &hz, &mag);
			if (!hz.empty() && hz[0] > CONN_8H[3])
				lo = mid;
			else
				hi = mid;
		}
		const BrassBore b = BrassBore::tromboneWhole(0.0, 0.5 * (lo + hi));
		std::printf("\n(tuning slide pulled %.1f mm: %.3f m in all)", 1000.0 * 0.5 * (lo + hi), b.length());
		againstConn("WHOLE, TUNED TO THE CONN'S FOURTH PEAK", b, air);
		againstConn("TIMED (cup and throat lumped), the same pull", BrassBore::tromboneTimed(0.0, 0.5 * (lo + hi)), air);
		// THE GRID against the calculation for the same bore, with the radiation the grid carries.
		BrassBore timed = BrassBore::tromboneTimed(0.0, 0.5 * (lo + hi));
		timed.radiation = BrassRadiation::PARALLEL_RL;
		for (double rate : {44100.0, 48000.0, 88200.0})
			for (int terms : {4, 3, 2}) {
				if (rate == 88200.0 && terms != 4)
					continue;
				BrassGrid grid;
				BrassGrid::Setup s;
				s.bore = timed;
				s.air = air;
				s.rate = rate;
				s.lossTerms = terms;
				grid.setup(s);
				std::vector<double> hz, mag;
				double cost = 0.0;
				timedPeaks([&](double q) { return grid.step(q); }, rate, 3.0, 1200.0, &hz, &mag, &cost);
				char name[160];
				std::snprintf(name, sizeof name, "THE GRID at %.1f kHz, %d points %.1f mm apart, %s", rate / 1000.0, grid.points(),
					1000.0 * grid.spacing(), terms == 4 ? "wall losses from 4 memory variables" : (terms == 3 ? "from 3" : "from 2"));
				againstCalculation(name, timed, air, hz, mag, cost);
			}
		// THE SEVEN POSITIONS: how far the slide goes out for each semitone, found as the extension that lowers
		// the geometric mean of peaks 2 to 8 by that many semitones from first position.
		{
			const double pullNow = 0.5 * (lo + hi);
			auto meanOf = [&](double extension) {
				std::vector<double> f, m, hz, mag;
				for (double x = 50.0; x <= 520.0; x += 0.05) {
					f.push_back(x);
					m.push_back(std::abs(BrassBore::tromboneTimed(extension, pullNow).impedance(x, air)));
				}
				brassPeaks(f, m, &hz, &mag);
				double sum = 0.0;
				int count = 0;
				for (size_t i = 0; i < hz.size() && count < 7; i++)
					if (hz[i] > 60.0) {
						sum += std::log(hz[i] / (double) (count + 2));
						count++;
					}
				return std::exp(sum / std::max(1, count));
			};
			const double first = meanOf(0.0);
			std::printf("\nTHE SEVEN POSITIONS (slide out from first, for each semitone down; mean of peaks 2 to 8 over their numbers, first %.2f Hz):\n", first);
			for (int semitones = 1; semitones <= 6; semitones++) {
				const double want = first * std::pow(2.0, -semitones / 12.0);
				double a = 0.0, b = 0.7;
				for (int i = 0; i < 22; i++) {
					const double mid = 0.5 * (a + b);
					if (meanOf(mid) > want)
						a = mid;
					else
						b = mid;
				}
				std::printf("  position %d: %.1f cm\n", semitones + 1, 100.0 * 0.5 * (a + b));
			}
		}
		// THE DELAY LINES WITH GRIDS AT THE ENDS, at three places of the slide.
		const double pull = 0.5 * (lo + hi);
		for (double rate : {44100.0, 48000.0})
			for (double extension : {0.0, 0.25, 0.53}) {
				BrassWaveguide w;
				BrassWaveguide::Setup s;
				s.air = air;
				s.rate = rate;
				s.extension = extension;
				s.tuningPull = pull;
				w.setup(s);
				std::vector<double> hz, mag;
				double cost = 0.0;
				timedPeaks([&](double q) { return w.step(q); }, rate, 3.0, 1200.0, &hz, &mag, &cost);
				BrassBore reference = BrassBore::tromboneTimed(extension, pull);
				reference.radiation = BrassRadiation::PARALLEL_RL;
				char name[160];
				std::snprintf(name, sizeof name, "THE DELAY LINES at %.1f kHz, slide out %.2f m, %d grid points", rate / 1000.0,
					extension, w.gridPoints());
				againstCalculation(name, reference, air, hz, mag, cost);
			}
	}
	return 0;
}
