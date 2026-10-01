/** mpxGuitarChartExpander — the Guitar Chart's own band. See SoundCore.hpp.

PLACED TO THE RIGHT OF mpxGuitarChart, touching it, it plays all twelve of the chart's rows with no
cables. Each track plays the General MIDI program its file names, and a drum track the kit, so
there is nothing to choose: the instruments are the song's. The chart's mutes are the mutes, since
a muted row sends nothing.

AN OUTPUT PER TRACK, with a gain beside it, and the whole band as a stereo pair. A track's own
output is a polyphonic cable with a channel for each voice — a string of a fretted part, or a held
note of anything else — as a piano module's output has a channel for each note. Each channel is
that voice's dry sound, both sides summed to one; the reverb and chorus are in the pair only,
since they are one unit shared by every track.
*/
#include "plugin.hpp"
#include "SoundCore.hpp"
#include "Layout.hpp"

#include <osdialog.h>

namespace px {


/** As many tracks as the chart has rows. */
static const int TRACKS = 12;
static const int TRACKS_PER_COLUMN = 6;


struct ExpanderModule : Module {
	enum ParamId {
		P_LEVEL,
		P_HUMANISE,
		P_REVERB,
		P_CHORUS,
		P_GAIN,
		NUM_PARAMS = P_GAIN + TRACKS
	};
	enum InputId {
		NUM_INPUTS
	};
	enum OutputId {
		O_L,
		O_R,
		O_TRACK,
		NUM_OUTPUTS = O_TRACK + TRACKS
	};
	enum LightId {
		/** Lit once the bank has been read and the module can make a sound. */
		L_BANK,
		NUM_LIGHTS
	};

	SoundCore core;
	/** Whether a Guitar Chart is beside it, for the panel. */
	std::atomic<bool> beside{false};


	ExpanderModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		//? How loud the whole band is, on every output. A General MIDI bank is quiet on
		//? purpose, so that a hundred instruments can play at once without clipping; this makes
		//? that up.
		configParam(P_LEVEL, 0.f, 4.f, 1.f, "Level");
		//? How much the playing is moved about — the timing, the loudness and the lengths. At
		//? nought the same notes come out the same way every time.
		configParam(P_HUMANISE, 0.f, 2.f, 1.f, "Humanise", "%", 0.f, 100.f);
		//? The synthesiser's own reverb, on the stereo pair only.
		configSwitch(P_REVERB, 0.f, 1.f, 1.f, "Reverb", {"Off", "On"});
		//? Its chorus, likewise.
		configSwitch(P_CHORUS, 0.f, 1.f, 1.f, "Chorus", {"Off", "On"});
		for (int t = 0; t < TRACKS; t++) {
			//? This track's loudness, on its own output and in the stereo pair.
			configParam(P_GAIN + t, 0.f, 2.f, 1.f, string::f("Gain %d", t + 1), "%", 0.f, 100.f);
			configOutput(O_TRACK + t, string::f("Track %d", t + 1));
		}
		configOutput(O_L, "Band left");
		configOutput(O_R, "Band right");
		core.setParts(TRACKS, true);
		core.logName = "mpxGuitarChartExpander";
		core.followInstrument = true;
		core.readRules();
	}

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
		core.startEngine(rate, params[P_REVERB].getValue() > 0.5f,
			params[P_CHORUS].getValue() > 0.5f);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		if (!core.engine.bankPath().empty())
			json_object_set_new(rootJ, "bank", json_string(core.engine.bankPath().c_str()));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* bankJ = json_object_get(rootJ, "bank");
		if (json_is_string(bankJ) && json_string_value(bankJ)[0] != '\0')
			core.loadBank(json_string_value(bankJ));
	}

	void process(const ProcessArgs& args) override {
		if (!core.engine.running()) {
			for (int o = 0; o < NUM_OUTPUTS; o++) {
				outputs[o].setChannels(1);
				outputs[o].setVoltage(0.f);
			}
			return;
		}
		lights[L_BANK].setBrightness(core.playing() ? 1.f : 0.f);
		core.step(args.sampleTime, NULL, params[P_HUMANISE].getValue(),
			params[P_REVERB].getValue() > 0.5f, params[P_CHORUS].getValue() > 0.5f);

		const float level = params[P_LEVEL].getValue() * 5.f;    // To Rack's ten volts peak.
		float left = core.wetLeft(), right = core.wetRight();
		for (int t = 0; t < TRACKS; t++) {
			const float gain = params[P_GAIN + t].getValue();
			// A CHANNEL A STRING for a fretted part, so a guitar's cable has six; anything else
			// has one for every voice it can hold.
			const int strings = core.instrument[t].stringCount;
			const int channels = (strings > 0) ? std::min(strings, core.voices) : core.voices;
			Output& out = outputs[O_TRACK + t];
			out.setChannels(channels);
			for (int v = 0; v < core.voices; v++) {
				const float l = core.voiceLeft(t, v) * gain;
				const float r = core.voiceRight(t, v) * gain;
				left += l;
				right += r;
				// Both sides as one, at the level a centred sound has on either side.
				if (v < channels)
					out.setVoltage((l + r) * 0.7071f * level, v);
			}
		}
		outputs[O_L].setVoltage(left * level);
		outputs[O_R].setVoltage(right * level);
	}
};


