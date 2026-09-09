/** mpxScatter — one lane of an MPX cable, made to vary.

WHAT IT IS. Notes go in, the same notes come out with one thing about them changed by a random
process you choose. It is not a humaniser, though it will do that at low settings: a scatter at
full depth on duration turns an even arpeggio into something between a stutter and a drone, and
that is the point of it as much as realism is.

ONE LANE PER INSTANCE, AND THAT IS THE DESIGN RATHER THAN A LIMITATION. A single module doing
four lanes at once would drive them from one generator, so the level would rise exactly as the
duration lengthened and every note would be long-and-loud or short-and-quiet. Four narrow modules
in a row have four generators, and the lanes drift against each other, which is what makes it
sound like several things happening rather than one.

CHAINED, because every MPX module already forwards what it does not touch. A scatter passes the
harmony and every lane but its own straight through, so a row of them is a patch.

THE GENERATOR RUNS ON THE MUSIC, NOT ON THE CLOCK. It is a function of the seed on the cable and
the beat the chart has reached, sampled when a note starts — so a drift measured in beats gives
neighbouring values to a fast arpeggio and unrelated ones to a slow chord, and playing the same
bar twice gives the same numbers. Rewinding the chart rewinds the randomness, because the
randomness never depended on anything else.

DURATION IS THE ONE THAT NEEDS MACHINERY. A note ends twice over — the duration it was sent with,
and the note-off that follows it — and whichever comes first wins. Rewriting the duration alone
could therefore only ever make notes SHORTER; the source's note-off would still cut a lengthened
one at its original end. So for that lane this module takes ownership of the note's end: it
swallows the source's note-off and sends its own when its own clock says so. Every other lane is
a rewrite on the note-on with no state at all.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

#include <algorithm>
#include <cmath>

namespace px {


/** WHICH LANE, ordered by how often you would reach for it.

NAMED APART FROM NoteBus's OWN Lane, which is the bend, pressure and timbre a note carries.
Two different ideas were both called Lane and the compiler said so. */
enum ScatterLane {
	SCAT_DURATION,
	SCAT_LEVEL,
	SCAT_PAN,
	SCAT_TIMING,
	SCAT_DETUNE,
	NUM_SCATTER_LANES,
};

static const char* SCAT_LANE_NAMES[NUM_SCATTER_LANES] =
	{"Duration", "Level", "Pan", "Timing", "Detune"};

/** THE SHAPE OF THE RANDOMNESS, which is really a question about how much each note resembles
the last one.

White resembles it not at all. A walk is the sum of small steps, so consecutive notes are close
and the value wanders — brown noise, by another name. Perlin interpolates smoothly between
values a rate apart in the music, so it undulates rather than jitters, and never has a corner
in it.

Red is not a fourth position: red IS a walk seen over a longer window, which is what the rate
control does. Naming it separately would be two names for one knob. */
enum Shape {
	SHAPE_WHITE,
	SHAPE_WALK,
	SHAPE_PERLIN,
	NUM_SHAPES,
};

static const char* SHAPE_NAMES[NUM_SHAPES] = {"White", "Walk", "Perlin"};

/** How far each lane may be moved at full depth.

DURATION IS A FACTOR, not an addition — half of something is half of it whether it was a
semiquaver or a whole bar — so the depth is in octaves of time. Two octaves either way is a
quarter to four times, which covers "very short to double" with room to spare. */
static const float DURATION_OCTAVES = 2.f;
/** Level is multiplied, so full depth is silence to twice as loud. */
static const float LEVEL_DEPTH = 1.f;
/** Pan is added, and the lane runs from minus one to one. */
static const float PAN_DEPTH = 1.f;
/** The longest a note may be held back, in seconds. A note can only ever be made LATE: nothing
can be sent before it happens. */
static const float TIMING_MAX = 0.12f;
/** Detune, in semitones either way. Fifty cents at full, which is as far as a note can go before
it is a different note. */
static const float DETUNE_SEMITONES = 0.5f;

/** Notes whose ends this module owns, and events waiting to be sent late. Both are bounded: a
table that cannot grow is a table that cannot be made to grow by a stuck patch. */
static const int MAX_HELD = 32;
static const int MAX_PENDING = 64;


