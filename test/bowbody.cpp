/** THE BODY OF A BOWED INSTRUMENT, from a command line: phase 5 of docs/bowed-strings.md.

WHAT IT CHECKS. That the body built from an instrument's published measurements has the response
those measurements describe — its signature modes where they were measured, the bridge hill, the
level from the bottom of the range to the top — that the filter running sample by sample gives the
response worked out from its resonators, and that nothing in it grows. Then it plays: notes on the
violin and the cello, through the body and without it, to listen to. This is the first phase whose
result is judged by ear as well as by measurement.

    make bowbody                      all of it
    make bowbody ARGS="response"      one section: response, filter, listen
*/
#include "bowbench.hpp"
#include "../src/BowedBody.hpp"
#include "../src/BowedInstrument.hpp"
#include "../src/Decimator.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>


// ---- THE INSTRUMENTS -------------------------------------------------------------------------

/** THE INSTRUMENTS' BODIES live with the instruments, in src/BowedInstrument.cpp, with where every
figure in them came from. */
static BowedBody::Design violinBody() { return BowedInstrument::violin().voice; }
static BowedBody::Design celloBody() { return BowedInstrument::cello().voice; }


// ---- A NOTE, PLAYED ----------------------------------------------------------------------------

/** WHAT A NOTE NEEDS TO BE PLAYED: the string, where the bow is, how hard and how fast. */
struct Note {
	double hz = 440.0;
	float impedance = -1.f;
	float beta = 0.1f;
	float force = 0.5f;
	float speed = 0.2f;
	float seconds = 1.5f;
};

/** ONE NOTE AT THE ENGINE'S RATE: the string run at the schedule's factor, rehearsed by the player
of bowbench.hpp, bitten,
eased back over EASE_SECONDS once four regular periods are heard, lifted at the end; the force on the bridge brought
back down to the engine's rate. */
static std::vector<float> bridgeForce(const Note& n, Rehearsed* chose = nullptr) {
	const float base = RATE;
	const int factor = BowedString::oversampling((float) n.hz, base);
	RATE = base * factor;
	const Rehearsed c = rehearseAttack(n.hz, n.impedance, n.beta, n.force, n.speed);
	if (chose)
		*chose = c;
	// Played where the attack was found, which is where the note was planned unless nothing there
	// started cleanly.
	BowedString s = cello((float) n.hz, c.beta, n.impedance);
	Decimator down;
	down.setFactor(factor);
	std::vector<float> out;
	const double period = RATE / n.hz;
	std::vector<size_t> slips;
	bool was = true;
	double easeFrom = -1.0;
	const size_t total = (size_t) ((n.seconds + 0.6) * RATE);
	for (size_t i = 0; i < total; i++) {
		const double t = i / RATE;
		const double bite = n.force * c.bite;
		double force = easeFrom < 0.0 ? bite : bite + (n.force - bite) * std::fmin(1.0, (t - easeFrom) / EASE_SECONDS);
		if (t > n.seconds)
			force = 0.0;
		s.setBow((float) force, (float) std::fmin(c.accel * t, (double) n.speed));
		s.process();
		const bool stuck = s.sticking();
		if (i && was && !stuck && easeFrom < 0.0) {
			slips.push_back(i);
			if (slips.size() >= 5) {
				bool regular = true;
				for (size_t q = slips.size() - 4; q < slips.size(); q++)
					regular = regular && std::fabs((double) (slips[q] - slips[q - 1]) - period) <= 0.1 * period;
				if (regular)
					easeFrom = t;
			}
		}
		was = stuck;
		float y;
		if (down.push(s.bridgeForce(), &y))
			out.push_back(y);
	}
	RATE = base;
	return out;
}


// ---- THE SECTIONS -----------------------------------------------------------------------------