// ---- the panel ---------------------------------------------------------------------------------

static const float PANEL_W = 50.8f;       /**< Ten HP. */
/** THE ROWS LINE UP WITH THE CHART'S: the same six heights, in two columns of six, so a track's
output sits level with its name on the chart beside it. */
static const float ROW_TOP = 66.f;
static const float ROW_STEP = 11.f;
static const float COLUMN_W = PANEL_W / 2.f;
static const float NUMBER_DX = 3.f;
static const float GAIN_DX = 9.5f;
static const float JACK_DX = 18.5f;


/** Which bank is in, as a reading. Choosing another is in the menu. */
struct ExpanderBank : widget::Widget {
	ExpanderModule* module = NULL;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;
		std::string text = "no bank";
		if (module) {
			if (module->core.busy.load())
				text = "loading";
			else if (module->core.failed.load() || module->core.haveBank.load())
				text = module->core.message;
			if (!module->beside.load())
				text = "not beside a Guitar Chart";
		}
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 8.f);
		nvgFillColor(vg, PANEL_INK);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int pass = 0; pass < 2; pass++)
			nvgText(vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
	}
};


static Layout expanderLayout() {
	Layout L;
	L.hp = 10.f;
	L.title = "mpxGuitarChartExpander";

	auto label = [&](const char* key, float x, float y, const char* text, Panel::Align align,
			bool heading, float size, const char* owner) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.defaultText = text; i.align = align; i.heading = heading; i.size = size;
		if (owner)
			i.owner = owner;
		L.items.push_back(i);
	};
	auto param = [&](const char* key, int id, const char* style, float x, float y) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = style; i.x = x; i.y = y;
		if (std::string(style) == "latch")
			i.diameter = 5.6f;
		L.items.push_back(i);
	};
	auto jack = [&](const char* key, int id, float x, float y) {
		Item i;
		i.key = key; i.kind = Item::PORT_OUT; i.id = id; i.x = x; i.y = y; i.ring = SIG_AUDIO;
		L.items.push_back(i);
	};

	// WHICH BANK IS IN, and whether there is a chart to play.
	Item bank;
	bank.key = "d.bank"; bank.kind = Item::DISPLAY;
	bank.x = 8.f; bank.y = 24.f; bank.w = PANEL_W - 10.f; bank.h = 6.f;
	L.items.push_back(bank);
	Item lit;
	lit.key = "lamp.bank"; lit.kind = Item::LIGHT; lit.id = ExpanderModule::L_BANK;
	lit.x = 4.5f; lit.y = bank.y + bank.h / 2.f;
	L.items.push_back(lit);

	// The effects, each a latch with its name beside it.
	param("p.reverb", ExpanderModule::P_REVERB, "latch", 6.f, 35.f);
	label("p.reverb.label", 10.f, 35.f, "REVERB", Panel::LEFT, true, 7.f, "p.reverb");
	param("p.chorus", ExpanderModule::P_CHORUS, "latch", 26.f, 35.f);
	label("p.chorus.label", 30.f, 35.f, "CHORUS", Panel::LEFT, true, 7.f, "p.chorus");

	// The band's loudness, how much the playing moves, and the whole band out.
	param("p.level", ExpanderModule::P_LEVEL, "knob.small", 6.f, 47.f);
	label("p.level.label", 6.f, 55.5f, "LEVEL", Panel::CENTRE, true, 7.f, "p.level");
	param("p.humanise", ExpanderModule::P_HUMANISE, "knob.small", 18.5f, 47.f);
	label("p.humanise.label", 18.5f, 55.5f, "HUMANISE", Panel::CENTRE, true, 7.f, "p.humanise");
	label("h.mix", 38.5f, 40.5f, "MIX", Panel::CENTRE, true, 7.f, NULL);
	jack("out.l", ExpanderModule::O_L, 33.f, 47.f);
	label("out.l.label", 33.f, 55.5f, "L", Panel::CENTRE, true, 7.f, "out.l");
	jack("out.r", ExpanderModule::O_R, 44.f, 47.f);
	label("out.r.label", 44.f, 55.5f, "R", Panel::CENTRE, true, 7.f, "out.r");

	// THE TRACKS: the row's number, its gain and its output.
	for (int c = 0; c < 2; c++) {
		const float x = c * COLUMN_W;
		for (int i = 0; i < TRACKS_PER_COLUMN; i++) {
			const int t = c * TRACKS_PER_COLUMN + i;
			const float y = ROW_TOP + i * ROW_STEP;
			param(string::f("p.gain%d", t + 1).c_str(), ExpanderModule::P_GAIN + t, "knob.trim",
				x + GAIN_DX, y);
			jack(string::f("out.track%d", t + 1).c_str(), ExpanderModule::O_TRACK + t,
				x + JACK_DX, y);
			const std::string key = string::f("h.track%d", t + 1);
			const std::string text = string::f("%d", t + 1);
			Item n;
			n.key = key; n.kind = Item::LABEL; n.x = x + NUMBER_DX; n.y = y; n.text = text;
			n.defaultText = text; n.align = Panel::CENTRE; n.heading = false; n.size = 8.f;
			L.items.push_back(n);
		}
	}

	L.bindOffsets();
	return L;
}


