/** mpxSound — a SoundFont band, played from MPX cables. See docs/guitar-player-spec.md.

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
#include "FluidEngine.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"

#include <osdialog.h>

#include <atomic>
#include <thread>

namespace px {


/** How many parts. Six because a band is a rhythm section and a few voices on top, and because
six parts of eight channels sit inside the sixty-four a synth is given. */
static const int PARTS = 6;
/** Channels per part: as many as a guitar has strings, and two spare for a part that turns out
to want more voices than it has strings. */
static const int PART_CHANNELS = 8;
static const int CHANNELS = PARTS * PART_CHANNELS + 2;
static const int POLYPHONY = 256;

/** How many frames are rendered at a time. About one and a third milliseconds at 48k, which is
finer than the ear places an onset, and a power of two. */
static const int BLOCK = 64;

/** Where a bank is kept, and the one that is looked for when nothing has been chosen. */
static std::string bankFolder() {
	return asset::user("DreamerMPX/banks");
}


/** One sounding note of one part. A note carries its own length, so its end is known when it
starts and nothing has to arrive to stop it. */
struct SoundVoice {
	bool on = false;
	int channel = 0;
	int key = 0;
	int64_t handle = 0;
	double remaining = 0.0;      /**< Seconds of it left to sound. */
};


struct SoundModule : Module, NoteSink {
	enum ParamId {
		P_LEVEL,
		P_REVERB,
		P_CHORUS,
		P_MUTE,
		NUM_PARAMS = P_MUTE + PARTS
	};
	enum InputId {
		I_PART,
		NUM_INPUTS = I_PART + PARTS
	};
	enum OutputId {
		O_L,
		O_R,
		NUM_OUTPUTS
	};
	enum LightId {
		L_PART,
		NUM_LIGHTS = L_PART + PARTS
	};

	FluidEngine engine;
	/** The bank is read on a worker, since thirty megabytes of samples is not something to do
	between two audio callbacks. Nothing sounds while it is loading. */
	std::atomic<bool> busy{false};
	std::atomic<bool> haveBank{false};
	std::atomic<bool> failed{false};
	std::thread worker;
	std::string wantBank;
	std::string message;

	/** Which sound each part plays, as a place in the bank's own list. */
	int sound[PARTS] = {};
	std::string soundName[PARTS];

	BusReader reader[PARTS];
	std::atomic<bool> relink{false};
	std::atomic<int> wantCount[PARTS];
	std::atomic<int> wantSlots[PARTS][MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[PARTS][MAX_UPSTREAM];

	SoundVoice voices[PARTS][PART_CHANNELS];
	float active[PARTS] = {};

	float rate = 44100.f;
	bool reverbOn = true, chorusOn = true;
	float blockL[BLOCK] = {};
	float blockR[BLOCK] = {};
	int blockAt = BLOCK;


	SoundModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		//? How loud the whole band is. A General MIDI bank is quiet on purpose, so that a
		//? hundred instruments can play at once without clipping; this makes that up.
		configParam(P_LEVEL, 0.f, 4.f, 1.f, "Level");
		//? The synthesiser's own reverb, which is the one the Ultimate Guitar player uses.
		configSwitch(P_REVERB, 0.f, 1.f, 1.f, "Reverb", {"Off", "On"});
		//? Its chorus, likewise.
		configSwitch(P_CHORUS, 0.f, 1.f, 1.f, "Chorus", {"Off", "On"});
		for (int p = 0; p < PARTS; p++) {
			//? Silences this part without unpatching it.
			configSwitch(P_MUTE + p, 0.f, 1.f, 0.f, string::f("Mute %d", p + 1),
				{"Playing", "Muted"});
			configInput(I_PART + p, string::f("MPX part %d", p + 1));
			sound[p] = -1;
			wantCount[p] = 0;
			for (int i = 0; i < MAX_UPSTREAM; i++) {
				wantSlots[p][i] = -1;
				wantGenerations[p][i] = 0;
			}
		}
		configOutput(O_L, "Left");
		configOutput(O_R, "Right");
	}

