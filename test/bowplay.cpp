/** WHERE A BOWED STRING PLAYS, AND HOW ITS NOTES START: phase 3 of docs/bowed-strings.md.

TWO PUBLISHED DIAGRAMS ARE THE SPECIFICATION. Schelleng's says over what band of bow force a held
note keeps Helmholtz motion, at each distance of the bow from the bridge. Guettler's says which
combinations of bow force and bow acceleration start a note cleanly from rest. A model that draws
both is playable; one that does not is not, however a single note sounds. Each cell of each
diagram is one simulated note, so the bench prints the diagram and measures it against the source.

WHAT EACH SECTION TESTS AGAINST.

  buildup    The friction force under a bow held stuck: Guettler's equation (2), exactly, on a
             string that loses nothing. A check of the junction and the two segments' delays.
  firstslip  When the string first lets go of an accelerating bow, against Guettler's (5b).
  guettler   Guettler's own verification (his Figure 10): the simple string, six bow positions,
             the perfect attacks against his four limits A to D.
  attacks    The modelled cello D3 started from rest over a grid of force and acceleration, the
             attacks graded by Guettler and Askenfelt's listening limits.
  schelleng  Helmholtz motion held across force and bow position on the modelled cello D3, against
             Galluzzo's measured limits for a cello D string.
  measured   Attacks on Matusiak and Chatziioannou's cello G string, against the Guettler diagram they
             measured with a robot arm and a real bow.
  rehearse   How hard starting notes is for a player under the friction the string plays with, and
             what a player who rehearses each attack on a copy of the string gets.
  oversampling  Phase 4: Schelleng's limits from the cello up to the top of the violin's E string, at
             one to thirty-two times the sample rate, and the schedule that follows from them.
  listen     A clean start, a creaky one and a slipping one, to hear.

    make bowplay                      all of it
    make bowplay ARGS="attacks"       one section
*/
#include "bowbench.hpp"

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <thread>


// ---- GUETTLER'S LIMITS ----------------------------------------------------------------------

/** THE FOUR LIMITS ON A PERFECT ATTACK, Guettler (2002) equations (8b), (10b) and (12), for a
string that loses nothing, bowed at 1/beta a whole number, from rest at constant acceleration.
Each is an acceleration in metres a second squared for the given force: a perfect attack needs
the acceleration above B and C and no more than A and D. `muFirst` is the friction coefficient in
the first slip and `muLater` in the slips that follow it, which Guettler too took from the
simulations themselves. */
struct Limits { double a, b, c, d; };

static Limits guettlerLimits(double force, double beta, double periodSeconds, double z,
                             double muS, double muFirst, double muLater, double nutReflect = 1.0) {
	const double tz = periodSeconds * z;
	const double b = beta;
	Limits L;
	auto root = [](double x) { return std::sqrt(std::fmax(0.0, x)); };
	// (8b), left: B, the second release must come.
	L.b = b * (1.0 - b) * force * (3.0 * muS - muFirst - 2.0 * root(2.0 * muS * muS - muS * muFirst)) / tz;
	// (8b), right: A, no release before it is due.
	L.a = b * (1.0 - b) * force * ((3.0 - 4.0 * b) * muS - muFirst
		- 2.0 * root((1.0 - 2.0 * b) * (2.0 * (1.0 - b) * muS * muS - muS * muFirst)))
		/ ((1.0 - 2.0 * b) * tz);
	// (10b): C, the release 1/beta periods in, which the first slip's own reflection from the nut
	// arrives to cancel.
	{
		const double lam = std::pow(nutReflect, 1.0 / b);
		const double cc = (muS - muLater) / std::fmax(1e-9, muS - muFirst);
		const double q = 1.0 - 1.5 * b;
		L.c = b * b * (1.0 - b) * force * (q * (cc + lam) * (muS - muFirst) + b * muS
			- root((2.0 * b - 3.0 * b * b) * (cc + lam) * (muS * muS - muS * muFirst) + b * b * muS * muS))
			/ (2.0 * q * q * tz);
	}
	// (12): D, the highest of the static friction's later peaks, a third of 1/beta periods in.
	{
		const double poly = 8.0 - 44.0 * b + 104.0 * b * b - 132.0 * b * b * b
			+ 90.0 * b * b * b * b - 27.0 * b * b * b * b * b;
		const double m = 2.0 - 4.0 * b + 3.0 * b * b;
		L.d = 9.0 * b * b * (1.0 - b) * force * ((2.0 - 3.0 * b * b) * muS - m * muFirst
			- 2.0 * root((2.0 * b - 3.0 * b * b) * ((2.0 - 2.0 * b) * muS * muS - m * muS * muFirst)))
			/ (poly * tz);
	}
	return L;
}


static bool insideWedge(const Limits& L, double a) {
	return a > L.b && a <= L.a && a > L.c && a <= L.d;
}


// ---- THE SIMPLE STRING ----------------------------------------------------------------------

/** GUETTLER'S STRING for checking his equations: a period of five milliseconds, 0.22 kg/s, no
losses, and his hyperbolic friction — 1.0 static, 0.75 at 10 cm/s, falling to 0.5. */
static const double SIMPLE_HZ = 200.0;
static const float SIMPLE_Z = 0.22f;

/** THE PERIOD NEAREST FIVE MILLISECONDS THAT 1/beta DIVIDES in whole samples. His equations are
for a bow at a whole-number fraction of the length, and at 48 kHz a ninth of 240 samples is not a
whole number: the interpolated reads reshape the first slip's pulse a little on each of its trips,
and by the ninth it triggers a stray slip of its own. */
static double simpleHz(float beta) {
	const int n = (int) std::lround(1.0 / beta);
	const int samples = n * (int) std::lround(RATE / SIMPLE_HZ / n);
	return RATE / samples;
}

