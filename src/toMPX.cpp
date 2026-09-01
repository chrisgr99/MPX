#include "plugin.hpp"
#include "NoteBus.hpp"

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
		P_NOTES,
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
		O_VOICE1,
		NUM_OUTPUTS = O_VOICE1 + 4
	};
	enum LightId {
		L_VOICE1,
		NUM_LIGHTS = L_VOICE1 + 4
	};

	/** Four voice cables out. The number is what a panel holds, not a limit in the transport:
	each cable is polyphonic in its own right, so four cables is four instruments rather than
	four notes. */
	static const int VOICES = 4;

	/** One of these per channel of the gate input, so a polyphonic source makes polyphonic
	notes on ONE note cable. This is the whole reason the transport carries events: sixteen
	channels of voltage would have been one note. */
	struct Channel {
		bool on = false;
		bool gateHigh = false;
		int64_t handle = 0;
		/** Which cable this note went out on, remembered rather than recomputed, so turning
		the knob while a note sounds cannot send its end to a different cable. */
		int voice = 0;
		float heldPitch = 0.f;
		int age = 0;
		int len = 0;
		float bendSent = 0.f;
		float pressureSent = 0.f;
		float timbreSent = 0.f;
		bool everSent = false;
	};
	Channel channels[16];

	int slots[VOICES];
	uint32_t generations[VOICES];
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
		// HOW THE CHANNELS DIVIDE. Sixteen channels across four cables: at four notes each,
		// channels one to four are the first voice, five to eight the second, and so on. At
		// one note each it is four monophonic instruments; at eight, two polyphonic ones with
		// the last two cables silent.
		configParam(P_NOTES, 1.f, 16.f, 4.f, "Notes per voice");
		paramQuantities[P_NOTES]->snapEnabled = true;

		configInput(I_GATE, "Gate");
		configInput(I_PITCH, "1V/oct");
		configInput(I_LEVEL, "Level");
		configInput(I_DURATION, "Duration");
		configInput(I_PAN, "Pan");
		configInput(I_PRESSURE, "Pressure");
		configInput(I_TIMBRE, "Timbre");
		for (int v = 0; v < VOICES; v++)
			configOutput(O_VOICE1 + v, string::f("Voice %d", v + 1));

		for (int v = 0; v < VOICES; v++)
			slots[v] = busClaim(&generations[v]);
	}

	~NoteModule() {
		for (int v = 0; v < VOICES; v++)
			busRelease(slots[v]);
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
		for (int v = 0; v < VOICES; v++) {
			outputs[O_VOICE1 + v].setChannels(1);
			outputs[O_VOICE1 + v].setVoltage(0.f);
		}


		// The gate decides how many notes this source can sound at once. A monophonic gate
		// makes one; a sixteen-channel one from a poly sequencer or MIDI-CV makes sixteen, all
		// on the one cable.
		int count = std::max(1, inputs[I_GATE].getChannels());

		bool update = (updatePhase == 0);
		updatePhase = (updatePhase + 1) & (UPDATE_EVERY - 1);

		const float bendRange = params[P_BEND_RANGE].getValue();
		const bool holds = params[P_ENDS].getValue() > 0.5f;
		const int perVoice = clamp((int) std::round(params[P_NOTES].getValue()), 1, 16);
		bool anyOn[VOICES] = {};

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
				// The channel's place in the division decides which cable it goes out on. A
				// channel past the fourth voice has no cable and is dropped, which is what
				// makes a high notes-per-voice setting silence the later outputs rather than
				// fold them back onto the first.
				ch.voice = c / perVoice;
				if (ch.voice >= VOICES) {
					ch.on = false;
					continue;
				}
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
				busPush(slots[ch.voice], e);

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

			if (ch.on && ch.voice < VOICES)
				anyOn[ch.voice] = true;
		}

		for (int v = 0; v < VOICES; v++)
			lights[L_VOICE1 + v].setBrightnessSmooth(anyOn[v] ? 1.f : 0.f, args.sampleTime);
	}

	void sendOff(const Channel& ch) {
		if (ch.voice < 0 || ch.voice >= VOICES)
			return;
		Event e;
		e.kind = Event::OFF;
		e.handle = ch.handle;
		busPush(slots[ch.voice], e);
	}

	void sendUpdate(const Channel& ch, Lane lane, float value) {
		if (ch.voice < 0 || ch.voice >= VOICES)
			return;
		Event e;
		e.kind = Event::UPDATE;
		e.lane = (uint8_t) lane;
		e.handle = ch.handle;
		e.value = value;
		busPush(slots[ch.voice], e);
	}
};


