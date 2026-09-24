/** mpxDrums — a kit, played from an MPX cable.

WHY NOT GATES. A drum patched with a gate per part says when and nothing else, so every stroke is
the same stroke and a pattern comes out as a machine playing it. A note on an MPX cable carries how
hard it was struck and how long it is meant to ring, which is what separates a drummer from a drum
machine: the ghost notes under a backbeat, the hat opening and closing, the ride's shoulder against
its tip.

WHICH DRUM A NOTE IS comes from its pitch, on the General MIDI drum map — kick 36, snare 38, hats
42 and 46, toms, ride 51, crash 49 — so a part written anywhere, by mpxGroove or by anything that
speaks MIDI, arrives here meaning the same thing.

SYNTHESISED RATHER THAN SAMPLED, for now. Each part is made from a sine, some noise and an
envelope, which is what the classic machines are; it needs no download and no samples to host, and
it answers velocity properly, which is the thing being judged while a groove is written. A sampled
kit is the same module with a different sound source behind the same map.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

#include <cmath>

namespace px {


/** The parts this kit has, and the MIDI note each answers to. */
enum KitPart {
	K_KICK, K_SNARE, K_STICK, K_CLAP, K_HAT, K_OPEN, K_TOM_LO, K_TOM_HI, K_RIDE, K_CRASH,
	K_PARTS
};

/** THE MAP, as MIDI note numbers. Anything not in it is played as the nearest part below, so a
stray note is a sound rather than a silence. */
static const int KIT_NOTE[K_PARTS] = {36, 38, 37, 39, 42, 46, 45, 50, 51, 49};

static const char* KIT_NAMES[K_PARTS] = {"Kick", "Snare", "Stick", "Clap", "Hat", "Open hat",
	"Low tom", "High tom", "Ride", "Crash"};


/** ONE STROKE SOUNDING. A drum is a short thing with a shape rather than a note with an envelope,
so each part is its own few lines of arithmetic rather than a voice with settings. */
struct KitVoice {
	bool active = false;
	int part = 0;
	int64_t handle = 0;
	float level = 0.f;
	float age = 0.f;
	/** How long the stroke lasts, which velocity and the note's own duration both have a say in. */
	float span = 0.2f;
	/** The oscillator, for the parts that have one. */
	float phase = 0.f;
	/** A one-pole on the noise, which is what makes a hat bright and a snare dull. */
	float hp = 0.f, lp = 0.f;
	uint32_t noise = 0x9e3779b9u;

	float white() {
		noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
		return (float) (noise >> 8) / 8388608.f - 1.f;
	}
};


struct DrumsModule : Module, NoteSource, NoteSink {
	enum ParamId {
		P_KICK, P_SNARE, P_HAT, P_TOM, P_METAL, P_TONE, P_LEVEL, NUM_PARAMS
	};
	enum InputId { I_MPX, NUM_INPUTS };
	enum OutputId { O_L, O_R, O_MPX, NUM_OUTPUTS };
	enum LightId { L_HIT, NUM_LIGHTS };

	static const int MAX_VOICES = 16;
	KitVoice voices[MAX_VOICES];

	DrumsModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_KICK, 0.f, 1.f, 0.8f, "Kick", "%", 0.f, 100.f);
		configParam(P_SNARE, 0.f, 1.f, 0.8f, "Snare", "%", 0.f, 100.f);
		configParam(P_HAT, 0.f, 1.f, 0.6f, "Hats", "%", 0.f, 100.f);
		configParam(P_TOM, 0.f, 1.f, 0.7f, "Toms", "%", 0.f, 100.f);
		configParam(P_METAL, 0.f, 1.f, 0.6f, "Ride and crash", "%", 0.f, 100.f);
		// BRIGHT OR DARK, across the whole kit: one knob rather than a filter on each part,
		// because what anybody actually wants is the kit further forward or further back.
		configParam(P_TONE, 0.f, 1.f, 0.5f, "Tone", "%", 0.f, 100.f);
		configParam(P_LEVEL, 0.f, 1.f, 0.8f, "Level", "%", 0.f, 100.f);

		configInput(I_MPX, "MPX note");
		configOutput(O_L, "Left");
		configOutput(O_R, "Right");
		// A THRU, so a kit can sit in the middle of a chain rather than only at the end of one:
		// a groove into the drums still has to reach the comp and the piano beyond it.
		configOutput(O_MPX, "MPX note");

		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		slot = busClaim(&generation);
	}

	~DrumsModule() {
		busRelease(slot);
	}

	int slot = -1;
	uint32_t generation = 0;

	int busSlotFor(int outputId, uint32_t* gen) override {
		if (outputId != O_MPX)
			return -1;
		if (gen)
			*gen = generation;
		return slot;
	}

	bool isMPXInputId(int inputId) override {
		return inputId == I_MPX;
	}

	BusReader reader;
	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
	std::atomic<bool> relink{false};
	float hitLight = 0.f;

	void link(const int* slots, const uint32_t* generations, int n) {
		bool same = (n == wantCount.load());
		for (int i = 0; same && i < n; i++) {
			same = (slots[i] == wantSlots[i].load())
				&& (generations[i] == wantGenerations[i].load());
		}
		if (same)
			return;
		for (int i = 0; i < n && i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(slots[i]);
			wantGenerations[i].store(generations[i]);
		}
		wantCount.store(std::min(n, MAX_UPSTREAM));
		relink.store(true);
	}

	/** WHICH PART A PITCH MEANS. The nearest note at or below it, so a map with more parts than
	this kit has still lands on something sensible. */
	static int partFor(float pitch) {
		const int note = (int) std::lround(pitch * 12.f) + 60;
		int best = K_KICK;
		int bestGap = 1000;
		for (int p = 0; p < K_PARTS; p++) {
			const int gap = std::abs(note - KIT_NOTE[p]);
			if (gap < bestGap) {
				bestGap = gap;
				best = p;
			}
		}
		return best;
	}

	void startStroke(int part, float level, float duration) {
		// A CHOKE: a closed hat stops an open one, the way a foot does. Nothing else chokes.
		if (part == K_HAT || part == K_OPEN) {
			for (int i = 0; i < MAX_VOICES; i++) {
				if (voices[i].active && (voices[i].part == K_HAT || voices[i].part == K_OPEN))
					voices[i].span = std::fmin(voices[i].span, voices[i].age + 0.01f);
			}
		}
		int place = -1;
		float oldest = -1.f;
		for (int i = 0; i < MAX_VOICES; i++) {
			if (!voices[i].active) {
				place = i;
				break;
			}
			if (voices[i].age > oldest) {
				oldest = voices[i].age;
				place = i;
			}
		}
		KitVoice& v = voices[place];
		v.active = true;
		v.part = part;
		v.level = math::clamp(level, 0.f, 1.f);
		v.age = 0.f;
		v.phase = 0.f;
		v.hp = v.lp = 0.f;
		v.noise = 0x9e3779b9u + (uint32_t) (place * 2654435761u);
		// HOW LONG IT RINGS. The part decides most of it; an open hat or a ride also takes the
		// note's own length, since that is what holding one means.
		switch (part) {
			case K_KICK:    v.span = 0.35f; break;
			case K_SNARE:   v.span = 0.22f; break;
			case K_STICK:   v.span = 0.06f; break;
			case K_CLAP:    v.span = 0.20f; break;
			case K_HAT:     v.span = 0.05f + 0.05f * v.level; break;
			case K_OPEN:    v.span = std::fmax(0.25f, std::fmin(1.5f, duration)); break;
			case K_TOM_LO:  v.span = 0.35f; break;
			case K_TOM_HI:  v.span = 0.28f; break;
			case K_RIDE:    v.span = std::fmax(0.4f, std::fmin(2.5f, duration * 2.f)); break;
			case K_CRASH:   v.span = std::fmax(1.2f, std::fmin(4.f, duration * 3.f)); break;
			default:        v.span = 0.2f; break;
		}
		hitLight = 1.f;
	}

	/** ONE STROKE'S SAMPLE. The shapes are the classic ones: a sine whose pitch falls for the
	drums, filtered noise for the metal, and the two together for a snare. */
	float voiceSample(KitVoice& v, float sr, float tone) {
		const float t = v.age;
		const float done = t / std::fmax(0.001f, v.span);
		if (done >= 1.f) {
			v.active = false;
			return 0.f;
		}
		// A LEVEL THAT IS A VELOCITY: harder is louder and brighter, which is what a drum does.
		const float hard = 0.35f + 0.65f * v.level;
		float out = 0.f;
		auto env = [&](float shape) { return std::exp(-done * shape); };
		auto sine = [&](float hz) {
			v.phase += hz / sr;
			if (v.phase >= 1.f)
				v.phase -= 1.f;
			return std::sin(2.f * M_PI * v.phase);
		};
		switch (v.part) {
			case K_KICK: {
				// The pitch falls from a click to the body, which is the whole of a kick.
				const float hz = 48.f + 160.f * std::exp(-t * 55.f);
				out = sine(hz) * env(4.5f);
				out += v.white() * std::exp(-t * 400.f) * 0.35f * hard;
				break;
			}
			case K_SNARE: {
				const float body = std::sin(2.f * M_PI * 185.f * t) * 0.5f
					+ std::sin(2.f * M_PI * 278.f * t) * 0.3f;
				float n = v.white();
				v.lp += (n - v.lp) * (0.35f + 0.4f * tone);
				out = (body * env(9.f) * 0.6f + v.lp * env(6.f)) * hard;
				break;
			}
			case K_STICK: {
				out = (std::sin(2.f * M_PI * 800.f * t) + v.white() * 0.6f) * env(45.f) * hard;
				break;
			}
			case K_CLAP: {
				// Four bursts a few milliseconds apart, which is what a clap is.
				const float burst = (t < 0.03f)
					? (std::fmod(t, 0.008f) < 0.004f ? 1.f : 0.25f) : 1.f;
				float n = v.white();
				v.lp += (n - v.lp) * (0.5f + 0.3f * tone);
				out = v.lp * env(10.f) * burst * hard;
				break;
			}
			case K_HAT:
			case K_OPEN: {
				float n = v.white();
				// A one-pole high pass: the noise minus its own low end.
				v.lp += (n - v.lp) * 0.55f;
				v.hp = n - v.lp;
				out = v.hp * env(v.part == K_HAT ? 16.f : 3.2f) * hard * (0.6f + 0.6f * tone);
				break;
			}
			case K_TOM_LO:
			case K_TOM_HI: {
				const float base = (v.part == K_TOM_LO) ? 110.f : 180.f;
				const float hz = base * (1.f + 0.35f * std::exp(-t * 18.f));
				out = sine(hz) * env(5.f) * hard;
				out += v.white() * std::exp(-t * 200.f) * 0.15f;
				break;
			}
			case K_RIDE:
			case K_CRASH: {
				// Several inharmonic partials through a high pass, which is a cymbal's whole
				// character: no fundamental and no decay you can hear the end of.
				float n = 0.f;
				static const float RATIO[6] = {1.f, 1.41f, 1.73f, 2.11f, 2.57f, 3.13f};
				const float base = (v.part == K_RIDE) ? 620.f : 480.f;
				for (int k = 0; k < 6; k++)
					n += std::sin(2.f * M_PI * base * RATIO[k] * t);
				n /= 6.f;
				float w = v.white();
				v.lp += (w - v.lp) * 0.6f;
				w = w - v.lp;
				const float mix = (v.part == K_RIDE) ? 0.55f : 0.8f;
				out = (n * (1.f - mix) + w * mix) * env(v.part == K_RIDE ? 3.2f : 1.6f) * hard;
				// A ride's stick is the part that says where the beat is.
				if (v.part == K_RIDE)
					out += std::sin(2.f * M_PI * 3200.f * t) * std::exp(-t * 90.f) * 0.3f * hard;
				break;
			}
			default:
				break;
		}
		v.age += 1.f / sr;
		return out;
	}

	float partLevel(int part) {
		switch (part) {
			case K_KICK:    return params[P_KICK].getValue();
			case K_SNARE:
			case K_STICK:
			case K_CLAP:    return params[P_SNARE].getValue();
			case K_HAT:
			case K_OPEN:    return params[P_HAT].getValue();
			case K_TOM_LO:
			case K_TOM_HI:  return params[P_TOM].getValue();
			default:        return params[P_METAL].getValue();
		}
	}

	/** Where each part sits across the stereo field, as a kit is heard from behind it. */
	static float partPan(int part) {
		switch (part) {
			case K_HAT:
			case K_OPEN:    return -0.35f;
			case K_TOM_LO:  return 0.3f;
			case K_TOM_HI:  return -0.2f;
			case K_RIDE:    return 0.4f;
			case K_CRASH:   return -0.5f;
			default:        return 0.f;
		}
	}

	void process(const ProcessArgs& args) override {
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}

		// EVERYTHING PASSES THROUGH on the way past: the events, the harmony, the voicing and the
		// pedals belong to the chain rather than to this module.
		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(0.f);
		Harmony h;
		if (reader.harmony(h))
			busPublishHarmony(slot, h);
		ChordVoicing cv;
		if (reader.voicing(cv))
			busPublishVoicing(slot, cv);
		{
			float sustain = 0.f, soft = 0.f;
			reader.pedals(sustain, soft);
			busPublishPedals(slot, sustain, soft);
		}

		Event e;
		while (reader.next(e)) {
			busPush(slot, e);
			if (e.kind == Event::ON)
				startStroke(partFor(e.pitch), e.level, e.duration);
			// A DRUM IGNORES A NOTE-OFF. It rings for as long as it rings; the only thing that
			// stops one early is a choke, and that is a stroke rather than a release.
		}

		const float tone = params[P_TONE].getValue();
		const float master = params[P_LEVEL].getValue();
		float left = 0.f, right = 0.f;
		for (int i = 0; i < MAX_VOICES; i++) {
			if (!voices[i].active)
				continue;
			const float s = voiceSample(voices[i], args.sampleRate, tone) * partLevel(voices[i].part);
			const float pan = partPan(voices[i].part);
			left += s * (1.f - std::fmax(0.f, pan));
			right += s * (1.f + std::fmin(0.f, pan));
		}
		// AS LOUD AS THE OTHER INSTRUMENTS. The piano sends five volts at a full knob, and a kit
		// that arrives quieter than the part it is accompanying has to be made up for at the
		// mixer every time. The parts are each well under one, so the sum of a busy bar still
		// has room before it clips.
		const float gain = master * 8.f;
		outputs[O_L].setVoltage(math::clamp(left * gain, -12.f, 12.f));
		outputs[O_R].setVoltage(math::clamp(right * gain, -12.f, 12.f));

		hitLight = std::fmax(0.f, hitLight - args.sampleTime * 8.f);
		lights[L_HIT].setBrightness(hitLight);
	}
};


