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


struct VoiceModule : Module {
	enum ParamId {
		P_POLY,
		P_ROLLOVER,
		P_GLIDE,
		NUM_PARAMS
	};
	enum InputId {
		I_NOTE,
		NUM_INPUTS
	};
	enum OutputId {
		O_GATE,
		O_PITCH,
		O_LEVEL,
		O_BEND,
		O_BENDV,
		O_PRESSURE,
		O_TIMBRE,
		O_PAN,
		O_DURATION,
		NUM_OUTPUTS
	};
	enum LightId {
		L_LINKED,
		NUM_LIGHTS
	};

	struct Slot {
		bool active = false;
		int64_t handle = 0;
		/** Pitch is a ramp only so that GLIDE has something to travel along. Every other note
		sets it outright. */
		Ramp pitch;
		Ramp bend, pressure, timbre;
		float level = 0.f;
		float pan = 0.f;
		float duration = 0.f;
		float bendRange = 2.f;
		/** Held after the note ends: a voice in its release still reads the note it is
		releasing, which is what a downstream envelope and filter need. */
		int64_t started = 0;
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

		configInput(I_NOTE, "Note");
		configOutput(O_GATE, "Gate");
		configOutput(O_PITCH, "1V/oct");
		configOutput(O_LEVEL, "Level");
		configOutput(O_BEND, "Bend");
		configOutput(O_BENDV, "Bend 1V/oct");
		configOutput(O_PRESSURE, "Pressure");
		configOutput(O_TIMBRE, "Timbre");
		configOutput(O_PAN, "Pan");
		configOutput(O_DURATION, "Duration");
	}

	void onReset() override {
		for (Slot& s : slots)
			s = Slot();
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
			// A lost note-off cannot leave a voice sounding: the duration came with the
			// note-on, so this end can finish it on its own.
			if (s.active && s.remaining > 0 && --s.remaining == 0)
				s.active = false;

			const float pitch = s.pitch.tick();
			const float bend = s.bend.tick();
			const float pressure = s.pressure.tick();
			const float timbre = s.timbre.tick();

			outputs[O_GATE].setVoltage((s.active && s.gateLow == 0) ? 10.f : 0.f, i);
			outputs[O_PITCH].setVoltage(pitch, i);
			outputs[O_LEVEL].setVoltage(s.level * 10.f, i);
			// The control voltage runs to five volts at full deflection, scaled by the bend
			// range the note was sent with, and clamped there as a wheel at its stop is.
			const float full = std::max(1e-4f, s.bendRange / 12.f);
			outputs[O_BEND].setVoltage(clamp(bend / full, -1.f, 1.f) * 5.f, i);
			// And this one carries the pitch's real movement, unscaled and unclamped, so that
			// held pitch plus it is exactly where the source has gone. The range knob does not
			// touch it: a range describes a control signal, not a pitch.
			outputs[O_BENDV].setVoltage(bend, i);
			outputs[O_PRESSURE].setVoltage(pressure * 10.f, i);
			outputs[O_TIMBRE].setVoltage(timbre * 10.f, i);
			outputs[O_PAN].setVoltage(s.pan * 5.f, i);
			// One volt is one second, which is how the source's duration cable reads too.
			outputs[O_DURATION].setVoltage(clamp(s.duration, 0.f, 10.f), i);
		}

		lights[L_LINKED].setBrightness(reader.attached() ? 1.f : 0.f);
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

