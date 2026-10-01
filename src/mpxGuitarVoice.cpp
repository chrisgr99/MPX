/** mpxGuitarVoice — an articulated MPX part played through any oscillator. See
docs/guitar-voice.md.

THE PERFORMANCE IS HERE AND THE SOUND IS NOT. The pitch output, one channel per string, goes to an
oscillator of the user's choosing, and the oscillator's audio comes back into the return input.
Everything else a played part needs is done to that audio here: each string struck and dying away
as the performer says, at the level it was played, placed where the file puts it, and the strings
mixed to stereo. So the patch is two cables to an oscillator and a stereo pair to a mixer, where it
was an oscillator, an envelope, an amplifier and a polyphonic mixer with the right voltages patched
between them.

THE PERFORMER IS mpxGuitarist'S. The same library and the same rules file, so a hammer-on is the
same hammer-on whichever module plays it: no new strike, the pitch gliding under an envelope that
is already sounding. See Perform.hpp; what is done to the audio is GuitarVoice.hpp.
*/
#include "plugin.hpp"
#include "GuitarVoice.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"
#include "Perform.hpp"

namespace px {


static std::string rulesPath() {
	return asset::user("DreamerMPX/perform.txt");
}


/** THE ENVELOPE CONTROLS ARE EXPONENTIAL, nought to one across a range of times, so each part of
the range gets its share of the knob's travel: a millisecond and two are as far apart as a second
and two. */
static const float ATTACK_MIN = 0.001f, ATTACK_RATIO = 1000.f;     // 1 ms to 1 s
static const float DECAY_MIN = 0.1f, DECAY_RATIO = 200.f;          // 0.1 s to 20 s
static const float RELEASE_MIN = 0.005f, RELEASE_RATIO = 400.f;    // 5 ms to 2 s

/** The filter envelope's own times, and how far it moves the cutoff. */
static const float FATTACK_MIN = 0.001f, FATTACK_RATIO = 1000.f;   // 1 ms to 1 s
static const float FDECAY_MIN = 0.02f, FDECAY_RATIO = 500.f;       // 20 ms to 10 s
static const float FDEPTH_MAX = 6.f;                               // octaves

/** The cutoff for a string at middle C, 20 Hz to 20 kHz across the knob. */
static const float CUTOFF_MIN = 20.f, CUTOFF_RATIO = 1000.f;

static float timeOf(float knob, float least, float ratio) {
	return least * std::pow(ratio, knob);
}

static float knobFor(float seconds, float least, float ratio) {
	return std::log(seconds / least) / std::log(ratio);
}


struct GuitarVoiceModule : Module, NoteSink {
	enum ParamId {
		P_ATTACK,
		P_DECAY,
		P_RELEASE,
		P_CUTOFF,
		P_RESONANCE,
		P_KEYTRACK,
		P_SLOPE,
		P_FILTER,
		P_FATTACK,
		P_FDECAY,
		P_FDEPTH,
		NUM_PARAMS
	};
	enum InputId {
		I_MPX,
		I_RETURN,
		NUM_INPUTS
	};
	enum OutputId {
		O_PITCH,
		O_L,
		O_R,
		O_ENV,
		NUM_OUTPUTS
	};
	enum LightId {
		NUM_LIGHTS
	};

	Performer performer;
	GuitarVoices voices;
	BusReader reader;
	Instrument instrument;
	uint32_t instrumentChange = 0;

	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
	std::atomic<bool> relink{false};
	PerformRules loadedRules;
	std::atomic<bool> rulesReady{false};
	std::string rulesMessage;

	bool attachedWas = false;


	GuitarVoiceModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		//? How long a struck string takes to reach its level, from 1 ms, a hard pick, to 1 s, a
		//? swell.
		configParam(P_ATTACK, 0.f, 1.f, knobFor(0.002f, ATTACK_MIN, ATTACK_RATIO), "Attack",
			" ms", ATTACK_RATIO, ATTACK_MIN * 1000.f);
		//? How long a held string takes to die away by 60 dB, from 0.1 s, a muted plunk, to
		//? 20 s. A string has no sustain level: it falls for as long as it is held.
		configParam(P_DECAY, 0.f, 1.f, knobFor(3.f, DECAY_MIN, DECAY_RATIO), "Decay", " s",
			DECAY_RATIO, DECAY_MIN);
		//? How long a string takes to fall silent by 60 dB once its note has ended, from 5 ms
		//? to 2 s.
		configParam(P_RELEASE, 0.f, 1.f, knobFor(0.08f, RELEASE_MIN, RELEASE_RATIO), "Release",
			" ms", RELEASE_RATIO, RELEASE_MIN * 1000.f);
		//? The filter's cutoff for a string at middle C, 20 Hz to 20 kHz. Key tracking moves it
		//? with each string's pitch, and the performer's timbre moves it darker for a palm mute
		//? or a dead note and brighter for a harmonic.
		configParam(P_CUTOFF, 0.f, 1.f, knobFor(2000.f, CUTOFF_MIN, CUTOFF_RATIO),
			"Cutoff", " Hz", CUTOFF_RATIO, CUTOFF_MIN);
		//? Emphasis at the cutoff, from a flat response to a peak about 20 dB high.
		configParam(P_RESONANCE, 0.f, 1.f, 0.1f, "Resonance", "%", 0.f, 100.f);
		//? How far the cutoff follows each string's pitch: at 100% an octave for every octave,
		//? so a setting that suits the low strings does not muffle the high ones.
		configParam(P_KEYTRACK, 0.f, 1.f, 1.f, "Key tracking", "%", 0.f, 100.f);
		//? The filter's slope: 12 dB per octave, gentle, or 24, steep.
		configSwitch(P_SLOPE, 0.f, 1.f, 0.f, "Slope", {"12 dB per octave", "24 dB per octave"});
		//? Lit, each string goes through the filter; unlit, its audio goes straight to its
		//? envelope, as the oscillator made it.
		configSwitch(P_FILTER, 0.f, 1.f, 1.f, "Filter", {"Off", "On"});
		//? How long each string's filter envelope takes to rise, from 1 ms to 1 s.
		configParam(P_FATTACK, 0.f, 1.f, knobFor(0.001f, FATTACK_MIN, FATTACK_RATIO),
			"Filter attack", " ms", FATTACK_RATIO, FATTACK_MIN * 1000.f);
		//? How long each string's filter envelope takes to fall by 60 dB after its peak, from
		//? 20 ms to 10 s, whether the note is held or not: the brightness of a pluck dying away.
		configParam(P_FDECAY, 0.f, 1.f, knobFor(0.4f, FDECAY_MIN, FDECAY_RATIO), "Filter decay",
			" s", FDECAY_RATIO, FDECAY_MIN);
		//? How far the filter envelope raises the cutoff at its peak, from nothing to six
		//? octaves.
		configParam(P_FDEPTH, 0.f, FDEPTH_MAX, 2.f, "Filter envelope depth", " octaves");
		configInput(I_MPX, "MPX note");
		//? The oscillator's audio, one channel per string, in the order the pitch output sends
		//? them. A single channel is used for every string, which only sounds right for a part
		//? that plays one note at a time.
		configInput(I_RETURN, "Return audio");
		//? Volts per octave, one channel per string, with the bends, slides and vibrato in it.
		//? Patch it to a polyphonic oscillator and the oscillator's output to the return.
		configOutput(O_PITCH, "1V/oct, as played");
		configOutput(O_L, "Left");
		configOutput(O_R, "Right");
		//? Each string's filter envelope as 0 to 10 V, one channel per string as the pitch
		//? output has them, for driving anything else in time with the plucks.
		configOutput(O_ENV, "Filter envelope");
		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		performer.seed(1u);
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

