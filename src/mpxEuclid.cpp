#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

#include <cmath>

namespace px {


static const int VOICES = 4;
/** How many values a wander is built from. The loop uses as many of these as it needs. */
static const int NOISE_POINTS = 256;


/** A wandering value, built from a fixed table of random points read at a position that moves
with the beat and wraps.

WHY NOISE RATHER THAN A RANDOM NUMBER PER HIT. A velocity drawn afresh every time sounds like
one machine being random. A velocity CORRELATED WITH ITS OWN PREVIOUS VALUE sounds like somebody
playing — and four such wanderings, independent of each other, sound like four people. That
correlation is the whole of what makes a stream read as a single entity over time.

Which is why each voice owns its own table. Share one between them and they swell and fade
together, and the four collapse back into one thing with dynamics on it.

AND IT REPEATS. The position wraps after a set number of beats, so the wander is a phrase rather
than a drift — the same shape comes round again, which is what makes it music rather than
weather. */
struct Wander {
	float points[NOISE_POINTS];

	void seed(uint32_t s) {
		// A small deterministic generator: the same seed gives the same wander every session,
		// so a patch sounds like itself when it is opened again.
		uint32_t x = s * 2654435761u + 1013904223u;
		for (int i = 0; i < NOISE_POINTS; i++) {
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			points[i] = (x >> 8) / 16777216.f;
		}
	}

	/** `pos` is in points, and `len` is how many of them the loop uses. */
	float at(float pos, int len) const {
		len = clamp(len, 2, NOISE_POINTS);
		float p = std::fmod(pos, (float) len);
		if (p < 0.f)
			p += len;
		const int i = (int) p;
		const float f = p - i;
		// Smoothed rather than straight, so the value has no corners at the points: what is
		// wanted is a curve through them, not a path between them.
		const float t = f * f * (3.f - 2.f * f);
		return points[i] * (1.f - t) + points[(i + 1) % len] * t;
	}
};


struct EuclidModule : Module, NoteSource {
	enum ParamId {
		P_STEPS1, P_PULSES1 = P_STEPS1 + VOICES, P_OFFSET1 = P_PULSES1 + VOICES,
		P_DIVIDE1 = P_OFFSET1 + VOICES,
		P_LEVEL = P_DIVIDE1 + VOICES,
		P_MOVE,
		P_LENGTH,
		P_WEIGHT,
		P_DRIFT,
		P_LOOP,
		P_TEMPO,
		NUM_PARAMS
	};
	enum InputId {
		I_CLOCK,
		I_RESET,
		NUM_INPUTS
	};
	enum OutputId {
		O_VOICE,
		NUM_OUTPUTS
	};
	enum LightId {
		L_VOICE1,
		NUM_LIGHTS = L_VOICE1 + VOICES
	};

	struct Voice {
		Wander velocity, duration;
		/** Which clock this lane is on, since it may be running slower than the master. */
		int divided = 0;
		int step = 0;
		bool on = false;
		int64_t handle = 0;
		int age = 0, len = 0;
	};
	Voice voices[VOICES];

	int slot = -1;
	uint32_t generation = 0;
	/** Master pulses since the last reset, which is what every wander is positioned by — so the
	four repeat together rather than each on its own schedule. */
	int64_t pulses = 0;
	dsp::SchmittTrigger clockTrigger, resetTrigger;
	/** For the internal clock, which runs when nothing is patched into CLOCK. */
	float internalPhase = 0.f;
	float lightFade[VOICES] = {};

	EuclidModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int v = 0; v < VOICES; v++) {
			configParam(P_STEPS1 + v, 1.f, 32.f, 16.f, string::f("Voice %d steps", v + 1));
			paramQuantities[P_STEPS1 + v]->snapEnabled = true;
			configParam(P_PULSES1 + v, 0.f, 32.f, (float) (3 + v), string::f("Voice %d pulses", v + 1));
			paramQuantities[P_PULSES1 + v]->snapEnabled = true;
			configParam(P_OFFSET1 + v, 0.f, 31.f, 0.f, string::f("Voice %d offset", v + 1));
			paramQuantities[P_OFFSET1 + v]->snapEnabled = true;
			configSwitch(P_DIVIDE1 + v, 0.f, 3.f, 0.f, string::f("Voice %d clock", v + 1),
				{"Every beat", "Half speed", "A third", "A quarter"});
			// A seed per lane per voice, so no two wanderings are the same shape.
			voices[v].velocity.seed(v * 2 + 1);
			voices[v].duration.seed(v * 2 + 2);
		}
		configParam(P_LEVEL, 0.f, 1.f, 0.7f, "Level", "%", 0.f, 100.f);
		configParam(P_MOVE, 0.f, 1.f, 0.5f, "How far the level wanders", "%", 0.f, 100.f);
		configParam(P_LENGTH, std::log2(0.01f), std::log2(4.f), std::log2(0.15f),
			"Length", " s", 2.f);
		configParam(P_WEIGHT, 0.f, 1.f, 0.5f, "How much length follows level", "%", 0.f, 100.f);
		// In beats per point of the wander: a large number is a slow drift.
		configParam(P_DRIFT, std::log2(0.25f), std::log2(16.f), std::log2(2.f),
			"Drift", " beats a step", 2.f);
		configParam(P_LOOP, 1.f, 64.f, 16.f, "Repeats after", " beats");
		paramQuantities[P_LOOP]->snapEnabled = true;
		configParam(P_TEMPO, 30.f, 300.f, 120.f, "Tempo (when no clock is patched)", " bpm");

		configInput(I_CLOCK, "Clock");
		configInput(I_RESET, "Reset");
		configOutput(O_VOICE, "MPX note out \u2014 goes to an MPX input only");

		slot = busClaim(&generation);
	}

	~EuclidModule() {
		busRelease(slot);
	}

	int busSlotFor(int outputId, uint32_t* gen) override {
		if (outputId != O_VOICE)
			return -1;
		if (gen)
			*gen = generation;
		return slot;
	}

	void onReset() override {
		pulses = 0;
		for (Voice& v : voices) {
			v.divided = 0;
			v.step = 0;
			v.on = false;
		}
	}

	/** THE EUCLIDEAN TEST, and it is one line. A pattern of K pulses spread as evenly as
	possible over N steps is the set of i where i times K, modulo N, is less than K. It gives
	the same patterns Bjorklund's algorithm does without building them. */
	static bool euclid(int step, int steps, int pulses) {
		if (steps <= 0 || pulses <= 0)
			return false;
		if (pulses >= steps)
			return true;
		return ((int64_t) step * pulses) % steps < pulses;
	}

	void process(const ProcessArgs& args) override {
		// The cable carries no voltage; the notes travel through the bus.
		outputs[O_VOICE].setChannels(1);
		outputs[O_VOICE].setVoltage(0.f);

		if (resetTrigger.process(inputs[I_RESET].getVoltage(), 0.1f, 1.f))
			onReset();

		bool tick = false;
		if (inputs[I_CLOCK].isConnected()) {
			tick = clockTrigger.process(inputs[I_CLOCK].getVoltage(), 0.1f, 1.f);
		}
		else {
			// An internal clock so the module can be listened to on its own. Patch a clock and
			// it stands aside, which is the ordinary normalling convention.
			internalPhase += params[P_TEMPO].getValue() / 60.f * args.sampleTime;
			if (internalPhase >= 1.f) {
				internalPhase -= 1.f;
				tick = true;
			}
		}

		const float level = params[P_LEVEL].getValue();
		const float move = params[P_MOVE].getValue();
		const float length = std::pow(2.f, params[P_LENGTH].getValue());
		const float weight = params[P_WEIGHT].getValue();
		const float drift = std::pow(2.f, params[P_DRIFT].getValue());
		const int loopBeats = (int) std::round(params[P_LOOP].getValue());
		// How many points of the wander the loop covers. Rounded, because a loop has to close
		// on a whole point or it does not repeat exactly.
		const int loopPoints = clamp((int) std::round(loopBeats / drift), 2, NOISE_POINTS);

		if (tick) {
			for (int v = 0; v < VOICES; v++) {
				Voice& voice = voices[v];
				const int divide = (int) std::round(params[P_DIVIDE1 + v].getValue()) + 1;
				// A lane on a divided clock only looks at every second, third or fourth pulse.
				if (voice.divided % divide != 0) {
					voice.divided++;
					continue;
				}
				voice.divided++;

				const int steps = (int) std::round(params[P_STEPS1 + v].getValue());
				const int pulses = (int) std::round(params[P_PULSES1 + v].getValue());
				const int offset = (int) std::round(params[P_OFFSET1 + v].getValue());
				const int step = ((voice.step % steps) + offset) % steps;
				voice.step++;

				if (!euclid(step, steps, pulses))
					continue;

				// EVERY WANDER IS POSITIONED BY THE MASTER BEAT, not by the lane's own divided
				// one, so the four repeat together and the phrase closes on all of them at once.
				const float pos = (float) pulses_position(drift);
				const float nv = voice.velocity.at(pos, loopPoints);
				const float nd = voice.duration.at(pos, loopPoints);

				const float velocity = clamp(level + move * (nv - 0.5f) * 2.f, 0.f, 1.f);
				// HARDER HITS RING LONGER, by however much the weight asks. At nothing the two
				// wander independently; at full the length is the velocity's own shape, which
				// is one instrument being played rather than two things happening at once.
				const float shape = nd * (1.f - weight) + velocity * weight;
				const float seconds = clamp(length * std::pow(4.f, shape - 0.5f), 0.005f, 30.f);

				if (voice.on)
					sendOff(voice);

				Event e;
				e.kind = Event::ON;
				e.handle = voice.handle = mintHandle();
				e.pitch = 0.f;
				e.level = velocity;
				e.duration = seconds;
				e.pan = 0.f;
				e.bendRange = 2.f;
				busPush(slot, e);

				voice.on = true;
				voice.age = 0;
				voice.len = std::max(1, (int) std::round(seconds * args.sampleRate));
				lightFade[v] = 1.f;
			}
			pulses++;
		}

		for (int v = 0; v < VOICES; v++) {
			Voice& voice = voices[v];
			if (voice.on && ++voice.age >= voice.len) {
				sendOff(voice);
				voice.on = false;
			}
			lightFade[v] = std::fmax(0.f, lightFade[v] - args.sampleTime * 6.f);
			lights[L_VOICE1 + v].setBrightness(lightFade[v]);
		}
	}

	/** The wander's position at this moment, in points. */
	double pulses_position(float drift) const {
		return (double) pulses / (double) std::fmax(0.01f, drift);
	}

	void sendOff(const Voice& voice) {
		Event e;
		e.kind = Event::OFF;
		e.handle = voice.handle;
		busPush(slot, e);
	}
};


