#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

#include <cmath>

namespace px {


static const int SLOTS = 8;


/** A short chord progression, set on the panel, published as the harmony on an MPX cable.

NOT A CHART. It has no repeats, no sections, no endings and no import — those are what mpxChart
will be. This exists so that harmony-on-the-cable can be built and heard end to end before any
of that is written, and so the melody generator has something to be played against.

A slot whose length is zero is not in the progression, so the cycle is as long as the slots that
are set rather than always eight of them. */
struct ProgressionModule : Module, NoteSource {
	enum ParamId {
		P_KEY,
		P_MODE,
		P_TEMPO,
		P_DEGREE,
		P_QUALITY = P_DEGREE + SLOTS,
		P_BEATS = P_QUALITY + SLOTS,
		NUM_PARAMS = P_BEATS + SLOTS
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
		L_SLOT,
		NUM_LIGHTS = L_SLOT + SLOTS
	};

	int slot = -1;
	uint32_t generation = 0;
	/** Beats since the last reset. Fractional, so the position within a beat is known between
	clock pulses and beatsToNext counts down smoothly rather than in steps. */
	double beats = 0.0;
	dsp::SchmittTrigger clockTrigger, resetTrigger;
	float internalPhase = 0.f;
	/** Seconds a beat lasts, measured from the clock, so the position can advance between
	pulses. Falls back to the tempo knob until two pulses have been seen. */
	float beatSeconds = 0.5f;
	float sinceLastPulse = 0.f;

	ProgressionModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_KEY, 0.f, 11.f, 0.f, "Key");
		paramQuantities[P_KEY]->snapEnabled = true;
		configSwitch(P_MODE, 0.f, 1.f, 0.f, "Mode", {"Major", "Minor"});
		configParam(P_TEMPO, 30.f, 300.f, 120.f, "Tempo (when no clock is patched)", " bpm");

		// A two-five-one and a turnaround, so the module makes sense the moment it is placed.
		const int degree[SLOTS] = {1, 6, 2, 5, 1, 6, 2, 5};
		const int quality[SLOTS] = {Q_MAJ7, Q_MIN7, Q_MIN7, Q_DOM7, Q_MAJ7, Q_MIN7, Q_MIN7, Q_DOM7};
		for (int i = 0; i < SLOTS; i++) {
			configParam(P_DEGREE + i, 1.f, 7.f, (float) degree[i],
				string::f("Slot %d degree", i + 1));
			paramQuantities[P_DEGREE + i]->snapEnabled = true;
			configParam(P_QUALITY + i, 0.f, (float) (NUM_QUALITIES - 1), (float) quality[i],
				string::f("Slot %d quality", i + 1));
			paramQuantities[P_QUALITY + i]->snapEnabled = true;
			configParam(P_BEATS + i, 0.f, 16.f, 4.f, string::f("Slot %d beats", i + 1));
			paramQuantities[P_BEATS + i]->snapEnabled = true;
		}

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

	Chord chordAt(int i) {
		Chord c;
		c.valid = true;
		c.degree = (int8_t) std::round(params[P_DEGREE + i].getValue());
		c.quality = (uint8_t) std::round(params[P_QUALITY + i].getValue());
		return c;
	}

	float beatsAt(int i) {
		return std::round(params[P_BEATS + i].getValue());
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
				// The interval between the last two pulses is what a beat is worth, so the
				// position can be interpolated between them.
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

		float cycle = 0.f;
		for (int i = 0; i < SLOTS; i++)
			cycle += beatsAt(i);

		Harmony h;
		h.key.tonic = (int8_t) std::round(params[P_KEY].getValue());
		h.key.minor = params[P_MODE].getValue() > 0.5f;
		h.barBeats = 4;
		h.barUnit = 4;
		h.cycleBeats = cycle;

		if (cycle <= 0.f) {
			// Nothing set: still publish, so a module downstream knows the key and the beat
			// even when there are no chords to speak of.
			h.valid = true;
			h.beat = std::fmod(beats, 4.0);
			h.bar = 0;
			h.beatInBar = (float) h.beat;
			busPublishHarmony(slot, h);
			for (int i = 0; i < SLOTS; i++)
				lights[L_SLOT + i].setBrightness(0.f);
			return;
		}

		const double pos = std::fmod(std::fmod(beats, (double) cycle) + cycle, (double) cycle);

		// Which slot is sounding, and the two after it. Slots of no length are stepped over
		// rather than treated as silence, so shortening one to nothing removes it.
		int order[SLOTS];
		int n = 0;
		for (int i = 0; i < SLOTS; i++) {
			if (beatsAt(i) > 0.f)
				order[n++] = i;
		}

		int at = 0;
		float start = 0.f;
		for (int k = 0; k < n; k++) {
			const float len = beatsAt(order[k]);
			if (pos < start + len) {
				at = k;
				break;
			}
			start += len;
		}

		h.valid = true;
		h.current = chordAt(order[at]);
		h.next = chordAt(order[(at + 1) % n]);
		h.after = chordAt(order[(at + 2) % n]);
		h.beatsToNext = (float) (start + beatsAt(order[at]) - pos);
		h.beat = pos;
		h.bar = (int) (pos / h.barBeats);
		h.beatInBar = (float) (pos - h.bar * h.barBeats);
		busPublishHarmony(slot, h);

		for (int i = 0; i < SLOTS; i++)
			lights[L_SLOT + i].setBrightness(i == order[at] ? 1.f : 0.f);
	}
};


