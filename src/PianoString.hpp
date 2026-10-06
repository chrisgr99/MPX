#pragma once
/** ONE PIANO STRING AND THE HAMMER THAT STRIKES IT, physically modelled. Phases 1 and 2 of
docs/piano-model.md.

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

AND A FELT HAMMER, WHICH IS WHAT MAKES IT A PIANO AND NOT A HARP.

	THE STRING IS SPLIT AT THE STRIKE POINT, which is the structural price of having a hammer at
	all. A hammer is not a shape put into a string; it is a mass in contact with it for a
	millisecond or two, pushing while the string pushes back. To work that out there has to be a
	PLACE on the string where the pushing happens, so the one loop of phase 1 becomes two
	sections — strike point to the agraffe, strike point to the bridge — meeting at a junction
	where the force goes in. The round trip is the same length through the same filters, so every
	number phase 1 fitted still holds.

	AND THE MISSING PARTIALS THEN HAPPEN RATHER THAN BEING ARRANGED. A mode with a node at the
	strike point cannot be moved by a force applied there, so striking an eighth of the way along
	leaves the eighth partial and the sixteenth all but absent — which is true of a real piano,
	and is one reason the strike point is where it is. Phase 1's placeholder faked this with the
	corner of a triangle; now it is a consequence of where the force enters.

	FELT IS A NONLINEAR SPRING, and that is the whole reason for the trouble. The force it returns
	goes as its compression raised to a power of between two and three, so a harder blow
	compresses it further, which makes it STIFFER, which SHORTENS the contact and puts the energy
	higher up the spectrum. It is why a piano brightens when it is played harder rather than only
	getting louder, and no envelope or filter reproduces it.

	SOLVED IMPLICITLY, EVERY SAMPLE. The force depends on how far the felt is compressed, which
	depends on where the string has moved to, which depends on the force, all within one sample.
	Done explicitly — with last sample's string position — it is wrong at a whisper and unstable
	at full force. It is instead solved as one scalar equation per sample, which has exactly one
	root and is reached by Newton's method in a handful of steps from any start.

WHAT IS NOT HERE YET. One string is one string: the choir of two or three coupled at a bridge is
phase 3, the whole board phase 4, the dampers and pedals phase 5. */
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
the bass bridge upward. The hammer needs this even when only one string is modelled, because the
strings of a note resist it together and so decide how long the contact lasts. */
int pianoStringsPerNote(float midi);

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

	/** THE HAMMER'S FELT, overriding the curves. Not positive puts the curve back. */
	void setFeltStiffness(float newtonsPerMetreToThePower);
	void setFeltPower(float power);
	void setHammerMass(float kilograms);

	/** STRUCK, by the hammer, at this many metres per second.
	
	A SPEED AND NOT A LEVEL, because that is the only thing a pianist's finger actually decides and
	everything else follows from it: how loud, how bright, how long the felt stays on the string.
	About a fifth of a metre per second is as quiet as a note can be made to speak, and five or six
	is as hard as anyone can hit it.
	
	A RINGING STRING IS NOT STOPPED. The hammer meets whatever the string is already doing and
	pushes against it, which is what makes a note restruck before its damper has landed sound as it
	does on a piano. */
	void hit(float metresPerSecond);

	/** Whether the felt is still on the string. */
	bool inContact() const { return hammerOn; }
	/** How long the last blow's contact lasted, in milliseconds — the measurement phase 2 lives or
	dies by, since it must fall from about four at a whisper toward one at full force. */
	float contactMilliseconds() const { return (float) (1000.0 * contactSamples / rate); }
	/** The force the felt last returned, in newtons. */
	float hammerForce() const { return (float) lastForce; }
	/** How many strings the note has, which the hammer pushes together even while one is modelled. */
	int stringsPerNote() const { return strings; }

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
	/** The whole samples from the strike point to the agraffe and back. Nought when the note is
	too short to divide: at the top of the compass a period is a dozen samples, and there is no
	room for a junction in it. */
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
	double readDelay = 100.0;
	bool dirty = true;
	// What the chain was fitted for, so a cent of detuning does not refit it.
	double fitFreq = 0.0, fitB = -1.0, fitRate = 0.0, fitPower = -1.0, fitDecay = -1.0;
	int fitSplit = -1;

	// ---- the hammer ----
	double feltKAsked = -1.0, feltPAsked = -1.0, massAsked = -1.0;
	double feltK = 1e10, feltP = 2.5, hammerMass = 0.008, impedance = 2.0;
	int strings = 3;
	double hammerAt = 0.0, hammerSpeed = 0.0, stringAt = 0.0;
	double lastForce = 0.0;
	bool hammerOn = false;
	int contactSamples = 0, hammerSamples = 0;

	// ---- the two sections ----
	// A POWER OF TWO EACH AND NO LONGER THAN THE NOTE NEEDS. Eighty-eight notes three strings deep
	// is two hundred and sixty-odd of these, and lines long enough for the bottom A would spend
	// megabytes on strings that are four inches long.
	//
	// EACH LINE IS A SECTION'S WHOLE ROUND TRIP — out to the end and back — rather than a pair of
	// one-way lines. The two are equivalent and this is half the memory and half the arithmetic;
	// the end's reflection is applied once where the wave comes back.
	std::vector<float> rightLine, leftLine;
	unsigned rightMask = 0, leftMask = 0;
	unsigned rightWrite = 0, leftWrite = 0;
	int leftDelay = 0;        // whole samples: strike point to the agraffe and back
	int bridgeTap = 0;        // where in the right line the wave is at the bridge

	static const int MAX_STAGES = 48;
	float chainIn[MAX_STAGES] = {}, chainOut[MAX_STAGES] = {};
	float shelfB0[LOSS_SHELVES] = {1.f, 1.f, 1.f}, shelfB1[LOSS_SHELVES] = {};
	float shelfA1[LOSS_SHELVES] = {};
	float shelfIn[LOSS_SHELVES] = {}, shelfOut[LOSS_SHELVES] = {};
	float lastRead = 0.f;
	float driven = 0.f;
	float dcIn = 0.f, dcOut = 0.f;

	void tune();
	void setupHammer();
	void fitChain();
	void solveLoss();
	void refreshGain();
	void solveRead();
	void resize();
};


} // namespace px
