/** See PianoNote.hpp. */
#include "PianoNote.hpp"

#include <algorithm>
#include <cmath>

namespace px {


void PianoNote::setSampleRate(float hz) {
	if (hz > 0.f && (double) hz != rate) {
		rate = hz;
		dirty = true;
	}
}

void PianoNote::setNote(float midi) {
	midi = std::fmax(12.f, std::fmin(120.f, midi));
	if ((double) midi != note) {
		note = midi;
		dirty = true;
	}
}

void PianoNote::setStringCount(int c) {
	if (c != countAsked) {
		countAsked = c;
		dirty = true;
	}
}

void PianoNote::setUnisonSpread(float cents) {
	if ((double) cents != spreadAsked) {
		spreadAsked = cents;
		dirty = true;
	}
}

void PianoNote::setBridgeImpedance(float times) {
	const double r = std::fmax(1.5, std::fmin((double) times, 100000.0));
	if (r != bridgeRatio) {
		bridgeRatio = r;
		dirty = true;
	}
}

void PianoNote::setFeltStiffness(float k) {
	feltKAsked = k;
	dirty = true;
}

void PianoNote::setFeltPower(float p) {
	feltPAsked = p;
	dirty = true;
}

void PianoNote::setHammerMass(float kg) {
	massAsked = kg;
	dirty = true;
}


/** THE STRINGS LAID OUT AND TUNED AGAINST EACH OTHER.

SPREAD EVENLY ABOUT THE NOTE, so that the note itself stays where it was put: three strings at
minus a half, nought and plus a half of the spread rather than at nought, a half and one. A tuner
tunes the note and then the unison to it, and a model that tuned the unison by pushing every string
upward would put the whole instrument sharp by a fraction of a cent, which is small but is the kind
of small that accumulates into an argument with the harmony module. */
void PianoNote::setup() {
	dirty = false;
	count = (countAsked > 0) ? std::min(countAsked, (int) MAX_STRINGS)
		: pianoStringsPerNote((float) note);
	const double spread = (spreadAsked >= 0.0) ? spreadAsked
		: (double) pianoUnisonSpread((float) note);

	// WHAT THE BRIDGE SENDS BACK WHEN THEY ALL MOVE TOGETHER: the strings in step present n times
	// one string's impedance to it, so the junction keeps (R - n) over (R + n) of what arrives.
	// This is not fed to the strings — their loss filters deliver their own internal losses and
	// the bridge's share is on top — but it is what decides how much faster the initial fall is
	// than the aftersound, so it is worth being able to ask for.
	keep = (bridgeRatio - (double) count) / (bridgeRatio + (double) count);

	hammerMass = (massAsked > 0.0) ? massAsked : (double) pianoHammerMass((float) note);
	feltP = (feltPAsked > 0.0) ? feltPAsked : (double) pianoFeltPower((float) note);
	feltK = (feltKAsked > 0.0) ? feltKAsked : (double) pianoFeltStiffness((float) note);

	for (int i = 0; i < count; i++) {
		const double at = (count > 1)
			? spread * ((double) i / (double) (count - 1) - 0.5) : 0.0;
		strings[i].setSampleRate((float) rate);
		strings[i].setNote((float) note);
		strings[i].setDetuneCents((float) at);
		// THE FIRST STRING SOLVES AND THE REST TAKE ITS ANSWER, which is a third of the work for
		// the same instrument: two hundred and sixty-four strings become eighty-eight designs.
		if (i == 0)
			strings[0].prepare();
		else
			strings[i].adoptDesign(strings[0]);
	}
}


void PianoNote::hit(float metresPerSecond) {
	if (dirty)
		setup();
	hammerSpeed = std::fmax(0.01, std::fmin((double) metresPerSecond, 12.0));
	hammerAt = 0.0;
	for (int i = 0; i < MAX_STRINGS; i++) {
		stringAt[i] = 0.0;
		force[i] = 0.0;
	}
	lastForce = 0.0;
	hammerOn = true;
	contactSamples = 0;
	hammerSamples = 0;
}


void PianoNote::silence() {
	if (dirty)
		setup();
	for (int i = 0; i < MAX_STRINGS; i++) {
		strings[i].silence();
		stringAt[i] = 0.0;
		force[i] = 0.0;
	}
	hammerOn = false;
	lastForce = 0.0;
	envelope = 0.0;
	dcIn = dcOut = 0.f;
}


/** ONE CONTACT'S FORCE, solved. The same scalar equation phase 2 derived: the compression left
over is what the hammer and the string have not closed between them, the felt's force goes as that
compression raised to a power, and the two sides of F = K (u0 - c F) to the power p cross exactly
once because one rises and the other falls. Newton's method, whose derivative here is one plus a
positive quantity, so it cannot divide by nothing. */
static double solveFelt(double u0, double c, double k, double p, double from) {
	if (u0 <= 0.0)
		return 0.0;
	double force = std::fmax(0.0, from);
	for (int i = 0; i < 6; i++) {
		const double u = std::fmax(u0 - c * force, 0.0);
		const double fromFelt = k * std::pow(u, p);
		const double slope = 1.0 + k * p * c * std::pow(u, p - 1.0);
		force -= (force - fromFelt) / slope;
		if (force < 0.0)
			force = 0.0;
	}
	return force;
}


/** The longest the felt is let stay on the string. A real contact is one to seven milliseconds and
this is thirty; it is here because a blow slow enough could in principle leave the hammer resting
against the string, and a hammer that never leaves is a hammer damping the note it just sounded. */
static const double CONTACT_LIMIT_MS = 30.0;


void PianoNote::advanceStrike() {
	if (dirty)
		setup();

	// ---- what has reached each strike point ----
	double arriving[MAX_STRINGS];
	for (int i = 0; i < count; i++)
		arriving[i] = (double) strings[i].arrivingAtStrike();

	// ---- the hammer, pressing on all of them at once ----
	//
	// THE CONTACTS ARE SEPARATE BUT THE HAMMER IS ONE, which is the only thing here that phase 2's
	// single contact did not have to think about. Each string compresses its own patch of felt by
	// its own amount — they are not in the same place, being a fraction of a cent apart and
	// drifting out of step — so each has its own force; but all those forces push back on one
	// hammer, so each depends a little on all the others through how fast the hammer is slowing.
	//
	// SOLVED BY GOING ROUND A FEW TIMES rather than as a system, and the arithmetic says why that
	// is enough: the term through which the contacts feel each other is the hammer's inertia, a
	// sample squared over its mass, and the term by which each stands alone is a sample over its
	// string's impedance. At a piano's numbers the second is a hundred times the first, so the
	// coupling is a small correction to nearly independent solves and three passes settle it.
	double total = 0.0;
	if (hammerOn) {
		const double dt = 1.0 / rate;
		const double inertia = dt * dt / hammerMass;
		double was[MAX_STRINGS];
		for (int i = 0; i < count; i++) {
			// How far this string and the hammer would overlap by the end of the sample if no
			// force passed between them, bar what the other strings do to the hammer.
			was[i] = (hammerAt + dt * hammerSpeed) - (stringAt[i] + dt * arriving[i]);
			total += force[i];
		}
		for (int pass = 0; pass < 3; pass++) {
			double sum = 0.0;
			for (int i = 0; i < count; i++) {
				const double c = dt / (2.0 * (double) strings[i].impedanceOf());
				force[i] = solveFelt(was[i] - inertia * total, c, feltK, feltP, force[i]);
				sum += force[i];
			}
			total = sum;
		}
		lastForce = total;

		hammerSpeed -= dt * total / hammerMass;
		hammerAt += dt * hammerSpeed;
		hammerSamples++;
		if (total > 0.0)
			contactSamples++;
		// The felt leaves when nothing is pressing anywhere and the hammer is going away. Both
		// conditions: a hammer stopped dead by the strings is not finished with them, and one
		// still pressing has not left however it is moving.
		if (total <= 0.0 && hammerSpeed <= 0.0)
			hammerOn = false;
		else if ((double) hammerSamples > CONTACT_LIMIT_MS * 0.001 * rate)
			hammerOn = false;
	}
	else {
		for (int i = 0; i < count; i++)
			force[i] = 0.0;
		lastForce = 0.0;
	}

	// HOW MUCH THE NOTE IS MOVING, kept as it goes because PianoBoard needs it every sample and
	// would otherwise have to go and ask every string. A falling memory with the largest thing
	// seen written straight into it: it rises at once when a note is struck and takes about a
	// tenth of a second to forget, which is slow enough that it never reports a note as finished
	// between two cycles of its own waveform.
	double most = std::fabs(lastForce);
	for (int i = 0; i < count; i++) {
		strings[i].pushStrike((float) force[i]);
		stringAt[i] += (double) strings[i].strikeVelocity() / rate;
		most = std::fmax(most, std::fabs((double) strings[i].strikeVelocity()));
	}
	envelope = std::fmax(most, envelope * (1.0 - 10.0 / rate));
}


float PianoNote::process() {
	advanceStrike();

	// ---- the bridge, with nothing on it but this note ----
	//
	// A JUNCTION OF THE NOTE'S STRINGS AND A LOAD. Each string's impedance times twice the wave
	// arriving along it, over the impedances of everything meeting there including the bridge's
	// own: that is the junction's velocity, and what goes back along each string is the junction's
	// velocity less what came in. A rigid bridge is an infinite impedance, which gives a velocity
	// of nought and sends back exactly what it was sent, inverted — the plain reflection phase 2
	// had. A bridge of finite impedance moves instead, and because it moves, every string of the
	// note hears every other one.
	//
	// AND THE DOUBLE DECAY IS THAT AND NOTHING MORE. Strings in step add up at the junction and
	// shift it, so they lose energy into it quickly; strings out of step cancel there, barely move
	// it, and keep what they have. Two rates, from one junction.
	double weighted = 0.0, impedances = 0.0;
	double atBridge[MAX_STRINGS];
	for (int i = 0; i < count; i++) {
		const double z = (double) strings[i].impedanceOf();
		atBridge[i] = (double) strings[i].arrivingAtBridge();
		weighted += z * atBridge[i];
		impedances += z;
	}
	const double bridgeZ = bridgeRatio * (impedances / (double) count);
	const double velocity = 2.0 * weighted / (impedances + bridgeZ);
	for (int i = 0; i < count; i++)
		strings[i].returnFromBridge((float) (velocity - atBridge[i]));

	// WHAT COMES OUT IS THE FORCE ON THE BRIDGE, not its motion. The bridge is where the string
	// drives the soundboard, and the force is what survives a bridge being made rigid: as the
	// impedance rises the velocity falls away to nothing while the force it is pressed with does
	// not. Phase 6 replaces this resistance with a soundboard that has modes of its own.
	const float out = (float) (bridgeZ * velocity);

	// A DC blocker on the way out, outside every loop: the loops' tuning is worked out to a
	// fraction of a sample and a filter inside one would have to be accounted for in that.
	const float y = out - dcIn + 0.995f * dcOut;
	dcIn = out;
	dcOut = y;
	return y;
}


} // namespace px
