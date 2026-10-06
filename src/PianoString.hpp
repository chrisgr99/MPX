#pragma once
/** ONE PIANO STRING, physically modelled. Phase 1 of docs/piano-model.md.

NO RACK IN HERE, as with the guitar's string and the performer: it builds into a command-line
bench that measures it — the frequency of every partial, the stretch fitted to them, how long each
takes to die — so that a fault is a number rather than a vague wrongness heard in a patch.

NOT GuitarString WITH DIFFERENT NUMBERS, though it is the same kind of thing: a delay line closed
through a loss filter with an allpass chain for stiffness. The guitar's string is asked for an
amount of stiffness and a decay in seconds, and those are the wrong questions to ask a piano.

	A PIANO STRING IS NOT GIVEN ITS STIFFNESS, IT IS GIVEN ITS NOTE. The bench's first finding
	was that one stiffness control cannot express a piano, because the partial stretch is least
	near the middle of the compass and rises toward both ends — gently into the wound bass,
	steeply at the top where the strings are only a few inches long. So this string is told which
	note it is and takes its own stretch, its own decay and its own losses from that.

	THE STRETCH IS ASKED FOR AS ITSELF. Not "how stiff", but B, the inharmonicity coefficient:
	the nth partial sits at n times the fundamental times the square root of one plus B n squared.
	That is the number the literature publishes, the number the bench measures, and the number a
	piano tuner's stretched octaves follow, so it is the number the model takes.

	THE ALLPASS CHAIN IS FITTED, NOT GUESSED. The guitar's string places its eighth partial a
	fixed number of cents sharp and lets the rest fall where they may, which is enough for steel
	strings and nowhere near enough here: being right at the eighth and wrong at the sixteenth is
	audible as a note that will not quite tune. This one solves for the chain — how many stages
	and what coefficient — by working out where the loop ACTUALLY puts every partial up to the
	twenty-fourth, including what the interpolated read and the loss filter do to them, and
	choosing the chain whose worst partial is least wrong in cents.

	THE READ'S OWN STRETCH IS SUBTRACTED RATHER THAN IGNORED. Reading the delay line between
	samples is itself slightly dispersive, which the bench measured as three ten-thousandths of
	stretch at A5 with the stiffness control at nought — small, growing with pitch, and in the
	same direction as the real thing, so a model that merely added its own stretch on top would
	come out doubly stretched in the treble. Because the fit asks where the partials actually
	land, it cancels this by itself: at B of nought it chooses a chain that FLATTENS the partials
	by exactly as much as the read sharpens them.

	THE DECAY IS TWO NUMBERS, NOT ONE. How long the fundamental takes to fall sixty decibels, and
	how much faster the eighth partial does. A loop closed through a lowpass cannot make its upper
	partials ring LONGER than one-over-n, which is a real limit and not a shortcoming: a piano's
	high partials die faster than that, so the model is on the right side of it.

WHAT IS NOT HERE YET. The hammer is phase 2 and this string is struck with a stand-in, a plain
triangle as the guitar's pick leaves. One string is one string: the choir of two or three coupled
at a bridge is phase 3, the whole board phase 4, the dampers and pedals phase 5. */
#include <vector>

namespace px {


/** THE INHARMONICITY OF A NOTE on a medium grand, as the published measurements have it: least
around the lower middle, rising toward both ends. Representative rather than exact — instruments
differ by a factor of two or more, and phase 9 puts the curve on the panel — but the SHAPE is not
in doubt, and it is the shape that makes the difference between a piano and a harp. */
float pianoInharmonicity(float midi);

/** HOW LONG THE FUNDAMENTAL OF A NOTE RINGS, damper up, in seconds to fall sixty decibels: tens of
seconds in the bass, a fraction of one at the top. */
float pianoDecaySeconds(float midi);


struct PianoString {
	/** THE LOOP'S LOSS: three first-order shelves, each a pole with its zero further from one.
	Why three, and why shelves rather than poles or a symmetric FIR, is argued where they are
	fitted — with the measurements of the forms that were tried and rejected. Public only because
	the arithmetic that fits them lives outside the class, with the rest of the filter design. */
	static const int LOSS_SHELVES = 3;
	struct Loss {
		double pole[LOSS_SHELVES] = {0.0, 0.0, 0.0};
		double zero[LOSS_SHELVES] = {0.0, 0.0, 0.0};
	};

	PianoString();

	void setSampleRate(float hz);

	/** THE NOTE, as a MIDI number and fractional if need be. Everything the compass decides
	follows from it: the pitch, the stretch, how long it rings. */
	void setNote(float midi);
	/** Out of tune by this many cents, which is how the two or three strings of one note are made
	to beat against each other. Does not change what the note takes from the compass. */
	void setDetuneCents(float cents);