static void responseSection(const char* name, const BowedBody::Design& d) {
	BowedBody body;
	body.setSampleRate(RATE);
	body.setDesign(d);
	std::printf("\nTHE %s'S BODY: %d resonators. Radiativity in dB re 1 Pa/N a metre away, mobility at the bridge "
		"in dB re 1 m/(s N).\n", name, body.resonators());
	for (double hz = 100.0; hz <= 10000.0; hz *= std::pow(2.0, 1.0 / 6.0)) {
		const float r = body.radiativityAt((float) hz), m = body.mobilityAt((float) hz);
		std::printf("  %7.0f Hz  %6.1f  %6.1f\n", hz, 20.0 * std::log10(std::fmax(1e-9f, r)),
			20.0 * std::log10(std::fmax(1e-9f, m)));
	}
}


/** THE VIOLIN AGAINST BISSINGER'S SEVENTEEN: its radiativity averaged over each 250 Hz band, as he
averaged his, against his band averages, and its signature modes against his. */
static void violinAgainstMeasured() {
	std::printf("\nTHE VIOLIN AGAINST BISSINGER'S SEVENTEEN VIOLINS, band by band, dB re 1 Pa/N a metre away.\n");
	BowedBody body;
	body.setSampleRate(RATE);
	body.setDesign(violinBody());
	const double bands[] = {625, 875, 1125, 1375, 1625, 1875, 2125, 2375, 2625, 2875, 3125, 3375, 3625, 3875};
	const double measured[] = {-10.5, -7.1, -6.6, -7.3, -6.0, -3.9, -2.6, -1.0, -2.6, -3.2, -4.8, -4.3, -4.3, -4.8};
	std::vector<double> off;
	for (size_t i = 0; i < sizeof bands / sizeof bands[0]; i++) {
		double power = 0.0;
		const int n = 100;
		for (int k = 0; k < n; k++) {
			const double r = body.radiativityAt((float) (bands[i] - 125.0 + 250.0 * (k + 0.5) / n));
			power += r * r;
		}
		const double db = 10.0 * std::log10(power / n);
		off.push_back(db - measured[i]);
		std::printf("  %5.0f Hz  modelled %6.1f  measured %6.1f  %+5.1f\n", bands[i], db, measured[i], db - measured[i]);
	}
	std::vector<double> sorted = off;
	for (double& x : sorted)
		x = std::fabs(x);
	std::sort(sorted.begin(), sorted.end());
	check(sorted[sorted.size() / 2] < 1.5, "  bands off by, the median (dB)", sorted[sorted.size() / 2]);
	check(sorted.back() < 3.0, "  and at worst (dB)", sorted.back());
	struct Peak { double hz, pa; const char* name; };
	const Peak peaks[] = {{275.0, 0.444, "A0"}, {475.0, 1.212, "B1-"}, {541.0, 1.140, "B1+"}};
	for (const Peak& p : peaks) {
		double best = 0.0, at = p.hz;
		for (double hz = p.hz * 0.95; hz <= p.hz * 1.05; hz += 0.5)
			if (body.radiativityAt((float) hz) > best) {
				best = body.radiativityAt((float) hz);
				at = hz;
			}
		char what[96];
		std::snprintf(what, sizeof what, "  %s at %.0f Hz, %.2f Pa/N against %.2f measured (dB)", p.name, at, best, p.pa);
		check(std::fabs(20.0 * std::log10(best / p.pa)) < 3.0, what, 20.0 * std::log10(best / p.pa));
	}
}


