#pragma once
/** THE SOUNDBOARD. Phase 6 of docs/piano-model.md.

IT IS ONE THING AND NOT TWO, which is the whole idea here. A soundboard is usually modelled as a
tone filter bolted on the end — the strings make a signal and the board colours it — and that is two
objects where there is one. The board the strings are tied to and the board that radiates the sound
are the same piece of spruce, so the thing that decides how hard the bridge is to move is the same
thing that decides what comes out of it. Here it is one object, asked two questions:

	WHAT THE STRINGS FEEL. The bridge gives way, and how much it gives way at a frequency is this
	board's admittance there. Near one of its own modes it gives way easily and a string tuned to
	that frequency loses its energy quickly; between modes it hardly moves and a string hangs on.
	That is why a real piano is UNEVEN from note to note, and the unevenness is not a defect to be
	smoothed out — it is most of what makes an instrument sound like an instrument rather than
	like a synthesiser playing a piano patch.

	AND WHAT COMES OUT. The sound is the board's own surface moving, which is the same motion.

SO THE PHASES BEFORE THIS ONE HAD A SOUNDBOARD ALREADY — a very poor one. A plain resistance, the
same at every frequency, which is a board with no modes at all: perfectly even, and with no reason
to prefer one note over another.

HOW IT IS BUILT: A FEW MODES AND A SMOOTH REMAINDER.

A real soundboard has hundreds of modes, something like one every twenty hertz all the way up,
because a plate's modes crowd together as the square root of nothing in particular — they are
evenly spaced in frequency rather than in octaves. Modelling several hundred resonators would be
both expensive and pointless: above a few hundred hertz they overlap so heavily that what a string
sees is a smooth average rather than any one of them.

So the low ones, where they are sparse and individually audible, are resonators; everything above
is the smooth average they tend to, which is a plain admittance. That is what the earlier phases
had, so the calibration they were built on still holds.

NOTHING HERE IS A DELAY. The board responds to the force on it in the same sample, which makes a
loop with no delay in it between the strings and the board: the strings' force depends on how far
the bridge moves and how far the bridge moves depends on the force. That is solved rather than
broken with a delay, and it is one line of algebra — see `PianoBoard::process`, which asks this
object for the part of its answer that does not depend on the present force, and for the gain that
multiplies the part that does. */
#include <vector>

namespace px {


struct Soundboard {
	void setSampleRate(float hz);

	/** HOW READILY THE BRIDGE MOVES WELL AWAY FROM ANY MODE, in metres per second per newton. A
	piano soundboard's is around a two-thousandth, which is the figure the earlier phases were
	calibrated against as a plain resistance; the modes are added around it rather than instead of
	it, so those calibrations stand. */
	void setAdmittance(float metresPerSecondPerNewton);

	/** HOW MANY MODES ARE MODELLED ONE BY ONE. The rest are the smooth remainder. */
	void setModeCount(int modes);

	/** Builds the modes. Not for the audio thread, though it is quick. */
	void prepare();

	void silence();

	// ---- one sample, in the two parts a delay-free junction needs ----
	/** THE GAIN ON THE PRESENT FORCE: how much the bridge moves this sample per newton applied
	this sample. The junction needs it to solve itself. */
	double instantly() const { return instant; }
	/** WHAT THE BRIDGE WOULD DO THIS SAMPLE WITH NO FORCE AT ALL: everything the modes are
	already carrying. The other half of what the junction needs. */
	double standing() const;
	/** Advance by one sample, with the force the junction worked out. */
	void drive(double newtons);

	/** THE SOUND: the board's surface moving, as each mode's motion weighted by how well a mode
	of that frequency gets it into the air. */
	float radiated() const { return (float) sound; }
	/** How fast the bridge itself is moving, which is what the strings see. */
	float bridgeVelocity() const { return (float) velocity; }

	int modeCount() const { return (int) modes.size(); }
	float modeFrequency(int i) const { return (float) modes[(size_t) i].hz; }
	/** The admittance the whole board presents at a frequency, for the bench to plot. */
	float admittanceAt(float hz) const;

private:
	struct Mode {
		double hz = 0.0;
		double q = 40.0;
		double peak = 0.0;        // its admittance at its own frequency
		double radiates = 1.0;
		// The resonator: y = g (f - f[-2]) + a1 y[-1] + a2 y[-2].
		double g = 0.0, a1 = 0.0, a2 = 0.0;
		double y1 = 0.0, y2 = 0.0;
	};

	double rate = 48000.0;
	double smooth = 5.0e-4;
	int wanted = 28;
	bool built = false;
	bool flat = false;

	std::vector<Mode> modes;
	double instant = 5.0e-4;
	double force1 = 0.0, force2 = 0.0;
	double velocity = 0.0;
	double sound = 0.0;
	double smoothGain = 0.0, smoothPole = 0.0, smoothOut = 0.0;
	double smoothRadiates = 1.0;
};


} // namespace px
