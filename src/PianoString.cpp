/** See PianoString.hpp. */
#include "PianoString.hpp"

#include <algorithm>
#include <cmath>
#ifdef PIANO_FIT_TRACE
#include <cstdio>
#endif

namespace px {


static const double PI = 3.14159265358979323846;

/** THE PARTIALS THE FIT LOOKS AT. Twenty-four is past where a piano's partials carry much weight
and well past where being a few cents out is audible as anything, but a chain that is right to the
twenty-fourth is right everywhere below it, and one fitted only to the eighth is not. */
static const int FIT_PARTIALS = 24;

/** AND NOT ABOVE THIS, whatever the sample rate allows. Fitting partials up to half the sample
rate is fitting what nobody can hear at the expense of what everybody can: at A5 the twenty-fourth
partial is above twenty kilohertz, the interpolated read has taken most of it away already, and
asking the two-parameter loss filter to get its decay right as well threw the fit for every
partial below it out by a factor of two hundred. A piano has nothing worth modelling up there. */
static const double FIT_CEILING_HZ = 10000.0;

/** THE ALLPASS CHAIN LENGTHS TRIED. One first-order allpass can only bend the delay curve so far;
the wound bass wants several, and the top of the compass wants almost none. Trying them all and
keeping the best costs a few thousand trigonometric calls once per note, which is nothing beside
being half a cent out on every partial of every bass note for ever. */
static const int STAGE_CHOICES[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48};
static const int STAGE_COUNT = 11;


// ---- what the compass decides -------------------------------------------------------------------
//
// AS ANCHORS AND NOT A FORMULA. There is no tidy expression for either of these: both are set by
// how a particular instrument is strung — where the wound strings stop, where the breaks fall, how
// long the maker could make the bass — so the published measurements are a curve through points,
// and that is how they are kept here. Between the anchors, straight lines in the logarithm, which
// is how both quantities vary.

struct Anchor {
	float midi;
	float value;
};

static float throughAnchors(const Anchor* a, int n, float midi) {
	if (midi <= a[0].midi)
		return a[0].value;
	if (midi >= a[n - 1].midi)
		return a[n - 1].value;
	for (int i = 1; i < n; i++) {
		if (midi <= a[i].midi) {
			const float t = (midi - a[i - 1].midi) / (a[i].midi - a[i - 1].midi);
			return std::exp(std::log(a[i - 1].value)
				+ t * (std::log(a[i].value) - std::log(a[i - 1].value)));
		}
	}
	return a[n - 1].value;
}


/** B ACROSS THE COMPASS. The minimum is not in the middle of the keyboard but below it, around the
bottom of the treble bridge, and the rise from there to the top is much steeper than the rise down
to the bottom A: the top strings are a couple of inches of thick wire, which is nearly a bar, and a
bar's partials are stretched enormously. The wound bass strings are stiff too, but winding is what
keeps them from being worse — the mass is in the winding and the stiffness is only in the core. */
static const Anchor INHARMONICITY[] = {
	{21.f, 2.0e-4f},    // A0, the bottom: wound, long, and stiffer than its neighbours above
	{33.f, 1.1e-4f},    // A1
	{45.f, 6.0e-5f},    // A2
	{52.f, 5.0e-5f},    // E3, about where it is least
	{57.f, 6.5e-5f},    // A3
	{69.f, 1.3e-4f},    // A4
	{81.f, 3.2e-4f},    // A5
	{93.f, 8.0e-4f},    // A6
	{108.f, 1.6e-3f},   // C8, the top: very short, very stiff, audibly clangorous
};

float pianoInharmonicity(float midi) {
	return throughAnchors(INHARMONICITY, (int) (sizeof(INHARMONICITY) / sizeof(Anchor)), midi);
}


/** HOW LONG THE FUNDAMENTAL RINGS, damper up. The bass hangs on for the better part of a minute and
the top octave is gone almost at once, which is why the top two octaves have no dampers at all:
there would be nothing for them to stop. */
static const Anchor DECAY[] = {
	{21.f, 30.f},
	{33.f, 25.f},
	{45.f, 18.f},
	{57.f, 11.f},
	{69.f, 6.f},
	{81.f, 3.f},
	{93.f, 1.2f},
	{108.f, 0.4f},
};

float pianoDecaySeconds(float midi) {
	return throughAnchors(DECAY, (int) (sizeof(DECAY) / sizeof(Anchor)), midi);
}


/** THE STRING'S WAVE IMPEDANCE: its mass per unit length times the speed waves travel along it.
It decides how hard the string pushes back at the felt, and so how long the two stay together.

WORKED OUT FROM THE STRINGING RATHER THAN GUESSED AT, because guessing at it got it wrong by a
factor of fourteen. The speed along a string is twice its speaking length times its fundamental,
and the mass per unit length is the wire's cross-section times the density of steel, with the
wound bass strings carrying several times the mass of their core. For a six or seven foot grand:

	note   length m   diameter mm   speed m/s   kg/m      Z
	A0       1.20     wound          66         0.055    3.6
	A1       1.05     wound         116         0.020    2.3
	A2       0.95     1.20          209         0.0089   1.9
	A3       0.72     1.05          317         0.0068   2.2
	A4       0.40     0.95          352         0.0056   2.0
	A5       0.22     0.90          387         0.0050   1.9
	A6       0.115    0.85          405         0.0045   1.8
	C8       0.05     0.80          419         0.0039   1.6

WHICH IS VERY NEARLY FLAT, and that is the point rather than a coincidence. The first attempt at
this curve had it falling from four to a quarter across the compass, on the reasoning that treble
strings are short and thin — but they are only a little thinner, and what they lose in mass they
make up in speed, because a short string at a high pitch carries waves fast. A maker aims for an
even impedance so that the tone is even, and the numbers show them succeeding.

The wrong curve was caught by the hammer rather than by inspection: a quarter of a kilogram per
second at the top of the compass let the string slide out from under the felt, and the contact at
A6 came out at two and a sixth milliseconds where a real one is under one, with no stiffness of
felt able to shorten it. */
static const Anchor IMPEDANCE[] = {
	{21.f, 3.6f},
	{33.f, 2.3f},
	{45.f, 1.9f},
	{57.f, 2.2f},
	{69.f, 2.0f},
	{81.f, 1.9f},
	{93.f, 1.8f},
	{108.f, 1.6f},
};

float pianoImpedance(float midi) {
	return throughAnchors(IMPEDANCE, (int) (sizeof(IMPEDANCE) / sizeof(Anchor)), midi);
}


/** HOW MANY STRINGS THE NOTE HAS, which the hammer needs to know even while only one of them is
being modelled.

A STEP AND NOT A CURVE, because that is what it is: a maker chooses where the breaks fall. The
bottom octave or so is single wound strings, a stretch above that is pairs, and from somewhere
around the bottom of the bass bridge upward it is threes all the way to the top.

WHY A SINGLE STRING HAS TO KNOW. The hammer pushes all of a note's strings at once, so they resist
it together: three strings are three times as hard to shift as one, the hammer's momentum drains
three times as fast, and the contact is correspondingly shorter. A hammer modelled against one
string where a real one pushes three stays on for twice as long as it should, which is exactly what
the bench measured before this was put in. So the hammer feels the whole choir's impedance and the
one string modelled takes its own share of the force — which is also what makes it a correct member
of the choir that phase 3 will build around it, rather than an approximation to be undone. */
int pianoStringsPerNote(float midi) {
	if (midi < 30.f)
		return 1;
	if (midi < 41.f)
		return 2;
	return 3;
}


/** HOW FAR APART A TUNER LEAVES THE STRINGS OF ONE NOTE, in cents from lowest to highest.

NOT A DEFECT TO BE MINIMISED. A tuner aims at nought and nobody reaches it — a few tenths of a cent
is a clean unison and about one is a tired one — but the residue is where the shimmer of a piano
note comes from, and a note whose strings are exactly in tune sounds dead. It is also what makes the
double decay audible as a change in colour rather than only in level.

A LITTLE WIDER IN THE BASS, because the beats there are slow enough to be heard as beats rather
than as warmth, and tuners leave them so on purpose. */
static const Anchor UNISON[] = {
	{21.f, 0.9f},
	{45.f, 0.6f},
	{69.f, 0.4f},
	{108.f, 0.3f},
};

float pianoUnisonSpread(float midi) {
	return throughAnchors(UNISON, (int) (sizeof(UNISON) / sizeof(Anchor)), midi);
}


/** THE HAMMER'S MASS. About eleven grams at the bottom falling to four at the top, and that taper
is most of why the treble speaks so much more quickly than the bass: a light hammer on a light
string is done with it in a fraction of the time. */
static const Anchor HAMMER_MASS[] = {
	{21.f, 0.0115f},
	{45.f, 0.0085f},
	{69.f, 0.0062f},
	{93.f, 0.0046f},
	{108.f, 0.0040f},
};

float pianoHammerMass(float midi) {
	return throughAnchors(HAMMER_MASS, (int) (sizeof(HAMMER_MASS) / sizeof(Anchor)), midi);
}


/** THE POWER THE FELT'S FORCE GOES AS, rising toward the treble where the felt is harder and
thinner. Published measurements on real hammers put it between about one and a half and three and a
half, and it is not a constant even for one hammer — felt stiffens as it is played in, which is
what a technician is undoing when they needle a hammer. */
static const Anchor FELT_POWER[] = {
	{21.f, 2.2f},
	{57.f, 2.5f},
	{81.f, 2.8f},
	{108.f, 3.0f},
};

float pianoFeltPower(float midi) {
	return throughAnchors(FELT_POWER, (int) (sizeof(FELT_POWER) / sizeof(Anchor)), midi);
}


/** THE FELT'S STIFFNESS, in newtons per metre raised to that power.
//
SET BY MEASUREMENT AND NOT QUOTED, which is the only honest way to come by it. Its units depend on
the power above, so the number means nothing on its own and cannot be compared between registers or
with anything in the literature; what CAN be compared is how long the felt stays on the string,
which is published, which the bench measures directly, and which this was adjusted until it
matched: about two milliseconds in the middle of the compass at a moderate blow, longer in the
bass, shorter in the treble, and falling by a factor of three or so from the quietest note to the
loudest. */
static const Anchor FELT_STIFFNESS[] = {
	{21.f, 5.0e9f},
	{45.f, 8.0e9f},
	{57.f, 8.0e9f},
	{69.f, 1.6e10f},
	{81.f, 1.3e11f},
	{93.f, 1.6e12f},
	{108.f, 8.0e12f},
};

float pianoFeltStiffness(float midi) {
	return throughAnchors(FELT_STIFFNESS, (int) (sizeof(FELT_STIFFNESS) / sizeof(Anchor)), midi);
}


/** WHERE THE HAMMER LANDS, as a fraction of the speaking length from the agraffe. About an eighth
over most of the compass, and a smaller fraction at the top, where there is not much string to aim
at. The choice is a compromise a maker makes: nearer the end is brighter and thinner, further along
is fuller but loses the partials whose nodes are there. */
static const Anchor STRIKE[] = {
	{21.f, 0.125f},
	{69.f, 0.125f},
	{93.f, 0.10f},
	{108.f, 0.07f},
};

float pianoStrikeFraction(float midi) {
	return throughAnchors(STRIKE, (int) (sizeof(STRIKE) / sizeof(Anchor)), midi);
}


// ---- the filters, as the design has to see them -------------------------------------------------
//
// IN DOUBLE PRECISION. The same lesson as the path reading: a stretch of a hundredth of a per cent
// is a real and audible quantity, and it does not survive being worked out in single precision
// through a dozen trigonometric calls.

/** A first-order allpass's phase delay in samples: (c + z^-1) / (1 + c z^-1). Negative c delays
the low frequencies more than the high, which is what a stiff string does; positive c does the
opposite, which is how the read's own dispersion gets cancelled. */
static double allpassDelay(double c, double w) {
	const double num = std::atan2(-std::sin(w), c + std::cos(w));
	const double den = std::atan2(-c * std::sin(w), 1.0 + c * std::cos(w));
	return -(num - den) / w;
}

/** One pole of the loss filter, y = (1 - s) x + s y[-1]: its phase delay in samples, and its gain.

TWO OF THESE IN SERIES AND NOT ONE, which the bench asked for. A string's partials are to die by a
power law — the one at f in the fundamental's time times f0 over f raised to about one — so the
loss the loop must take grows without limit as the frequency rises. A single pole cannot do that:
its loss is a shelf, nought at the bottom and a fixed figure at the top, with one bend between, and
fitted to a power law across four and a half octaves it left the worst partial's decay forty per
cent out. Two poles at different corners give the curve two bends, which is enough to follow a
power law closely, and cost one multiply and two adds per sample against a dispersion chain of
eight and forty stages. */
/** THE LOOP'S LOSS: THREE FIRST-ORDER SHELVES.
//
WHAT THE LOSS HAS TO BE, which is what decides the form. Every partial goes round the loop once per
period of the FUNDAMENTAL — the delay line is one of those long, and the twentieth partial is going
round it just as often as the first — so a partial's decay is set entirely by how much the loop
keeps at that frequency. For the partial at f to fall sixty decibels in the fundamental's time
multiplied by f0 over f raised to about one, the loss in decibels must grow very nearly in
PROPORTION to the frequency, from the fundamental all the way to the top of the run.

THREE FORMS WERE TRIED AND MEASURED BEFORE THIS ONE, in test/lossprobe.cpp, which asked of each the
only question that matters: with the best coefficients it can have, how far out is its worst
partial's decay time?

	ONE POLE: nineteen per cent at the bottom A, twenty-three in the middle. A pole's loss grows
	as the SQUARE of the frequency below its corner and stops growing above it, and those are the
	only two shapes it has. The law wanted is between the two.

	TWO BARE POLES: identical to one, to three figures. Two curves that are each
	squared-then-flat still make squared-then-flat, and the fit left the second pole doing
	nothing.

	A SYMMETRIC FIR, which was tried because its taps enter its magnitude linearly, so the best of
	them is a linear solve rather than a search — no sweeping, no local minima, and no phase to
	account for. It is unusable, and badly: fitted to the partials it is free to do as it likes
	between them and above them, and it does, peaking at gains of twenty thousand and upward
	outside the fitted band. A loop that gains anywhere at all finds that frequency and screams on
	it. Seven taps also cannot shape anything across the bottom hundredth of the band, where a
	bass note's partials all live, so the bass came out silent.

	THREE POLE-ZERO SHELVES: five per cent at the bottom A, ten to twenty in the middle and at the
	top. Each shelf contributes its own squared-law rise over its own octave or two and then
	levels off, and three of them staggered follow a gentler power law across the whole run. It is
	the ordinary way a fractional-order response is approximated.

AND THIS FORM CANNOT GAIN, WHICH THE FIR COULD NOT PROMISE. A shelf with its zero further from one
than its pole is loudest at DC, where it is exactly transparent, and falls monotonically from
there; three of them in series do the same; so the whole loop's gain is highest at DC and is what
the flat gain sets it to. Stability needs no sweeping of the band to check — it is in the form.

THE COST is three multiplies and three adds a sample, against a dispersion chain of up to
eight-and-forty stages, so it is not where the time goes. */
static const int LOSS_SECTIONS = 3;

typedef PianoString::Loss Loss;

/** One shelf: K (1 - q z^-1) / (1 - p z^-1), with K making it transparent at DC. */
static void shelfResponse(double p, double q, double w, double* delay, double* gain) {
	const double k = (1.0 - p) / (1.0 - q);
	const double nRe = 1.0 - q * std::cos(w), nIm = q * std::sin(w);
	const double dRe = 1.0 - p * std::cos(w), dIm = p * std::sin(w);
	*gain = k * std::sqrt((nRe * nRe + nIm * nIm) / (dRe * dRe + dIm * dIm));
	*delay = (std::atan2(dIm, dRe) - std::atan2(nIm, nRe)) / w;
}

static double lossDelay(const Loss& L, double w) {
	double d = 0.0;
	for (int i = 0; i < LOSS_SECTIONS; i++) {
		double di, gi;
		shelfResponse(L.pole[i], L.zero[i], w, &di, &gi);
		d += di;
	}
	return d;
}

static double lossGain(const Loss& L, double w) {
	double g = 1.0;
	for (int i = 0; i < LOSS_SECTIONS; i++) {
		double di, gi;
		shelfResponse(L.pole[i], L.zero[i], w, &di, &gi);
		g *= gi;
	}
	return g;
}


/** READING BETWEEN SAMPLES, by four-point Lagrange: the weights for a delay whose whole part is one
less than its floor, so the fraction lies between one and two where the interpolator is best. */
static void lagrange(double f, double h[4]) {
	h[0] = -(f - 1.0) * (f - 2.0) * (f - 3.0) / 6.0;
	h[1] = f * (f - 2.0) * (f - 3.0) / 2.0;
	h[2] = -f * (f - 1.0) * (f - 3.0) / 2.0;
	h[3] = f * (f - 1.0) * (f - 2.0) / 6.0;
}

/** What that read does to a sine at w: its delay in samples, and its gain. The gain is not one —
the interpolator is a gentle lowpass, and so is one of the things deciding how the upper partials
die.

THE PHASE OF THE FOUR TAPS ONLY, with the whole samples added back afterwards. Taken from the taps
at their absolute positions, the phase of a delay of several hundred samples is some hundreds of
radians, and an arctangent cannot tell that from the same angle one turn round: the answer comes
back folded into a single period of the frequency being asked about, which for a bass note is a
delay wrong by a factor of a hundred. Everything that was wrong with the first attempt at this
phase was downstream of that. The four taps relative to each other span three samples and delay by
between one and two, so their phase is a fraction of a turn and unambiguous — and it is nudged onto
the turn nearest the delay we already know it is close to, so that even at the top of the band it
cannot fold. */
static void readResponse(double d, double w, double* delay, double* gain) {
	const double base = std::floor(d) - 1.0;
	const double frac = d - base;          // between one and two
	double h[4];
	lagrange(frac, h);
	double re = 0.0, im = 0.0;
	for (int j = 0; j < 4; j++) {
		re += h[j] * std::cos(w * (double) j);
		im -= h[j] * std::sin(w * (double) j);
	}
	double phi = std::atan2(im, re);
	const double expect = -w * frac;
	phi += 2.0 * PI * std::floor((expect - phi) / (2.0 * PI) + 0.5);
	*delay = base + (-phi / w);
	*gain = std::sqrt(re * re + im * im);
}


// ---- setting up ---------------------------------------------------------------------------------

PianoString::PianoString() {
	leftLine.assign(1024, 0.f);
	outLine.assign(1024, 0.f);
	backLine.assign(1024, 0.f);
	leftMask = outMask = backMask = 1023;
}


void PianoString::setSampleRate(float hz) {
	if (hz > 0.f && (double) hz != rate) {
		rate = hz;
		dirty = true;
	}
}

void PianoString::setNote(float midi) {
	midi = std::fmax(12.f, std::fmin(120.f, midi));
	if ((double) midi != note) {
		note = midi;
		dirty = true;
	}
}

void PianoString::setDetuneCents(float cents) {
	if ((double) cents != detune) {
		detune = cents;
		dirty = true;
	}
}

void PianoString::setInharmonicity(float b) {
	if ((double) b != bAsked) {
		bAsked = b;
		dirty = true;
	}
}

void PianoString::setDecaySeconds(float seconds) {
	if ((double) seconds != decayAsked) {
		decayAsked = seconds;
		dirty = true;
	}
}

void PianoString::setDecayExponent(float power) {
	power = std::fmax(0.f, std::fmin(4.f, power));
	if ((double) power != powerAsked) {
		powerAsked = power;
		dirty = true;
	}
}


/** THE LINE, AS LONG AS THE NOTE NEEDS AND A POWER OF TWO. Growing it loses whatever was in it, so
it only ever grows, and a string that has been down to the bottom A keeps the room for it. */
void PianoString::resize() {
	const unsigned want = (unsigned) (rate / freq) + 32u;
	if (want <= leftMask)
		return;
	unsigned n = 64;
	while (n < want)
		n <<= 1;
	// Every line gets room for the whole period: which way the strike point divides it is the
	// note's business and may move, and a line is cheap beside getting that wrong.
	leftLine.assign(n, 0.f);
	outLine.assign(n, 0.f);
	backLine.assign(n, 0.f);
	leftMask = outMask = backMask = n - 1;
	writeAt = 0;
	fromLeft = fromRight = lastJunction = 0.0;
	for (int i = 0; i < LOSS_SECTIONS; i++)
		shelfIn[i] = shelfOut[i] = 0.f;
	for (int i = 0; i < MAX_STAGES; i++)
		chainIn[i] = chainOut[i] = 0.f;
}


/** THE FIXED DELAY, solved so that the FIRST partial lands on the note. What is left of the period
after the loss filter and the allpass chain have taken their share, corrected for what the
interpolator itself delays — in two passes, because the correction moves the delay and the delay
moves the correction. */
/** THE RIGHT SECTION'S DELAY, solved so that the FIRST partial lands on the note.

WHAT IS LEFT OF THE PERIOD once the left section, the loss filter and the allpass chain have taken
their shares. The left section is a whole number of samples — exact, with no gain and no dispersion
of its own — so the whole of the interpolating is in the right one, and the whole of the correcting
with it. */
static double solveDelayFor(double period, double w1, double leftWhole, double stagesN, double a,
		const Loss& loss, unsigned mask) {
	const double want = period - leftWhole - lossDelay(loss, w1) - stagesN * allpassDelay(a, w1);
	double d = want;
	for (int pass = 0; pass < 3; pass++) {
		d = std::fmax(3.0, std::fmin(d, (double) mask - 4.0));
		double actual, gain;
		readResponse(d, w1, &actual, &gain);
		d += want - actual;
	}
	return std::fmax(3.0, std::fmin(d, (double) mask - 4.0));
}


/** FITTING THE CHAIN TO THE NOTE'S STRETCH.

The thing being fitted is the only part of this that is a search rather than a calculation, and it
is a search because there is no closed form: a cascade of identical first-order allpasses has a
delay curve of its own shape, the stiff string's is a different shape, and the best that can be
done is to choose the number of stages and the coefficient whose WORST partial is least wrong.

WORST AND NOT AVERAGE. A fit that is perfect on the low partials and four cents out on the
sixteenth sounds like a note that cannot be tuned, because the ear tunes a piano by its upper
partials; one that is half a cent out everywhere sounds in tune. So the thing minimised is the
largest error and not the sum of them.

AND IT INCLUDES THE WHOLE LOOP, which is the point. The partials are not asked for from the
dispersion relation and hoped for; they are worked out from where this loop — this interpolated
read, this loss filter, this chain — will actually ring, and compared against where the stiff
string says they should be. Everything the implementation does to the partials, including the
read's own small stretch, is therefore in the error being minimised, and gets cancelled rather
than added to. */
void PianoString::fitChain() {
	const double period = rate / freq;
	const double w1 = 2.0 * PI * freq / rate;

	// WHERE THE PARTIALS SHOULD BE. The nth of a stiff string is n times the fundamental times the
	// square root of one plus B n squared — but the note a person hears and tunes is the FIRST
	// partial, not the hypothetical B-less fundamental, so the ideal is written relative to the
	// first. Tune a piano to its first partials and the stretch is what is left over; that is why
	// octaves come out stretched.
	int want = 0;
	double ideal[FIT_PARTIALS + 1] = {};
	for (int n = 1; n <= FIT_PARTIALS; n++) {
		const double f = freq * (double) n
			* std::sqrt((1.0 + bUsed * (double) n * (double) n) / (1.0 + bUsed));
		if (f > rate * 0.45 || f > FIT_CEILING_HZ)
			break;
		ideal[n] = f;
		want = n;
	}

	// HOW WRONG A CANDIDATE IS, WITHOUT SOLVING FOR WHERE ITS PARTIALS LAND.
	//
	// The loop rings where w times its phase delay at w is a whole number of turns, and finding
	// that frequency means a fixed point — six passes through four arctangents each, per partial,
	// per candidate, which is a second of work per string and two hundred and sixty strings to do
	// it for.
	//
	// So the error is read the other way round. At the frequency where the partial SHOULD be, the
	// loop's round trip is some number of turns, and how far that is from a whole number says how
	// far the partial will sit from where it belongs: a round trip a thousandth of a turn too long
	// puts the partial about a thousandth low, because the delay curve is far flatter than the
	// thing being solved for. One evaluation instead of six, and the exact solve is kept for
	// judging between the candidates at the end — which is also the check that the shortcut is
	// fair, since the two are printed side by side under PIANO_FIT_TRACE and agree to a few
	// hundredths of a cent.
	auto errorOf = [&](int stages, double a, bool exact, double* worstOut) {
		const double stagesN = (double) stages;
		// Never ask the chain for more delay than the loop has to give: a chain that eats the
		// period cannot be tuned at all.
		const double chainAt1 = stagesN * allpassDelay(a, w1);
		if (chainAt1 > (period - (double) (leftDelay + outDelay)) * 0.6 || chainAt1 < 0.0)
			return false;
		const double d = solveDelayFor(period, w1, (double) (leftDelay + outDelay), stagesN, a, loss, backMask);

		double worst = 0.0;
		for (int n = 2; n <= want; n++) {
			const double w = 2.0 * PI * ideal[n] / rate;
			double cents;
			if (exact) {
				double wn = w;
				for (int i = 0; i < 6; i++) {
					double dr, gr;
					readResponse(d, wn, &dr, &gr);
					const double total = (double) (leftDelay + outDelay) + dr
						+ lossDelay(loss, wn) + stagesN * allpassDelay(a, wn);
					if (total <= 1.0)
						return false;
					wn = 2.0 * PI * (double) n / total;
				}
				cents = 1200.0 * std::log2(wn / w);
			}
			else {
				double dr, gr;
				readResponse(d, w, &dr, &gr);
				const double total = (double) (leftDelay + outDelay) + dr + lossDelay(loss, w)
					+ stagesN * allpassDelay(a, w);
				if (total <= 1.0)
					return false;
				// Turns per round trip, against the whole number it should be.
				cents = -1200.0 * std::log2(w * total / (2.0 * PI * (double) n));
			}
			worst = std::fmax(worst, std::fabs(cents));
		}
		*worstOut = worst;
		return true;
	};

	double bestError = 1e30;
	int bestStages = 1;
	double bestA = 0.0;

	// HOW MANY STAGES, AND WHY SO MANY MAY BE AFFORDED.
	//
	// A stiff string's delay falls away gradually across the whole run of its partials, and a
	// cascade of first-order allpasses falls away mostly around one frequency, so matching the
	// curve is done by quantity: the worst partial's error falls very nearly as one over the
	// number of stages, from twenty-five cents with one stage at the bottom A to under one with
	// eight and forty.
	//
	// TWO GROUPS OF STAGES AT DIFFERENT COEFFICIENTS WERE TRIED AND DO NOT HELP. Giving the chain
	// a second corner should let fewer stages follow the curve, and it does not: the best pair is
	// always the equal pair, so the extra freedom is a valley with nothing at the bottom of it,
	// and the search for it was slower and found worse points than the search along the diagonal.
	// Quantity is the answer and the two-coefficient chain is not.
	//
	// AND THE QUANTITY IS CHEAP WHERE IT IS NEEDED, which is the part that makes this bearable.
	// The stages wanted go as the stretch does — most in the bass, almost none at the top — and
	// the bass is where a note has ONE string rather than three. The bottom dozen notes at
	// eight-and-forty stages each cost less than a twentieth of the whole instrument's filtering;
	// the treble, which is three strings a note and most of the notes, wants a handful.
	for (int k = 0; k < STAGE_COUNT; k++) {
		const int stages = STAGE_CHOICES[k];
		const int GRID = 48;
		double at = 0.0, atError = 1e30;
		for (int i = 0; i <= GRID; i++) {
			const double a = -0.95 + 1.90 * (double) i / (double) GRID;
			double e;
			if (errorOf(stages, a, false, &e) && e < atError) {
				atError = e;
				at = a;
			}
		}
		if (atError > 1e29)
			continue;
		// Then narrowed, halving the step each round. This probes either side of the best point
		// rather than trusting a gradient: the worst-partial error has corners in it, because as
		// the coefficient moves, WHICH partial is worst changes.
		double span = 1.90 / (double) GRID;
		for (int round = 0; round < 16; round++) {
			for (int side = -1; side <= 1; side += 2) {
				const double a = std::fmax(-0.98, std::fmin(0.98, at + side * span));
				double e;
				if (errorOf(stages, a, false, &e) && e < atError) {
					atError = e;
					at = a;
				}
			}
			span *= 0.55;
		}
		// JUDGED ON THE EXACT ERROR and not the estimate: the estimate is what makes the search
		// affordable, and the decision is made on the truth.
		double exact;
		if (!errorOf(stages, at, true, &exact))
			continue;
#ifdef PIANO_FIT_TRACE
		std::fprintf(stderr, "  f0=%8.2f stages=%2d a=%+.4f estimated=%7.3f exact=%7.3f\n",
			freq, stages, at, atError, exact);
#endif
		// A TENTH BETTER OR IT IS NOT WORTH THE FILTER. Every stage is a multiply-and-add on every
		// sample of every string that is ringing, so a longer chain has to earn its place rather
		// than take it by a hair. A quarter was tried first and is too mean a margin: it left A3
		// on sixteen stages at 1.85 cents when eight-and-forty would have had it at 1.40, and
		// below a tenth nothing changes at all, so a tenth is where the curve flattens.
		if (exact < bestError * 0.90) {
			bestError = exact;
			bestStages = stages;
			bestA = at;
		}
	}

	chainStages = bestStages;
	chainA = bestA;
	fitError = (bestError > 1e28) ? 0.0 : bestError;
}


/** THE LOSS, FITTED TO A DECAY LAW ACROSS THE WHOLE RUN OF PARTIALS.

What a partial's decay is made of: it goes round the loop once per cycle of itself, so what matters
is how much the loop keeps per pass at that frequency, and the loop has two things that decide it —
a flat gain, which treats every frequency alike, and the one-pole lowpass, which takes more off the
top. Two unknowns.

NOT MATCHED AT TWO POINTS BUT FITTED ACROSS ALL OF THEM, and that is a correction of the first
attempt rather than a refinement of it. Setting the flat gain from the fundamental and the pole
from the eighth partial is exact at two frequencies and unconstrained everywhere else, and it falls
apart in the treble, where there IS no eighth partial: at the top of the compass the eighth is
above half the sample rate, the reference has to be dragged down to whatever will fit, and the
number asked for stops meaning what it says. Least squares over every partial the note actually
has is the same arithmetic for every note in the compass and does not care how many there are.

	THE LAW. A partial at f falls sixty decibels in the fundamental's time multiplied by f0 over f
	raised to a power.

	AND EVERY PARTIAL GOES ROUND THE LOOP AT THE SAME RATE, which took being measured to see. The
	delay line is one period of the FUNDAMENTAL long, and everything in it — fundamental, second
	partial, twentieth — comes round once per that period, not once per cycle of itself. So a flat
	loop gain does not give a one-over-n decay as the first version of this assumed: it gives
	every partial the SAME decay, and the bench said so plainly, reporting a power of 0.04 where
	the arithmetic claimed 1.18. The whole of the power law is therefore the lowpass's work, and
	nothing of it is free. A power of nought is what costs nothing; below that the top would have
	to ring longer than the fundamental, which a lossy loop cannot do.

	THE READ IS PART OF THE LOSS. Interpolating between samples is a gentle lowpass of its own, so
	some of the law is already satisfied before the pole does anything, and at the top of the
	compass rather more than is wanted. Its gain is in the fit, so the pole makes up the
	difference instead of adding to it. */
void PianoString::solveLoss() {
	const double w1 = 2.0 * PI * freq / rate;

	// The partials this note has, and what each one's loop gain should be: the base-ten logarithm
	// of what one pass must leave if that partial is to take its share of time.
	int count = 0;
	double w[FIT_PARTIALS + 1] = {}, need[FIT_PARTIALS + 1] = {};
	for (int n = 1; n <= FIT_PARTIALS; n++) {
		const double f = freq * (double) n
			* std::sqrt((1.0 + bUsed * (double) n * (double) n) / (1.0 + bUsed));
		if (f > rate * 0.45 || f > FIT_CEILING_HZ)
			break;
		const double t60 = t60Used * std::pow(freq / f, powerAsked);
		w[count] = 2.0 * PI * f / rate;
		// PASSES PER SECOND IS THE FUNDAMENTAL'S FREQUENCY, for every partial alike.
		need[count] = -3.0 / (freq * t60);
		count++;
	}
	if (count == 0) {
		loss = Loss();
		passGain = 0.99;
		powerReached = 0.0;
		lossError = 0.0;
		return;
	}

	// WHAT THE REST OF THE LOOP ALREADY TAKES, which the loss filter must therefore not take
	// again: the interpolating read's own gentle lowpassing.
	//
	// AND NOT THE BRIDGE, WHICH WAS TRIED AND IS WRONG. The decay this fit delivers is the
	// STRING'S OWN, from its internal losses alone, and the bridge's share is on top of it. The
	// first attempt divided the bridge out as well, so that the compass's figure would be what
	// came out of the finished instrument — and the arithmetic refused: at any bridge impedance
	// worth having, what the bridge absorbs is several times a note's whole decay budget, so
	// making up for it would need a loop gain above one. A3 asked for eleven seconds and rang for
	// two and a fifth, which is the bridge's rate and nothing else.
	//
	// WHICH IS THE RIGHT DECOMPOSITION ANYWAY, and the refusal was pointing at it. A string's
	// internal losses are the slow floor under everything; what the bridge takes is the sound
	// leaving, and it is the fast one. So the compass's curve is the string's own, and it is the
	// AFTERSOUND it describes — the long tail, when the strings of a choir are out of step and
	// barely moving the bridge at all. The initial fall is faster by whatever the bridge is good
	// at. A note with one string has no out-of-step mode and so no aftersound, and rings for
	// rather less than the curve says; there is nothing to sustain it.
	double readGain[FIT_PARTIALS + 1];
	for (int i = 0; i < count; i++) {
		double d;
		readResponse(readDelay, w[i], &d, &readGain[i]);
	}

	// WHAT THE LOSS MUST BE AT EACH PARTIAL, and weighted for what is heard.
	//
	// The read's own gentle lowpassing is part of the loop, so it is divided out rather than added
	// to. And the error is counted RELATIVE: the loop gains the partials need differ by a factor
	// of thirty between the fundamental and the twenty-fourth, so counting absolute error in the
	// logarithm of the gain would spend all the accuracy at the bottom. A decay time wrong by a
	// tenth is a tenth wrong wherever it is.
	//
	// ANCHORED AT THE FUNDAMENTAL, not centred on the mean. The note's own decay is a thing a
	// player hears directly and the partials above it are not, so the flat gain is whatever makes
	// the fundamental exactly right, and what error is left is pushed upward. The anchor is part
	// of the thing being minimised, not applied afterwards: minimising one arrangement and
	// shipping another was what an earlier version of this did, and it reported the same forty per
	// cent however the filter was changed, because the number had come loose from the filter.
	// AND THE FILTER'S DELAY IS PART OF WHAT IT IS ALLOWED TO BE, which is not a refinement but a
	// correction: fitted on magnitude alone, the search put shelf corners BELOW the fundamental,
	// where a first-order section delays by thousands of samples. The loss sits inside the tuned
	// loop, so that delay is subtracted from the delay line's, and when it exceeds the period
	// there is no loop left to tune: A3 came out a hundred and twenty-seven cents flat with its
	// partials scattered over an octave and a half. A shelf is allowed a quarter of the period and
	// no more, which costs nothing real — the corners that matter are up among the partials, not
	// below the note.
	auto residualOf = [&](const Loss& L) {
		// A SHELF IS ALLOWED A QUARTER OF THE PERIOD OF DELAY AND NO MORE. The loss sits inside
		// the tuned loop, so its delay is subtracted from the delay line's, and when it exceeds
		// the period there is no loop left to tune. Fitted on magnitude alone the search put
		// corners BELOW the fundamental, where a first-order section delays by hundreds of
		// samples, and A3 came out a hundred and thirty cents flat with its partials strewn over
		// an octave and a half.
		if (lossDelay(L, w[0]) > 0.25 * rate / freq)
			return 1e29;
		// AND IT MUST BE ALL BUT TRANSPARENT AT THE FUNDAMENTAL, which is the constraint that
		// took longest to see because the arithmetic hid it. The loop's gain is highest at DC and
		// is set there by the flat gain, which cannot exceed one; so the whole of the
		// fundamental's loss has to fit inside its own budget, and any shelf that eats more than
		// that at the fundamental can only be made up for by a flat gain above one. The fit, free
		// to add its anchor as a signed constant, happily chose such shapes and read as excellent
		// — thirteen per cent at the bottom A, every partial in place — while what was actually
		// built had its gain clamped at one and every partial ringing twenty times too long.
		const double anchor = need[0] - std::log10(std::fmax(lossGain(L, w[0]) * readGain[0], 1e-12));
		if (anchor > 0.0)
			return 1e29;
		double worst = 0.0;
		for (int i = 1; i < count; i++) {
			const double got = anchor
				+ std::log10(std::fmax(lossGain(L, w[i]) * readGain[i], 1e-12));
			worst = std::fmax(worst, std::fabs((got - need[i]) / need[i]));
		}
		return worst;
	};

	// THE SEARCH: a few dozen starts spread over the whole space, each walked downhill by moving
	// one coefficient at a time.
	//
	// FROM MANY STARTS AND NOT ONE, which was measured rather than assumed. Started where the
	// design says the shelves belong — corners staggered geometrically across the run of partials,
	// each zero a little behind its pole — a single descent stalls at once and lands between
	// twenty and ninety-five per cent out: the surface is narrow valleys, and one coefficient at a
	// time cannot turn a corner in one. Two and thirty starts reach within a few points of what
	// three hundred reach, and cost a few milliseconds, which is a thing done when a note's pitch
	// changes and never while it sounds.
	//
	// SWEPT IN DECADES SHORT OF ONE rather than in the coefficient. A pole or a zero does all its
	// work in the last per cent of its travel toward one; spaced evenly in the coefficient, a
	// sweep spends nearly all of itself on corner frequencies up near the sample rate where
	// nothing is happening.
	auto coefficient = [](double u) { return 1.0 - std::pow(10.0, -4.0 * std::fmax(0.0,
		std::fmin(1.0, u))); };
	auto build = [&](const double* u) {
		Loss L;
		for (int i = 0; i < LOSS_SECTIONS; i++) {
			L.pole[i] = coefficient(u[2 * i]);
			// THE ZERO FURTHER FROM ONE THAN ITS POLE, ALWAYS. The other way round makes the
			// section a high shelf — a filter that lifts the top instead of taking it off — and a
			// loop that gains at any frequency will find it.
			L.zero[i] = coefficient(std::fmin(u[2 * i + 1], u[2 * i]));
		}
		return L;
	};

	const int PARAMS = 2 * LOSS_SECTIONS;
	Loss best;
	double bestResidual = 1e30;
	unsigned seed = 2463534242u;
	auto random = [&]() {
		seed ^= seed << 13;
		seed ^= seed >> 17;
		seed ^= seed << 5;
		return (double) (seed >> 8) / 16777216.0;
	};
	for (int trial = -1; trial < 32; trial++) {
		double u[2 * LOSS_SECTIONS];
		if (trial == -1) {
			// WHEREVER IT ENDED UP LAST TIME. On the second round of the setup nothing has
			// changed but a thousandth of the read's gain, and this start is what makes the
			// second round return to the same valley rather than to another one that is as good
			// on magnitude and quite different on delay.
			if (loss.pole[0] <= 0.0 && loss.pole[1] <= 0.0 && loss.pole[2] <= 0.0)
				continue;
			for (int i = 0; i < LOSS_SECTIONS; i++) {
				u[2 * i] = (loss.pole[i] >= 1.0) ? 1.0
					: -std::log10(std::fmax(1.0 - loss.pole[i], 1e-12)) / 4.0;
				u[2 * i + 1] = (loss.zero[i] >= 1.0) ? 1.0
					: -std::log10(std::fmax(1.0 - loss.zero[i], 1e-12)) / 4.0;
			}
		}
		else if (trial == 0) {
			// The designed start, kept as one of the candidates: when it is right it is right.
			const double fLo = w[0] * rate / (2.0 * PI);
			const double fHi = w[count - 1] * rate / (2.0 * PI);
			for (int i = 0; i < LOSS_SECTIONS; i++) {
				const double frac = (double) i / (double) (LOSS_SECTIONS - 1);
				const double fc = fLo * std::pow(fHi / fLo, frac);
				u[2 * i] = std::fmax(0.0, std::fmin(1.0,
					-std::log10(std::fmin(2.0 * PI * fc / rate, 0.5)) / 4.0));
				u[2 * i + 1] = u[2 * i] - 0.08;
			}
		}
		else {
			for (int i = 0; i < PARAMS; i++)
				u[i] = random();
		}
		Loss L = build(u);
		double at = residualOf(L);
		double span = 0.3;
		for (int round = 0; round < 120; round++) {
			bool moved = false;
			for (int i = 0; i < PARAMS; i++) {
				for (int side = -1; side <= 1; side += 2) {
					const double keep = u[i];
					u[i] = std::fmax(0.0, std::fmin(1.0, keep + side * span));
					const Loss candidate = build(u);
					const double e = residualOf(candidate);
					if (e < at) {
						at = e;
						L = candidate;
						moved = true;
					}
					else
						u[i] = keep;
				}
			}
			if (!moved)
				span *= 0.6;
			if (span < 1e-7)
				break;
		}
		if (at < bestResidual && at < 1e28) {
			bestResidual = at;
			best = L;
		}
	}
	loss = best;

	// The flat gain that the fit was judged against, so that what it reports below is what the
	// loop will do. `refreshGain` works out the same number again at the end of the setup, once
	// the read has settled.
	const double mag = lossGain(loss, w1) * readGain[0];
	passGain = std::fmin(std::pow(10.0, need[0]) / std::fmax(mag, 1e-12), 0.9999999);

	// What it came to, as the worst partial's decay time against the law.
	double realised = 0.0;
	for (int i = 1; i < count; i++) {
		const double got = std::log10(std::fmax(passGain * lossGain(loss, w[i]) * readGain[i],
			1e-12));
		realised = std::fmax(realised, std::fabs((got - need[i]) / need[i]));
	}
	bestResidual = realised;

	// WHAT IT ACTUALLY CAME OUT AS, fitted back out of the loop's own gains: the power law through
	// the decay times this loop will really give. A model that quietly failed to do what it was
	// told would be the worst of the outcomes available here, so it is asked rather than assumed.
	double sx = 0.0, sy = 0.0, sxy = 0.0, sxx = 0.0;
	int used = 0;
	for (int i = 0; i < count; i++) {
		const double per = std::log10(std::fmax(passGain * lossGain(loss, w[i]) * readGain[i], 1e-12));
		if (per >= -1e-14)
			continue;
		const double f = w[i] * rate / (2.0 * PI);
		const double t60 = -3.0 / (freq * per);
		const double x = std::log(f / freq), y = std::log(t60Used / t60);
		sx += x; sy += y; sxy += x * y; sxx += x * x;
		used++;
	}
	powerReached = (used > 1 && used * sxx - sx * sx > 1e-12)
		? (used * sxy - sx * sy) / (used * sxx - sx * sx) : 0.0;
	lossError = 100.0 * bestResidual;
#ifdef PIANO_FIT_TRACE
	std::fprintf(stderr, "  LOSS f0=%8.2f worst=%8.1f%%  lossDelay(f1)=%10.2f of period %9.2f"
		"  poles %.6f %.6f %.6f  zeros %.6f %.6f %.6f\n", freq, 100.0 * bestResidual,
		lossDelay(loss, w1), rate / freq, loss.pole[0], loss.pole[1], loss.pole[2],
		loss.zero[0], loss.zero[1], loss.zero[2]);
#endif

	// The shelves as the sample loop wants them: y = b0 x + b1 x[-1] + a1 y[-1].
	for (int i = 0; i < LOSS_SECTIONS; i++) {
		const double k = (1.0 - loss.pole[i]) / (1.0 - loss.zero[i]);
		shelfB0[i] = (float) k;
		shelfB1[i] = (float) (-k * loss.zero[i]);
		shelfA1[i] = (float) loss.pole[i];
	}


}


void PianoString::solveRead() {
	const double period = rate / freq;
	const double w1 = 2.0 * PI * freq / rate;
	readDelay = solveDelayFor(period, w1, (double) (leftDelay + outDelay), (double) chainStages, chainA, loss, backMask);
}


/** THE WHOLE SETUP, in the order the parts depend on each other.

NOT ON THE AUDIO THREAD. Fitting the chain is a few thousand trigonometric calls, which is nothing
once per note and far too much once per sample, so this runs when the note or the sample rate
changes and not otherwise. Detuning by a cent for a unison does not refit it — the chain for a note
a cent away is the same chain — which is what makes phase 3 affordable.

The read's delay depends on the chain and the loss; the loss depends on the read's gain; the chain
depends on the loss. Round twice and it has settled, because each dependency is weak: the loss
filter's delay is a fraction of a sample against a period of hundreds. */
/** THE WHOLE SETUP, in the order the parts depend on each other, which is the order that was got
wrong first.

NOT ON THE AUDIO THREAD. Fitting the chain and the loss is some thousands of trigonometric calls
and a search, which is nothing once per note and far too much once per sample, so this runs when
the note or the sample rate changes and not otherwise. Detuning by a cent for a unison does not
refit anything — the design for a note a cent away is the same design — which is what makes the
choir of phase 3 affordable.

THE PARTS DEPEND ON EACH OTHER IN A RING, and the ring has to be gone round rather than cut:

	the delay line's length depends on what the chain and the loss delay at the fundamental,
	the chain's fit depends on what the loss delays at every partial,
	and the loss's fit depends on what the interpolated read takes off the top, which depends on
	the delay line's length.

The first version of this cut the ring in the wrong place: it fitted the chain, and THEN fitted the
loss again, so the chain was tuned against a loss filter that no longer existed. While the loss was
a bare pole, whose delay is a fraction of a sample, that was a slip with no consequences. With
three shelves in it, whose delay can be hundreds of samples, it put A3 a hundred and twenty-seven
cents flat with its partials strewn over an octave and a half.

So: loss, then chain, then round again, and the gain alone at the end. The second round settles it
because the only thing feeding backwards is the read's own gain, which is within a thousandth of
one across the whole fitted band — and because the loss fit is given its previous answer as one of
its starting points, so it returns to the same valley instead of finding another one that is as
good on magnitude and different on delay. */
void PianoString::tune() {
	dirty = false;
	freq = 440.0 * std::pow(2.0, (note - 69.0) / 12.0 + detune / 1200.0);
	freq = std::fmax(16.0, std::fmin(freq, rate * 0.25));
	bUsed = (bAsked >= 0.0) ? bAsked : (double) pianoInharmonicity((float) note);
	t60Used = (decayAsked > 0.0) ? decayAsked : (double) pianoDecaySeconds((float) note);

	resize();
	impedance = (double) pianoImpedance((float) note);

	// WHERE THE STRIKE POINT DIVIDES THE PERIOD. The left section is the hammer's distance from
	// the agraffe and back, in whole samples, and the right section is everything else; together
	// they are the round trip, so the loop is the same length it was in phase 1 and every number
	// fitted for it still holds.
	//
	// AND IT IS ALWAYS DIVIDED, however short the note. The top C's period is eleven samples and a
	// fourteenth of that is under one; phase 2 left those notes whole and had to special-case both
	// the sign of the loop and the junction's arithmetic, getting each wrong before getting it
	// right. One whole sample is no worse a model of a very short string than none, and it is the
	// same model as every other note.
	//
	// AND THE BRIDGE SITS HALFWAY ALONG WHAT IS LEFT. The two one-way lines to it need a whole
	// number of samples on the way out; the way back carries the loop's fractional part, since it
	// is the one with the interpolating read on it.
	{
		const double period = rate / freq;
		const double frac = (double) pianoStrikeFraction((float) note);
		leftDelay = std::max(1, std::min((int) std::lround(frac * period),
			(int) (period * 0.5)));
		outDelay = std::max(1, (int) (0.5 * (period - (double) leftDelay)));
	}

	const bool refit = (chainStages <= 0 || fitB != bUsed || fitRate != rate
		|| fitPower != powerAsked || fitDecay != t60Used || fitSplit != leftDelay
		|| std::fabs(std::log2(freq / std::fmax(fitFreq, 1.0))) > 1.0 / 48.0);

	if (refit) {
		// A provisional read, so that the loss has something to divide its own share out of.
		readDelay = std::fmax(3.0, std::fmin(rate / freq - (double) leftDelay - (double) outDelay,
			(double) backMask - 4.0));
		for (int round = 0; round < 2; round++) {
			solveLoss();
			fitChain();
			solveRead();
		}
		fitFreq = freq;
		fitB = bUsed;
		fitRate = rate;
		fitPower = powerAsked;
		fitDecay = t60Used;
		fitSplit = leftDelay;
	}
	else {
		// A cent of detuning, or a bend: the design stands and only the length moves.
		solveRead();
	}
	refreshGain();
}


/** THE FLAT GAIN ALONE, without touching the shape of anything. What a bend or a detuning needs,
and what the end of a full setup needs once the read has settled: the shelves decide how the decay
varies with frequency and this decides the level it all sits at, which is the note's own decay. */
void PianoString::refreshGain() {
	const double w1 = 2.0 * PI * freq / rate;
	const double need1 = -3.0 / (freq * t60Used);
	double d, g;
	readResponse(readDelay, w1, &d, &g);
	// AND THIS IS THE WHOLE OF THE STABILITY ARGUMENT. The shelves are exactly transparent at DC
	// and fall monotonically from there, so the loop's gain is highest at DC and equal to this
	// number. Keep it below one and nothing at any frequency can grow. If the shelves have somehow
	// already lost more at the fundamental than was wanted, this would have to rise above one to
	// make up for it; it is clamped instead, and the note rings shorter than it was asked to,
	// which is a disappointment where the alternative is a loop that screams.
	const double mag = lossGain(loss, w1) * g;
	passGain = std::fmin(std::pow(10.0, need1) / std::fmax(mag, 1e-12), 0.9999999);
}


// ---- playing it ---------------------------------------------------------------------------------

void PianoString::prepare() {
	if (dirty)
		tune();
}


void PianoString::silence() {
	std::fill(leftLine.begin(), leftLine.end(), 0.f);
	std::fill(outLine.begin(), outLine.end(), 0.f);
	std::fill(backLine.begin(), backLine.end(), 0.f);
	for (int i = 0; i < MAX_STAGES; i++)
		chainIn[i] = chainOut[i] = 0.f;
	for (int i = 0; i < LOSS_SECTIONS; i++)
		shelfIn[i] = shelfOut[i] = 0.f;
	fromLeft = fromRight = lastJunction = 0.0;
}


/** WHAT HAS REACHED THE STRIKE POINT, from the agraffe on one side and from the bridge on the
other. This is what the hammer's felt is compressed against, so it has to be known before the
hammer can decide anything — and known for every string of the note, since one hammer pushes them
all.

THE LOOP'S LOSSES ARE PAID HERE, on the way back from the bridge. Where in the loop they are paid
makes no difference to what comes out — a round trip is a round trip — and this is the one place
every circulating wave passes through exactly once per period.

THE AGRAFFE TURNS THE SIGN OVER and the bridge does its own inverting, further down, by being a
junction that hardly moves. Between them a full round trip comes back the way it started, which is
what phase 1 tuned the loop for. */
float PianoString::arrivingAtStrike() {
	if (dirty)
		tune();

	// From the bridge: read between samples, because this is the line carrying the loop's
	// fractional part.
	const int base = (int) std::floor(readDelay) - 1;
	double h[4];
	lagrange(readDelay - (double) base, h);
	float back = 0.f;
	for (int j = 0; j < 4; j++)
		back += (float) h[j] * backLine[(writeAt - (unsigned) (base + j)) & backMask];

	// The loss: the three shelves, in the direct form of K (1 - q z^-1) / (1 - p z^-1).
	for (int i = 0; i < LOSS_SECTIONS; i++) {
		const float y = shelfB0[i] * back + shelfB1[i] * shelfIn[i] + shelfA1[i] * shelfOut[i];
		shelfIn[i] = back;
		shelfOut[i] = y;
		back = y;
	}
	// The stiffness: the fitted chain of first-order allpasses.
	const float a = (float) chainA;
	for (int i = 0; i < chainStages; i++) {
		const float y = a * back + chainIn[i] - a * chainOut[i];
		chainIn[i] = back;
		chainOut[i] = y;
		back = y;
	}
	fromRight = (double) ((float) passGain * back);

	// From the agraffe: a whole number of samples, out and back in the one line, turned over.
	fromLeft = -(double) leftLine[(writeAt - (unsigned) leftDelay) & leftMask];

	return (float) (fromLeft + fromRight);
}


/** THE STRIKE JUNCTION, CLOSED.

Two sections of the same string meet here, so they have the same impedance and the junction is
transparent when nothing is pushing on it: the wave from one side goes straight out the other,
which is why the loop still behaves as the single loop phase 1 tuned. A force applied at a point on
a string sees its two halves in parallel, so it adds a velocity of itself over twice the impedance,
and what leaves towards each side is the junction's own motion less what arrived from that side. */
void PianoString::pushStrike(float newtons) {
	const double junction = fromLeft + fromRight + (double) newtons / (2.0 * impedance);
	lastJunction = junction;
	outLine[writeAt & outMask] = (float) (junction - fromRight);
	leftLine[writeAt & leftMask] = (float) (junction - fromLeft);
}


/** WHAT HAS REACHED THE BRIDGE: what left the strike point `outDelay` samples ago. */
float PianoString::arrivingAtBridge() const {
	return outLine[(writeAt - (unsigned) outDelay) & outMask];
}


void PianoString::returnFromBridge(float wave) {
	backLine[writeAt & backMask] = wave;
	writeAt = (writeAt + 1u) & leftMask;
}

} // namespace px