static Layout drumsLayout() {
	Layout L;
	L.hp = 10.f;
	L.title = "mpxDrums";

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			float size = 0.f, const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = Panel::CENTRE; i.heading = true; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};

	auto knob = [&](const std::string& key, float x, float y, int id, const std::string& name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.ticks = 2;
		L.items.push_back(i);
		label(key + ".label", x, y + 8.02f, name, 0.f, key);
	};

	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor ring) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = ring;
		L.items.push_back(i);
		label(key + ".label", x, y + 6.99f, name, 7.f, key);
	};

	knob("p.kick", 13.f, 28.f, DrumsModule::P_KICK, "KICK");
	knob("p.snare", 38.f, 28.f, DrumsModule::P_SNARE, "SNARE");
	knob("p.hat", 13.f, 50.f, DrumsModule::P_HAT, "HATS");
	knob("p.tom", 38.f, 50.f, DrumsModule::P_TOM, "TOMS");
	knob("p.metal", 13.f, 72.f, DrumsModule::P_METAL, "RIDE");
	knob("p.tone", 38.f, 72.f, DrumsModule::P_TONE, "TONE");
	knob("p.level", 25.f, 90.f, DrumsModule::P_LEVEL, "LEVEL");

	jack("in.mpx", Item::PORT_IN, 10.f, 104.f, DrumsModule::I_MPX, "mpx\nIN", NOTE_CABLE);
	jack("out.mpx", Item::PORT_OUT, 40.f, 104.f, DrumsModule::O_MPX, "mpx\nOUT", NOTE_CABLE);
	jack("out.l", Item::PORT_OUT, 18.f, 119.f, DrumsModule::O_L, "L", SIG_AUDIO);
	jack("out.r", Item::PORT_OUT, 33.f, 119.f, DrumsModule::O_R, "R", SIG_AUDIO);

	Item lamp;
	lamp.key = "lamp.hit"; lamp.kind = Item::LIGHT; lamp.id = DrumsModule::L_HIT;
	lamp.x = 25.f; lamp.y = 104.f;
	L.items.push_back(lamp);
	// THE LABELS ARE TIED TO WHAT THEY NAME. Each one's offset from its control is taken from
	// the positions above, so that moving a control in the panel editor takes its name with it.
	// Without this every offset is nought, and the first layout anybody saves puts every name
	// underneath the control it belongs to, where it cannot be seen.
	L.bindOffsets();
	return L;
}


struct DrumsWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	DrumsWidget(DrumsModule* module) {
		setModule(module);
		layout = drumsLayout();
		layoutApplyUser("mpxDrums", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxDrums");
	}

	void step() override {
		ModuleWidget::step();
		DrumsModule* m = dynamic_cast<DrumsModule*>(module);
		if (!m)
			return;
		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(DrumsModule::I_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				if (n >= MAX_UPSTREAM)
					break;
				engine::Cable* cable = cw->getCable();
				if (!cable)
					continue;
				uint32_t g = 0;
				const int s = noteBusOf(cable->outputModule, cable->outputId, &g);
				if (s < 0)
					continue;
				cw->color = NOTE_CABLE;
				slots[n] = s;
				generations[n] = g;
				n++;
			}
		}
		m->link(slots, generations, n);
	}
};

} // namespace px


Model* modelMpxDrums = createModel<px::DrumsModule, px::DrumsWidget>("mpxDrums");
