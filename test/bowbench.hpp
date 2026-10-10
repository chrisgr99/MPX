#pragma once
/** WHAT THE BOWED-STRING BENCHES SHARE: making a string, bowing it, and measuring what it did.
test/bowtest.cpp tests the string and the bow, phases 1 and 2 of docs/bowed-strings.md;
test/bowplay.cpp sweeps where it plays and how notes start, phase 3. Each includes this once. */
#include "../src/BowedString.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace px;


/** THE SAMPLE RATE THE BENCHES RUN AT. A variable, so a section can oversample: phase 4 runs the same
diagrams at two, four, eight and sixteen times. */
static float RATE = 48000.f;

/** HOW LONG A PLAYER TAKES TO RELAX FROM THE BITE INTO THE NOTE, in seconds: the attack starts with
more force than the note wants (phase 3's rehearsal) and eases to it once four regular periods are
heard. Chosen by ear on the cello's open C and G: over a tenth of a second the ease swept every
start quickly from bright to dull and sounded rough; 0.8 s sounded the most natural of the times
tried. */
static const double EASE_SECONDS = 0.8;
static int failures = 0;


static inline void check(bool ok, const char* what, double got) {
	std::printf("%s  %-60s %g\n", ok ? "ok  " : "FAIL", what, got);
	if (!ok)
		failures++;
}


static inline BowedString string(float hz, float decay = 2.f, float damping = 0.3f,
                          float contact = 0.12f) {
	BowedString s;
	s.setSampleRate(RATE);
	s.setFrequency(hz);
	s.setDecay(decay);
	s.setDamping(damping);
	s.setContact(contact);
	return s;
}


static inline std::vector<float> play(BowedString& s, float seconds) {
	std::vector<float> out((size_t) (seconds * RATE));
	for (float& x : out)
		x = s.process();
	return out;
}


/** The pitch over a stretch of the sound, by autocorrelation with the peak found between samples.
stringtest's. */
static inline double pitchOf(const std::vector<float>& x, double from, double to, double expect) {
	const size_t a = (size_t) (from * RATE), b = (size_t) (to * RATE);
	const double period = RATE / expect;
	const int lo = (int) (period * 0.8), hi = (int) (period * 1.2) + 2;
	std::vector<double> r(hi + 2, 0.0);
	for (int lag = lo - 1; lag <= hi + 1; lag++) {
		double sum = 0.0;
		for (size_t i = a; i + lag < b; i++)
			sum += (double) x[i] * x[i + lag];
		r[lag] = sum;
	}
	int best = lo;
	for (int lag = lo; lag <= hi; lag++)
		if (r[lag] > r[best])
			best = lag;
	const double y0 = r[best - 1], y1 = r[best], y2 = r[best + 1];
	const double shift = 0.5 * (y0 - y2) / (y0 - 2.0 * y1 + y2);
	return RATE / (best + shift);
}


static inline double cents(double got, double want) { return 1200.0 * std::log2(got / want); }


/** HOW MUCH OF A PARTIAL IS THERE, by a Goertzel at that frequency. */
static inline double magnitudeAt(const std::vector<float>& x, double from, double len, double hz) {
	const size_t a = (size_t) (from * RATE), n = (size_t) (len * RATE);
	const double w = 2.0 * M_PI * hz / RATE;
	double re = 0.0, im = 0.0;
	for (size_t i = 0; i < n && a + i < x.size(); i++) {
		re += x[a + i] * std::cos(w * (double) i);
		im -= x[a + i] * std::sin(w * (double) i);
	}
	return std::sqrt(re * re + im * im) / (double) n;
}


/** ONE PARTIAL'S OWN FREQUENCY, by scanning the magnitude around where it is expected and
refining the peak with a parabola. Autocorrelation answers a different question — the period the
whole waveform best repeats at — and that moves with which partials happen to be loud, which is
exactly what changes when the string is plucked at a different point. */
static inline double partialHz(const std::vector<float>& x, double from, double len, double near) {
	const size_t a = (size_t) (from * RATE), n = (size_t) (len * RATE);
	auto mag = [&](double hz) {
		const double w = 2.0 * M_PI * hz / RATE;
		double re = 0.0, im = 0.0;
		for (size_t i = 0; i < n && a + i < x.size(); i++) {
			re += x[a + i] * std::cos(w * (double) i);
			im -= x[a + i] * std::sin(w * (double) i);
		}
		return std::sqrt(re * re + im * im);
	};
	const double step = near * 0.0004;          // about 0.7 cents a step
	double best = near, bestMag = -1.0;
	for (int k = -40; k <= 40; k++) {
		const double hz = near + k * step;
		const double m = mag(hz);
		if (m > bestMag) { bestMag = m; best = hz; }
	}
	const double y0 = mag(best - step), y1 = bestMag, y2 = mag(best + step);
	const double den = y0 - 2.0 * y1 + y2;
	return best + ((den != 0.0) ? 0.5 * step * (y0 - y2) / den : 0.0);
}


