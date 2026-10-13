/** See BowedString.hpp. */
#include "BowedString.hpp"

#include <algorithm>
#include <cmath>

namespace px {


static const float PI = 3.14159265358979f;

/** THE LEAST A SEGMENT MAY BE, in samples. Four-point Lagrange wants a delay of about two to read
between the samples it has, so a contact point closer to the bridge than this cannot be had and is
held out to it. On a cello's low C there are 367 samples each way at 48 kHz and no bow position is
out of reach; on a violin's top string there are 36, and anything under about 0.055 of the length
is. That is the sample rate problem, and the figure the oversampling schedule has to beat. */
static const float MIN_SEGMENT = 2.f;

/** How much the nut sends back: almost all of it, an open string's nut being nearly rigid. The bridge
takes the rest of the fundamental's loss, which puts its resistance at about 350 times the string's
impedance on a cello's open strings, where measurements put it at 340 to 390. A finger stopping
the string loses more, which is for the instrument to set. */
static const float NUT_REFLECT = 0.9995f;
/** And a finger's: Q 281 stopped against 410 open on a violin's D, an amplitude a period of
1 - pi/281 against 1 - pi/410, the finger the difference. */
static const float FINGER_REFLECT = 0.9965f;


BowedString::BowedString() {}


void BowedString::setSampleRate(float r) {
	if (r > 0.f && r != rate) {
		rate = r;
		dirty = true;
		heatDirty = true;
		// Built now rather than at the first sample bowed, which may be on the audio thread.
		if (thermal)
			buildHeat();
	}
}


int BowedString::oversampling(float hz, float rate) {
	int factor = 1;
	while (factor < 16 && rate * (float) factor / std::fmax(1.f, hz) < (float) SAMPLES_A_PERIOD)
		factor *= 2;
	return factor;
}


void BowedString::setFrequency(float hz) {
	hz = std::fmax(20.f, std::fmin(hz, rate * 0.25f));
	if (hz != freq) {
		freq = hz;
		dirty = true;
	}
}


void BowedString::setDecay(float seconds) {
	seconds = std::fmax(0.01f, seconds);
	if (seconds != decay) {
		decay = seconds;
		dirty = true;
	}
}


void BowedString::setDamping(float amount) {
	amount = std::fmax(0.f, std::fmin(1.f, amount));
	if (amount != damping) {
		damping = amount;
		dirty = true;
	}
}


void BowedString::setStiffness(float amount) {
	amount = std::fmax(0.f, std::fmin(1.5f, amount));
	if (amount != stiffness) {
		stiffness = amount;
		dirty = true;
	}
}


void BowedString::setStopped(bool on) {
	if (on != stopped) {
		stopped = on;
		dirty = true;
	}
}


void BowedString::setLossless(bool on) {
	if (on != lossless) {
		lossless = on;
		dirty = true;
	}
}


void BowedString::setTorsion(float impedanceRatio, float speedRatio, float q) {
	twistRatio = std::fmax(0.f, impedanceRatio);
	twistSpeed = std::fmax(1.f, speedRatio);
	twistQ = std::fmax(1.f, q);
	dirty = true;
}


void BowedString::setImpedance(float kgPerSecond) {
	z = std::fmax(0.001f, kgPerSecond);
}


void BowedString::setBow(float forceNewtons, float speedMetresPerSecond) {
	bowForce = std::fmax(0.f, forceNewtons);
	bowSpeed = speedMetresPerSecond;
}


void BowedString::setFriction(float staticCoefficient, float dynamicCoefficient, float speed) {
	muStatic = std::fmax(0.f, staticCoefficient);
	muDynamic = std::fmax(0.f, std::fmin(muStatic, dynamicCoefficient));
	fallSpeed = std::fmax(1e-4f, speed);
}


void BowedString::setBristles(const Bristles& b) {
	bristles = b;
	bristles.stiffness = std::fmax(1.f, b.stiffness);
	bristles.fall = std::fmax(1e-4f, b.fall);
	bristles.breakaway = std::fmax(0.f, std::fmin(0.99f, b.breakaway));
	bristled = true;
	thermal = false;
	rosined = false;
}


void BowedString::useFrictionCurve() {
	bristled = false;
	thermal = false;
	rosined = false;
}


void BowedString::setThermal(const Thermal& t) {
	thermalSet = t;
	thermalSet.hotAt = std::fmax(1e-3f, t.hotAt);
	thermalSet.contactRadius = std::fmax(1e-6f, t.contactRadius);
	thermalSet.layer = std::fmax(1e-8f, t.layer);
	thermal = true;
	bristled = false;
	rosined = false;
	heatDirty = true;
	if (rate > 0.f)
		buildHeat();
}


/** THE LAYERS UNDER THE CONTACT. Each stack, string and bow, is HEAT_LAYERS layers growing
geometrically from a quarter of a micron at the surface, deep enough in all that a steady heating
comes to the steady temperature of a disk heated on a half-space: 8 a / (3 pi k) per unit of heat a
square metre, which is the stack's thermal resistance at 0.85 a of depth. Below the deepest layer is
the room. The rosin that shears is the contact itself, its own heat capacity, and half its thickness
of resistance on the way into either solid. Everything per unit area. */
void BowedString::buildHeat() {
	// What cooling is owed under the old coefficients, before they change.
	catchUpHeat();
	heatDirty = false;
	const Thermal& t = thermalSet;
	const double dt = 1.0 / (double) rate;
	const double a = t.contactRadius;
	const double depth = 8.0 * a / (3.0 * 3.14159265358979);
	const double first = 0.25e-6;
	// The growth that makes HEAT_LAYERS layers from `first` add up to `depth`.
	double lo = 1.0001, hi = 4.0;
	for (int i = 0; i < 60; i++) {
		const double r = 0.5 * (lo + hi);
		const double sum = first * (std::pow(r, HEAT_LAYERS) - 1.0) / (r - 1.0);
		(sum > depth ? hi : lo) = r;
	}
	const double growth = 0.5 * (lo + hi);
	const double rosinCap = (double) t.rosinDensity * t.rosinHeat * t.layer;
	const double rosinHalf = 0.5 * t.layer / t.rosinConductivity;
	heatCap[0] = rosinCap / dt;
	auto stack = [&](int from, double density, double specific, double conductivity, double* toRoom) {
		double d = first, above = rosinHalf;
		for (int i = 0; i < HEAT_LAYERS; i++) {
			heatCap[from + i] = density * specific * d / dt;
			heatUp[from + i] = 1.0 / (above + 0.5 * d / conductivity);
			above = 0.5 * d / conductivity;
			if (i == HEAT_LAYERS - 1)
				*toRoom = 1.0 / above;
			d *= growth;
		}
		// THE ELIMINATION FROM THE DEEPEST LAYER UP: T_i = alpha_i + beta_i T_(i-1), beta and the
		// diagonal fixed by the capacities and conductances; alpha follows the temperatures, per step.
		for (int i = HEAT_LAYERS - 1; i >= 0; i--) {
			const int k = from + i;
			double diag = heatCap[k] + heatUp[k];
			if (i == HEAT_LAYERS - 1)
				diag += *toRoom;
			else
				diag += heatUp[k + 1] * (1.0 - heatBeta[k + 1]);
			heatDiag[k] = diag;
			heatBeta[k] = heatUp[k] / diag;
		}
	};
	stack(1, t.stringDensity, t.stringHeat, t.stringConductivity, &toRoomString);
	stack(1 + HEAT_LAYERS, t.bowDensity, t.bowHeat, t.bowConductivity, &toRoomBow);
	convectPerSpeed = (2.0 / 3.14159265358979) * rosinCap / a;
	heatPerWork = 1.0 / (3.14159265358979 * a * a);

	// ONE STEP OF COOLING AS A MATRIX, column by column: each node warmed by a kelvin alone, stepped.
	// Then its powers of two by squaring.
	const int N = HEAT_NODES;
	std::vector<double>* powers = new std::vector<double>((size_t) (HEAT_POWERS * N * N), 0.0);
	double kept[HEAT_NODES];
	for (int i = 0; i < N; i++)
		kept[i] = heat[i];
	for (int j = 0; j < N; j++) {
		for (int i = 0; i < N; i++)
			heat[i] = (i == j) ? 1.0 : 0.0;
		stepHeat(0.0, 0.0, 0.0);
		for (int i = 0; i < N; i++)
			(*powers)[(size_t) (i * N + j)] = heat[i];
	}
	for (int i = 0; i < N; i++)
		heat[i] = kept[i];
	for (int p = 1; p < HEAT_POWERS; p++) {
		const double* a2 = &(*powers)[(size_t) ((p - 1) * N * N)];
		double* to = &(*powers)[(size_t) (p * N * N)];
		for (int i = 0; i < N; i++)
			for (int j = 0; j < N; j++) {
				double sum = 0.0;
				for (int k = 0; k < N; k++)
					sum += a2[i * N + k] * a2[k * N + j];
				to[i * N + j] = sum;
			}
	}
	heatPowers.reset(powers);
}


void BowedString::catchUpHeat() const {
	if (heatIdle <= 0 || !heatPowers)
		return;
	const int N = HEAT_NODES;
	double v[HEAT_NODES], w[HEAT_NODES];
	for (int i = 0; i < N; i++)
		v[i] = heat[i];
	for (int p = 0; p < HEAT_POWERS && (heatIdle >> p) != 0; p++) {
		if (!((heatIdle >> p) & 1))
			continue;
		const double* m = &(*heatPowers)[(size_t) (p * N * N)];
		for (int i = 0; i < N; i++) {
			double sum = 0.0;
			for (int k = 0; k < N; k++)
				sum += m[i * N + k] * v[k];
			w[i] = sum;
		}
		for (int i = 0; i < N; i++)
			v[i] = w[i];
	}
	for (int i = 0; i < N; i++)
		heat[i] = v[i];
	heatIdle = 0;
}


/** ONE STEP OF BACKWARD EULER through the contact and both stacks: `input` watts a square metre into
the contact, and `convection` watts a square metre a kelvin carried out with the rosin. Unconditionally
stable, so the thinnest layers, which settle in less than a sample, cost nothing to keep.

THE BOW MOVES PAST THE CONTACT. While the string slides, the hair under it slides along, so the bow's
side of the contact is fresh material arriving at the sliding speed: the share `renew` of each of the
bow's layers is replaced by hair at the room's temperature before the step. The string's side stays
the same string, and keeps its heat. Woodhouse's model has this in its Green's function for the moving
surface; without it a stationary bow soaks up the heat until, at a player's bow speeds, the rosin is
hot throughout and the bow only slides. */
void BowedString::stepHeat(double input, double convection, double renew) {
	if (renew > 0.0) {
		const double keep = 1.0 - std::fmin(1.0, renew);
		for (int i = 0; i < HEAT_LAYERS; i++)
			heat[1 + HEAT_LAYERS + i] *= keep;
	}
	double alpha[1 + 2 * HEAT_LAYERS];
	for (int from : {1, 1 + HEAT_LAYERS}) {
		for (int i = HEAT_LAYERS - 1; i >= 0; i--) {
			const int k = from + i;
			double rhs = heatCap[k] * heat[k];
			if (i < HEAT_LAYERS - 1)
				rhs += heatUp[k + 1] * alpha[k + 1];
			alpha[k] = rhs / heatDiag[k];
		}
	}
	const int s1 = 1, b1 = 1 + HEAT_LAYERS;
	const double diag = heatCap[0] + convection + heatUp[s1] * (1.0 - heatBeta[s1])
		+ heatUp[b1] * (1.0 - heatBeta[b1]);
	const double rhs = heatCap[0] * heat[0] + input + heatUp[s1] * alpha[s1] + heatUp[b1] * alpha[b1];
	heat[0] = rhs / diag;
	for (int from : {1, 1 + HEAT_LAYERS}) {
		double above = heat[0];
		for (int i = 0; i < HEAT_LAYERS; i++) {
			const int k = from + i;
			heat[k] = alpha[k] + heatBeta[k] * above;
			above = heat[k];
		}
	}
}


/** THE BOW ON ROSIN THAT HEATS, solved for one sample.

The friction's limit this sample is the coefficient the contact's temperature gives, times the bow
force: it is known before the string is, so the bow either holds the string or it does not, and there
is one answer — Woodhouse's figure 5, the Friedlander construction with a step for a curve, with no
third answer and no hysteresis rule: the memory is the temperature. STUCK, the string moves with the
bow and the friction is whatever that takes, within the limit; SLIPPING, the friction is the limit,
against the slide. Then the heat: the friction's work into the contact while it slides, and the
temperatures moved on a sample. */
float BowedString::thermalJunction(float unbowed) {
	if (heatDirty)
		buildHeat();
	const Thermal& t = thermalSet;
	// NO BOW: the contact only cools, and that is owed rather than stepped (see heatIdle).
	if (bowForce <= 0.f) {
		stuck = false;
		slide = 0.f;
		atBowVelocity = unbowed;
		atBowForce = 0.f;
		heatIdle++;
		return 0.f;
	}
	catchUpHeat();
	const double mu = (double) t.cold - ((double) t.cold - (double) t.hot)
		* std::fmin(1.0, std::fmax(0.0, heat[0] / (double) t.hotAt));
	const double zt = (double) z * (double) twistRatio;
	const double zBow = (twistRatio > 0.f) ? (double) z * zt / ((double) z + zt) : (double) z;
	const double T = 1.0 / (double) rate;
	const double k = (double) bowForce / (2.0 * zBow);
	const double u = (double) bowSpeed - (double) unbowed;
	double x = 0.0;
	if (std::fabs(u) <= k * mu) {
		stuck = true;
	}
	else {
		stuck = false;
		x = u - (u > 0.0 ? 1.0 : -1.0) * k * mu;
	}
	slide = (float) x;
	const double f = (u - x) * 2.0 * zBow;
	// The contact grows with the force, its radius as the square root of it, so the friction's work
	// a square metre does not depend on the force, and the convection, through a narrower contact at
	// a lighter bow, does.
	const double ax = std::fabs(x);
	const double scale = std::sqrt(std::fmax(0.05, (double) bowForce));
	const double width = 2.0 * (double) t.contactRadius * scale;
	stepHeat(mu * ax * heatPerWork, convectPerSpeed * ax / scale, ax * T / width);
	const float added = (float) (f / (2.0 * zBow));
	atBowVelocity = unbowed + added;
	atBowForce = (float) f;
	return added;
}


// ---- the rosin as it is: thermal elasto-plastic friction ---------------------------------------

/** THE SOFTENING AT A TEMPERATURE, y(tau): one at the room's, falling towards `softest` around the
glass transition (their equation 30). */
double BowedString::softening(double tau) const {
	const Rosin& p = rosinSet;
	const double x = std::pow(std::fmax(0.0, tau) / (double) p.glass, (double) p.steepness);
	return (1.0 + (double) p.softest * x) / (1.0 + x);
}


void BowedString::setRosin(const Rosin& r) {
	Rosin p = r;
	p.muStatic = std::fmax(0.01f, r.muStatic);
	p.muSliding = std::fmax(0.f, std::fmin(p.muStatic, r.muSliding));
	p.sigma0 = std::fmax(1.f, r.sigma0);
	p.glass = std::fmax(1e-3f, r.glass);
	p.softest = std::fmax(0.01f, std::fmin(1.f, r.softest));
	p.steepness = std::fmax(0.1f, r.steepness);
	p.breakaway = std::fmax(0.f, std::fmin(0.99f, r.breakaway));
	p.heatCapacity = std::fmax(1e-12f, r.heatCapacity);
	p.hairWidth = std::fmax(1e-4f, r.hairWidth);
	rosinSet = p;
	// THE STRIBECK CURVE FROM THE STEADY HEAT BALANCE. Sliding steadily at speed v under a force F a
	// metre, the contact settles where the heat in, mu_ss(tau) v, equals the heat out,
	// (b sqrt(v / F) + c) tau (their equation 28 with y mu_str = mu_ss); the difference rises with tau,
	// so there is one such tau, found by halving. The sliding coefficient is then mu_ss / y there
	// (equation 32): what the rosin would give at the speed if it were still at the temperature the
	// speed brings it to. Above the steady curve, by the softening.
	auto table = std::make_shared<StribeckTable>();
	const double ms = p.muStatic, md = p.muSliding, ya = p.softest, xi = p.steepness, tg = p.glass;
	auto steady = [&](double tau) {
		const double x = std::pow(tau / tg, xi);
		return (ms + md * ya * x) / (1.0 + x);
	};
	for (int f = 0; f < StribeckTable::FORCES; f++) {
		const double perMetre = std::pow(10.0, table->lowForce
			+ (table->highForce - table->lowForce) * f / (StribeckTable::FORCES - 1));
		for (int v = 0; v < StribeckTable::SPEEDS; v++) {
			const double speed = std::pow(10.0, table->lowSpeed
				+ (table->highSpeed - table->lowSpeed) * v / (StribeckTable::SPEEDS - 1));
			const double out = (double) p.heatToMovingHair * std::sqrt(speed / perMetre) + (double) p.heatAway;
			double lo = 0.0, hi = 1.0;
			while (out * hi < steady(hi) * speed && hi < 1e6)
				hi *= 2.0;
			for (int i = 0; i < 60; i++) {
				const double mid = 0.5 * (lo + hi);
				(out * mid < steady(mid) * speed ? lo : hi) = mid;
			}
			const double tau = 0.5 * (lo + hi);
			const double x = std::pow(tau / tg, xi);
			table->mu[f * StribeckTable::SPEEDS + v] = (float) ((ms + md * ya * x) / (1.0 + ya * x));
		}
	}
	stribeck = table;
	rosined = true;
	thermal = false;
	bristled = false;
	bend = bendRate = hairLag = hairSpeed = 0.0;
	rosinHeat = 0.0;
}


/** The sliding coefficient at a speed and a force a metre of hair, from the table: bilinear in the
logarithms, the ends held. */
double BowedString::stribeckAt(double speed, double perMetre) const {
	const StribeckTable& t = *stribeck;
	const double ls = std::log10(std::fmax(1e-12, speed)), lf = std::log10(std::fmax(1e-12, perMetre));
	const double sv = std::fmin((double) (StribeckTable::SPEEDS - 1), std::fmax(0.0,
		(ls - t.lowSpeed) / (t.highSpeed - t.lowSpeed) * (StribeckTable::SPEEDS - 1)));
	const double sf = std::fmin((double) (StribeckTable::FORCES - 1), std::fmax(0.0,
		(lf - t.lowForce) / (t.highForce - t.lowForce) * (StribeckTable::FORCES - 1)));
	const int v0 = std::min(StribeckTable::SPEEDS - 2, (int) sv), f0 = std::min(StribeckTable::FORCES - 2, (int) sf);
	const double a = sv - v0, b = sf - f0;
	const float* row0 = t.mu + f0 * StribeckTable::SPEEDS;
	const float* row1 = row0 + StribeckTable::SPEEDS;
	return (1.0 - b) * ((1.0 - a) * row0[v0] + a * row0[v0 + 1]) + b * ((1.0 - a) * row1[v0] + a * row1[v0 + 1]);
}


/** THE BOW ON ROSIN THAT SHEARS AND SOFTENS, solved for one sample.

The shear's rate r is the one unknown, as in bristleJunction: a step of backward Euler sets the shear,
z = z' + T r; the friction is the bow force times sigma0 z; the string under the bow moves at the
velocity that friction leaves it, and the hair, a mass on a spring and a damper behind the stick,
gives under the same friction, so the sliding speed v between hair and string is a straight line in r.
What is left is r = dz/dt(v, z) — their equation 9: the shear follows the slide while the rosin only
shears, stops growing once it has reached the steady shear for the speed and the temperature, and in
between some of the contact is slipping — solved by Newton's method kept inside a bracket. The
temperature is the last sample's while the shear is found, and then moves on a sample by backward Euler
with the heat of the sliding that was not shear: the friction times v less r. */
float BowedString::rosinJunction(float unbowed) {
	const Rosin& p = rosinSet;
	const double T = 1.0 / (double) rate;
	const double zt = (double) z * (double) twistRatio;
	const double zBow = (twistRatio > 0.f) ? (double) z * zt / ((double) z + zt) : (double) z;
	if (bowForce <= 0.f) {
		stuck = false;
		slide = 0.f;
		bend = bendRate = hairLag = hairSpeed = 0.0;
		// Off the string the contact only cools.
		rosinHeat *= (double) p.heatCapacity / ((double) p.heatCapacity + T * (double) p.heatAway);
		atBowVelocity = unbowed;
		atBowForce = 0.f;
		return 0.f;
	}
	const double fb = (double) bowForce;
	const double perMetre = fb / (double) p.hairWidth;
	const double s0 = fb * (double) p.sigma0;                  // friction a metre of shear, N/m
	// THE HAIR over the ribbon's width: lag q behind the stick, moving at w, M dw/dt = f - K q - D w.
	// By backward Euler the lag's speed this sample is (M w / T - K q + f) / (M / T + D + K T): the
	// hair under the string slows by that, a second admittance in series with the string's.
	const double M = (double) p.hairMass * p.hairWidth, K = (double) p.hairStiffness * p.hairWidth;
	const double D = (double) p.hairDamping * p.hairWidth;
	const double hairAdmit = 1.0 / (M / T + D + K * T);
	const double hairFree = (M * hairSpeed / T - K * hairLag) * hairAdmit;
	const double u = (double) bowSpeed - (double) unbowed - hairFree;
	const double admit = 1.0 / (2.0 * zBow) + hairAdmit;
	// v = vt + k1 r, z = bend + T r.
	const double vt = u - admit * s0 * bend;
	const double k1 = -admit * s0 * T;
	const double y = softening(rosinHeat);
	const double zBreak = (double) p.breakaway * y * (double) p.muSliding / (double) p.sigma0;
	auto shearRate = [&](double v, double zz) {
		if (v == 0.0)
			return 0.0;
		if ((v > 0.0) != (zz > 0.0))
			return v;
		const double steady = y * stribeckAt(std::fabs(v), perMetre) / (double) p.sigma0;
		const double az = std::fabs(zz);
		double alpha;
		if (az <= zBreak)
			alpha = 0.0;
		else if (az >= steady)
			alpha = 1.0;
		else
			alpha = 0.5 * (1.0 + std::sin(3.14159265358979 * (az - 0.5 * (steady + zBreak)) / (steady - zBreak)));
		return v * (1.0 - alpha * az / steady);
	};
	auto h = [&](double r) { return shearRate(vt + k1 * r, bend + T * r) - r; };

	// THE BRACKET, as bristleJunction's: far out either way shear and slide have opposite signs and h
	// is the slide less r, so it runs from plus infinity to minus infinity as r rises.
	double lo, hi;
	const double h0 = h(0.0);
	if (h0 == 0.0) {
		lo = hi = 0.0;
	}
	else {
		double step = std::fmax(1e-9, std::fabs(vt));
		const double dir = (h0 > 0.0) ? 1.0 : -1.0;
		double far = dir * step;
		for (int i = 0; i < 80 && dir * h(far) > 0.0; i++)
			far *= 2.0;
		lo = std::fmin(0.0, far);
		hi = std::fmax(0.0, far);
	}
	double r = std::fmin(hi, std::fmax(lo, bendRate));
	for (int i = 0; i < 60 && hi - lo > 1e-12; i++) {
		const double hr = h(r);
		if (hr == 0.0)
			break;
		if (hr > 0.0)
			lo = r;
		else
			hi = r;
		const double d = 1e-7 * std::fmax(1e-6, std::fabs(r)) + 1e-12;
		const double slope = (h(r + d) - hr) / d;
		double next = (slope < 0.0) ? r - hr / slope : 0.5 * (lo + hi);
		if (!(next > lo && next < hi))
			next = 0.5 * (lo + hi);
		if (std::fabs(next - r) < 1e-13)
			break;
		r = next;
	}
	const double v = vt + k1 * r;
	bendRate = r;
	bend += T * r;
	const double f = s0 * bend;
	hairSpeed = (M * hairSpeed / T - K * hairLag + f) * hairAdmit;
	hairLag += T * hairSpeed;
	// THE HEAT: the friction's work on the part of the sliding that is not shear, per newton of bow
	// force, into the contact; out through the moving hair and steadily (their equation 22 over F).
	const double sliding = v - r;
	const double in = std::fmax(0.0, (double) p.sigma0 * bend * sliding);
	const double out = (double) p.heatToMovingHair * std::sqrt(std::fabs(v) / perMetre) + (double) p.heatAway;
	rosinHeat = ((double) p.heatCapacity * rosinHeat / T + in) / ((double) p.heatCapacity / T + out);
	// STUCK, for a bench: sliding less than a hundredth of the bow's speed. Benches under this model
	// count slips from the motion (heating), so this is only a hint.
	stuck = std::fabs(sliding) <= 0.01 * std::fabs((double) bowSpeed) + 1e-5;
	slide = (float) v;
	const float added = (float) (f / (2.0 * zBow));
	atBowVelocity = unbowed + added;
	atBowForce = (float) f;
	return added;
}


void BowedString::setContact(float b) {
	b = std::fmax(0.005f, std::fmin(0.5f, b));
	if (b != beta) {
		beta = b;
		dirty = true;
	}
}


/** A FIRST-ORDER ALLPASS'S DELAY at a frequency, in samples. GuitarString's. */
static float allpassDelay(float c, float w) {
	const float num = std::atan2(-std::sin(w), c + std::cos(w));
	const float den = std::atan2(-c * std::sin(w), 1.f + c * std::cos(w));
	return -(num - den) / w;
}


/** Four-point Lagrange weights, for a whole part one below the floor so the fraction lies between
one and two, where the interpolator is most accurate. GuitarString's. */
static void lagrange(float f, float h[4]) {
	h[0] = -(f - 1.f) * (f - 2.f) * (f - 3.f) / 6.f;
	h[1] = f * (f - 2.f) * (f - 3.f) / 2.f;
	h[2] = -f * (f - 1.f) * (f - 3.f) / 2.f;
	h[3] = f * (f - 1.f) * (f - 2.f) / 6.f;
}


/** What such a read does to a sine at `w`: its delay in samples and its gain. */
static void lagrangeResponse(float d, float w, float* delay, float* magnitude) {
	const int base = (int) std::floor(d) - 1;
	float h[4];
	lagrange(d - (float) base, h);
	float re = 0.f, im = 0.f;
	for (int j = 0; j < 4; j++) {
		re += h[j] * std::cos(w * (float) (base + j));
		im -= h[j] * std::sin(w * (float) (base + j));
	}
	*delay = -std::atan2(im, re) / w;
	if (*delay < 0.f)
		*delay += 2.f * PI / w;
	*magnitude = std::sqrt(re * re + im * im);
}


/** THE LOOP, WORKED OUT FOR THE NOTE AND THE CONTACT POINT.

The round trip is twice each segment, because every one is crossed once going and once coming
back, plus whatever the two reflections' filters delay a sine at the fundamental. So the sum of the
two segments is half of what GuitarString solves for a single line, and the only new thing is
dividing it by the contact point.

DIVIDED SO THE BOW IS WHERE IT WAS ASKED TO BE: the bridge side's round trip, its two reads and the
bridge's reflection, is beta of the period, and the nut side, with the loss filter and the
stiffness allpasses in its reflection, takes the rest.

SOLVED, NOT ASSUMED. Four interpolated reads each put their own small phase error into the loop,
and the four are not equal because the segments are not. The total is measured at the fundamental
and the error shared back between the segments in proportion, twice over, which is what keeps the
string in tune wherever the bow is standing.

WHERE IT RUNS OUT. A contact point near the bridge wants a segment of a sample or two, and the
read cannot be done in less than about two. Rather than detune the string the short segment is
held at the floor and the long one takes the rest: the pitch stays right, the contact point is not
where it was asked for, and `contactClamped` says so. */
void BowedString::tune() {
	dirty = false;
	const float period = rate / freq;
	const float w = 2.f * PI * freq / rate;

	// THE STRING'S OWN LOSS, A ONE-POLE LOWPASS AT EACH END, as GuitarString's loss filter — SET
	// PER HERTZ. `damping` names a Q for a partial at two kilohertz, internal friction makes a
	// partial's decay rate go as its frequency squared, and a lowpass of coefficient s takes away
	// about s / (1 - s)^2 times w^2 / 2 nepers a pass. So k = s / (1 - s)^2 is what the whole round
	// trip needs, and it is shared by length: beta of it at the bridge, the rest at the nut.
	float lossDelay = 0.f, lossDelayB = 0.f, lossGain = 1.f;
	{
		const float q2k = 1000.f * std::pow(0.02f, damping);
		const float b = PI / (q2k * 2000.f);
		const float k = lossless ? 0.f : b * rate * rate / (2.f * PI * PI * freq);
		auto coefficient = [](float k) {
			const float s = (k > 1e-6f) ? ((2.f * k + 1.f) - std::sqrt(4.f * k + 1.f)) / (2.f * k) : 0.f;
			return std::fmax(0.f, std::fmin(0.9995f, s));
		};
		lossS = coefficient((1.f - beta) * k);
		lossSB = coefficient(beta * k);
		auto response = [&](float s, float* delay) {
			const float re = 1.f - s * std::cos(w), im = s * std::sin(w);
			*delay = std::atan2(im, re) / w;
			return (1.f - s) / std::sqrt(re * re + im * im);
		};
		lossGain = response(lossS, &lossDelay) * response(lossSB, &lossDelayB);
	}

	// Stiffness, worked out for the note so the eighth partial sits the same number of cents
	// sharp whatever is played, as in GuitarString. Only when the pitch has moved a quarter tone.
	if (stiffness != dispersionStiff
			|| std::fabs(std::log2(freq / std::fmax(dispersionFor, 1.f))) > 1.f / 24.f) {
		dispersionStiff = stiffness;
		dispersionFor = freq;
		const float target = 6.f * stiffness;
		auto centsAt = [&](float a) {
			const float rest = period - (float) DISPERSION_STAGES * allpassDelay(a, w);
			float w8 = 8.f * w;
			for (int i = 0; i < 8; i++)
				w8 = 2.f * PI * 8.f / (rest + (float) DISPERSION_STAGES * allpassDelay(a, w8));
			return 1200.f * std::log2(w8 / (8.f * w));
		};
		float lo = -0.95f, hi = 0.f;
		if (target <= 0.f)
			lo = 0.f;
		for (int i = 0; i < 16 && target > 0.f; i++) {
			const float mid = 0.5f * (lo + hi);
			if (centsAt(mid) > target)
				lo = mid;
			else
				hi = mid;
		}
		dispersionA = 0.5f * (lo + hi);
		while (dispersionA < 0.f && (float) DISPERSION_STAGES * allpassDelay(dispersionA, w)
				> period * 0.5f)
			dispersionA *= 0.9f;
	}
	const float dispersionDelay = (float) DISPERSION_STAGES * allpassDelay(dispersionA, w);

	// EACH END PASSES A STEADY VELOCITY BACK EXACTLY. A reflection that lost a little of it let the
	// mean of the travelling waves drift — the whole string moving sideways — which under a bow
	// is not harmless: the friction's own steady part never balanced, and the string spent the
	// wrong share of each period stuck. So the loss that sets the decay is taken only at the
	// note's own frequencies: what reflects is `gain` of the wave plus the rest of it through a
	// lowpass an octave and a half below the note, which is the whole of it at nought hertz and
	// almost none of it at the fundamental. McIntyre, Schumacher and Woodhouse's condition, that
	// each reflection's area be exactly minus one.
	lowPole = std::exp(-2.f * PI * (freq / 8.f) / rate);
	const float cDen = (1.f - lowPole * std::cos(w)) * (1.f - lowPole * std::cos(w))
		+ (lowPole * std::sin(w)) * (lowPole * std::sin(w));
	const float cRe = (1.f - lowPole) * (1.f - lowPole * std::cos(w)) / cDen;
	const float cIm = -(1.f - lowPole) * lowPole * std::sin(w) / cDen;
	// What a reflection of `g` does to the fundamental: its gain and its delay in samples.
	auto through = [&](float g, float* delay) {
		const float re = g + (1.f - g) * cRe, im = (1.f - g) * cIm;
		if (delay)
			*delay = -std::atan2(im, re) / w;
		return std::sqrt(re * re + im * im);
	};
	// And the `g` that gives the fundamental a gain of `target`: a quadratic in g.
	auto solveFor = [&](float target) {
		const float a = 1.f - cRe;
		const float qa = a * a + cIm * cIm;
		const float qb = 2.f * (a * cRe - cIm * cIm);
		const float qc = cRe * cRe + cIm * cIm - target * target;
		const float disc = std::fmax(0.f, qb * qb - 4.f * qa * qc);
		return std::fmax(0.f, std::fmin(0.99999f, (-qb + std::sqrt(disc)) / (2.f * qa)));
	};
	nutReflect = lossless ? 1.f : (stopped ? FINGER_REFLECT : NUT_REFLECT);
	float nutDelay = 0.f;
	const float nutGain = through(nutReflect, &nutDelay);
	const float perPass = std::pow(0.001f, 1.f / (decay * freq));
	float bridgeDelay = 0.f;
	through(lossless ? 1.f : solveFor(perPass / std::fmax(lossGain * nutGain, 1e-6f)), &bridgeDelay);

	// What the four reads have to give between them, and how it divides.
	const float want = period - lossDelay - lossDelayB - dispersionDelay - nutDelay - bridgeDelay;
	float half = std::fmax(2.f * MIN_SEGMENT, want) * 0.5f;
	float db = std::fmin(0.5f * (beta * period - lossDelayB - bridgeDelay), half - MIN_SEGMENT);
	float dn = half - db;
	clamped = false;
	if (db < MIN_SEGMENT) {
		db = MIN_SEGMENT;
		dn = half - db;
		clamped = true;
	} else if (dn < MIN_SEGMENT) {
		dn = MIN_SEGMENT;
		db = half - dn;
		clamped = true;
	}
	const float shareB = db / (db + dn);

	// Two passes over the whole loop, the error shared between the segments as their lengths are.
	float readGain = 1.f;
	for (int pass = 0; pass < 2; pass++) {
		float gotB = db, gotN = dn, magB = 1.f, magN = 1.f;
		lagrangeResponse(std::fmax(MIN_SEGMENT, db), w, &gotB, &magB);
		lagrangeResponse(std::fmax(MIN_SEGMENT, dn), w, &gotN, &magN);
		readGain = magB * magB * magN * magN;
		const float err = want - 2.f * (gotB + gotN);
		db += shareB * err * 0.5f;
		dn += (1.f - shareB) * err * 0.5f;
		if (db < MIN_SEGMENT) { dn += db - MIN_SEGMENT; db = MIN_SEGMENT; clamped = true; }
		if (dn < MIN_SEGMENT) { db += dn - MIN_SEGMENT; dn = MIN_SEGMENT; clamped = true; }
	}
	const float room = (float) MAX_DELAY - 4.f;
	toBridgeDelay = std::fmax(MIN_SEGMENT, std::fmin(db, room));
	toNutDelay = std::fmax(MIN_SEGMENT, std::fmin(dn, room));
	shortest = std::fmin(toBridgeDelay, toNutDelay);

	// Sixty decibels in `decay` seconds is a thousandth over decay x freq round trips. A round
	// trip meets each end once, not twice: bow to bridge, back to bow, on to the nut, back
	// again. Counting the nut's reflection for each of the two segments it borders left a
	// string asked for six seconds ringing for nearly nine.
	gain = lossless ? 1.f : solveFor(perPass / std::fmax(lossGain * readGain * nutGain, 1e-6f));

	// THE TWIST'S LOOP, the same division at speedRatio times the speed: its period is the note's
	// over speedRatio, and the bow is beta of the way along it as along the other. Its loss is
	// shared between its two ends, Q cycles of its own to fall by e^-pi, and each end passes a
	// steady turning back exactly, as the transverse ends pass a steady velocity: the string's ends
	// are held, so a steady torque twists it and does not set it spinning.
	{
		const float tPeriod = period / twistSpeed;
		const float tHalf = std::fmax(2.f * MIN_SEGMENT, tPeriod) * 0.5f;
		float tb = std::fmax(MIN_SEGMENT, beta * tHalf);
		float tn = tHalf - tb;
		if (tn < MIN_SEGMENT) {
			tn = MIN_SEGMENT;
			tb = std::fmax(MIN_SEGMENT, tHalf - tn);
		}
		const float room = (float) TORSION_DELAY - 4.f;
		tToBridgeDelay = std::fmin(tb, room);
		tToNutDelay = std::fmin(tn, room);
		// EVERY PARTIAL OF THE TWIST AT THE SAME Q, as the measured reflection functions have it:
		// the n-th loses e^(-pi n / Q) a round trip, so the tenth about half. A one-pole lowpass
		// matched there takes the high ones, shared between the ends by length as the transverse
		// loss is, and a flat gain makes up the fundamental's own.
		const float wt = 2.f * PI * freq * twistSpeed / rate;
		const float wm = std::fmin(10.f * wt, 2.5f);
		const float target = lossless ? 1.f : std::exp(-PI * (wm / wt) / twistQ);
		// The coefficient whose lowpass gives `target` at wm: (1 - s)^2 = target^2 (1 - 2 s cos wm + s^2).
		const float t2 = target * target, c = std::cos(wm);
		const float qa = 1.f - t2, qb = -2.f + 2.f * t2 * c, qc = 1.f - t2;
		const float sAll = (qa > 1e-6f) ? (-qb - std::sqrt(std::fmax(0.f, qb * qb - 4.f * qa * qc))) / (2.f * qa) : 0.f;
		const float kAll = (sAll > 0.f) ? sAll / ((1.f - sAll) * (1.f - sAll)) : 0.f;
		auto coefficient = [](float k) {
			const float s = (k > 1e-6f) ? ((2.f * k + 1.f) - std::sqrt(4.f * k + 1.f)) / (2.f * k) : 0.f;
			return std::fmax(0.f, std::fmin(0.9995f, s));
		};
		tLossB = coefficient(beta * kAll);
		tLossN = coefficient((1.f - beta) * kAll);
		auto magnitude = [&](float s) {
			const float re = 1.f - s * std::cos(wt), im = s * std::sin(wt);
			return (1.f - s) / std::sqrt(re * re + im * im);
		};
		const float atFundamental = magnitude(tLossB) * magnitude(tLossN);
		tGain = lossless ? 1.f : std::sqrt(std::fmin(1.f, std::exp(-PI / twistQ) / atFundamental));
		tPole = std::exp(-2.f * PI * (freq * twistSpeed / 8.f) / rate);
	}
}


float BowedString::random() {
	noise ^= noise << 13;
	noise ^= noise >> 17;
	noise ^= noise << 5;
	return (float) noise / 2147483648.f - 1.f;
}


float BowedString::readTwist(const float* line, float d) const {
	const int base = (int) std::floor(d) - 1;
	float h[4];
	lagrange(d - (float) base, h);
	float out = 0.f;
	for (int j = 0; j < 4; j++) {
		int at = tWrite - base - j;
		while (at < 0)
			at += TORSION_DELAY;
		out += h[j] * line[at];
	}
	return out;
}


float BowedString::read(const float* line, float d) const {
	const int base = (int) std::floor(d) - 1;
	float h[4];
	lagrange(d - (float) base, h);
	float out = 0.f;
	for (int j = 0; j < 4; j++) {
		int at = write - base - j;
		while (at < 0)
			at += MAX_DELAY;
		out += h[j] * line[at];
	}
	return out;
}


/** PIZZICATO, which is the only way to set the string going until there is a bow — and wanted in
its own right, so it is built here rather than added later. GuitarString's triangle, rounded by
the finger and started where it crosses nought for the same reasons; half of it goes into each
outgoing wave, because a string let go at a point sends the same shape both ways. */
void BowedString::pluck(float level, float hardness) {
	if (dirty)
		tune();
	const float period = rate / freq;
	const int n = std::max(2, std::min((int) std::lround(period), (int) MAX_DELAY));
	hardness = std::fmax(0.f, std::fmin(1.5f, hardness));

	float shape[MAX_DELAY];
	for (int i = 0; i < n; i++) {
		const float x = (float) i / (float) n;
		shape[i] = (x < beta) ? x / beta : (1.f - x) / (1.f - beta);
	}
	const float a = std::fmin(1.f, 0.02f * std::pow(47.5f, hardness));
	float lp = shape[n - 1];
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < n; i++) {
			lp += a * (shape[i] - lp);
			if (pass == 1)
				excite[i] = lp;
		}
	const float click = 0.04f * hardness;
	float mean = 0.f;
	for (int i = 0; i < n; i++) {
		excite[i] += click * random();
		mean += excite[i];
	}
	mean /= (float) n;
	float peak = 1e-6f;
	for (int i = 0; i < n; i++) {
		excite[i] -= mean;
		peak = std::fmax(peak, std::fabs(excite[i]));
	}
	int start = 0;
	float best = 1e9f;
	for (int i = 0; i < n; i++) {
		const float x = excite[i], y = excite[(i + 1) % n];
		if (x < 0.f && y >= 0.f && std::fabs(y) < best) {
			best = std::fabs(y);
			start = (i + 1) % n;
		}
	}
	if (start != 0) {
		float turned[MAX_DELAY];
		for (int i = 0; i < n; i++)
			turned[i] = excite[(start + i) % n];
		for (int i = 0; i < n; i++)
			excite[i] = turned[i];
	}
	const float scale = 0.5f * std::fmax(0.f, std::fmin(1.f, level)) / peak;
	for (int i = 0; i < n; i++)
		excite[i] *= scale;
	exciteLength = n;
	excitePos = 0;
}


