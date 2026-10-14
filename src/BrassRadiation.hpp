#pragma once
/** THE SOUND THAT LEAVES THE BELL, AS A LISTENER HEARS IT: phase 5 of docs/brass.md.

FROM THE FLOW AT THE MOUTH TO THE PRESSURE AT THE LISTENER. A mouth of radius a with flow U radiates power
U^2 Re(Z_rad); a listener r away at angle theta from the bell's axis hears

    p^2 = rho c U^2 Re(Z_rad) G(theta) / (4 pi r^2)

G being the power gain over a source radiating the same power equally every way. While ka is small, G is
one and Re(Z_rad) is rho c k^2 / (4 pi): p = rho c k U / (4 pi r), a small source. As ka grows the mouth
radiates less than a small source would (Re(Z_rad) tends to rho c / (pi a^2)) and beams it forward (G at
nought tends to (ka)^2): on the axis twice the small source's pressure, to the side far less. For a
trombone's bell, a = 0.108 m, ka is one at 505 Hz.

LEVINE AND SCHWINGER'S UNFLANGED PIPE (Physical Review 73, 383, 1948), exact while one mode travels in the
pipe, ka under 3.83:

    |R| = exp( -(2 ka / pi) int_0^ka delta(x) / (x sqrt((ka)^2 - x^2)) dx )

    G(theta) = 4 / (pi sin^2 theta) * J1(s) / sqrt(J1(s)^2 + N1(s)^2) * |R| / (1 - |R|^2)
               * exp( (2 ka cos theta / pi) P int_0^ka x delta(x) / ((x^2 - s^2) sqrt((ka)^2 - x^2)) dx )

with s = ka sin theta and delta(x) = tan^-1(-J1(x) / N1(x)), taken from nought to pi. G(0) = (ka)^2 /
(1 - |R|^2) and G(pi) = |R|^2 G(0), as they state. Re(Z_rad) / (rho c / (pi a^2)) = (1 - |R|^2) / |1 -
R|^2, R = -|R| exp(2 i k l), the end correction l from Silva et al.'s (2009) fit.

ABOVE ka = 3.83 the mouth carries more than one mode and the formula no longer holds; there the pattern is
a piston's with Kirchhoff's obliquity, ((1 + cos theta) / 2 * 2 J1(s) / s)^2, scaled so that it radiates
all the power, and between 3.4 and 3.83 the two are mixed; |R| falls to nought and Re(Z_rad) to rho c /
(pi a^2). The listener hears the mean gain over the angles within 10 degrees of theirs: the flat piston
has directions of no sound at each frequency that a bell's curved mouth does not.

THE FILTER. A bell is fixed and linear, so what reaches the listener from the wave entering it is one
filter: the bell's own response, a BrassGrid driven at its port with an impulse, from that wave to the flow
at its mouth; times the radiation above, to the listener's pressure, with the least phase that magnitude
allows. It is designed at setup and run as a finite impulse response.

NO RACK IN HERE. */
#include "BrassBore.hpp"
#include "BrassGrid.hpp"

#include <vector>

namespace px {


/** Bessel's functions of order one, of the first and second kind (Abramowitz and Stegun 9.4.4 to 9.4.6). */
double brassJ1(double x);
double brassY1(double x);

/** LEVINE AND SCHWINGER'S |R| for the dominant mode at an unflanged mouth, ka under 3.83. */
double brassUnflangedReflection(double ka);

/** THE POWER GAIN at angle `theta` (radians from the axis), ka under 3.83, as above. */
double brassUnflangedGain(double ka, double theta);

/** THE POWER GAIN over all ka: Levine and Schwinger's under 3.4, a piston's with Kirchhoff's obliquity
over 3.83, mixed between; the mean over the angles within `spread` radians of theta. */
double brassBellGain(double ka, double theta, double spread = 0.0);

/** THE RADIATION RESISTANCE over all ka, as a share of rho c / (pi a^2). */
double brassBellResistance(double ka);


/** WHERE THE LISTENER IS. */
struct BrassListener {
	/** Radians from the bell's axis. 20 degrees brings the model nearest the Iowa recordings, whose
	microphone's place is not given (make brasssound). */
	double angle = 20.0 * 3.14159265358979 / 180.0;
	/** Metres from the bell's mouth. */
	double distance = 1.0;
	/** The gain is the mean over the angles this far either side, radians: a flat piston's pattern has
	directions of no sound at all at each frequency, which a bell's curved mouth does not, and which
	measured directivity, reported in bands, does not show. */
	double spread = 10.0 * 3.14159265358979 / 180.0;
	/** A small source instead: G one and Re(Z_rad) rho c k^2 / (4 pi) at every frequency, for checking. */
	bool smallSource = false;
};


/** THE BELL TO THE LISTENER, as a finite impulse response: from the wave entering the bell's port, pascals,
to the pressure at the listener, pascals. */
struct BrassListenerFilter {
	/** `bell` the bell's grid as BrassWaveguide sets it up, its start a port. */
	void design(const BrassGrid::Setup& bell, const BrassListener& where);
	double process(double x);
	int taps() const { return (int) h.size(); }
	/** The filter's response at `hz`, for checking. */
	double magnitude(double hz) const;

private:
	void designAnew(const BrassGrid::Setup& bell, const BrassListener& where);
	std::vector<double> h, history;
	double rate = 48000.0;
	int at = 0;
};


} // namespace px