static BowedString simple(float beta) {
	BowedString s;
	s.setSampleRate(RATE);
	s.setFrequency((float) simpleHz(beta));
	s.setContact(beta);
	s.setImpedance(SIMPLE_Z);
	s.setLossless(true);
	s.setTorsion(0.f);
	s.setFriction(1.0f, 0.5f, 0.1f);
	return s;
}


static void buildupSection() {
	std::printf("\nTHE FRICTION BUILDING UP under a bow held stuck, on a string that loses nothing.\n");
	// Each wave the stuck bow sends out comes back inverted from its own end and has to be held
	// as well: a step of 2 Z v0 every beta T from the bridge side and every (1 - beta) T from the
	// nut side, as Guettler describes under his equation (2). Averaged over the steps that is his
	// (3), 2 Z [v0 + v0 t / (T beta (1 - beta))]. In whole samples at 48 kHz the delays are exact.
	const float beta = 1.f / 6.f, v0 = 0.1f;
	BowedString s = simple(beta);
	const int period = (int) std::lround(RATE / simpleHz(beta));
	const int nb = (int) std::lround(beta * period), nn = period - nb;
	const int n = 3 * period;
	double worst = 0.0, sum = 0.0;
	bool slipped = false;
	for (int i = 0; i < n; i++) {
		const double want = 2.0 * SIMPLE_Z * v0 * (1 + i / nb + i / nn);
		s.setBow(1000.f, v0);
		s.process();
		slipped = slipped || !s.sticking();
		worst = std::fmax(worst, std::fabs(s.frictionForce() - want) / want);
		sum += s.frictionForce();
	}
	check(!slipped, "  the bow never lets go", slipped ? 1.0 : 0.0);
	check(worst < 1e-4, "  three periods of steps against the count of returns, worst error (share)",
		worst);
	// (3) is a line along the steps' corners, so it is the slope that has to agree: fitted over the
	// three periods, against (3)'s 2 Z v0 / (T beta (1 - beta)).
	(void) sum;
	{
		BowedString r = simple(beta);
		double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
		for (int i = 0; i < n; i++) {
			r.setBow(1000.f, v0);
			r.process();
			const double x = i / RATE, y = r.frictionForce();
			sx += x; sy += y; sxx += x * x; sxy += x * y;
		}
		const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
		const double T = 1.0 / simpleHz(beta);
		const double line = 2.0 * SIMPLE_Z * v0 / (T * beta * (1.0 - beta));
		check(std::fabs(slope - line) / line < 0.02, "  its slope against Guettler's (3) (share)",
			(slope - line) / line);
	}
}


static void firstSlipSection() {
	std::printf("\nTHE FIRST SLIP of an accelerating bow, against Guettler's (5b): "
		"t = sqrt(T beta (1 - beta) F mu_s / (a Z)).\n");
	const float beta = 1.f / 9.f;
	const double T = 1.0 / simpleHz(beta);
	double worst = 0.0;
	for (float f : {0.25f, 0.5f, 1.0f})
		for (float a : {1.f, 3.f, 6.f}) {
			BowedString s = simple(beta);
			const Attack r = attack(s, f, a, simpleHz(beta), 2);
			const double want = std::sqrt(T * beta * (1.0 - beta) * f * 1.0 / (a * SIMPLE_Z));
			const double err = (r.firstSlip - want) / want;
			worst = std::fmax(worst, std::fabs(err));
			std::printf("    %.2f N  %.0f m/s2: %6.2f ms against %6.2f\n", f, a, r.firstSlip * 1e3, want * 1e3);
		}
	// The equation smooths the friction's steps into a line, so it is exact only on average.
	check(worst < 0.05, "  worst difference (share)", worst);
}


static const float GUETTLER_FORCES_FROM = 0.05f, GUETTLER_FORCE_STEP = 0.05f;
static const int GUETTLER_ROWS = 20;
static const float GUETTLER_ACCEL_FROM = 0.5f, GUETTLER_ACCEL_STEP = 0.5f;
static const int GUETTLER_COLUMNS = 18;


static void guettlerSection() {
	std::printf("\nGUETTLER'S VERIFICATION (his Figure 10): the simple string, 0.05 to 1 N up the page, "
		"0.5 to 9 m/s2 across.\n");
	std::printf("  Left, the simulation: . a perfect attack, a digit the periods before regular "
		"triggering, # more.\n  Right, his limits A to D: . inside the wedge.\n");
	int inside = 0, insidePerfect = 0, outside = 0, outsidePerfect = 0, perfectAll = 0, cells = 0;
	for (int n = 7; n <= 12; n++) {
		const float beta = 1.f / (float) n;
		const double T = 1.0 / simpleHz(beta);
		std::printf("\n  beta 1/%d\n", n);
		std::vector<std::string> left(GUETTLER_ROWS), right(GUETTLER_ROWS);
		for (int row = 0; row < GUETTLER_ROWS; row++) {
			const float f = GUETTLER_FORCES_FROM + GUETTLER_FORCE_STEP * (float) row;
			for (int col = 0; col < GUETTLER_COLUMNS; col++) {
				const float a = GUETTLER_ACCEL_FROM + GUETTLER_ACCEL_STEP * (float) col;
				BowedString s = simple(beta);
				const Attack r = attack(s, f, a, 1.0 / T, 12);
				char c = '#';
				if (r.periodsBefore == 0) c = '.';
				else if (r.periodsBefore > 0 && r.periodsBefore < 10) c = (char) ('0' + r.periodsBefore);
				left[row] += c;
				const Limits L = guettlerLimits(f, beta, T, SIMPLE_Z, 1.0, r.muFirst, r.muLater);
				const bool in = insideWedge(L, a);
				right[row] += in ? '.' : ' ';
				cells++;
				if (r.periodsBefore == 0) perfectAll++;
				if (in) { inside++; if (r.periodsBefore == 0) insidePerfect++; }
				else { outside++; if (r.periodsBefore == 0) outsidePerfect++; }
			}
		}
		for (int row = GUETTLER_ROWS; row-- > 0; )
			std::printf("  %4.2f N  %s   %s\n", GUETTLER_FORCES_FROM + GUETTLER_FORCE_STEP * row,
				left[row].c_str(), right[row].c_str());
	}
	std::printf("\n");
	const double hit = inside ? (double) insidePerfect / inside : 0.0;
	const double miss = outside ? (double) outsidePerfect / outside : 0.0;
	std::printf("  perfect attacks: %d of %d cells\n", perfectAll, cells);
	check(hit > 0.8, "  inside his wedge, share perfect", hit);
	check(miss < 0.1, "  outside it, share perfect", miss);
}


