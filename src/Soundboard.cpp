/** See Soundboard.hpp. */
#include "Soundboard.hpp"

#include <algorithm>
#include <cmath>

namespace px {


static const double PI = 3.14159265358979323846;

/** WHERE THE MODES SIT.

EVENLY SPACED IN FREQUENCY AND NOT IN OCTAVES, which is the difference between a plate and a string.
A string's resonances are a harmonic series and crowd together on a logarithmic axis; a plate's are
evenly spread in hertz, so there are as many between four hundred and five hundred as between
fifty and a hundred and fifty. A soundboard modelled with modes an octave apart would have a dense
bass and an empty treble, which is backwards.

THE FIRST ONE IS WHERE THE BOARD'S OWN SIZE PUTS IT: a piano soundboard is a couple of square
metres of spruce, and its lowest mode is somewhere around fifty hertz — which is why the bottom
octave of a piano has so little fundamental in it and is heard largely through its partials. */
static const double FIRST_MODE_HZ = 48.0;
static const double MODE_SPACING_HZ = 21.0;

/** AND NOT ON A PERFECT GRID. A real board's modes are where its shape, its ribs and its bridges
put them, which is irregular; a perfectly even spacing would make a comb, and a comb with notes
played through it rings like a plate reverb rather than like an instrument. Scattered by about a
third of the spacing, deterministically, so that an instrument is the same instrument every time it
is built. */
static const double MODE_SCATTER = 0.34;

/** HOW SHARP THEY ARE. Measured soundboard modes have quality factors of a few tens — they are
heavily damped, because a board that rang like a bell would be a board that was not giving its
energy to the air, which is its whole job. Lower at the bottom, where the ribs and the case damp
it most. */
static const double Q_LOW = 22.0;
static const double Q_HIGH = 55.0;

/** HOW FAR A MODE LIFTS THE ADMITTANCE ABOVE THE SMOOTH BACKGROUND AT ITS OWN FREQUENCY. A handful
of times, which is what measured mobility curves show: peaks and dips of ten or fifteen decibels
about the trend, not the forty or fifty of a lightly damped resonator. */
static const double MODE_LIFT = 4.0;

/** HOW WELL A MODE OF A GIVEN FREQUENCY GETS THE SOUND INTO THE AIR.

A PLATE IS A POOR RADIATOR UNTIL ITS BENDING WAVES TRAVEL AS FAST AS SOUND IN AIR. Below that
frequency — the critical frequency, a few hundred hertz for a piano soundboard — the board's
surface pushes air back and forth between its own crests and troughs instead of pushing it away, so
very little leaves. Above it, nearly all of the motion radiates. This rises as the frequency does
and levels off, which is the shape of it. It is why a piano needs a board of that size at all, and
why the bottom octave is carried by its partials rather than its fundamentals. */
static const double CRITICAL_HZ = 420.0;

static double radiatesAt(double hz) {
	const double x = hz / CRITICAL_HZ;
	return std::sqrt(x * x / (1.0 + x * x));
}


/** WHERE THE SMOOTH REMAINDER TAKES OVER FROM THE MODES, as a multiple of the highest modelled
mode's frequency. The remainder stands for every mode above the ones modelled one by one, so it has
no business being there at frequencies where those modes are not — and putting it there was the
first attempt's mistake: a full flat background with sparse modes added on top counts the low
region twice, and the admittance between fifty hertz and seven hundred came out two to five times
what it was asked for, which would have quietly undone the decay calibration of every phase before
this one.

AND A PLATE REALLY DOES GO STIFF BELOW ITS LOWEST MODE. There is nothing for the force to excite,
so the bridge barely moves at all, and a bass note's fundamental is hardly radiated — which is why
the bottom octave of a piano is heard through its partials and why the board has to be as large as
it is. So the remainder is not a resistance but a rising one: nothing at the bottom, the asked-for
figure above the modes. */
static const double REMAINDER_CORNER = 1.15;


void Soundboard::setSampleRate(float hz) {
	if (hz > 0.f && (double) hz != rate) {
		rate = hz;
		built = false;
	}
}

void Soundboard::setAdmittance(float y) {
	const double v = std::fmax(1e-6, std::fmin((double) y, 1e-1));
	if (v != smooth) {
		smooth = v;
		built = false;
	}
}

void Soundboard::setModeCount(int n) {
	const int v = std::max(0, std::min(n, 128));
	if (v != wanted) {
		wanted = v;
		built = false;
	}
}


/** ONE MODE AS A TWO-POLE RESONATOR.

	y[n] = g (f[n] - f[n-2]) + a1 y[n-1] + a2 y[n-2]

The two zeros, at nothing and at the top of the band, are what make it an ADMITTANCE rather than a
displacement: a mass on a spring moves furthest at the bottom of the band and this has to move
FASTEST at its own frequency and nowhere else, which is a bandpass. Its gain at its own frequency
works out at twice g over one minus the pole radius squared, so g is set from the peak wanted.

AND g IS ALSO THE GAIN ON THE PRESENT SAMPLE, which is the number the junction needs. That is not a
coincidence: it is the only term in the difference equation that has f[n] in it. */
void Soundboard::prepare() {
	built = true;
	modes.clear();
	modes.reserve((size_t) wanted);

	// Scattered deterministically, so an instrument is the same instrument every time.
	unsigned seed = 1234567u;
	auto random = [&]() {
		seed ^= seed << 13;
		seed ^= seed >> 17;
		seed ^= seed << 5;
		return (double) (seed >> 8) / 16777216.0 - 0.5;
	};

	double highest = FIRST_MODE_HZ;
	for (int k = 0; k < wanted; k++) {
		Mode m;
		m.hz = FIRST_MODE_HZ + MODE_SPACING_HZ * ((double) k + MODE_SCATTER * 2.0 * random());
		if (m.hz < 20.0 || m.hz > rate * 0.45)
			continue;
		const double up = std::fmin(1.0, (double) k / std::fmax(1.0, (double) wanted - 1.0));
		m.q = Q_LOW + (Q_HIGH - Q_LOW) * up;
		m.peak = smooth * MODE_LIFT;
		m.radiates = radiatesAt(m.hz);

		const double theta = 2.0 * PI * m.hz / rate;
		const double r = std::exp(-theta / (2.0 * m.q));
		m.a1 = 2.0 * r * std::cos(theta);
		m.a2 = -r * r;
		m.g = m.peak * (1.0 - r * r) * 0.5;
		modes.push_back(m);
		highest = std::fmax(highest, m.hz);
	}

	// THE SMOOTH REMAINDER, as a first-order rise: nothing at the bottom, the asked-for admittance
	// above the modelled modes.
	//
	//     H(z) = g (1 - z^-1) / (1 - p z^-1)
	//
	// which is nought at DC by construction — the numerator is a difference — and twice g over one
	// plus p at the top of the band, so g comes from the figure wanted there.
	// WITH NO MODES IT IS A PLAIN RESISTANCE, which is the board the phases before this one had
	// and the control this one is measured against. It has to be genuinely flat to be that
	// control: leaving the rise in place would make a board that goes stiff below fifty hertz, and
	// comparing against it would be comparing two boards rather than one board with and without
	// its modes.
	flat = modes.empty();
	const double corner = std::fmin(highest * REMAINDER_CORNER, rate * 0.4);
	smoothPole = flat ? 0.0 : std::exp(-2.0 * PI * corner / rate);
	smoothGain = flat ? smooth : smooth * (1.0 + smoothPole) * 0.5;
	smoothRadiates = flat ? 1.0 : radiatesAt(corner);

	instant = smoothGain;
	for (const Mode& m : modes)
		instant += m.g;
	silence();
}


void Soundboard::silence() {
	for (Mode& m : modes)
		m.y1 = m.y2 = 0.0;
	smoothOut = 0.0;
	force1 = force2 = 0.0;
	velocity = 0.0;
	sound = 0.0;
}


double Soundboard::standing() const {
	// Everything the modes are already carrying, which is their answer with no force applied now.
	double at = flat ? 0.0 : -smoothGain * force1 + smoothPole * smoothOut;
	for (const Mode& m : modes)
		at += -m.g * force2 + m.a1 * m.y1 + m.a2 * m.y2;
	return at;
}


void Soundboard::drive(double newtons) {
	const double s = flat ? smooth * newtons
		: smoothGain * (newtons - force1) + smoothPole * smoothOut;
	smoothOut = s;
	double total = s;
	double heard = s * smoothRadiates;
	for (Mode& m : modes) {
		const double y = m.g * (newtons - force2) + m.a1 * m.y1 + m.a2 * m.y2;
		m.y2 = m.y1;
		m.y1 = y;
		total += y;
		heard += y * m.radiates;
	}
	force2 = force1;
	force1 = newtons;
	velocity = total;
	sound = heard;
}


float Soundboard::admittanceAt(float hz) const {
	const double w = 2.0 * PI * (double) hz / rate;
	// The smooth part is real; each mode is its resonator's response at that frequency.
	const double cw = std::cos(w), sw = std::sin(w);
	// The remainder: g(1 - z^-1)/(1 - p z^-1), or a plain resistance when there are no modes.
	double re = flat ? smooth : 0.0, im = 0.0;
	if (!flat) {
		const double nRe = smoothGain * (1.0 - cw), nIm = smoothGain * sw;
		const double dRe = 1.0 - smoothPole * cw, dIm = smoothPole * sw;
		const double den = dRe * dRe + dIm * dIm;
		if (den > 1e-30) {
			re += (nRe * dRe + nIm * dIm) / den;
			im += (nIm * dRe - nRe * dIm) / den;
		}
	}
	const double c2 = std::cos(2.0 * w), s2 = std::sin(2.0 * w);
	for (const Mode& m : modes) {
		// numerator g(1 - z^-2), denominator 1 - a1 z^-1 - a2 z^-2
		const double nRe = m.g * (1.0 - c2), nIm = m.g * s2;
		const double dRe = 1.0 - m.a1 * cw - m.a2 * c2;
		const double dIm = m.a1 * sw + m.a2 * s2;
		const double den = dRe * dRe + dIm * dIm;
		if (den < 1e-30)
			continue;
		re += (nRe * dRe + nIm * dIm) / den;
		im += (nIm * dRe - nRe * dIm) / den;
	}
	return (float) std::sqrt(re * re + im * im);
}


} // namespace px
