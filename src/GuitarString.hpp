#pragma once
/** One guitar string, physically modelled. See docs/guitar-model.md.

NO RACK IN HERE, as with the performer and the other engines: it builds into a command-line test
that measures the string — its tuning, its decay, where the pick puts the nodes — and writes what
it plays to a file, so a fault is a number rather than a vague wrongness heard in a patch.

A DIGITAL WAVEGUIDE, the extended Karplus-Strong string. A delay line one period of the note long
is closed in a loop through a filter that takes a little energy away on every pass, more of it
from the upper partials, which is how a string's brightness dies before its body does. The loop's
length is the pitch; the loss per pass is the sustain; what is put into the loop when it is
plucked is the pick.
*/
#include <cstdint>

namespace px {


struct GuitarString {
	/** Long enough for 30 Hz at 192 kHz, below the lowest note of a seven-string's low B. */
	static const int MAX_DELAY = 8192;

	GuitarString();

	void setSampleRate(float rate);
	/** The pitch, in hertz. May change while the string rings: that is a bend, a slide or a
	vibrato, and the loop's length follows it. */
	void setFrequency(float hz);
	/** How long the fundamental takes to fall by 60 dB, in seconds. */
	void setDecay(float seconds);
	/** How much faster the upper partials die than the fundamental: nought, as slowly, to one,
	far faster, as on old strings. */
	void setDamping(float amount);
	/** How stiff the string is, nought to one: a stiff string's upper partials sit a little sharp
	of whole multiples of the fundamental, which is much of what makes steel sound like steel. */
	void setStiffness(float amount);

	/** Adds to what goes into the string at the bridge on the next sample: another string's
	vibration, coming through the bridge. */
	void drive(float amount) { driven += amount; }
	/** What the string is doing at the bridge, for driving the others. */
	float bridge() const { return lastRead; }

	/** Plucks the string. `level` nought to one is how hard; `hardness` nought to one is the
	pick, from a fingertip to a hard plectrum; `position` is where along the string, as a
	fraction of its length from the bridge, from about 0.02 to 0.5. A string still ringing is
	struck again, not cut off: caught first, as a pick or finger catches it, its old vibration
	cut to a quarter while the new pluck goes in. */
	/** `caught`: the string is caught by the pick or finger as it is struck, whatever it was
	doing, so the old vibration goes and the new note sounds at its own level. False for a knock
	that leaves the string ringing, as a hammer-on's does. */
	void pluck(float level, float hardness, float position, bool caught = true);

	/** Stops the string at once. */
	void silence();

	/** One sample of the string's sound. */
	float process();

	float frequency() const { return freq; }
	float sampleRate() const { return rate; }

private:
	float rate = 48000.f;
	float freq = 110.f;
	float decay = 4.f;
	float damping = 0.3f;
	float stiffness = 0.f;

	float line[MAX_DELAY] = {};
	int write = 0;

	// The loop, worked out from the pitch, the decay, the damping and the stiffness.
	float delay = 100.f;      // samples from writing to reading, between samples
	float lossS = 0.3f;       // the loss filter's pole
	float dispersionA = 0.f;  // the stiffness allpasses' coefficient
	float dispersionFor = 0.f;    // the pitch and stiffness it was worked out for
	float dispersionStiff = -1.f;
	float gain = 0.99f;       // what is left after one pass
	bool dirty = true;

	// The loop filters' memories.
	float lastOut = 0.f;
	static const int DISPERSION_STAGES = 4;
	float dIn[DISPERSION_STAGES] = {}, dOut[DISPERSION_STAGES] = {};
	float lastRead = 0.f;
	float driven = 0.f;
	// The DC blocker's.
	float dcIn = 0.f, dcOut = 0.f;

	// THE PLUCK, played into the loop one sample at a time over one period.
	float excite[MAX_DELAY] = {};
	int exciteLength = 0;
	int excitePos = 0;
	/** How much of the old vibration survives the pick catching the string: applied to the loop
	for the period the new pluck takes to go in, so every sample of the loop is caught once. */
	float catchGain = 1.f;
	uint32_t noise = 22222u;

	void tune();
	float random();
};


} // namespace px
