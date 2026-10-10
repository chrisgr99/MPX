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
  listen     A clean start, a creaky one and a slipping one, to hear.

    make bowplay                      all of it
    make bowplay ARGS="attacks"       one section
*/
#include "bowbench.hpp"

#include <cstdlib>
#include <cstring>


// ---- ONE ATTACK -----------------------------------------------------------------------------

/** WHAT A NOTE STARTED FROM REST DID. The bow is put on the string at its force and drawn from
standing still at a constant acceleration, as in Guettler's analysis and in the measurements. */
struct Attack {
	/** When the string first let go of the hair, in seconds; -1 if it never did. */
	double firstSlip = -1.0;
	/** NOMINAL PERIODS FROM THE FIRST SLIP UNTIL THE TRIGGERING TURNS REGULAR — one slip every
	period from then to the end of the run. Nought is Guettler's perfect attack. -1 if it never
	turned regular within the run. */
	int periodsBefore = -1;
	/** What went wrong first, when something did: a period too long (the string held on past the
	release, which is the creak Guettler calls prolonged periods), or too short (it let go early,
	multiple slipping). */
	bool prolonged = false;
	/** The friction coefficient while slipping: in the first slip, and in the later slips before
	the first one's reflection comes back from the nut. What Guettler's limits are worked out with. */
	double muFirst = 0.0, muLater = 0.0;
	/** The string's speed against the bow just after it first lets go, in metres a second. */
	double slipSpeed = 0.0;
	/** How long the transient lasted, in seconds: the first slip to regular triggering. */
	double seconds = 0.0;
};


static Attack attack(BowedString& s, float force, float accel, double hz, int periodsAfter,
                     std::vector<float>* bridge = nullptr, float topSpeed = 1e9f) {
	Attack r;
	const double period = RATE / hz;
	std::vector<size_t> slips;
	std::vector<double> slipMu;            // mean friction coefficient, slip by slip
	double muSum = 0.0;
	int muN = 0;
	bool was = false;
	const size_t cap = (size_t) (3.0 * RATE);
	size_t end = cap;
	for (size_t i = 0; i < end; i++) {
		const float t = (float) i / RATE;
		s.setBow(force, std::fmin(accel * t, topSpeed));
		const float out = s.process();
		if (bridge)
			bridge->push_back(out);
		const bool stuck = s.sticking();
		// A slip starts where the string comes off the hair.
		if (i > 0 && was && !stuck) {
			if (!slips.empty())
				slipMu.push_back(muN ? muSum / muN : 0.0);
			slips.push_back(i);
			muSum = 0.0;
			muN = 0;
			if (slips.size() == 1) {
				end = std::min(cap, i + (size_t) (periodsAfter * period));
				r.slipSpeed = s.velocityAtBow() - std::fmin(accel * t, topSpeed);
			}
		}
		if (!stuck && !slips.empty()) {
			muSum += std::fabs(s.frictionForce()) / force;
			muN++;
		}
		was = stuck;
	}
	if (slips.empty())
		return r;
	r.firstSlip = (double) slips[0] / RATE;
	if (!slipMu.empty())
		r.muFirst = slipMu[0];
	// The later slips: those before the first one's reflection from the nut returns.
	{
		const double n = 1.0 / std::fmax(1e-3, (double) s.contact());
		double sum = 0.0;
		int k = 0;
		for (size_t j = 1; j < slipMu.size() && (double) j < n - 1.0; j++, k++)
			sum += slipMu[j];
		r.muLater = k ? sum / k : r.muFirst;
	}
	// REGULAR FROM SLIP k: every gap from there to the end within a tenth of a period, and at
	// least three of them, or the end of the run is not telling us anything.
	size_t k = slips.size();
	for (size_t j = slips.size(); j-- > 1; ) {
		const double gap = (double) (slips[j] - slips[j - 1]);
		if (std::fabs(gap - period) > 0.1 * period)
			break;
		k = j - 1;
	}
	if (k < slips.size() && slips.size() - k < 4)
		k = slips.size();
	if (k < slips.size()) {
		r.periodsBefore = (int) std::lround((double) (slips[k] - slips[0]) / period);
		r.seconds = (double) (slips[k] - slips[0]) / RATE;
	}
	// What went wrong first.
	const size_t upTo = (k < slips.size()) ? k : slips.size() - 1;
	for (size_t j = 1; j <= upTo; j++) {
		const double gap = (double) (slips[j] - slips[j - 1]);
		if (gap > 1.1 * period) { r.prolonged = true; break; }
		if (gap < 0.9 * period) { r.prolonged = false; break; }
	}
	if (k >= slips.size()) {
		// Never regular: what dominated, gaps too long or too short?
		int longer = 0, shorter = 0;
		for (size_t j = 1; j < slips.size(); j++) {
			const double gap = (double) (slips[j] - slips[j - 1]);
			if (gap > 1.1 * period) longer++;
			else if (gap < 0.9 * period) shorter++;
		}
		r.prolonged = longer >= shorter || slips.size() < 2;
		r.seconds = (double) (end - slips[0]) / RATE;
	}
	return r;
}


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