/** THE FILTER AGAINST ITS OWN RESPONSE: an impulse through it, its spectrum at the response's
frequencies, and how much is left after two seconds. */
static void filterSection(const char* name, const BowedBody::Design& d) {
	std::printf("\nTHE %s'S BODY, RUN: its impulse response against the response worked out from its resonators.\n", name);
	BowedBody body;
	body.setSampleRate(RATE);
	body.setDesign(d);
	const size_t n = (size_t) (2.0 * RATE);
	std::vector<float> h(n);
	for (size_t i = 0; i < n; i++)
		h[i] = body.process(i == 0 ? 1.f : 0.f);
	double worst = 0.0;
	for (double hz : {150.0, 300.0, 600.0, 1200.0, 2400.0, 4800.0}) {
		double re = 0.0, im = 0.0;
		for (size_t i = 0; i < n; i++) {
			re += h[i] * std::cos(2.0 * M_PI * hz * i / RATE);
			im -= h[i] * std::sin(2.0 * M_PI * hz * i / RATE);
		}
		const double got = std::sqrt(re * re + im * im), want = body.radiativityAt((float) hz);
		const double db = 20.0 * std::log10(std::fmax(1e-12, got) / std::fmax(1e-12, want));
		worst = std::fmax(worst, std::fabs(db));
	}
	check(worst < 0.5, "  impulse response against the resonators' sum, worst (dB)", worst);
	double tail = 0.0, head = 0.0;
	for (size_t i = 0; i < n; i++)
		(i > n - (size_t) (0.1 * RATE) ? tail : head) += (double) h[i] * h[i];
	check(tail < 1e-6 * head, "  nothing left after two seconds, energy against the first", tail / std::fmax(1e-30, head));
}


static void listenSection() {
	std::printf("\nTO LISTEN TO.\n");
	struct Instrument { const char* name; BowedBody::Design (*design)(); std::vector<Note> notes; const char* file; };
	// Each note on its own string, with that string's impedance: the open strings, then the top
	// string stopped a fifth and an octave up.
	auto note = [](double hz, float impedance, float force) {
		Note n;
		n.hz = hz;
		n.impedance = impedance;
		n.beta = 0.1f;
		n.force = force;
		return n;
	};
	const Instrument instruments[] = {
		{"violin", violinBody, {note(196.0, 0.34f, 0.5f), note(293.66, 0.23f, 0.4f), note(440.0, 0.19f, 0.4f),
		                        note(659.26, 0.17f, 0.3f), note(987.77, 0.17f, 0.3f), note(1318.51, 0.17f, 0.25f)},
		 "build/bowed-violin"},
		{"cello", celloBody, {note(65.41, 1.46f, 3.f), note(98.0, 1.0f, 1.5f), note(146.83, 0.67f, 1.f),
		                       note(220.0, 0.56f, 0.8f), note(329.63, 0.56f, 0.8f), note(440.0, 0.56f, 0.6f)},
		 "build/bowed-cello"},
	};
	for (const Instrument& inst : instruments) {
		BowedBody body;
		body.setSampleRate(RATE);
		body.setDesign(inst.design());
		std::vector<float> dry, wet;
		for (const Note& n : inst.notes) {
			Rehearsed c;
			const std::vector<float> f = bridgeForce(n, &c);
			std::printf("  %-6s %7.2f Hz: rehearsed, %s, bite %.1f, bow at %.3f\n", inst.name, n.hz,
				c.periods >= 0 ? (std::to_string(c.periods) + " periods to Helmholtz motion").c_str() : "no clean start",
				c.bite, c.beta);
			for (float x : f) {
				dry.push_back(x);
				wet.push_back(body.process(x));
			}
			dry.insert(dry.end(), (size_t) (0.2 * RATE), 0.f);
			for (size_t i = 0; i < (size_t) (0.2 * RATE); i++)
				wet.push_back(body.process(0.f));
		}
		normalise(dry);
		normalise(wet);
		writeWav(std::string(inst.file) + "-bridge.wav", dry);
		writeWav(std::string(inst.file) + "-body.wav", wet);
	}
}


int main(int argc, char** argv) {
	const char* only = argc > 1 ? argv[1] : "";
	auto run = [&](const char* name) { return !*only || std::strcmp(only, name) == 0; };
	if (run("response")) {
		responseSection("VIOLIN", violinBody());
		violinAgainstMeasured();
		responseSection("CELLO", celloBody());
	}
	if (run("filter")) {
		filterSection("VIOLIN", violinBody());
		filterSection("CELLO", celloBody());
	}
	if (run("listen"))
		listenSection();
	std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "all passed");
	return failures ? 1 : 0;
}
