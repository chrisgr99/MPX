/** mpxModelPiano — a piano modelled rather than recorded, played from an MPX cable.
See docs/piano-model.md.

DEPRECATED, AND KEPT ON A BRANCH RATHER THAN DEVELOPED. It works and it is correct about a great
many things no sample set can do — half-pedalling, a note restruck before its damper has landed, a
chord held silently and set ringing by another — and it does not sound as good as a recording of a
real piano. Measured against the Salamander set it is right about the treble balance, the tuning to
under a cent and the hammer's contact times, and wrong about three things that each need real work:
its brightness barely changes with how hard a note is struck where a real piano's changes by a
factor of two and a half, its attack is five to twenty times too fast, and its bass rings a third
as long as it should while its tenor rings twice too long.

AND THE FAULTS THAT MADE THE SAMPLED PIANO SOUND BAD TURNED OUT NOT TO BE THE SAMPLES. Every group
volume in the sample map had been lost, so the key-release noise played thirty-seven decibels hot
and was louder than the music; and neither piano read a note's own length from the cable, so
nothing was ever damped when played from a score. With those fixed the sampled piano is good
enough, which is the honest reason this one stops here rather than any fault in it.

ITS OWN NAME, AND THE SAME NAME IN ITS SLUG. This was arranged the other way round first, with the
modelled piano taking the plain name on its face and carrying a different one in its slug, because
a slug is what a saved patch stores and the sampler's could not move without emptying every rack it
is in. It worked, and it left two modules whose displayed names and stored names were different
things, each needing a comment to explain why. A name of its own ends all of it: the sampler stays
mpxPiano, which is what it has always been called, and this is mpxModelPiano in both places.

NO SAMPLES, AND SO NO DOWNLOAD. Every string of the instrument is here, two hundred and sixty of
them on one bridge, and what comes out is what they do. The sampled piano beside it is better at
what a recording is better at; this one is better at everything a recording was not doing when it
was made — a key released while the pedal is half down, a note struck again before its damper has
landed, a chord held silently and set ringing by something else.

ONLY AN MPX CABLE IN, as with the sampler: ordinary Rack signals reach it through mpxIn, which
turns pitch, gate and level into notes and carries the two pedals as well.

WHAT IS NOT HERE YET. The soundboard is phase 6 and until it exists this is the force on the
bridge, which is the right signal with nothing yet to radiate it — so the sound is bare and
somewhat thin, and that is the next phase rather than a fault. The mechanical noises are phase 8
and the panel proper is phase 9. */
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"
#include "PianoKeys.hpp"

#include <atomic>
#include <cmath>
#include <thread>


namespace px {


struct ModelPianoModule : Module, NoteSink {
	enum ParamId {
		P_VOLUME,
		P_SUSTAIN,
		P_SOFT,
		P_SOSTENUTO,
		NUM_PARAMS
	};
	enum InputId { I_MPX, NUM_INPUTS };
	enum OutputId { O_OUT, NUM_OUTPUTS };
	enum LightId { L_SOUNDING, NUM_LIGHTS };

	BusReader reader;
	std::atomic<bool> relink{false};
	std::atomic<int> wantCount{0};
	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];

	PianoBoard board;

	/** THE KEYS, which is where the bookkeeping between a cable and an instrument lives. It is
	Rack-free on purpose: see PianoKeys.hpp, and test/pianorender.cpp, which plays it. */
	PianoKeys keys;

	// ---- building the instrument, which cannot happen on the audio thread ----
	//
	// HALF A SECOND OF ARITHMETIC. Every string's dispersion chain and loss filter is solved for,
	// and there are two hundred and sixty-four strings. That is nothing once and impossible in a
	// callback, so it is done on a thread of its own and the module is silent until it is ready.
	//
	// AND THE AUDIO THREAD MUST NOT BE INSIDE THE BOARD WHILE IT IS REBUILT. `usable` says whether
	// the board may be touched and `inProcess` says whether it is being touched; the builder
	// clears the first and then waits for the second, which is a handful of microseconds at most
	// because process returns promptly whatever else it does.
	std::atomic<bool> usable{false};
	std::atomic<bool> inProcess{false};
	std::atomic<bool> building{false};
	std::atomic<float> wantedRate{48000.f};
	std::thread builder;

	ModelPianoModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_VOLUME, 0.f, 2.f, 1.f, "Volume", "%", 0.f, 100.f);
		configSwitch(P_SUSTAIN, 0.f, 1.f, 0.f, "Sustain pedal", {"up", "down"});
		configSwitch(P_SOFT, 0.f, 1.f, 0.f, "Soft pedal", {"up", "down"});
		configSwitch(P_SOSTENUTO, 0.f, 1.f, 0.f, "Sostenuto pedal", {"up", "down"});
		configInput(I_MPX, "MPX note");
		configOutput(O_OUT, "Audio");
		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i] = -1;
			wantGenerations[i] = 0;
		}
		startBuild(48000.f);
	}

	~ModelPianoModule() {
		usable = false;
		if (builder.joinable())
			builder.join();
	}

	bool isMPXInputId(int id) override {
		return id == I_MPX;
	}

	/** NEVER FROM THE AUDIO THREAD. It waits for the audio thread to leave the board and then
	joins the last builder, either of which would stop a callback for as long as half a second —
	and the wait would never end, since the thread it is waiting for is the one that called it.
	Rack calls the constructor and onSampleRateChange from elsewhere, which is the whole of when
	this needs to happen. */
	void startBuild(float rate) {
		wantedRate = rate;
		// A BUILD ALREADY RUNNING PICKS THE NEW RATE UP RATHER THAN BEING IGNORED. Rack sets the
		// sample rate shortly after the module is made, so the second call lands while the first
		// build is still going — and returning here, which is what this did first, would have
		// left an instrument built for forty-eight thousand being played at forty-four thousand
		// and one, every note a tone and a half sharp.
		if (building.exchange(true))
			return;
		usable = false;
		// Wait for the audio thread to leave, if it is in there.
		while (inProcess.load()) {}
		if (builder.joinable())
			builder.join();
		builder = std::thread([this]() {
			for (;;) {
				const float rate = wantedRate.load();
				board.setSampleRate(rate);
				board.silence();
				if (wantedRate.load() == rate) {
					usable = true;
					building = false;
					return;
				}
				// Asked for a different rate while this one was being worked out.
			}
		});
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		startBuild(e.sampleRate);
	}

	void onReset() override {
		if (usable.load()) {
			keys.allUp(board);
			board.silence();
		}
	}

	void link(const int* slots, const uint32_t* generations, int n) {
		bool same = (n == wantCount.load());
		for (int i = 0; same && i < n; i++)
			same = (wantSlots[i].load() == slots[i] && wantGenerations[i].load() == generations[i]);
		if (same)
			return;
		for (int i = 0; i < n; i++) {
			wantSlots[i] = slots[i];
			wantGenerations[i] = generations[i];
		}
		wantCount = n;
		relink = true;
	}

	bool attachedWas = false;

	void process(const ProcessArgs& args) override {
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}

		if (!usable.load()) {
			// Still being built: read the cable so nothing piles up, and say nothing.
			Event e;
			while (reader.next(e)) {}
			outputs[O_OUT].setVoltage(0.f);
			lights[L_SOUNDING].setBrightness(0.f);
			return;
		}
		inProcess = true;
		if (!usable.load()) {
			inProcess = false;
			outputs[O_OUT].setVoltage(0.f);
			return;
		}

		const bool attached = reader.attached();
		if (!attached && attachedWas)
			keys.allUp(board);
		attachedWas = attached;

		// The pedals: whichever is further down, the button or the cable.
		float busSustain = 0.f, busSoft = 0.f;
		reader.pedals(busSustain, busSoft);
		board.setSustain(std::max(busSustain, params[P_SUSTAIN].getValue()));
		board.setSoft(std::max(busSoft, params[P_SOFT].getValue()));
		board.setSostenuto(params[P_SOSTENUTO].getValue() >= 0.5f);

		keys.setSampleRate(args.sampleRate);
		keys.step(board);

		Event e;
		while (reader.next(e)) {
			if (e.kind == Event::ON) {
				if (!keys.down(board, e.handle, e.pitch, e.level, e.duration))
					WARN("mpxModelPiano: more than %d keys at once; one will stay down",
						PianoKeys::MAX_KEYS);
			}
			else if (e.kind == Event::OFF)
				keys.up(board, e.handle);
			// UPDATE messages — bend, timbre, level — are read and dropped. A piano string
			// cannot be bent and its tone after the blow is not under anyone's hand.
		}

		// WHAT COMES OUT IS THE SOUNDBOARD'S SURFACE MOVING, in metres per second, which is a
		// number of the order of a hundredth. The scale was measured rather than guessed: both
		// hands at fortissimo peaks at about nine hundredths of a metre per second and a middle C
		// played mezzo-forte at about seven thousandths, so this puts the loudest playing just
		// under nine volts and leaves the rest of the range below it where it belongs.
		const float out = board.process() * (100.f * params[P_VOLUME].getValue());
		outputs[O_OUT].setVoltage(math::clamp(out, -10.f, 10.f));
		lights[L_SOUNDING].setBrightnessSmooth(board.awake() > 0 ? 1.f : 0.f, args.sampleTime);
		inProcess = false;
	}
};


