/** One modelled bowed string, from a command line: the string in phase 1, the bow in phase 2.

WHY THERE IS A PROGRAM FOR THIS. GuitarString is a single loop and this is four delay lines that
have to behave as one, divided at a point that moves while the string sounds. Every way that can
go wrong is silent: a string a few cents flat, a ring that shortens when the bow is moved towards
the bridge, a loop that grows instead of dying. Measured, each is a number against the one asked
for, and the contact point can be swept over its whole range in a second rather than listened to
at four or five settings.

IT RUNS AT CELLO PITCHES, as docs/bowed-strings.md says to. A cello's periods are 218 to 734
samples at 48 kHz where a violin's top string gives 73 and its high positions 18, so a fault that
is a tenth of a sample shows up here and hides there. The violin's own range is measured too, and
the last check reports where the contact point starts to run out of room, which is the figure the
oversampling schedule in phase 4 has to beat.

    make bowtest
*/
#include "../src/BowedString.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace px;


static const float RATE = 48000.f;
static int failures = 0;


static void check(bool ok, const char* what, double got) {
	std::printf("%s  %-60s %g\n", ok ? "ok  " : "FAIL", what, got);
	if (!ok)
		failures++;
}


static BowedString string(float hz, float decay = 2.f, float damping = 0.3f,
                          float contact = 0.12f) {
	BowedString s;
	s.setSampleRate(RATE);
	s.setFrequency(hz);
	s.setDecay(decay);
	s.setDamping(damping);
	s.setContact(contact);
	return s;
}


static std::vector<float> play(BowedString& s, float seconds) {
	std::vector<float> out((size_t) (seconds * RATE));
	for (float& x : out)
		x = s.process();
	return out;
}


