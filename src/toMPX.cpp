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
			{"At its duration, whatever the gate does",
			 "At the gate's fall or its duration, whichever comes first"});

		configInput(I_GATE, "Gate");
		configInput(I_PITCH, "1V/oct");
		configInput(I_LEVEL, "Level");
		configInput(I_DURATION, "Duration");
		configInput(I_PAN, "Pan");
		configInput(I_PRESSURE, "Pressure");
		configInput(I_TIMBRE, "Timbre");
		configOutput(O_VOICE, "MPX note out \u2014 goes to an MPX input only");

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

static const float JACK_X = 9.5f;
static const float KNOB_X = 23.f;
static const float BEND_X = 36.5f;
static const float OUT_X = 48.f;
static const float ROW_TOP = 17.5f;
static const float ROW_PITCH = 19.5f;
/** The bend range ring, outside the knob rather than on it. */
static const float BEND_RING = 10.f;

static float row(int n) {
	return ROW_TOP + n * ROW_PITCH;
}

static Layout toMPXLayout() {
	Layout L;
	L.hp = 12.f;
	L.title = "toMPX";
	L.titleAbove = "DREAMER DEVELOPMENT";

	auto label = [&](const char* key, float x, float y, const char* text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const char* owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	// `name` may be empty, and then the jack gets none. THE KNOB BESIDE IT ALREADY SAYS WHAT IT
	// IS: a jack labelled "level" next to a knob labelled LEVEL is the same word twice, and the
	// second one only tells you that the panel was generated rather than laid out.
	auto jack = [&](const char* key, Item::Kind kind, float x, float y, int id,
			const char* name, NVGcolor color) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = color;
		L.items.push_back(i);
		if (name && name[0])
			label((std::string(key) + ".label").c_str(), x, y + 7.5f, name,
				Panel::CENTRE, false, 0.f, key);
	};
	auto knob = [&](const char* key, float x, float y, int id, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y;
		i.style = "knob.large";
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 8.5f, name,
			Panel::CENTRE, true, 0.f, key);
	};

	// What arrives. The gate is the note: everything else is read at its rising edge.
	jack("in.pitch", Item::PORT_IN, JACK_X, row(0), NoteModule::I_PITCH, "1V/oct", SIG_PITCH);
	jack("in.gate", Item::PORT_IN, JACK_X, row(1), NoteModule::I_GATE, "gate", SIG_GATE);

	// THE JACK AND ITS KNOB ON ONE ROW. Each pair is one setting: the knob is what the note
	// carries, and a cable in the jack beside it takes over.
	jack("in.level", Item::PORT_IN, JACK_X, row(2), NoteModule::I_LEVEL, "", SIG_CV);
	knob("p.level", KNOB_X, row(2), NoteModule::P_LEVEL, "LEVEL");
	jack("in.dur", Item::PORT_IN, JACK_X, row(3), NoteModule::I_DURATION, "", SIG_CV);
	knob("p.dur", KNOB_X, row(3), NoteModule::P_DURATION, "DURATION");
	jack("in.pan", Item::PORT_IN, JACK_X, row(4), NoteModule::I_PAN, "", SIG_CV);
	knob("p.pan", KNOB_X, row(4), NoteModule::P_PAN, "PAN");

	// These two only ever arrive on a cable: there is nothing sensible for a still control to
	// say, and an unpatched one sends nothing at all rather than sending zero.
	jack("in.press", Item::PORT_IN, JACK_X, row(5), NoteModule::I_PRESSURE, "pressure", SIG_CV);
	jack("in.timb", Item::PORT_IN, KNOB_X, row(5), NoteModule::I_TIMBRE, "timbre", SIG_CV);

	// Bend has no jack of its own: it is the pitch input's movement measured from what the note
	// started on, so all it needs is how far full deflection reaches.
	knob("p.bend", BEND_X, 28.f, NoteModule::P_BEND_RANGE, "BEND RANGE");
	for (int i = 0; i <= 6; i++) {
		const float a = (-0.78f + i / 6.f * 1.56f) * (float) M_PI;
		label(("p.bend.n" + std::to_string(i)).c_str(),
			BEND_X + std::sin(a) * BEND_RING, 28.f - std::cos(a) * BEND_RING,
			std::to_string(i * 2).c_str(), Panel::CENTRE, false, 7.f, "p.bend");
	}
	// LOWER THAN THE OTHER KNOB LABELS, because this is the only knob with numbers ringed
	// round it and the name has to clear them.
	L.find("p.bend.label")->y = 28.f + 11.f;

	// What ends a note, as two named lamps rather than a switch whose two positions are only
	// distinguishable by which way it is leaning — and headed, because two words on their own
	// say what they are but not what they are about.
	label("h.ends", BEND_X, 51.f, "NOTES END AT", Panel::CENTRE, true);
	Item ends;
	ends.key = "p.ends"; ends.kind = Item::PARAM; ends.id = NoteModule::P_ENDS;
	ends.style = "lamps"; ends.x = BEND_X - 3.f; ends.y = 56.f;
	ends.w = 6.f; ends.h = 18.f; ends.pitch = 11.f;
	// Read under the heading, each of these is a whole sentence: the note ends at its duration,
	// or at whichever of the gate and the duration comes first. Naming both signals in the
	// second is what says the gate can only ever end a note EARLY.
	ends.names = {"DURATION", "GATE OR\nDURATION"};
	ends.labelSide = Panel::RIGHT;
	L.items.push_back(ends);

	// One cable out. A polyphonic cable in Rack carries one instrument's voices, so a module
	// looking at one has one instrument to hand on.
	label("h.out", OUT_X, 105.f, "mpx\nOUT", Panel::CENTRE, true, 12.f);
	Item out;
	out.key = "out.voice"; out.kind = Item::PORT_OUT; out.id = NoteModule::O_VOICE;
	out.x = OUT_X; out.y = row(5); out.ring = NOTE_CABLE;
	L.items.push_back(out);
	Item lamp;
	lamp.key = "lamp.active"; lamp.kind = Item::LIGHT; lamp.id = NoteModule::L_ACTIVE;
	lamp.x = OUT_X - 8.f; lamp.y = row(5); lamp.owner = "out.voice";
	L.items.push_back(lamp);


	L.bindOffsets();
	return L;
}

struct NoteWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	NoteWidget(NoteModule* module) {
		setModule(module);
		layout = toMPXLayout();
		layoutApplyUser("toMPX", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "toMPX");
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


Model* modelToMPX = createModel<px::NoteModule, px::NoteWidget>("toMPX");