// ---- panel -------------------------------------------------------------------------------
// A ROW PER VOICE, because a voice is a thing with four settings and reading it across is how
// anybody would describe it out loud: sixteen steps, five pulses, no offset, every beat.
//
// The four settings are named ONCE, as column headings, rather than sixteen times. That only
// works if the columns are exact, so every one of them is a named constant here and no control
// is placed by hand.

static const float LAMP_X = 8.f;      /**< The voice's own lamp, and its number beside it. */
static const float NUM_X = 14.f;
static const float C_STEPS = 26.f;
static const float C_PULSES = 44.f;
static const float C_OFFSET = 62.f;
/** The divider's four lamps, and the row of numbers naming them. */
static const float C_DIV = 76.f;
static const float DIV_PITCH = 6.f;
static const float DIV_FIRST = 78.2f;   /**< C_DIV plus the lamp's own radius. */

static const float HEAD_Y = 16.f;
static const float NUM_Y = 23.f;
static const float ROW_TOP = 33.f;
static const float ROW_PITCH = 14.f;

/** The six that every voice shares, in one row. */
static const float G[6] = {15.f, 29.f, 43.f, 57.f, 71.f, 85.f};
static const float G_Y = 99.f;
static const float JACK_Y = 117.f;

static Layout euclidLayout() {
	Layout L;
	L.hp = 20.f;
	L.title = "mpxEuclid";
	L.titleAbove = "DREAMER DEVELOPMENT";

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	auto knob = [&](const std::string& key, float x, float y, int id,
			const std::string& name, const char* style = "knob") {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = style;
		L.items.push_back(i);
		if (!name.empty())
			label(key + ".label", x, y + 8.f, name, Panel::CENTRE, false, 0.f, key);
	};
	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor color, float size = 0.f) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = color;
		L.items.push_back(i);
		label(key + ".label", x, y + 8.f, name, Panel::CENTRE, size > 0.f, size, key);
	};

	// Named once, at the top of the column they belong to.
	label("h.steps", C_STEPS, HEAD_Y, "STEPS", Panel::CENTRE, true);
	label("h.pulses", C_PULSES, HEAD_Y, "PULSES", Panel::CENTRE, true);
	label("h.offset", C_OFFSET, HEAD_Y, "OFFSET", Panel::CENTRE, true);
	label("h.divide", DIV_FIRST + 1.5f * DIV_PITCH, HEAD_Y, "CLOCK", Panel::CENTRE, true);
	// The divider's positions are the same on every row, so they are named once above the top
	// one rather than four times over.
	for (int d = 0; d < 4; d++)
		label("h.div" + std::to_string(d + 1), DIV_FIRST + d * DIV_PITCH, NUM_Y,
			d == 0 ? "1" : "/" + std::to_string(d + 1), Panel::CENTRE, false, 7.5f);

	for (int v = 0; v < VOICES; v++) {
		const float y = ROW_TOP + v * ROW_PITCH;
		const std::string n = std::to_string(v + 1);

		Item lamp;
		lamp.key = "lamp.voice" + n; lamp.kind = Item::LIGHT;
		lamp.id = EuclidModule::L_VOICE1 + v;
		lamp.x = LAMP_X; lamp.y = y;
		L.items.push_back(lamp);
		label("h.voice" + n, NUM_X, y, n, Panel::RIGHT, true);

		knob("p.steps" + n, C_STEPS, y, EuclidModule::P_STEPS1 + v, "");
		knob("p.pulses" + n, C_PULSES, y, EuclidModule::P_PULSES1 + v, "");
		knob("p.offset" + n, C_OFFSET, y, EuclidModule::P_OFFSET1 + v, "");

		Item div;
		div.key = "p.divide" + n; div.kind = Item::PARAM;
		div.id = EuclidModule::P_DIVIDE1 + v;
		div.style = "lamps"; div.x = C_DIV; div.y = y - 3.f;
		div.w = 6.f; div.h = 6.f; div.pitch = DIV_PITCH; div.horizontal = true;
		div.names = {"", "", "", ""};
		L.items.push_back(div);
	}

	// WHAT EVERY VOICE SHARES. They are independent because their wanders differ, not because
	// their settings do — so these are common ground on purpose.
	label("h.every", 8.f, 88.f, "EVERY VOICE", Panel::LEFT, true);
	knob("p.level", G[0], G_Y, EuclidModule::P_LEVEL, "LEVEL");
	knob("p.move", G[1], G_Y, EuclidModule::P_MOVE, "MOVE");
	knob("p.length", G[2], G_Y, EuclidModule::P_LENGTH, "LENGTH");
	knob("p.weight", G[3], G_Y, EuclidModule::P_WEIGHT, "WEIGHT");
	knob("p.drift", G[4], G_Y, EuclidModule::P_DRIFT, "DRIFT");
	knob("p.loop", G[5], G_Y, EuclidModule::P_LOOP, "REPEAT");

	jack("in.clock", Item::PORT_IN, G[0], JACK_Y, EuclidModule::I_CLOCK, "clock", SIG_GATE);
	jack("in.reset", Item::PORT_IN, G[1], JACK_Y, EuclidModule::I_RESET, "reset", SIG_GATE);
	knob("p.tempo", G[2], JACK_Y, EuclidModule::P_TEMPO, "tempo");
	jack("out.voice", Item::PORT_OUT, G[5], JACK_Y, EuclidModule::O_VOICE, "mpx\nOUT",
		NOTE_CABLE, 12.f);

	L.bindOffsets();
	return L;
}

struct EuclidWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	EuclidWidget(EuclidModule* module) {
		setModule(module);
		layout = euclidLayout();
		layoutApplyUser("mpxEuclid", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxEuclid");
	}

	void step() override {
		ModuleWidget::step();
		if (!module)
			return;
		PortWidget* port = getOutput(EuclidModule::O_VOICE);
		if (!port)
			return;
		for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
			engine::Cable* cable = cw->getCable();
			if (cable && isMPXInput(cable->inputModule, cable->inputId))
				cw->color = NOTE_CABLE;
		}
	}
};


} // namespace px


Model* modelEuclid = createModel<px::EuclidModule, px::EuclidWidget>("mpxEuclid");
