#pragma once
/** WHAT THE BOWED-STRING BENCHES SHARE: making a string, bowing it, and measuring what it did.
test/bowtest.cpp tests the string and the bow, phases 1 and 2 of docs/bowed-strings.md;
test/bowplay.cpp sweeps where it plays and how notes start, phase 3. Each includes this once. */
#include "../src/BowedRehearsal.hpp"
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

/** HOW FAR A GAP BETWEEN SLIPS MAY STRAY FROM THE PERIOD and still count as regular, and from which
slip a run is looked for: see attackFromRest in src/BowedRehearsal.hpp, where the attack, the
roughness and the rehearsing player now live, so a module can rehearse as the benches do. */
static double REGULAR = 0.1;
static size_t FIRST_RUN = 0;

static inline Attack attack(BowedString& s, float force, float accel, double hz, int periodsAfter,
                     std::vector<float>* bridge = nullptr, float topSpeed = 1e9f) {
	return attackFromRest(s, force, accel, hz, periodsAfter, bridge, topSpeed, REGULAR, FIRST_RUN);
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

static inline double startRoughness(const std::vector<float>& x, double firstSlip) {
	return px::startRoughness(x, firstSlip, RATE);
}

/** THE PLAYER'S CHOICE: the first of its shortlist. */
static inline Rehearsed rehearseAttackOn(const BowedString& model, double hz, float beta, float force,
                                         float speed) {
	const std::vector<Rehearsed> list = rehearseShortlist(model, hz, beta, force, speed);
	return list.empty() ? Rehearsed() : list.front();
}

static inline Rehearsed rehearseAttack(double hz, float impedance, float beta, float force, float speed) {
	return rehearseAttackOn(cello((float) hz, beta, impedance), hz, beta, force, speed);
}