struct ExpanderWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	ExpanderWidget(ExpanderModule* module) {
		setModule(module);
		layout = expanderLayout();
		layoutApplyUser("mpxGuitarChartExpander", layout);
		panel = new Panel;
		addChild(panel);

		ExpanderBank* bank = new ExpanderBank;
		bank->module = module;
		layoutPlaceDisplay(this, layout, "d.bank", bank);

		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxGuitarChartExpander");
		ExpanderModule* m = dynamic_cast<ExpanderModule*>(module);
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuItem("Choose a SoundFont…", "", [=]() {
			if (m->core.busy.load())
				return;
			std::string dir = SoundCore::bankFolder();
			system::createDirectories(dir);
			osdialog_filters* filters = osdialog_filters_parse("SoundFont:sf2,SF2");
			char* path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
			osdialog_filters_free(filters);
			if (!path)
				return;
			m->core.loadBank(path);
			std::free(path);
		}));
		if (!m->core.engine.bankPath().empty())
			menu->addChild(createMenuLabel(m->core.engine.bankPath()));
	}

	/** THE LINK TO THE CHART, made here on the main thread as a cable's is: the chart's row r is
	its output r, and the bus behind it is asked for exactly as a cable plugged into that output
	would ask. The chart's own name for each row becomes the name of this module's output, less
	the word MPX, since these outputs are audio. */
	void step() override {
		ModuleWidget::step();
		ExpanderModule* m = dynamic_cast<ExpanderModule*>(module);
		if (!m)
			return;
		Module* left = m->leftExpander.module;
		const bool chart = left && left->model == modelMpxGuitarChart;
		m->beside = chart;
		NoteSource* source = chart ? dynamic_cast<NoteSource*>(left) : NULL;
		for (int t = 0; t < TRACKS; t++) {
			int slot = -1;
			uint32_t gen = 0;
			if (source)
				slot = source->busSlotFor(t, &gen);
			m->core.link(t, &slot, &gen, (slot >= 0) ? 1 : 0);

			std::string name = string::f("Track %d", t + 1);
			if (chart && t < (int) left->outputInfos.size()) {
				const std::string theirs = left->outputInfos[(size_t) t]->name;
				if (theirs.compare(0, 4, "MPX ") == 0 && theirs.size() > 4) {
					name = theirs.substr(4);
					name[0] = (char) std::toupper((unsigned char) name[0]);
				}
			}
			m->outputInfos[(size_t) (ExpanderModule::O_TRACK + t)]->name = name;
		}
	}
};


} // namespace px


Model* modelMpxGuitarChartExpander =
	createModel<px::ExpanderModule, px::ExpanderWidget>("mpxGuitarChartExpander");