int noteBusOf(engine::Module* module, int outputId, uint32_t* generation) {
	NoteModule* note = dynamic_cast<NoteModule*>(module);
	if (!note)
		return -1;
	const int v = outputId - NoteModule::O_VOICE1;
	if (v < 0 || v >= NoteModule::VOICES)
		return -1;
	if (generation)
		*generation = note->generations[v];
	return note->slots[v];
}


// ---- panel -------------------------------------------------------------------------------

static const float COL[4] = {12.7f, 38.1f, 63.5f, 88.9f};   // millimetres

struct NoteWidget : ModuleWidget {
	NoteWidget(NoteModule* module) {
		setModule(module);
		box.size = Vec(20 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

		Panel* panel = new Panel;
		panel->box.size = box.size;
		panel->titleAbove = "DREAMER DEVELOPMENT";
		panel->title = "toMPX";
		panel->rules = {mm2px(Vec(0, 38.f)).y, mm2px(Vec(0, 89.f)).y, mm2px(Vec(0, 112.f)).y};
		const float centre = box.size.x / mm2px(Vec(1.f, 0)).x / 2.f;
		auto label = [&](float x, float y, const char* text, bool heading = false) {
			Panel::Label l;
			l.x = mm2px(Vec(x, 0)).x;
			l.y = mm2px(Vec(0, y)).y;
			l.text = text;
			l.heading = heading;
			panel->labels.push_back(l);
		};
		label(COL[0], 18.f, "GATE");
		label(COL[1], 18.f, "1V/OCT");
		label(COL[2], 18.f, "PRESSURE");
		label(COL[3], 18.f, "TIMBRE");
		label(centre, 43.f, "HELD AT THE GATE\u2019S EDGE", true);
		label(COL[0], 51.f, "LEVEL");
		label(COL[1], 51.f, "DURATION");
		label(COL[2], 51.f, "PAN");
		label(COL[3], 51.f, "BEND RANGE");
		label(COL[0], 94.f, "NOTES PER VOICE");
		label(COL[1], 94.f, "NOTE ENDS");
		label(centre, 116.f, "VOICES", true);
		addChild(panel);

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[0], 26.f)), module, NoteModule::I_GATE));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[1], 26.f)), module, NoteModule::I_PITCH));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[2], 26.f)), module, NoteModule::I_PRESSURE));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[3], 26.f)), module, NoteModule::I_TIMBRE));

		// The cable above, its knob below: the pairing is the point, since one overrides the
		// other. Bend range has no cable, so it is the knob alone.
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[0], 59.f)), module, NoteModule::I_LEVEL));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[1], 59.f)), module, NoteModule::I_DURATION));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COL[2], 59.f)), module, NoteModule::I_PAN));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COL[0], 76.f)), module, NoteModule::P_LEVEL));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COL[1], 76.f)), module, NoteModule::P_DURATION));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COL[2], 76.f)), module, NoteModule::P_PAN));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COL[3], 76.f)), module, NoteModule::P_BEND_RANGE));

		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COL[0], 102.f)), module, NoteModule::P_NOTES));
		addParam(createParamCentered<CKSS>(mm2px(Vec(COL[1], 102.f)), module, NoteModule::P_ENDS));

		for (int v = 0; v < NoteModule::VOICES; v++) {
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COL[v], 123.f)), module,
				NoteModule::O_VOICE1 + v));
			addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(COL[v] + 8.f, 118.5f)),
				module, NoteModule::L_VOICE1 + v));
		}
	}

	/** Voice cables are drawn violet, so the domain shows in a patch without anybody having to
	remember what was plugged in where. */
	void step() override {
		ModuleWidget::step();
		if (!module)
			return;
		for (int v = 0; v < NoteModule::VOICES; v++) {
			PortWidget* port = getOutput(NoteModule::O_VOICE1 + v);
			if (!port)
				continue;
			for (CableWidget* cw : APP->scene->rack->getCablesOnPort(port))
				cw->color = NOTE_CABLE;
		}
	}
};


} // namespace px


Model* modelToMPX = createModel<px::NoteModule, px::NoteWidget>("toMPX");
