/** mpxGuitarist — an articulated MPX part played as control voltage.

WHAT IT IS FOR. A note on an MPX cable can say that it is hammered on, palm muted, let ring, the
third string of a downstroke, bent a whole tone and released. mpxOut sends the notes as they were
written; this sends them as they would be PLAYED, so any voice in the rack can play a guitar part
without knowing what a guitar is.

A CHANNEL IS A STRING, NOT A NOTE. That is what makes the articulations work with ordinary
modules: a bend moves one channel and leaves the others where they are, a hammer-on sends no new
gate so the pitch glides under a held envelope, and let ring ends when that string is struck
again rather than after some length. Six channels for a six-string part whether or not all six
sound, taken from the instrument on the cable — so a bass gets four, tuned as a bass, with
nothing set on this panel.

THE BEND IS IN THE PITCH. A slide, a hammer-on and a bend all move the pitch, so the
volts-per-octave output moves with them and one cable into any oscillator plays the performance.
There is no separate bend output to add back in.

HOW IT SOUNDS IS IN A FILE. Every number — how long a palm mute lasts, how deep a wide vibrato
is, how fast the hand crosses the strings — is in DreamerMPX/perform.txt, which is written with
the defaults the first time the module is used and reloaded from the menu. See Perform.hpp and
docs/guitar-player-spec.md.
*/
#include "plugin.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"
#include "Perform.hpp"

namespace px {


static std::string rulesPath() {
	return asset::user("DreamerMPX/perform.txt");
}


//?module Plays one MPX part as control voltage, the way a guitarist would: each string a channel of a
//? polyphonic cable, its pitch carrying the bends, slides and vibrato, its gate, level and
//? timbre shaped by how each note is played. The number of strings, their tuning and the capo
//? come from the instrument on the cable.
//?note A hammer-on, pull-off or legato slide sends no new gate: the pitch moves while the gate is
//? held, so an envelope patched to the gate carries on through the change of note.
//?note The performance rules are read from DreamerMPX/perform.txt, which is written with the
//? defaults the first time, and read again from the right-click menu.
struct GuitaristModule : Module, NoteSink {
	enum ParamId {
		P_HUMANISE,
		NUM_PARAMS
	};
	enum InputId {
		I_MPX,
		NUM_INPUTS
	};
	enum OutputId {
		O_GATE,
		O_PITCH,
		O_LEVEL,
		O_TIMBRE,
		O_PRESSURE,
		O_PAN,
		NUM_OUTPUTS
	};
	enum LightId {
		NUM_LIGHTS
	};

	Performer performer;
	BusReader reader;
	Instrument instrument;
	uint32_t instrumentChange = 0;

	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
	std::atomic<bool> relink{false};
	/** Set from the menu, acted on in process: reading a file is not something to do between
	two audio callbacks. */
	std::atomic<bool> wantRules{false};
	PerformRules loadedRules;
	std::atomic<bool> rulesReady{false};
	std::string rulesMessage;

	bool attachedWas = false;


	GuitaristModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_HUMANISE, 0.f, 2.f, 1.f, "Humanise", "%", 0.f, 100.f);
		//? How much the timing, loudness and lengths of the notes are varied, from 0%, where the same
		//? notes come out the same way every time, through 100%, the amount the rules file sets, to
		//? 200%.
		configInput(I_MPX, "MPX note");
		//? One part: its notes and how each is played, from Guitar Chart or any MPX source.
		configOutput(O_GATE, "Gate");
		//? 10V while each string sounds, one channel per string. A hammer-on or legato slide keeps
		//? the gate high rather than starting a new one.
		configOutput(O_PITCH, "1V/oct, as played");
		//? Each string's pitch as played, 1V per octave with 0V at middle C, one channel per string,
		//? with the bends, slides and vibrato in it.
		configOutput(O_LEVEL, "Level");
		//? Each string's loudness, 0 to 10V, one channel per string: the note's dynamic with its
		//? accents, ghost notes and palm mutes in it.
		configOutput(O_TIMBRE, "Timbre");
		//? Each string's brightness, 0 to 10V, one channel per string: low for a palm mute or a dead
		//? note, high for a harmonic, 6V for an ordinary note.
		configOutput(O_PRESSURE, "Pressure");
		//? Each string's pressure, 0 to 10V, one channel per string, passed through from the part
		//? when its source sends one.
		configOutput(O_PAN, "Pan");
		//? Each string's place in the stereo field, -5V at the left to 5V at the right, one channel
		//? per string.
		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		readRules();
	}

	bool isMPXInputId(int id) override {
		return id == I_MPX;
	}

	void link(const int* slots, const uint32_t* generations, int n) {
		bool same = (n == wantCount.load());
		for (int i = 0; i < n && same; i++) {
			same = (wantSlots[i].load() == slots[i]
				&& wantGenerations[i].load() == generations[i]);
		}
		if (same)
			return;
		for (int i = 0; i < n && i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(slots[i]);
			wantGenerations[i].store(generations[i]);
		}
		wantCount.store((n < MAX_UPSTREAM) ? n : MAX_UPSTREAM);
		relink.store(true);
	}

	/** Reads the rules, writing the defaults first if there is no file. Main thread. */
	void readRules() {
		const std::string path = rulesPath();
		PerformRules rules;
		if (!system::isFile(path)) {
			system::createDirectories(asset::user("DreamerMPX"));
			FILE* f = std::fopen(path.c_str(), "w");
			if (f) {
				const std::string text = performRulesWrite(rules);
				std::fwrite(text.data(), 1, text.size(), f);
				std::fclose(f);
			}
			rulesMessage = "wrote " + path;
		}
		else {
			FILE* f = std::fopen(path.c_str(), "rb");
			std::string text;
			if (f) {
				char buf[4096];
				size_t n = 0;
				while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
					text.append(buf, n);
				std::fclose(f);
			}
			std::string complaint;
			const int set = performRulesRead(text, rules, &complaint);
			rulesMessage = complaint.empty()
				? string::f("%d rules read", set) : complaint;
		}
		loadedRules = rules;
		rulesReady.store(true);
	}

	void onReset() override {
		performer.silence();
	}

	void process(const ProcessArgs& args) override {
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}
		if (rulesReady.exchange(false))
			performer.rules = loadedRules;

		// THE CABLE GOING IS EVERYTHING STOPPING. A note ends when its length runs out, but a
		// let-ring note waits for its string to be struck again, and a cable pulled out means it
		// never will be.
		const bool attached = reader.attached();
		if (!attached && attachedWas)
			performer.silence();
		attachedWas = attached;

		// WHAT INSTRUMENT IS ON THE CABLE, and so how many strings to hold voices for. Read
		// every sample and acted on when it changes, which is when a song is loaded.
		Instrument in;
		if (reader.instrument(in) && in.change != instrumentChange) {
			instrumentChange = in.change;
			instrument = in;
			performer.strings(in.stringCount);
		}

		performer.humanise(params[P_HUMANISE].getValue());
		performer.seed(1u);

		Event e;
		while (reader.next(e)) {
			if (e.kind == Event::ON) {
				PerformNote n;
				n.handle = e.handle;
				n.pitch = e.pitch;
				n.level = e.level;
				n.seconds = (e.duration > 0.f) ? e.duration : 0.25f;
				n.string = e.string;
				n.fret = e.fret;
				n.technique = e.technique;
				n.vibrato = e.vibrato;
				n.grace = e.grace;
				n.strum = e.strum;
				n.strumIndex = e.strumIndex;
				n.strumMs = e.strumMs;
				n.pan = e.pan;
				n.bendCount = e.bendCount;
				for (int k = 0; k < n.bendCount && k < 4; k++) {
					n.bendAt[k] = (float) e.bendPoints[k].at / 100.f;
					n.bendCents[k] = (float) e.bendPoints[k].cents;
				}
				performer.note(n);
			}
			else if (e.kind == Event::OFF) {
				// A source that ends its own notes: the performer holds the length, so an early
				// end is the string being stopped by hand. Nothing else can say it.
				for (int i = 0; i < performer.voiceCount(); i++) {
					if (performer.voice(i).gate && performer.voice(i).handle == e.handle)
						performer.silenceVoice(i);
				}
			}
			else if (e.lane == LANE_PRESSURE || e.lane == LANE_TIMBRE) {
				// Something moving while the note sounds, which is not the performer's business:
				// passed straight through to the voice it names.
				for (int i = 0; i < performer.voiceCount(); i++) {
					if (performer.voice(i).gate && performer.voice(i).handle == e.handle)
						performer.set(i, e.lane == LANE_TIMBRE, e.value);
				}
			}
		}

		performer.advance(args.sampleTime);
		// The messages are for a synthesiser; this module reads the voices, so they are dropped
		// rather than left to fill.
		PerformMessage m;
		while (performer.next(m)) {}

		// ---- out ----
		//
		// A CHANNEL PER STRING, and as many channels as the instrument has strings. Rack reads
		// the count from the first output, so every output carries the same number.
		int channels = instrument.stringCount;
		if (channels <= 0) {
			// Nothing has said what the instrument is: as many as are sounding, so a patch
			// works before a song is loaded.
			for (int i = 0; i < performer.voiceCount(); i++) {
				if (performer.voice(i).gate)
					channels = i + 1;
			}
			if (channels <= 0)
				channels = 1;
		}
		if (channels > 16)
			channels = 16;

		for (int id = O_GATE; id < NUM_OUTPUTS; id++)
			outputs[id].setChannels(channels);
		for (int c = 0; c < channels; c++) {
			const PerformVoice& v = performer.voice(c);
			outputs[O_GATE].setVoltage(v.gate ? 10.f : 0.f, c);
			outputs[O_PITCH].setVoltage(v.pitch, c);
			outputs[O_LEVEL].setVoltage(v.level * 10.f, c);
			outputs[O_TIMBRE].setVoltage(v.timbre * 10.f, c);
			outputs[O_PRESSURE].setVoltage(v.pressure * 10.f, c);
			outputs[O_PAN].setVoltage(v.pan * 5.f, c);
		}
	}
};