		int take = -1;
		for (int i = 0; i < voices; i++) {
			if (!slots[i].active) {
				take = i;
				break;
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
			// The one being left goes quiet; no retrigger gap, because nothing is being stolen
			// out from under itself.
			for (int i = 0; i < voices; i++) {
				if (i != take)
					slots[i].active = false;
			}
			Slot& s = slots[take];
			adopt(s, e, args.sampleRate);
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
					if (take < 0 || slots[i].level < quietest) {
						quietest = slots[i].level;
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
		s.pitch.set(e.pitch);
	}

	/** Everything a note carries, taken on by a voice. Pitch is set by the caller, because
	glide is the one case where it travels rather than jumps. */
	void adopt(Slot& s, const Event& e, float sampleRate) {
		s.active = true;
		s.handle = e.handle;
		s.level = e.level;
		s.pan = e.pan;
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

	void noteOff(int64_t handle) {
		for (Slot& s : slots) {
			if (s.active && s.handle == handle) {
				s.active = false;
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


// ---- panel -------------------------------------------------------------------------------
// LAID OUT LIKE DREAMRACK'S VOICE IN, which is the same module: what it does controls down the
// left, what comes out of it in one column down the right with each name right-aligned against
// its own jack. A column of nine jacks reads as a list; nine jacks in a grid reads as a puzzle.
//
// Written here rather than in a file that ships, so the default cannot fall out of step with
// the module. What a person moves is saved over the top of it — see Layout.hpp.

static const float CTRL_X = 18.f;
static const float JACK_X = 55.f;
static const float JACK_LABEL_X = 47.5f;
static const float JACK_TOP = 38.f;
static const float JACK_PITCH = 10.4f;

static Layout fromMPXLayout() {
	Layout L;
	L.hp = 14.f;
	L.title = "fromMPX";
	L.titleAbove = "DREAMER DEVELOPMENT";

	auto label = [&](const char* key, float x, float y, const char* text,
			Panel::Align align = Panel::CENTRE, bool heading = false, float size = 0.f) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = heading; i.size = size;
		L.items.push_back(i);
	};
	auto outJack = [&](const char* key, float y, int id, const char* name, NVGcolor color) {
		Item i;
		i.key = key; i.kind = Item::PORT_OUT; i.id = id; i.x = JACK_X; i.y = y; i.ring = color;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), JACK_LABEL_X, y, name, Panel::RIGHT);
	};

	// The cable comes in at the top of the control column, above everything it feeds.
	Item note;
	note.key = "in.voice"; note.kind = Item::PORT_IN; note.id = VoiceModule::I_NOTE;
	note.x = CTRL_X; note.y = 38.f; note.ring = NOTE_CABLE;
	L.items.push_back(note);
	label("in.voice.label", CTRL_X, 45.5f, "voice");
	Item lamp;
	lamp.key = "lamp.linked"; lamp.kind = Item::LIGHT; lamp.id = VoiceModule::L_LINKED;
	lamp.x = CTRL_X + 8.5f; lamp.y = 34.f;
	L.items.push_back(lamp);

	// How many notes this instrument can hold at once, with the count printed round the knob
	// so the setting can be read without a tooltip.
	Item poly;
	poly.key = "p.poly"; poly.kind = Item::PARAM; poly.id = VoiceModule::P_POLY;
	poly.style = "knob.huge"; poly.x = CTRL_X; poly.y = 62.f;
	L.items.push_back(poly);
	label("p.poly.label", CTRL_X, 77.5f, "VOICES", Panel::CENTRE, true);
	for (int i = 1; i <= 8; i++) {
		// Eight of the sixteen are marked; marking all sixteen would be a ring of numbers too
		// small to read and too close together to tell apart.
		const float a = (-0.75f + (i - 1) / 7.f * 1.5f) * (float) M_PI;
		label(("p.poly.n" + std::to_string(i)).c_str(),
			CTRL_X + std::sin(a) * 13.f, 62.f - std::cos(a) * 13.f,
			std::to_string(i * 2).c_str(), Panel::CENTRE, false, 7.f);
	}

	// What gives when a note arrives and nothing is free. Five names, because a knob with five
	// detents says nothing about what the five are.
	Item roll;
	roll.key = "p.rollover"; roll.kind = Item::PARAM; roll.id = VoiceModule::P_ROLLOVER;
	roll.style = "lamps"; roll.x = CTRL_X + 3.f; roll.y = 84.f;
	roll.w = 6.f; roll.h = 32.f; roll.pitch = 7.2f;
	roll.names = {"OLDEST", "QUIETEST", "IGNORE", "GLIDE", "LEGATO"};
	roll.labelSide = Panel::LEFT;
	L.items.push_back(roll);

	Item glide;
	glide.key = "p.glide"; glide.kind = Item::PARAM; glide.id = VoiceModule::P_GLIDE;
	glide.x = CTRL_X - 5.f; glide.y = 121.f;
	L.items.push_back(glide);
	label("p.glide.label", CTRL_X + 2.f, 121.f, "GLIDE", Panel::LEFT, true);

	// The nine lanes, in the order a voice is built: what starts it, what pitches it, how hard
	// it was struck, then everything that moves while it sounds.
	float y = JACK_TOP;
	outJack("out.gate", y, VoiceModule::O_GATE, "gate", SIG_GATE);        y += JACK_PITCH;
	outJack("out.pitch", y, VoiceModule::O_PITCH, "v/oct", SIG_PITCH);    y += JACK_PITCH;
	outJack("out.level", y, VoiceModule::O_LEVEL, "level", SIG_CV);       y += JACK_PITCH;
	outJack("out.bend", y, VoiceModule::O_BEND, "bend", SIG_CV);          y += JACK_PITCH;
	outJack("out.bendv", y, VoiceModule::O_BENDV, "bend v", SIG_PITCH);   y += JACK_PITCH;
	outJack("out.press", y, VoiceModule::O_PRESSURE, "press", SIG_CV);    y += JACK_PITCH;
	outJack("out.timb", y, VoiceModule::O_TIMBRE, "timb", SIG_CV);        y += JACK_PITCH;
	outJack("out.pan", y, VoiceModule::O_PAN, "pan", SIG_CV);             y += JACK_PITCH;
	outJack("out.dur", y, VoiceModule::O_DURATION, "dur", SIG_CV);
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
