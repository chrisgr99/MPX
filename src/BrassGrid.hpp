#pragma once
/** THE AIR COLUMN ON A GRID: one of the two ways phase 2 of docs/brass.md builds it.

THE WEBSTER EQUATION WITH ITS WALL LOSSES (Webster-Lokshin, Hélie 2003), in pressure p and particle
velocity v along the bore, the area S(x) taken from the bore's sections:

    rho dv/dt + dp/dx + (2 rho sqrt(nu) / r) d^(1/2)v/dt^(1/2) = 0
    (S / (rho c^2)) (dp/dt + (2 (gamma - 1) sqrt(nu / Pr) / r) d^(1/2)p/dt^(1/2)) + d(S v)/dx = 0

the same physics as BrassBore's calculation in frequency. Pressure at the grid's points and velocity
half way between, a step of time apart by half, the spacing h no shorter than c times the step (the
Courant condition) — 7.8 mm at 44.1 kHz.

THE HALF-ORDER DERIVATIVE, from a few memory variables at every point: s^(1/2) is close, over the band
that matters, to a sum of first-order high-passes, mu_m s / (s + xi_m), whose corners xi_m are spread
evenly on a logarithmic scale and whose weights mu_m are fitted once by least squares (the diffusive
representation; Hélie and Matignon 2006, Berjamin et al. 2017).

THE ENDS. At the mouthpiece the cup (a volume) and the throat (an inertance with a resistance) are lumped,
the lips' flow going into the cup; at the bell's mouth the radiation is an inertance, the end correction,
in parallel with the characteristic resistance (BrassRadiation::PARALLEL_RL).

THE SLIDE, for now, by building the grid again for each position; moving it while playing is the next
step.

NO RACK IN HERE. */
#include "BrassBore.hpp"

#include <vector>

namespace px {


struct BrassGrid {
	/** ASSEMBLED FIELD BY FIELD. */
	struct Setup {
		BrassBore bore;
		BrassAir air;
		double rate = 44100.0;
		/** How many memory variables approximate each half-order derivative; nought, no wall losses. */
		int lossTerms = 4;
		/** THE FLOW STRAIGHT INTO THE GRID'S FIRST POINT, the cup and throat left out: for a bench. */
		bool direct = false;
		/** WHAT EACH END MEETS. At the start the mouthpiece, or a PORT: a tube of the start's radius going on
		as travelling waves (BrassWaveguide); at the end the radiation, or such a port. */
		bool startIsPort = false, endIsPort = false;
	};
	void setup(const Setup& s);

	/** ONE STEP: `flow` cubic metres a second into the cup over the step; the cup's pressure after it,
	pascals. */
	double step(double flow);
	/** THE SAME IN TWO HALVES, for lips whose flow depends on the cup's pressure: begin works out all it can
	without the flow and says what the cup's pressure will be after the step, `cupWithout` plus
	`cupPerFlow` times the flow; end takes the flow and finishes the step. */
	void begin(double* cupWithout, double* cupPerFlow);
	double end(double flow);

	/** THE WAVES AT THE PORTS: what arrives at each before a step, and what leaves each after it, pascals,
	both for the half step between the last step and this one. */
	void arrive(double atStart, double atEnd) {
		arriving[0] = atStart;
		arriving[1] = atEnd;
	}
	double leaving(int end) const { return leavingWave[end]; }

	/** THE FLOW OUT OF THE BELL'S MOUTH over the last step, cubic metres a second: what radiates. A metre in
	front of the bell the pressure is the air's density over four pi times its rate of change. */
	double radiatedFlow() const { return mouthFlow; }

	/** The pressure at the bell's mouth, pascals, after the last step. */
	double mouthPressure() const { return p.empty() ? 0.0 : p.back(); }
	int points() const { return (int) p.size(); }
	double spacing() const { return h; }

private:
	BrassAir air;
	double k = 1.0 / 44100.0, h = 0.0078;
	// The grid: pressure at N + 1 points, velocity at N half-points; their areas and loss coefficients.
	std::vector<double> p, v, area, halfArea, lossP, lossV;
	// The memory variables, M of them at every point and every half-point, laid out point by point.
	int terms = 0;
	std::vector<double> mu, xi, gain, phiP, phiV;
	double sumMuGain = 0.0;
	// The cup's pressure, the throat's flow, and the flow through the radiation's inertance.
	double cup = 0.0, throat = 0.0, radiated = 0.0, mouthFlow = 0.0;
	// What begin worked out for end.
	double firstCell = 0.0, firstD = 1.0, firstX = 0.0, throatWithout = 0.0, throatPerFlow = 0.0;
	double cupVolume = 5e-6, inertance = 1000.0, resistance = 0.0;
	bool direct = false;
	bool startPort = false, endPort = false;
	double portImpedance[2] = {1.0, 1.0}, arriving[2] = {0.0, 0.0}, leavingWave[2] = {0.0, 0.0};
	double radiationInertance = 0.0, radiationResistance = 0.0;
};


/** THE WEIGHTS OF THE DIFFUSIVE REPRESENTATION: `count` corners from `lowHz` to `highHz`, spread evenly
on a logarithmic scale, their weights fitted by least squares to s^(1/2) between 10 Hz and `fitTopHz`,
each frequency weighted by the inverse of |s^(1/2)| so the fit is relative. Corners and weights in
radians a second. */
void brassHalfOrderFit(int count, double lowHz, double highHz, double fitTopHz, std::vector<double>* xi,
                       std::vector<double>* mu);


} // namespace px
