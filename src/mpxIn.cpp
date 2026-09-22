#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

namespace px {


/** How often the continuing values are looked at. About one and a half milliseconds at 44.1k,
which is finer than breath or a bend wheel needs, and a power of two so the test is a mask. */
static const int UPDATE_EVERY = 64;
/** Movement below this is not worth a message. A twentieth of a cent of pitch, and a
thousandth of the range of pressure and timbre. */
static const float DEADBAND = 0.0002f;

static const float GATE_HIGH = 1.f;
static const float GATE_LOW = 0.1f;


struct NoteModule : Module, NoteSource {
	enum ParamId {
		P_LEVEL,
		P_DURATION,
		P_PAN,
		P_BEND_RANGE,
		P_ENDS,
		NUM_PARAMS
	};
	enum InputId {
		I_GATE,
		I_PITCH,
		I_LEVEL,
		I_DURATION,
		I_PAN,
		I_PRESSURE,
		I_TIMBRE,
		/** THE PEDALS, APPENDED so saved patches keep their cables. A gate is a pedal down; a
		voltage between nought and ten is a pedal part way down, for half-pedalling. They go on
		the cable as state — see busPublishPedals in NoteBus.hpp. */
		I_SUSTAIN,
		I_SOFT,
		NUM_INPUTS
	};
	enum OutputId {
		O_VOICE,
		NUM_OUTPUTS
	};
	enum LightId {
		L_ACTIVE,
		NUM_LIGHTS
	};

	/** One of these per channel of the gate input, so a polyphonic source makes polyphonic
	notes on ONE note cable. This is the whole reason the transport carries events: sixteen
	channels of voltage would have been one note. */
	struct Channel {
		bool on = false;
		bool gateHigh = false;
		int64_t handle = 0;
		float heldPitch = 0.f;
		int age = 0;
		int len = 0;
		float bendSent = 0.f;
		float pressureSent = 0.f;
		float timbreSent = 0.f;
		bool everSent = false;
	};
	Channel channels[16];

	int slot = -1;
	uint32_t generation = 0;
	int updatePhase = 0;


	NoteModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_LEVEL, 0.f, 1.f, 0.8f, "Level", "%", 0.f, 100.f);
		// Exponential, because the useful range runs from a grace note to a held chord and a
		// linear knob spends most of its travel above a second.
		configParam(P_DURATION, std::log2(0.01f), std::log2(30.f), std::log2(0.25f),
			"Duration", " s", 2.f);
		configParam(P_PAN, -1.f, 1.f, 0.f, "Pan");
		// Semitones, but not whole ones. A quarter-tone bend, a scale that is not twelve-tone
		// and a range set by ear are all ordinary things to want, and a knob that stopped only
		// on integers would refuse all three.
		// Zero is a real setting and not a mistake: it means any movement at all reaches full
		// deflection, which is the most sensitive the control output can be. The volts-per-octave
		// bend output is unscaled either way, so nothing is lost there.
		configParam(P_BEND_RANGE, 0.f, 12.f, 2.f, "Bend range", " semitones");
		// DURATION FIRST, because a note carrying its own length is what this whole domain is
		// about; the gate cutting one short is the exception. The second position is named for
		// what it does rather than for which signal it watches: the gate can only ever end a
		// note EARLY, since duration is a maximum in both positions.
		configSwitch(P_ENDS, 0.f, 1.f, 0.f, "Note ends",
			{"Duration", "Gate or duration"});

		configInput(I_GATE, "Gate");
		configInput(I_PITCH, "1V/oct");
		configInput(I_LEVEL, "Level");
		configInput(I_DURATION, "Duration");
		configInput(I_PAN, "Pan");
		configInput(I_PRESSURE, "Pressure");
		configInput(I_TIMBRE, "Timbre");
		configInput(I_SUSTAIN, "Sustain pedal");
		configInput(I_SOFT, "Soft pedal");
		configOutput(O_VOICE, "MPX note");