// ---- THE MODELLED CELLO -----------------------------------------------------------------------

static const double CELLO_D3 = 146.83;

static void attacksSection() {
	std::printf("\nATTACKS ON THE MODELLED CELLO D3 from rest: force up the page, acceleration across.\n");
	std::printf("  . perfect, a digit the periods before regular triggering where that is within the\n"
		"  listening limits, c creaks (prolonged periods past 50 ms), s slips (multiple slipping past\n"
		"  90 ms).\n");
	const int rows = 16, cols = 16;
	auto forceAt = [&](int row) { return 0.1f * std::pow(40.f, (float) row / (rows - 1)); };   // 0.1..4 N
	auto accelAt = [&](int col) { return 0.25f * std::pow(16.f, (float) col / (cols - 1)); };  // 0.25..4
	for (float beta : {0.0899f, 1.f / 6.f}) {
		std::printf("\n  beta %.4f (%.1f cm from the bridge on a 69 cm string)\n", beta, beta * 69.f);
		std::printf("  %7s  ", "m/s2");
		for (int col = 0; col < cols; col += 3)
			std::printf("%-6.2f", accelAt(col));
		std::printf("\n");
		int perfect = 0, good = 0, all = 0;
		double periodsSum = 0.0;
		std::vector<std::string> lines(rows);
		for (int row = 0; row < rows; row++)
			for (int col = 0; col < cols; col++) {
				BowedString s = cello((float) CELLO_D3, beta);
				const Attack r = attack(s, forceAt(row), accelAt(col), CELLO_D3, 25);
				const Grade g = grade(r);
				char c = g == CREAKS ? 'c' : g == SLIPS ? 's' : '.';
				if (g == ACCEPTABLE)
					c = r.periodsBefore < 10 ? (char) ('0' + r.periodsBefore) : '+';
				lines[row] += c;
				lines[row] += ' ';
				all++;
				if (g == PERFECT) perfect++;
				if (g == PERFECT || g == ACCEPTABLE) { good++; periodsSum += r.periodsBefore; }
			}
		for (int row = rows; row-- > 0; )
			std::printf("  %5.2f N  %s\n", forceAt(row), lines[row].c_str());
		std::printf("  perfect %d, within the listening limits %d, of %d; mean transient of those %.1f "
			"periods\n", perfect, good, all, good ? periodsSum / good : 0.0);
	}

	// THE SAME GRID UNDER TWO FRICTION CURVES, with the string's twist and without. The published
	// curve the model plays with, fitted to steady bowing (Woodhouse and Galluzzo), against the
	// steeper one Smith and Woodhouse measured for rosin sliding steadily, 1.2 falling to 0.35, here
	// as the hyperbola closest to their fit. Counted: perfect attacks, and those within the limits.
	std::printf("\n  The whole grid, perfect / within the limits, by friction curve and twist:\n");
	std::printf("  %-34s", "");
	const float betas[] = {0.0899f, 0.1f, 0.125f, 1.f / 6.f};
	for (float beta : betas)
		std::printf("   beta %.3f", beta);
	std::printf("\n");
	struct Curve { float s, d, v; const char* name; };
	const Curve curves[] = {{0.8f, 0.4f, 0.2f, "0.8 to 0.4 over 0.2 m/s"},
	                        {1.2f, 0.35f, 0.02f, "1.2 to 0.35 over 0.02 m/s"}};
	for (const Curve& c : curves)
		for (float twist : {3.27f, 0.f}) {
			std::printf("  %-26s %s", c.name, twist > 0.f ? "twist   " : "no twist");
			for (float beta : betas) {
				int perfect = 0, good = 0;
				for (int row = 0; row < rows; row++)
					for (int col = 0; col < cols; col++) {
						BowedString s = cello((float) CELLO_D3, beta);
						s.setFriction(c.s, c.d, c.v);
						s.setTorsion(twist);
						const Grade g = grade(attack(s, forceAt(row), accelAt(col), CELLO_D3, 25));
						if (g == PERFECT) perfect++;
						if (g == PERFECT || g == ACCEPTABLE) good++;
					}
				std::printf("   %4d / %-4d", perfect, good);
			}
			std::printf("\n");
		}
}


/** THE BRISTLES AS MATUSIAK AND CHATZIIOANNOU FITTED THEM to their cello G string (2024): the S curve
of their Table III, and their bow hair, whose stiffness and damping per metre of ribbon are taken
over a centimetre-wide ribbon. */
static void publishedBristles(BowedString& s) {
	BowedString::Bristles b;
	b.stiffness = 3.186e5f;
	b.damping = 0.0027f;
	b.muStatic = 1.0207f;
	b.muCoulomb = 0.5071f;
	b.fall = 0.228f;
	b.gaussian = true;
	s.setBristles(b);
}


// ---- SCHELLENG --------------------------------------------------------------------------------