struct ScatterModule : Module, NoteSource, NoteSink {
	enum ParamId {
		P_LANE,
		P_SHAPE,
		P_AMOUNT,
		P_RATE,
		// APPENDED, NEVER INSERTED — a patch stores a parameter by its position in this list.
		/** Whether every pass of the form is the same, or each differs and the whole
		performance still repeats from the top. */
		P_EVOLVE,
		NUM_PARAMS
	};
	enum InputId {
		I_MPX,
		NUM_INPUTS
	};
	enum OutputId {
		O_MPX,
		NUM_OUTPUTS
	};
	enum LightId {
		L_ACT,
		NUM_LIGHTS
	};

	ScatterModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(P_LANE, 0.f, (float) (NUM_SCATTER_LANES - 1), 0.f, "Lane",
			{SCAT_LANE_NAMES[0], SCAT_LANE_NAMES[1], SCAT_LANE_NAMES[2],
			SCAT_LANE_NAMES[3], SCAT_LANE_NAMES[4]});
		configSwitch(P_SHAPE, 0.f, (float) (NUM_SHAPES - 1), 0.f, "Shape",
			{SHAPE_NAMES[0], SHAPE_NAMES[1], SHAPE_NAMES[2]});
		// NOUGHT IS A BYPASS, exactly. Not nearly: at nought the events that come out are the
		// events that went in, so a module sitting in a chain doing nothing is doing nothing.
		configParam(P_AMOUNT, 0.f, 1.f, 0.f, "Depth", "%", 0.f, 100.f);
		// HOW FAST THE DRIFT MOVES, IN BEATS, not seconds. Seconds would make it unrepeatable —
		// two passes of the same bars are never the same number of samples once the tempo has
		// moved — and would also mean the drift ignored the music it was decorating. One is a
		// cycle per beat; a quarter is a cycle per bar in four four.
		//
		// Meaningless for white, which is said on the panel rather than by disabling the knob:
		// a knob that sometimes does nothing is easier to understand than one that disappears.
		configParam(P_RATE, 0.05f, 8.f, 1.f, "Rate", " cycles per beat");
		configSwitch(P_EVOLVE, 0.f, 1.f, 0.f, "Each pass",
			{"Repeat — the same every time round", "Evolve — different each time round"});
		configInput(I_MPX, "MPX in");
		configOutput(O_MPX, "MPX out");
		slot = busClaim(&generation);
	}

	~ScatterModule() {
		busRelease(slot);
	}

	int busSlotFor(int outputId, uint32_t* gen) override {
		if (outputId != O_MPX)
			return -1;
		if (gen)
			*gen = generation;
		return slot;
	}

	bool isMPXInputId(int inputId) override {
		return inputId == I_MPX;
	}

	// ---- the cables ----

	int slot = -1;
	uint32_t generation = 0;
	BusReader reader;

	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
	std::atomic<bool> relink{false};

	void link(const int* slots, const uint32_t* generations, int n) {
		bool same = (n == wantCount.load());
		for (int i = 0; same && i < n; i++) {
			same = (slots[i] == wantSlots[i].load())
				&& (generations[i] == wantGenerations[i].load());
		}
		if (same)
			return;
		for (int i = 0; i < n && i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(slots[i]);
			wantGenerations[i].store(generations[i]);
		}
		wantCount.store(std::min(n, MAX_UPSTREAM));
		relink.store(true);
	}

	// ---- the generator ----

	/** A FUNCTION OF WHERE WE ARE, NOT OF HOW LONG WE HAVE BEEN RUNNING.

	This started as a generator that stepped every sample against the clock, which is the obvious
	way and cannot be reproduced. Two passes of the same eight bars are never the same number of
	samples once the tempo has moved at all, so a rewind put the music back and left the
	randomness wherever it had got to.

	Every shape here is instead a pure function of the seed and the musical position, so playing
	the same bar twice gives the same numbers by construction. There is nothing to reseed and no
	state to put back: rewinding the chart rewinds the randomness because the randomness never
	depended on anything else. */
	static uint32_t hashU(uint32_t x) {
		x ^= x >> 16; x *= 0x7feb352du;
		x ^= x >> 15; x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}

	/** Two numbers in, one value from minus one to one out, with no state of any kind. */
	static float hashF(uint32_t a, uint32_t b) {
		const uint32_t h = hashU(a ^ hashU(b));
		return (float) (h >> 8) / 8388608.f - 1.f;
	}

	/** Value noise along a line: the two nearest whole positions, smoothstepped between. Flat
	where it meets each end, so there is no corner where one span joins the next. */
	static float smoothNoise(uint32_t seed, float t) {
		const float base = std::floor(t);
		const uint32_t i = (uint32_t) (int32_t) base;
		const float f = t - base;
		const float a = hashF(seed, i);
		const float b = hashF(seed, i + 1u);
		const float w = f * f * (3.f - 2.f * f);
		return a + (b - a) * w;
	}

	/** The seed this instance is working from: the chart's, and on the evolving setting the
	pass number folded in, so each time round the form differs and the whole performance still
	repeats from the top. */
	uint32_t seedNow(const Harmony& h, bool evolve) const {
		return evolve ? hashU(h.seed ^ (h.epoch * 0x9e3779b9u)) : hashU(h.seed);
	}

	/** WHAT THE GENERATOR SAYS AT THIS POINT IN THE MUSIC.

	White is per NOTE rather than per position, so the notes of one chord scatter against each
	other rather than all taking the same value. The other two are per position, so a chord
	drifts as a body — which is the difference between jitter and a hand moving. */
	float sample(int shape, uint32_t seed, double beat, float rate, uint32_t noteIndex) {
		if (shape == SHAPE_WHITE)
			return hashF(seed, noteIndex);

		const float t = (float) (beat * (double) rate);
		if (shape == SHAPE_PERLIN)
			return smoothNoise(seed, t);

		// A WALK, WITHOUT WALKING. Brown noise is a wander dominated by its slowest components,
		// and summing octaves of smooth noise with the weight falling as fast as the frequency
		// rises is that same shape — got by looking it up rather than by accumulating, so it can
		// be asked for any position at any time and gives the same answer.
		float v = 0.f, amp = 1.f, total = 0.f, f = 1.f;
		for (int o = 0; o < 4; o++) {
			v += smoothNoise(seed + (uint32_t) o * 7919u, t * f) * amp;
			total += amp;
			amp *= 0.5f;
			f *= 2.f;
		}
		return math::clamp(v / total, -1.f, 1.f);
	}

	// ---- notes whose ends we own, and events held back ----

	struct Held {
		int64_t handle = 0;
		float endIn = 0.f;
	};
	Held held[MAX_HELD];

	struct Pending {
		Event e;
		float in = 0.f;
		bool used = false;
	};
	Pending pending[MAX_PENDING];

	/** Delays given to notes on the timing lane, so an update or an off for a note that was held
	back is held back by the same amount and does not overtake it. */
	struct Late {
		int64_t handle = 0;
		float by = 0.f;
	};
	Late late[MAX_HELD];

	float lightFade = 0.f;

	/** WHICH NOTE THIS IS SINCE THE MUSIC LAST WENT BACK TO THE TOP. White scatter is a function
	of it, so the same note of the same pass gets the same value however the tempo has moved.
	Counting from the rewind rather than from the module being placed is what makes it repeat. */
	uint32_t noteIndex = 0;
	uint32_t lastEpoch = 0;
	bool haveEpoch = false;

	int findHeld(int64_t handle) {
		for (int i = 0; i < MAX_HELD; i++) {
			if (held[i].handle == handle)
				return i;
		}
		return -1;
	}

	bool adopt(int64_t handle, float seconds) {
		for (int i = 0; i < MAX_HELD; i++) {
			if (held[i].handle == 0) {
				held[i].handle = handle;
				held[i].endIn = seconds;
				return true;
			}
		}
		// FULL, so this note is not adopted and its own note-off is left to end it. A table that
		// cannot grow has to say no, and saying no here costs one note its scatter rather than
		// costing a voice its ending.
		return false;
	}

	float lateFor(int64_t handle) {
		for (int i = 0; i < MAX_HELD; i++) {
			if (late[i].handle == handle)
				return late[i].by;
		}
		return -1.f;
	}

	void setLate(int64_t handle, float by) {
		for (int i = 0; i < MAX_HELD; i++) {
			if (late[i].handle == 0 || late[i].handle == handle) {
				late[i].handle = handle;
				late[i].by = by;
				return;
			}
		}
	}

	void clearLate(int64_t handle) {
		for (int i = 0; i < MAX_HELD; i++) {
			if (late[i].handle == handle)
				late[i].handle = 0;
		}
	}

	void queue(const Event& e, float in) {
		for (int i = 0; i < MAX_PENDING; i++) {
			if (!pending[i].used) {
				pending[i].used = true;
				pending[i].e = e;
				pending[i].in = in;
				return;
			}
		}
		// Nowhere to hold it: send it now rather than lose it. A note out of time is a fault you
		// can hear and correct; a note that never arrives is one you cannot.
		if (slot >= 0)
			busPush(slot, e);
	}

	void send(const Event& e) {
		if (slot >= 0)
			busPush(slot, e);
	}

	/** Everything due this sample: notes we are ending, and events we held back. */
	void tick(float dt) {
		for (int i = 0; i < MAX_HELD; i++) {
			if (held[i].handle == 0)
				continue;
			held[i].endIn -= dt;
			if (held[i].endIn <= 0.f) {
				Event off;
				off.kind = Event::OFF;
				off.handle = held[i].handle;
				send(off);
				held[i].handle = 0;
			}
		}
		for (int i = 0; i < MAX_PENDING; i++) {
			if (!pending[i].used)
				continue;
			pending[i].in -= dt;
			if (pending[i].in <= 0.f) {
				pending[i].used = false;
				send(pending[i].e);
				if (pending[i].e.kind == Event::OFF)
					clearLate(pending[i].e.handle);
			}
		}
	}

	/** ONE EVENT, on its way through. */
	void pass(Event e, int lane, float amount, float r) {
		const bool isOn = (e.kind == Event::ON);

		// A DEPTH OF NOUGHT IS A WIRE. Nothing is sampled, nothing is adopted, nothing is held —
		// so a module left in a chain at rest cannot change the timing of anything by being there.
		if (amount <= 0.f) {
			send(e);
			return;
		}

		switch (lane) {
			case SCAT_DURATION: {
				if (isOn) {
					e.duration = math::clamp(
						e.duration * std::pow(2.f, r * amount * DURATION_OCTAVES),
						0.005f, 30.f);
					send(e);
					// OURS TO END NOW. The source's off for this handle is swallowed below.
					if (adopt(e.handle, e.duration))
						lightFade = 1.f;
					return;
				}
				if (e.kind == Event::OFF) {
					const int at = findHeld(e.handle);
					if (at >= 0)
						return;   // swallowed: our own clock ends this one
				}
				send(e);
				return;
			}
			case SCAT_TIMING: {
				if (isOn) {
					// LATE ONLY. Half of the generator's range would be early, and nothing can
					// be sent before it happens, so the range is folded rather than clipped —
					// clipping would make half of every setting do nothing at all.
					const float by = std::fabs(r) * amount * TIMING_MAX;
					setLate(e.handle, by);
					queue(e, by);
					lightFade = 1.f;
					return;
				}
				const float by = lateFor(e.handle);
				if (by > 0.f) {
					queue(e, by);
					return;
				}
				send(e);
				return;
			}
			default: {
				if (!isOn) {
					send(e);
					return;
				}
				if (lane == SCAT_LEVEL)
					e.level = math::clamp(e.level * (1.f + r * amount * LEVEL_DEPTH), 0.f, 1.f);
				else if (lane == SCAT_PAN)
					e.pan = math::clamp(e.pan + r * amount * PAN_DEPTH, -1.f, 1.f);
				else if (lane == SCAT_DETUNE)
					e.pitch += r * amount * DETUNE_SEMITONES / 12.f;
				send(e);
				lightFade = 1.f;
				return;
			}
		}
	}

	void process(const ProcessArgs& args) override {
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}

		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(0.f);

		const int lane = (int) std::round(params[P_LANE].getValue());
		const int shape = (int) std::round(params[P_SHAPE].getValue());
		const float amount = params[P_AMOUNT].getValue();
		const float rate = params[P_RATE].getValue();
		const bool evolve = params[P_EVOLVE].getValue() > 0.5f;

		// WHERE WE ARE IN THE MUSIC, which is what the generator is a function of. With no
		// harmony on the cable there is no position and no seed, and the scatter falls back to
		// counting notes — which still varies, and still cannot repeat, because nothing is
		// telling it when the top of the form is.
		Harmony h;
		const bool haveHarmony = reader.harmony(h) && h.valid;
		if (haveHarmony) {
			if (!haveEpoch || h.epoch != lastEpoch) {
				haveEpoch = true;
				lastEpoch = h.epoch;
				noteIndex = 0;
			}
		}
		const uint32_t seed = haveHarmony ? seedNow(h, evolve) : 0x5bd1e995u;
		const double beat = haveHarmony ? h.beat : 0.0;

		Event e;
		while (reader.next(e)) {
			// SAMPLED ONCE PER NOTE, here, so the note counter advances for every note whether
			// or not this lane happens to use it.
			float r = 0.f;
			if (e.kind == Event::ON) {
				r = sample(shape, seed, beat, rate, noteIndex);
				noteIndex++;
			}
			pass(e, lane, amount, r);
		}

		tick(args.sampleTime);

		// The harmony goes through whole and untouched — the seed and the pass number with it, so
		// a row of these all work from the same number and go back to the top together. This
		// module has an opinion about notes and none whatever about the chart.
		if (slot >= 0 && haveHarmony)
			busPublishHarmony(slot, h);

		if (lightFade > 0.f)
			lightFade = std::fmax(0.f, lightFade - args.sampleTime * 6.f);
		lights[L_ACT].setBrightness(lightFade);
	}
};


