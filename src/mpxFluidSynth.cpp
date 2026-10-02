/** mpxFluidSynth — a SoundFont band, played from MPX cables. See docs/guitar-player-spec.md.

ONE SYNTH, ONE BANK, SIX PARTS. A General MIDI bank is thirty megabytes, so it is loaded once and
shared: each input is one instrument with its own sound out of the bank, and each gets a block of
channels — one per string of a fretted part — so a bend on one string leaves the others where they
are and each string is monophonic as a real one is.

A part per input rather than a module per part, for that reason alone. Everywhere else in MPX one
cable is one instrument, and it still is here: an input is a part.

WHAT IT IS FOR. Hearing an imported song, and hearing a generated one, without a rack full of
voices. The realism of a guitar is not in these samples — it is in what is sent to them, which is
the performer library's business and not this module's. Until the cable carries the articulations,
this plays the right notes with the right dynamics and no playing.

DRUMS ARE A PART LIKE ANY OTHER: a percussion sound out of the bank, on an input whose notes are
General MIDI drum numbers, which is what mpxGuitarChart and mpxGroove already send.
*/
#include "plugin.hpp"
#include "SoundCore.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"
#include "Perform.hpp"

#include <osdialog.h>

#include <atomic>
#include <thread>

namespace px {


/** How many parts. Six because a band is a rhythm section and a few voices on top. The twelve
channels each part has, one per string, are the shared core's; see SoundCore.hpp. */
static const int PARTS = 6;


//?module Plays up to six MPX parts through one FluidSynth and one SoundFont: each part on its own
//? sound from the bank, with a channel for each of its strings so that bends and legato stay on
//? the string they belong to, mixed to stereo.
//?note The bank is the first SoundFont in DreamerMPX/banks; another is chosen from the right-click
//? menu. A part's sound is chosen by clicking its name, from the bank's own list.
//?note Drums are a part like any other: a kit from the bank, playing General MIDI drum numbers.
struct SoundModule : Module, NoteSink {
	enum ParamId {
		P_LEVEL,
		P_HUMANISE,
		P_REVERB,
		P_CHORUS,
		ENUMS(P_MUTE, PARTS),
		NUM_PARAMS
	};
	enum InputId {
		ENUMS(I_PART, PARTS),
		NUM_INPUTS
	};
	enum OutputId {
		O_L,
		O_R,
		NUM_OUTPUTS
	};
	enum LightId {
		/** Lit once the bank has been read and the module can make a sound. */
		L_BANK,
		ENUMS(L_PART, PARTS),
		NUM_LIGHTS
	};

	SoundCore core;

	/** Which sound each part plays, as a place in the bank's own list. */
	int sound[PARTS] = {};
	std::string soundName[PARTS];


	SoundModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_LEVEL, 0.f, 4.f, 1.f, "Level");
		//? How loud the whole band is, from 0 to 4 times: at 1, a note sounds as loud as the same
		//? note from mpxGuitar.
		configParam(P_HUMANISE, 0.f, 2.f, 1.f, "Humanise", "%", 0.f, 100.f);
		//? How much the timing, loudness and lengths of the notes are varied, from 0%, where the same
		//? notes come out the same way every time, through 100%, the amount the rules file sets, to
		//? 200%.
		configSwitch(P_REVERB, 0.f, 1.f, 1.f, "Reverb", {"Off", "On"});
		//? FluidSynth's own reverb, on or off.
		configSwitch(P_CHORUS, 0.f, 1.f, 1.f, "Chorus", {"Off", "On"});
		//? FluidSynth's own chorus, on or off.
		for (int p = 0; p < PARTS; p++) {
			configSwitch(P_MUTE + p, 0.f, 1.f, 0.f, string::f("Mute %d", p + 1),
				{"Playing", "Muted"});
			//? Silences one part without unpatching it.
			configInput(I_PART + p, string::f("MPX part %d", p + 1));
			//? One part: its notes and how each is played, from Guitar Chart or any MPX source, on the
			//? sound chosen for it.
			sound[p] = -1;
		}
		configOutput(O_L, "Left");
		//? The band, mixed to stereo.
		configOutput(O_R, "Right");
		//? The band, mixed to stereo.
		core.setParts(PARTS);
		core.logName = "mpxFluidSynth";
		core.onBankRead = [this]() {
			applyAsked();
			chooseDefaults();
		};
		core.readRules();
	}

	bool isMPXInputId(int id) override {
		return id >= I_PART && id < I_PART + PARTS;
	}

	void link(int part, const int* slots, const uint32_t* generations, int n) {
		core.link(part, slots, generations, n);
	}

	// ---- the bank ------------------------------------------------------------------------------

	void loadBank(const std::string& path) {
		core.loadBank(path);
	}

	/** WHAT EACH PART PLAYS BEFORE ANYBODY CHOOSES. A band rather than six pianos: a guitar for
	the first two parts, a bass, a kit, a piano and an organ, which is what an imported song most
	often holds. Any sound in the bank can be chosen afterwards. */
	void chooseDefaults() {
		const std::vector<FluidPreset>& list = core.engine.presets();
		if (list.empty())
			return;
		static const int WANT[PARTS][2] = {
			{0, 29}, {0, 27}, {0, 33}, {128, 0}, {0, 0}, {0, 16},
		};
		for (int p = 0; p < PARTS; p++) {
			// Already chosen, or read from the patch: set again, since a synthesiser made anew
			// has no programs.
			if (sound[p] >= 0 && sound[p] < (int) list.size()) {
				setSound(p, sound[p]);
				continue;
			}
			int found = -1;
			for (size_t i = 0; i < list.size() && found < 0; i++) {
				if (list[i].bank == WANT[p][0] && list[i].program == WANT[p][1])
					found = (int) i;
			}
			setSound(p, (found >= 0) ? found : 0);
		}
	}

	void setSound(int part, int which) {
		const std::vector<FluidPreset>& list = core.engine.presets();
		if (part < 0 || part >= PARTS || which < 0 || which >= (int) list.size())
			return;
		sound[part] = which;
		soundName[part] = list[(size_t) which].name;
		core.setProgram(part, list[(size_t) which].bank, list[(size_t) which].program);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		if (!core.engine.bankPath().empty())
			json_object_set_new(rootJ, "bank", json_string(core.engine.bankPath().c_str()));
		json_t* partsJ = json_array();
		for (int p = 0; p < PARTS; p++) {
			json_t* one = json_object();
			const std::vector<FluidPreset>& list = core.engine.presets();
			// THE SOUND BY NAME AND NUMBER, not by its place in the list: a different bank, or a
			// newer version of the same one, holds its sounds in a different order.
			if (sound[p] >= 0 && sound[p] < (int) list.size()) {
				json_object_set_new(one, "bank", json_integer(list[(size_t) sound[p]].bank));
				json_object_set_new(one, "program", json_integer(list[(size_t) sound[p]].program));
			}
			json_array_append_new(partsJ, one);
		}
		json_object_set_new(rootJ, "parts", partsJ);
		return rootJ;
	}

	/** The sounds a patch asked for, once the bank they name has been read. */
	std::vector<std::pair<int, int> > asked;

	void dataFromJson(json_t* rootJ) override {
		json_t* partsJ = json_object_get(rootJ, "parts");
		asked.clear();
		if (json_is_array(partsJ)) {
			for (size_t p = 0; p < json_array_size(partsJ) && p < PARTS; p++) {
				json_t* one = json_array_get(partsJ, p);
				json_t* b = json_object_get(one, "bank");
				json_t* g = json_object_get(one, "program");
				asked.push_back(std::make_pair(
					json_is_integer(b) ? (int) json_integer_value(b) : -1,
					json_is_integer(g) ? (int) json_integer_value(g) : -1));
			}
		}
		json_t* bankJ = json_object_get(rootJ, "bank");
		if (json_is_string(bankJ) && json_string_value(bankJ)[0] != '\0')
			loadBank(json_string_value(bankJ));
	}

	/** Called once the bank has arrived, to put back what the patch asked for. */
	void applyAsked() {
		const std::vector<FluidPreset>& list = core.engine.presets();
		for (size_t p = 0; p < asked.size() && p < PARTS; p++) {
			if (asked[p].first < 0)
				continue;
			for (size_t i = 0; i < list.size(); i++) {
				if (list[i].bank == asked[p].first && list[i].program == asked[p].second) {
					setSound((int) p, (int) i);
					break;
				}
			}
		}
		asked.clear();
	}

	// ---- playing -------------------------------------------------------------------------------

	void onAdd(const AddEvent& e) override {
		startEngine(APP->engine->getSampleRate());
	}

	void onRemove(const RemoveEvent& e) override {
		core.stopEngine();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		startEngine(e.sampleRate);
	}

	void startEngine(float rate) {
		// A new synthesiser has no programs set: every part takes its sound again once the bank
		// is read.
		core.startEngine(rate, params[P_REVERB].getValue() > 0.5f,
			params[P_CHORUS].getValue() > 0.5f);
	}

	void process(const ProcessArgs& args) override {
		if (!core.engine.running()) {
			outputs[O_L].setVoltage(0.f);
			outputs[O_R].setVoltage(0.f);
			return;
		}
		lights[L_BANK].setBrightness(core.playing() ? 1.f : 0.f);
		bool muted[PARTS];
		for (int p = 0; p < PARTS; p++)
			muted[p] = params[P_MUTE + p].getValue() > 0.5f;
		core.step(args.sampleTime, muted, params[P_HUMANISE].getValue(),
			params[P_REVERB].getValue() > 0.5f, params[P_CHORUS].getValue() > 0.5f);
		for (int p = 0; p < PARTS; p++)
			lights[L_PART + p].setBrightness(core.active[p]);

		float left = core.wetLeft(), right = core.wetRight();
		for (int p = 0; p < PARTS; p++) {
			left += core.partLeft(p);
			right += core.partRight(p);
		}
		const float level = params[P_LEVEL].getValue() * SOUND_VOLTS;    // To Rack's volts.
		outputs[O_L].setVoltage(left * level);
		outputs[O_R].setVoltage(right * level);
	}
};


