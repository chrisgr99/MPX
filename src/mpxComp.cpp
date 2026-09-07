/** mpxComp — chordal accompaniment.

WHAT IT IS FOR. A chord arriving as polyphonic volt-per-octave is a set of notes; it is not yet
anything a player would play. Every chord out of mpxChart is voiced upward from its own root, so
consecutive chords move in parallel and all four voices leap together — which is why the chart
through a polyphonic oscillator sounds like a chord machine rather than like hands. This module
is the part that decides WHERE the notes go.

THE RULE, and it is the whole trick: on a chord change each voice takes the nearest unused tone
of the new chord instead of being rebuilt from scratch. Common tones do not move at all, the
others step, and the same progression through the same oscillator stops lurching. It is the rule
that fixed the chart's bass output, applied to four voices rather than one.

TWO WAYS IN. A polyphonic cable, so it works with anybody's chord source; and an MPX cable, which
carries the same harmony plus what is coming — the next chord, the beats until it turns over, and
where the beat sits in the bar. None of that is needed to voice a chord and all of it is needed
to play a rhythm, which is why the input is here from the start.

THREE WAYS OUT, in the same voice order, so an envelope and an amplifier per voice fall out of
one pair of cables: pitch, gate, and level. Level is flat until there is a rhythm to accent; the
jack is here now so that a patch built today does not have to be re-patched when it is not.

WHAT IS NOT BUILT YET. The rhythm — broken chords, accompaniment figures, and the accents that
make them sound played rather than counted. Its controls are on the panel, greyed and inert, and
they are on the PANEL rather than in the menu because a parameter can be mapped to a controller
and automated where a menu item cannot. See comp.md.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

#include <algorithm>
#include <cmath>

namespace px {


static const int MAX_VOICES = 6;
/** How many cables one MPX input may be fed by, matching every other reader in the plugin. */
static const int MAX_UP = 4;

/** The voicings, in the order the knob walks through them. */
enum Spread {
	SPREAD_CLOSE,   /**< every voice in the smallest space that holds them */
	SPREAD_DROP2,   /**< the second voice from the top dropped an octave: the pianist's default */
	SPREAD_OPEN,    /**< voices spread across the range rather than packed */
	NUM_SPREADS,
};

static const char* SPREAD_NAMES[NUM_SPREADS] = {"Close", "Drop two", "Open"};

/** Planned, and named here so the panel can show what is coming. */
static const char* PATTERN_NAMES[] = {"Block", "Broken up", "Broken down", "Alberti", "Waltz"};
static const int NUM_PATTERNS = (int) (sizeof(PATTERN_NAMES) / sizeof(PATTERN_NAMES[0]));

static const char* ACCENT_NAMES[] = {"Even", "Metric", "Downbeat", "Backbeat", "Offbeat", "Push"};
static const int NUM_ACCENTS = (int) (sizeof(ACCENT_NAMES) / sizeof(ACCENT_NAMES[0]));


struct CompModule : Module, NoteSink {
	enum ParamId {
		P_VOICES,
		P_CENTRE,
		P_SPAN,
		P_SPREAD,
		P_LEAD,
		P_LEVEL,
		/** Inert until the rhythm is written. */
		P_PATTERN,
		P_GATE,
		P_ACCENT,
		P_AMOUNT,
		P_HUMAN,
		NUM_PARAMS
	};
	enum InputId {
		I_CHORD,
		I_MPX,
		I_CLOCK,
		NUM_INPUTS
	};
	enum OutputId {
		O_CHORD,
		O_GATES,
		O_LEVEL,
		NUM_OUTPUTS
	};
	enum LightId {
		L_CHANGE,
		NUM_LIGHTS
	};

	CompModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(P_VOICES, 1.f, (float) MAX_VOICES, 4.f, "Voices");
		paramQuantities[P_VOICES]->snapEnabled = true;
		// SEMITONES FROM MIDDLE C, because that is how a musician says where a part sits, and
		// because the number then reads the same whatever the chart's key is.
		configParam(P_CENTRE, -24.f, 24.f, 0.f, "Centre", " semitones from middle C");
		paramQuantities[P_CENTRE]->snapEnabled = true;
		configParam(P_SPAN, 1.f, 3.f, 2.f, "Span", " octaves");
		paramQuantities[P_SPAN]->snapEnabled = true;
		configSwitch(P_SPREAD, 0.f, (float) (NUM_SPREADS - 1), 0.f, "Spread",
			{SPREAD_NAMES[0], SPREAD_NAMES[1], SPREAD_NAMES[2]});
		// NOUGHT IS WHAT A CHORD SOURCE ALREADY DOES: rebuild every chord from its root. One is
		// a voice taking the nearest tone of the new chord. Between them the movement is
		// allowed but limited, which is a musical control rather than a switch.
		configParam(P_LEAD, 0.f, 1.f, 1.f, "Voice leading", "%", 0.f, 100.f);
		configParam(P_LEVEL, 0.f, 10.f, 8.f, "Level", " V");