/** A STRING ALREADY IN HELMHOLTZ MOTION, a tenth of the way from the bridge, to start every cell of
Schelleng's diagram from — as Galluzzo started each of his. Brought there once by bowing a newton
at 0.2 m/s, scaled with the speed, reached over 50 ms, and held for a second; whether that worked
is checked, since how a note starts is the other diagram's question. */
static bool helmholtzStart(double hz, float speed, BowedString* out, float* startForce) {
	// A newton on the cello's D at 0.2 m/s, scaled with the string's impedance and the speed.
	const float scale = impedanceOf((float) hz) / 0.67f * speed / 0.2f;
	for (float force : {1.f, 0.5f, 2.f, 0.25f})
		for (float rise : {0.05f, 0.3f}) {
			BowedString s = cello((float) hz, 0.1f);
			const Bowing b = bow(s, force * scale, speed, 1.5f, rise);
			const Helmholtz h = helmholtz(b, hz, 1.1, 1.5);
			if (h.periods > 0 && h.periodsWithOne == h.periods) {
				*out = s;
				*startForce = force * scale;
				return true;
			}
		}
	return false;
}


/** WHAT A HELD NOTE SETTLES INTO at one force and bow position: the string in Helmholtz motion at a
tenth of the length, moved to the cell over 0.15 s, held, and the last 0.4 s read. 'H' Helmholtz,
a digit that many slips a period, '-' irregular. */
static char heldMotion(const BowedString& start, double hz, float f0, float beta, float force, float speed) {
	BowedString s = start;
	const size_t n = (size_t) (1.0 * RATE);
	std::vector<uint8_t> stuck(n);
	SlipWatch watch;
	for (size_t i = 0; i < n; i++) {
		const float t = (float) i / RATE;
		const float m = std::fmin(1.f, t / 0.15f);
		s.setContact(0.1f * std::pow(beta / 0.1f, m));
		const float fNow = f0 * std::pow(force / f0, m);
		s.setBow(fNow, speed);
		s.process();
		const bool slips = watch.update(s, fNow, speed);
		stuck[i] = (s.bristling() ? !slips : s.sticking()) ? 1 : 0;
	}
	const double period = RATE / hz;
	int first = -1;
	bool same = true;
	for (double t0 = 0.6 * RATE; t0 + period <= (double) n; t0 += period) {
		int k = 0;
		for (size_t i = (size_t) t0 + 1; i < (size_t) (t0 + period); i++)
			if (stuck[i - 1] && !stuck[i])
				k++;
		if (first < 0)
			first = k;
		same = same && k == first;
	}
	if (!same)
		return '-';
	if (first == 1) {
		// ONE SLIP A PERIOD IS NOT ENOUGH: under too heavy a bow the string holds on past the
		// corner and the period stretches, the flattening that comes before the crunch. More than
		// fifty cents of it is not Helmholtz motion.
		std::vector<size_t> slips;
		for (size_t i = (size_t) (0.6 * RATE) + 1; i < n; i++)
			if (stuck[i - 1] && !stuck[i])
				slips.push_back(i);
		if (slips.size() > 2) {
			const double mean = (double) (slips.back() - slips.front()) / (slips.size() - 1);
			if (mean > 1.03 * period)
				return 'f';
		}
		return 'H';
	}
	return first < 10 ? (char) ('0' + first) : '+';
}


static void schellengSection() {
	// GALLUZZO'S MEASURED LIMITS for a cello D string bowed at 0.05 m/s by a rosined rod, fitted by
	// eye over beta 0.02 to 0.1: above 0.13 / beta N it crunches, below 0.00086 / beta^2 it slips
	// more than once a period. Both scale with the bow speed. His string was not ours and his rod
	// was not a bow, so the test is a factor of three either way, and the middle of his band has to
	// play. Tested where he measured; further from the bridge his lines are printed for comparison
	// only, and past about 0.17 is further than cellists bow (Schoonderwaldt's players: 2.4 to 12 cm
	// on a 69 cm string).
	// Under the friction curve, checked; under the bristles fitted to the cello G, printed alongside.
	for (int model = 0; model < 2; model++)
	for (float speed : {0.05f, 0.2f}) {
		benchFriction = model ? publishedBristles : nullptr;
		std::printf("\nSCHELLENG'S DIAGRAM, the modelled cello D3 at %.2f m/s, %s: force across, bow position "
			"down.\n  H Helmholtz motion, f flattened past fifty cents, a digit that many slips a period, - "
			"irregular, 0 no flyback at all.\n", speed, model ? "bristles (their S fit), not checked"
			: "the friction curve");
		const float forces[] = {0.01f, 0.02f, 0.03f, 0.05f, 0.1f, 0.2f, 0.35f, 0.5f, 1.f, 2.f, 4.f,
			6.f, 8.f, 12.f, 16.f};
		const int nf = (int) (sizeof forces / sizeof forces[0]);
		const float betas[] = {0.02f, 0.03f, 0.05f, 0.08f, 0.1f, 0.12f, 0.15f, 0.18f, 0.22f, 0.25f, 0.3f};
		std::printf("  %-6s", "beta");
		for (float f : forces)
			std::printf("%6.2f", f);
		std::printf("   measured, scaled to this speed\n");
		const double k = speed / 0.05;
		BowedString start;
		float f0 = 0.f;
		if (!helmholtzStart(CELLO_D3, speed, &start, &f0)) {
			check(false, "  the string reaches Helmholtz motion to start from", 0.0);
			continue;
		}
		int rows = 0, within = 0, middles = 0;
		const float measuredTo = 0.1f;
		for (float be : betas) {
			const double lower = k * 0.00086 / (be * be), upper = k * 0.13 / be;
			std::printf("  %-6.2f", be);
			char cells[16];
			for (int j = 0; j < nf; j++) {
				cells[j] = heldMotion(start, CELLO_D3, f0, be, forces[j], speed);
				std::printf("%6c", cells[j]);
			}
			// The run of Helmholtz motion that holds the middle of the measured band, or the
			// nearest to it.
			const double middle = std::sqrt(lower * upper);
			int at = 0;
			for (int j = 0; j < nf; j++)
				if (std::fabs(std::log(forces[j] / middle)) < std::fabs(std::log(forces[at] / middle)))
					at = j;
			const bool plays = cells[at] == 'H';
			int lo = at, hi = at;
			while (lo > 0 && cells[lo - 1] == 'H') lo--;
			while (hi + 1 < nf && cells[hi + 1] == 'H') hi++;
			const bool fits = plays && forces[lo] > lower / 3.0 && forces[lo] < lower * 3.0
				&& forces[hi] > upper / 3.0 && forces[hi] < upper * 3.0;
			const bool measured = be <= measuredTo + 1e-4f;
			std::printf("   %.2f to %.1f N%s%s\n", lower, upper, measured ? "" : " (beyond his measurements)",
				!plays ? "   the middle does not play" : fits ? "" : "   a limit is out by more than three");
			if (!measured)
				continue;
			rows++;
			if (plays) middles++;
			if (fits) within++;
		}
		if (model) {
			std::printf("  measured positions where the middle plays %d, with both limits within three "
				"times %d, of %d\n", middles, within, rows);
			continue;
		}
		char what[96];
		std::snprintf(what, sizeof what, "  at %.2f m/s, measured positions where the middle plays, of %d",
			speed, rows);
		check(middles == rows, what, middles);
		std::snprintf(what, sizeof what, "  at %.2f m/s, measured positions with both limits within three "
			"times, of %d", speed, rows);
		check(within >= rows - 1, what, within);
	}
	benchFriction = nullptr;
}