// ---- the panel ---------------------------------------------------------------------------------

static const float PANEL_W = 60.96f;      /**< Twelve HP. */
static const float LAMP_X = 3.5f;
static const float JACK_X = 9.5f;
static const float MUTE_X = 18.f;
static const float NAME_X = 23.f;
static const float ROW_TOP = 47.f;
static const float ROW_STEP = 10.5f;


/** WHICH BANK IS IN, AS A READING RATHER THAN AS A CONTROL.

There is one bank, it is found in the banks folder when the module is made, and it is read without
being asked for — so the panel has nothing to ask. It said CHOOSE A SOUNDFONT and was a button
across the whole panel, which offered a modal file dialog to anybody who clicked near it and
stopped Rack drawing until they answered it.

Choosing another is in the right-click menu, where something done once and rarely belongs. */
struct SoundBank : widget::Widget {
	SoundModule* module = NULL;

	void choose() {
		if (!module || module->core.busy.load())
			return;
		std::string dir = SoundCore::bankFolder();
		system::createDirectories(dir);
		osdialog_filters* filters = osdialog_filters_parse("SoundFont:sf2,SF2");
		char* path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
		osdialog_filters_free(filters);
		if (!path)
			return;
		module->loadBank(path);
		std::free(path);
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;
		std::string text = "no bank";
		if (module) {
			if (module->core.busy.load())
				text = "loading";
			else if (module->core.failed.load())
				text = module->core.message;
			else if (module->core.haveBank.load())
				text = module->core.message;
		}
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 10.f);
		nvgFillColor(vg, PANEL_INK);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int pass = 0; pass < 2; pass++)
			nvgText(vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
	}
};