		configSwitch(P_PATTERN, 0.f, (float) (NUM_PATTERNS - 1), 0.f, "Pattern (not yet built)",
			{PATTERN_NAMES[0], PATTERN_NAMES[1], PATTERN_NAMES[2], PATTERN_NAMES[3],
			PATTERN_NAMES[4]});
		configParam(P_GATE, 0.05f, 1.f, 0.9f, "Gate length (not yet built)", "%", 0.f, 100.f);
		configSwitch(P_ACCENT, 0.f, (float) (NUM_ACCENTS - 1), 1.f, "Accent (not yet built)",
			{ACCENT_NAMES[0], ACCENT_NAMES[1], ACCENT_NAMES[2], ACCENT_NAMES[3],
			ACCENT_NAMES[4], ACCENT_NAMES[5]});
		configParam(P_AMOUNT, 0.f, 1.f, 0.3f, "Accent amount (not yet built)", "%", 0.f, 100.f);
		configParam(P_HUMAN, 0.f, 1.f, 0.f, "Humanise (not yet built)", "%", 0.f, 100.f);

		configInput(I_CHORD, "Chord as polyphonic V/Oct");
		configInput(I_MPX, "MPX note in");
		configInput(I_CLOCK, "Clock (not yet built)");
		configOutput(O_CHORD, "Voiced chord as polyphonic V/Oct");
		configOutput(O_GATES, "A gate per voice");
		configOutput(O_LEVEL, "A level per voice");
	}

	bool isMPXInputId(int inputId) override {
		return inputId == I_MPX;
	}

	// ---- what the widget tells us about the cables ----

	std::atomic<int> wantSlots[MAX_UP];
	std::atomic<uint32_t> wantGenerations[MAX_UP];
	std::atomic<int> wantCount{0};

	void link(const int* slots, const uint32_t* generations, int n) {
		for (int i = 0; i < n && i < MAX_UP; i++) {
			wantSlots[i].store(slots[i]);
			wantGenerations[i].store(generations[i]);
		}
		wantCount.store(std::min(n, MAX_UP));
	}

	// ---- state ----

	/** Where each voice sits, in volts. Kept between chords: this is the memory that makes a
	line rather than a series of chords. */
	float voice[MAX_VOICES];
	int voices = 0;
	/** The chord last voiced, as pitch classes, so a change can be recognised. */
	int hadClasses[MAX_VOICES * 2];
	int hadCount = -1;
	/** A moment of silence at a chord change, so an envelope retriggers. */
	float retrigger = 0.f;
	float lightFade = 0.f;

	/** The chord to voice, taken from the polyphonic cable if one is patched and from the MPX
	cable otherwise. Returns how many pitch classes were found.

	THE CABLE WINS. A patch with both is asking for the notes on the cable it can see; the MPX
	link is then what tells the rhythm where the beat is, which is what it is for. */
	int gatherChord(int* out) {
		if (inputs[I_CHORD].isConnected()) {
			const int n = inputs[I_CHORD].getChannels();
			int count = 0;
			for (int i = 0; i < n && count < MAX_VOICES * 2; i++) {
				const float v = inputs[I_CHORD].getVoltage(i);
				const int pc = ((int) std::lround(v * 12.f) % 12 + 12) % 12;
				bool seen = false;
				for (int k = 0; k < count; k++)
					seen = seen || (out[k] == pc);
				if (!seen)
					out[count++] = pc;
			}
			return count;
		}

		for (int i = 0; i < wantCount.load(); i++) {
			Harmony h;
			if (!busReadHarmony(wantSlots[i].load(), h) || !h.valid)
				continue;
			int classes[8];
			const int n = chordPitchClasses(h.current, h.key, classes);
			int count = 0;
			for (int k = 0; k < n && count < MAX_VOICES * 2; k++)
				out[count++] = ((classes[k] % 12) + 12) % 12;
			return count;
		}
		return 0;
	}

	/** THE VOICING.

	Each voice in turn takes the nearest tone of the new chord to where that voice already was,
	within the range the panel allows, and no two voices take the same note. Voices beyond the
	number of tones double the lower ones an octave up, which is what a player does with a
	four-note chord in five fingers.

	The leading control is a blend rather than a switch: at nought the chord is built from the
	bottom up as a chord source would build it, at one every voice moves as little as it can,
	and between the two the movement is allowed but pulled towards the plain voicing. */
	void revoice(const int* classes, int count, float lead) {
		const int want = (int) std::round(params[P_VOICES].getValue());
		const float centre = params[P_CENTRE].getValue() / 12.f;
		const float span = params[P_SPAN].getValue();
		const int spread = (int) std::round(params[P_SPREAD].getValue());

		const float lo = centre - span / 2.f;
		const float hi = centre + span / 2.f;

		bool taken[MAX_VOICES];
		for (int i = 0; i < MAX_VOICES; i++)
			taken[i] = false;

		float placed[MAX_VOICES];
		for (int v = 0; v < want; v++) {
			// Where this voice would sit if the chord were simply stacked from the bottom.
			const int which = v % std::max(1, count);
			const int octave = v / std::max(1, count);
			const float plain = lo + (float) classes[which] / 12.f + (float) octave;

			float best = plain;
			if (lead > 0.f && v < voices) {
				// The nearest tone of the new chord to where this voice already was.
				float bestGap = 1e9f;
				for (int c = 0; c < count; c++) {
					if (taken[c] && count >= want)
						continue;
					float cand = (float) classes[c] / 12.f;
					// Into the octave nearest the note this voice is leaving.
					while (cand - voice[v] > 0.5f)
						cand -= 1.f;
					while (voice[v] - cand > 0.5f)
						cand += 1.f;
					// And inside the range the panel allows.
					while (cand < lo)
						cand += 1.f;
					while (cand > hi)
						cand -= 1.f;
					const float gap = std::fabs(cand - voice[v]);
					if (gap < bestGap) {
						bestGap = gap;
						best = cand;
						if (count >= want)
							taken[c] = true;
					}
				}
			}
			placed[v] = plain + (best - plain) * lead;
		}

		// DROP TWO, and open, are said in terms of the voicing that is already there rather than
		// built separately: the second voice from the top an octave down is what a pianist means
		// by drop two, and open is every other voice pushed down.
		std::sort(placed, placed + want);
		if (spread == SPREAD_DROP2 && want >= 3)
			placed[want - 2] -= 1.f;
		else if (spread == SPREAD_OPEN && want >= 3) {
			for (int v = want - 2; v >= 0; v -= 2)
				placed[v] -= 1.f;
		}
		std::sort(placed, placed + want);

		for (int v = 0; v < want; v++)
			voice[v] = placed[v];
		voices = want;
	}

	void process(const ProcessArgs& args) override {
		int classes[MAX_VOICES * 2];
		const int count = gatherChord(classes);

		bool changed = (count != hadCount);
		for (int i = 0; !changed && i < count; i++)
			changed = (classes[i] != hadClasses[i]);
		// A change of voice count is a new voicing too, or the extra voice never appears.
		changed = changed || (voices != (int) std::round(params[P_VOICES].getValue()));

		if (count > 0 && changed) {
			revoice(classes, count, params[P_LEAD].getValue());
			hadCount = count;
			for (int i = 0; i < count; i++)
				hadClasses[i] = classes[i];
			// A millisecond of silence, so whatever is playing this hears a new note rather
			// than one long one. Once there is a rhythm, the rhythm decides this instead.
			retrigger = 0.001f;
			lightFade = 1.f;
		}
		if (count == 0) {
			voices = 0;
			hadCount = -1;
		}

		if (retrigger > 0.f)
			retrigger -= args.sampleTime;
		if (lightFade > 0.f)
			lightFade = std::fmax(0.f, lightFade - args.sampleTime * 4.f);

		const int n = std::max(1, voices);
		outputs[O_CHORD].setChannels(n);
		outputs[O_GATES].setChannels(n);
		outputs[O_LEVEL].setChannels(n);
		const float level = params[P_LEVEL].getValue();
		for (int v = 0; v < n; v++) {
			outputs[O_CHORD].setVoltage(v < voices ? voice[v] : 0.f, v);
			outputs[O_GATES].setVoltage((voices > 0 && retrigger <= 0.f) ? 10.f : 0.f, v);
			outputs[O_LEVEL].setVoltage(voices > 0 ? level : 0.f, v);
		}
		lights[L_CHANGE].setBrightness(lightFade);
	}
};


