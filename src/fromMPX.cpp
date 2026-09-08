#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

namespace px {


/** The interval the source sends updates on. Ramping over exactly that removes the steps
between them completely, and unlike a filter it attenuates nothing and lags nothing beyond the
one interval. A low-pass is the tempting answer and the wrong one: it cannot tell a step it
should remove from a transient it should keep, and tonguing a note is a five millisecond dip in
pressure that a filter slow enough to smooth the steps is fast enough to blunt. */
static const int UPDATE_RAMP = 64;
/** A voice taken while it is still sounding has to retrigger, and a gate that never falls is
not an edge. Without this the note is replaced under a gate that stays up, nothing downstream
strikes again, and what you hear is the first note decaying while its successors pass silently
through. */
static const float RETRIG_MS = 1.f;
/** Below this, in volts, an envelope coming back counts as finished. Half a percent of a
ten-volt envelope — low enough that a long tail is respected, high enough that the last
thousandth of an exponential release does not hold a voice for ever. */
static const float SOUNDING_FLOOR = 0.05f;
/** THE LEVEL NEVER STEPS, at any note, in any mode, and neither does the pan. In glide and
legato one voice sounds continuously while the note changes under it, so a level that jumps from
one note's velocity to the next is a jump in a signal you are listening to. On a fresh voice it
is harmless into an envelope that opens from zero and not harmless into a patch that goes
straight to an amplifier. A millisecond and a third of ramp costs nothing musically.

Pan travels for the same reason: a pattern alternating hard left and hard right is a normal
thing to write, and a jump from one side to the other is a discontinuity in both channels. */
static const float RISE_MS = 10.f;
/** NEVER LONGER THAN A QUARTER OF THE NOTE. Ten milliseconds is the right rise for a note that
lasts and a catastrophe for one that does not: a five-millisecond note would spend its whole
life climbing and never reach the velocity it was played at, so a fast pattern would come out
flat. Short notes take a short rise, which is also when it matters least. */
static const int RISE_MIN = 8;

enum Rollover {
	R_OLDEST,
	R_QUIETEST,
	R_IGNORE,
	R_GLIDE,
	R_LEGATO,
	NUM_ROLLOVER,
};


/** A value on its way to another one. */
struct Ramp {
	float value = 0.f, target = 0.f, step = 0.f;
	int left = 0;

	void set(float v) {
		value = target = v;
		step = 0.f;
		left = 0;
	}
	void to(float t, int samples) {
		target = t;
		if (samples <= 0) {
			value = t;
			step = 0.f;
			left = 0;
			return;
		}
		step = (t - value) / samples;
		left = samples;
	}
	float tick() {
		if (left > 0) {
			value += step;
			// Snapped at the end rather than left wherever the additions landed.
			if (--left == 0)
				value = target;
		}
		return value;
	}
};


struct VoiceModule : Module, NoteSink {
	enum ParamId {
		P_POLY,
		P_ROLLOVER,
		P_GLIDE,
		NUM_PARAMS
	};
	enum InputId {
		I_NOTE,
		/** THE ENVELOPE COMING BACK, so that a voice is not handed to a new note while the last
		one is still sounding.

		A voice whose note has ended is not silent: its gate has fallen and whatever envelope is
		downstream is in its release. Reusing it cuts that release off and moves it to a new
		pitch part way through, and no envelope, however clever, can fix a pitch that has already
		moved — the fact that matters lives in the allocator and the allocator cannot see it.

		Patching the envelope back in is the whole fix. The channels line up by construction,
		since the gate that drove that envelope came from here; it works with any envelope at all,
		because it watches the actual signal rather than modelling one; and with nothing patched
		the module behaves as it did. */
		I_SOUNDING,
		NUM_INPUTS
	};
	enum OutputId {
		O_GATE,
		O_PITCH,
		O_LEVEL,
		O_BEND,
		O_PRESSURE,
		O_TIMBRE,
		O_PAN,
		NUM_OUTPUTS
	};
	enum LightId {
		NUM_LIGHTS
	};

