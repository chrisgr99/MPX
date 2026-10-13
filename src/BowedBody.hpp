#pragma once
/** THE BODY OF A BOWED INSTRUMENT. Phase 5 of docs/bowed-strings.md.

WHAT IT DOES. The strings push on the bridge; the bridge rocks on the top plate; the body's modes
move; the moving surfaces push on the air. This takes the force the strings put on the bridge, in
newtons, and gives back the sound, as the pressure a metre away in pascals — the body's
radiativity, which is how the measurements describe a violin's voice.

ONE WAY. What is heard does not push back on the strings. The bridge the strings feel is a second
body built from the same kind of modes, the low ones with how readily they let the bridge move
(`mobility`), which BowedInstrument runs at the strings' rate. See the doc, phase 6.

HOW IT IS BUILT, as the deprecated piano's soundboard was: the low modes, the instrument's
signature, one resonator each, and the dense ones above, where they overlap and no one of them is
heard, generated from the body's modal density and damping — deterministically, so an instrument
is the same instrument every time it is built. Every mode is a two-pole resonator driven by the
force; its velocity weighted by how well that mode radiates is its share of the sound.

THE BRIDGE stands between the strings and the body with a resonance of its own, its rocking on its
feet: below it the bridge passes the strings' force through, around it lifts it, and above it lets
less through. That, on the body's dense upper modes, is the bridge hill — the broad rise between
two and three kilohertz that much of a violin's brilliance comes from. A second-order filter on the
force, ahead of the modes.

NO RACK IN HERE, so a bench can measure it. */
#include <cstdint>
#include <vector>

namespace px {


struct BowedBody {
	/** ONE MODE: its frequency, its quality factor, how readily it lets the bridge move at its peak
	(metres a second per newton), and how much sound it makes there (pascals a metre away per
	newton on the bridge). */
	struct Mode {
		float hz = 0.f;
		float q = 30.f;
		float mobility = 0.f;
		float radiativity = 0.f;
	};

	/** A POINT ON A CURVE: a frequency and a level in decibels. Between points a curve is a
	straight line on a logarithmic frequency scale, and beyond its ends it is flat. */
	struct Point {
		float hz;
		float db;
	};

	/** THE BODY'S DENSE UPPER MODES, as statistics: from `fromHz` to `toHz`, `perHz` modes a hertz
	on average, each damped by the loss factor `loss` (Q = 1 / loss). Their radiativity averaged
	over a band follows `radiativity`, in decibels re a pascal per newton a metre away, and their
	mobility at the bridge `mobility`, in decibels re a metre a second per newton. Their
	frequencies are scattered off an even grid and their strengths spread as measured modal
	strengths are, both from `seed`. */
	struct Dense {
		float fromHz = 1000.f, toHz = 8000.f;
		float perHz = 0.02f;
		float loss = 0.02f;
		std::vector<Point> radiativity;
		std::vector<Point> mobility;
		uint32_t seed = 1u;
	};

	/** THE BRIDGE'S ROCKING RESONANCE: its frequency and quality factor. Nought frequency for none. */
	struct Bridge {
		float hz = 3000.f;
		float q = 2.f;
	};

	/** EVERYTHING ABOUT ONE INSTRUMENT'S BODY, assembled field by field. */
	struct Design {
		std::vector<Mode> modes;
		Dense dense;
		Bridge bridge;
	};

	void setSampleRate(float hz);
	/** Builds the resonators. Quick, and not for the audio thread. */
	void setDesign(const Design& d);
	const Design& design() const { return wanted; }

	void silence();

	/** ONE SAMPLE: the force the strings put on the bridge, in newtons, in; the pressure a metre
	away, in pascals, out. */
	float process(float bridgeForce);

	/** How fast the bridge is moving, metres a second: what the strings would feel. */
	float bridgeVelocity() const { return (float) velocity; }

	/** THE BRIDGE'S MOTION THIS SAMPLE IN TWO PARTS, for strings that push on it and are moved by
	it in the same sample (BowedInstrument): how fast it moves per newton of this sample's force,
	and how fast it would move with no force this sample, from what its modes already carry. Its
	velocity is then the one times the force plus the other, exactly, so the instrument can solve
	for the force and the motion together with no delay between them. */
	double velocityPerNewton() const;
	double velocityUnforced() const;

	/** THE RESPONSE AT A FREQUENCY, worked out from the resonators as built: radiativity in pascals
	per newton, and mobility in metres a second per newton. For a bench. */
	float radiativityAt(float hz) const;
	float mobilityAt(float hz) const;
	int resonators() const { return (int) built.size(); }

	/** HEARD AND NOT FELT: a body whose motion nothing reads back — the instrument's voice, from the
	force on the bridge to the sound. Its modes are then run two at a time, side by side, and only the
	sound is summed, which is several times faster for the hundreds of a dense body. The sound differs
	from running them one at a time only in the order the last sum is added in, and goes nowhere but
	out. The bridge as the strings feel it is in the loop with them, and is never run this way. */
	void setVoiceOnly(bool on) { voiceOnly = on; }

private:
	struct Resonator {
		double hz = 0.0, q = 30.0, mobility = 0.0, radiativity = 0.0;
		// y = b (x - x[-2]) + a1 y[-1] + a2 y[-2]: a resonance whose peak gain is one.
		double b = 0.0, a1 = 0.0, a2 = 0.0;
		double y1 = 0.0, y2 = 0.0;
	};

	double rate = 48000.0;
	Design wanted;
	std::vector<Resonator> built;
	bool voiceOnly = false;
	/** THE MODES TWO AT A TIME, for a voice: each pair's coefficients, memories and radiativity side by
	side, an odd one paired with a silent one. */
	typedef double Two __attribute__((vector_size(16)));
	std::vector<Two> pairB, pairA1, pairA2, pairY1, pairY2, pairRadiativity;
	// The bridge filter's coefficients and memories, a biquad.
	double bb0 = 1.0, bb1 = 0.0, bb2 = 0.0, ba1 = 0.0, ba2 = 0.0;
	double bx1 = 0.0, bx2 = 0.0, by1 = 0.0, by2 = 0.0;
	// The force through the bridge, two samples back, which every resonator shares.
	double f1 = 0.0, f2 = 0.0;
	double velocity = 0.0;
	/** velocityPerNewton, which depends only on the coefficients. */
	double perNewton = 0.0;

	void build();
	/** The bridge filter's gain at a frequency, for the response functions. */
	double bridgeGainAt(double hz) const;
	/** One resonator's complex response at a frequency, peak one. */
	void resonance(const Resonator& r, double hz, double* re, double* im) const;
};


} // namespace px