/** The pitch over a stretch of the sound, by autocorrelation with the peak found between samples.
stringtest's. */
static double pitchOf(const std::vector<float>& x, double from, double to, double expect) {
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


static double cents(double got, double want) { return 1200.0 * std::log2(got / want); }


/** HOW MUCH OF A PARTIAL IS THERE, by a Goertzel at that frequency. */
static double magnitudeAt(const std::vector<float>& x, double from, double len, double hz) {
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
static double partialHz(const std::vector<float>& x, double from, double len, double near) {
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


static double rmsOver(const std::vector<float>& x, double from, double to) {
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
static double decayOf(const std::vector<float>& x, double over) {
	const double a = 0.1 * over, b = 0.8 * over;
	const double early = rmsOver(x, a, a + 0.1 * over), late = rmsOver(x, b, b + 0.1 * over);
	if (early <= 0.0 || late <= 0.0)
		return 0.0;
	const double perSecond = 20.0 * std::log10(late / early) / (b - a);
	return (perSecond < 0.0) ? -60.0 / perSecond : 1e9;
}


static double peakOf(const std::vector<float>& x) {
	double p = 0.0;
	for (float v : x)
		p = std::fmax(p, std::fabs((double) v));
	return p;
}


static void writeWav(const std::string& path, const std::vector<float>& left) {
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
static Bowing bow(BowedString& s, float force, float speed, float seconds, float rise = 0.05f) {
	Bowing b;
	const size_t n = (size_t) (seconds * RATE);
	b.bridge.resize(n);
	b.velocity.resize(n);
	b.stuck.resize(n);
	for (size_t i = 0; i < n; i++) {
		const float t = (float) i / RATE;
		s.setBow(force, speed * std::fmin(1.f, t / rise));
		b.bridge[i] = s.process();
		b.velocity[i] = s.velocityAtBow();
		b.stuck[i] = s.sticking() ? 1 : 0;
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


static Helmholtz helmholtz(const Bowing& b, double hz, double from, double to) {
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
static float impedanceOf(float hz) {
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

static BowedString cello(float hz, float contact, float impedance = -1.f) {
	const float q = 500.f;
	BowedString s = string(hz, 6.91f * q / (3.14159265f * hz), 0.3f, contact);
	s.setImpedance(impedance > 0.f ? impedance : impedanceOf(hz));
	return s;
}


static void normalise(std::vector<float>& x, float to = 0.8f) {
	const double p = peakOf(x);
	if (p > 0.0)
		for (float& v : x)
			v = (float) (v / p * to);
}


static void bowPhase() {
	// ---- A HELD NOTE REACHES HELMHOLTZ MOTION -----------------------------------------------
	std::printf("\nTHE BOW: a held cello D3, a newton at 0.2 m/s, a tenth of the way from the "
		"bridge; impedance 0.67 kg/s, Q 500.\n");
	const double hz = 146.83;
	const float beta = 0.1f, force = 1.f, speed = 0.2f;
	{
		BowedString s = cello((float) hz, beta);
		const Bowing b = bow(s, force, speed, 1.5f);
		const Helmholtz h = helmholtz(b, hz, 1.0, 1.5);
		// WITHIN A SECOND, from a plain attack. A cellist does it in tens of milliseconds, and a
		// clean start is phase 3's question — this friction model is known to start slowly.
		check(h.settledAt >= 0.0 && h.settledAt < 1.0,
			"  settles into one slip a period, after (s)", h.settledAt);
		check(h.periods > 0 && h.periodsWithOne == h.periods,
			"  periods in the last half second with exactly one slip", h.periodsWithOne);
		// SLIPPING FOR BETA OF THE PERIOD, OR A LITTLE MORE. The ideal Helmholtz motion has a
		// perfectly sharp corner and slips for exactly beta; a real string's losses round the
		// corner, the slip spreads, and it goes back more slowly for longer. Up to half again.
		check(1.0 - h.stuckShare >= beta - 0.01 && 1.0 - h.stuckShare <= 1.5 * beta,
			"  share of each period slipping (beta is 0.1)", 1.0 - h.stuckShare);
		check(std::fabs(h.stickSpeed - speed) < 1e-4,
			"  while stuck the string moves with the bow (m/s)", h.stickSpeed);
		check(h.slipSpeed < 0.0, "  while slipping it goes back, at (m/s; sharp-cornered ideal -1.8)",
			h.slipSpeed);
		check(std::fabs(h.meanSpeed) < 0.05 * speed,
			"  and on average goes nowhere (m/s)", h.meanSpeed);

		// The pitch is the string's, not half or twice it.
		const double f1 = partialHz(b.bridge, 1.0, 0.5, hz);
		const double c = cents(f1, hz);
		check(std::fabs(c) < 10.0, "  fundamental at the bridge against the string's (cents)", c);
		const double sub = magnitudeAt(b.bridge, 1.0, 0.5, hz * 0.5);
		const double fund = magnitudeAt(b.bridge, 1.0, 0.5, f1);
		check(20.0 * std::log10(sub / fund) < -30.0,
			"  nothing at half the pitch: f/2 against f (dB)", 20.0 * std::log10(sub / fund));
		check(std::fabs(cents(pitchOf(b.bridge, 1.0, 1.5, hz), hz)) < 10.0,
			"  the waveform repeats at the string's period (cents)",
			cents(pitchOf(b.bridge, 1.0, 1.5, hz), hz));

		// HELMHOLTZ'S SPECTRUM: the force at the bridge is a sawtooth, every harmonic falling as
		// one over its number, WHEREVER the bow is. A missing harmonic at the bow's node is a
		// pluck's property and not Helmholtz motion's; the dip that does show here sits at one
		// over the share of the period spent slipping, the shape of the slip pulse. Printed, not
		// checked, until the corner's rounding is physical (see docs/bowed-strings.md, phase 2).
		std::printf("  the bridge's harmonics, against one over the harmonic number:\n");
		for (int k = 1; k <= 12; k++) {
			const double m = magnitudeAt(b.bridge, 1.0, 0.5, f1 * k);
			std::printf("    %2d  %+6.1f dB\n", k, 20.0 * std::log10(m * k / fund));
		}

		std::vector<float> out = b.bridge;
		normalise(out);
		writeWav("build/bowed-held.wav", out);
	}

	// ---- AN UP-BOW IS A DOWN-BOW TURNED ROUND -----------------------------------------------
	std::printf("\nAN UP-BOW, the same note drawn the other way.\n");
	{
		BowedString s = cello((float) hz, beta);
		const Bowing b = bow(s, force, -speed, 1.5f);
		const Helmholtz h = helmholtz(b, hz, 1.0, 1.5);
		check(h.periods > 0 && h.periodsWithOne == h.periods,
			"  one slip a period, periods", h.periodsWithOne);
		check(std::fabs(h.stuckShare - (1.0 - beta)) < 0.05, "  share stuck", h.stuckShare);
		check(h.slipSpeed > 0.0, "  and slips the other way (m/s)", h.slipSpeed);
	}

	// ---- THE BOW LIFTS AND THE STRING RINGS ON ----------------------------------------------
	std::printf("\nTHE BOW LIFTED after a second: the string rings down as a plucked one does.\n");
	{
		BowedString s = cello((float) hz, beta);
		std::vector<float> x((size_t) (3.0 * RATE));
		for (size_t i = 0; i < x.size(); i++) {
			const float t = (float) i / RATE;
			s.setBow(t < 1.f ? force : 0.f, speed * std::fmin(1.f, t / 0.05f));
			x[i] = s.process();
		}
		const double held = rmsOver(x, 0.8, 1.0), after = rmsOver(x, 2.6, 2.9);
		check(after < held && after > held * 1e-3,
			"  rings on and dies away: late over held", after / held);
		check(peakOf(x) < 100.0, "  nothing runs away, peak (m/s)", peakOf(x));
	}

	// ---- OVER THE CELLO'S STRINGS AND UP INTO THE VIOLIN'S ----------------------------------
	std::printf("\nHELMHOLTZ ACROSS THE RANGE, the same bowing at each pitch, 48 kHz.\n");
	{
		// EACH STRING AT A FORCE A PLAYER WOULD USE ON IT. The low C finds clean Helmholtz motion
		// only from about three newtons at this speed, and below that slips two to seven times a
		// period. At 48 kHz the violin's A has the bow five samples from the bridge, and at about
		// 0.3 N it starts to go astray, which is phase 4's oversampling.
		struct Note { double hz; const char* name; float force; };
		const Note notes[] = {{65.41,"cello C2",3.f}, {98.00,"cello G2",1.5f}, {220.00,"cello A3",0.8f},
		                      {196.00,"violin G3",0.5f}, {440.00,"violin A4",0.5f}, {659.26,"violin E5",0.3f}};
		for (const Note& n : notes) {
			BowedString s = cello((float) n.hz, beta);
			const Bowing b = bow(s, n.force, speed, 1.5f, 0.2f);
			const Helmholtz h = helmholtz(b, n.hz, 1.0, 1.5);
			char what[96];
			std::snprintf(what, sizeof what, "  %-10s at %.1f N, periods with one slip, %d of %d",
				n.name, n.force, h.periodsWithOne, h.periods);
			check(h.periods > 0 && h.periodsWithOne >= 0.9 * h.periods, what,
				h.periods > 0 ? (double) h.periodsWithOne / h.periods : 0.0);
		}
	}

	// ---- WHERE IT PLAYS: A FIRST LOOK AT SCHELLENG'S DIAGRAM --------------------------------
	// Not a pass or a fail; the real sweep, with the published limits drawn on it, is phase 3.
	// Printed to show the region exists and has roughly the right shape: a band between too
	// little force and too much, rising towards the bridge.
	// WHETHER HELMHOLTZ MOTION SURVIVES at each force and bow position, which is what Schelleng's
	// diagram draws: started cleanly at a newton a tenth of the way from the bridge, moved to the
	// cell over a seventh of a second, then held. Whether a note can START there is Guettler's
	// question and phase 3's.
	std::printf("\nWHERE IT PLAYS, cello D3 at 0.2 m/s: H is Helmholtz motion held, a number is "
		"that many slips a period, - irregular.\n");
	{
		const float forces[] = {0.05f, 0.1f, 0.2f, 0.5f, 1.f, 2.f, 4.f, 6.f, 8.f};
		const float betas[] = {0.03f, 0.05f, 0.08f, 0.12f, 0.18f, 0.25f};
		std::printf("  %-8s", "beta");
		for (float f : forces)
			std::printf("%6.2fN", f);
		std::printf("\n");
		for (float be : betas) {
			std::printf("  %-8.2f", be);
			for (float f : forces) {
				BowedString s = cello((float) hz, 0.1f);
				const size_t n = (size_t) (1.6 * RATE);
				std::vector<uint8_t> stuck(n);
				for (size_t i = 0; i < n; i++) {
					const float t = (float) i / RATE;
					const float m = std::fmin(1.f, std::fmax(0.f, (t - 0.6f) / 0.15f));
					s.setContact(0.1f * std::pow(be / 0.1f, m));
					s.setBow(std::pow(f, m), speed * std::fmin(1.f, t / 0.05f));
					s.process();
					stuck[i] = s.sticking() ? 1 : 0;
				}
				const double period = RATE / hz;
				int first = -1;
				bool same = true;
				for (double t0 = (double) n - 0.4 * RATE; t0 + period <= (double) n; t0 += period) {
					int slips = 0;
					for (size_t i = (size_t) t0 + 1; i < (size_t) (t0 + period); i++)
						if (stuck[i - 1] && !stuck[i])
							slips++;
					if (first < 0)
						first = slips;
					same = same && slips == first;
				}
				char cell[8];
				std::snprintf(cell, sizeof cell, "%d", first);
				std::printf("%7s", !same ? "-" : (first == 1 ? "H" : cell));
			}
			std::printf("\n");
		}
	}

	// ---- SOMETHING TO LISTEN TO -------------------------------------------------------------
	std::printf("\n");
	{
		// The four open strings, each bowed for a second and a half with a swell in the middle.
		std::vector<float> out;
		for (double f : {65.41, 98.00, 146.83, 220.00}) {
			BowedString s = cello((float) f, 0.1f);
			const size_t n = (size_t) (1.6 * RATE);
			for (size_t i = 0; i < n; i++) {
				const float t = (float) i / RATE;
				const float swell = 0.6f + 0.4f * std::sin(3.14159265f * t / 1.6f);
				s.setBow(t < 1.4f ? 0.8f * swell : 0.f,
					0.15f * swell * std::fmin(1.f, t / 0.08f));
				out.push_back(s.process());
			}
		}
		normalise(out);
		writeWav("build/bowed-open-strings.wav", out);
	}
	{
		// One note, the bow moving from over the fingerboard to near the bridge and back:
		// sul tasto to sul ponticello. The force follows it, as a player's must.
		BowedString s = cello(146.83f, 0.2f);
		std::vector<float> out((size_t) (4.0 * RATE));
		for (size_t i = 0; i < out.size(); i++) {
			const float t = (float) i / RATE;
			const float b = 0.125f - 0.085f * std::sin(3.14159265f * t / 4.f);
			s.setContact(b);
			s.setBow(0.1f / b * 0.12f * 8.f * std::fmin(1.f, t / 0.05f) * 0.5f, 0.2f);
			out[i] = s.process();
		}
		normalise(out);
		writeWav("build/bowed-tasto-ponticello.wav", out);
	}
}


int main() {
	// ---- TUNING, over the cello's range and then the violin's -------------------------------
	std::printf("\nTUNING, the open strings and a high position on each instrument.\n");
	struct Note { double hz; const char* name; };
	const Note cello[] = {{65.41,"cello C2"}, {98.00,"cello G2"}, {146.83,"cello D3"},
	                      {220.00,"cello A3"}, {440.00,"cello A4, high"}};
	const Note violin[] = {{196.00,"violin G3"}, {293.66,"violin D4"}, {440.00,"violin A4"},
	                       {659.26,"violin E5"}, {1318.51,"violin E6, high"}};
	for (const Note* set : {cello, violin})
		for (int i = 0; i < 5; i++) {
			BowedString s = string((float) set[i].hz);
			s.pluck(1.f, 0.4f);
			const std::vector<float> x = play(s, 1.2f);
			// THE FUNDAMENTAL ITSELF, and not autocorrelation, which answers how the whole
			// waveform repeats and moves with whichever partials are loud: with the losses set
			// physically it read the low C two cents out while its fundamental was a quarter of a
			// cent from true. See THE SPLIT below for the same lesson.
			const double c = cents(partialHz(x, 0.3, 0.8, set[i].hz), set[i].hz);
			char what[96];
			std::snprintf(what, sizeof what, "tuning: %s, %.2f Hz (cents)",
				set[i].name, set[i].hz);
			check(std::fabs(c) < 1.0, what, c);
		}

	// ---- THE SPLIT ADDS UP ------------------------------------------------------------------
	// The point of the phase. Whatever the contact point, the two segments must make the same
	// loop a single line would have, so the pitch must not move when the bow does.
	// MEASURED ON THE FUNDAMENTAL, not by autocorrelation. Autocorrelation answers a different
	// question — the lag at which the whole waveform best repeats — and on a decaying sound
	// that answer is pulled about by which partials are loud, which is precisely what changes
	// when the plucking point moves. It read up to 1.4 cents sharp at the midpoint while the
	// fundamental had not moved at all. The fundamental is the claim being made here.
	std::printf("\nTHE SPLIT: the same loop wherever it is divided.\n");
	{
		const double hz = 146.83;   // cello D3, 327 samples a period
		double worst = 0.0;
		for (float b : {0.02f, 0.05f, 0.08f, 0.12f, 0.18f, 0.25f, 0.5f}) {
			BowedString s = string((float) hz, 3.f, 0.3f, b);
			s.pluck(1.f, 0.4f);
			const std::vector<float> x = play(s, 1.2f);
			const double c = cents(partialHz(x, 0.3, 0.5, hz), hz);
			char what[96];
			std::snprintf(what, sizeof what, "  contact %.2f of the length from the bridge", b);
			check(std::fabs(c) < 1.0, what, c);
			worst = std::fmax(worst, std::fabs(c));
		}
		check(worst < 1.0, "the contact point moves the fundamental by at most (cents)", worst);
	}

	// ---- HOW HARMONIC THE PARTIALS ARE ------------------------------------------------------
	// It has to be known before the bow goes in. The loop is solved at the fundamental only,
	// and the loss filter does not delay every partial alike, so there was no reason to assume
	// the rest would land on whole multiples. They do, within a tenth of a cent to the seventh.
	// Helmholtz motion is a periodic regime and wants a harmonic string, so this is the result
	// phase 2 needs; the eighth is at the edge of what the measurement can see rather than out.
	std::printf("\nTHE PARTIALS, against whole multiples of the fundamental.\n");
	{
		const double hz = 146.83;
		BowedString s = string((float) hz, 4.f, 0.3f, 0.12f);
		s.pluck(1.f, 0.9f);
		const std::vector<float> x = play(s, 1.2f);
		const double f1 = partialHz(x, 0.3, 0.5, hz);
		std::printf("  fundamental %.3f Hz, %+.2f cents from %.2f\n", f1, cents(f1, hz), hz);
		for (int k = 2; k <= 8; k++) {
			const double got = partialHz(x, 0.3, 0.5, f1 * k);
			std::printf("  partial %d: %+7.2f cents from whole\n", k, cents(got, f1 * k));
		}
	}

	// ---- THE CONTACT POINT IS A REAL PLACE ON THE STRING ------------------------------------
	// Arithmetic alone would let the two segments add up correctly while the junction sat
	// nowhere in particular. A string let go at a point cannot hold a partial that has a node
	// there, so plucking a fifth of the way along must lose the fifth partial and plucking a
	// quarter of the way along must lose the fourth. That is the split being a place.
	std::printf("\nTHE CONTACT POINT IS A PLACE: the partial with a node under it goes.\n");
	{
		const double hz = 146.83;
		for (int k : {4, 5, 6}) {
			const float b = 1.f / (float) k;
			BowedString s = string((float) hz, 3.f, 0.15f, b);
			s.pluck(1.f, 0.9f);
			const std::vector<float> x = play(s, 1.2f);
			const double gone = magnitudeAt(x, 0.1, 0.4, hz * k);
			double others = 0.0;
			int n = 0;
			for (int j = 2; j <= 8; j++)
				if (j != k) { others += magnitudeAt(x, 0.1, 0.4, hz * j); n++; }
			others /= (double) n;
			const double down = 20.0 * std::log10(gone / std::fmax(others, 1e-12));
			char what[96];
			std::snprintf(what, sizeof what,
				"  plucked at 1/%d: partial %d against its neighbours (dB)", k, k);
			check(down < -14.0, what, down);
		}
	}

	// ---- THE LOOP IS THE PERIOD -------------------------------------------------------------
	// Measured from the inside as well, because a pitch that is right for the wrong reason —
	// the two segments wrong and the filters making it up — would pass the check above.
	std::printf("\nTHE LOOP: both segments against the period they have to make.\n");
	{
		const double hz = 146.83;
		for (float b : {0.02f, 0.12f, 0.5f}) {
			BowedString s = string((float) hz, 2.f, 0.3f, b);
			s.process();
			const double period = RATE / hz;
			const double got = s.loopDelay();
			char what[96];
			std::snprintf(what, sizeof what,
				"  contact %.2f: loop %.1f of %.1f samples, short by", b, got, period);
			// The filters hold the rest, so the lines alone are a little under the period.
			check(got < period && got > period - 12.0, what, period - got);
		}
	}

	// ---- DECAY ------------------------------------------------------------------------------
	std::printf("\nDECAY, against the time asked for.\n");
	for (double want : {0.5, 2.0, 6.0}) {
		BowedString s = string(146.83f, (float) want);
		s.pluck(1.f, 0.4f);
		const std::vector<float> x = play(s, (float) want);
		const double got = decayOf(x, want);
		char what[96];
		std::snprintf(what, sizeof what, "  %.1f s asked, %.2f s measured, ratio", want, got);
		check(got > want * 0.7 && got < want * 1.4, what, got / want);
	}

	// ---- THE CONTACT POINT MOVING WHILE IT SOUNDS -------------------------------------------
	// What a player does on every note. Nothing may click, run away or detune.
	std::printf("\nMOVING THE CONTACT POINT while the string rings.\n");
	{
		const double hz = 146.83;
		BowedString s = string((float) hz, 6.f);
		s.pluck(1.f, 0.4f);
		std::vector<float> x((size_t) (2.0 * RATE));
		double biggestStep = 0.0, last = 0.0;
		for (size_t i = 0; i < x.size(); i++) {
			// Sweep from over the fingerboard to hard by the bridge and back, twice a second.
			const float t = (float) i / RATE;
			s.setContact(0.11f + 0.09f * std::sin(2.f * 3.14159265f * t));
			x[i] = s.process();
			if (i > (size_t) (0.05 * RATE))
				biggestStep = std::fmax(biggestStep, std::fabs((double) x[i] - last));
			last = x[i];
		}
		check(peakOf(x) < 4.0, "  stays bounded while the contact point sweeps, peak", peakOf(x));
		// A click is a step far larger than the waveform's own slope. At 147 Hz a sine of peak
		// one moves by at most 0.02 a sample, so anything over a tenth is a discontinuity.
		check(biggestStep < 0.1, "  no click: largest jump between samples", biggestStep);
		const double c = cents(pitchOf(x, 1.0, 1.9, hz), hz);
		check(std::fabs(c) < 2.0, "  still in tune after the sweep (cents)", c);
		writeWav("build/bowed-contact-sweep.wav", x);
	}

	// ---- STABILITY --------------------------------------------------------------------------
	std::printf("\nSTABILITY: nothing grows.\n");
	for (double hz : {65.41, 146.83, 440.0, 1318.51}) {
		BowedString s = string((float) hz, 8.f, 0.05f);
		s.pluck(1.f, 0.4f);
		const std::vector<float> x = play(s, 4.f);
		const double early = rmsOver(x, 0.2, 0.4), late = rmsOver(x, 3.6, 3.9);
		char what[96];
		std::snprintf(what, sizeof what, "  %.2f Hz, long decay: late over early", hz);
		check(late < early && late == late, what, late / std::fmax(early, 1e-12));
	}

	// ---- WHERE THE CONTACT POINT RUNS OUT ---------------------------------------------------
	// Not a pass or a fail: the figure phase 4 needs. A segment cannot be read in under about
	// two samples, so near the bridge the contact point asked for cannot be had.
	std::printf("\nHOW CLOSE TO THE BRIDGE THE BOW CAN GET, at 48 kHz.\n");
	std::printf("  %-18s %8s %10s   %s\n", "note", "period", "closest", "at 0.02 of the length");
	const Note range[] = {{65.41,"cello C2"}, {146.83,"cello D3"}, {220.00,"cello A3"},
	                      {196.00,"violin G3"}, {440.00,"violin A4"}, {659.26,"violin E5"},
	                      {1318.51,"violin E6"}, {2637.02,"violin E7"}};
	for (const Note& n : range) {
		float closest = 0.5f;
		for (float b = 0.005f; b <= 0.5f; b += 0.001f) {
			BowedString s = string((float) n.hz, 2.f, 0.3f, b);
			s.process();
			if (!s.contactClamped()) { closest = b; break; }
		}
		BowedString at2 = string((float) n.hz, 2.f, 0.3f, 0.02f);
		at2.process();
		std::printf("  %-18s %8.1f %10.3f   %s\n", n.name, RATE / n.hz, closest,
			at2.contactClamped() ? "out of reach" : "reached");
	}

	// ---- SOMETHING TO LISTEN TO -------------------------------------------------------------
	std::printf("\n");
	{
		// Pizzicato up a cello's strings, then the same note plucked at three points.
		std::vector<float> out;
		for (double hz : {65.41, 98.00, 146.83, 220.00}) {
			BowedString s = string((float) hz, 3.f, 0.25f, 0.15f);
			s.pluck(0.9f, 0.3f);
			const std::vector<float> x = play(s, 0.9f);
			out.insert(out.end(), x.begin(), x.end());
		}
		writeWav("build/bowed-pizzicato.wav", out);
	}
	{
		std::vector<float> out;
		for (float b : {0.04f, 0.12f, 0.3f}) {
			BowedString s = string(146.83f, 3.f, 0.25f, b);
			s.pluck(0.9f, 0.3f);
			const std::vector<float> x = play(s, 1.2f);
			out.insert(out.end(), x.begin(), x.end());
		}
		writeWav("build/bowed-pluck-positions.wav", out);
	}

	// =========================================================================================
	// PHASE 2: THE BOW
	// =========================================================================================
	bowPhase();

	std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "all passed");
	return failures ? 1 : 0;
}