	struct Slot {
		bool active = false;
		int64_t handle = 0;
		/** Pitch is a ramp only so that GLIDE has something to travel along. Every other note
		sets it outright. */
		Ramp pitch;
		Ramp bend, pressure, timbre;
		/** A RAMP, NOT A NUMBER, because of legato. The crossfade between two voices lives on
		this lane and nowhere else: DreamRack's does the same, so that an external amplifier
		reproduces exactly what its internal one did. Every other note sets it outright. */
		Ramp level;
		/** Samples of crossfade left on a voice being handed over. Its gate stays up until the
		fade is done, so the envelope holds while the level takes the sound away — two fades
		multiplied together is not a crossfade, it is a dip. */
		int fading = 0;
		Ramp pan;
		float duration = 0.f;
		float bendRange = 2.f;
		/** Held after the note ends: a voice in its release still reads the note it is
		releasing, which is what a downstream envelope and filter need. */
		int64_t started = 0;
		/** WHEN THIS VOICE FELL SILENT, on the same counter as `started`.

		A voice that has just ended is still sounding: its gate has fallen and whatever envelope
		is downstream is in its release. Handing the next note to it cuts that release off and
		moves it to a new pitch part way through, which is the one thing a polyphonic patch must
		not do — and taking the first free slot each time did exactly that, since the slot that
		just ended is the first one found.

		So a free voice is chosen by how long it has been free. Nothing here can know how long
		the release downstream actually is, and it does not need to: the voice that has been
		silent longest is the one whose release is furthest along, whatever its length. */
		int64_t ended = 0;
		int remaining = 0;
		int gateLow = 0;
	};
	Slot slots[16];

	BusReader reader;
	/** Set on the main thread when the patching changes, acted on by the audio thread, so the
	reader is only ever touched by the thread that reads it. */
	std::atomic<int> wantSlot{-1};
	std::atomic<uint32_t> wantGeneration{0};
	std::atomic<bool> relink{false};
	int64_t ordinal = 1;

	VoiceModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_POLY, 1.f, 16.f, 4.f, "Voices");
		paramQuantities[P_POLY]->snapEnabled = true;
		configSwitch(P_ROLLOVER, 0.f, NUM_ROLLOVER - 1, 0.f, "When no voice is free",
			{"Take the oldest", "Take the quietest", "Ignore the note",
			 "Glide — one voice", "Legato — two voices"});
		configParam(P_GLIDE, 0.f, 2.f, 0.06f, "Glide time", " s");