// ---- the panel ---------------------------------------------------------------------------------

static const float PANEL_W = 40.64f;      /**< Eight HP. */
static const float LEFT = 10.f;
static const float RIGHT = 28.f;


/** What is on the cable, and what the rules file had to say. */
struct GuitaristDisplay : widget::Widget {
	GuitaristModule* module = NULL;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0 || !module)
			return;
		std::string name = "—";
		if (module->instrument.valid && module->instrument.name[0])
			name = module->instrument.name;
		else if (module->instrument.valid)
			name = string::f("%d strings", module->instrument.stringCount);

		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 8.f);
		nvgFillColor(vg, PANEL_INK);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int pass = 0; pass < 2; pass++)
			nvgText(vg, box.size.x / 2.f, box.size.y / 2.f, name.c_str(), NULL);
	}
};


static Layout guitaristLayout() {
	Layout L;
	L.hp = 8.f;
	L.title = "mpxGuitarist";

	auto label = [&](const char* key, float x, float y, const char* text, bool heading,
			const char* owner) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.defaultText = text; i.align = Panel::CENTRE; i.heading = heading;
		if (owner)
			i.owner = owner;
		L.items.push_back(i);
	};
	auto jack = [&](const char* key, Item::Kind kind, float x, float y, int id,
			const char* name, NVGcolor colour) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = colour;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 7.5f, name, false, key);
	};

	jack("in.mpx", Item::PORT_IN, PANEL_W / 2.f, 26.f, GuitaristModule::I_MPX, "mpx IN",
		NOTE_CABLE);

	Item what;
	what.key = "d.instrument"; what.kind = Item::DISPLAY;
	what.x = 3.f; what.y = 34.f; what.w = PANEL_W - 6.f; what.h = 6.f;
	L.items.push_back(what);

	Item human;
	human.key = "p.humanise"; human.kind = Item::PARAM; human.id = GuitaristModule::P_HUMANISE;
	human.style = "knob"; human.x = PANEL_W / 2.f; human.y = 50.f;
	human.ticks = 3; human.tickMarks = {"OFF", "", "2x"}; human.nameSize = 5.4f;
	L.items.push_back(human);
	label("p.humanise.label", PANEL_W / 2.f, 61.f, "HUMANISE", true, "p.humanise");

	// THE PITCH AND THE GATE TOGETHER AT THE TOP, since those two are the patch; the rest are
	// what makes it sound played rather than typed.
	jack("out.pitch", Item::PORT_OUT, LEFT, 76.f, GuitaristModule::O_PITCH, "1V/oct", SIG_PITCH);
	jack("out.gate", Item::PORT_OUT, RIGHT, 76.f, GuitaristModule::O_GATE, "gate", SIG_GATE);
	jack("out.level", Item::PORT_OUT, LEFT, 94.f, GuitaristModule::O_LEVEL, "level", SIG_CV);
	jack("out.timbre", Item::PORT_OUT, RIGHT, 94.f, GuitaristModule::O_TIMBRE, "timbre", SIG_CV);
	jack("out.press", Item::PORT_OUT, LEFT, 112.f, GuitaristModule::O_PRESSURE, "pressure",
		SIG_CV);
	jack("out.pan", Item::PORT_OUT, RIGHT, 112.f, GuitaristModule::O_PAN, "pan", SIG_CV);

	L.bindOffsets();
	return L;
}


struct GuitaristWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	GuitaristWidget(GuitaristModule* module) {
		setModule(module);
		layout = guitaristLayout();
		layoutApplyUser("mpxGuitarist", layout);
		panel = new Panel;
		addChild(panel);
		GuitaristDisplay* what = new GuitaristDisplay;
		what->module = module;
		layoutPlaceDisplay(this, layout, "d.instrument", what);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxGuitarist");
		GuitaristModule* m = dynamic_cast<GuitaristModule*>(module);
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuItem("Read the performance rules again", "", [=]() {
			m->readRules();
		}));
		menu->addChild(createMenuLabel(rulesPath()));
		if (!m->rulesMessage.empty())
			menu->addChild(createMenuLabel(m->rulesMessage));
	}

	void step() override {
		ModuleWidget::step();
		GuitaristModule* m = dynamic_cast<GuitaristModule*>(module);
		if (!m)
			return;
		PortWidget* port = getInput(GuitaristModule::I_MPX);
		int slots[MAX_UPSTREAM];
		uint32_t gens[MAX_UPSTREAM];
		int n = 0;
		if (port) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				engine::Cable* cable = cw->getCable();
				if (!cable || n >= MAX_UPSTREAM)
					continue;
				uint32_t g = 0;
				const int slot = noteBusOf(cable->outputModule, cable->outputId, &g);
				if (slot >= 0) {
					slots[n] = slot;
					gens[n] = g;
					n++;
					cw->color = NOTE_CABLE;
				}
			}
		}
		m->link(slots, gens, n);
	}
};


} // namespace px


Model* modelMpxGuitarist = createModel<px::GuitaristModule, px::GuitaristWidget>("mpxGuitarist");