/** The sound each part plays. A press opens the bank's own list. */
struct SoundParts : widget::OpaqueWidget {
	SoundModule* module = NULL;
	float rowY[PARTS] = {};

	int rowAt(float y) const {
		for (int p = 0; p < PARTS; p++) {
			if (std::fabs(y - rowY[p]) < mm2px(ROW_STEP) / 2.f)
				return p;
		}
		return -1;
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module
				&& module->core.haveBank.load()) {
			const int p = rowAt(e.pos.y);
			if (p >= 0) {
				showList(p);
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onButton(e);
	}

	/** THE BANK'S OWN SOUNDS, in the bank's own order, sized to itself inside a menu overlay so
	it covers only as much of the screen as it needs. */
	void showList(int part) {
		SoundModule* m = module;
		const std::vector<FluidPreset>& list = m->core.engine.presets();
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Part %d", part + 1)));
		for (size_t i = 0; i < list.size(); i++) {
			const int which = (int) i;
			const std::string name = list[i].percussion
				? list[i].name + "  (kit)" : list[i].name;
			menu->addChild(createCheckMenuItem(name, "",
				[=]() { return m->sound[part] == which; },
				[=]() { m->setSound(part, which); }));
		}
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 9.f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		for (int p = 0; p < PARTS; p++) {
			std::string name = "—";
			if (module && module->core.haveBank.load() && !module->soundName[p].empty())
				name = module->soundName[p];
			nvgFillColor(vg, PANEL_INK);
			for (int pass = 0; pass < 2; pass++)
				nvgText(vg, 2.f, rowY[p], name.c_str(), NULL);
		}
	}
};


static Layout soundLayout() {
	Layout L;
	L.hp = 12.f;
	L.title = "mpxFluidSynth";

	auto label = [&](const char* key, float x, float y, const char* text, Panel::Align align,
			bool heading, const char* owner) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.defaultText = text; i.align = align; i.heading = heading;
		if (owner)
			i.owner = owner;
		L.items.push_back(i);
	};

	// WHICH BANK IS IN. A reading and a lamp, not a control: there is one bank and it is read
	// without being asked for. Choosing another is in the menu.
	label("h.bank", PANEL_W / 2.f, 25.f, "SOUNDFONT", Panel::CENTRE, true, NULL);
	Item bank;
	bank.key = "d.bank"; bank.kind = Item::DISPLAY;
	bank.w = 44.f; bank.h = 6.f;
	bank.x = (PANEL_W - bank.w) / 2.f + 2.f; bank.y = 28.f;
	L.items.push_back(bank);
	Item lit;
	lit.key = "lamp.bank"; lit.kind = Item::LIGHT; lit.id = SoundModule::L_BANK;
	lit.x = 4.5f; lit.y = bank.y + bank.h / 2.f;
	L.items.push_back(lit);

	// SIX PARTS, ONE TO A ROW: a lamp that flickers when it plays, the cable, a mute, and the
	// sound it is making. The name is as wide as the panel has left, which is what decides the
	// width of the panel.
	for (int p = 0; p < PARTS; p++) {
		const float y = ROW_TOP + p * ROW_STEP;
		Item in;
		in.key = string::f("in.part%d", p + 1);
		in.kind = Item::PORT_IN; in.id = SoundModule::I_PART + p;
		in.x = JACK_X; in.y = y; in.ring = NOTE_CABLE;
		L.items.push_back(in);

		Item mute;
		mute.key = string::f("p.mute%d", p + 1);
		mute.kind = Item::PARAM; mute.id = SoundModule::P_MUTE + p;
		mute.style = "latch"; mute.diameter = 5.6f; mute.x = MUTE_X; mute.y = y;
		L.items.push_back(mute);

		Item lamp;
		lamp.key = string::f("lamp.part%d", p + 1);
		lamp.kind = Item::LIGHT; lamp.id = SoundModule::L_PART + p;
		lamp.x = LAMP_X; lamp.y = y;
		L.items.push_back(lamp);
	}
	label("h.in", JACK_X, ROW_TOP - 6.5f, "mpx IN", Panel::CENTRE, true, NULL);
	label("h.mute", MUTE_X, ROW_TOP - 6.5f, "MUTE", Panel::CENTRE, true, NULL);
	label("h.sound", NAME_X + 1.f, ROW_TOP - 6.5f, "SOUND", Panel::LEFT, true, NULL);