// ---- the panel ---------------------------------------------------------------------------------

static const float PANEL_W = 6.f * RACK_GRID_WIDTH * 25.4f / 75.f;
static const float MID = PANEL_W / 2.f;
static const float PAIR_L = MID - 5.f;
static const float PAIR_R = MID + 5.f;

static Layout mpxModelPianoLayout() {
	Layout L;
	L.hp = 6.f;
	L.title = "mpxModelPiano";

	auto label = [&](const char* key, float x, float y, const char* text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const char* owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	auto knob = [&](const char* key, float x, float y, int id, const char* name, float diameter) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "knob";
		i.diameter = diameter;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 8.f, name, Panel::CENTRE, true, 0.f,
			key);
	};
	auto latch = [&](const char* key, float x, float y, int id, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "latch";
		i.diameter = 6.6f;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 7.f, name, Panel::CENTRE, true, 8.f,
			key);
	};
	auto jack = [&](const char* key, Item::Kind kind, float x, float y, int id, const char* name,
			NVGcolor ring) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = ring;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y - 8.f, name, Panel::CENTRE, true, 0.f,
			key);
	};

	knob("p.volume", MID, 24.f, ModelPianoModule::P_VOLUME, "VOLUME", 11.f);

	label("h.pedals", MID, 46.f, "PEDALS", Panel::CENTRE, true);
	latch("p.sustain", MID, 58.f, ModelPianoModule::P_SUSTAIN, "SUST");
	latch("p.soft", PAIR_L, 76.f, ModelPianoModule::P_SOFT, "SOFT");
	latch("p.sostenuto", PAIR_R, 76.f, ModelPianoModule::P_SOSTENUTO, "SOST");

	// A lamp saying whether anything is sounding, which while the instrument is still being
	// built is the only way to tell it apart from a module that is simply not working.
	Item lamp;
	lamp.key = "l.sounding"; lamp.kind = Item::LIGHT; lamp.id = ModelPianoModule::L_SOUNDING;
	lamp.x = MID; lamp.y = 94.f;
	L.items.push_back(lamp);

	jack("in.mpx", Item::PORT_IN, MID, 110.f, ModelPianoModule::I_MPX, "mpx IN", NOTE_CABLE);
	jack("out.audio", Item::PORT_OUT, MID, 119.5f, ModelPianoModule::O_OUT, "OUT", SIG_AUDIO);

	L.bindOffsets();
	return L;
}


struct ModelPianoWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	ModelPianoWidget(ModelPianoModule* module) {
		setModule(module);
		layout = mpxModelPianoLayout();
		layoutApplyUser("mpxModelPiano", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void step() override {
		ModuleWidget::step();
		ModelPianoModule* m = dynamic_cast<ModelPianoModule*>(module);
		if (!m)
			return;

		PortWidget* port = getInput(ModelPianoModule::I_MPX);
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

	void appendContextMenu(Menu* menu) override {
		ModelPianoModule* m = dynamic_cast<ModelPianoModule*>(module);
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("A modelled piano: every string, on one bridge"));
		layoutAppendMenu(menu, this, panel, &layout, "mpxModelPiano");
	}
};


} // namespace px


Model* modelMpxModelPiano = createModel<px::ModelPianoModule, px::ModelPianoWidget>("mpxModelPiano");
