/** See BrassLips.hpp. */
#include "BrassLips.hpp"

#include <cmath>

namespace px {


void BrassLips::setup(const Shape& s, const BrassAir& a, double rate) {
	shape = s;
	air = a;
	k = 1.0 / rate;
	y = yPrev = lastDp = 0.0;
	mouth = 0.0;
	setResonance(200.0);
}


void BrassLips::setResonance(double hz) {
	hz = std::fmax(10.0, hz);
	omega = 2.0 * M_PI * hz;
	mass = shape.massAtReference * shape.referenceHz / hz;
}


double BrassLips::step(double cupNow, double cupWithout, double cupPerFlow) {
	// THE LIP MOVES under the last step's pressure difference: centred differences, the damping's between
	// the step before and the step after.
	const bool shut = shape.restOpening + y < 0.0;
	const double q = shut ? shape.qualityShut : shape.quality;
	const double sigma = omega / q;
	double force = shape.area * lastDp;
	if (shut)
		force -= shape.shutStiffness * mass * omega * omega * (shape.restOpening + y);
	const double next = (2.0 * y - (1.0 - 0.5 * sigma * k) * yPrev - k * k * omega * omega * y + k * k * force / mass)
		/ (1.0 + 0.5 * sigma * k);
	const double swept = shape.area * (next - y) / k;
	const double open = std::fmax(0.0, shape.restOpening + 0.5 * (y + next));
	yPrev = y;
	y = next;

	// THE FLOW AND THE PRESSURE DIFFERENCE OVER THE STEP, TOGETHER. The cup's mean pressure over the step
	// is a straight line in the flow, so dp = D - (cupPerFlow / 2) (Bernoulli's flow), with D what is known;
	// writing dp as its sign times X squared, X^2 + c X - |D| = 0.
	const double halfSlope = 0.5 * cupPerFlow;
	const double d = mouth - 0.5 * (cupNow + cupWithout) - halfSlope * swept;
	const double bernoulli = shape.width * open * std::sqrt(2.0 / air.rho);
	const double c = halfSlope * bernoulli;
	const double x = 0.5 * (-c + std::sqrt(c * c + 4.0 * std::fabs(d)));
	const double dp = (d >= 0.0 ? 1.0 : -1.0) * x * x;
	lastDp = dp;
	return (dp >= 0.0 ? 1.0 : -1.0) * bernoulli * x + swept;
}


void BrassHingedLips::setup(const Shape& s, const BrassAir& a, double rate) {
	shape = s;
	air = a;
	k = 1.0 / rate;
	x = xPrev = shape.restX;
	y = yPrev = shape.restY;
	flow = 0.0;
	mouth = 0.0;
	inOpening = 0.0;
	setResonance(200.0);
}


void BrassHingedLips::setResonance(double hz) {
	hz = std::fmax(10.0, hz);
	m = shape.mass / (4.0 * M_PI * M_PI * hz);
	stiffness = shape.mass * hz;
}


double BrassHingedLips::step(double cupNow, double cupWithout, double cupPerFlow) {
	// THE LIP from this step's forces: the pressure across it on its length, at right angles; the pressure in
	// the opening on its inner face; its spring towards rest; and, shut, the lips pressing back.
	const bool shut = y < 0.0;
	// The damping rises as the corners meet, from the open lip's to the shut lip's.
	const double open1 = shape.cornerHeight > 0.0 && blendDamping ? openShare(y) : (shut ? 0.0 : 1.0);
	const double q = 1.0 / (open1 / shape.quality + (1.0 - open1) / shape.qualityShut);
	const double lx = x - shape.jointX, ly = y - shape.jointY;
	const double across = shape.breadth * (mouth - cupNow);
	double fx = across * -ly - 0.5 * stiffness * (x - shape.restX);
	double fy = across * lx + shape.breadth * shape.thickness * inOpening - 0.5 * stiffness * (y - shape.restY);
	if (shut)
		fy -= shape.shutStiffness * stiffness * y;
	// (1/2) m xi'' + (1/2) (sqrt(m k) / Q) xi' = F, centred.
	const double inertia = 0.5 * m / (k * k), damping = 0.25 * std::sqrt(m * stiffness) / (q * k);
	const double nx = (inertia * (2.0 * x - xPrev) + damping * xPrev + fx) / (inertia + damping);
	const double ny = (inertia * (2.0 * y - yPrev) + damping * yPrev + fy) / (inertia + damping);
	// The flow the lip sweeps into the cup as it moves, over the step.
	const double mx = 0.5 * (x + nx) - shape.jointX, my = 0.5 * (y + ny) - shape.jointY;
	const double swept = shape.breadth * (mx * (ny - y) - my * (nx - x)) / k;
	const double mean = 0.5 * (y + ny);
	const double open = areaAt(mean), share = openShare(mean);
	xPrev = x;
	yPrev = y;
	x = nx;
	y = ny;

	// THE FLOW THROUGH THE OPENING, with the cup's mean pressure over the step a straight line in the total
	// flow: a U|U| + G U = R, U = 2R / (G + sqrt(G^2 + 4 a |R|)).
	const double halfSlope = 0.5 * cupPerFlow;
	const double cupMeanWithout = 0.5 * (cupNow + cupWithout);
	double through = 0.0;
	if (open > 0.0) {
		const double sc = shape.cupArea;
		const double a = air.rho * (0.5 / (open * open) - 1.0 / (sc * open) + 1.0 / (sc * sc));
		const double l = air.rho * shape.thickness / open;
		const double g = l / k + halfSlope;
		const double r = mouth - cupMeanWithout - halfSlope * swept + l / k * flow;
		through = 2.0 * r / (g + std::sqrt(g * g + 4.0 * a * std::fabs(r)));
		const double cupMean = cupMeanWithout + halfSlope * (through + swept);
		const double channel = cupMean - air.rho * through * through * (1.0 / (sc * open) - 1.0 / (sc * sc));
		inOpening = share * channel + (1.0 - share) * mouth;
	}
	else
		inOpening = mouth;
	flow = through;
	return through + swept;
}


} // namespace px