	/** B, overriding the curve. Negative puts the curve back. */
	void setInharmonicity(float b);
	/** The fundamental's time to fall sixty decibels, overriding the curve. Not positive puts the
	curve back. */
	void setDecaySeconds(float seconds);
	/** HOW MUCH FASTER THE UPPER PARTIALS DIE, as the exponent of a power law: the partial at
	frequency f falls sixty decibels in the fundamental's time multiplied by f0 over f, raised to
	this.

	NOUGHT IS FREE AND MEANS NOTHING IS ADDED. Everything in the loop comes round once per period
	of the FUNDAMENTAL — the delay line is one of those long, and the twentieth partial is going
	round it just as often as the first — so a loop gain that treats every frequency alike gives
	every partial the same decay, and the whole of this power law is the lowpass's work. Real
	pianos are somewhere near one, which is to say the eighth partial dies in about an eighth of
	the fundamental's time. Below nought the upper partials would have to outlast the fundamental,
	which a lossy loop cannot do, so it is clamped and `decayExponentReached` reports what the
	loop actually settled for rather than what it was told. */
	void setDecayExponent(float power);

	/** STRUCK, for now, as a guitar's string is picked: the string's own shape when it is let go,
	a triangle with its apex at the strike point. A PLACEHOLDER, and a visible one — the whole
	point of phase 2 is that a felt hammer is not this. What it does have right is the strike
	point, a seventh or so of the way along, which is why the seventh and ninth partials are weak
	on a real piano: a triangle cornered at a point has no partial with a node there. */
	void strike(float level, float position = 0.14f);

	/** Adds to what goes into the string at the bridge on the next sample: another string's
	vibration, arriving through the bridge. */
	void drive(float amount) { driven += amount; }
	/** What this string is doing at the bridge, for driving the others. */
	float bridge() const { return lastRead; }

	void silence();
	float process();

	// ---- what it came out as, for the bench and for the phases after this one ----
	float frequency() const { return (float) freq; }
	float inharmonicity() const { return (float) bUsed; }
	float decaySeconds() const { return (float) t60Used; }
	int stages() const { return chainStages; }
	float dispersion() const { return (float) chainA; }
	/** The worst partial's error, in cents, that the fitted chain leaves. */
	float fitErrorCents() const { return (float) fitError; }
	/** What the decay power law came out as: fitted to the loop's own gains, so it is what the
	string will actually do and not what it was asked for. */
	float decayExponentReached() const { return (float) powerReached; }
	/** How far the fitted loss misses that law at its worst partial, as a percentage of that
	partial's own decay time. */
	float decayFitError() const { return (float) lossError; }

private:
	// ---- what it was asked for ----
	double rate = 48000.0;
	double note = 57.0;
	double detune = 0.0;
	double bAsked = -1.0;        // negative: from the curve
	double decayAsked = -1.0;    // not positive: from the curve
	double powerAsked = 1.15;

	// ---- what it works out from that ----
	double freq = 220.0;
	double bUsed = 1e-4;
	double t60Used = 8.0;
	double passGain = 0.999;
	double powerReached = 1.0;
	double lossError = 0.0;
	int chainStages = 4;
	double chainA = 0.0;
	double fitError = 0.0;
	Loss loss;
	double readDelay = 100.0;
	bool dirty = true;
	// What the chain was fitted for, so a cent of detuning does not refit it.
	double fitFreq = 0.0, fitB = -1.0, fitRate = 0.0, fitPower = -1.0, fitDecay = -1.0;

	// ---- the loop ----
	// A POWER OF TWO AND NO LONGER THAN THE NOTE NEEDS. Eighty-eight notes three strings deep is
	// two hundred and sixty-odd of these, and a fixed line long enough for the bottom A would
	// spend eight megabytes on strings that are four inches long.
	std::vector<float> line;
	unsigned mask = 0;
	unsigned write = 0;

	static const int MAX_STAGES = 48;
	float chainIn[MAX_STAGES] = {}, chainOut[MAX_STAGES] = {};
	float shelfB0[LOSS_SHELVES] = {1.f, 1.f, 1.f}, shelfB1[LOSS_SHELVES] = {};
	float shelfA1[LOSS_SHELVES] = {};
	float shelfIn[LOSS_SHELVES] = {}, shelfOut[LOSS_SHELVES] = {};
	float lastRead = 0.f;
	float driven = 0.f;
	float dcIn = 0.f, dcOut = 0.f;

	// The strike, played into the loop a sample at a time over one period.
	std::vector<float> excitation;
	int excitePos = 0;

	void tune();
	void fitChain();
	void solveLoss();
	void refreshGain();
	void solveRead();
	void resize();
};


} // namespace px
