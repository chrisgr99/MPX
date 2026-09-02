#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

#include <cmath>
#include <cstdio>

namespace px {


static const int MAX_STEPS = 16;

struct Step {
	int8_t degree;
	int8_t accidental;
	uint8_t quality;
	float beats;
};

struct Progression {
	const char* name;
	bool minor;
	int count;
	Step steps[MAX_STEPS];
};

#define CH(d, q) {(int8_t) d, 0, (uint8_t) q, 4.f}

/** A dozen progressions worth playing a line against.

CHOSEN, NOT TYPED. An earlier version of this module gave each of eight slots a degree, a
quality and a length — twenty-four knobs to arrive at four chords, with no way to see what you
had until every one was set. The module exists so the melody generator has something to be
played against, and a list of progressions everybody already knows does that with one control.

Stored as degrees, so choosing a key transposes them and nothing is written twice. */
static const Progression PROGRESSIONS[] = {
	{"Two five one", false, 4, {CH(2, Q_MIN7), CH(5, Q_DOM7), CH(1, Q_MAJ7), CH(1, Q_MAJ7)}},
	{"Turnaround", false, 4, {CH(1, Q_MAJ7), CH(6, Q_MIN7), CH(2, Q_MIN7), CH(5, Q_DOM7)}},
	{"Jazz eight", false, 8, {CH(2, Q_MIN7), CH(5, Q_DOM7), CH(1, Q_MAJ7), CH(6, Q_DOM7),
		CH(2, Q_MIN7), CH(5, Q_DOM7), CH(1, Q_MAJ7), CH(1, Q_MAJ7)}},
	{"Four chords", false, 4, {CH(1, Q_MAJOR), CH(5, Q_MAJOR), CH(6, Q_MINOR), CH(4, Q_MAJOR)}},
	{"Sensitive", false, 4, {CH(6, Q_MINOR), CH(4, Q_MAJOR), CH(1, Q_MAJOR), CH(5, Q_MAJOR)}},
	{"Doo wop", false, 4, {CH(1, Q_MAJOR), CH(6, Q_MINOR), CH(4, Q_MAJOR), CH(5, Q_MAJOR)}},
	{"Three chord", false, 4, {CH(1, Q_MAJOR), CH(1, Q_MAJOR), CH(4, Q_MAJOR), CH(5, Q_MAJOR)}},
	{"Canon", false, 8, {CH(1, Q_MAJOR), CH(5, Q_MAJOR), CH(6, Q_MINOR), CH(3, Q_MINOR),
		CH(4, Q_MAJOR), CH(1, Q_MAJOR), CH(4, Q_MAJOR), CH(5, Q_MAJOR)}},
	{"Twelve bar blues", false, 12, {CH(1, Q_DOM7), CH(1, Q_DOM7), CH(1, Q_DOM7), CH(1, Q_DOM7),
		CH(4, Q_DOM7), CH(4, Q_DOM7), CH(1, Q_DOM7), CH(1, Q_DOM7),
		CH(5, Q_DOM7), CH(4, Q_DOM7), CH(1, Q_DOM7), CH(5, Q_DOM7)}},
	{"Minor two five one", true, 4,
		{CH(2, Q_HALFDIM), CH(5, Q_DOM7), CH(1, Q_MINOR), CH(1, Q_MINOR)}},
	{"Andalusian", true, 4, {CH(1, Q_MINOR), CH(7, Q_MAJOR), CH(6, Q_MAJOR), CH(5, Q_MAJOR)}},
	{"Dorian vamp", true, 2, {{1, 0, Q_MIN7, 8.f}, {4, 0, Q_DOM7, 8.f}}},
};

#undef CH

static const int NUM_PROGRESSIONS = (int) (sizeof(PROGRESSIONS) / sizeof(PROGRESSIONS[0]));


struct ProgressionModule : Module, NoteSource {
	enum ParamId {
		P_WHICH,
		P_KEY,
		P_TEMPO,
		NUM_PARAMS
	};
	enum InputId {
		I_CLOCK,
		I_RESET,
		NUM_INPUTS
	};
	enum OutputId {
		O_MPX,
		NUM_OUTPUTS
	};
	enum LightId {
		L_BEAT,
		NUM_LIGHTS
	};

	int slot = -1;
	uint32_t generation = 0;
	/** Beats since the last reset. Fractional, so the position within a beat is known between
	clock pulses and beatsToNext counts down smoothly rather than in steps. */
	double beats = 0.0;
	dsp::SchmittTrigger clockTrigger, resetTrigger;
	/** Seconds a beat lasts, measured from the clock so the position can advance between
	pulses. The tempo knob supplies it until a clock is patched. */
	float beatSeconds = 0.5f;
	float sinceLastPulse = 0.f;
	/** For the display, which runs on the other thread. */
	std::atomic<int> playing{0};

	ProgressionModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_WHICH, 0.f, (float) (NUM_PROGRESSIONS - 1), 0.f, "Progression");
		paramQuantities[P_WHICH]->snapEnabled = true;
		configParam(P_KEY, 0.f, 11.f, 0.f, "Key");
		paramQuantities[P_KEY]->snapEnabled = true;
		configParam(P_TEMPO, 30.f, 300.f, 100.f, "Tempo (when no clock is patched)", " bpm");

		configInput(I_CLOCK, "Clock");
		configInput(I_RESET, "Reset");
		configOutput(O_MPX, "MPX note out");

		slot = busClaim(&generation);
	}

	~ProgressionModule() {
		busRelease(slot);
	}

	int busSlotFor(int outputId, uint32_t* gen) override {
		if (outputId != O_MPX)
			return -1;
		if (gen)
			*gen = generation;
		return slot;
	}

	void onReset() override {
		beats = 0.0;
	}

	int which() {
		return clamp((int) std::round(params[P_WHICH].getValue()), 0, NUM_PROGRESSIONS - 1);
	}

	Key currentKey() {
		Key k;
		k.tonic = (int8_t) std::round(params[P_KEY].getValue());
		k.minor = PROGRESSIONS[which()].minor;
		return k;
	}

	void process(const ProcessArgs& args) override {
		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(0.f);
		if (slot < 0)
			return;

		if (resetTrigger.process(inputs[I_RESET].getVoltage(), 0.1f, 1.f))
			beats = 0.0;

		// THE POSITION MOVES CONTINUOUSLY, not in steps. A chord change is a moment, and a
		// module asking how many beats are left wants an answer that counts down rather than
		// one that stays put until the next pulse.
		sinceLastPulse += args.sampleTime;
		if (inputs[I_CLOCK].isConnected()) {
			if (clockTrigger.process(inputs[I_CLOCK].getVoltage(), 0.1f, 1.f)) {
				if (sinceLastPulse > 0.001f && sinceLastPulse < 10.f)
					beatSeconds = sinceLastPulse;
				sinceLastPulse = 0.f;
				beats = std::floor(beats) + 1.0;
			}
			else if (beatSeconds > 0.f) {
				beats += args.sampleTime / beatSeconds;
			}
		}
		else {
			beatSeconds = 60.f / std::fmax(1.f, params[P_TEMPO].getValue());
			beats += args.sampleTime / beatSeconds;
		}

		const Progression& p = PROGRESSIONS[which()];
		float cycle = 0.f;
		for (int i = 0; i < p.count; i++)
			cycle += p.steps[i].beats;

		const double pos = (cycle > 0.f)
			? std::fmod(std::fmod(beats, (double) cycle) + cycle, (double) cycle) : 0.0;

		int at = 0;
		float start = 0.f;
		for (int i = 0; i < p.count; i++) {
			if (pos < start + p.steps[i].beats) {
				at = i;
				break;
			}
			start += p.steps[i].beats;
		}

		Harmony h;
		h.valid = true;
		h.key = currentKey();
		h.current = chordOf(p, at);
		h.next = chordOf(p, (at + 1) % p.count);
		h.after = chordOf(p, (at + 2) % p.count);
		h.beatsToNext = (float) (start + p.steps[at].beats - pos);
		h.beat = pos;
		h.cycleBeats = cycle;
		h.barBeats = 4;
		h.barUnit = 4;
		h.bar = (int) (pos / 4.0);
		h.beatInBar = (float) (pos - h.bar * 4.0);
		busPublishHarmony(slot, h);

		playing.store(at);
		// A pulse each beat, so the panel shows it running with no cable patched.
		lights[L_BEAT].setBrightness(std::fmod(pos, 1.0) < 0.25 ? 1.f : 0.f);
	}

	static Chord chordOf(const Progression& p, int i) {
		Chord c;
		c.valid = true;
		c.degree = p.steps[i].degree;
		c.accidental = p.steps[i].accidental;
		c.quality = p.steps[i].quality;
		return c;
	}
};