	/** Reads the rules, the same file mpxGuitarist reads. Main thread. */
	void readRules() {
		const std::string path = rulesPath();
		PerformRules rules;
		if (system::isFile(path)) {
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
			rulesMessage = complaint.empty() ? string::f("%d rules read", set) : complaint;
		}
		else
			rulesMessage = "no rules file; the defaults are in use";
		loadedRules = rules;
		rulesReady.store(true);
	}

	void onReset() override {
		performer.silence();
		voices.silence();
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

		// THE CABLE GOING IS EVERYTHING STOPPING, as on mpxGuitarist: a let-ring note waits for
		// its string to be struck again, and a cable pulled out means it never will be.
		const bool attached = reader.attached();
		if (!attached && attachedWas)
			performer.silence();
		attachedWas = attached;

		Instrument in;
		if (reader.instrument(in) && in.change != instrumentChange) {
			instrumentChange = in.change;
			instrument = in;
			performer.strings(in.stringCount);
		}

		// HUMANISE AT ITS OWN AMOUNT, as the built-in band has it.
		performer.humanise(1.f);

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
				for (int i = 0; i < performer.voiceCount(); i++) {
					if (performer.voice(i).gate && performer.voice(i).handle == e.handle)
						performer.silenceVoice(i);
				}
			}
			else if (e.lane == LANE_PRESSURE || e.lane == LANE_TIMBRE) {
				for (int i = 0; i < performer.voiceCount(); i++) {
					if (performer.voice(i).gate && performer.voice(i).handle == e.handle)
						performer.set(i, e.lane == LANE_TIMBRE, e.value);
				}
			}
		}

		performer.advance(args.sampleTime);

		// WHAT THE PERFORMER DECIDED, applied to the strings. A strike starts the envelope; a
		// hammer-on is not a strike, only a new level, so the envelope carries on under it. The
		// bends are in the voice's pitch, read below.
		PerformMessage m;
		while (performer.next(m)) {
			switch (m.kind) {
				case PerformMessage::ATTACK:
					voices.strike(m.voice, m.value);
					break;
				case PerformMessage::LEVEL:
					voices.setLevel(m.voice, m.value);
					break;
				case PerformMessage::RELEASE:
					voices.end(m.voice);
					break;
				default:
					break;
			}
		}

		voices.times(timeOf(params[P_ATTACK].getValue(), ATTACK_MIN, ATTACK_RATIO),
			timeOf(params[P_DECAY].getValue(), DECAY_MIN, DECAY_RATIO),
			timeOf(params[P_RELEASE].getValue(), RELEASE_MIN, RELEASE_RATIO));

		// WHERE EACH STRING SITS: the note's own place, moved by where the file puts the track.
		// And what moves its filter: its pitch as played, and its brightness.
		for (int v = 0; v < GuitarVoices::VOICES && v < performer.voiceCount(); v++) {
			const PerformVoice& pv = performer.voice(v);
			voices.pan[v] = math::clamp(pv.pan + instrument.pan, -1.f, 1.f);
			voices.pitch[v] = pv.pitch;
			voices.timbre[v] = pv.timbre;
		}
		voices.filter.cutoff = CUTOFF_MIN * std::pow(CUTOFF_RATIO, params[P_CUTOFF].getValue());
		voices.filter.resonance = params[P_RESONANCE].getValue();
		voices.filter.keyTracking = params[P_KEYTRACK].getValue();
		voices.filter.steep = params[P_SLOPE].getValue() > 0.5f;
		voices.filter.on = params[P_FILTER].getValue() > 0.5f;
		voices.filter.envelopeDepth = params[P_FDEPTH].getValue();
		voices.filterTimes(timeOf(params[P_FATTACK].getValue(), FATTACK_MIN, FATTACK_RATIO),
			timeOf(params[P_FDECAY].getValue(), FDECAY_MIN, FDECAY_RATIO));

