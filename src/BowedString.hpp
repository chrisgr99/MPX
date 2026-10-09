#pragma once
/** One bowed string, physically modelled. See docs/bowed-strings.md.

NO RACK IN HERE, as with GuitarString and the performer: this builds into a command-line test that
measures the string and writes what it plays to a file, so a fault is a number rather than a vague
wrongness heard in a patch.

A DIGITAL WAVEGUIDE SPLIT AT THE BOW. GuitarString is one delay line closed in a loop, because a
plucked string is only ever touched at the start. A bowed string is driven continuously at one
point, so the travelling waves have to be available there: the string is four one-way delay lines,
a pair each side of the contact point, carrying what is going towards the bridge, coming back from
it, going towards the nut, and coming back from that. The bow will be solved at the junction where
the four meet, in phase 2. This phase has no bow in it.

WHAT TRAVELS. Velocity waves. The junction then adds the bow's velocity to both outgoing waves,
which is the form the friction model wants, and the bridge sees the sum of what arrives and what
it sends back.

WHERE THE LOSSES ARE. At the bridge, because that is where a string's energy actually leaves: the
loss filter and the dispersion allpasses sit in the bridge reflection, so each is crossed once per
round trip exactly as GuitarString's are. The nut reflects almost everything.

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
	/** The pitch, in hertz. May change while the string sounds: that is a slide or a vibrato,
	and the loop's length follows it. */
	void setFrequency(float hz);
	/** How long the fundamental takes to fall by 60 dB once the bow leaves, in seconds. */
	void setDecay(float seconds);
	/** How much faster the upper partials die than the fundamental: nought, as slowly, to one,
	far faster. */
	void setDamping(float amount);
	/** How stiff the string is, nought to one. Less of a bowed string's character than of a
	piano's, and still there. */
	void setStiffness(float amount);

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

	/** Plucks the string at the contact point: pizzicato, and the only way to set it going until
	there is a bow. `level` nought to one is how hard, `hardness` nought to one is the finger.
	The shape is GuitarString's, halved into the two outgoing waves. */
	void pluck(float level, float hardness);

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
	float lossS = 0.3f;
	float dispersionA = 0.f;
	float dispersionFor = 0.f;
	float dispersionStiff = -1.f;
	float gain = 0.99f;           // what is left after one round trip
	bool dirty = true;

	// The bridge reflection's memories.
	float lastOut = 0.f;
	static const int DISPERSION_STAGES = 4;
	float dIn[DISPERSION_STAGES] = {}, dOut[DISPERSION_STAGES] = {};
	float atBridge = 0.f;
	// The DC blocker's.
	float dcIn = 0.f, dcOut = 0.f;

	// The pluck, played in over one period, half into each outgoing wave.
	float excite[MAX_DELAY] = {};
	int exciteLength = 0;
	int excitePos = 0;
	uint32_t noise = 22222u;

	void tune();
	float random();
	float read(const float* line, float d) const;
};


} // namespace px