/** THE BOW, SOLVED FOR ONE SAMPLE.

`unbowed` is the velocity the string would have under the bow if nothing touched it: the two waves
arriving there, added. The bow's friction force f adds f / 2Z to both outgoing waves, so the string
under the bow moves at

    v = unbowed + f / 2Z,    and the friction is    f = F x mu(bow speed - v),

where F is how hard the bow presses and mu, the coefficient, depends on how fast the string slides
against the hair. Writing x for that sliding speed, bow minus string, and k for F / 2Z, the two
together are one equation in x:

    x + k mu(x) = bow speed - unbowed.

STUCK, x is nought and mu can be anything from minus to plus the static coefficient: the string
moves with the bow, held there by whatever friction it takes, and it can stay so as long as that
is no more than the static coefficient allows.

SLIPPING, mu is the hyperbolic curve, mu_d + (mu_s - mu_d) v0 / (v0 + |x|), signed with the slide.
For a slide in the positive direction that makes the equation a quadratic,

    x^2 + (v0 + k mu_d - u) x + v0 (k mu_s - u) = 0,    with u = bow speed - unbowed,

and the same with signs turned for the other way.

THERE CAN BE THREE ANSWERS AT ONCE: stuck, and two slipping speeds. The middle one is unstable; of
the other two, the string keeps to the one it is on. Stuck, it stays stuck until the friction it
needs would be more than the static coefficient gives; slipping, it stays on the outer slipping
answer — the faster one — until that answer stops existing, and only then sticks. That memory is
the hysteresis of the classical model, and it is what makes the slip begin late and end abruptly,
which is the Helmholtz corner. */
float BowedString::bowJunction(float unbowed) {
	if (bowForce <= 0.f) {
		stuck = false;
		slide = 0.f;
		atBowVelocity = unbowed;
		atBowForce = 0.f;
		return 0.f;
	}
	// THE STRING'S SURFACE answers a force through both of its motions at once, so it gives way
	// as the two impedances in parallel would: Z Zt / (Z + Zt), Zt the torsional one.
	const double zt = (double) z * (double) twistRatio;
	const double zBow = (twistRatio > 0.f) ? (double) z * zt / ((double) z + zt) : (double) z;
	const double k = (double) bowForce / (2.0 * zBow);
	const double u = (double) bowSpeed - (double) unbowed;
	const double holdLimit = k * (double) muStatic;

	// The faster of the slipping answers in direction `s`, or a negative number where there is
	// none. Worked in double: the two terms nearly cancel close to where the answer vanishes.
	auto outer = [&](double s) {
		const double su = s * u;
		const double b = (double) fallSpeed + k * (double) muDynamic - su;
		const double c = (double) fallSpeed * (k * (double) muStatic - su);
		const double disc = b * b - 4.0 * c;
		if (disc < 0.0)
			return -1.0;
		const double y = 0.5 * (-b + std::sqrt(disc));
		return (y > 0.0) ? y : -1.0;
	};

	double x = 0.0;
	if (stuck && std::fabs(u) <= holdLimit) {
		x = 0.0;
	}
	else {
		// Slipping, or breaking away: the way it was slipping, or the way it is being pulled.
		double s = (!stuck && slide != 0.f) ? (slide > 0.f ? 1.0 : -1.0) : (u >= 0.0 ? 1.0 : -1.0);
		double y = outer(s);
		if (y < 0.0) {
			// THE SLIP HAS RUN OUT. Stick if the bow can hold it; otherwise it is slipping the
			// other way, which a big enough reversal of the pull can do.
			if (std::fabs(u) <= holdLimit) {
				stuck = true;
				x = 0.0;
			}
			else {
				s = -s;
				y = outer(s);
				x = (y > 0.0) ? s * y : 0.0;
				stuck = (y <= 0.0);
			}
		}
		else {
			x = s * y;
			stuck = false;
		}
	}
	slide = (float) x;
	const float v = bowSpeed - (float) x;
	const float added = v - unbowed;
	atBowVelocity = v;
	atBowForce = (float) (2.0 * zBow * (double) added);
	return added;
}


