/** See BrassGrid.hpp. */
#include "BrassGrid.hpp"

#include <cmath>
#include <complex>

namespace px {

using cd = std::complex<double>;


void brassHalfOrderFit(int count, double lowHz, double highHz, double fitTopHz, std::vector<double>* xi,
                       std::vector<double>* mu) {
	xi->assign((size_t) count, 0.0);
	mu->assign((size_t) count, 0.0);
	if (count <= 0)
		return;
	for (int m = 0; m < count; m++) {
		const double f = count == 1 ? std::sqrt(lowHz * highHz)
			: lowHz * std::pow(highHz / lowHz, (double) m / (count - 1));
		(*xi)[(size_t) m] = 2.0 * M_PI * f;
	}
	// The normal equations of a real least-squares fit to complex values, over 400 frequencies.
	std::vector<double> a((size_t) count * count, 0.0), b((size_t) count, 0.0);
	const int samples = 400;
	for (int i = 0; i < samples; i++) {
		const double f = 10.0 * std::pow(fitTopHz / 10.0, (double) i / (samples - 1));
		const cd s(0.0, 2.0 * M_PI * f);
		const cd target = std::sqrt(s);
		const double w = 1.0 / std::norm(target);
		std::vector<cd> basis((size_t) count);
		for (int m = 0; m < count; m++)
			basis[(size_t) m] = s / (s + (*xi)[(size_t) m]);
		for (int r = 0; r < count; r++) {
			for (int c = 0; c < count; c++)
				a[(size_t) (r * count + c)] += w * std::real(std::conj(basis[(size_t) r]) * basis[(size_t) c]);
			b[(size_t) r] += w * std::real(std::conj(basis[(size_t) r]) * target);
		}
	}
	// Gaussian elimination with partial pivoting.
	for (int col = 0; col < count; col++) {
		int pivot = col;
		for (int r = col + 1; r < count; r++)
			if (std::fabs(a[(size_t) (r * count + col)]) > std::fabs(a[(size_t) (pivot * count + col)]))
				pivot = r;
		for (int c = 0; c < count; c++)
			std::swap(a[(size_t) (col * count + c)], a[(size_t) (pivot * count + c)]);
		std::swap(b[(size_t) col], b[(size_t) pivot]);
		for (int r = col + 1; r < count; r++) {
			const double f = a[(size_t) (r * count + col)] / a[(size_t) (col * count + col)];
			for (int c = col; c < count; c++)
				a[(size_t) (r * count + c)] -= f * a[(size_t) (col * count + c)];
			b[(size_t) r] -= f * b[(size_t) col];
		}
	}
	for (int r = count - 1; r >= 0; r--) {
		double s = b[(size_t) r];
		for (int c = r + 1; c < count; c++)
			s -= a[(size_t) (r * count + c)] * (*mu)[(size_t) c];
		(*mu)[(size_t) r] = s / a[(size_t) (r * count + r)];
	}
}


/** THE RADIUS of the bore `x` metres from its start, along its sections. */
static double radiusAt(const BrassBore& b, double x) {
	double at = 0.0;
	for (const BrassSection& s : b.sections) {
		if (x <= at + s.length || &s == &b.sections.back()) {
			const double u = s.length > 0.0 ? std::fmin(1.0, std::fmax(0.0, (x - at) / s.length)) : 0.0;
			return s.r0 + (s.r1 - s.r0) * u;
		}
		at += s.length;
	}
	return b.sections.empty() ? 0.01 : b.sections.back().r1;
}


void BrassGrid::setup(const Setup& s) {
	air = s.air;
	k = 1.0 / s.rate;
	const double length = s.bore.length();
	// THE SPACING a little over c times the step, never at it: at exactly the Courant limit the scheme with
	// its ends was unstable, at 88.2 kHz for one pull of the tuning slide and not another. Willemsen et al.
	// (2021) keep the same margin, a Courant number of 0.999.
	const int n = std::max(2, (int) std::floor(0.999 * length / (air.c * k)));
	h = length / n;
	p.assign((size_t) n + 1, 0.0);
	v.assign((size_t) n, 0.0);
	area.assign((size_t) n + 1, 0.0);
	halfArea.assign((size_t) n, 0.0);
	lossP.assign((size_t) n + 1, 0.0);
	lossV.assign((size_t) n, 0.0);
	for (int l = 0; l <= n; l++) {
		const double r = radiusAt(s.bore, l * h);
		area[(size_t) l] = M_PI * r * r;
		lossP[(size_t) l] = 2.0 * (air.gamma - 1.0) * std::sqrt(air.nu() / air.prandtl) / r;
	}
	for (int l = 0; l < n; l++) {
		const double r = radiusAt(s.bore, (l + 0.5) * h);
		halfArea[(size_t) l] = M_PI * r * r;
		lossV[(size_t) l] = 2.0 * air.rho * std::sqrt(air.nu()) / r;
	}
	terms = std::max(0, s.lossTerms);
	brassHalfOrderFit(terms, 20.0, 0.45 * s.rate, 0.45 * s.rate, &xi, &mu);
	gain.assign((size_t) terms, 0.0);
	sumMuGain = 0.0;
	for (int m = 0; m < terms; m++) {
		gain[(size_t) m] = 1.0 / (1.0 + 0.5 * xi[(size_t) m] * k);
		sumMuGain += mu[(size_t) m] * gain[(size_t) m];
	}
	phiP.assign((size_t) (n + 1) * terms, 0.0);
	phiV.assign((size_t) n * terms, 0.0);
	cup = throat = radiated = mouthFlow = 0.0;
	direct = s.direct;
	startPort = s.startIsPort;
	endPort = s.endIsPort;
	portImpedance[0] = air.rho * air.c / area.front();
	portImpedance[1] = air.rho * air.c / area.back();
	arriving[0] = arriving[1] = leavingWave[0] = leavingWave[1] = 0.0;
	cupVolume = s.bore.mouthpiece.cupVolume;
	inertance = s.bore.mouthpieceInertance(air);
	resistance = s.bore.mouthpiece.resistance;
	const double a = std::sqrt(area.back() / M_PI);
	radiationInertance = air.rho * 0.6133 * a / area.back();
	radiationResistance = air.rho * air.c / area.back();
}


double BrassGrid::step(double flow) {
	begin(nullptr, nullptr);
	return end(flow);
}


void BrassGrid::begin(double* cupWithout, double* cupPerFlow) {
	const int n = (int) v.size();
	const double rhoC2 = air.rho * air.c * air.c;
	// VELOCITY, half a step on, each with its memory of the half-order derivative.
	for (int l = 0; l < n; l++) {
		double* phi = &phiV[(size_t) l * terms];
		const double a = lossV[(size_t) l];
		double memory = 0.0;
		for (int m = 0; m < terms; m++)
			memory += mu[(size_t) m] * xi[(size_t) m] * gain[(size_t) m] * phi[m];
		const double old = v[(size_t) l];
		const double next = ((air.rho / k - 0.5 * a * sumMuGain) * old - (p[(size_t) l + 1] - p[(size_t) l]) / h + a * memory)
			/ (air.rho / k + 0.5 * a * sumMuGain);
		v[(size_t) l] = next;
		const double mean = 0.5 * (old + next);
		for (int m = 0; m < terms; m++)
			phi[m] = (phi[m] + k * mean - 0.5 * xi[(size_t) m] * k * phi[m]) * gain[(size_t) m];
	}

	// THE FIRST POINT'S CELL, as far as it can be worked out before the flow into the cup is known: and the
	// cup's pressure after the step as a straight line in that flow, for the lips (see end).
	{
		const double a = lossP[0];
		double memory = 0.0;
		for (int m = 0; m < terms; m++)
			memory += mu[(size_t) m] * xi[(size_t) m] * gain[(size_t) m] * phiP[(size_t) m];
		firstCell = 0.5 * h * area[0];
		firstD = 1.0 / k + 0.5 * a * sumMuGain;
		firstX = (1.0 / k - 0.5 * a * sumMuGain) * p[0] + a * memory - rhoC2 / firstCell * halfArea[0] * v[0];
		if (!startPort && !direct) {
			const double c = cupVolume / rhoC2;
			const double den = inertance / k + resistance + 0.5 * k / c + 0.5 * rhoC2 / (firstD * firstCell);
			throatWithout = (inertance / k * throat + cup - 0.5 * p[0] - 0.5 * firstX / firstD) / den;
			throatPerFlow = 0.5 * k / c / den;
			if (cupWithout)
				*cupWithout = cup - k / c * throatWithout;
			if (cupPerFlow)
				*cupPerFlow = k / c * (1.0 - throatPerFlow);
		}
		else {
			if (cupWithout)
				*cupWithout = p[0];
			if (cupPerFlow)
				*cupPerFlow = 0.0;
		}
	}
}


double BrassGrid::end(double flow) {
	const int n = (int) v.size();
	const double rhoC2 = air.rho * air.c * air.c;
	// THE MOUTHPIECE AND THE FIRST POINT, SOLVED TOGETHER. The cup fills with what the lips let in and
	// empties into the throat; the throat's flow follows the difference between the cup's pressure and the
	// first point's, both their means over the step, through the inertance and the resistance; the first
	// point's half cell takes the throat's flow in and gives the grid's out. Three equations, linear, in the
	// throat's flow and the two new pressures: solved at once, so that the throat's inertance against the
	// first point's small cell — about 0.14 cubic centimetres at 44.1 kHz — cannot run away, as it did
	// stepped explicitly. Worked out as far as it can be in begin.
	{
		double* phi = &phiP[0];
		const double cell = firstCell, d = firstD, x = firstX;
		const double old = p[0];
		double next;
		if (startPort) {
			// A TUBE GOING ON: the flow in is the arriving wave's twice over, less the mean pressure, over the
			// tube's impedance; solved with the half cell, and what leaves is the mean pressure less what came.
			const double z = portImpedance[0];
			next = (x + rhoC2 / cell * (2.0 * arriving[0] - 0.5 * old) / z) / (d + 0.5 * rhoC2 / (cell * z));
			leavingWave[0] = 0.5 * (old + next) - arriving[0];
		}
		else {
			if (direct)
				throat = flow;
			else {
				const double c = cupVolume / rhoC2;
				throat = throatWithout + throatPerFlow * flow;
				cup += k / c * (flow - throat);
			}
			next = (x + rhoC2 / cell * throat) / d;
		}
		p[0] = next;
		const double mean = 0.5 * (old + next);
		for (int m = 0; m < terms; m++)
			phi[m] = (phi[m] + k * mean - 0.5 * xi[(size_t) m] * k * phi[m]) * gain[(size_t) m];
	}

	// PRESSURE, a step on, at the other points: each one's cell takes the flow in on one side and out on the
	// other; the last has half a cell, the radiation's flow out at the mouth, or a port's.
	for (int l = 1; l <= n; l++) {
		double* phi = &phiP[(size_t) l * terms];
		const double a = lossP[(size_t) l];
		double memory = 0.0;
		for (int m = 0; m < terms; m++)
			memory += mu[(size_t) m] * xi[(size_t) m] * gain[(size_t) m] * phi[m];
		const double old = p[(size_t) l];
		double inflow, outflow, cell;
		if (l == n && endPort) {
			inflow = halfArea[(size_t) n - 1] * v[(size_t) n - 1];
			cell = 0.5 * h * area[(size_t) n];
			const double z = portImpedance[1];
			const double c = cell / (rhoC2 * k);
			const double lossFactor = cell / rhoC2 * 0.5 * a * sumMuGain;
			const double next = ((c - lossFactor - 0.5 / z) * old + cell / rhoC2 * a * memory + inflow + 2.0 * arriving[1] / z)
				/ (c + lossFactor + 0.5 / z);
			p[(size_t) l] = next;
			leavingWave[1] = 0.5 * (old + next) - arriving[1];
			const double mean = 0.5 * (old + next);
			for (int m = 0; m < terms; m++)
				phi[m] = (phi[m] + k * mean - 0.5 * xi[(size_t) m] * k * phi[m]) * gain[(size_t) m];
			continue;
		}
		if (l == n) {
			inflow = halfArea[(size_t) n - 1] * v[(size_t) n - 1];
			cell = 0.5 * h * area[(size_t) n];
			// The radiation: its inertance's flow, from the last step's mouth pressure, and its resistance's,
			// from the mean of this step's and the last, solved for together with the cell.
			radiated += k / radiationInertance * old;
			const double c = cell / (rhoC2 * k);
			const double lossFactor = cell / rhoC2 * 0.5 * a * sumMuGain;
			const double next = ((c - lossFactor) * old + cell / rhoC2 * a * memory + inflow - radiated
				- 0.5 * old / radiationResistance) / (c + lossFactor + 0.5 / radiationResistance);
			p[(size_t) l] = next;
			mouthFlow = radiated + 0.5 * (old + next) / radiationResistance;
			const double mean = 0.5 * (old + next);
			for (int m = 0; m < terms; m++)
				phi[m] = (phi[m] + k * mean - 0.5 * xi[(size_t) m] * k * phi[m]) * gain[(size_t) m];
			continue;
		}
		else {
			inflow = halfArea[(size_t) l - 1] * v[(size_t) l - 1];
			outflow = halfArea[(size_t) l] * v[(size_t) l];
			cell = 0.5 * h * (halfArea[(size_t) l - 1] + halfArea[(size_t) l]);
		}
		// cell / (rho c^2) times (dp/dt + a d^(1/2)p) = inflow - outflow
		const double next = ((1.0 / k - 0.5 * a * sumMuGain) * old + a * memory + rhoC2 / cell * (inflow - outflow))
			/ (1.0 / k + 0.5 * a * sumMuGain);
		p[(size_t) l] = next;
		const double mean = 0.5 * (old + next);
		for (int m = 0; m < terms; m++)
			phi[m] = (phi[m] + k * mean - 0.5 * xi[(size_t) m] * k * phi[m]) * gain[(size_t) m];
	}
	return direct ? p[0] : cup;
}


} // namespace px
