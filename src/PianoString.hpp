#pragma once
/** ONE PIANO STRING, physically modelled. Phases 1 to 3 of docs/piano-model.md.

THE HAMMER AND THE BRIDGE BELONG TO THE NOTE AND NOT TO THE STRING, and PianoNote has them. One
hammer strikes all of a note's strings at once and one bridge joins them, so neither can be a
string's private business; a string that owned its own hammer could only ever be a solo. What is
here is the string itself, and an interface built so that a note can put a force on one end of it
and a bridge on the other.

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
	the power by which the partials above it die faster.

AND IT IS CUT IN THREE PIECES, so that a hammer can push one end of it and a bridge can hold the
other.

	THE STRIKE POINT IS A PLACE AND SO IT NEEDS A JUNCTION. A hammer is not a shape put into a
	string; it is a mass in contact with it, pushing while the string pushes back, so there has to
	be somewhere on the string where the pushing happens. Phase 1's one loop therefore becomes a
	section from the strike point to the agraffe and a section from the strike point to the
	bridge.

	AND THE MISSING PARTIALS THEN HAPPEN RATHER THAN BEING ARRANGED. A mode with a node at the
	strike point cannot be moved by a force applied there, so striking an eighth of the way along
	leaves the eighth partial and the sixteenth all but absent — which is true of a real piano,
	and is one reason the strike point is where it is. Phase 1's placeholder faked this with the
	corner of a triangle.

	AND THE BRIDGE IS A PLACE TOO, which is phase 3's price. A rigid end can be a sign turned over
	in the middle of a delay line, and that is what phase 2 had; a bridge that MOVES cannot,
	because the note's other strings are pushing on it at the same moment. So the section to the
	bridge is two one-way lines with the bridge between them, and the string hands its arriving
	wave out and takes a returning one back rather than reflecting for itself.

	THE ROUND TRIP IS THE SAME LENGTH THROUGH THE SAME FILTERS, so every number phase 1 fitted
	still holds — which the bench re-measures rather than assumes.

	ALWAYS CUT, EVEN AT THE TOP OF THE COMPASS. The top C's period is eleven samples and a
	fourteenth of that is under one, so phase 2 left those notes undivided and had to special-case
	the sign of the loop and the junction's arithmetic, getting both wrong first. A section of one
	whole sample is no worse a model of a very short string and it is the same model as every
	other note, so the shortest left section is one sample and there are no special cases.

HOW A SAMPLE GOES, which matters because the hammer and the bridge both need the string's state
before they can decide anything:

	`arrivingAtStrike` — what has reached the strike point from both sides. The hammer needs this
	from every string of the note before it can work out its force.
	`pushStrike` — the string's share of that force, which closes the junction and sends waves off
	in both directions.
	`arrivingAtBridge` — what has reached the bridge. The bridge needs this from every string.
	`returnFromBridge` — what the bridge sends back, which ends the sample. */
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

/** THE STRING'S WAVE IMPEDANCE, in kilograms per second: its mass per unit length times the speed
waves travel along it. This is what decides how hard the string pushes back at the hammer, and so
how long the two stay in contact. A few kilograms per second in the bass, a fraction of one at the
top, where the strings are thin and short. */
float pianoImpedance(float midi);

/** HOW MANY STRINGS THE NOTE HAS: one in the bottom octave, two for a stretch above it, three from
the bass bridge upward. */
int pianoStringsPerNote(float midi);

/** HOW FAR APART A TUNER LEAVES THE STRINGS OF ONE NOTE, in cents from the lowest to the highest.
Nought would be a tuner's ideal and nobody reaches it: a few tenths of a cent is a clean unison and
about one is a tired one. It is not a defect to be minimised in the model either — it is where the
shimmer of a piano note comes from, and a note whose strings are exactly in tune sounds dead. */
float pianoUnisonSpread(float midi);

/** THE HAMMER'S MASS, in kilograms: about eleven grams in the bass falling to about four at the
top, which is most of why the treble speaks so much more quickly. */
float pianoHammerMass(float midi);

/** THE POWER THE FELT'S FORCE GOES AS. Between two and three, rising toward the treble, where the
felt is harder and thinner. The nonlinearity itself is what brightens a piano under a harder blow;
this number is how sharply. */
float pianoFeltPower(float midi);

/** THE FELT'S STIFFNESS, in newtons per metre raised to that power. Set by measurement rather than
quoted: it is whatever makes the contact last as long as a real hammer's does, which the bench
reports directly. */
float pianoFeltStiffness(float midi);