/** THE BRISTLES' STEADY BEND at a sliding speed: the bend at which, sliding steadily, all of them
are broken and the friction is the curve's, F mu(v) / s0, signed with the speed. */
double BowedString::steadyBend(double v) const {
	const double av = std::fabs(v);
	const double drop = bristles.gaussian
		? std::exp(-(av / bristles.fall) * (av / bristles.fall))
		: std::exp(-av / bristles.fall);
	const double mu = bristles.muCoulomb + (bristles.muStatic - bristles.muCoulomb) * drop;
	return (v >= 0.0 ? 1.0 : -1.0) * (double) bowForce * mu / bristles.stiffness;
}


/** HOW FAST THE BEND CHANGES, dz/dt = v (1 - alpha z / z_ss): with the speed while the bristles only
bend, not at all once all are broken and the bend is steady, and smoothly between, alpha going from
nought to one over a half sine as the bend goes from the breakaway bend to the steady one (Dupont and
colleagues' adhesion map). A bend against the slide is always elastic. `broken` says whether all
of them are. */
double BowedString::bendSpeed(double v, double z, bool* broken) const {
	*broken = false;
	if (v == 0.0)
		return 0.0;
	if ((v > 0.0) != (z > 0.0))
		return v;
	const double steady = std::fabs(steadyBend(v));
	const double zBreak = (double) bristles.breakaway * (double) bowForce * bristles.muCoulomb
		/ bristles.stiffness;
	const double az = std::fabs(z);
	double alpha;
	if (az <= zBreak)
		alpha = 0.0;
	else if (az >= steady) {
		alpha = 1.0;
		*broken = true;
	}
	else
		alpha = 0.5 * (1.0 + std::sin(3.14159265358979 * (az - 0.5 * (steady + zBreak)) / (steady - zBreak)));
	return v * (1.0 - alpha * az / steady);
}