		configInput(I_NOTE, "MPX note in \u2014 takes an MPX output only");
		configInput(I_SOUNDING, "Envelope back in, so a releasing voice is not reused");
		configOutput(O_GATE, "Gate");
		configOutput(O_PITCH, "1V/oct, bend included");
		configOutput(O_LEVEL, "Level");
		configOutput(O_BEND, "Bend");
		configOutput(O_PRESSURE, "Pressure");
		configOutput(O_TIMBRE, "Timbre");
		configOutput(O_PAN, "Pan");
	}

	void onReset() override {
		for (Slot& s : slots)
			s = Slot();
	}

	bool isMPXInputId(int inputId) override {
		return inputId == I_NOTE;
	}

	/** Called from the widget once a frame with whatever the cable says. */
	void link(int slot, uint32_t generation) {
		if (slot == wantSlot.load() && generation == wantGeneration.load())
			return;
		wantSlot.store(slot);
		wantGeneration.store(generation);
		relink.store(true);
	}

	int voiceCount() {
		int poly = (int) std::round(params[P_POLY].getValue());
		int rollover = (int) std::round(params[P_ROLLOVER].getValue());
		// GLIDE KEEPS ONE VOICE AND USES ONLY ONE, however high POLY is set. Using one is the
		// point: at two voices every other note would find the second one free, start fresh
		// with no glide at all, and half the line would slide while half jumped. A glide
		// between two independent voices is not a glide.
		if (rollover == R_GLIDE)
			return 1;
		// LEGATO HANDS OVER, BETWEEN TWO VOICES AND NO MORE. Notes alternate between a pair:
		// the one being left releases as the new one strikes. A third voice has nothing to do,
		// because only one note is ever giving way to one other.
		if (rollover == R_LEGATO)
			return std::min(2, poly);
		return clamp(poly, 1, 16);
	}

	void process(const ProcessArgs& args) override {
		if (relink.exchange(false)) {
			int slot = wantSlot.load();
			reader.attach(slot, wantGeneration.load());
			// The cable has gone. Anything sounding is released rather than left held up for
			// ever by a note whose end can no longer arrive.
			if (slot < 0) {
				for (Slot& s : slots)
					s.active = false;
			}
		}

		// NOTHING PATCHED IN, SO NOTHING DRIVEN OUT. One channel at nought volts rather than the
		// polyphony count at nought volts, which is not the same thing at all: a VCO handed four
		// channels runs four oscillators, and four oscillators at one pitch with four phases
		// beat against each other and sound like a hum that moves. Changing POLY then changed
		// the channel count under whatever was listening, which is a click.
		//
		// A module with no cable in it should be silent in every sense, including this one.
		if (!reader.attached()) {
			for (int o = 0; o < NUM_OUTPUTS; o++) {
				outputs[o].setChannels(1);
				outputs[o].setVoltage(0.f);
			}
			return;
		}

		const int voices = voiceCount();
		const int rollover = (int) std::round(params[P_ROLLOVER].getValue());
		const int retrig = std::max(1, (int) (RETRIG_MS * 0.001f * args.sampleRate));

		for (int o = 0; o < NUM_OUTPUTS; o++)
			outputs[o].setChannels(voices);
		// A voice left sounding above a lowered POLY would reappear when POLY was raised again.
		for (int i = voices; i < 16; i++)
			slots[i].active = false;

		Event e;
		while (reader.next(e)) {
			if (e.kind == Event::ON)
				noteOn(e, voices, rollover, retrig, args);
			else if (e.kind == Event::OFF)
				noteOff(e.handle);
			else
				noteUpdate(e);
		}

		for (int i = 0; i < voices; i++) {
			Slot& s = slots[i];
			if (s.gateLow > 0)
				s.gateLow--;
			// The voice being left ends when its fade does, not before.
			if (s.fading > 0 && --s.fading == 0) {
				s.active = false;
				s.ended = ordinal++;
			}
			// A lost note-off cannot leave a voice sounding: the duration came with the
			// note-on, so this end can finish it on its own.
			if (s.active && s.remaining > 0 && --s.remaining == 0) {
				s.active = false;
				s.ended = ordinal++;
			}

			const float pitch = s.pitch.tick();
			const float bend = s.bend.tick();
			const float pressure = s.pressure.tick();
			const float timbre = s.timbre.tick();

			outputs[O_GATE].setVoltage((s.active && s.gateLow == 0) ? 10.f : 0.f, i);
			// PITCH INCLUDES THE BEND, because where the note is now is what an oscillator wants
			// and summing the two outside was a cable everybody had to remember. A guitar note
			// bent up a semitone after it starts is one signal, not a note plus a correction.
			outputs[O_PITCH].setVoltage(pitch + bend, i);
			outputs[O_LEVEL].setVoltage(s.level.tick() * 10.f, i);
			// The control voltage runs to five volts at full deflection, scaled by the bend
			// range the note was sent with, and clamped there as a wheel at its stop is.
			const float full = std::max(1e-4f, s.bendRange / 12.f);
			outputs[O_BEND].setVoltage(clamp(bend / full, -1.f, 1.f) * 5.f, i);
			outputs[O_PRESSURE].setVoltage(pressure * 10.f, i);
			outputs[O_TIMBRE].setVoltage(timbre * 10.f, i);
			outputs[O_PAN].setVoltage(s.pan.tick() * 5.f, i);
		}

	}

	void noteOn(const Event& e, int voices, int rollover, int retrig, const ProcessArgs& args) {
		// GLIDE: one voice, the gate stays up, and the pitch travels to each new note over the
		// glide time. Portamento.
		if (rollover == R_GLIDE) {
			Slot& s = slots[0];
			const bool sounding = s.active;
			adopt(s, e, args.sampleRate);
			if (sounding) {
				const int n = (int) (params[P_GLIDE].getValue() * args.sampleRate);
				s.pitch.to(e.pitch, n);
			}
			else {
				s.pitch.set(e.pitch);
			}
			return;
		}

		// A FREE VOICE THAT IS ACTUALLY SILENT, first choice and by a long way. Where the
		// envelope is patched back in, silent means silent; where it is not, the longest free is
		// the best guess available.
		int take = -1;
		int64_t quietestSince = 0;
		for (int i = 0; i < voices; i++) {
			if (slots[i].active || stillSounding(i))
				continue;
			if (take < 0 || slots[i].ended < quietestSince) {
				quietestSince = slots[i].ended;
				take = i;
			}
		}
		// FAILING THAT, THE QUIETEST OF THE RELEASING ONES rather than the oldest of them.
		//
		// Where the envelope is patched back in, how far a release has got is a number we can
		// read rather than infer, and the quietest is by definition the one least missed — a
		// tail at a twentieth of its level is nearly gone whether it started a moment ago or a
		// long time back. Age is only a stand-in for that, and a poor one under an envelope
		// whose releases differ in length.
		//
		// This is not the ROLLOVER setting. That decides what gives when every voice is still
		// PLAYING, and its "quietest" means the quietest note — the level it was struck at —
		// which is the right measure there and the wrong one here. This is about voices whose
		// notes have already ended, where the only question is which sound is furthest gone.
		if (take < 0) {
			const bool watching = inputs[I_SOUNDING].isConnected();
			float faintest = 0.f;
			for (int i = 0; i < voices; i++) {
				if (slots[i].active)
					continue;
				if (watching) {
					const float now = std::fabs(inputs[I_SOUNDING].getPolyVoltage(i));
					if (take < 0 || now < faintest) {
						faintest = now;
						take = i;
					}
				}
				else if (take < 0 || slots[i].ended < quietestSince) {
					quietestSince = slots[i].ended;
					take = i;
				}
			}
		}

		if (rollover == R_LEGATO) {
			// Alternates between the pair, so the note being left releases exactly as the new
			// one strikes. That is what a wind instrument does: changing the length of a
			// vibrating column does not move the pitch — one resonance dies while the next
			// establishes, which is why a slurred saxophone line sounds nothing like a
			// portamento. At one voice there is no pair, so the notes simply butt.
			int newest = -1;
			int64_t best = -1;
			for (int i = 0; i < voices; i++) {
				if (slots[i].started > best) {
					best = slots[i].started;
					newest = i;
				}
			}
			take = (voices >= 2 && newest >= 0) ? (newest == 0 ? 1 : 0) : 0;
			const int fade = (int) (params[P_GLIDE].getValue() * args.sampleRate);

			// THE CROSSFADE, AND IT IS ON THE LEVEL LANE. The voice being left keeps its gate
			// up and its level travels to nothing over the time; the new one starts silent and
			// travels up to its own level over the same time. Two resonances overlapping, one
			// dying as the next establishes, which is what a slurred wind line actually is.
			//
			// Dropping the old voice's gate instead would let its envelope release — a fade,
			// but the envelope's shape and the envelope's length, not this knob's, and it
			// would multiply with the new note's attack rather than crossing with it.
			for (int i = 0; i < voices; i++) {
				if (i == take || !slots[i].active)
					continue;
				if (fade > 0) {
					slots[i].level.to(0.f, fade);
					slots[i].fading = fade;
				}
				else {
					// No time set: the notes butt, the old one ending exactly as the new one
					// begins. The closest a pair can come to a slur without a crossfade.
					slots[i].active = false;
					slots[i].ended = ordinal++;
				}
			}
			Slot& s = slots[take];
			adopt(s, e, args.sampleRate);
			s.fading = 0;
			if (fade > 0) {
				// FROM SILENCE, over the same span the other one is taking to reach it. The
				// two halves have to be the same length or what you hear is not a crossfade.
				s.level.set(0.f);
				s.level.to(e.level, fade);
			}
			// THE PITCH JUMPS. Each note keeps its own for its whole life, and the crossing is
			// entirely in the amplitude — which is what makes legato a different thing from
			// glide rather than a slower version of it.
			s.pitch.set(e.pitch);
			return;
		}

		if (take < 0) {
			if (rollover == R_IGNORE)
				return;                      // a drum machine that cannot be interrupted
			int64_t best = 0;
			float quietest = 0.f;
			for (int i = 0; i < voices; i++) {
				if (rollover == R_QUIETEST) {
					// The value it is heading for, not the one it is passing through: a note
					// part way through a fade is quiet at this instant and not a quiet note.
					if (take < 0 || slots[i].level.target < quietest) {
						quietest = slots[i].level.target;
						take = i;
					}
				}
				else {
					if (take < 0 || slots[i].started < best) {
						best = slots[i].started;
						take = i;
					}
				}
			}
			if (take < 0)
				take = 0;
			// Taken from under a note that was still sounding, so the gate breaks for a
			// moment: a millisecond, which is enough for any trigger and too short to hear.
			slots[take].gateLow = retrig;
		}

		Slot& s = slots[take];
		adopt(s, e, args.sampleRate);
		s.fading = 0;
		s.pitch.set(e.pitch);
	}

	/** Everything a note carries, taken on by a voice. Pitch is set by the caller, because
	glide is the one case where it travels rather than jumps. */
	void adopt(Slot& s, const Event& e, float sampleRate) {
		s.active = true;
		s.handle = e.handle;
		const int rise = std::max(RISE_MIN, std::min(
			(int) (RISE_MS * 0.001f * sampleRate),
			(int) (e.duration * sampleRate / 4.f)));
		s.level.to(e.level, rise);
		s.pan.to(e.pan, rise);
		s.duration = e.duration;
		s.bendRange = e.bendRange;
		s.started = ordinal++;
		s.remaining = std::max(1, (int) (e.duration * sampleRate));
		// Bend is a deviation, and every note starts with none. Pressure and timbre keep
		// whatever the last note left them at: a source with nothing to say about them never
		// sends any, and a voice should fall back to its own envelope rather than be held shut
		// by a lane reporting silence.
		s.bend.set(0.f);
	}

	/** Whether this voice is still making a sound although its note has ended — which only the
	envelope can say, so it is only known where the envelope is patched back in. */
	bool stillSounding(int i) {
		if (!inputs[I_SOUNDING].isConnected())
			return false;
		return std::fabs(inputs[I_SOUNDING].getPolyVoltage(i)) > SOUNDING_FLOOR;
	}

	void noteOff(int64_t handle) {
		for (Slot& s : slots) {
			if (s.active && s.handle == handle) {
				s.active = false;
				s.ended = ordinal++;
				// Pitch, level and pan are left where they are, so a voice in its release
				// still reads the note it is releasing.
				return;
			}
		}
	}

	void noteUpdate(const Event& e) {
		for (Slot& s : slots) {
			if (!s.active || s.handle != e.handle)
				continue;
			switch (e.lane) {
				case LANE_BEND: s.bend.to(e.value, UPDATE_RAMP); break;
				case LANE_PRESSURE: s.pressure.to(e.value, UPDATE_RAMP); break;
				case LANE_TIMBRE: s.timbre.to(e.value, UPDATE_RAMP); break;
				default: break;
			}
			return;
		}
	}
};


