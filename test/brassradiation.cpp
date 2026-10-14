/** THE SOUND THAT LEAVES THE BELL, from a command line: phase 5 of docs/brass.md (src/BrassRadiation).

    make brassradiation

Checks Bessel's functions against tabulated values; Levine and Schwinger's |R| against their Figure 1;
that their power gain carries all the power at every ka (half the integral of G sin theta over theta is
one) and meets their own limits on the axis, behind and to the side; then the listener's filter for the
trombone's bell at several angles: its response against the small source's, its length and its cost.
*/
#include "../src/BrassBore.hpp"
#include "../src/BrassGrid.hpp"
#include "../src/BrassRadiation.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace px;

/** Half the integral over theta of G sin theta: one where G carries all the power. */
static double total(double ka) {
	const int n = std::max(600, (int) (60.0 * ka));
	double sum = 0.0;
	for (int i = 0; i < n; i++) {
		const double t = M_PI * ((double) i + 0.5) / n;
		sum += brassBellGain(ka, t) * std::sin(t);
	}
	return 0.5 * sum * M_PI / n;
}

int main() {
	std::printf("BESSEL'S FUNCTIONS against tabulated values\n");
	const double xs[5] = {0.5, 1.0, 2.0, 5.0, 10.0};
	const double j1[5] = {0.2422684577, 0.4400505857, 0.5767248078, -0.3275791376, 0.0434727462};
	const double y1[5] = {-1.4714723927, -0.7812128213, -0.1070324315, 0.1478631434, 0.2490154242};
	double worst = 0.0;
	for (int i = 0; i < 5; i++) {
		worst = std::fmax(worst, std::fabs(brassJ1(xs[i]) - j1[i]));
		worst = std::fmax(worst, std::fabs(brassY1(xs[i]) - y1[i]));
	}
	std::printf("  worst difference %.2g; J1's first zero %.4f (3.8317), N1's %.4f (2.1971)\n", worst,
		[] { double lo = 3.5, hi = 4.0; for (int i = 0; i < 60; i++) { const double m = 0.5 * (lo + hi); (brassJ1(m) > 0 ? lo : hi) = m; } return lo; }(),
		[] { double lo = 1.5, hi = 2.5; for (int i = 0; i < 60; i++) { const double m = 0.5 * (lo + hi); (brassY1(m) < 0 ? lo : hi) = m; } return lo; }());

	std::printf("\nLEVINE AND SCHWINGER'S |R| against Silva et al.'s fit to it\n");
	for (double ka : {0.05, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 3.8})
		std::printf("  ka %.2f: |R| %.3f, Silva %.3f\n", ka, brassUnflangedReflection(ka),
			(1.0 + 0.2 * ka - 0.084 * ka * ka) / (1.0 + 0.2 * ka + (0.5 - 0.084) * ka * ka));
	// At small ka, |R| = 1 - (ka)^2 / 2 to leading order.
	std::printf("  at ka 0.05, (1 - |R|) / (ka)^2 = %.4f (0.5)\n", (1.0 - brassUnflangedReflection(0.05)) / 0.0025);

	std::printf("\nTHE POWER GAIN: all the power (one), and their limits — G(0) = ka^2 / (1 - |R|^2), G(pi) / G(0) = |R|^2\n");
	std::printf("     ka   all the power   G(0)   their G(0)   G(pi)/G(0)   |R|^2   G at 30, 60, 90, 120 degrees\n");
	for (double ka : {0.1, 0.5, 1.0, 2.0, 3.0, 3.3, 3.6, 4.0, 6.0, 10.0, 20.0, 40.0}) {
		const double r = ka < 3.83 ? brassUnflangedReflection(ka) : 0.0;
		const double g0 = brassBellGain(ka, 0.0);
		std::printf("  %5.1f %15.4f %7.2f %12.2f %12.4f %7.4f   %.3f %.3f %.3f %.4f\n", ka, total(ka), g0,
			ka < 3.4 ? ka * ka / (1.0 - r * r) : NAN, brassBellGain(ka, M_PI) / g0, r * r, brassBellGain(ka, M_PI / 6.0),
			brassBellGain(ka, M_PI / 3.0), brassBellGain(ka, M_PI / 2.0), brassBellGain(ka, 2.0 * M_PI / 3.0));
	}
	{
		const double ka = 2.0, r = brassUnflangedReflection(ka);
		const double j = brassJ1(ka), y = brassY1(ka);
		std::printf("  at ka 2, G(pi/2) %.4f, their closed form %.4f\n", brassBellGain(ka, M_PI / 2.0),
			4.0 / M_PI * j / std::sqrt(j * j + y * y) * r / (1.0 - r * r));
	}

	// THE TROMBONE'S BELL as BrassWaveguide sets it up.
	std::printf("\nTHE LISTENER'S FILTER for the trombone's bell at 48 kHz, a metre away: decibels against the small source\n");
	const BrassAir air(20.0);
	const BrassBore bore = BrassBore::tromboneTimed(0.0, 0.033);
	size_t first = 2, last = first;
	while (last < bore.sections.size() && bore.sections[last].r0 == bore.sections[last].r1)
		last++;
	BrassGrid::Setup g;
	g.air = air;
	g.rate = 48000.0;
	g.bore = bore;
	g.bore.sections.assign(bore.sections.begin() + (long) last, bore.sections.end());
	g.startIsPort = true;
	BrassListener small;
	small.smallSource = true;
	BrassListenerFilter reference;
	reference.design(g, small);
	const double hzs[10] = {100, 250, 500, 1000, 1500, 2000, 3000, 5000, 8000, 12000};
	std::printf("   angle   taps ");
	for (double hz : hzs)
		std::printf("%8.0f", hz);
	std::printf("  Hz\n");
	std::printf("   small %6d ", reference.taps());
	for (double hz : hzs)
		std::printf("%8.1f", 20.0 * std::log10(reference.magnitude(hz) + 1e-30));
	std::printf("  dB, the small source's own response\n");
	for (double degrees : {0.0, 15.0, 30.0, 45.0, 60.0, 90.0}) {
		BrassListener where;
		where.angle = degrees * M_PI / 180.0;
		BrassListenerFilter f;
		const auto start = std::chrono::steady_clock::now();
		f.design(g, where);
		const double ms = 1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::printf("  %5.0f %6d ", degrees, f.taps());
		for (double hz : hzs)
			std::printf("%8.1f", 20.0 * std::log10(f.magnitude(hz) / reference.magnitude(hz)));
		std::printf("  (designed in %.0f ms)\n", ms);
	}
	// The cost of running one.
	{
		BrassListener where;
		where.angle = M_PI / 6.0;
		BrassListenerFilter f;
		f.design(g, where);
		double sink = 0.0;
		const int n = 48000 * 4;
		const auto start = std::chrono::steady_clock::now();
		for (int i = 0; i < n; i++)
			sink += f.process(std::sin(0.01 * i));
		const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::printf("  running it: %.2f per cent of one core at 48 kHz (%d taps)%s\n", 100.0 * s / 4.0, f.taps(), sink == 12345.0 ? " " : "");
	}
	return 0;
}