/** THE BOW AS BRISTLES, solved for one sample.

The bend's rate r is the one unknown. A step of backward Euler sets the bend, z = z' + T r, and the
string under the bow then moves at the velocity the friction s0 z + s1 r leaves it, so the sliding
speed is v = u - (s0 z + s1 r) / 2Z, u the bow's speed less the unbowed string's: both are straight
lines in r. What is left is r = dz/dt(v, z), one equation, solved by Newton's method kept inside a
bracket so it cannot run away. Backward and not trapezoidal: the bristles are stiff enough to settle
in a few microseconds, a fraction of a sample, and the trapezoidal step answers a mode that fast
with a ringing at half the sample rate where backward Euler lets it settle, which is all it does.
The bristles' own microsecond motion is not heard; their bend is. */
float BowedString::bristleJunction(float unbowed) {
	const double zt = (double) z * (double) twistRatio;
	const double zBow = (twistRatio > 0.f) ? (double) z * zt / ((double) z + zt) : (double) z;
	if (bowForce <= 0.f) {
		stuck = false;
		slide = 0.f;
		bend = bendRate = hairLag = 0.0;
		atBowVelocity = unbowed;
		atBowForce = 0.f;
		return 0.f;
	}
	const double T = 1.0 / (double) rate;
	const double s0 = bristles.stiffness, s1 = bristles.damping;
	// THE HAIR, if it gives: its lag z_h moves at (f - K z_h) / D, so by backward Euler the contact
	// slows by (f - K z_h') / (D + K T) this sample, a second admittance in series with the string's.
	const double K = bristles.hairStiffness, D = bristles.hairDamping;
	const bool hair = K > 0.0 && D > 0.0;
	const double hairAdmit = hair ? 1.0 / (D + K * T) : 0.0;
	const double u = (double) bowSpeed - (double) unbowed + (hair ? K * hairLag * hairAdmit : 0.0);
	const double admit = 1.0 / (2.0 * zBow) + hairAdmit;
	// v = vt + k1 r, z = bend + T r.
	const double vt = u - admit * s0 * bend;
	const double k1 = -admit * (s0 * T + s1);
	bool broken = false;
	auto h = [&](double r) { return bendSpeed(vt + k1 * r, bend + T * r, &broken) - r; };

	// THE BRACKET. h goes from plus infinity to minus infinity as r rises: far out either way the
	// bend and the slide have opposite signs, the bristles only bend, and h is the slide less r.
	double lo, hi;
	const double h0 = h(0.0);
	if (h0 == 0.0) {
		lo = hi = 0.0;
	}
	else {
		double step = std::fmax(1e-9, std::fabs(vt));
		const double dir = (h0 > 0.0) ? 1.0 : -1.0;
		double far = dir * step;
		for (int i = 0; i < 80 && dir * h(far) > 0.0; i++)
			far *= 2.0;
		lo = std::fmin(0.0, far);
		hi = std::fmax(0.0, far);
	}
	// NEWTON INSIDE IT, from last sample's rate, falling back on halving where a step leaves it.
	double r = std::fmin(hi, std::fmax(lo, bendRate));
	for (int i = 0; i < 60 && hi - lo > 1e-12; i++) {
		const double hr = h(r);
		if (hr == 0.0)
			break;
		if (hr > 0.0)
			lo = r;
		else
			hi = r;
		const double d = 1e-7 * std::fmax(1e-6, std::fabs(r)) + 1e-12;
		const double slope = (h(r + d) - hr) / d;
		double next = (slope < 0.0) ? r - hr / slope : 0.5 * (lo + hi);
		if (!(next > lo && next < hi))
			next = 0.5 * (lo + hi);
		if (std::fabs(next - r) < 1e-13)
			break;
		r = next;
	}
	const double v = vt + k1 * r;
	bendSpeed(v, bend + T * r, &broken);
	bendRate = r;
	bend += T * r;
	const double f = s0 * bend + s1 * r;
	if (hair)
		hairLag += T * (f - K * hairLag) * hairAdmit;
	// STUCK AND SLIPPING, for a bench to count. Not whether the bristles are broken: before it
	// flies back, a string under a heavy bow creeps, a few centimetres a second behind the bow with
	// every bristle broken and the friction still at its static value. Slipping is sliding fast
	// enough that the friction has fallen a tenth of the way to its sliding value.
	{
		const double av = std::fabs(v) / bristles.fall;
		const double drop = bristles.gaussian ? std::exp(-av * av) : std::exp(-av);
		stuck = drop > 0.9;
	}
	slide = (float) v;
	const float added = (float) (f / (2.0 * zBow));
	atBowVelocity = unbowed + added;
	atBowForce = (float) f;
	return added;
}