/** A GREY VEIL OVER WHAT IS NOT BUILT YET.

The rhythm's controls are on the panel from the first version, so that the face never changes
shape under somebody's patch and so that they can be mapped to a controller the moment they do
something. Until then they are covered: dimmed, and deaf to the mouse, because a knob that turns
and changes nothing is worse than one that plainly cannot yet. Each one comes out from under its
veil as its part is written. */
struct Veil : widget::OpaqueWidget {
	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, nvgRGBA(0x16, 0x1a, 0x20, 0xc0));
		nvgFill(args.vg);
	}
	void onButton(const ButtonEvent& e) override {
		// Right-click still reaches the panel's own menu; a left click does nothing at all.
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
};


static Layout compLayout() {
	Layout L;
	L.hp = 14.f;
	L.title = "mpxComp";
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
		label(key + ".label", x, y + 8.5f, name, Panel::CENTRE, true, 0.f, key);
	};
	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor color, float size = 0.f) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = color;
		L.items.push_back(i);
		label(key + ".label", x, y + 7.5f, name, Panel::CENTRE, size > 0.f, size, key);
	};

	// FOURTEEN HP IS 71.1 MM. Three columns at 14, 35.5 and 57 leave a knob's width of air at
	// each edge; four rows at twenty millimetres clear the labels, which sit 8.5 mm under each
	// knob and are 3 mm tall.
	knob("p.voices", 14.f, 30.f, CompModule::P_VOICES, "VOICES");
	knob("p.centre", 35.5f, 30.f, CompModule::P_CENTRE, "CENTRE");
	knob("p.span", 57.f, 30.f, CompModule::P_SPAN, "SPAN");

	knob("p.spread", 14.f, 50.f, CompModule::P_SPREAD, "SPREAD");
	knob("p.lead", 35.5f, 50.f, CompModule::P_LEAD, "LEAD");
	knob("p.level", 57.f, 50.f, CompModule::P_LEVEL, "LEVEL");

	label("h.rhythm", 35.5f, 62.5f, "RHYTHM — NOT YET BUILT", Panel::CENTRE, false, 7.f);

	knob("p.pattern", 14.f, 72.f, CompModule::P_PATTERN, "PATTERN");
	knob("p.gate", 35.5f, 72.f, CompModule::P_GATE, "GATE");
	knob("p.accent", 57.f, 72.f, CompModule::P_ACCENT, "ACCENT");

	knob("p.amount", 14.f, 92.f, CompModule::P_AMOUNT, "AMOUNT");
	knob("p.human", 35.5f, 92.f, CompModule::P_HUMAN, "HUMAN");

	// The jacks: what comes in along the top of the pair of rows, what goes out below it.
	jack("in.chord", Item::PORT_IN, 12.f, 108.f, CompModule::I_CHORD, "chord", SIG_PITCH);
	jack("in.mpx", Item::PORT_IN, 30.f, 108.f, CompModule::I_MPX, "mpxIn", NOTE_CABLE, 7.f);
	jack("in.clock", Item::PORT_IN, 48.f, 108.f, CompModule::I_CLOCK, "clock", SIG_GATE);

	jack("out.chord", Item::PORT_OUT, 12.f, 120.f, CompModule::O_CHORD, "chord", SIG_PITCH);
	jack("out.gates", Item::PORT_OUT, 30.f, 120.f, CompModule::O_GATES, "gates", SIG_GATE);
	jack("out.level", Item::PORT_OUT, 48.f, 120.f, CompModule::O_LEVEL, "level", SIG_CV);

	Item lamp;
	lamp.key = "lamp.change"; lamp.kind = Item::LIGHT; lamp.id = CompModule::L_CHANGE;
	lamp.x = 63.f; lamp.y = 108.f;
	L.items.push_back(lamp);

	L.bindOffsets();
	return L;
}