		slot = busClaim(&generation);
	}

	~NoteModule() {
		busRelease(slot);
	}

	void onReset() override {
		for (Channel& c : channels)
			c = Channel();
	}

	int busSlotFor(int outputId, uint32_t* generation) override {
		if (outputId != O_VOICE)
			return -1;
		if (generation)
			*generation = this->generation;
		return slot;
	}

	void process(const ProcessArgs& args) override {
		// A note cable carries no voltage. It is a real cable so that Rack owns it — draws it,
		// saves it, undoes it, removes it with either module — and the events travel through
		// the bus. Patching it into an oscillator therefore does nothing rather than something
		// surprising.
		outputs[O_VOICE].setChannels(1);
		outputs[O_VOICE].setVoltage(0.f);

		// THE PEDALS, every sample, as state: whatever is listening knows where the pedal is the
		// moment it starts listening. Unpatched is up.
		if (slot >= 0) {
			const float sustain = inputs[I_SUSTAIN].isConnected()
				? math::clamp(inputs[I_SUSTAIN].getVoltage() / 10.f, 0.f, 1.f) : 0.f;
			const float soft = inputs[I_SOFT].isConnected()
				? math::clamp(inputs[I_SOFT].getVoltage() / 10.f, 0.f, 1.f) : 0.f;
			busPublishPedals(slot, sustain, soft);
		}


		// The gate decides how many notes this source can sound at once. A monophonic gate
		// makes one; a sixteen-channel one from a poly sequencer or MIDI-CV makes sixteen, all
		// on the one cable.
		int count = std::max(1, inputs[I_GATE].getChannels());

		bool update = (updatePhase == 0);
		updatePhase = (updatePhase + 1) & (UPDATE_EVERY - 1);

		const float bendRange = params[P_BEND_RANGE].getValue();
		const bool holds = params[P_ENDS].getValue() < 0.5f;
		bool anyOn = false;

		for (int c = 0; c < 16; c++) {
			Channel& ch = channels[c];

			// A channel that has gone away ends its note rather than leaving a voice sounding
			// at the far end for ever.
			if (c >= count) {
				if (ch.on) {
					sendOff(ch);
					ch.on = false;
				}
				ch.gateHigh = false;
				continue;
			}

			const float g = inputs[I_GATE].getPolyVoltage(c);
			const float pitchNow = inputs[I_PITCH].getPolyVoltage(c);

			// Hysteresis on the gate, at Rack's own thresholds, so a signal sitting on the
			// edge does not chatter out a hundred notes.
			bool wasHigh = ch.gateHigh;
			if (ch.gateHigh ? (g < GATE_LOW) : (g >= GATE_HIGH))
				ch.gateHigh = !ch.gateHigh;

			// THE RISING EDGE IS THE NOTE, and everything the note carries is read here once
			// and then held. Holding is what makes a note a note: a source whose pitch keeps
			// moving after the gate — an unquantised drift, or the next step of a sequence
			// arriving early — must not drag a sounding note around with it.
			if (!wasHigh && ch.gateHigh) {
				if (ch.on)
					sendOff(ch);

				Event e;
				e.kind = Event::ON;
				e.handle = ch.handle = mintHandle();
				e.pitch = ch.heldPitch = pitchNow;
				// Each of these three takes its cable where there is one and its knob where
				// there is not, so a bare gate still makes a complete note.
				e.level = inputs[I_LEVEL].isConnected()
					? clamp(inputs[I_LEVEL].getPolyVoltage(c) / 10.f, 0.f, 1.f)
					: params[P_LEVEL].getValue();
				// A duration cable is in seconds — one volt is one second — so it reads the
				// same way the knob does and can come from a sequencer's own step length with
				// nothing converting anything.
				e.duration = inputs[I_DURATION].isConnected()
					? clamp(inputs[I_DURATION].getPolyVoltage(c), 0.001f, 30.f)
					: std::pow(2.f, params[P_DURATION].getValue());
				e.pan = inputs[I_PAN].isConnected()
					? clamp(inputs[I_PAN].getPolyVoltage(c) / 5.f, -1.f, 1.f)
					: params[P_PAN].getValue();
				e.bendRange = bendRange;
				busPush(slot, e);

				ch.on = true;
				ch.age = 0;
				ch.len = std::max(1, (int) std::round(e.duration * args.sampleRate));
				// Bend is measured from the held value, so it is exactly zero at every
				// note-on by construction and the source patches one ordinary moving voltage
				// without ever knowing that handles or updates exist.
				ch.bendSent = 0.f;
				ch.everSent = false;
			}

			if (ch.on) {
				ch.age++;
				// Duration is a maximum rather than the arbiter. HOLD lets it decide alone, so
				// a note outlives its gate and the next one can begin while it is still
				// sounding — which is what lets one source overlap notes into one voice pool.
				if ((!ch.gateHigh && !holds) || ch.age >= ch.len) {
					sendOff(ch);
					ch.on = false;
				}
			}

			// THE CONTINUING VALUES, sent on change and not on a clock. A control that is not
			// moving sends nothing at all, which is most of them most of the time.
			if (ch.on && update) {
				const float dv = pitchNow - ch.heldPitch;
				if (!ch.everSent || std::fabs(dv - ch.bendSent) > DEADBAND) {
					ch.bendSent = dv;
					// SENT IN VOLTS, RAW. The range belongs to the control-voltage output at
					// the far end, where it says how many semitones count as full deflection.
					// Scaling here would make the volts-per-octave output a clamped copy of
					// the control one, when the whole point of it is that held pitch plus it
					// is exactly where the source has gone.
					sendUpdate(ch, LANE_BEND, dv);
				}
				// An unpatched input says nothing at all rather than sending zeros: a voice
				// with no breath behind it should fall back to its own envelope, not be held
				// shut by a lane that is reporting silence.
				if (inputs[I_PRESSURE].isConnected()) {
					const float v = clamp(inputs[I_PRESSURE].getPolyVoltage(c) / 10.f, 0.f, 1.f);
					if (!ch.everSent || std::fabs(v - ch.pressureSent) > DEADBAND) {
						ch.pressureSent = v;
						sendUpdate(ch, LANE_PRESSURE, v);
					}
				}
				if (inputs[I_TIMBRE].isConnected()) {
					const float v = clamp(inputs[I_TIMBRE].getPolyVoltage(c) / 10.f, 0.f, 1.f);
					if (!ch.everSent || std::fabs(v - ch.timbreSent) > DEADBAND) {
						ch.timbreSent = v;
						sendUpdate(ch, LANE_TIMBRE, v);
					}
				}
				ch.everSent = true;
			}

			anyOn |= ch.on;
		}

		lights[L_ACTIVE].setBrightnessSmooth(anyOn ? 1.f : 0.f, args.sampleTime);

	}

	void sendOff(const Channel& ch) {
		Event e;
		e.kind = Event::OFF;
		e.handle = ch.handle;
		busPush(slot, e);
	}

	void sendUpdate(const Channel& ch, Lane lane, float value) {
		Event e;
		e.kind = Event::UPDATE;
		e.lane = (uint8_t) lane;
		e.handle = ch.handle;
		e.value = value;
		busPush(slot, e);
	}
};