static inline double rmsOver(const std::vector<float>& x, double from, double to) {
	const size_t a = (size_t) (from * RATE), b = (size_t) (to * RATE);
	double sum = 0.0;
	size_t n = 0;
	for (size_t i = a; i < b && i < x.size(); i++, n++)
		sum += (double) x[i] * x[i];
	return n ? std::sqrt(sum / (double) n) : 0.0;
}


/** How long the sound takes to fall by 60 dB, from two windows on the way down. The windows are
placed against the decay being measured, not at fixed times: over a tenth of a long decay the
sound barely moves and the answer is mostly the pluck's own transient. */
static inline double decayOf(const std::vector<float>& x, double over) {
	const double a = 0.1 * over, b = 0.8 * over;
	const double early = rmsOver(x, a, a + 0.1 * over), late = rmsOver(x, b, b + 0.1 * over);
	if (early <= 0.0 || late <= 0.0)
		return 0.0;
	const double perSecond = 20.0 * std::log10(late / early) / (b - a);
	return (perSecond < 0.0) ? -60.0 / perSecond : 1e9;
}


static inline double peakOf(const std::vector<float>& x) {
	double p = 0.0;
	for (float v : x)
		p = std::fmax(p, std::fabs((double) v));
	return p;
}


static inline void writeWav(const std::string& path, const std::vector<float>& left) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	const uint32_t frames = (uint32_t) left.size(), bytes = frames * 4;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16(2); u32((uint32_t) RATE); u32((uint32_t) RATE * 4); u16(4); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (uint32_t i = 0; i < frames; i++) {
		const int16_t v = (int16_t) std::lround(
			std::fmax(-1.f, std::fmin(1.f, left[i])) * 32000.f);
		std::fwrite(&v, 2, 1, f);
		std::fwrite(&v, 2, 1, f);
	}
	std::fclose(f);
	std::printf("wrote %s\n", path.c_str());
}


/** SLIPPING, JUDGED FROM THE MOTION, as the measurements judge it, and not from the friction
model's own state: a slip is a flyback, the string going back against the bow at half the speed of
an ideal Helmholtz slip or more (the bow's speed over beta), or, while the bow is too slow for that
to mean anything, at more than the slip a twentieth of the bow force would cause. It ends when the
string has all but caught the bow again. Under the friction curve this agrees with the junction's
stuck flag, which marks the moment of release exactly and is used there instead; under bristles
the string can creep, sliding slowly with every bristle broken, which is not a slip. */
struct SlipWatch {
	bool slipping = false;
	bool update(const BowedString& s, float force, float bowSpeed) {
		const float dir = bowSpeed < 0.f ? -1.f : 1.f;
		const float back = dir * (bowSpeed - s.velocityAtBow());
		const float ideal = std::fabs(bowSpeed) / std::fmax(0.01f, s.contact());
		const float floor = 0.05f * force / (2.f * s.impedance());
		const float start = std::fmax(0.5f * ideal, floor);
		slipping = slipping ? back > 0.2f * start : back > start;
		return slipping;
	}
};


/** WHAT A BOWED NOTE DID, sample by sample: the bridge, the string under the bow, and whether it
was stuck to the hair. */
struct Bowing {
	std::vector<float> bridge, velocity;
	std::vector<uint8_t> stuck;
};


/** A NOTE BOWED FROM REST. The bow is put down at its force and drawn from standing still up to
its speed over `rise` seconds, which is a plain attack and not a clever one: Guettler's diagram,
which says which attacks start cleanly, is phase 3. */
static inline Bowing bow(BowedString& s, float force, float speed, float seconds, float rise = 0.05f) {
	Bowing b;
	const size_t n = (size_t) (seconds * RATE);
	b.bridge.resize(n);
	b.velocity.resize(n);
	b.stuck.resize(n);
	SlipWatch watch;
	for (size_t i = 0; i < n; i++) {
		const float t = (float) i / RATE;
		const float v = speed * std::fmin(1.f, t / rise);
		s.setBow(force, v);
		b.bridge[i] = s.process();
		b.velocity[i] = s.velocityAtBow();
		const bool slips = watch.update(s, force, v);
		b.stuck[i] = (s.bristling() ? !slips : s.sticking()) ? 1 : 0;
	}
	return b;
}


