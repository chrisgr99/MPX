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
	corner = shape.cornerHeight;
	setResonance(200.0);
	for (Resonance& r : tract)
		r = Resonance();
	tractHeld = 0.0;
}


void BrassHingedLips::setFormant(int k, const Formant& f) {
	if (k < 0 || k > 5)
		return;
	Resonance& r = tract[k];
	r.on = f.strength > 0.0 && f.hz > 0.0 && f.quality > 0.0;
	if (!r.on) {
		r.b0 = r.b2 = r.a1 = r.a2 = 0.0;
		return;
	}
	// R beta s / (s^2 + beta s + w^2), s = K (1 - 1/z) / (1 + 1/z), K putting w where it belongs.
	const double rate = 1.0 / k_(), w = 2.0 * M_PI * std::fmin(f.hz, 0.45 * rate), beta = w / f.quality;
	const double K = w / std::tan(0.5 * w / rate), d = K * K + beta * K + w * w;
	r.b0 = f.strength * beta * K / d;
	r.b2 = -r.b0;
	r.a1 = 2.0 * (w * w - K * K) / d;
	r.a2 = (K * K - beta * K + w * w) / d;
}


void BrassHingedLips::setResonance(double hz) {
	hz = std::fmax(10.0, hz);
	lipHz = hz;
	m = shape.mass / (4.0 * M_PI * M_PI * hz);
	stiffness = shape.mass * hz;
}


double BrassHingedLips::step(double cupNow, double cupWithout, double cupPerFlow) {
	// THE CORNERS, firmer the harder the player blows, where the shape says so.
	if (shape.cornerSoft > 0.0 && shape.cornerHeight > 0.0) {
		const double over = mouth / (shape.thresholdPerHz * lipHz);
		const double u = std::fmax(0.0, std::fmin(1.0, std::log(std::fmax(1e-9, over) / shape.firmFrom) / std::log(shape.firmTo / shape.firmFrom)));
		corner = shape.cornerSoft * std::pow(shape.cornerHeight / shape.cornerSoft, u);
	}
	else
		corner = shape.cornerHeight;
	// THE LIP from this step's forces: the pressure across it on its length, at right angles; the pressure in
	// the opening on its inner face; its spring towards rest; and, shut, the lips pressing back.
	const bool shut = y < 0.0;
	// The damping rises as the corners meet, from the open lip's to the shut lip's.
	const double open1 = shape.cornerHeight > 0.0 && blendDamping ? openShare(y) : (shut ? 0.0 : 1.0);
	const double q = 1.0 / (open1 / shape.quality + (1.0 - open1) / shape.qualityShut);
	const double lx = x - shape.jointX, ly = y - shape.jointY;
	// The gap at rest, held open by the breath where the lips are set apart.
	const double restY = shape.restY > 0.0 && gapBreath > 0.0 ? shape.restY * std::fmin(1.0, std::fmax(0.0, mouth / (0.8 * gapBreath))) : shape.restY;
	const double mouthNow = mouth - tongueShare * tractHeld;
	const double across = shape.breadth * (mouthNow - cupNow);
	double fx = across * -ly - 0.5 * stiffness * (x - shape.restX);
	double fy = across * lx + shape.breadth * shape.thickness * inOpening - 0.5 * stiffness * (y - restY);
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
	// The vocal tract's pressure after the step, a straight line in the flow: what it holds back whatever
	// the flow, and how much more for each cubic metre a second.
	double tractWithout = 0.0, tractPerFlow = 0.0;
	for (const Resonance& r : tract)
		if (r.on) {
			tractWithout += r.b2 * r.u2 - r.a1 * r.y1 - r.a2 * r.y2;
			tractPerFlow += r.b0;
		}
	// Through the tongue: its share of the tract's pressure, and of the flow into it.
	tractWithout *= tongueShare;
	tractPerFlow *= tongueShare * tongueShare;
	const double mouthWithout = mouth - tractWithout;
	double through = 0.0;
	if (open > 0.0) {
		const double sc = shape.cupArea;
		const double a = air.rho * (0.5 / (open * open) - 1.0 / (sc * open) + 1.0 / (sc * sc));
		const double l = air.rho * shape.thickness / open;
		const double g = l / k + halfSlope + tractPerFlow;
		const double r = mouthWithout - cupMeanWithout - halfSlope * swept + l / k * flow;
		through = 2.0 * r / (g + std::sqrt(g * g + 4.0 * a * std::fabs(r)));
		const double cupMean = cupMeanWithout + halfSlope * (through + swept);
		const double channel = cupMean - air.rho * through * through * (1.0 / (sc * open) - 1.0 / (sc * sc));
		inOpening = share * channel + (1.0 - share) * (mouthWithout - tractPerFlow * through);
	}
	else
		inOpening = mouthWithout;
	// The formants take the step's flow.
	double held = 0.0;
	for (Resonance& r : tract) {
		if (!r.on) {
			r.u1 = r.u2 = r.y1 = r.y2 = 0.0;
			continue;
		}
		const double drive = tongueShare * through;
		const double yNow = r.b0 * drive + r.b2 * r.u2 - r.a1 * r.y1 - r.a2 * r.y2;
		r.u2 = r.u1;
		r.u1 = drive;
		r.y2 = r.y1;
		r.y1 = yNow;
		held += yNow;
	}
	tractHeld = held;
	flow = through;
	return through + swept;
}


} // namespace px