struct CompWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	CompWidget(CompModule* module) {
		setModule(module);
		layout = compLayout();
		layoutApplyUser("mpxComp", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
		veilTheUnbuilt();
	}

	/** Over each control whose part is not written. Placed from the layout, so a control the
	editor has moved keeps its veil. */
	void veilTheUnbuilt() {
		static const char* keys[] = {"p.pattern", "p.gate", "p.accent", "p.amount", "p.human"};
		for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
			Item* item = layout.find(keys[i]);
			if (!item || !item->widget)
				continue;
			Veil* veil = new Veil;
			// The knob and the name under it, with a little air around both.
			veil->box.pos = item->widget->box.pos.minus(mm2px(math::Vec(2.f, 1.5f)));
			veil->box.size = item->widget->box.size.plus(mm2px(math::Vec(4.f, 8.f)));
			addChild(veil);
		}
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxComp");
	}

	void step() override {
		ModuleWidget::step();
		CompModule* comp = dynamic_cast<CompModule*>(module);
		if (!comp)
			return;
		// WHICH MPX CABLES ARE PATCHED, resolved here rather than in the audio thread: the
		// engine knows nothing of cables, and the widget is walked once a frame anyway.
		int slots[MAX_UP];
		uint32_t generations[MAX_UP];
		int n = 0;
		if (PortWidget* port = getInput(CompModule::I_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				if (n >= MAX_UP)
					break;
				engine::Cable* cable = cw->getCable();
				if (!cable)
					continue;
				uint32_t g = 0;
				const int s = noteBusOf(cable->outputModule, cable->outputId, &g);
				if (s < 0)
					continue;
				cw->color = NOTE_CABLE;
				slots[n] = s;
				generations[n] = g;
				n++;
			}
		}
		comp->link(slots, generations, n);
	}
};


} // namespace px


Model* modelMpxComp = createModel<px::CompModule, px::CompWidget>("mpxComp");
