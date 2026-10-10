/** A BOWED INSTRUMENT, from a command line: phase 6 of docs/bowed-strings.md.

WHAT IT CHECKS. That four strings on a bridge that stands still are four strings on their own;
that on a bridge that moves nothing grows; that a string nobody plays rings when another plays a
note its partials share — and not when the note shares nothing with it; and which string each note
is played on. Then a scale on each instrument, with the strings' sympathy and without it.

    make bowinstrument                     all of it
    make bowinstrument ARGS="sympathy"     one section: still, stable, sympathy, strings, listen
*/
#include "bowbench.hpp"
#include "../src/BowedInstrument.hpp"

#include <cstring>


// ---- PLAYING ON IT ------------------------------------------------------------------------------

/** AN INSTRUMENT RUNNING AT THE ENGINE'S RATE TIMES `factor`, with the bench's RATE set to match for
as long as it lives, so the rehearsing player works at the strings' rate. */
struct Rig {
	BowedInstrument inst;
	float base;
	int factor;
	Rig(const BowedInstrument::Design& d, int f) : base(RATE), factor(f) {
		inst.setDesign(d);
		inst.setRate(base, f);
		RATE = base * f;
	}
	~Rig() { RATE = base; }
};

/** HOW AN ATTACK STARTS ON THE INSTRUMENT ITSELF, a copy of it with its bridge moving and its other
strings as they are: periods from the first slip until every period is one slip, -1 if not within
twenty. */
static int startsOn(const BowedInstrument& inst, int string, double hz, float force, float speed,
                    float distance, const Rehearsed& c) {
	BowedInstrument copy = inst;
	const double period = RATE / hz;
	std::vector<size_t> slips;
	bool was = true;
	size_t end = (size_t) (1.5 * RATE);
	for (size_t i = 0; i < end; i++) {
		copy.bow(string, (float) (force * c.bite), (float) std::fmin(c.accel * (i / RATE), (double) speed), distance);
		float sound;
		copy.step(&sound);
		const bool stuck = copy.string(string).sticking();
		if (i && was && !stuck) {
			slips.push_back(i);
			if (slips.size() == 1)
				end = std::min(end, i + (size_t) (20 * period));
		}
		was = stuck;
	}
	if (slips.size() < 5)
		return -1;
	size_t k = slips.size();
	for (size_t j = slips.size(); j-- > 1; ) {
		if (std::fabs((double) (slips[j] - slips[j - 1]) - period) > 0.1 * period)
			break;
		k = j - 1;
	}
	if (k + 4 > slips.size())
		return -1;
	return (int) std::lround((double) (slips[k] - slips[0]) / period);
}

/** THE PLAYER'S CHOICE ON THE INSTRUMENT: its shortlist from the lone string tried in order on
copies of the instrument, and the first that starts within three periods there played — or, if none
of the first twelve does, the one that started soonest. */
static Rehearsed confirmOn(const BowedInstrument& inst, int string, double hz, float force, float speed,
                           float vibrating, const std::vector<Rehearsed>& list) {
	Rehearsed best;
	int bestPeriods = 999;
	for (size_t i = 0; i < list.size() && i < 12; i++) {
		const int p = startsOn(inst, string, hz, force, speed, list[i].beta * vibrating, list[i]);
		if (p >= 0 && p < bestPeriods) {
			bestPeriods = p;
			best = list[i];
			best.periods = p;
		}
		if (p >= 0 && p <= 3)
			break;
	}
	if (bestPeriods == 999 && !list.empty())
		best = list.front();
	return best;
}


/** ONE NOTE ON ONE STRING: the finger down, the attack rehearsed on a copy of that string as it
stands and confirmed on a copy of the instrument (confirmOn), then played sample by sample at the strings' rate — the bite, eased over EASE_SECONDS once
four regular periods are heard, the bow lifted after `seconds`. The engine-rate sound appended to
`out`, `extra` seconds more of it after the lift; `onSample` called at every string sample. */
template <typename F>
static Rehearsed playNote(Rig& rig, int string, float hz, float force, float fromBridge, float seconds,
                          float extra, std::vector<float>* out, F onSample) {
	BowedInstrument& inst = rig.inst;
	inst.stop(string, hz);
	const float open = inst.design().strings[(size_t) string].openHz;
	const float vibrating = inst.design().length * open / hz;
	const float beta = fromBridge / vibrating;
	const float speed = 0.2f;
	const Rehearsed c = confirmOn(inst, string, hz, force, speed, vibrating,
		rehearseShortlist(inst.string(string), hz, beta, force, speed));
	const float distance = c.beta * vibrating;
	const double period = RATE / hz;
	std::vector<size_t> slips;
	bool was = true;
	double easeFrom = -1.0;
	const size_t total = (size_t) ((seconds + extra) * RATE);
	for (size_t i = 0; i < total; i++) {
		const double t = i / RATE;
		const double bite = force * c.bite;
		double f = easeFrom < 0.0 ? bite : bite + (force - bite) * std::fmin(1.0, (t - easeFrom) / EASE_SECONDS);
		if (t > seconds)
			f = 0.0;
		inst.bow(string, (float) f, (float) std::fmin(c.accel * t, (double) speed), distance);
		float sound;
		if (inst.step(&sound) && out)
			out->push_back(sound);
		onSample(i);
		const bool stuck = inst.string(string).sticking();
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
	}
	return c;
}