bool isMPXInput(engine::Module* module, int inputId) {
	// Asked of the capability, not of the class, for the same reason noteBusOf is: a module
	// added later should need no special case here.
	NoteSink* sink = dynamic_cast<NoteSink*>(module);
	return sink != NULL && sink->isMPXInputId(inputId);
}


// ---- panel -------------------------------------------------------------------------------
// LAID OUT LIKE DREAMRACK'S VOICE IN, which is the same module: what it does controls down the
// left, what comes out of it in one column down the right with each name right-aligned against
// its own jack. A column of nine jacks reads as a list; nine jacks in a grid reads as a puzzle.
//
// Written here rather than in a file that ships, so the default cannot fall out of step with
// the module. What a person moves is saved over the top of it — see Layout.hpp.

// YOUR ARRANGEMENT, TIDIED. The positions are the ones you set in the editor; what changed is
// that the control column now agrees on one x rather than four within a millimetre of each
// other, the jack column steps by exactly its pitch, and every value is on a half millimetre.

/** EIGHT HP, and every number below is one that was arrived at by moving the thing on the panel
and then written back here. The column of nine lanes and the column of controls sit as close as
their names allow, and a name that would have set the panel wider — the two that read 1V/oct —
is set over two lines instead. */
/** THE CONTROL COLUMN. Not quite one axis: the jack, the readout and the knob are different
widths, and each was placed by eye until it looked centred against the others rather than by
sharing a number with them. */
static const float CTRL_X = 11.5f;       /**< The transition knob; the others carry their own. */
static const float LAMP_X = 4.f;         /**< The rollover track's left edge, names to its right. */
static const float JACK_X = 34.f;        /**< The lanes, one column. */
static const float JACK_LABEL_DX = -5.5f;/**< Their names, ending just short of them. */
static const float JACK_LABEL_SIZE = 7.f;
static const float JACK_TOP = 21.5f;
static const float JACK_PITCH = 11.3f;

