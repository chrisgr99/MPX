#pragma once
/** One bowed string, physically modelled. See docs/bowed-strings.md.

NO RACK IN HERE, as with GuitarString and the performer: this builds into a command-line test that
measures the string and writes what it plays to a file, so a fault is a number rather than a vague
wrongness heard in a patch.

A DIGITAL WAVEGUIDE SPLIT AT THE BOW. GuitarString is one delay line closed in a loop, because a
plucked string is only ever touched at the start. A bowed string is driven continuously at one
point, so the travelling waves have to be available there: the string is four one-way delay lines,
a pair each side of the contact point, carrying what is going towards the bridge, coming back from
it, going towards the nut, and coming back from that. The bow is solved at the junction where the
four meet.

WHAT TRAVELS. Velocity waves, in metres a second, so that the bow's force in newtons and its speed
in metres a second mean what a player's do, and the published limits on them can be tested. What
arrives at the junction from the two sides adds to the velocity the string would have there if
nothing touched it; the bow's force adds the same velocity to both outgoing waves, the force over
twice the string's impedance, because a force at a point pushes both ways at once.

THE BOW IS FRICTION, solved once a sample. The classical model, in which the friction depends on
the sliding speed alone: the string either sticks to the hair and moves with it, or slips and is
held back by a dynamic friction that falls as it slips faster. That is McIntyre, Schumacher and
Woodhouse's, and it is known to get transients wrong — the rosin's friction depends on its
temperature, not its speed — but it gives Helmholtz motion and is the thing to test everything
else against. See docs/bowed-strings.md.

WHERE THE LOSSES ARE. The bridge takes the fundamental's loss, the same at every frequency: that is
the bridge's resistance, Schelleng's R, where the string's energy leaves for the body. What the
string loses inside itself grows with frequency and is spread along its length, so it is shared
between the two ends by the length of string on each side: beta of it in the bridge's reflection,
the rest in the nut's. A ripple running between the bow and the bridge then loses what that much
string would. The stiffness sits at the nut, at the far end of the long segment, where its few
samples of delay cannot move the bow.

THE SPLIT IS THE POINT OF THIS PHASE. Two half-lengths must add to the same loop a single line
would have given, and must keep adding to it while the contact point moves — which is what the
bow does on every note. Both are measured in test/bowtest.cpp. */
#include <cstdint>

namespace px {


struct BowedString {
	/** Long enough for 30 Hz at 192 kHz, which covers a double bass's low E oversampled. */
	static const int MAX_DELAY = 8192;

	BowedString();

	void setSampleRate(float rate);

	/** THE OVERSAMPLING SCHEDULE: how many times faster than `rate` the string should run to play a
	note at `hz` as it would at any faster rate — the smallest power of two giving the period at
	least SAMPLES_A_PERIOD samples, at most sixteen. Measured in phase 4 of docs/bowed-strings.md
	(make bowplay ARGS=oversampling): with fewer, Schelleng's limits move; at three hundred they are
	within an eighth of an octave of the fastest rates', mostly a twentieth. A low cello note needs none, the top of
	the violin's E string sixteen. */
	static const int SAMPLES_A_PERIOD = 300;
	static int oversampling(float hz, float rate);
	/** The pitch, in hertz. May change while the string sounds: that is a slide or a vibrato,
	and the loop's length follows it. */
	void setFrequency(float hz);
	/** How long the fundamental takes to fall by 60 dB once the bow leaves, in seconds. */
	void setDecay(float seconds);
	/** HOW FAST THE UPPER PARTIALS DIE, as a physical quantity rather than a filter setting: nought
	gives a partial at two kilohertz a Q of a thousand, 0.3 about three hundred — near what a cello
	string on its instrument measures — and one a Q of twenty. Set per hertz, so the same setting
	means the same string at any pitch and any sample rate, which oversampling needs. */
	void setDamping(float amount);
	/** How stiff the string is, nought to one. Less of a bowed string's character than of a
	piano's, and still there. */
	void setStiffness(float amount);
	/** NOTHING LOST AT EITHER END: the string of the analyses. A bench uses it to test the junction
	against Guettler's equations for how a note starts, which assume it. Never for playing: under
	a bow a string that loses nothing has no stable motion at all. */
	void setLossless(bool on);

	/** WHERE THE BOW TOUCHES, as a fraction of the string's length from the bridge. A player
	works between about 0.02, hard by the bridge, and 0.2, over the fingerboard. May move while
	the string sounds, which is how the tone is changed, so it is as smooth as the pitch. */
	void setContact(float beta);
	float contact() const { return beta; }
	/** WHAT THE CONTACT POINT COST, in samples: the shorter of the two segments. Below about two
	samples the interpolated read runs out of room and the contact point has to be held away from
	the bridge, which is the sample rate problem in docs/bowed-strings.md. Reported so a bench can
	say where it begins, and so the oversampling schedule can be measured rather than guessed. */
	float shortestSegment() const { return shortest; }
	/** True while the contact point being asked for cannot be had at this pitch and rate. */
	bool contactClamped() const { return clamped; }