int noteBusOf(engine::Module* module, int outputId, uint32_t* generation) {
	// Asked of the capability, not of the class. Any module that can put notes on a cable
	// answers this, so a native source needs no special case at the far end.
	NoteSource* source = dynamic_cast<NoteSource*>(module);
	if (!source)
		return -1;
	return source->busSlotFor(outputId, generation);
}


// ---- panel -------------------------------------------------------------------------------
// LAID OUT LIKE DREAMRACK'S SEQUENCE OUT: a knob with the jack that overrides it directly
// beneath, so the pair reads as one setting with two ways of arriving. What is read at the
// gate's edge is grouped together, what is followed while the note sounds is grouped together,
// and the cables this all becomes are down the right under OUT.
//
// The one thing DreamRack does not have to show is four cables out. It has one, because a page
// is one instrument; here four instruments is four cables, so OUT is a column of its own.

// YOUR ARRANGEMENT, TIDIED. Three columns — the jacks, the knobs that stand in for them when
// nothing is patched, and bend on its own — with the rows stepping by one pitch rather than by
// whatever each drag happened to land on. Where a knob and a jack are the same setting they now
// share a row exactly, which is the whole reason they are side by side.

/** SIX HP: two columns, the jacks and the knobs that stand in for them, with the rows as they were
arranged by hand and evened up. Notes end at and Bend range are set once per patch rather than
turned while playing, so they are in the right-click menu rather than on the face. */
static const float JACK_X = 7.5f;
static const float KNOB_X = 20.5f;