// ---- AGAINST A MEASURED GUETTLER DIAGRAM ----------------------------------------------------

/** MATUSIAK AND CHATZIIOANNOU'S CELLO G STRING (2024), as they give it: 98 Hz, 149.6 N on a 0.5 mm
radius at 10059 kg/m^3, so 1.087 kg/s; the fundamental at Q 200 (their sigma0, 1.537/s) and a
partial at 2 kHz at about Q 84 (their sigma1); the twist 1.43 kg/s (sqrt(K_T theta) / r^2), 6.19
times as fast, Q 29; bowed 0.0786 of the way from the bridge. */
static BowedString celloG() {
	BowedString s = cello(98.f, 0.0786f, 1.087f);
	s.setDecay(6.91f * 200.f / (3.14159265f * 98.f));
	s.setDamping(0.633f);
	s.setTorsion(1.31f, 6.19f, 29.f);
	return s;
}


static void measuredSection() {
	std::printf("\nAGAINST A MEASURED GUETTLER DIAGRAM: their cello G string, 0.43 to 4.1 N up the page, 0.15 to "
		"3.15 m/s2 across,\n  30 by 30 as they measured it with a robot arm and a real bow "
		"(research/bowed-strings/guettler-measured-cello-g.txt).\n  @ playable in both, o measured only, + "
		"modelled only, . neither, ? not readable on the figure. Playable: Helmholtz motion within 20 periods.\n");
	std::vector<int> target;
	{
		FILE* f = std::fopen("research/bowed-strings/guettler-measured-cello-g.txt", "r");
		if (!f) {
			check(false, "  the measured diagram is readable", 0.0);
			return;
		}
		char line[1024];
		while (std::fgets(line, sizeof line, f)) {
			if (line[0] == '#')
				continue;
			char* p = line;
			char* end;
			for (long v = std::strtol(p, &end, 10); end != p; v = std::strtol(p, &end, 10)) {
				target.push_back((int) v);
				p = end;
			}
		}
		std::fclose(f);
	}
	if (target.size() != 900) {
		check(false, "  the measured diagram has 900 cells", (double) target.size());
		return;
	}
	// THE TWO FRICTION MODELS, the curve the string plays with and the bristles with the values
	// Matusiak and Chatziioannou fitted to this string (their S curve, Table III, and their hair).
	struct Model { const char* name; void (*apply)(BowedString&); };
	const Model models[] = {{"the friction curve, 0.8 to 0.4 over 0.2 m/s", nullptr},
	                        {"bristles, their S fit, with the hair", publishedBristles}};
	for (const Model& m : models) {
		benchFriction = m.apply;
		std::printf("\n  %s:\n", m.name);
		int both = 0, either = 0, measured = 0, modelled = 0, falseAlarm = 0, negatives = 0;
		for (int row = 0; row < 30; row++) {
			std::printf("  %4.2f N  ", 4.1f - (4.1f - 0.43f) * row / 29.f);
			for (int col = 0; col < 30; col++) {
				const float f = 4.1f - (4.1f - 0.43f) * row / 29.f, a = 0.15f + 3.0f * col / 29.f;
				BowedString s = celloG();
				const Attack r = attack(s, f, a, 98.0, 25);
				const bool ours = r.periodsBefore >= 0 && r.periodsBefore < 20;
				const int t = target[row * 30 + col];
				if (t < 0) {
					std::printf("?");
					continue;
				}
				const bool theirs = t < 20;
				std::printf("%c", ours ? (theirs ? '@' : '+') : (theirs ? 'o' : '.'));
				measured += theirs;
				modelled += ours;
				both += ours && theirs;
				either += ours || theirs;
				if (!theirs) {
					negatives++;
					falseAlarm += ours;
				}
			}
			std::printf("\n");
		}
		std::printf("  playable: measured %d, modelled %d, both %d. The model plays %.0f%% of the measured "
			"playable cells, and %.0f%% of the unplayable ones; overlap %.2f.\n", measured, modelled, both,
			100.0 * both / std::max(1, measured), 100.0 * falseAlarm / std::max(1, negatives),
			either ? (double) both / either : 0.0);
	}
	benchFriction = nullptr;
}


// ---- A PLAYER WHO REHEARSES ------------------------------------------------------------------