	/** Plucks the string at the contact point: pizzicato. `level` nought to one is how hard,
	`hardness` nought to one is the finger. The shape is GuitarString's, halved into the two
	outgoing waves. */
	void pluck(float level, float hardness);

	/** THE STRING'S WAVE IMPEDANCE, in kilograms a second: the square root of its tension times
	its mass per metre. How far a given force moves it, so it is what makes the same bow need more
	force on a cello than on a violin. */
	void setImpedance(float kgPerSecond);
	float impedance() const { return z; }

	/** THE STRING TWISTS AS WELL AS MOVES. The bow's friction acts on the string's surface, so it
	turns the string about its axis as well as pushing it sideways, and the twist travels as waves of
	its own: faster, heavily damped, and carrying away what would otherwise ring between the bow and
	the bridge. `impedanceRatio` is the torsional wave impedance over the transverse one, nought for
	none; `speedRatio` how much faster the torsional waves travel; `q` how quickly they die. A second
	waveguide, split at the bow as the first is. The defaults are Woodhouse and Loach's cello D: 1.8
	against 0.55 kg/s, 5.2 times as fast, Q 45. */
	void setTorsion(float impedanceRatio, float speedRatio = 5.2f, float q = 45.f);

	/** THE BOW, as the player has it at this moment: how hard it presses, in newtons, and how
	fast it is drawn, in metres a second, positive for a down-bow and negative for an up-bow.
	Force nought is the bow off the string. Both may change every sample, which is what bowing is. */
	void setBow(float forceNewtons, float speedMetresPerSecond);

	/** THE FRICTION CURVE: the coefficient while sticking, the one the slipping falls towards, and
	the sliding speed, in metres a second, over which it falls halfway. The hyperbolic curve of the
	classical model; see process. */
	void setFriction(float staticCoefficient, float dynamicCoefficient, float fallSpeed);

	/** THE ROSIN AS BRISTLES: the elasto-plastic friction model (Dupont and colleagues 2002), as
	Serafin, Avanzini and Rocchesso put it at a waveguide's bow and Matusiak and Chatziioannou fitted
	it to a cello string (2024). The contact is a field of elastic bristles whose mean bend, z, is
	the model's memory: the friction is the bend's spring and damper, s0 z + s1 dz/dt; while the bend
	is small the bristles only bend and the string moves with the bow; past `breakaway` of the
	Coulomb bend they start to break; at the steady bend for the sliding speed all are broken and
	the string slides. So whether it sticks depends on how the contact got where it is, not only on
	the speed now, which a friction curve cannot say. Set field by field; the defaults are their
	fit for the epsilon curve. */
	struct Bristles {
		/** s0, newtons a metre: how stiff the bristles are. */
		float stiffness = 2.4099e5f;
		/** s1, kilograms a second: their internal damping. */
		float damping = 0.0115f;
		/** The coefficients stuck and sliding fast. */
		float muStatic = 1.1489f, muCoulomb = 0.3382f;
		/** How fast the steady friction falls with sliding speed, metres a second: epsilon for the
		curve mu_C + (mu_S - mu_C) e^(-|v| / epsilon), or the Stribeck speed for the Gaussian one. */
		float fall = 0.4f;
		/** The Gaussian Stribeck curve, e^(-(v / v_S)^2), in place of the exponential one. */
		bool gaussian = false;
		/** The breakaway bend, below which the bristles only bend, as a share of the Coulomb bend
		mu_C F / s0. */
		float breakaway = 0.7f;
		/** THE BOW HAIR GIVES, a spring and a damper between the stick and the rosin, so the hair
		where it touches the string lags the stick by however far the friction stretches it. Newtons
		a metre and kilograms a second; nought stiffness for hair that does not give. Matusiak and
		Chatziioannou's fit, over a centimetre-wide ribbon: 4.83e6 N/m^2 and 5.77e3 kg/(s m). */
		float hairStiffness = 4.8297e4f, hairDamping = 57.674f;
	};
	/** Friction from bristles with these properties, in place of the friction curve. */
	void setBristles(const Bristles& b);
	/** Back to the friction curve. */
	void useFrictionCurve();
	bool bristling() const { return bristled; }

	/** WHAT THE BOW DID LAST SAMPLE, for a bench: whether the string was stuck to the hair, the
	velocity of its surface under the bow in metres a second, and the friction force in newtons. */
	bool sticking() const { return stuck; }
	float velocityAtBow() const { return atBowVelocity; }
	float frictionForce() const { return atBowForce; }

