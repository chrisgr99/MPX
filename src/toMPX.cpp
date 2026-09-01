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


struct NoteModule : Module {
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
		configParam(P_BEND_RANGE, 0.1f, 24.f, 2.f, "Bend range", " semitones");
		configSwitch(P_ENDS, 0.f, 1.f, 0.f, "Note ends", {"At the gate's fall", "At its duration"});

		configInput(I_GATE, "Gate");
		configInput(I_PITCH, "1V/oct");
		configInput(I_LEVEL, "Level");
		configInput(I_DURATION, "Duration");
		configInput(I_PAN, "Pan");
		configInput(I_PRESSURE, "Pressure");
		configInput(I_TIMBRE, "Timbre");
		configOutput(O_VOICE, "Voice");

		slot = busClaim(&generation);
	}

	~NoteModule() {
		busRelease(slot);
	}

	void onReset() override {
		for (Channel& c : channels)
			c = Channel();
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
		const bool holds = params[P_ENDS].getValue() > 0.5f;
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
	NoteModule* note = dynamic_cast<NoteModule*>(module);
	if (!note)
		return -1;
	if (outputId != NoteModule::O_VOICE)
		return -1;
	if (generation)
		*generation = note->generation;
	return note->slot;
}


// ---- panel -------------------------------------------------------------------------------
// LAID OUT LIKE DREAMRACK'S SEQUENCE OUT: a knob with the jack that overrides it directly
// beneath, so the pair reads as one setting with two ways of arriving. What is read at the
// gate's edge is grouped together, what is followed while the note sounds is grouped together,
// and the cables this all becomes are down the right under OUT.
//
// The one thing DreamRack does not have to show is four cables out. It has one, because a page
// is one instrument; here four instruments is four cables, so OUT is a column of its own.

static const float COL[3] = {14.f, 40.6f, 67.f};   // millimetres


static Layout toMPXLayout() {
	Layout L;
	L.hp = 16.f;
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
	auto inJack = [&](const char* key, float x, float y, int id, const char* name,
			NVGcolor color) {
		Item i;
		i.key = key; i.kind = Item::PORT_IN; i.id = id; i.x = x; i.y = y; i.ring = color;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 7.5f, name,
			Panel::CENTRE, false, 0.f, key);
	};
	auto knob = [&](const char* key, float x, float y, int id, const char* style) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = style;
		L.items.push_back(i);
	};

	// What arrives, at the top. The gate is the note: everything else is read at its edge.
	label("h.in", 9.f, 36.f, "IN", Panel::LEFT, true);
	inJack("in.gate", COL[0], 46.f, NoteModule::I_GATE, "gate", SIG_GATE);
	inJack("in.pitch", COL[1], 46.f, NoteModule::I_PITCH, "v/oct", SIG_PITCH);

	// What ends a note, as two named lamps rather than a switch whose two positions are only
	// distinguishable by which way it is leaning.
	label("h.ends", 9.f, 51.f, "NOTE ENDS AT", Panel::LEFT);
	Item ends;
	ends.key = "p.ends"; ends.kind = Item::PARAM; ends.id = NoteModule::P_ENDS;
	ends.style = "lamps"; ends.x = 9.f; ends.y = 55.f;
	ends.w = 2.f; ends.h = 6.f; ends.pitch = 17.f; ends.horizontal = true;
	ends.names = {"GATE", "HOLD"};
	ends.labelSide = Panel::RIGHT;
	L.items.push_back(ends);

	// THE KNOB ABOVE, ITS CABLE BELOW. Each of these three is one setting: the knob is what the
	// note carries, and a cable in the jack under it takes over. Stacked because that is the
	// relationship; side by side would have made them six things.
	label("h.held", 9.f, 62.f, "HELD AT THE GATE", Panel::LEFT, true);
	knob("p.level", COL[0], 74.f, NoteModule::P_LEVEL, "knob.large");
	label("p.level.label", COL[0], 82.5f, "LEVEL", Panel::CENTRE, true, 0.f, "p.level");
	inJack("in.level", COL[0], 92.f, NoteModule::I_LEVEL, "level", SIG_CV);

	knob("p.dur", COL[1], 74.f, NoteModule::P_DURATION, "knob.large");
	label("p.dur.label", COL[1], 82.5f, "DURATION", Panel::CENTRE, true, 0.f, "p.dur");
	inJack("in.dur", COL[1], 92.f, NoteModule::I_DURATION, "duration", SIG_CV);

	knob("p.pan", COL[2], 74.f, NoteModule::P_PAN, "knob.large");
	label("p.pan.label", COL[2], 82.5f, "PAN", Panel::CENTRE, true, 0.f, "p.pan");
	inJack("in.pan", COL[2], 92.f, NoteModule::I_PAN, "pan", SIG_CV);

	// The two that keep moving have no knob, because there is nothing sensible for a still
	// control to say: an unpatched one sends nothing at all rather than sending zero.
	label("h.moving", 9.f, 106.f, "WHILE IT SOUNDS", Panel::LEFT, true);
	inJack("in.press", COL[0], 116.f, NoteModule::I_PRESSURE, "pressure", SIG_CV);
	inJack("in.timb", COL[1], 116.f, NoteModule::I_TIMBRE, "timbre", SIG_CV);
	knob("p.bend", COL[1], 74.f, NoteModule::P_BEND_RANGE, "knob");
	label("p.bend.label", COL[1], 62.f, "BEND RANGE", Panel::CENTRE, false, 0.f, "p.bend");

	// ONE CABLE OUT. A polyphonic cable in Rack carries one instrument's voices, so a module
	// looking at one has one instrument to hand on. Four instruments is four of these, which
	// costs little: they are adapters, and they leave the patch entirely once a source speaks
	// MPX for itself.
	label("h.out", COL[2], 106.f, "OUT", Panel::CENTRE, true);
	Item out;
	out.key = "out.voice"; out.kind = Item::PORT_OUT; out.id = NoteModule::O_VOICE;
	out.x = COL[2]; out.y = 116.f; out.ring = NOTE_CABLE;
	L.items.push_back(out);
	label("out.voice.label", COL[2], 123.5f, "voice", Panel::CENTRE, false, 0.f, "out.voice");
	Item lamp;
	lamp.key = "lamp.active"; lamp.kind = Item::LIGHT; lamp.id = NoteModule::L_ACTIVE;
	lamp.x = COL[2] + 9.f; lamp.y = 111.f; lamp.owner = "out.voice";
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

	/** Voice cables are drawn violet, so the domain shows in a patch without anybody having to
	remember what was plugged in where. */
	void step() override {
		ModuleWidget::step();
		if (!module)
			return;
		PortWidget* port = getOutput(NoteModule::O_VOICE);
		if (!port)
			return;
		for (CableWidget* cw : APP->scene->rack->getCablesOnPort(port))
			cw->color = NOTE_CABLE;
	}
};


} // namespace px


Model* modelToMPX = createModel<px::NoteModule, px::NoteWidget>("toMPX");
