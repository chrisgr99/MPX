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


void BrassSteepeningDelay::setup(int size, double steepness, double rate) {
	int n = 8;
	while (n < 4 * size + 16)
		n *= 2;
	arrival.assign((size_t) n, 0.0);
	value.assign((size_t) n, 0.0);
	clusters.assign((size_t) n, Cluster());
	mask = n - 1;
	steep = steepness;
	slow = 0.0;
	slowPole = 1.0 - std::exp(-2.0 * M_PI * 10.0 / rate);
	// SILENCE ALREADY IN FLIGHT: samples of nought written before the start, arriving from well before it.
	now = 0.0;
	head = 0;
	for (int i = 0; i < size + 4; i++) {
		arrival[(size_t) (head & mask)] = (double) (i - size - 4);
		value[(size_t) (head & mask)] = 0.0;
		head++;
	}
	cursor = 0;
	oldest = newest = 0;
}


void BrassSteepeningDelay::write(double x, double delay) {
	slow += slowPole * (x - slow);
	const double sound = x - slow;
	// Written in the step whose read has already moved the clock on: this step's time is now less one. Each
	// read is the mean over the step before it, centred half a step back, so the arrival is half a step sooner.
	const double own = now - 1.5 + delay / (1.0 + steep * sound);
	value[(size_t) (head & mask)] = x;
	// A SAMPLE THAT CATCHES THE ONES AHEAD OF IT JOINS THEM, and they travel on together at the mean of their
	// speeds: they arrive at the mean of the times each would have arrived alone. Joining may bring the group
	// ahead of the one before it, which it then joins too.
	Cluster c;
	c.first = head;
	c.count = 1;
	c.sumOwn = own;
	while (newest > oldest) {
		const Cluster& before = clusters[(size_t) ((newest - 1) & mask)];
		if (c.sumOwn / c.count >= before.sumOwn / before.count)
			break;
		c.first = before.first;
		c.count += before.count;
		c.sumOwn += before.sumOwn;
		newest--;
	}
	clusters[(size_t) (newest & mask)] = c;
	newest++;
	const double at = c.sumOwn / c.count;
	for (int64_t i = c.first; i <= head; i++)
		arrival[(size_t) (i & mask)] = at;
	head++;
	// Groups that have arrived and been read can no longer be caught.
	while (newest - oldest > 1 && clusters[(size_t) (oldest & mask)].sumOwn / clusters[(size_t) (oldest & mask)].count < now - 2.0)
		oldest++;
}


double BrassSteepeningDelay::span(int64_t i, double from, double to) const {
	auto a = [&](int64_t j) { return arrival[(size_t) (j & mask)]; };
	auto v = [&](int64_t j) { return value[(size_t) (j & mask)]; };
	const int64_t i0 = i > 0 ? i - 1 : i, i3 = i + 2 < head ? i + 2 : i + 1;
	const double h = a(i + 1) - a(i);
	const double t0 = (from - a(i)) / h, t1 = (to - a(i)) / h;
	const double v1 = v(i), v2 = v(i + 1);
	if (h < 0.5) {
		// A straight line between samples arriving close together.
		auto f = [&](double t) { return v1 * (t - 0.5 * t * t) + v2 * 0.5 * t * t; };
		return h * (f(t1) - f(t0));
	}
	// Catmull and Rom's cubic on uneven times: each end's slope from its neighbours, scaled to this span;
	// the integrals of Hermite's four functions.
	const double s1 = a(i + 1) - a(i0) > 1e-9 ? (v2 - v(i0)) / (a(i + 1) - a(i0)) * h : 0.0;
	const double s2 = a(i3) - a(i) > 1e-9 ? (v(i3) - v1) / (a(i3) - a(i)) * h : 0.0;
	auto f = [&](double t) {
		const double t2 = t * t, t3 = t2 * t, t4 = t3 * t;
		return v1 * (0.5 * t4 - t3 + t) + s1 * (0.25 * t4 - 2.0 / 3.0 * t3 + 0.5 * t2) + v2 * (-0.5 * t4 + t3) + s2 * (0.25 * t4 - t3 / 3.0);
	};
	return h * (f(t1) - f(t0));
}