		// ---- the pitch out ----
		//
		// A CHANNEL PER STRING, as many as the instrument has, as mpxGuitarist does.
		int channels = instrument.stringCount;
		if (channels <= 0) {
			for (int i = 0; i < performer.voiceCount(); i++) {
				if (performer.voice(i).gate || voices.envelope[i].sounding())
					channels = i + 1;
			}
			if (channels <= 0)
				channels = 1;
		}
		channels = std::min(channels, std::min(16, (int) GuitarVoices::VOICES));
		outputs[O_PITCH].setChannels(channels);
		for (int c = 0; c < channels; c++)
			outputs[O_PITCH].setVoltage(performer.voice(c).pitch, c);

		// ---- the sound coming back, played ----
		float back[16] = {};
		const int count = inputs[I_RETURN].getChannels();
		for (int c = 0; c < count && c < 16; c++)
			back[c] = inputs[I_RETURN].getVoltage(c);
		float left = 0.f, right = 0.f;
		if (count > 0)
			voices.process(back, count, args.sampleTime, &left, &right);
		else {
			// Nothing to play, but the envelopes still move, so a return patched in mid-note
			// arrives where the note has got to.
			const float silent = 0.f;
			voices.process(&silent, 1, args.sampleTime, &left, &right);
		}
		outputs[O_L].setVoltage(left);
		outputs[O_R].setVoltage(right);

		// THE FILTER ENVELOPES, after this sample's step, on as many channels as the pitch.
		outputs[O_ENV].setChannels(channels);
		for (int c = 0; c < channels; c++)
			outputs[O_ENV].setVoltage(voices.filterEnvelope[c].value * 10.f, c);
	}
};


// ---- the panel ---------------------------------------------------------------------------------

static const float PANEL_W = 71.12f;      /**< Fourteen HP. */
static const float CENTRE = PANEL_W / 2.f;
/** THREE COLUMNS OF KNOBS: the envelope every string follows, the filter every string goes
through, and the envelope that moves the filter. */
static const float LEFT = 13.f;
static const float MIDDLE = CENTRE;
static const float RIGHT = PANEL_W - 13.f;