void BowedString::silence() {
	stuck = false;
	bend = bendRate = hairLag = hairSpeed = 0.0;
	rosinHeat = 0.0;
	for (double& h : heat)
		h = 0.0;
	heatIdle = 0;
	slide = 0.f;
	atBowVelocity = atBowForce = 0.f;
	for (int i = 0; i < MAX_DELAY; i++)
		bowToBridge[i] = bridgeToBow[i] = bowToNut[i] = nutToBow[i] = 0.f;
	lastOut = lastOutB = atBridge = 0.f;
	for (int i = 0; i < DISPERSION_STAGES; i++)
		dIn[i] = dOut[i] = 0.f;
	dcIn = dcOut = 0.f;
	bridgeLow = nutLow = 0.f;
	for (int i = 0; i < TORSION_DELAY; i++)
		tBowToBridge[i] = tBridgeToBow[i] = tBowToNut[i] = tNutToBow[i] = 0.f;
	tLowB = tLowN = tLastB = tLastN = 0.f;
	exciteLength = excitePos = 0;
}


/** ONE SAMPLE, ROUND THE WHOLE STRING.

Read what has arrived at each of the three places something happens, then write what leaves them.
Reading everything before writing anything is what makes the four lines one loop rather than four,
and is why a segment may be as short as the interpolator allows without the order of the writes
mattering.

THE BOW JUNCTION. What arrives from one side leaves towards the other, as it would with nothing
touching the string, plus whatever velocity the bow's friction adds, the same to both. See
bowJunction. */
float BowedString::process() {
	pushOnStillBridge();
	return finishWithBridge(0.f);
}