/** The progression as it would be written: its name, then its chords in bars, with the one
sounding picked out. */
struct ProgressionDisplay : widget::Widget {
	ProgressionModule* module = NULL;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> body =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		std::shared_ptr<window::Font> face =
			APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
		if (!body || body->handle < 0)
			return;

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, nvgRGB(0x12, 0x15, 0x1a));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x35, 0x3c, 0x47));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		const int index = module ? module->which() : 0;
		const Progression& p = PROGRESSIONS[index];
		Key key;
		if (module)
			key = module->currentKey();
		else
			key.minor = p.minor;
		const int at = module ? module->playing.load() : -1;

		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFontFaceId(args.vg, (face && face->handle >= 0) ? face->handle : body->handle);
		nvgFontSize(args.vg, 13.f);
		nvgFillColor(args.vg, PANEL_INK);
		nvgText(args.vg, 7.f, 14.f, p.name, NULL);

		nvgFontFaceId(args.vg, body->handle);
		nvgFontSize(args.vg, 9.f);
		nvgFillColor(args.vg, PANEL_DIM);
		char head[64];
		std::snprintf(head, sizeof(head), "%s %s", pitchClassName(key.tonic),
			p.minor ? "minor" : "major");
		nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		nvgText(args.vg, box.size.x - 7.f, 14.f, head, NULL);

		// FOUR TO A LINE, which is how a chart is written and how a phrase is counted.
		const float left = 7.f;
		const float width = (box.size.x - 14.f) / 4.f;
		const float top = 36.f;
		const float lineHeight = 24.f;
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < p.count; i++) {
			const int row = i / 4;
			const int col = i % 4;
			const float x = left + col * width;
			const float y = top + row * lineHeight;
			const Chord c = ProgressionModule::chordOf(p, i);

			if (i == at) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, x + 1.f, y - 10.f, width - 2.f, 20.f, 3.f);
				nvgFillColor(args.vg, nvgRGBA(0xff, 0x3c, 0xc8, 0x30));
				nvgFill(args.vg);
			}

			// A barline before each cell, so they read as bars rather than as a list.
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, x, y - 10.f);
			nvgLineTo(args.vg, x, y + 10.f);
			nvgStrokeColor(args.vg, nvgRGB(0x3a, 0x41, 0x4c));
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);

			nvgFontSize(args.vg, 12.f);
			nvgFillColor(args.vg, i == at ? nvgRGB(0xff, 0x3c, 0xc8) : PANEL_INK);
			nvgText(args.vg, x + width / 2.f, y - 3.f, chordLetter(c, key).c_str(), NULL);

			nvgFontSize(args.vg, 8.f);
			nvgFillColor(args.vg, PANEL_DIM);
			nvgText(args.vg, x + width / 2.f, y + 8.f, chordRoman(c).c_str(), NULL);

			if (col == 3 || i == p.count - 1) {
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, x + width, y - 10.f);
				nvgLineTo(args.vg, x + width, y + 10.f);
				nvgStrokeColor(args.vg, nvgRGB(0x3a, 0x41, 0x4c));
				nvgStroke(args.vg);
			}
		}
	}
};


// ---- panel -------------------------------------------------------------------------------

static Layout progressionLayout() {
	Layout L;
	L.hp = 16.f;
	L.title = "mpxProgression";
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
		label(key + ".label", x, y + 9.f, name, Panel::CENTRE, true, 0.f, key);
	};
	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor color, float size = 0.f) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = color;
		L.items.push_back(i);
		label(key + ".label", x, y + 8.f, name, Panel::CENTRE, size > 0.f, size, key);
	};

	knob("p.which", 18.f, 95.f, ProgressionModule::P_WHICH, "PROGRESSION", "knob.large");
	knob("p.key", 48.f, 95.f, ProgressionModule::P_KEY, "KEY");
	knob("p.tempo", 68.f, 95.f, ProgressionModule::P_TEMPO, "TEMPO");
	label("h.drive", 40.f, 106.f, "TEMPO drives it until a clock is patched",
		Panel::CENTRE, false, 7.f);

	jack("in.clock", Item::PORT_IN, 12.f, 116.f, ProgressionModule::I_CLOCK, "clock", SIG_GATE);
	jack("in.reset", Item::PORT_IN, 28.f, 116.f, ProgressionModule::I_RESET, "reset", SIG_GATE);
	jack("out.mpx", Item::PORT_OUT, 68.f, 116.f, ProgressionModule::O_MPX, "mpxOut",
		NOTE_CABLE, 12.f);

	Item lamp;
	lamp.key = "lamp.beat"; lamp.kind = Item::LIGHT; lamp.id = ProgressionModule::L_BEAT;
	lamp.x = 45.f; lamp.y = 116.f;
	L.items.push_back(lamp);

	L.bindOffsets();
	return L;
}

struct ProgressionWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	ProgressionWidget(ProgressionModule* module) {
		setModule(module);
		layout = progressionLayout();
		layoutApplyUser("mpxProgression", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);

		ProgressionDisplay* display = new ProgressionDisplay;
		display->module = module;
		display->box.pos = mm2px(math::Vec(3.f, 13.f));
		display->box.size = mm2px(math::Vec(75.f, 68.f));
		addChild(display);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxProgression");
	}

	void step() override {
		ModuleWidget::step();
		if (!module)
			return;
		PortWidget* port = getOutput(ProgressionModule::O_MPX);
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


Model* modelProgression = createModel<px::ProgressionModule, px::ProgressionWidget>("mpxProgression");