/** What is on the cable. */
struct GuitarVoiceDisplay : widget::Widget {
	GuitarVoiceModule* module = NULL;

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


static Layout guitarVoiceLayout() {
	Layout L;
	L.hp = PANEL_W / 5.08f;
	L.title = "mpxGuitarVoice";

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
	auto knob = [&](const char* key, int id, float x, float y, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "knob"; i.x = x; i.y = y;
		L.items.push_back(i);
		Item n;
		n.key = std::string(key) + ".label"; n.kind = Item::LABEL; n.x = x; n.y = y + 8.5f;
		n.text = name; n.defaultText = name; n.align = Panel::CENTRE; n.heading = true;
		n.size = 9.f; n.owner = key;
		L.items.push_back(n);
	};

	// THE TWO ENDS OF THE PATCH TOGETHER AT THE TOP: the part coming in, and its pitch going to
	// the oscillator.
	jack("in.mpx", Item::PORT_IN, 10.f, 17.f, GuitarVoiceModule::I_MPX, "mpx IN", NOTE_CABLE);
	jack("out.pitch", Item::PORT_OUT, PANEL_W - 10.f, 17.f, GuitarVoiceModule::O_PITCH,
		"1V/oct", SIG_PITCH);

	Item what;
	what.key = "d.instrument"; what.kind = Item::DISPLAY;
	what.x = 3.f; what.y = 28.f; what.w = PANEL_W - 6.f; what.h = 6.f;
	L.items.push_back(what);

	// The oscillator's audio coming back.
	jack("in.return", Item::PORT_IN, CENTRE, 40.f, GuitarVoiceModule::I_RETURN, "return",
		SIG_AUDIO);

	auto heading = [&](const char* key, float x, const char* text) {
		Item n;
		n.key = key; n.kind = Item::LABEL; n.x = x; n.y = 53.f;
		n.text = text; n.defaultText = text; n.align = Panel::CENTRE; n.heading = true;
		n.size = 9.f;
		L.items.push_back(n);
	};
	heading("h.amp", LEFT, "ENVELOPE");
	heading("h.filter", MIDDLE, "FILTER");
	heading("h.fenv", RIGHT, "FILTER ENV");

	knob("p.attack", GuitarVoiceModule::P_ATTACK, LEFT, 62.f, "ATTACK");
	knob("p.decay", GuitarVoiceModule::P_DECAY, LEFT, 80.f, "DECAY");
	knob("p.release", GuitarVoiceModule::P_RELEASE, LEFT, 98.f, "RELEASE");
	knob("p.cutoff", GuitarVoiceModule::P_CUTOFF, MIDDLE, 62.f, "CUTOFF");
	knob("p.resonance", GuitarVoiceModule::P_RESONANCE, MIDDLE, 80.f, "RESONANCE");
	knob("p.keytrack", GuitarVoiceModule::P_KEYTRACK, MIDDLE, 98.f, "KEY TRACK");
	knob("p.fattack", GuitarVoiceModule::P_FATTACK, RIGHT, 62.f, "ATTACK");
	knob("p.fdecay", GuitarVoiceModule::P_FDECAY, RIGHT, 80.f, "DECAY");
	knob("p.fdepth", GuitarVoiceModule::P_FDEPTH, RIGHT, 98.f, "DEPTH");

	// The band out, the filter envelopes out, and the filter's two switches.
	jack("out.l", Item::PORT_OUT, 10.f, 114.f, GuitarVoiceModule::O_L, "L", SIG_AUDIO);
	jack("out.r", Item::PORT_OUT, 21.f, 114.f, GuitarVoiceModule::O_R, "R", SIG_AUDIO);
	jack("out.env", Item::PORT_OUT, 34.f, 114.f, GuitarVoiceModule::O_ENV, "env", SIG_CV);

	// THE FILTER ON OR OFF, and its slope: latches, lit for on and for the steeper one.
	auto latch = [&](const char* key, int id, float x, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "latch"; i.diameter = 5.6f;
		i.x = x; i.y = 113.f;
		L.items.push_back(i);
		Item n;
		n.key = std::string(key) + ".label"; n.kind = Item::LABEL; n.x = x; n.y = 120.5f;
		n.text = name; n.defaultText = name; n.align = Panel::CENTRE; n.heading = true;
		n.size = 9.f; n.owner = key;
		L.items.push_back(n);
	};
	latch("p.filter", GuitarVoiceModule::P_FILTER, 50.f, "FILTER");
	latch("p.slope", GuitarVoiceModule::P_SLOPE, 61.f, "24 dB");

	L.bindOffsets();
	return L;
}


struct GuitarVoiceWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	GuitarVoiceWidget(GuitarVoiceModule* module) {
		setModule(module);
		layout = guitarVoiceLayout();
		layoutApplyUser("mpxGuitarVoice", layout);
		panel = new Panel;
		addChild(panel);
		GuitarVoiceDisplay* what = new GuitarVoiceDisplay;
		what->module = module;
		layoutPlaceDisplay(this, layout, "d.instrument", what);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxGuitarVoice");
		GuitarVoiceModule* m = dynamic_cast<GuitarVoiceModule*>(module);
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

	/** The MPX link, made here on the main thread, as every MPX input's is. */
	void step() override {
		ModuleWidget::step();
		GuitarVoiceModule* m = dynamic_cast<GuitarVoiceModule*>(module);
		if (!m)
			return;
		PortWidget* port = getInput(GuitarVoiceModule::I_MPX);
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


Model* modelMpxGuitarVoice =
	createModel<px::GuitarVoiceModule, px::GuitarVoiceWidget>("mpxGuitarVoice");