static Layout mpxInLayout() {
	Layout L;
	L.hp = 6.f;
	L.title = "mpxIn";
	L.titleAbove = "DREAMER DEVELOPMENT";

	auto label = [&](const char* key, float x, float y, const char* text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const char* owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	// `below` is how far under the jack its name sits. The bottom four sit closer, so four jacks
	// with names fit in the space that held two.
	auto jack = [&](const char* key, Item::Kind kind, float x, float y, int id,
			const char* name, NVGcolor color, float below = 7.5f, float size = 0.f) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = color;
		L.items.push_back(i);
		if (name && name[0])
			label((std::string(key) + ".label").c_str(), x, y + below, name,
				Panel::CENTRE, false, size, key);
	};
	auto knob = [&](const char* key, float x, float y, int id, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y;
		i.style = "knob.large";
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 8.5f, name,
			Panel::CENTRE, true, 0.f, key);
	};

	// What arrives, and at the top of the second column the one cable that leaves.
	jack("in.pitch", Item::PORT_IN, JACK_X, 18.5f, NoteModule::I_PITCH, "1V/oct", SIG_PITCH);
	jack("in.gate", Item::PORT_IN, JACK_X, 36.f, NoteModule::I_GATE, "gate", SIG_GATE);

	Item out;
	out.key = "out.voice"; out.kind = Item::PORT_OUT; out.id = NoteModule::O_VOICE;
	out.x = KNOB_X; out.y = 18.5f; out.ring = NOTE_CABLE;
	L.items.push_back(out);
	label("h.out", KNOB_X, 26.f, "mpx OUT", Panel::CENTRE, true, 0.f, "out.voice");
	Item lamp;
	lamp.key = "lamp.active"; lamp.kind = Item::LIGHT; lamp.id = NoteModule::L_ACTIVE;
	lamp.x = KNOB_X; lamp.y = 32.5f; lamp.owner = "out.voice";
	L.items.push_back(lamp);

	// THE JACK AND ITS KNOB ON ONE ROW. Each pair is one setting: the knob is what the note
	// carries, and a cable in the jack beside it takes over.
	jack("in.level", Item::PORT_IN, JACK_X, 54.f, NoteModule::I_LEVEL, "", SIG_CV);
	knob("p.level", KNOB_X, 54.f, NoteModule::P_LEVEL, "LEVEL");
	jack("in.dur", Item::PORT_IN, JACK_X, 74.f, NoteModule::I_DURATION, "", SIG_CV);
	knob("p.dur", KNOB_X, 74.f, NoteModule::P_DURATION, "DURATION");
	jack("in.pan", Item::PORT_IN, JACK_X, 93.5f, NoteModule::I_PAN, "", SIG_CV);
	knob("p.pan", KNOB_X, 93.5f, NoteModule::P_PAN, "PAN");

	// Followed while the note sounds, then the pedals: four jacks, names tucked close.
	jack("in.press", Item::PORT_IN, JACK_X, 107.f, NoteModule::I_PRESSURE, "pressure", SIG_CV,
		5.5f, 7.f);
	jack("in.timb", Item::PORT_IN, KNOB_X, 107.f, NoteModule::I_TIMBRE, "timbre", SIG_CV, 5.5f, 7.f);
	jack("in.sustain", Item::PORT_IN, JACK_X, 119.5f, NoteModule::I_SUSTAIN, "sustain", SIG_GATE,
		5.5f, 7.f);
	jack("in.soft", Item::PORT_IN, KNOB_X, 119.5f, NoteModule::I_SOFT, "soft", SIG_GATE, 5.5f, 7.f);

	L.bindOffsets();
	return L;
}


struct NoteWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	NoteWidget(NoteModule* module) {
		setModule(module);
		layout = mpxInLayout();
		layoutApplyUser("mpxIn", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxIn");
		NoteModule* m = dynamic_cast<NoteModule*>(module);
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
		// SET ONCE PER PATCH, so here rather than on the face. Both are still params, so they are
		// saved with the patch and can be mapped like anything else.
		menu->addChild(createSubmenuItem("Notes end at",
			m->params[NoteModule::P_ENDS].getValue() < 0.5f ? "Duration" : "Gate or duration",
			[=](ui::Menu* sub) {
				sub->addChild(createCheckMenuItem("Their duration, whatever the gate does", "",
					[=]() { return m->params[NoteModule::P_ENDS].getValue() < 0.5f; },
					[=]() { m->params[NoteModule::P_ENDS].setValue(0.f); }));
				sub->addChild(createCheckMenuItem("The gate's fall or the duration, whichever is first", "",
					[=]() { return m->params[NoteModule::P_ENDS].getValue() >= 0.5f; },
					[=]() { m->params[NoteModule::P_ENDS].setValue(1.f); }));
			}));
		menu->addChild(createSubmenuItem("Bend range",
			string::f("%g semitones", m->params[NoteModule::P_BEND_RANGE].getValue()),
			[=](ui::Menu* sub) {
				const float ranges[] = {0.f, 1.f, 2.f, 3.f, 4.f, 5.f, 7.f, 12.f};
				for (float r : ranges)
					sub->addChild(createCheckMenuItem(string::f("%g semitones", r), "",
						[=]() { return std::fabs(m->params[NoteModule::P_BEND_RANGE].getValue() - r) < 0.01f; },
						[=]() { m->params[NoteModule::P_BEND_RANGE].setValue(r); }));
			}));
	}

	/** MAGENTA MEANS THE LINK WORKS, not merely that the cable left an MPX jack.

	Rack cannot refuse a connection — every output reaches every input — so a cable from here to
	an oscillator is something anybody can make. It does no harm, since this output puts zero
	volts on the wire, but colouring it like a working note cable would say it was one.

	So the colour is left alone unless the far end can actually receive notes. A magenta cable
	is a link; one in Rack's own colours came out of an MPX jack and goes nowhere that listens. */
	void step() override {
		ModuleWidget::step();
		if (!module)
			return;
		PortWidget* port = getOutput(NoteModule::O_VOICE);
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


Model* modelMpxIn = createModel<px::NoteModule, px::NoteWidget>("mpxIn");
