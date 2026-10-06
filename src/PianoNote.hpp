#pragma once
/** ONE NOTE OF A PIANO: its two or three strings, the one hammer that strikes them, and the bridge
that joins them. Phase 3 of docs/piano-model.md.

WHY THE NOTE IS THE OBJECT AND NOT THE STRING. A piano note is not a string; it is a small choir of
them, slightly out of tune with each other and joined at one end. Everything interesting about the
start and the end of a piano note comes from that:

	THE HAMMER PUSHES THEM ALL AT ONCE, so they resist it together. Three strings are three times
	as hard to shift as one, the hammer's momentum drains three times as fast, and the contact is
	correspondingly shorter — which phase 2 had to fake with a count, because a lone string had
	nobody to share the blow with.

	AND THE BRIDGE LETS THEM HEAR EACH OTHER, which is where the double decay comes from. Strings
	moving together drive the bridge hard and lose their energy quickly; strings moving against
	each other barely move it and hang on long after. A piano note therefore falls fast at first
	and then much more slowly — the quick fall into a long aftersound that no envelope shape
	reproduces convincingly, because it is not a shape but a consequence of two rates.

	AND BEING SLIGHTLY OUT OF TUNE IS WHAT MAKES IT AUDIBLE. Exactly tuned unisons would stay in
	phase for ever and the aftersound would never arrive; a few tenths of a cent apart, they drift
	out of step over a second or two, and the note changes colour as it dies.

HOW A SAMPLE GOES, which is why PianoString exposes four calls rather than a `process`:

	every string says what has reached its strike point,
	the hammer, knowing all of that, works out its force and how it is shared,
	every string takes its share and sends its waves away,
	every string says what has reached the bridge,
	the bridge, knowing all of that, works out its own motion and what goes back,
	and every string takes its returning wave.

Neither the hammer nor the bridge can be a string's private business, because both need every
string's state at the same instant. */
#include "PianoString.hpp"

#include <vector>

namespace px {


struct PianoNote {
	/** No piano has more than three strings to a note. */
	static const int MAX_STRINGS = 3;

	void setSampleRate(float hz);

	/** THE NOTE, as a MIDI number. How many strings it has, how far apart they are tuned, what its
	hammer weighs and how hard its felt is all follow from it. */
	void setNote(float midi);

	/** How many strings, overriding the stringing. Not positive puts it back. Chiefly for the
	bench, which wants to measure one string by itself. */
	void setStringCount(int count);
	/** How far apart the strings are tuned, in cents from lowest to highest. Negative puts the
	curve back; nought is a tuner's unreachable ideal and sounds dead. */
	void setUnisonSpread(float cents);

	/** THE BRIDGE'S IMPEDANCE, as a multiple of one string's.

	ABOUT A THOUSAND, which is a soundboard's measured driving-point impedance of a thousand-odd
	kilograms per second against a string's two. The number decides two things at once: how much
	of the sound leaves the string at every reflection, and how strongly the strings of a choir
	hear one another — which is to say how far apart the two decay rates are. A rigid bridge is
	an infinite impedance: no sound comes out and the strings are deaf to each other.

	AND IT IS EASY TO MAKE IT FAR TOO LOSSY WITHOUT NOTICING. A hundred and forty was tried and is
	five times a midrange note's whole decay budget: every note came out ringing for a fifth of
	the time it should, and the two rates came out the wrong way round, with the aftersound dying
	faster than the attack. At a thousand the bridge's share is one to two times the string's own,
	so the initial fall is two to three times faster than the aftersound, and more so in the
	three-strung middle than in the single-strung bass — which is how a real piano behaves. */
	void setBridgeImpedance(float timesStringImpedance);

	/** The hammer's felt and mass, overriding the curves. Not positive puts the curve back. */
	void setFeltStiffness(float newtonsPerMetreToThePower);
	void setFeltPower(float power);
	void setHammerMass(float kilograms);

	/** STRUCK at this many metres per second. A speed and not a level, because that is the only
	thing a pianist's finger decides; everything else follows. About a fifth of a metre per second
	is as quietly as a note can be made to speak and five or six is as hard as anyone can hit it.

	RINGING STRINGS ARE NOT STOPPED. The hammer meets whatever they are already doing, which is
	what makes a note restruck before its damper has landed sound as it does on a piano. */
	void hit(float metresPerSecond);

	void silence();

	/** One sample: the force the strings put on the bridge, which is what drives a soundboard and
	is therefore what this instrument's sound is. Phase 6 puts a real soundboard on the end of it;
	for now the bridge is a plain resistance and this is the sound. */
	float process();

	// ---- what it came out as, for the bench and the phases after this one ----
	bool inContact() const { return hammerOn; }
	/** How long the last blow's contact lasted, in milliseconds. */
	float contactMilliseconds() const { return (float) (1000.0 * contactSamples / rate); }
	/** The total force the felt last returned, over all the strings, in newtons. */
	float hammerForce() const { return (float) lastForce; }
	int stringCount() const { return count; }
	/** The strings themselves, for asking what the compass gave them. */
	PianoString& string(int i) { return strings[i]; }
	const PianoString& string(int i) const { return strings[i]; }
	/** What the bridge sends back when the strings all move together: what decides how much faster
	the initial fall is than the aftersound. */
	float bridgeReflection() const { return (float) keep; }

private:
	double rate = 48000.0;
	double note = 57.0;
	int countAsked = 0;
	double spreadAsked = -1.0;
	double bridgeRatio = 1000.0;
	double feltKAsked = -1.0, feltPAsked = -1.0, massAsked = -1.0;

	int count = 3;
	double keep = 1.0;
	double feltK = 1e10, feltP = 2.5, hammerMass = 0.008;
	bool dirty = true;

	PianoString strings[MAX_STRINGS];

	// The hammer, one of it, pressing on all of them.
	double hammerAt = 0.0, hammerSpeed = 0.0;
	double stringAt[MAX_STRINGS] = {};
	double force[MAX_STRINGS] = {};
	double lastForce = 0.0;
	bool hammerOn = false;
	int contactSamples = 0, hammerSamples = 0;

	float dcIn = 0.f, dcOut = 0.f;

	void setup();
};


} // namespace px