	~SoundModule() {
		if (worker.joinable())
			worker.join();
	}

	bool isMPXInputId(int id) override {
		return id >= I_PART && id < I_PART + PARTS;
	}

	void link(int part, const int* slots, const uint32_t* generations, int n) {
		if (part < 0 || part >= PARTS)
			return;
		bool same = (n == wantCount[part].load());
		for (int i = 0; i < n && same; i++) {
			same = (wantSlots[part][i].load() == slots[i]
				&& wantGenerations[part][i].load() == generations[i]);
		}
		if (same)
			return;
		for (int i = 0; i < n; i++) {
			wantSlots[part][i] = slots[i];
			wantGenerations[part][i] = generations[i];
		}
		wantCount[part] = n;
		relink = true;
	}

	// ---- the bank ------------------------------------------------------------------------------

	/** Reads a bank, on a thread of its own. Main thread. */
	void loadBank(const std::string& path) {
		if (busy.load())
			return;
		if (worker.joinable())
			worker.join();
		wantBank = path;
		busy = true;
		haveBank = false;
		failed = false;
		message = "loading";
		worker = std::thread([this]() {
			const bool ok = engine.running() && engine.loadBank(wantBank);
			if (ok) {
				message = engine.bankName();
				applyAsked();
				chooseDefaults();
				haveBank = true;
			}
			else {
				message = engine.reason();
				failed = true;
			}
			busy = false;
		});
	}

	/** WHAT EACH PART PLAYS BEFORE ANYBODY CHOOSES. A band rather than six pianos: a guitar for
	the first two parts, a bass, a kit, a piano and an organ, which is what an imported song most
	often holds. Any sound in the bank can be chosen afterwards. */
	void chooseDefaults() {
		const std::vector<FluidPreset>& list = engine.presets();
		if (list.empty())
			return;
		static const int WANT[PARTS][2] = {
			{0, 29}, {0, 27}, {0, 33}, {128, 0}, {0, 0}, {0, 16},
		};
		for (int p = 0; p < PARTS; p++) {
			if (sound[p] >= 0 && sound[p] < (int) list.size())
				continue;                      // Already chosen, or read from the patch.
			int found = -1;
			for (size_t i = 0; i < list.size() && found < 0; i++) {
				if (list[i].bank == WANT[p][0] && list[i].program == WANT[p][1])
					found = (int) i;
			}
			sound[p] = (found >= 0) ? found : 0;
		}
		for (int p = 0; p < PARTS; p++)
			soundName[p] = (sound[p] >= 0 && sound[p] < (int) list.size())
				? list[(size_t) sound[p]].name : "";
	}