/** The chord each row is set to, written out, because two knob positions are not a chord. */
struct ProgressionDisplay : widget::Widget {
	ProgressionModule* module = NULL;
	float rowTop = 0.f, rowPitch = 0.f;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 11.f);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

		Key key;
		if (module) {
			key.tonic = (int8_t) std::round(module->params[ProgressionModule::P_KEY].getValue());
			key.minor = module->params[ProgressionModule::P_MODE].getValue() > 0.5f;
		}

		for (int i = 0; i < SLOTS; i++) {
			const float y = rowTop + i * rowPitch;
			if (!module) {
				nvgFillColor(args.vg, PANEL_DIM);
				nvgText(args.vg, 0.f, y, "Imaj7", NULL);
				continue;
			}
			const bool unused = module->beatsAt(i) <= 0.f;
			const Chord c = module->chordAt(i);
			// The degree and the letter both: the degree is what the rules work on, and the
			// letter is what anybody reads.
			const std::string text = chordRoman(c) + "   " + chordLetter(c, key);
			nvgFillColor(args.vg, unused ? nvgRGB(0x4a, 0x52, 0x5e) : PANEL_INK);
			nvgText(args.vg, 0.f, y, text.c_str(), NULL);
		}
	}
};


// ---- panel -------------------------------------------------------------------------------

static const float PC_DEG = 13.f, PC_QUAL = 27.f, PC_BEATS = 41.f;
static const float PC_ROW_TOP = 40.f, PC_ROW_PITCH = 10.6f;

static Layout progressionLayout() {
	Layout L;
	L.hp = 20.f;
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

	knob("p.key", 13.f, 20.f, ProgressionModule::P_KEY, "KEY");
	Item mode;
	mode.key = "p.mode"; mode.kind = Item::PARAM; mode.id = ProgressionModule::P_MODE;
	mode.style = "lamps"; mode.x = 25.f; mode.y = 15.f;
	mode.pitch = 9.f; mode.names = {"MAJOR", "MINOR"};
	mode.labelSide = Panel::RIGHT;
	L.items.push_back(mode);

	label("h.degree", PC_DEG, 32.f, "DEG", Panel::CENTRE, true);
	label("h.quality", PC_QUAL, 32.f, "QUAL", Panel::CENTRE, true);
	label("h.beats", PC_BEATS, 32.f, "BEATS", Panel::CENTRE, true);

	for (int i = 0; i < SLOTS; i++) {
		const float y = PC_ROW_TOP + i * PC_ROW_PITCH;
		const std::string n = std::to_string(i + 1);
		knob("p.deg" + n, PC_DEG, y, ProgressionModule::P_DEGREE + i, "");
		knob("p.qual" + n, PC_QUAL, y, ProgressionModule::P_QUALITY + i, "");
		knob("p.beats" + n, PC_BEATS, y, ProgressionModule::P_BEATS + i, "");
		Item lamp;
		lamp.key = "lamp.slot" + n; lamp.kind = Item::LIGHT;
		lamp.id = ProgressionModule::L_SLOT + i;
		lamp.x = 6.f; lamp.y = y;
		L.items.push_back(lamp);
	}

	jack("in.clock", Item::PORT_IN, 13.f, 128.f - 12.f, ProgressionModule::I_CLOCK,
		"clock", SIG_GATE);
	jack("in.reset", Item::PORT_IN, 29.f, 128.f - 12.f, ProgressionModule::I_RESET,
		"reset", SIG_GATE);
	knob("p.tempo", 45.f, 128.f - 12.f, ProgressionModule::P_TEMPO, "tempo");
	jack("out.mpx", Item::PORT_OUT, 88.f, 128.f - 12.f, ProgressionModule::O_MPX,
		"mpxOut", NOTE_CABLE, 12.f);

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
		display->box.pos = mm2px(math::Vec(52.f, 0.f));
		display->box.size = mm2px(math::Vec(48.f, 128.5f));
		display->rowTop = mm2px(math::Vec(0, PC_ROW_TOP)).y;
		display->rowPitch = mm2px(math::Vec(0, PC_ROW_PITCH)).y;
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