float BowedString::pushOnStillBridge() {
	if (dirty)
		tune();
	pArrivingBridge = read(bowToBridge, toBridgeDelay);
	pArrivingNut = read(bowToNut, toNutDelay);
	pFromBridge = read(bridgeToBow, toBridgeDelay);
	pFromNut = read(nutToBow, toNutDelay);
	// THE BRIDGE, STANDING STILL: the bridge side's share of the string's loss, the round trip's
	// gain, the same at every frequency, and the sign change a reflection from a near-rigid end
	// makes. A bridge that moves adds its own velocity to what goes back down; see the second half.
	const float lossyB = (1.f - lossSB) * pArrivingBridge + lossSB * lastOutB;
	lastOutB = lossyB;
	bridgeLow += (1.f - lowPole) * (lossyB - bridgeLow);
	pReflect = -(gain * lossyB + (1.f - gain) * bridgeLow);
	return z * (pArrivingBridge - pReflect);
}


float BowedString::finishWithBridge(float bridgeVelocity) {
	const float arrivingAtBridge = pArrivingBridge;
	const float arrivingAtNut = pArrivingNut;
	const float fromBridge = pFromBridge;
	const float fromNut = pFromNut;
	atBridge = arrivingAtBridge;
	// The string's end moves with the bridge: what goes back down is the reflection plus the
	// bridge's own velocity, and the string pushes on the bridge the less for it.
	bridgeToBow[write] = pReflect + bridgeVelocity;
	atBridgeForce = z * (arrivingAtBridge - bridgeToBow[write]);

	// THE NUT: the rest of the string's loss, the stiffness allpasses, and the reflection.
	const float lossy = (1.f - lossS) * arrivingAtNut + lossS * lastOut;
	lastOut = lossy;
	float stiff = lossy;
	for (int i = 0; i < DISPERSION_STAGES; i++) {
		const float y = dispersionA * stiff + dIn[i] - dispersionA * dOut[i];
		dIn[i] = stiff;
		dOut[i] = y;
		stiff = y;
	}
	nutLow += (1.f - lowPole) * (stiff - nutLow);
	nutToBow[write] = -(nutReflect * stiff + (1.f - nutReflect) * nutLow);

	// THE TWIST, read where it arrives, and reflected at both ends with nothing but its loss.
	const bool twisting = twistRatio > 0.f;
	float tFromBridge = 0.f, tFromNut = 0.f;
	if (twisting) {
		const float tAtBridge = readTwist(tBowToBridge, tToBridgeDelay);
		const float tAtNut = readTwist(tBowToNut, tToNutDelay);
		tFromBridge = readTwist(tBridgeToBow, tToBridgeDelay);
		tFromNut = readTwist(tNutToBow, tToNutDelay);
		tLastB = (1.f - tLossB) * tAtBridge + tLossB * tLastB;
		tLowB += (1.f - tPole) * (tLastB - tLowB);
		tBridgeToBow[tWrite] = -(tGain * tLastB + (1.f - tGain) * tLowB);
		tLastN = (1.f - tLossN) * tAtNut + tLossN * tLastN;
		tLowN += (1.f - tPole) * (tLastN - tLowN);
		tNutToBow[tWrite] = -(tGain * tLastN + (1.f - tGain) * tLowN);
	}

	// THE BOW: straight through, plus what the friction adds, plus the pluck while one is going in.
	// What it adds to the surface's velocity divides between the two motions as their admittances
	// do: the sideways share Zt / (Z + Zt), the twist's the rest.
	float push = 0.f;
	if (excitePos < exciteLength)
		push = excite[excitePos++];
	const float unbowed = fromBridge + fromNut + tFromBridge + tFromNut;
	const float added = bristled ? bristleJunction(unbowed)
		: (rosined ? rosinJunction(unbowed) : (thermal ? thermalJunction(unbowed) : bowJunction(unbowed)));
	const float sideways = twisting ? twistRatio / (1.f + twistRatio) : 1.f;
	push += added * sideways;
	bowToBridge[write] = fromNut + push;
	bowToNut[write] = fromBridge + push;
	if (twisting) {
		const float turn = added * (1.f - sideways);
		tBowToBridge[tWrite] = tFromNut + turn;
		tBowToNut[tWrite] = tFromBridge + turn;
		if (++tWrite >= TORSION_DELAY)
			tWrite = 0;
	}

	if (++write >= MAX_DELAY)
		write = 0;

	// A DC blocker, so nothing the finger's noise left in the loop reaches the output.
	const float y = atBridge - dcIn + 0.995f * dcOut;
	dcIn = atBridge;
	dcOut = y;
	return y;
}


} // namespace px