// ---- THE SECTIONS -------------------------------------------------------------------------------

/** ON A BRIDGE THAT STANDS STILL the instrument's strings are strings on their own: the D string
bowed in the violin with the sympathy off, against a copy of it bowed alone. */
static void stillSection() {
	std::printf("\nON A STILL BRIDGE, a string in the instrument is a string on its own.\n");
	Rig rig(BowedInstrument::violin(), 4);
	rig.inst.setSympathetic(false);
	rig.inst.stop(1, 293.66f);
	BowedString alone = rig.inst.string(1);
	double worst = 0.0, peak = 0.0;
	for (size_t i = 0; i < (size_t) (0.5 * RATE); i++) {
		const float v = (float) std::fmin(0.2, 2.0 * i / RATE);
		rig.inst.bow(1, 0.4f, v, 0.0328f);
		alone.setContact(0.0328f / 0.328f);
		alone.setBow(0.4f, v);
		float sound;
		rig.inst.step(&sound);
		alone.process();
		worst = std::fmax(worst, std::fabs(rig.inst.string(1).bridgeForce() - alone.bridgeForce()));
		peak = std::fmax(peak, std::fabs(alone.bridgeForce()));
	}
	check(worst <= 1e-6 * peak, "  bridge force, in the instrument against alone, worst difference (N)", worst);
}


/** ON A BRIDGE THAT MOVES, nothing grows: every string plucked hard and one bowed, the sympathy on,
on both instruments; what is left after the bow lifts dies away. */
static void stableSection() {
	std::printf("\nON A BRIDGE THAT MOVES, nothing grows.\n");
	const char* names[] = {"violin", "cello"};
	for (int which = 0; which < 2; which++) {
		Rig rig(which ? BowedInstrument::cello() : BowedInstrument::violin(), which ? 2 : 4);
		BowedInstrument& inst = rig.inst;
		const float length = inst.design().length;
		for (int s = 0; s < inst.strings(); s++)
			inst.pluck(s, 1.f, 0.1f * length);
		double peak = 0.0, late = 0.0, held = 0.0;
		const size_t n = (size_t) (4.0 * RATE);
		for (size_t i = 0; i < n; i++) {
			const double t = i / RATE;
			inst.bow(2, t < 1.5 ? 1.f * inst.string(2).impedance() / 0.67f : 0.f, 0.2f, 0.1f * length);
			float sound;
			inst.step(&sound);
			const double v = std::fabs(inst.bridgeVelocity());
			peak = std::fmax(peak, v);
			if (t > 1.2 && t < 1.5) held = std::fmax(held, v);
			if (t > 3.7) late = std::fmax(late, v);
		}
		char what[96];
		std::snprintf(what, sizeof what, "  %s: bridge velocity, highest (m/s)", names[which]);
		check(peak < 1.0 && std::isfinite(peak), what, peak);
		std::snprintf(what, sizeof what, "  %s: two seconds after the bow lifts, against while bowed", names[which]);
		check(late < 0.5 * held, what, late / std::fmax(1e-12, held));
	}
}