static Layout scatterLayout() {
	Layout L;
	L.hp = 6.f;
	L.title = "mpxScatter";
	L.titleAbove = "DREAMER DEVELOPMENT";

	static const float NAME_HALF = 1.22f;
	static const float KNOB_EDGE = 3.84f;   /**< RoundSmallBlackKnob is 7.68 mm across. */
	static const float PORT_EDGE = 4.01f;
	static const float GAP = 2.f;

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	auto knob = [&](const std::string& key, float x, float y, int id, const std::string& name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "knob.small";
		i.ticks = 2;
		L.items.push_back(i);
		label(key + ".label", x, y + KNOB_EDGE + GAP + NAME_HALF, name, Panel::CENTRE, true,
			0.f, key);
	};
	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = NOTE_CABLE;
		L.items.push_back(i);
		label(key + ".label", x, y + PORT_EDGE + GAP + 0.98f, name, Panel::CENTRE, false, 7.f,
			key);
	};
	auto radio = [&](const std::string& key, float x, float y, int id, const std::string& group,
			const std::vector<std::string>& names, float pitch) {
		label(key + ".group", x, y - GAP - NAME_HALF, group, Panel::LEFT, true, 0.f, key);
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "lamps";
		i.names = names; i.horizontal = false; i.pitch = pitch;
		i.labelSide = Panel::RIGHT;
		L.items.push_back(i);
	};

	// SIX HP IS 30.48 MM. Narrow enough to put four of them in a row without thinking about it,
	// wide enough that the lane names are words rather than abbreviations.
	radio("p.lane", 3.f, 24.f, ScatterModule::P_LANE, "LANE",
		{"DUR", "LEVEL", "PAN", "TIME", "TUNE"}, 4.6f);
	radio("p.shape", 3.f, 54.f, ScatterModule::P_SHAPE, "SHAPE",
		{"WHITE", "WALK", "PERLIN"}, 4.6f);
	// EACH PASS OF THE FORM: the same again, or different. Beside the shape because it is a
	// question about the same thing — what the randomness does over time rather than within a
	// bar — and next to nothing else on the panel.
	radio("p.evolve", 3.f, 76.f, ScatterModule::P_EVOLVE, "PASS",
		{"REPEAT", "EVOLVE"}, 4.6f);

	knob("p.amount", 15.2f, 94.f, ScatterModule::P_AMOUNT, "DEPTH");
	knob("p.rate", 15.2f, 108.f, ScatterModule::P_RATE, "RATE");

	jack("in.mpx", Item::PORT_IN, 8.f, 118.f, ScatterModule::I_MPX, "mpx\nIN");
	jack("out.mpx", Item::PORT_OUT, 22.4f, 118.f, ScatterModule::O_MPX, "mpx\nOUT");

	Item lamp;
	lamp.key = "lamp.act"; lamp.kind = Item::LIGHT; lamp.id = ScatterModule::L_ACT;
	lamp.x = 26.f; lamp.y = 70.f;
	L.items.push_back(lamp);

	L.bindOffsets();
	return L;
}


struct ScatterWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	ScatterWidget(ScatterModule* module) {
		setModule(module);
		layout = scatterLayout();
		layoutApplyUser("mpxScatter", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxScatter");
	}

	void step() override {
		ModuleWidget::step();
		ScatterModule* s = dynamic_cast<ScatterModule*>(module);
		if (!s)
			return;
		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(ScatterModule::I_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				if (n >= MAX_UPSTREAM)
					break;
				engine::Cable* cable = cw->getCable();
				if (!cable)
					continue;
				uint32_t g = 0;
				const int sl = noteBusOf(cable->outputModule, cable->outputId, &g);
				if (sl < 0)
					continue;
				cw->color = NOTE_CABLE;
				slots[n] = sl;
				generations[n] = g;
				n++;
			}
		}
		s->link(slots, generations, n);

		if (PortWidget* out = getOutput(ScatterModule::O_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(out)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}
};


} // namespace px


Model* modelMpxScatter = createModel<px::ScatterModule, px::ScatterWidget>("mpxScatter");
