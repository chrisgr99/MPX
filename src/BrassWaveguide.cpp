/** See BrassWaveguide.hpp. */
#include "BrassWaveguide.hpp"

#include <cmath>

namespace px {


/** How much of each inner tube the outer tube beside it keeps, so that it is never too short to read. */
static const double OUTER_KEEPS = 0.02;


void BrassDelay::setup(int size) {
	int n = 4;
	while (n < size + 4)
		n *= 2;
	line.assign((size_t) n, 0.0);
	mask = n - 1;
	at = 0;
}


double BrassDelay::read(double delay) const {
	// Taps at whole delays N - 1 to N + 2 around the wanted one, N + f, as Lagrange's cubic through them
	// evaluated one and f along.
	const int whole = (int) std::floor(delay);
	const double t = 1.0 + (delay - whole);
	const double h0 = -(t - 1.0) * (t - 2.0) * (t - 3.0) / 6.0;
	const double h1 = t * (t - 2.0) * (t - 3.0) / 2.0;
	const double h2 = -t * (t - 1.0) * (t - 3.0) / 2.0;
	const double h3 = t * (t - 1.0) * (t - 2.0) / 6.0;
	auto tap = [&](int d) { return line[(size_t) ((at - d) & mask)]; };
	return h0 * tap(whole - 1) + h1 * tap(whole) + h2 * tap(whole + 1) + h3 * tap(whole + 2);
}


void BrassLossShelves::design(double beta, const std::vector<double>& xi, const std::vector<double>& mu, double rate) {
	// exp(-beta s^(1/2)) as the product over m of exp(-beta mu_m s / (s + xi_m)), each by its Padé
	// approximant ((1 - a/2) s + xi) / ((1 + a/2) s + xi) with a = beta mu_m, split into as many equal
	// shelves as keep each a under a half, where the approximant is good; then the bilinear transform.
	std::vector<Shelf> kept;
	for (size_t m = 0; m < xi.size(); m++) {
		const double a = beta * mu[m];
		if (!(a > 0.0))
			continue;
		const int pieces = std::max(1, (int) std::ceil(a / 0.5));
		const double each = a / pieces;
		const double k2 = 2.0 * rate;
		const double n0 = (1.0 - 0.5 * each) * k2 + xi[m], n1 = xi[m] - (1.0 - 0.5 * each) * k2;
		const double d0 = (1.0 + 0.5 * each) * k2 + xi[m], d1 = xi[m] - (1.0 + 0.5 * each) * k2;
		for (int i = 0; i < pieces; i++) {
			Shelf s;
			s.b0 = n0 / d0;
			s.b1 = n1 / d0;
			s.a1 = d1 / d0;
			// Keep what the shelves at this place held, so that a redesign while sounding does not jump.
			const size_t at = kept.size();
			if (at < shelves.size()) {
				s.x1 = shelves[at].x1;
				s.y1 = shelves[at].y1;
			}
			kept.push_back(s);
		}
	}
	shelves.swap(kept);
}


double BrassLossShelves::process(double x) {
	for (Shelf& s : shelves) {
		const double y = s.b0 * x + s.b1 * s.x1 - s.a1 * s.y1;
		s.x1 = x;
		s.y1 = y;
		x = y;
	}
	return x;
}


void BrassWaveguide::setup(const Setup& s) {
	air = s.air;
	rate = s.rate;
	const BrassBore bore = BrassBore::tromboneTimed(0.0, s.tuningPull);
	// THE PARTS: the front up to the slide (the backbore and the leadpipe, the first two sections), the
	// tubes of one radius from there to the tuning slide, and the rest — the tuning slide and the bell.
	size_t first = 2, last = first;
	while (last < bore.sections.size() && bore.sections[last].r0 == bore.sections[last].r1)
		last++;
	BrassGrid::Setup g;
	g.air = air;
	g.rate = rate;
	g.lossTerms = s.lossTerms;
	g.bore = bore;
	g.bore.sections.assign(bore.sections.begin(), bore.sections.begin() + (long) first);
	g.endIsPort = true;
	front.setup(g);
	g.bore = bore;
	g.bore.sections.assign(bore.sections.begin() + (long) last, bore.sections.end());
	g.endIsPort = false;
	g.startIsPort = true;
	back.setup(g);
	// THE RUN OF TUBES, with the outer slide tubes, absent from the bore in first position, put back at
	// their 7.2 mm between the inner tubes and the crook.
	tubes.clear();
	for (size_t i = first; i < last; i++) {
		const BrassSection& sec = bore.sections[i];
		if (sec.r0 == 0.0074) {
			Tube outer;
			outer.radius = 0.0072;
			outer.outer = true;
			tubes.push_back(outer);
			Tube crook;
			crook.length = sec.length;
			crook.radius = sec.r0;
			tubes.push_back(crook);
			tubes.push_back(outer);
			continue;
		}
		if (sec.r0 == 0.0072)
			continue;
		Tube t;
		t.length = sec.length;
		t.radius = sec.r0;
		tubes.push_back(t);
	}
	// Each outer tube keeps OUTER_KEEPS of the inner tube beside it.
	for (size_t i = 0; i < tubes.size(); i++)
		if (tubes[i].outer) {
			const size_t inner = i == 1 ? 0 : i + 1;
			tubes[inner].length -= OUTER_KEEPS;
		}
	const int longest = (int) std::ceil((0.711 + 0.6) / air.c * rate) + 8;
	for (Tube& t : tubes) {
		t.forward.setup(longest);
		t.backward.setup(longest);
	}
	reflection.assign(tubes.size(), 0.0);
	for (size_t j = 0; j + 1 < tubes.size(); j++) {
		const double a = tubes[j].radius * tubes[j].radius, b = tubes[j + 1].radius * tubes[j + 1].radius;
		reflection[j] = (a - b) / (a + b);
	}
	arrivingRight.assign(tubes.size(), 0.0);
	arrivingLeft.assign(tubes.size(), 0.0);
	brassHalfOrderFit(s.lossTerms, 20.0, 0.45 * rate, 0.45 * rate, &xi, &mu);
	setExtension(s.extension);
}


void BrassWaveguide::setExtension(double extension) {
	for (Tube& t : tubes)
		if (t.outer)
			t.length = std::fmax(0.0, extension) + OUTER_KEEPS;
	retune();
}


void BrassWaveguide::retune() {
	double beta = 0.0;
	const double wall = std::sqrt(air.nu()) * (1.0 + (air.gamma - 1.0) / std::sqrt(air.prandtl)) / air.c;
	for (Tube& t : tubes) {
		t.delay = std::fmax(2.0, t.length / air.c * rate);
		beta += t.length * wall / t.radius;
	}
	lossOn.design(beta, xi, mu, rate);
	lossBack.design(beta, xi, mu, rate);
}


double BrassWaveguide::step(double flow) {
	const size_t n = tubes.size();
	// WHAT ARRIVES at each end of each tube, before anything is written this step.
	for (size_t i = 0; i < n; i++) {
		arrivingRight[i] = tubes[i].forward.read(tubes[i].delay);
		arrivingLeft[i] = tubes[i].backward.read(tubes[i].delay);
	}
	// The grids, each meeting the run of tubes with the losses of the whole run.
	front.arrive(0.0, lossBack.process(arrivingLeft[0]));
	back.arrive(lossOn.process(arrivingRight[n - 1]), 0.0);
	const double cup = front.step(flow);
	back.step(0.0);
	// WHAT LEAVES: the grids' waves into the run's ends, and each junction's scattering.
	tubes[0].forward.write(front.leaving(1));
	tubes[n - 1].backward.write(back.leaving(0));
	for (size_t j = 0; j + 1 < n; j++) {
		const double r = reflection[j], fromLeft = arrivingRight[j], fromRight = arrivingLeft[j + 1];
		tubes[j + 1].forward.write((1.0 + r) * fromLeft - r * fromRight);
		tubes[j].backward.write(r * fromLeft + (1.0 - r) * fromRight);
	}
	return cup;
}


} // namespace px