double BrassSteepeningDelay::read() {
	// THE MEAN OF THE WAVE OVER THE STEP just ended, from now - 1 to now, through the samples arriving in it:
	// a shock arriving between two steps is shared between them by where it falls, as the samples can carry,
	// instead of landing whole on the next.
	const double from = now - 1.0, to = now;
	auto a = [&](int64_t j) { return arrival[(size_t) (j & mask)]; };
	while (cursor + 2 < head && a(cursor + 1) <= from)
		cursor++;
	double sum = 0.0;
	if (a(cursor) > from)
		sum += (std::fmin(a(cursor), to) - from) * value[(size_t) (cursor & mask)];
	int64_t i = cursor;
	for (; i + 1 < head && a(i) < to; i++) {
		const double lo = std::fmax(a(i), from), hi = std::fmin(a(i + 1), to);
		if (hi > lo)
			sum += span(i, lo, hi);
	}
	// Nothing yet arrived after the end of the step: the last value holds.
	if (a(i) < to)
		sum += (to - std::fmax(a(i), from)) * value[(size_t) (i & mask)];
	now += 1.0;
	return sum;
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
	listener.design(g, s.listener);
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
	steepens = s.steepening;
	// beta = (gamma + 1) / 2 over rho c^2: how much faster a pascal travels, as a share of c.
	const double steepness = 0.5 * (air.gamma + 1.0) / (air.rho * air.c * air.c);
	for (Tube& t : tubes) {
		t.forward.setup(longest);
		t.backward.setup(longest);
		t.steepening.setup(longest, steepness, rate);
	}
	reflection.assign(tubes.size(), 0.0);
	for (size_t j = 0; j + 1 < tubes.size(); j++) {
		const double a = tubes[j].radius * tubes[j].radius, b = tubes[j + 1].radius * tubes[j + 1].radius;
		reflection[j] = (a - b) / (a + b);
	}
	arrivingRight.assign(tubes.size(), 0.0);
	arrivingLeft.assign(tubes.size(), 0.0);
	heardRight.assign(tubes.size(), 0.0);
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
	lossHeard.design(beta, xi, mu, rate);
}


double BrassWaveguide::step(double flow) {
	begin(nullptr, nullptr);
	return end(flow);
}


void BrassWaveguide::begin(double* cupWithout, double* cupPerFlow) {
	const size_t n = tubes.size();
	// WHAT ARRIVES at each end of each tube, before anything is written this step.
	for (size_t i = 0; i < n; i++) {
		arrivingRight[i] = tubes[i].forward.read(tubes[i].delay);
		arrivingLeft[i] = tubes[i].backward.read(tubes[i].delay);
		if (steepens)
			heardRight[i] = tubes[i].steepening.read();
	}
	// The grids, each meeting the run of tubes with the losses of the whole run.
	front.arrive(0.0, lossBack.process(arrivingLeft[0]));
	const double intoBell = lossOn.process(arrivingRight[n - 1]);
	back.arrive(intoBell, 0.0);
	// WHAT THE LISTENER HEARS: the wave entering the bell, steepened where it steepens, through the bell and
	// out to them.
	heardIn = steepens ? lossHeard.process(heardRight[n - 1]) : intoBell;
	heardNow = listener.process(heardIn);
	front.begin(cupWithout, cupPerFlow);
}


double BrassWaveguide::end(double flow) {
	const size_t n = tubes.size();
	const double cup = front.end(flow);
	cupNow = cup;
	back.step(0.0);
	// WHAT LEAVES: the grids' waves into the run's ends, and each junction's scattering. The steepened wave
	// to the bell meets the same wave coming back at each junction.
	const double fromCup = front.leaving(1);
	tubes[0].forward.write(fromCup);
	if (steepens)
		tubes[0].steepening.write(fromCup, tubes[0].delay);
	tubes[n - 1].backward.write(back.leaving(0));
	for (size_t j = 0; j + 1 < n; j++) {
		const double r = reflection[j], fromLeft = arrivingRight[j], fromRight = arrivingLeft[j + 1];
		tubes[j + 1].forward.write((1.0 + r) * fromLeft - r * fromRight);
		tubes[j].backward.write(r * fromLeft + (1.0 - r) * fromRight);
		if (steepens)
			tubes[j + 1].steepening.write((1.0 + r) * heardRight[j] - r * fromRight, tubes[j + 1].delay);
	}
	return cup;
}


} // namespace px