/** WHERE ALONG THE STRING THE HAMMER LANDS, as a fraction of the speaking length from the agraffe.
About an eighth for most of the compass, and a smaller fraction at the top. */
float pianoStrikeFraction(float midi);


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

	/** WORK OUT THE DESIGN NOW rather than on the first sample.

	The fits are lazy by default, so that setting a note and then setting three other things about
	it costs one design and not four. The cost of that is that until the first sample has gone
	through, every accessor below is reporting the LAST note's answers — which is a quiet trap: a
	measurement taken straight after setting the note reads a frequency of 220 Hz whatever the note
	is, and silently measures the wrong thing. PianoNote calls this as soon as it has finished
	setting its strings up, so nothing outside ever sees a stale answer. */
	void prepare();

	/** TAKE ANOTHER STRING'S DESIGN instead of solving for one.

	THE THREE STRINGS OF A NOTE ARE THE SAME STRING to within a fraction of a cent, and solving
	the dispersion chain and the loss filter three times over to three identical answers is three
	times the work for nothing. This is not an approximation smuggled in: `tune` already holds a
	design good across a twenty-fourth of an octave before refitting, which is a hundred times the
	spread of a unison, so the claim being made here is one the design arithmetic makes anyway.

	WHAT IS NOT COPIED is the delay line's own length and the flat gain, which depend on this
	string's own pitch and are re-solved from it. That is the cheap part — no search and no fit —
	and it is what makes the detuning real rather than nominal. */
	void adoptDesign(const PianoString& other);

	void silence();	// ---- one sample, in four parts. See the note at the top of this file. ----
	/** What has reached the strike point from both sides, which is what the hammer compresses
	against. */
	float arrivingAtStrike();
	/** This string's share of the hammer's force, in newtons. Closes the strike junction and sends
	the waves away from it. */
	void pushStrike(float newtons);
	/** What has reached the bridge: the wave the bridge is being driven by. */
	float arrivingAtBridge() const;
	/** What the bridge sends back into this string, which ends the sample. */
	void returnFromBridge(float wave);

	/** The string's own velocity at the strike point, from the last `pushStrike`: what the hammer
	is resting against. */
	float strikeVelocity() const { return (float) lastJunction; }
	/** Its wave impedance, which the hammer and the bridge both need. */
	float impedanceOf() const { return (float) impedance; }

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
	/** The whole samples from the strike point to the agraffe and back. Never less than one. */
	int strikeSplit() const { return leftDelay; }

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
	bool dirty = true;
	// What the chain was fitted for, so a cent of detuning does not refit it.
	double fitFreq = 0.0, fitB = -1.0, fitRate = 0.0, fitPower = -1.0, fitDecay = -1.0;
	int fitSplit = -1;

	double impedance = 2.0;

	// ---- the three lines ----
	// A POWER OF TWO EACH AND NO LONGER THAN THE NOTE NEEDS. Eighty-eight notes three strings deep
	// is two hundred and sixty-odd of these, and lines long enough for the bottom A would spend
	// megabytes on strings that are four inches long.
	//
	// THE AGRAFFE END IS ONE LINE CARRYING THE WHOLE ROUND TRIP, out and back, with the end's
	// inversion applied once where the wave returns: an end that cannot move needs no junction, so
	// there is nothing to intercept halfway and this is half the memory and half the arithmetic.
	// THE BRIDGE END CANNOT BE DONE THAT WAY and is two one-way lines, because the bridge moves
	// and the note's other strings have to be heard at the same instant.
	std::vector<float> leftLine, outLine, backLine;
	unsigned leftMask = 0, outMask = 0, backMask = 0;
	unsigned writeAt = 0;
	int leftDelay = 1;        // whole samples: strike point to the agraffe and back
	int outDelay = 1;         // whole samples: strike point to the bridge
	double readDelay = 100.0; // and back again, between samples: the fractional part of the loop
	// Within a sample: what arrived from each side, kept between the four calls.
	double fromLeft = 0.0, fromRight = 0.0, lastJunction = 0.0;

	static const int MAX_STAGES = 48;
	float chainIn[MAX_STAGES] = {}, chainOut[MAX_STAGES] = {};
	float shelfB0[LOSS_SHELVES] = {1.f, 1.f, 1.f}, shelfB1[LOSS_SHELVES] = {};
	float shelfA1[LOSS_SHELVES] = {};
	float shelfIn[LOSS_SHELVES] = {}, shelfOut[LOSS_SHELVES] = {};

	void tune();
	void fitChain();
	void solveLoss();
	void refreshGain();
	void solveRead();
	void resize();
};


} // namespace px
