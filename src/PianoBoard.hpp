#pragma once
/** THE WHOLE INSTRUMENT: eighty-eight notes, every string of them tied to one bridge. Phase 4 of
docs/piano-model.md.

WHY ONE BRIDGE AND NOT EIGHTY-EIGHT. Phase 3 gave each note a bridge of its own, which is enough to
make the strings of one note hear each other and so to produce the double decay. It is not a piano.
On a real instrument every string is tied down to the same bridge on the same soundboard, and that
single fact is what makes a piano sound like a room rather than like a set of strings:

	STRIKING ONE NOTE SETS THE OTHERS RINGING. A note's partials arrive at the bridge, the bridge
	moves, and every other string in the instrument is driven by that motion — strongly if it
	happens to be in tune with one of those partials, imperceptibly otherwise. Strike a C and the
	G a twelfth above it starts to sound, because the C's third partial is the G's first.

	AND IT IS NOT A FEATURE THAT WAS ADDED. There is no resonance code here. The strings were
	always there and the bridge was always shared; what follows from that is what a piano does.
	The sustain pedal of phase 5 will work for the same reason — lift the dampers and the strings
	that were being held still are free to be driven — and will need no resonance code either.

	THE OTHER STRINGS ALSO LOAD THE BRIDGE, which is the same fact seen from the other side. Two
	hundred and sixty strings hanging on a bridge make it harder to move than three do, so every
	note's decay depends a little on the whole instrument being there. That is why this cannot be
	done by mixing eighty-eight independent notes together: it is one junction, not a sum.

WHAT PHASE 3 MEASURED STAYS TRUE, because PianoNote still has a `process` of its own that puts a
private bridge on one note. That is a note on a bridge with nothing else attached, which is exactly
the thing phases 1 to 3 were measured on, and keeping it means those measurements still mean what
they said. */
#include "PianoNote.hpp"

#include <vector>

namespace px {


struct PianoBoard {
	/** A piano's compass: the bottom A to the top C. */
	static const int LOWEST = 21;
	static const int HIGHEST = 108;
	static const int NOTES = HIGHEST - LOWEST + 1;

	PianoBoard();

	/** BUILDING IT IS THE EXPENSIVE PART AND IT HAPPENS HERE. Every string has its dispersion
	chain and its loss filter solved for, which is thousands of trigonometric calls each and two
	hundred and sixty-odd strings. It is done once, when the sample rate is set, and never while
	the instrument is being played. Not for the audio thread. */
	void setSampleRate(float hz);

	/** THE BRIDGE'S IMPEDANCE, as a multiple of the average string's. About a thousand: a
	soundboard's measured driving-point impedance against a string's two. */
	void setBridgeImpedance(float timesStringImpedance);

	/** Strike a note, at this many metres per second at the hammer. Out-of-compass notes are
	ignored rather than clamped: a piano has eighty-eight keys and pretending otherwise would put
	two notes on one set of strings. */
	void hit(int midi, float metresPerSecond);

	void silence();

	/** One sample: the force the whole instrument puts on its bridge. Phase 6 puts a soundboard
	with modes of its own on the end of this; for now the bridge is a plain resistance and this is
	the sound. */
	float process();

	/** THE FLOOR BELOW WHICH A NOTE IS LEFT ALONE. A string nobody has struck and nothing is
	driving costs nothing, which is what makes silence free; the threshold is what counts as
	nothing. Set in the same units as the output. */
	void setFloor(float level);

	// ---- what it is doing, for the bench ----
	/** How many notes were awake on the last sample. */
	int awake() const { return lastAwake; }
	/** How many of the compass's notes exist at all, which is all of them. */
	int noteCount() const { return NOTES; }
	bool inCompass(int midi) const { return midi >= LOWEST && midi <= HIGHEST; }
	PianoNote& note(int midi) { return notes[midi - LOWEST]; }
	const PianoNote& note(int midi) const { return notes[midi - LOWEST]; }
	/** The total of every string's impedance, which is what makes the bridge hard to move. */
	float hangingImpedance() const { return (float) sumZ; }

private:
	double rate = 48000.0;
	double bridgeRatio = 1000.0;
	double floorLevel = 1e-7;
	double sumZ = 0.0;
	double bridgeZ = 2000.0;
	bool dirty = true;
	int lastAwake = 0;
	double lastVelocity = 0.0;

	PianoNote notes[NOTES];
	bool asleep[NOTES] = {};

	float dcIn = 0.f, dcOut = 0.f;

	void setup();
};


} // namespace px