/** A STRING NOBODY PLAYS RINGS WHEN ANOTHER PLAYS A NOTE ITS PARTIALS SHARE: on the violin, D4
bowed on the G string with the D string open and untouched, against D sharp, which shares nothing
with it, and against D4 with the bridge standing still. How much the open D string moves, the force it
puts on the bridge, while the note is held and after it. */
static void sympathySection() {
	std::printf("\nSYMPATHY: the violin's open D string while D4 is bowed on the G string.\n");
	struct Case { const char* what; float hz; bool coupled; };
	const Case cases[] = {{"D4, bridge moving", 293.66f, true}, {"D sharp 4, bridge moving", 311.13f, true},
	                      {"D4, bridge still", 293.66f, false}};
	double level[3] = {0, 0, 0}, after[3] = {0, 0, 0};
	for (int k = 0; k < 3; k++) {
		Rig rig(BowedInstrument::violin(), 4);
		rig.inst.setSympathetic(cases[k].coupled);
		double sum = 0.0, sumAfter = 0.0;
		size_t n = 0, nAfter = 0;
		const double holdFrom = 0.6 * RATE, holdTo = 1.0 * RATE, afterFrom = 1.1 * RATE, afterTo = 1.4 * RATE;
		playNote(rig, 0, cases[k].hz, 0.5f, 0.0328f, 1.0f, 0.5f, nullptr, [&](size_t i) {
			const double f = rig.inst.string(1).bridgeForce();
			if (i >= holdFrom && i < holdTo) { sum += f * f; n++; }
			if (i >= afterFrom && i < afterTo) { sumAfter += f * f; nAfter++; }
		});
		level[k] = std::sqrt(sum / std::max<size_t>(1, n));
		after[k] = std::sqrt(sumAfter / std::max<size_t>(1, nAfter));
		std::printf("  %-26s the open D string while held %.2e N, after the note %.2e N\n", cases[k].what, level[k], after[k]);
	}
	const double vsSharp = 20.0 * std::log10(level[0] / std::fmax(1e-12, level[1]));
	check(vsSharp > 15.0, "  D4 against D sharp: the open D rings more by (dB)", vsSharp);
	check(level[2] < 1e-6 * level[0], "  and with the bridge still it does not ring at all, against moving",
		level[2] / std::fmax(1e-12, level[0]));
	check(after[0] > 0.1 * level[0], "  it rings on after the note, against while held", after[0] / level[0]);
}


static void stringsSection() {
	std::printf("\nWHICH STRING: each note on the string that puts it lowest on the fingerboard.\n");
	const char* names[] = {"violin", "cello"};
	for (int which = 0; which < 2; which++) {
		BowedInstrument inst;
		inst.setDesign(which ? BowedInstrument::cello() : BowedInstrument::violin());
		const double low = which ? 65.41 : 196.0;
		std::printf("  %s:", names[which]);
		for (int semis = 0; semis <= 30; semis += 2) {
			const double hz = low * std::pow(2.0, semis / 12.0);
			std::printf(" %.0f>%d", hz, inst.stringFor((float) hz));
		}
		std::printf("\n");
	}
}


/** A SCALE ON EACH, two octaves up and back, each note on its string, with the strings' sympathy and
without it. */
static void listenSection() {
	std::printf("\nTO LISTEN TO.\n");
	struct Inst { const char* name; BowedInstrument::Design (*design)(); int factor; double low; float force; float distance; const char* file; };
	const Inst insts[] = {{"violin", BowedInstrument::violin, 4, 196.0, 0.4f, 0.0328f, "build/bowed-violin-scale"},
	                      {"cello", BowedInstrument::cello, 2, 65.41, 1.5f, 0.069f, "build/bowed-cello-scale"}};
	const int major[] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24};
	for (const Inst& in : insts)
		for (int coupled = 1; coupled >= 0; coupled--) {
			Rig rig(in.design(), in.factor);
			rig.inst.setSympathetic(coupled != 0);
			std::vector<float> out;
			std::vector<int> order;
			for (int d : major) order.push_back(d);
			for (int k = (int) (sizeof major / sizeof major[0]) - 2; k >= 0; k--) order.push_back(major[k]);
			for (int semis : order) {
				const float hz = (float) (in.low * std::pow(2.0, semis / 12.0));
				const int s = rig.inst.stringFor(hz);
				const float force = in.force * rig.inst.string(s).impedance() / (in.name[0] == 'v' ? 0.34f : 1.46f);
				playNote(rig, s, hz, force, in.distance, 0.55f, 0.08f, &out, [](size_t) {});
			}
			for (size_t i = 0; i < (size_t) (1.0 * rig.base); i++)
				out.push_back(rig.inst.process());
			normalise(out);
			writeWav(std::string(in.file) + (coupled ? "-sympathy.wav" : "-still-bridge.wav"), out);
		}
}


int main(int argc, char** argv) {
	const char* only = argc > 1 ? argv[1] : "";
	auto run = [&](const char* name) { return !*only || std::strcmp(only, name) == 0; };
	if (run("still")) stillSection();
	if (run("stable")) stableSection();
	if (run("sympathy")) sympathySection();
	if (run("strings")) stringsSection();
	if (run("listen")) listenSection();
	std::printf("\n%s\n", failures ? "SOME CHECKS FAILED" : "all passed");
	return failures ? 1 : 0;
}