/** HOW HARD STARTING NOTES IS FOR A PLAYER, under the friction the string plays with: 210 notes, the
cello's four strings and the violin's G, A and E, at five bow positions and six forces across the
upper half of each position's Schelleng band at 0.2 m/s (Galluzzo's limits, scaled by impedance).
First one acceleration for every note, scaled as Guettler's limits scale, beta (1 - beta) F / (T Z);
then the player of bowbench.hpp, who rehearses each note on a copy of the string — 168 attacks,
the smoothest of those that turn regular within five periods, and where none turns regular within
three, 2,080 more at four other bow positions — and that attack played through, where it was
found, easing to the intended force once four regular periods are heard. */
static void rehearseSection() {
	std::printf("\nA PLAYER WHO REHEARSES: 210 notes from rest, seven strings, beta 0.06 to 0.15, six forces each.\n");
	struct Note { double hz; const char* name; };
	const Note notes[] = {{65.41, "cello C2"}, {98.0, "cello G2"}, {146.83, "cello D3"}, {220.0, "cello A3"},
	                      {196.0, "violin G3"}, {440.0, "violin A4"}, {659.26, "violin E5"}};
	const float betas[] = {0.06f, 0.08f, 0.1f, 0.125f, 0.15f};
	int fixedGood = 0, within5 = 0, within10 = 0, kept = 0, all = 0, moved = 0;
	for (const Note& n : notes) {
		int missed = 0;
		for (float beta : betas)
			for (int fi = 0; fi < 6; fi++) {
				const double z = impedanceOf((float) n.hz), zBow = z * 3.27 / 4.27, T = 1.0 / n.hz;
				const double upper = 4.0 * 0.13 / beta * (z / 0.67);
				const double lower = 4.0 * 0.00086 / (beta * beta) * (z / 0.67) * (z / 0.67);
				const double middle = std::sqrt(lower * upper);
				const double f = middle * std::pow(upper / middle, fi / 6.0);
				auto accel = [&](double k, double force) { return (float) (k * beta * (1.0 - beta) * force / (T * zBow)); };
				all++;
				{
					BowedString s = cello((float) n.hz, beta);
					const Grade g = grade(attack(s, (float) f, accel(0.024, f), n.hz, 25));
					fixedGood += (g == PERFECT || g == ACCEPTABLE);
				}
				const Rehearsed chose = rehearseAttack(n.hz, -1.f, beta, (float) f, 0.2f);
				within5 += chose.periods >= 0 && chose.periods <= 5;
				within10 += chose.periods >= 0 && chose.periods <= 10;
				moved += chose.beta != beta;
				if (chose.periods < 0 || chose.periods > 10) {
					missed++;
					continue;
				}
				// THE BEST ATTACK PLAYED THROUGH: bite, then ease to the intended force over EASE_SECONDS
				// once four regular periods are heard; held to a second and a half, the last 0.3 s read.
				const double bite = f * chose.bite;
				const float a = chose.accel;
				BowedString s = cello((float) n.hz, chose.beta);
				const double period = RATE / n.hz;
				std::vector<size_t> slips;
				std::vector<uint8_t> stuck((size_t) (1.5 * RATE));
				double easeFrom = -1.0;
				bool was = true;
				for (size_t i = 0; i < stuck.size(); i++) {
					const double t = i / RATE;
					const double force = easeFrom < 0.0 ? bite : bite + (f - bite) * std::fmin(1.0, (t - easeFrom) / EASE_SECONDS);
					s.setBow((float) force, (float) std::fmin(a * t, 0.2));
					s.process();
					stuck[i] = s.sticking() ? 1 : 0;
					if (i && was && !stuck[i]) {
						slips.push_back(i);
						if (easeFrom < 0.0 && slips.size() >= 5) {
							bool regular = true;
							for (size_t q = slips.size() - 4; q < slips.size(); q++)
								regular = regular && std::fabs((double) (slips[q] - slips[q - 1]) - period) <= 0.1 * period;
							if (regular)
								easeFrom = t;
						}
					}
					was = stuck[i] != 0;
				}
				bool held = true;
				for (double t0 = 1.2 * RATE; t0 + period <= 1.5 * RATE; t0 += period) {
					int k = 0;
					for (size_t i = (size_t) t0 + 1; i < (size_t) (t0 + period); i++)
						if (stuck[i - 1] && !stuck[i])
							k++;
					held = held && k == 1;
				}
				kept += held;
			}
		std::printf("  %-10s rehearsed, no start within ten periods: %d of 30\n", n.name, missed);
	}
	std::printf("  one acceleration for every note: %d of %d within the listening limits\n", fixedGood, all);
	std::printf("  rehearsed: %d start within five periods, %d within ten, %d of them with the bow moved; of those,\n"
		"  %d still in Helmholtz motion after easing to the intended force\n", within5, within10, moved, kept);
	check(within10 >= (3 * all) / 4, "  rehearsed notes that start within ten periods, share", (double) within10 / all);
	check(kept >= (9 * within10) / 10, "  of those, share that keep Helmholtz motion at the intended force",
		within10 ? (double) kept / within10 : 0.0);
}


// ---- PITCH, AND THE OVERSAMPLING SCHEDULE -----------------------------------------------------

/** SCHELLENG'S LIMITS BY RAMPS, as a bowing machine finds them: Helmholtz motion started in the
middle of the band, then the force slid down, and separately up, an octave every 0.3 s until three
periods running are not one slip each or the period has stretched past three per cent. Steadier
than a grid of separate notes, whose glides into each cell are chaotic enough to scatter stray
cells across the map. -1 where the limit was not reached, or the note would not start. */
struct RampLimits { double lower = -1.0, upper = -1.0; bool started = false; };