	void setSound(int part, int which) {
		const std::vector<FluidPreset>& list = engine.presets();
		if (part < 0 || part >= PARTS || which < 0 || which >= (int) list.size())
			return;
		sound[part] = which;
		soundName[part] = list[(size_t) which].name;
		// Every one of the part's channels, so each string plays the same instrument.
		for (int c = 0; c < PART_CHANNELS; c++)
			engine.program(part * PART_CHANNELS + c, list[(size_t) which].bank,
				list[(size_t) which].program);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		if (!engine.bankPath().empty())
			json_object_set_new(rootJ, "bank", json_string(engine.bankPath().c_str()));
		json_t* partsJ = json_array();
		for (int p = 0; p < PARTS; p++) {
			json_t* one = json_object();
			const std::vector<FluidPreset>& list = engine.presets();
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
		const std::vector<FluidPreset>& list = engine.presets();
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

	// THE SYNTH IS MADE ON THE MAIN THREAD, here and when the rate changes. Making it inside
	// process() would allocate in the audio callback, and creating a synthesiser is a great deal
	// of allocation.
	void onAdd(const AddEvent& e) override {
		rate = APP->engine->getSampleRate();
		startEngine();
	}

	void onRemove(const RemoveEvent& e) override {
		if (worker.joinable())
			worker.join();
		engine.stop();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		rate = e.sampleRate;
		startEngine();
	}

	void startEngine() {
		if (worker.joinable())
			worker.join();
		if (!engine.start((double) rate, CHANNELS, POLYPHONY))
			return;
		for (int c = 0; c < CHANNELS; c++)
			engine.bendRange(c, 12);
		engine.effects(params[P_REVERB].getValue() > 0.5f,
			params[P_CHORUS].getValue() > 0.5f);
		if (!engine.bankPath().empty()) {
			loadBank(engine.bankPath());
			return;
		}
		// A BANK THAT IS ALREADY THERE IS THE ONE TO USE. A module that comes up silent and asks
		// for a file is a module somebody thinks is broken, so the folder is looked in first and
		// the only thing in it is taken. Choosing another is still a press away.
		const std::string found = firstBank();
		if (!found.empty())
			loadBank(found);
	}

	/** The first SoundFont in the banks folder, or nothing. */
	static std::string firstBank() {
		const std::string dir = bankFolder();
		if (!system::isDirectory(dir))
			return "";
		std::vector<std::string> names = system::getEntries(dir);
		std::sort(names.begin(), names.end());
		for (size_t i = 0; i < names.size(); i++) {
			const std::string ext = system::getExtension(names[i]);
			if (ext == ".sf2" || ext == ".SF2")
				return names[i];
		}
		return "";
	}

	/** A channel for a note: the one its string uses where the cable says which string, and
	otherwise the next one that is free. */
	int take(int part, const Event& e) {
		SoundVoice* room = NULL;
		double oldest = 1e9;
		for (int c = 0; c < PART_CHANNELS; c++) {
			SoundVoice& v = voices[part][c];
			if (!v.on)
				return c;
			if (v.remaining < oldest) {
				oldest = v.remaining;
				room = &v;
			}
		}
		// Every channel sounding: the one with least left to sound gives way, which is the note
		// closest to ending anyway.
		if (room) {
			engine.noteOff(room->channel, room->key);
			room->on = false;
			return (int) (room - &voices[part][0]);
		}
		return 0;
	}

	void process(const ProcessArgs& args) override {
		if (!engine.running()) {
			outputs[O_L].setVoltage(0.f);
			outputs[O_R].setVoltage(0.f);
			return;
		}
		if (relink.exchange(false)) {
			for (int p = 0; p < PARTS; p++) {
				reader[p].clear();
				const int n = wantCount[p].load();
				for (int i = 0; i < n; i++)
					reader[p].add(wantSlots[p][i].load(), wantGenerations[p][i].load());
			}
		}
		const bool playing = haveBank.load() && !busy.load();

		// The effects follow their buttons, and only when one moves.
		const bool wantReverb = params[P_REVERB].getValue() > 0.5f;
		const bool wantChorus = params[P_CHORUS].getValue() > 0.5f;
		if (wantReverb != reverbOn || wantChorus != chorusOn) {
			reverbOn = wantReverb;
			chorusOn = wantChorus;
			engine.effects(reverbOn, chorusOn);
		}

		// ---- the notes ----
		for (int p = 0; p < PARTS; p++) {
			const bool muted = params[P_MUTE + p].getValue() > 0.5f;
			Event e;
			while (reader[p].next(e)) {
				if (!playing || muted)
					continue;
				if (e.kind == Event::ON) {
					const int c = take(p, e);
					SoundVoice& v = voices[p][c];
					v.on = true;
					v.channel = p * PART_CHANNELS + c;
					v.key = math::clamp((int) std::lround(60.f + 12.f * e.pitch), 0, 127);
					v.handle = e.handle;
					v.remaining = (e.duration > 0.f) ? (double) e.duration : 1e9;
					engine.bend(v.channel, 0.f);
					engine.noteOn(v.channel, v.key,
						math::clamp((int) std::lround(e.level * 127.f), 1, 127));
					active[p] = 1.f;
				}
				else if (e.kind == Event::OFF) {
					for (int c = 0; c < PART_CHANNELS; c++) {
						SoundVoice& v = voices[p][c];
						if (v.on && v.handle == e.handle) {
							engine.noteOff(v.channel, v.key);
							v.on = false;
						}
					}
				}
				else {
					// The continuing values, each aimed at one sounding note: a bend moves that
					// string's channel alone, which is the whole reason for a channel per string.
					for (int c = 0; c < PART_CHANNELS; c++) {
						SoundVoice& v = voices[p][c];
						if (!v.on || v.handle != e.handle)
							continue;
						if (e.lane == LANE_BEND)
							engine.bend(v.channel, e.value * e.bendRange * 100.f);
						else if (e.lane == LANE_TIMBRE)
							engine.controller(v.channel, 74,
								math::clamp((int) std::lround(e.value * 127.f), 0, 127));
						else if (e.lane == LANE_PRESSURE)
							engine.controller(v.channel, 11,
								math::clamp((int) std::lround(e.value * 127.f), 0, 127));
					}
				}
			}

			// A NOTE CARRIES ITS OWN LENGTH, so it ends itself. Nothing has to arrive to stop it,
			// and a cable pulled out mid-note leaves nothing sounding for ever.
			for (int c = 0; c < PART_CHANNELS; c++) {
				SoundVoice& v = voices[p][c];
				if (!v.on)
					continue;
				v.remaining -= args.sampleTime;
				if (v.remaining <= 0.0) {
					engine.noteOff(v.channel, v.key);
					v.on = false;
				}
			}
			active[p] = std::fmax(0.f, active[p] - args.sampleTime * 3.f);
			lights[L_PART + p].setBrightness(active[p]);
		}

		// ---- the audio ----
		if (blockAt >= BLOCK) {
			if (playing)
				engine.render(blockL, blockR, BLOCK);
			else {
				for (int i = 0; i < BLOCK; i++)
					blockL[i] = blockR[i] = 0.f;
			}
			blockAt = 0;
		}
		const float level = params[P_LEVEL].getValue() * 5.f;    // To Rack's ten volts peak.
		outputs[O_L].setVoltage(blockL[blockAt] * level);
		outputs[O_R].setVoltage(blockR[blockAt] * level);
		blockAt++;
	}
};


// ---- the panel ---------------------------------------------------------------------------------

static const float PANEL_W = 121.92f;      /**< Twenty-four HP. */
static const float JACK_X = 9.f;
static const float MUTE_X = 20.f;
static const float NAME_X = 28.f;
static const float ROW_TOP = 40.f;
static const float ROW_STEP = 13.f;


/** The bank, and a press chooses one. */
struct SoundBank : widget::OpaqueWidget {
	SoundModule* module = NULL;

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			choose();
			e.consume(this);
			return;
		}
		widget::OpaqueWidget::onButton(e);
	}

