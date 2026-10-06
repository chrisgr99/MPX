/** See PianoBoard.hpp. */
#include "PianoBoard.hpp"

#include <algorithm>
#include <cmath>

namespace px {


PianoBoard::PianoBoard() {
	for (int i = 0; i < NOTES; i++)
		notes[i].setNote((float) (LOWEST + i));
}


void PianoBoard::setSampleRate(float hz) {
	if (hz > 0.f && (double) hz != rate) {
		rate = hz;
		dirty = true;
	}
}

void PianoBoard::setBridgeImpedance(float times) {
	const double r = std::fmax(1.5, std::fmin((double) times, 100000.0));
	if (r != bridgeRatio) {
		bridgeRatio = r;
		dirty = true;
	}
}

void PianoBoard::setFloor(float level) {
	floorLevel = std::fmax(0.0, (double) level);
}


/** EVERY STRING BUILT, AND THE BRIDGE WEIGHED.

THE SUM OF THE IMPEDANCES IS TAKEN OVER EVERY STRING IN THE INSTRUMENT AND NOT OVER THE ONES THAT
HAPPEN TO BE RINGING, which matters and is easy to get wrong. A string is tied to the bridge whether
or not it is moving: it is still there, it still has to be shifted, and leaving a sleeping string
out of the sum would make the bridge easier to move the quieter the music got. Every note's decay
would then creep as the texture thinned, and a chord would die at a different rate from the notes
that make it up. So the sum is a constant of the instrument, fixed here, and sleeping only ever
leaves a string out of the other sum — the one weighted by what the strings are actually doing,
which a silent string contributes nothing to anyway. */
void PianoBoard::setup() {
	dirty = false;
	sumZ = 0.0;
	double mean = 0.0;
	int strings = 0;
	for (int i = 0; i < NOTES; i++) {
		notes[i].setSampleRate((float) rate);
		notes[i].setNote((float) (LOWEST + i));
		// The note's own bridge is made rigid, so that if anyone calls its `process` it is plainly
		// a note by itself rather than a note with a second bridge quietly in series.
		notes[i].setBridgeImpedance(100000.f);
		notes[i].silence();
		for (int j = 0; j < notes[i].stringCount(); j++) {
			sumZ += (double) notes[i].string(j).impedanceOf();
			mean += (double) notes[i].string(j).impedanceOf();
			strings++;
		}
		asleep[i] = true;
	}
	mean = (strings > 0) ? mean / (double) strings : 2.0;
	bridgeZ = bridgeRatio * mean;
	lastAwake = 0;
}


void PianoBoard::hit(int midi, float metresPerSecond) {
	if (dirty)
		setup();
	if (!inCompass(midi))
		return;
	notes[midi - LOWEST].hit(metresPerSecond);
	asleep[midi - LOWEST] = false;
}


void PianoBoard::silence() {
	if (dirty)
		setup();
	for (int i = 0; i < NOTES; i++) {
		notes[i].silence();
		asleep[i] = true;
	}
	lastAwake = 0;
	lastVelocity = 0.0;
	dcIn = dcOut = 0.f;
}


float PianoBoard::process() {
	if (dirty)
		setup();

	// ---- everything up to the bridge, for the notes that are doing anything ----
	//
	// A NOTE NOBODY HAS STRUCK AND NOTHING IS DRIVING COSTS NOTHING, which is what makes silence
	// free. It is an optimisation rather than a necessity — the whole instrument ringing at once
	// measures under a third of one processor — so it is allowed to be simple.
	//
	// ONE CRITERION AND NOT TWO, which is the whole of the lesson here. The first attempt slept a
	// note when its own motion was under the floor and woke it when the bridge was over it, and
	// those are different questions asked at different moments: a note woken by the bridge has not
	// moved YET, so it was put straight back to sleep in the same sample, before it had been given
	// anything to be driven by. Nothing ever resonated, and the measurement that was meant to
	// prove phase 4 reported a piano with one string in it.
	//
	// So the bridge's motion decides both. While the bridge is moving, nothing sleeps — every
	// string is being driven and has a right to be heard. When the bridge has gone quiet, a note
	// that is itself quiet may sleep. The two can then never disagree.
	const bool bridgeQuiet = std::fabs(lastVelocity) <= floorLevel;
	int count = 0;
	for (int i = 0; i < NOTES; i++) {
		if (asleep[i]) {
			if (bridgeQuiet)
				continue;
			// A sympathetic resonance waking up. It is meant to happen: the saving is in the
			// silence between the music, not during it.
			asleep[i] = false;
		}
		notes[i].advanceStrike();
		if (bridgeQuiet && !notes[i].inContact() && (double) notes[i].moving() < floorLevel) {
			asleep[i] = true;
			continue;
		}
		count++;
	}
	lastAwake = count;

	// ---- the bridge: every string in the instrument, and the soundboard ----
	//
	// One junction and not a sum. Each string's impedance times twice the wave arriving along it,
	// over the impedances of everything tied there including the soundboard's own; what goes back
	// along each string is the junction's velocity less what came in.
	double weighted = 0.0;
	for (int i = 0; i < NOTES; i++) {
		if (asleep[i])
			continue;
		PianoNote& n = notes[i];
		for (int j = 0; j < n.stringCount(); j++) {
			PianoString& s = n.string(j);
			weighted += (double) s.impedanceOf() * (double) s.arrivingAtBridge();
		}
	}
	const double velocity = 2.0 * weighted / (sumZ + bridgeZ);
	for (int i = 0; i < NOTES; i++) {
		if (asleep[i])
			continue;
		PianoNote& n = notes[i];
		for (int j = 0; j < n.stringCount(); j++) {
			PianoString& s = n.string(j);
			s.returnFromBridge((float) (velocity - (double) s.arrivingAtBridge()));
		}
	}

	lastVelocity = velocity;

	// The force on the bridge, not its motion: the bridge is where the strings drive the
	// soundboard, and the force is what survives a bridge being made rigid.
	const float out = (float) (bridgeZ * velocity);

	// A DC blocker on the way out, outside every loop.
	const float y = out - dcIn + 0.995f * dcOut;
	dcIn = out;
	dcOut = y;
	return y;
}


} // namespace px