static RampLimits rampLimits(double hz, float beta, float speed, double middle) {
	RampLimits r;
	BowedString start;
	double f0 = middle;
	for (double m : {1.0, 2.0, 0.5, 4.0, 0.25}) {
		BowedString s = cello((float) hz, beta);
		const Bowing b = bow(s, (float) (middle * m), speed, 1.2f);
		const Helmholtz h = helmholtz(b, hz, 0.9, 1.2);
		if (h.periods > 0 && h.periodsWithOne == h.periods) {
			start = s;
			f0 = middle * m;
			r.started = true;
			break;
		}
	}
	if (!r.started)
		return r;
	const double period = RATE / hz;
	for (int dir : {-1, 1}) {
		BowedString s = start;
		bool was = s.sticking();
		std::vector<size_t> slips;
		int count = 0, bad = 0;
		double found = -1.0;
		for (size_t i = 0; i < (size_t) (3.0 * RATE) && found < 0.0; i++) {
			const double force = f0 * std::pow(2.0, dir * (i / RATE) / 0.3);
			s.setBow((float) force, speed);
			s.process();
			const bool stuck = s.sticking();
			if (i && was && !stuck)
				slips.push_back(i);
			was = stuck;
			if (++count < (int) period)
				continue;
			count = 0;
			int k = 0;
			for (size_t q = slips.size(); q-- > 0 && slips[q] + (size_t) period > i; )
				k++;
			const bool stretched = k == 1 && slips.size() >= 2
				&& slips[slips.size() - 2] + 2 * (size_t) period > i
				&& (double) (slips.back() - slips[slips.size() - 2]) > 1.03 * period;
			bad = (k != 1 || stretched) ? bad + 1 : 0;
			if (bad >= 3)
				found = force;
		}
		(dir < 0 ? r.lower : r.upper) = found;
	}
	return r;
}


/** HOW FAR APART TWO SETS OF LIMITS ARE, in octaves of force: the median over the bow positions of
the larger of the two limits' differences, so one stray position does not decide it. */
static double limitsApart(const std::vector<RampLimits>& a, const std::vector<RampLimits>& b) {
	std::vector<double> d;
	for (size_t i = 0; i < a.size(); i++) {
		if (a[i].lower <= 0.0 || b[i].lower <= 0.0 || a[i].upper <= 0.0 || b[i].upper <= 0.0) {
			d.push_back(a[i].started == b[i].started ? 0.0 : 9.0);
			continue;
		}
		d.push_back(std::fmax(std::fabs(std::log2(a[i].lower / b[i].lower)), std::fabs(std::log2(a[i].upper / b[i].upper))));
	}
	std::sort(d.begin(), d.end());
	return d.empty() ? 0.0 : (d.size() % 2 ? d[d.size() / 2] : 0.5 * (d[d.size() / 2 - 1] + d[d.size() / 2]));
}


static void oversamplingSection() {
	std::printf("\nPITCH, AND THE OVERSAMPLING SCHEDULE: Schelleng's limits by ramps at 0.2 m/s, eleven bow positions from\n"
		"  0.04 to 0.16, at one to thirty-two times 48 kHz; against the median of eight, sixteen and thirty-two times.\n"
		"  In octaves of force, the median over the positions. * the schedule's factor, BowedString::oversampling.\n");
	struct Note { double hz; const char* name; };
	const Note notes[] = {{220.0, "cello A3"}, {196.0, "violin G3"}, {293.66, "violin D4"}, {440.0, "violin A4"},
	                      {659.26, "violin E5"}, {1318.51, "violin E6"}, {2637.02, "violin E7"}};
	// ELEVEN POSITIONS, so the median is not decided by the one or two where the motion breaks in
	// one of two ways and a ramp lands on either: the violin's G does that at 0.06 and 0.08.
	const float betas[] = {0.04f, 0.05f, 0.06f, 0.07f, 0.08f, 0.09f, 0.1f, 0.11f, 0.12f, 0.14f, 0.16f};
	const int nb = (int) (sizeof betas / sizeof betas[0]);
	const int factors[] = {1, 2, 4, 8, 16, 32};
	const float speed = 0.2f;
	const float base = RATE;
	std::printf("  %-10s %8s", "", "samples");
	for (int f : factors)
		std::printf("   x%-4d", f);
	std::printf("\n");
	int scheduledGood = 0, highNeeded = 0;
	for (const Note& n : notes) {
		const double zs = impedanceOf((float) n.hz) / 0.67;
		std::vector<std::vector<RampLimits>> got(6, std::vector<RampLimits>(nb));
		for (int fi = 0; fi < 6; fi++) {
			RATE = base * factors[fi];
			std::vector<std::thread> ts;
			for (int bi = 0; bi < nb; bi++)
				ts.emplace_back([&, fi, bi]() {
					const double lower = 4.0 * 0.00086 / (betas[bi] * betas[bi]) * zs * zs;
					const double upper = 4.0 * 0.13 / betas[bi] * zs;
					got[fi][bi] = rampLimits(n.hz, betas[bi], speed, std::sqrt(lower * upper));
				});
			for (auto& t : ts)
				t.join();
		}
		RATE = base;
		// THE REFERENCE: position by position, the median of the three fastest rates' limits.
		std::vector<RampLimits> ref(nb);
		for (int bi = 0; bi < nb; bi++) {
			double lo[3], hi[3];
			int k = 0;
			for (int fi = 3; fi < 6; fi++)
				if (got[fi][bi].lower > 0.0 && got[fi][bi].upper > 0.0) {
					lo[k] = got[fi][bi].lower;
					hi[k] = got[fi][bi].upper;
					k++;
				}
			if (k == 0)
				continue;
			std::sort(lo, lo + k);
			std::sort(hi, hi + k);
			ref[bi].lower = lo[k / 2];
			ref[bi].upper = hi[k / 2];
			ref[bi].started = true;
		}
		const int scheduled = BowedString::oversampling((float) n.hz, base);
		std::printf("  %-10s %8.0f", n.name, base / n.hz);
		for (int fi = 0; fi < 6; fi++) {
			const double apart = limitsApart(got[fi], ref);
			std::printf("  %5.2f%c", apart, factors[fi] == scheduled ? '*' : ' ');
			if (factors[fi] == scheduled && apart <= 0.2)
				scheduledGood++;
			if (factors[fi] == 1 && n.hz > 1000.0 && apart > 0.5)
				highNeeded++;
		}
		std::printf("\n");
	}
	check(scheduledGood == (int) (sizeof notes / sizeof notes[0]),
		"  notes within a fifth of an octave at the schedule's factor", scheduledGood);
	check(highNeeded == 2, "  the high E's notes more than half an octave out without it", highNeeded);
	std::printf("  the schedule: ");
	for (double hz : {100.0, 200.0, 400.0, 800.0, 1600.0, 2640.0})
		std::printf("%.0f Hz x%d  ", hz, BowedString::oversampling((float) hz, base));
	std::printf("\n");
}