/** WHAT HELMHOLTZ MOTION LOOKS LIKE UNDER THE BOW, measured over a stretch of a bowed note.

One kink goes round the string, and each time it passes the bow the string lets go of the hair,
slips back, and is caught again. So in every period there is exactly one slip; the string is stuck
for (1 - beta) of the period, moving with the bow; and it slips for beta of it, going the other way
at the bow speed times (1 - beta) / beta, which is what brings the average back to nothing. */
struct Helmholtz {
	double settledAt = -1.0;   // seconds until every period has one slip, and from then on
	double stuckShare = 0.0;   // of the samples in the stretch measured
	double slipSpeed = 0.0;    // the string's mean velocity while slipping
	double stickSpeed = 0.0;   // and while stuck
	double meanSpeed = 0.0;    // over whole periods
	int periodsWithOne = 0, periods = 0;
};


static inline Helmholtz helmholtz(const Bowing& b, double hz, double from, double to) {
	Helmholtz h;
	const double period = RATE / hz;
	const size_t n = b.stuck.size();
	// Slips start where the string comes off the hair.
	std::vector<size_t> starts;
	for (size_t i = 1; i < n; i++)
		if (b.stuck[i - 1] && !b.stuck[i])
			starts.push_back(i);
	// SETTLED: from this slip on, every gap to the next is one period, within a tenth. Searched
	// from the end backwards, so a run that holds to the end of the note is the one found.
	size_t good = starts.size();
	for (size_t k = starts.size(); k-- > 1; ) {
		const double gap = (double) (starts[k] - starts[k - 1]);
		if (std::fabs(gap - period) > 0.1 * period)
			break;
		good = k - 1;
	}
	if (good + 4 < starts.size())
		h.settledAt = (double) starts[good] / RATE;

	const size_t a = (size_t) (from * RATE), z = std::min(n, (size_t) (to * RATE));
	double stuckN = 0.0, slipSum = 0.0, slipN = 0.0, stickSum = 0.0, all = 0.0;
	for (size_t i = a; i < z; i++) {
		all += b.velocity[i];
		if (b.stuck[i]) { stuckN += 1.0; stickSum += b.velocity[i]; }
		else { slipSum += b.velocity[i]; slipN += 1.0; }
	}
	const double len = (double) (z - a);
	h.stuckShare = stuckN / len;
	h.slipSpeed = slipN > 0.0 ? slipSum / slipN : 0.0;
	h.stickSpeed = stuckN > 0.0 ? stickSum / stuckN : 0.0;
	// Over a whole number of periods, or the part period left over biases it.
	const size_t whole = (size_t) (std::floor(len / period) * period);
	double sum = 0.0;
	for (size_t i = a; i < a + whole; i++)
		sum += b.velocity[i];
	h.meanSpeed = whole ? sum / (double) whole : 0.0;
	for (size_t k = 0; k + 1 < starts.size(); k++) {
		if (starts[k] < a || starts[k + 1] > z)
			continue;
		h.periods++;
		const double gap = (double) (starts[k + 1] - starts[k]);
		if (std::fabs(gap - period) <= 0.1 * period)
			h.periodsWithOne++;
	}
	(void) all;
	return h;
}


/** A STRING AS IT IS ON ITS INSTRUMENT: the fundamental at a Q of five hundred, which open cello
strings measure (Mansour, Woodhouse and Scavone 2016; Serafin and colleagues used the same), so
sixty decibels in 6.91 Q / (pi f) seconds — nine seconds at D3, seventeen at the low C. And the
string's own wave impedance, measured: the D'Addario Kaplan cello set and Pickering's Dominant
violin set. */
static inline float impedanceOf(float hz) {
	struct Z { float hz, z; };
	static const Z SET[] = {{65.41f, 1.46f}, {98.f, 1.00f}, {146.83f, 0.67f}, {220.f, 0.56f},
		{196.f, 0.34f}, {293.66f, 0.23f}, {440.f, 0.19f}, {659.26f, 0.17f}};
	float best = 0.67f, near = 1e9f;
	for (const Z& z : SET)
		if (std::fabs(std::log(hz / z.hz)) < near) {
			near = std::fabs(std::log(hz / z.hz));
			best = z.z;
		}
	return best;
}

/** WHAT EVERY STRING THE BENCH MAKES IS GIVEN AFTER THE REST: a friction model, say, so a whole
section can be run under another. Nothing by default. */
static void (*benchFriction)(BowedString&) = nullptr;