/** GUETTLER AND ASKENFELT'S LISTENING LIMITS (1997, as Guettler 2002 gives them): a violin attack
was heard as unacceptable when its multiple slipping lasted more than 90 milliseconds, or its
prolonged periods more than 50. Perfect is regular from the first period. */
enum Grade { PERFECT, ACCEPTABLE, CREAKS, SLIPS };

static Grade grade(const Attack& r) {
	if (r.periodsBefore == 0)
		return PERFECT;
	if (r.periodsBefore > 0 && r.seconds <= (r.prolonged ? 0.050 : 0.090))
		return ACCEPTABLE;
	return r.prolonged ? CREAKS : SLIPS;
}


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


// ---- SCHELLENG --------------------------------------------------------------------------------

/** A STRING ALREADY IN HELMHOLTZ MOTION, a tenth of the way from the bridge, to start every cell of
Schelleng's diagram from — as Galluzzo started each of his. Brought there once by bowing a newton
at 0.2 m/s, scaled with the speed, reached over 50 ms, and held for a second; whether that worked
is checked, since how a note starts is the other diagram's question. */
static bool helmholtzStart(float speed, BowedString* out, float* startForce) {
	for (float force : {1.f, 0.5f, 2.f, 0.25f})
		for (float rise : {0.05f, 0.3f}) {
			BowedString s = cello((float) CELLO_D3, 0.1f);
			const Bowing b = bow(s, force * speed / 0.2f, speed, 1.5f, rise);
			const Helmholtz h = helmholtz(b, CELLO_D3, 1.1, 1.5);
			if (h.periods > 0 && h.periodsWithOne == h.periods) {
				*out = s;
				*startForce = force * speed / 0.2f;
				return true;
			}
		}
	return false;
}


/** WHAT A HELD NOTE SETTLES INTO at one force and bow position: the string in Helmholtz motion at a
tenth of the length, moved to the cell over 0.15 s, held, and the last 0.4 s read. 'H' Helmholtz,
a digit that many slips a period, '-' irregular. */
static char heldMotion(const BowedString& start, float f0, float beta, float force, float speed) {
	BowedString s = start;
	const size_t n = (size_t) (1.0 * RATE);
	std::vector<uint8_t> stuck(n);
	for (size_t i = 0; i < n; i++) {
		const float t = (float) i / RATE;
		const float m = std::fmin(1.f, t / 0.15f);
		s.setContact(0.1f * std::pow(beta / 0.1f, m));
		s.setBow(f0 * std::pow(force / f0, m), speed);
		s.process();
		stuck[i] = s.sticking() ? 1 : 0;
	}
	const double period = RATE / CELLO_D3;
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
	for (float speed : {0.05f, 0.2f}) {
		std::printf("\nSCHELLENG'S DIAGRAM, the modelled cello D3 at %.2f m/s: force across, bow position "
			"down.\n  H Helmholtz motion, f flattened past fifty cents, a digit that many slips a period, - "
			"irregular.\n", speed);
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
		if (!helmholtzStart(speed, &start, &f0)) {
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
				cells[j] = heldMotion(start, f0, be, forces[j], speed);
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
		char what[96];
		std::snprintf(what, sizeof what, "  at %.2f m/s, measured positions where the middle plays, of %d",
			speed, rows);
		check(middles == rows, what, middles);
		std::snprintf(what, sizeof what, "  at %.2f m/s, measured positions with both limits within three "
			"times, of %d", speed, rows);
		check(within >= rows - 1, what, within);
	}
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
}


int main(int argc, char** argv) {
	const char* only = argc > 1 ? argv[1] : "";
	auto run = [&](const char* name) { return !*only || std::strcmp(only, name) == 0; };
	if (run("buildup")) buildupSection();
	if (run("firstslip")) firstSlipSection();
	if (run("guettler")) guettlerSection();
	if (run("attacks")) attacksSection();
	if (run("schelleng")) schellengSection();
	if (run("listen")) listenSection();
	std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "all passed");
	return failures ? 1 : 0;
}