// ---- TO LISTEN TO -----------------------------------------------------------------------------

static void listenSection() {
	std::printf("\nTO LISTEN TO: three attacks on the cello D3 a tenth of the way from the bridge, found on "
		"the attacks grid,\n  each drawn at its acceleration up to 0.25 m/s and held for a second.\n");
	// THE CLEANEST START ON THE GRID, the creakiest and the one that slips longest.
	const int rows = 16, cols = 16;
	auto forceAt = [&](int row) { return 0.1f * std::pow(40.f, (float) row / (rows - 1)); };
	auto accelAt = [&](int col) { return 0.25f * std::pow(16.f, (float) col / (cols - 1)); };
	struct Take { float force = 0.f, accel = 0.f; double score = -1e9; Attack r; };
	Take clean, creaky, slipping;
	for (int row = 0; row < rows; row++)
		for (int col = 0; col < cols; col++) {
			BowedString s = cello((float) CELLO_D3, 0.1f);
			const Attack r = attack(s, forceAt(row), accelAt(col), CELLO_D3, 25, nullptr, 0.25f);
			const Grade g = grade(r);
			auto keep = [&](Take& t, double score) {
				if (score > t.score) { t.force = forceAt(row); t.accel = accelAt(col); t.score = score; t.r = r; }
			};
			if (g == PERFECT || g == ACCEPTABLE)
				keep(clean, -r.periodsBefore);
			else if (g == CREAKS)
				keep(creaky, r.seconds + 0.01 * forceAt(row));
			else
				keep(slipping, r.seconds);
		}
	std::vector<float> out;
	const char* names[] = {"clean", "creaky", "slipping"};
	const Take* takes[] = {&clean, &creaky, &slipping};
	for (int k = 0; k < 3; k++) {
		const Take& t = *takes[k];
		if (t.score <= -1e9) {
			std::printf("  no %s attack on the grid\n", names[k]);
			continue;
		}
		std::printf("  %-9s %.2f N at %.2f m/s2: %s\n", names[k], t.force, t.accel,
			t.r.periodsBefore >= 0 ? (std::to_string(t.r.periodsBefore) + " periods to regular").c_str()
			                       : "never regular in the run");
		BowedString s = cello((float) CELLO_D3, 0.1f);
		const size_t n = (size_t) (1.2 * RATE);
		for (size_t i = 0; i < n; i++) {
			const float time = (float) i / RATE;
			s.setBow(time < 1.0f ? t.force : 0.f, std::fmin(t.accel * time, 0.25f));
			out.push_back(s.process());
		}
		out.insert(out.end(), (size_t) (0.4 * RATE), 0.f);
	}
	normalise(out);
	writeWav("build/bowed-attacks.wav", out);

	// THE SAME ATTACKS UNDER BOTH FRICTION MODELS, on Matusiak and Chatziioannou's cello G string,
	// at three cells their robot found playable: each first with the friction curve, then with the
	// bristles. Drawn at its acceleration for 0.4 s, then held at the speed reached, for a second.
	std::printf("\n  On their cello G string, three attacks they measured as playable, each with the friction "
		"curve and then the bristles:\n");
	// Cells of the measured grid, by row from the top and column: a bowed attack is chaotic enough
	// that rounding the force to two places can change how it starts.
	struct Cell { float force, accel; };
	auto at = [](int row, int col) { return Cell{4.1f - (4.1f - 0.43f) * row / 29.f, 0.15f + 3.0f * col / 29.f}; };
	const Cell cells[] = {at(10, 5), at(2, 10), at(0, 12)};
	std::vector<float> both;
	for (const Cell& c : cells)
		for (int model = 0; model < 2; model++) {
			benchFriction = model ? publishedBristles : nullptr;
			BowedString probe = celloG();
			const Attack r = attack(probe, c.force, c.accel, 98.0, 25, nullptr, 0.4f * c.accel);
			std::printf("  %.1f N at %.2f m/s2, %-9s %s\n", c.force, c.accel, model ? "bristles:" : "curve:",
				r.periodsBefore >= 0 ? (std::to_string(r.periodsBefore) + " periods to Helmholtz motion").c_str()
				                     : "no Helmholtz motion in the run");
			BowedString s = celloG();
			const size_t n = (size_t) (1.2 * RATE);
			for (size_t i = 0; i < n; i++) {
				const float time = (float) i / RATE;
				s.setBow(time < 1.0f ? c.force : 0.f, std::fmin(c.accel * time, 0.4f * c.accel));
				both.push_back(s.process());
			}
			both.insert(both.end(), (size_t) (0.4 * RATE), 0.f);
		}
	benchFriction = nullptr;
	normalise(both);
	writeWav("build/bowed-attacks-friction.wav", both);
}


int main(int argc, char** argv) {
	const char* only = argc > 1 ? argv[1] : "";
	auto run = [&](const char* name) { return !*only || std::strcmp(only, name) == 0; };
	if (run("buildup")) buildupSection();
	if (run("firstslip")) firstSlipSection();
	if (run("guettler")) guettlerSection();
	if (run("attacks")) attacksSection();
	if (run("schelleng")) schellengSection();
	if (run("measured")) measuredSection();
	if (run("rehearse")) rehearseSection();
	if (run("oversampling")) oversamplingSection();
	if (run("listen")) listenSection();
	std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "all passed");
	return failures ? 1 : 0;
}