static inline BowedString cello(float hz, float contact, float impedance = -1.f) {
	const float q = 500.f;
	BowedString s = string(hz, 6.91f * q / (3.14159265f * hz), 0.3f, contact);
	s.setImpedance(impedance > 0.f ? impedance : impedanceOf(hz));
	if (benchFriction)
		benchFriction(s);
	return s;
}


static inline void normalise(std::vector<float>& x, float to = 0.8f) {
	const double p = peakOf(x);
	if (p > 0.0)
		for (float& v : x)
			v = (float) (v / p * to);
}


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


/** HOW FAR A GAP BETWEEN SLIPS MAY STRAY FROM THE PERIOD and still count as regular. */
static double REGULAR = 0.1;
/** Nought: regular from a slip to the end of the run. More: from the first slip with that many
regular periods after it. */
static size_t FIRST_RUN = 0;

static inline Attack attack(BowedString& s, float force, float accel, double hz, int periodsAfter,
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
	SlipWatch watch;
	for (size_t i = 0; i < end; i++) {
		const float t = (float) i / RATE;
		s.setBow(force, std::fmin(accel * t, topSpeed));
		const float out = s.process();
		if (bridge)
			bridge->push_back(out);
		const bool flyback = watch.update(s, force, std::fmin(accel * t, topSpeed));
		const bool stuck = s.bristling() ? !flyback : s.sticking();
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
		if (std::fabs(gap - period) > REGULAR * period)
			break;
		k = j - 1;
	}
	if (k < slips.size() && slips.size() - k < 4)
		k = slips.size();
	// OR, IF ASKED, the first slip from which five periods running are regular.
	if (FIRST_RUN > 0) {
		k = slips.size();
		for (size_t j = 0; j + FIRST_RUN < slips.size(); j++) {
			bool run = true;
			for (size_t q = j + 1; q <= j + FIRST_RUN && run; q++)
				run = std::fabs((double) (slips[q] - slips[q - 1]) - period) <= REGULAR * period;
			if (run) { k = j; break; }
		}
	}
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


/** GUETTLER AND ASKENFELT'S LISTENING LIMITS (1997, as Guettler 2002 gives them): a violin attack
was heard as unacceptable when its multiple slipping lasted more than 90 milliseconds, or its
prolonged periods more than 50. Perfect is regular from the first period. */
enum Grade { PERFECT, ACCEPTABLE, CREAKS, SLIPS };

static inline Grade grade(const Attack& r) {
	if (r.periodsBefore == 0)
		return PERFECT;
	if (r.periodsBefore > 0 && r.seconds <= (r.prolonged ? 0.050 : 0.090))
		return ACCEPTABLE;
	return r.prolonged ? CREAKS : SLIPS;
}


// ---- THE PLAYER WHO REHEARSES ------------------------------------------------------------------

/** HOW ROUGH A START IS: in the first 80 ms after the string first lets go, the energy above two
kilohertz over the energy below one, through second-order filters. A clean start is no brighter
than the note it becomes; a rough one, its first periods irregular or pressed too hard, is several
times brighter. Cheap enough to measure on every attack the player tries. */
static inline double startRoughness(const std::vector<float>& x, double firstSlip) {
	auto biquad = [](double hz, bool high, double* b, double* a) {
		const double w = 2.0 * M_PI * hz / RATE, cw = std::cos(w), alpha = std::sin(w) / (2.0 * 0.7071);
		const double a0 = 1.0 + alpha;
		b[0] = (high ? (1.0 + cw) / 2.0 : (1.0 - cw) / 2.0) / a0;
		b[1] = (high ? -(1.0 + cw) : (1.0 - cw)) / a0;
		b[2] = b[0];
		a[0] = -2.0 * cw / a0;
		a[1] = (1.0 - alpha) / a0;
	};
	double hb[3], ha[2], lb[3], la[2];
	biquad(2000.0, true, hb, ha);
	biquad(1000.0, false, lb, la);
	double hx1 = 0, hx2 = 0, hy1 = 0, hy2 = 0, lx1 = 0, lx2 = 0, ly1 = 0, ly2 = 0, high = 0, low = 0;
	const size_t from = (size_t) (firstSlip * RATE), to = std::min(x.size(), from + (size_t) (0.08 * RATE));
	for (size_t i = 0; i < to; i++) {
		const double v = x[i];
		const double h = hb[0] * v + hb[1] * hx1 + hb[2] * hx2 - ha[0] * hy1 - ha[1] * hy2;
		hx2 = hx1; hx1 = v; hy2 = hy1; hy1 = h;
		const double l = lb[0] * v + lb[1] * lx1 + lb[2] * lx2 - la[0] * ly1 - la[1] * ly2;
		lx2 = lx1; lx1 = v; ly2 = ly1; ly1 = l;
		if (i >= from) {
			high += h * h;
			low += l * l;
		}
	}
	return low > 0.0 ? high / low : 0.0;
}


/** WHAT THE PLAYER CHOSE: where the bow goes, how much harder than the note wants it starts, the
bow's acceleration, how many periods that attack takes to turn regular, how rough its start, and the
soonest any attack it tried at the planned position turned regular. */
struct Rehearsed {
	float beta = 0.1f;
	double bite = 1.0;
	float accel = 0.f;
	int periods = -1;
	double roughness = 0.0;
	int soonest = -1;
};

/** A PLAYER WHO REHEARSES EACH NOTE on a copy of the string before playing it (phase 3 of
docs/bowed-strings.md): seven starting forces from the note's own to four times it, each at
twenty-four bow accelerations scaled as Guettler's limits scale, beta (1 - beta) F / (T Z). Of the
attacks that turn regular within five periods, the smoothest start; if none does, the soonest. The
finer search and the smoothness were settled by ear on the violin's open G and D: the coarser
search, twelve accelerations at four forces taking the soonest, found only attacks at four times
the force, and they started harshly.

WHERE NOTHING TURNS REGULAR WITHIN THREE PERIODS, A LITTLE NEARER THE BRIDGE OR FURTHER FROM IT: the
search again at 0.7, 0.85, 1.2 and 1.4 of the planned bow position, more finely — thirteen forces
to eight times the note's, forty accelerations — and the soonest attack found anywhere, the
smoothest of the soonest, played where it was found. Settled by ear on the cello's open C, whose
clean starts are so few that at a tenth of the length the best took five periods, about 76 ms;
at 0.085 one starts at once, and sounded best. At the current RATE, which the caller sets. */
static inline Rehearsed rehearseAttack(double hz, float impedance, float beta, float force, float speed) {
	const double z = impedance > 0.f ? impedance : impedanceOf((float) hz);
	const double zBow = z * 3.27 / 4.27, T = 1.0 / hz;
	Rehearsed smoothest, quickest;
	for (int b = 0; b < 7; b++)
		for (int k = 0; k < 24; k++) {
			const double bite = std::pow(4.0, b / 6.0);
			const double kk = 0.004 * std::pow(30.0, k / 23.0);
			const float a = (float) (kk * beta * (1.0 - beta) * force * bite / (T * zBow));
			BowedString s = cello((float) hz, beta, impedance);
			std::vector<float> bridge;
			const Attack r = attack(s, (float) (force * bite), a, hz, 20, &bridge, speed);
			if (r.periodsBefore < 0)
				continue;
			const double rough = startRoughness(bridge, r.firstSlip);
			if (quickest.periods < 0 || r.periodsBefore < quickest.periods) {
				quickest.bite = bite;
				quickest.accel = a;
				quickest.periods = r.periodsBefore;
				quickest.roughness = rough;
			}
			if (r.periodsBefore <= 5 && (smoothest.periods < 0 || rough < smoothest.roughness)) {
				smoothest.bite = bite;
				smoothest.accel = a;
				smoothest.periods = r.periodsBefore;
				smoothest.roughness = rough;
			}
		}
	Rehearsed best = smoothest.periods >= 0 ? smoothest : quickest;
	best.beta = beta;
	const int soonest = quickest.periods;
	if (best.periods < 0 || best.periods > 3) {
		Rehearsed moved;
		for (float shift : {0.85f, 1.2f, 0.7f, 1.4f}) {
			const float b2 = beta * shift;
			for (int b = 0; b < 13; b++)
				for (int k = 0; k < 40; k++) {
					const double bite = std::pow(8.0, b / 12.0);
					const double kk = 0.002 * std::pow(100.0, k / 39.0);
					const float a = (float) (kk * b2 * (1.0 - b2) * force * bite / (T * zBow));
					BowedString s = cello((float) hz, b2, impedance);
					std::vector<float> bridge;
					const Attack r = attack(s, (float) (force * bite), a, hz, 20, &bridge, speed);
					if (r.periodsBefore < 0)
						continue;
					const double rough = startRoughness(bridge, r.firstSlip);
					if (moved.periods < 0 || r.periodsBefore < moved.periods
							|| (r.periodsBefore == moved.periods && rough < moved.roughness)) {
						moved.beta = b2;
						moved.bite = bite;
						moved.accel = a;
						moved.periods = r.periodsBefore;
						moved.roughness = rough;
					}
				}
		}
		if (moved.periods >= 0 && (best.periods < 0 || moved.periods < best.periods))
			best = moved;
	}
	best.soonest = soonest;
	return best;
}

