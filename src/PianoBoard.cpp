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

void PianoBoard::setSoundboardModes(int modes) {
	if (modes != wantedModes) {
		wantedModes = modes;
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
	board.setSampleRate((float) rate);
	board.setModeCount(wantedModes);
	board.setAdmittance((float) (1.0 / bridgeZ));
	board.prepare();
	lastAwake = 0;
}


void PianoBoard::hit(int midi, float metresPerSecond) {
	if (dirty)
		setup();
	if (!inCompass(midi))
		return;
	keyDown[midi - LOWEST] = true;
	refreshDampers();
	// THE DAMPER IS ALREADY CLEAR BY THE TIME THE HAMMER ARRIVES, which is how the action is
	// built: the key lifts the damper when it is about half way down and the hammer is thrown
	// from much further on. Letting the damper travel at its usual pace from the instant of the
	// blow would put felt on the string through the whole attack and take the note's edge off.
	notes[midi - LOWEST].settleDamper();
	notes[midi - LOWEST].hit(metresPerSecond);
	asleep[midi - LOWEST] = false;
}


void PianoBoard::hold(int midi) {
	if (dirty)
		setup();
	if (!inCompass(midi))
		return;
	keyDown[midi - LOWEST] = true;
	dampersStale = true;
	// NOT WOKEN. A string with nothing on it and nothing arriving is still silent; it will wake
	// when the bridge gives it something, which is exactly the point of holding it.
}


void PianoBoard::release(int midi) {
	if (dirty)
		setup();
	if (!inCompass(midi))
		return;
	keyDown[midi - LOWEST] = false;
	dampersStale = true;
}


void PianoBoard::setSustain(float amount) {
	const double a = std::fmax(0.0, std::fmin((double) amount, 1.0));
	if (a != sustainAt) {
		sustainAt = a;
		dampersStale = true;
	}
}


void PianoBoard::setSostenuto(bool down) {
	if (down == sostenutoDown)
		return;
	sostenutoDown = down;
	// WHAT IS CAUGHT IS WHAT WAS HELD AT THAT MOMENT, which is the whole of the pedal. Nothing
	// played afterwards is affected, because the mechanism has already moved under the dampers
	// that were up and cannot reach the ones that were down.
	for (int i = 0; i < NOTES; i++)
		caught[i] = down && keyDown[i];
	dampersStale = true;
}


void PianoBoard::setSoft(float amount) {
	const double a = std::fmax(0.0, std::fmin((double) amount, 1.0));
	if (a != softAt) {
		softAt = a;
		for (int i = 0; i < NOTES; i++)
			notes[i].setSoftPedal((float) softAt);
	}
}


/** WHAT EACH DAMPER IS BEING ASKED TO DO, which is the one place all three pedals and the keys
meet. A damper is off its strings if the key is down, or the sustain pedal is holding every damper
off, or the sostenuto pedal caught this one; otherwise it rests with whatever weight the sustain
pedal is not taking. The top two octaves have no damper to ask. */
void PianoBoard::refreshDampers() {
	dampersStale = false;
	for (int i = 0; i < NOTES; i++) {
		const double want = (keyDown[i] || caught[i]) ? 0.0 : 1.0 - sustainAt;
		notes[i].setDamperTarget((float) want);
	}
}


void PianoBoard::silence() {
	if (dirty)
		setup();
	for (int i = 0; i < NOTES; i++) {
		keyDown[i] = false;
		caught[i] = false;
	}
	refreshDampers();
	for (int i = 0; i < NOTES; i++) {
		notes[i].silence();
		asleep[i] = true;
	}
	board.silence();
	lastAwake = 0;
	lastVelocity = 0.0;
	dcIn = dcOut = 0.f;
}


float PianoBoard::process() {
	if (dirty)
		setup();
	if (dampersStale)
		refreshDampers();

	// A DAMPER ON THE MOVE KEEPS ITS NOTE AWAKE. A note that has gone quiet and is then asked for
	// its damper has nothing to do; but a note whose damper is still travelling has to be run, or
	// it would arrive at its new position without ever having passed through the positions
	// between, and a note released under a half pedal would stop as if it had been released under
	// none.
	for (int i = 0; i < NOTES; i++) {
		if (asleep[i] && notes[i].damperAt() != notes[i].damperGoingTo())
			notes[i].settleDamper();
	}

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
	// ONE JUNCTION AND NOT A SUM. Each string's impedance times twice the wave arriving along it,
	// against everything tied there including the board; what goes back along each string is the
	// junction's velocity less what came in.
	//
	// AND THE BOARD GIVES WAY DIFFERENTLY AT DIFFERENT FREQUENCIES, which is phase 6 and is the
	// reason this is no longer a division by a constant. Near one of the board's own modes the
	// bridge moves easily and a string tuned there loses its energy quickly; between modes it
	// hardly moves and a string hangs on. That is why a real piano is uneven from note to note.
	//
	// A LOOP WITH NO DELAY IN IT, SOLVED RATHER THAN BROKEN. The strings' force depends on how far
	// the bridge moves, and how far the bridge moves depends on the force, both within the same
	// sample. Putting a delay in to break it would be a sample of delay added to every string's
	// loop, which phase 1 tuned to a fraction of one — and a lie about the physics besides. So the
	// board is asked for the two halves of its answer separately: what it will do with no force at
	// all this sample, and how much it moves per newton applied now. Then
	//
	//     v = A (2W - S v) + c      and so      v = (2 A W + c) / (1 + A S)
	//
	// where W is the strings' weighted drive, S the sum of their impedances, A how much the board
	// gives per newton and c what it was already doing. One line, exact, and it cannot divide by
	// nothing, since A and S are both positive.
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
	const double give = board.instantly();
	const double velocity = (2.0 * give * weighted + board.standing()) / (1.0 + give * sumZ);
	// What the strings are pressing the bridge with, now that it is known how far it gives.
	board.drive(2.0 * weighted - sumZ * velocity);

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

	// WHAT COMES OUT IS THE BOARD'S SURFACE MOVING, which is the sound. Before phase 6 it was the
	// force on the bridge, which was the right signal with nothing to radiate it; now there is
	// something to radiate it, and a plate radiates its own motion — poorly at the bottom, where
	// its bending waves travel slower than sound and it pushes air about between its own crests
	// instead of away, and freely above that.
	const float out = board.radiated();

	// A DC blocker on the way out, outside every loop.
	const float y = out - dcIn + 0.995f * dcOut;
	dcIn = out;
	dcOut = y;
	return y;
}


} // namespace px