	/** Stops the string at once. */
	void silence();

	/** One sample. What the string does at the bridge, which is what the body will hear. */
	float process();

	float frequency() const { return freq; }
	float sampleRate() const { return rate; }
	/** The whole loop's delay in samples, for a bench to check against the period. */
	float loopDelay() const { return 2.f * (toBridgeDelay + toNutDelay); }

private:
	float rate = 48000.f;
	float freq = 220.f;
	float decay = 2.f;
	float damping = 0.3f;
	float stiffness = 0.f;
	float beta = 0.12f;
	bool lossless = false;

	// THE FOUR LINES, named for what they carry: written at one end, read at the other.
	float bowToBridge[MAX_DELAY] = {};
	float bridgeToBow[MAX_DELAY] = {};
	float bowToNut[MAX_DELAY] = {};
	float nutToBow[MAX_DELAY] = {};
	int write = 0;

	// Worked out from the pitch, the decay, the damping, the stiffness and the contact point.
	float toBridgeDelay = 20.f;   // one way, bow to bridge, between samples
	float toNutDelay = 100.f;     // one way, bow to nut
	float shortest = 20.f;
	bool clamped = false;
	float lossS = 0.3f;           // the nut side's share of the string's own loss
	float lossSB = 0.f;           // the bridge side's
	float dispersionA = 0.f;
	float dispersionFor = 0.f;
	float dispersionStiff = -1.f;
	float gain = 0.99f;           // what is left after one round trip
	float nutReflect = 0.995f;    // what the nut sends back
	bool dirty = true;

	// The two loss filters' memories, and the stiffness allpasses'.
	float lastOut = 0.f, lastOutB = 0.f;
	static const int DISPERSION_STAGES = 4;
	float dIn[DISPERSION_STAGES] = {}, dOut[DISPERSION_STAGES] = {};
	float atBridge = 0.f;
	// The DC blocker's.
	float dcIn = 0.f, dcOut = 0.f;
	/** WHAT EACH END PASSES BACK UNTOUCHED: a steady velocity, exactly. See tune. The lowpass that
	carries it, its pole, and the nut's own. */
	float bridgeLow = 0.f, nutLow = 0.f, lowPole = 0.99f;

	// THE TWIST: four more lines, the same shape as the first four, a speedRatio-th as long.
	static const int TORSION_DELAY = MAX_DELAY / 4;
	float twistRatio = 3.27f, twistSpeed = 5.2f, twistQ = 45.f;
	float tBowToBridge[TORSION_DELAY] = {};
	float tBridgeToBow[TORSION_DELAY] = {};
	float tBowToNut[TORSION_DELAY] = {};
	float tNutToBow[TORSION_DELAY] = {};
	int tWrite = 0;
	float tToBridgeDelay = 4.f, tToNutDelay = 20.f;
	float tGain = 0.97f, tLowB = 0.f, tLowN = 0.f, tPole = 0.99f;
	// Its loss filters, one at each end, and their memories.
	float tLossB = 0.f, tLossN = 0.f, tLastB = 0.f, tLastN = 0.f;

	// The bow and the friction.
	float z = 0.5f;
	float bowForce = 0.f, bowSpeed = 0.f;
	/** THE PUBLISHED CURVE: Woodhouse and Galluzzo's hyperbolic fit, static 0.8, falling to 0.4,
	halfway at 0.2 m/s. */
	float muStatic = 0.8f, muDynamic = 0.4f, fallSpeed = 0.2f;
	bool stuck = false;
	/** The bristles, when they are what makes the friction: their properties, their bend and how
	fast it last changed. */
	bool bristled = false;
	Bristles bristles;
	double bend = 0.0, bendRate = 0.0, hairLag = 0.0;
	/** The sliding speed last sample, bow minus string: which way the string is slipping. */
	float slide = 0.f;
	float atBowVelocity = 0.f, atBowForce = 0.f;

	/** The velocity the bow adds to both outgoing waves, given what the string would be doing
	under it with nothing touching it. */
	float bowJunction(float unbowed);
	float bristleJunction(float unbowed);
	/** The bristles' steady bend for a sliding speed, signed with it, and what the bend does next. */
	double steadyBend(double slideSpeed) const;
	double bendSpeed(double slideSpeed, double z, bool* broken) const;

	// The pluck, played in over one period, half into each outgoing wave.
	float excite[MAX_DELAY] = {};
	int exciteLength = 0;
	int excitePos = 0;
	uint32_t noise = 22222u;

	void tune();
	float random();
	float read(const float* line, float d) const;
	float readTwist(const float* line, float d) const;
};


} // namespace px