static Layout fromMPXLayout() {
	Layout L;
	L.hp = 8.f;
	L.title = "fromMPX";
	L.titleAbove = "DREAMER DEVELOPMENT";

	auto label = [&](const char* key, float x, float y, const char* text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f,
			const char* owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};
	auto outJack = [&](const char* key, float y, int id, const char* name, NVGcolor color) {
		Item i;
		i.key = key; i.kind = Item::PORT_OUT; i.id = id; i.x = JACK_X; i.y = y; i.ring = color;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), JACK_X + JACK_LABEL_DX, y, name,
			Panel::RIGHT, false, JACK_LABEL_SIZE, key);
	};

	// THE ENVELOPE COMING BACK, at the foot of the lane column where the two retired jacks used
	// to be — an input among outputs, which is why it is the only one there and why it sits
	// below the gap rather than continuing the list.
	Item back;
	back.key = "in.sounding"; back.kind = Item::PORT_IN; back.id = VoiceModule::I_SOUNDING;
	back.x = JACK_X; back.y = 111.9f; back.ring = SIG_CV;
	L.items.push_back(back);
	label("in.sounding.label", JACK_X + JACK_LABEL_DX, 111.9f, "env\nback", Panel::RIGHT,
		false, JACK_LABEL_SIZE, "in.sounding");

	// The cable in at the top of the control column, above everything it feeds.
	Item note;
	note.key = "in.voice"; note.kind = Item::PORT_IN; note.id = VoiceModule::I_NOTE;
	note.x = 13.f; note.y = 19.5f; note.ring = NOTE_CABLE;
	L.items.push_back(note);
	label("in.voice.label", 13.f, 28.5f, "mpx\nIN", Panel::CENTRE, true, 12.f, "in.voice");

	// HOW MANY NOTES THIS INSTRUMENT CAN HOLD AT ONCE, as a figure rather than a pointer.
	//
	// It was a knob with sixteen detents ringed by eight of the counts, which took the width of
	// the panel to say what two figures say exactly. A knob is also the wrong control for this:
	// you cannot see what it is set to without a tooltip, and getting from four to twelve means
	// dragging through everything between. A click opens the list of sixteen and the wheel steps
	// it for the times when the next one along is what is wanted.
	Item poly;
	poly.key = "p.poly"; poly.kind = Item::PARAM; poly.id = VoiceModule::P_POLY;
	poly.style = "readout"; poly.x = 12.5f; poly.y = 42.5f; poly.chars = 0; poly.h = 8.f;
	L.items.push_back(poly);
	label("p.poly.label", 12.5f, 50.2f, "POLYPHONY", Panel::CENTRE, true, 0.f, "p.poly");

	// What gives when a note arrives and nothing is free. Five names, because a knob with five
	// detents says nothing about what the five are.
	Item roll;
	roll.key = "p.rollover"; roll.kind = Item::PARAM; roll.id = VoiceModule::P_ROLLOVER;
	roll.style = "lamps"; roll.x = LAMP_X; roll.y = 57.8f; roll.nameSize = 7.f;
	roll.w = 6.f; roll.h = 34.f; roll.pitch = 6.5f;
	roll.names = {"OLDEST", "QUIETEST", "IGNORE\nNEWEST", "GLIDE", "LEGATO"};
	roll.labelSide = Panel::RIGHT;
	L.items.push_back(roll);

	// GLIDE AND LEGATO ARE THE TWO THAT USE A TIME, and the bracket says so — reaching from
	// them down to the knob, which can then be called what it is rather than being made to
	// list its owners.
	Item brace;
	brace.key = "brace.time"; brace.kind = Item::BRACKET;
	// Measured, not guessed, and measured again each time the lamps or the knob move. With the
	// column starting at 57.8 its lamps fall at 60, 66.5, 73, 79.5 and 86; the two it belongs to
	// are GLIDE and LEGATO, so it has to start ABOVE the GLIDE lamp rather than between the two
	// of them. IGNORE NEWEST is two lines and its lower one sits at 74.1; GLIDE's word begins at
	// 78.6; halfway between is 76.4. The bottom passes 2.5 below the knob's centre at 96.3.
	// SHORT ARMS, AND CLOSE IN. The bracket is the leftmost thing on the panel and its arms only
	// have to read as reaching the lamps beside them; every millimetre they were given was a
	// millimetre of panel width, and shortening them is most of what took this from ten HP to
	// nine.
	brace.x = 2.2f; brace.y = 76.4f; brace.w = 1.6f; brace.h = 22.4f;
	L.items.push_back(brace);

	Item glide;
	glide.key = "p.glide"; glide.kind = Item::PARAM; glide.id = VoiceModule::P_GLIDE;
	glide.style = "knob"; glide.x = CTRL_X; glide.y = 96.3f;
	L.items.push_back(glide);
	label("p.glide.label", CTRL_X, 105.f, "TRANSITION\nTIME", Panel::CENTRE, true, 7.f,
		"p.glide");

	// THE SEVEN LANES, in the order a voice is built: what starts it, what pitches it, how far it
	// has moved, how hard it was struck, then what changes while it sounds.
	//
	// TWO ARE GONE. Duration went because the gate already says the whole of it — the gate falls
	// at whichever came first, the duration expiring or the note ending, so an ordinary envelope
	// keyed on it releases at the right moment with nothing else patched. Bend 1V/oct went
	// because pitch now carries the bend, which is what an oscillator wants; the Bend output
	// stays, because a scaled deflection is what an effect wants and a pitch in volts per octave
	// is far too small a number to modulate anything with.
	float y = JACK_TOP;
	outJack("out.gate", y, VoiceModule::O_GATE, "gate", SIG_GATE);           y += JACK_PITCH;
	outJack("out.pitch", y, VoiceModule::O_PITCH, "Pitch\nV/oct", SIG_PITCH); y += JACK_PITCH;
	outJack("out.bend", y, VoiceModule::O_BEND, "bend", SIG_CV);             y += JACK_PITCH;
	outJack("out.level", y, VoiceModule::O_LEVEL, "level", SIG_CV);          y += JACK_PITCH;
	outJack("out.pan", y, VoiceModule::O_PAN, "pan", SIG_CV);                y += JACK_PITCH;
	outJack("out.press", y, VoiceModule::O_PRESSURE, "pressure", SIG_CV);    y += JACK_PITCH;
	outJack("out.timb", y, VoiceModule::O_TIMBRE, "timbre", SIG_CV);
	L.bindOffsets();
	return L;
}

struct VoiceWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	VoiceWidget(VoiceModule* module) {
		setModule(module);
		layout = fromMPXLayout();
		layoutApplyUser("fromMPX", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "fromMPX");
	}

	/** WHERE THE CABLE BECOMES A LINK. Rack owns the cable; this reads it. A cable whose other
	end is a toMPX registers that source with this module, and pulling the cable unregisters it
	on the next frame, because the scan is the only thing that establishes it. Patching happens
	at human speed, so once a frame is far faster than it needs to be. */
	void step() override {
		ModuleWidget::step();
		VoiceModule* voice = dynamic_cast<VoiceModule*>(module);
		if (!voice)
			return;
		PortWidget* port = getInput(VoiceModule::I_NOTE);
		int slot = -1;
		uint32_t generation = 0;
		if (port) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				engine::Cable* cable = cw->getCable();
				if (!cable)
					continue;
				slot = noteBusOf(cable->outputModule, cable->outputId, &generation);
				if (slot >= 0) {
					cw->color = NOTE_CABLE;
					break;
				}
			}
		}
		voice->link(slot, generation);
	}
};


} // namespace px


Model* modelFromMPX = createModel<px::VoiceModule, px::VoiceWidget>("fromMPX");