	Item parts;
	parts.key = "d.parts"; parts.kind = Item::DISPLAY;
	parts.x = NAME_X; parts.y = ROW_TOP - ROW_STEP / 2.f;
	parts.w = PANEL_W - NAME_X - 2.f; parts.h = PARTS * ROW_STEP;
	L.items.push_back(parts);

	// The whole band's loudness and how much the playing is moved about, and the pair out.
	const float y = ROW_TOP + PARTS * ROW_STEP + 6.f;
	Item level;
	level.key = "p.level"; level.kind = Item::PARAM; level.id = SoundModule::P_LEVEL;
	level.style = "knob"; level.x = 11.f; level.y = y;
	L.items.push_back(level);
	label("p.level.label", 11.f, y + 8.5f, "LEVEL", Panel::CENTRE, true, "p.level");

	Item human;
	human.key = "p.humanise"; human.kind = Item::PARAM; human.id = SoundModule::P_HUMANISE;
	human.style = "knob"; human.x = 27.f; human.y = y;
	human.ticks = 3; human.tickMarks = {"OFF", "", "2x"}; human.nameSize = 5.4f;
	L.items.push_back(human);
	label("p.humanise.label", 27.f, y + 8.5f, "HUMANISE", Panel::CENTRE, true, "p.humanise");

	auto jack = [&](const char* key, int id, float x, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PORT_OUT; i.id = id; i.x = x; i.y = y;
		i.ring = SIG_AUDIO;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 7.5f, name, Panel::CENTRE, false, key);
	};
	jack("out.l", SoundModule::O_L, 43.f, "L");
	jack("out.r", SoundModule::O_R, 53.f, "R");

	L.bindOffsets();
	return L;
}


struct SoundWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;
	SoundBank* bankDisplay = NULL;

	SoundWidget(SoundModule* module) {
		setModule(module);
		layout = soundLayout();
		layoutApplyUser("mpxFluidSynth", layout);
		panel = new Panel;
		addChild(panel);

		bankDisplay = new SoundBank;
		bankDisplay->module = module;
		layoutPlaceDisplay(this, layout, "d.bank", bankDisplay);

		SoundParts* parts = new SoundParts;
		parts->module = module;
		const Item* area = layout.find("d.parts");
		for (int p = 0; p < PARTS; p++)
			parts->rowY[p] = mm2px(ROW_TOP + p * ROW_STEP - (area ? area->y : 0.f));
		layoutPlaceDisplay(this, layout, "d.parts", parts);

		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxFluidSynth");
		SoundModule* m = dynamic_cast<SoundModule*>(module);
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuItem("Choose a SoundFont…", "", [=]() {
			if (bankDisplay)
				bankDisplay->choose();
		}));
		if (!m->core.engine.bankPath().empty())
			menu->addChild(createMenuLabel(m->core.engine.bankPath()));
	}

	void step() override {
		ModuleWidget::step();
		SoundModule* m = dynamic_cast<SoundModule*>(module);
		if (!m)
			return;
		for (int p = 0; p < PARTS; p++) {
			PortWidget* port = getInput(SoundModule::I_PART + p);
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
			m->link(p, slots, gens, n);
		}
	}
};


} // namespace px


Model* modelMpxFluidSynth = createModel<px::SoundModule, px::SoundWidget>("mpxFluidSynth");