	void choose() {
		if (!module || module->busy.load())
			return;
		std::string dir = bankFolder();
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
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 3.f);
		nvgFillColor(vg, nvgRGB(0x2a, 0x2f, 0x36));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(0xcf, 0xcf, 0xcf, 0x90));
		nvgStrokeWidth(vg, 1.3f);
		nvgStroke(vg);

		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;
		std::string text = "CHOOSE A SOUNDFONT";
		if (module) {
			if (module->busy.load())
				text = "LOADING";
			else if (module->failed.load())
				text = module->message;
			else if (module->haveBank.load())
				text = module->message;
		}
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 11.f);
		nvgFillColor(vg, nvgRGB(0xff, 0xff, 0xff));
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
				&& module->haveBank.load()) {
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
		const std::vector<FluidPreset>& list = m->engine.presets();
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
			if (module && module->haveBank.load() && !module->soundName[p].empty())
				name = module->soundName[p];
			nvgFillColor(vg, PANEL_INK);
			for (int pass = 0; pass < 2; pass++)
				nvgText(vg, 2.f, rowY[p], name.c_str(), NULL);
		}
	}
};


static Layout soundLayout() {
	Layout L;
	L.hp = 24.f;
	L.title = "mpxSound";

	auto label = [&](const char* key, float x, float y, const char* text, Panel::Align align,
			bool heading, const char* owner) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.defaultText = text; i.align = align; i.heading = heading;
		if (owner)
			i.owner = owner;
		L.items.push_back(i);
	};

	Item bank;
	bank.key = "d.bank"; bank.kind = Item::DISPLAY;
	bank.x = 4.f; bank.y = 24.f; bank.w = PANEL_W - 8.f; bank.h = 10.f;
	L.items.push_back(bank);

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
		mute.style = "latch"; mute.diameter = 6.6f; mute.x = MUTE_X; mute.y = y;
		L.items.push_back(mute);

		Item lamp;
		lamp.key = string::f("lamp.part%d", p + 1);
		lamp.kind = Item::LIGHT; lamp.id = SoundModule::L_PART + p;
		lamp.x = 3.5f; lamp.y = y;
		L.items.push_back(lamp);
	}
	label("h.in", JACK_X, ROW_TOP - 7.f, "mpx IN", Panel::CENTRE, true, NULL);
	label("h.mute", MUTE_X, ROW_TOP - 7.f, "MUTE", Panel::CENTRE, true, NULL);
	label("h.sound", NAME_X + 8.f, ROW_TOP - 7.f, "SOUND", Panel::LEFT, true, NULL);

	Item parts;
	parts.key = "d.parts"; parts.kind = Item::DISPLAY;
	parts.x = NAME_X; parts.y = ROW_TOP - ROW_STEP / 2.f;
	parts.w = PANEL_W - NAME_X - 3.f; parts.h = PARTS * ROW_STEP;
	L.items.push_back(parts);

	const float y = ROW_TOP + PARTS * ROW_STEP + 6.f;
	Item level;
	level.key = "p.level"; level.kind = Item::PARAM; level.id = SoundModule::P_LEVEL;
	level.style = "knob"; level.x = 12.f; level.y = y;
	L.items.push_back(level);
	label("p.level.label", 12.f, y + 8.5f, "LEVEL", Panel::CENTRE, true, "p.level");

	Item verb;
	verb.key = "p.reverb"; verb.kind = Item::PARAM; verb.id = SoundModule::P_REVERB;
	verb.style = "latch"; verb.diameter = 6.6f; verb.x = 30.f; verb.y = y;
	L.items.push_back(verb);
	label("p.reverb.label", 30.f, y + 7.f, "REVERB", Panel::CENTRE, true, "p.reverb");

	Item chorus;
	chorus.key = "p.chorus"; chorus.kind = Item::PARAM; chorus.id = SoundModule::P_CHORUS;
	chorus.style = "latch"; chorus.diameter = 6.6f; chorus.x = 46.f; chorus.y = y;
	L.items.push_back(chorus);
	label("p.chorus.label", 46.f, y + 7.f, "CHORUS", Panel::CENTRE, true, "p.chorus");

	auto jack = [&](const char* key, int id, float x, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PORT_OUT; i.id = id; i.x = x; i.y = y;
		i.ring = SIG_AUDIO;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 7.5f, name, Panel::CENTRE, false, key);
	};
	jack("out.l", SoundModule::O_L, PANEL_W - 24.f, "L");
	jack("out.r", SoundModule::O_R, PANEL_W - 12.f, "R");

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
		layoutApplyUser("mpxSound", layout);
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
		layoutAppendMenu(menu, this, panel, &layout, "mpxSound");
		SoundModule* m = dynamic_cast<SoundModule*>(module);
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuItem("Choose a SoundFont…", "", [=]() {
			if (bankDisplay)
				bankDisplay->choose();
		}));
		if (!m->engine.bankPath().empty())
			menu->addChild(createMenuLabel(m->engine.bankPath()));
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


Model* modelMpxSound = createModel<px::SoundModule, px::SoundWidget>("mpxSound");
