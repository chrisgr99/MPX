/** mpxGuitar — an MPX part played on a physically modelled guitar. See docs/guitar-model.md.

NOTHING TO PATCH BUT THE PART AND THE MIXER. The strings are digital waveguides (GuitarString.hpp)
and what the hands do to them is GuitarModel.hpp; this module is the cable, the performer and the
panel around them.

THE PERFORMER IS mpxGuitarist'S, so a part plays with the same timing, dynamics, strums and
bends whichever of the three plays it. What this module adds is that each strike arrives with its
technique: a palm mute puts the side of the hand on the string, a harmonic touches a node, a
hammer-on lands a finger on a string that is ringing. The strings act on that rather than on a
darker or shorter note.

THE MAIN PANEL is the guitar: where the strings are picked, how hard the pick is, how long they
ring, and the acoustic body or the electric's pickup, on large knobs; how much the free strings
ring in sympathy, how loud a sliding finger is and how stiff the strings are, on small ones; and
the switch between the two guitars. How each note is played comes from the part and has no
controls.

THE ELECTRIC'S FOLD-OUT is its tone, drive, bass, middle and treble, in one column. Pressing the
electric switch widens the module to show it, pushing the modules to its right along; switching
back to the acoustic folds it away. So every control on show does something in the guitar that
is playing.
*/
#include "plugin.hpp"
#include "GuitarModel.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"
#include "Perform.hpp"

namespace px {


static std::string rulesPath() {
	return asset::user("DreamerMPX/perform.txt");
}

/** From the strings' own units to Rack's: a string plucked as hard as it goes peaks at one. */
static const float OUTPUT_VOLTS = 4.f;
/** The level lamp's thresholds and how long each colour is held. */
static const float CLIP_VOLTS = 10.f, NEAR_VOLTS = 7.07f;
static const float CLIP_HOLD = 1.f, NEAR_HOLD = 0.3f, NOTE_HOLD = 0.06f;

/** Where the pick is, across the knob: from four hundredths of the string from the bridge, hard
against it, to two fifths, over the neck. */
static const float PICK_MIN = 0.02f, PICK_SPAN = 0.48f;
/** HOW FAR PAST A REAL GUITAR THE CHARACTER KNOBS GO: their tops are half again beyond the most a
real instrument does, so that turning one up makes plain what it does, and it can then be brought
back to taste. Each starts at the same realistic value as before, which now sits two thirds as far
round the dial. */
static const float EXAGGERATE = 1.5f;
/** How long the strings ring, 0.5 s to 20 s across the knob. */
static const float SUSTAIN_MIN = 0.5f, SUSTAIN_RATIO = 40.f;

static float sustainOf(float knob) {
	return SUSTAIN_MIN * std::pow(SUSTAIN_RATIO, knob);
}

static float knobForSustain(float seconds) {
	return std::log(seconds / SUSTAIN_MIN) / std::log(SUSTAIN_RATIO);
}


//?module Plays one MPX part on a modelled guitar: each string a digital waveguide, picked,
//? hammered, slid, palm-muted, bent and touched for harmonics as the part's notes say, through a
//? nylon or steel-string acoustic body or an electric's pickup and amplifier, mixed to stereo.
//?note How each note is played, its timing, dynamics, strum, bends and articulations, comes from
//? the part through the same performer as mpxGuitarist, whose rules are read from
//? DreamerMPX/perform.txt when the module starts and again from the right-click menu.
//?note A knob turned while a string rings takes effect when that string is next picked.
//?note Choosing the electric widens the module with a column of five more controls; choosing the
//? nylon or the steel-string narrows it again. The modules to its right are pushed along as it
//? widens and stay where they are as it narrows.
struct GuitarModule : Module, NoteSink {
	enum ParamId {
		P_PICK,
		P_BRIGHTNESS,
		P_SUSTAIN,
		P_BODY,
		P_ELECTRIC,
		P_TONE,
		P_DRIVE,
		P_FOLD,
		P_SYMPATHY,
		P_FINGER,
		P_STIFFNESS,
		P_BASS,
		P_MIDDLE,
		P_TREBLE,
		// APPENDED, NEVER INSERTED: a patch stores a parameter by its position in this list.
		P_TAPER,
		P_FRET_DAMPING,
		NUM_PARAMS
	};
	enum InputId {
		I_MPX,
		NUM_INPUTS
	};
	enum OutputId {
		O_L,
		O_R,
		NUM_OUTPUTS
	};
	enum LightId {
		/** The level lamp: green, orange and red. */
		ENUMS(L_LEVEL, 3),
		NUM_LIGHTS
	};

	/** THE LEVEL LAMP. Rack's audio interface takes 10 V as full scale and clips beyond it, so
	that is red; 3 dB under it is orange, the warning; a note struck is a green flash. Each is
	held long enough to see: red a second, orange 0.3 s, green 60 ms. The worst wins. */
	float clipHold = 0.f, nearHold = 0.f, noteHold = 0.f;

	Performer performer;
	GuitarModel guitar;
	BusReader reader;
	Instrument instrument;
	uint32_t instrumentChange = 0;
	float rate = 0.f;

	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
	std::atomic<bool> relink{false};
	PerformRules loadedRules;
	std::atomic<bool> rulesReady{false};
	std::string rulesMessage;

	bool attachedWas = false;


	GuitarModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(P_PICK, 0.f, 1.f, (0.15f - PICK_MIN) / PICK_SPAN, "Pick position",
			"% of the string from the bridge", 0.f, PICK_SPAN * 100.f, PICK_MIN * 100.f);
		//? Where along the strings they are picked, from 2% of their length from the bridge,
		//? glassy and thin, to 50%, the middle of the string, hollow and soft. A string picked at
		//? a point sounds none of the partials with a node there.
		configParam(P_BRIGHTNESS, 0.f, 1.f, 0.6f / EXAGGERATE, "Brightness", "%", 0.f, 100.f);
		//? The pick's hardness, from a fingertip, which rounds the string's shape and takes its
		//? upper partials down from about 200 Hz, to a hard plectrum at two thirds of the way,
		//? which leaves the shape sharp and adds a click as it leaves the string. Above that the
		//? click grows, to three times a plectrum's at the top.
		configParam(P_SUSTAIN, 0.f, 1.f, knobForSustain(5.f), "Sustain", " s", SUSTAIN_RATIO,
			SUSTAIN_MIN);
		//? How long the open low E string rings: the time its fundamental takes to fall 60 dB,
		//? from 0.5 s to 20 s. Higher notes ring shorter as Taper says, fretted notes shorter as
		//? Fret damping says. Shorter is also duller, the upper partials dying faster in
		//? proportion, which is how old strings sound.
		configParam(P_BODY, 0.f, 1.f, 0.8f / EXAGGERATE, "Body", "%", 0.f, 100.f);
		//? On the nylon and the steel-string, how much of the body is heard, from the bare strings
		//? to the full body at two thirds of the way: its air resonance near 100 Hz, the top
		//? plate's modes above it, and its highs rolled away. Above that the resonances are pushed
		//? past a real body's. On the electric, where the pickup is, from the bridge, thin and
		//? bright, to the neck, round and warm.
		// IN THE ORDER THE PANEL LISTS THEM, since the lamp group shows a choice's value as its
		// place in the list. Patches saved before are renumbered as they open: see dataFromJson.
		configSwitch(P_ELECTRIC, 0.f, 2.f, 1.f, "Guitar", {"Nylon", "Steel-string", "Electric"});
		//? The guitar. Nylon: a classical, its strings soft, quick to lose their highs and played
		//? with the flesh of the finger, its body warm. Steel: a steel-string acoustic, brighter
		//? and stiffer. Electric: steel strings heard by a magnetic pickup through an amplifier,
		//? with tone, drive, bass, middle and treble beside the panel.
		configParam(P_TONE, 0.f, 1.f, 1.f, "Tone", "%", 0.f, 100.f);
		//? The electric guitar's tone, two low-pass stages before the amplifier, from open at the
		//? top to rolled off from 300 Hz at the bottom.
		configParam(P_DRIVE, 0.f, 1.f, 0.3f, "Drive", "%", 0.f, 100.f);
		//? How hard the electric's strings drive its amplifier, a drive of 1 to 18: at the bottom
		//? a single note is clean and a chord played hard begins to break up; at the top
		//? everything is broken up, heavily.
		// NOT ON THE PANEL: the electric switch opens the fold-out now. Kept so the parameters
		// after it keep their numbers in patches already saved.
		configParam(P_FOLD, 0.f, 1.f, 0.f, "Unused");
		configParam(P_SYMPATHY, 0.f, 1.f, 0.5f / EXAGGERATE, "Sympathy", "%", 0.f, 100.f);
		//? How much the strings not being played ring in sympathy with those that are, each at
		//? its open pitch, from the vibration that reaches it through the bridge. At the bottom,
		//? none; at a third of the way, an ordinary acoustic's; at the top, three times that.
		configParam(P_FINGER, 0.f, 1.f, 0.5f / EXAGGERATE, "Finger noise", "%", 0.f, 100.f);
		//? How loud the squeak of a finger sliding along a wound string is, in slides into, out
		//? of and between notes. At the bottom, silent; at the top, plainly louder than any
		//? player's. A bend makes none.
		configParam(P_STIFFNESS, 0.f, 1.f, 0.7f / EXAGGERATE, "Stiffness", "%", 0.f, 100.f);
		//? How stiff the strings are: the stiffer, the sharper their upper partials stand above
		//? whole multiples of the fundamental. At the top the eighth partial is 9 cents sharp,
		//? on any string, past any real string's. The nylon's strings take a sixth of it.
		configParam(P_TAPER, 0.f, 1.f, 0.5f / EXAGGERATE, "Taper", "%", 0.f, 100.f);
		//? How much shorter high notes ring than low ones. At the bottom every note rings as long
		//? as Sustain says. Above it each octave over the open low E rings shorter: at a third of
		//? the way 0.66 of the octave below, at the top 0.29, so the treble dies away under a
		//? ringing bass.
		configParam(P_FRET_DAMPING, 0.f, 1.f, 0.5f / EXAGGERATE, "Fret damping", "%", 0.f, 100.f);
		//? How much a fretting finger shortens and dulls a note compared with the same string
		//? open. At the bottom fretted and open notes ring alike; at a third of the way a fretted
		//? note rings three quarters as long; at the top a quarter as long and much duller, and
		//? the open strings stand out.
		configParam(P_BASS, -18.f, 18.f, 3.f, "Bass", " dB");
		//? The electric amplifier's low shelf, below about 100 Hz, from -18 dB to +18 dB.
		configParam(P_MIDDLE, -18.f, 18.f, -6.f, "Middle", " dB");
		//? The electric amplifier's middle band, around 500 Hz, from -18 dB to +18 dB. Below
		//? nought, the scooped middle of a clean amplifier.
		configParam(P_TREBLE, -18.f, 18.f, 4.f, "Treble", " dB");
		//? The electric amplifier's high shelf, above about 3 kHz, from -18 dB to +18 dB.
		configInput(I_MPX, "MPX note");
		//? One part, played on the guitar: its notes, with how each is played, from Guitar Chart
		//? or any MPX source. The instrument the cable names is shown below the guitars and
		//? gives the strings their tuning and capo.
		configOutput(O_L, "Left");
		//? The guitar, left and right. A single note at a moderate dynamic peaks about 2V.
		configOutput(O_R, "Right");
		//? The guitar, left and right. A single note at a moderate dynamic peaks about 2V.
		configLight(L_LEVEL, "Level");
		//? Flashes green as each note is struck, lights orange when the output passes 7.07V,
		//? 3 dB under clipping, and red when it reaches 10V, where Rack's audio interface
		//? clips. Red is held for a second and orange for 0.3 s, so a single peak can be seen.
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
		guitar.silence();
	}

	/** Which numbering the guitar setting is saved in. */
	static const int GUITAR_ORDER = 2;

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "guitarOrder", json_integer(GUITAR_ORDER));
		return rootJ;
	}

	/** A patch saved when sympathy and finger noise were switches in the menu: a switch left off
	is a knob at the bottom.

	AND ONE SAVED BEFORE THE GUITARS WERE NUMBERED AS THE PANEL LISTS THEM. The setting was the
	acoustic at nought and the electric at one, and then for a while the nylon at two; now it is
	nylon, steel-string, electric. Each old number is one less than its new one, round the three. */
	void dataFromJson(json_t* rootJ) override {
		if (!json_is_integer(json_object_get(rootJ, "guitarOrder"))) {
			const int old = (int) std::lround(params[P_ELECTRIC].getValue());
			params[P_ELECTRIC].setValue((float) ((old + 1) % 3));
		}
		if (json_is_false(json_object_get(rootJ, "sympathetic")))
			params[P_SYMPATHY].setValue(0.f);
		if (json_is_false(json_object_get(rootJ, "fingerNoise")))
			params[P_FINGER].setValue(0.f);
	}

	/** THE OPEN STRINGS, from the instrument on the cable, so a harmonic knows what it is a
	harmonic of. String one is the highest, as the performer counts. */
	void tuneOpenStrings() {
		for (int s = 0; s < GuitarModel::STRINGS; s++) {
			guitar.openKnown[s] = s < (int) instrument.stringCount;
			if (guitar.openKnown[s])
				guitar.open[s] = ((float) instrument.tuning[s] + (float) instrument.capo - 60.f)
					/ 12.f;
		}
	}

	void process(const ProcessArgs& args) override {
		if (args.sampleRate != rate) {
			rate = args.sampleRate;
			guitar.setSampleRate(rate);
		}
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}
		if (rulesReady.exchange(false))
			performer.rules = loadedRules;

		// THE CABLE GOING IS EVERYTHING STOPPING, as on mpxGuitarist.
		const bool attached = reader.attached();
		if (!attached && attachedWas)
			performer.silence();
		attachedWas = attached;

		Instrument in;
		if (reader.instrument(in) && in.change != instrumentChange) {
			instrumentChange = in.change;
			instrument = in;
			performer.strings(in.stringCount);
			tuneOpenStrings();
		}

		// HUMANISE AT ITS OWN AMOUNT, as the built-in band has it.
		performer.humanise(1.f);

		// THE PANEL, applied as each string is next struck: a knob turned does not change a
		// string that is already ringing, as it would not on an instrument.
		const int kind = (int) std::lround(params[P_ELECTRIC].getValue());
		const float sustainKnob = params[P_SUSTAIN].getValue();
		guitar.settings.pickPosition = PICK_MIN + PICK_SPAN * params[P_PICK].getValue();
		guitar.settings.hardness = EXAGGERATE * params[P_BRIGHTNESS].getValue();
		guitar.settings.sustain = sustainOf(sustainKnob);
		guitar.settings.damping = 0.7f - 0.5f * sustainKnob;
		// The body exaggerated on the acoustics; on the electric the same knob is where the
		// pickup is, which cannot go past the neck.
		guitar.settings.body = (kind == 2 ? 1.f : EXAGGERATE) * params[P_BODY].getValue();
		guitar.settings.nylon = kind == 0;
		guitar.settings.electric = kind == 2;
		guitar.settings.tone = params[P_TONE].getValue();
		guitar.settings.amp.drive = params[P_DRIVE].getValue();
		guitar.settings.sympathy = EXAGGERATE * params[P_SYMPATHY].getValue();
		guitar.settings.fingerNoise = EXAGGERATE * params[P_FINGER].getValue();
		guitar.settings.stiffness = EXAGGERATE * params[P_STIFFNESS].getValue();
		guitar.settings.taper = EXAGGERATE * params[P_TAPER].getValue();
		guitar.settings.fretDamping = EXAGGERATE * params[P_FRET_DAMPING].getValue();
		guitar.settings.amp.bass = params[P_BASS].getValue();
		guitar.settings.amp.middle = params[P_MIDDLE].getValue();
		guitar.settings.amp.treble = params[P_TREBLE].getValue();

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

		// WHERE EVERY STRING'S PITCH IS, before anything is struck: a strike tunes its string
		// to the note it is striking, which the performer has already moved the voice to.
		for (int s = 0; s < GuitarModel::STRINGS && s < performer.voiceCount(); s++) {
			const PerformVoice& v = performer.voice(s);
			guitar.setPitch(s, v.pitch);
			guitar.pan[s] = math::clamp(v.pan + instrument.pan, -1.f, 1.f);
		}

		// WHAT THE HANDS DO. A strike is a pick, with its technique. A level that comes with a
		// technique is a note the left hand sounded without one — a hammer-on, a pull-off, a
		// tap — and any other is the performer moving a sounding note's loudness. A release is
		// the hand coming down on the string.
		PerformMessage m;
		while (performer.next(m)) {
			switch (m.kind) {
				case PerformMessage::ATTACK:
					guitar.strike(m.voice, m.value, m.technique, m.fret);
					noteHold = NOTE_HOLD;
					break;
				case PerformMessage::LEVEL:
					if (m.technique != 0)
						guitar.legato(m.voice, m.value, m.technique);
					else
						guitar.setLevel(m.voice, m.value);
					break;
				case PerformMessage::RELEASE:
					guitar.end(m.voice);
					break;
				default:
					break;
			}
		}

		float left = 0.f, right = 0.f;
		guitar.process(&left, &right);
		outputs[O_L].setVoltage(left * OUTPUT_VOLTS);
		outputs[O_R].setVoltage(right * OUTPUT_VOLTS);

		const float peak = std::fmax(std::fabs(left), std::fabs(right)) * OUTPUT_VOLTS;
		if (peak >= CLIP_VOLTS)
			clipHold = CLIP_HOLD;
		else if (peak >= NEAR_VOLTS)
			nearHold = NEAR_HOLD;
		const float dt = args.sampleTime;
		clipHold = std::fmax(0.f, clipHold - dt);
		nearHold = std::fmax(0.f, nearHold - dt);
		noteHold = std::fmax(0.f, noteHold - dt);
		const bool red = clipHold > 0.f, orange = !red && nearHold > 0.f;
		lights[L_LEVEL + 0].setBrightness(!red && !orange && noteHold > 0.f ? 1.f : 0.f);
		lights[L_LEVEL + 1].setBrightness(orange ? 1.f : 0.f);
		lights[L_LEVEL + 2].setBrightness(red ? 1.f : 0.f);
	}
};


// ---- the panel ---------------------------------------------------------------------------------

/** THE MAIN PANEL, ten HP, and the electric's fold-out beside it, four more. */
static const float MAIN_W = 50.8f;
static const float FOLD_W = 20.32f;
static const float FULL_W = MAIN_W + FOLD_W;
/** The large knobs' two columns, the small knobs' three, and the fold-out's one. */
static const float BIG1 = 13.f, BIG2 = MAIN_W - 13.f;
static const float SMALL1 = 8.1f, SMALL2 = MAIN_W / 2.f, SMALL3 = MAIN_W - 7.85f;
static const float FOLD_X = MAIN_W + FOLD_W / 2.f;
/** THE THREE GUITARS' LAMP GROUP, placed by its top left corner as a lamp group is. It sizes
itself from its names: the longest, ELECTRIC, eight characters at eight points, comes to 48 px
of names and lamp spacing and 13 px of lamp, 21 mm in all, so the corner that far and a 1.5 mm
margin in from the right edge puts the lamps against the margin. The lamps 5.5 mm apart. */
static const float KIND_PITCH = 5.5f;
static const float KIND_X = MAIN_W - 1.5f - (48.3f + 13.f) * 25.4f / 75.f;


/** What is on the cable. */
struct GuitarDisplay : widget::Widget {
	GuitarModule* module = NULL;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0 || !module)
			return;
		std::string name = "\u2014";
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


static Layout guitarLayout() {
	Layout L;
	// BUILT AT ITS FULL WIDTH, the fold-out included; the widget narrows itself to the main
	// panel and hides the fold-out's controls until it is shown. The title stays over the main
	// panel.
	L.hp = FULL_W / 5.08f;
	L.title = "mpxGuitar";
	L.titleWidth = MAIN_W;

	auto label = [&](const char* key, float x, float y, const char* text, const char* owner,
			bool heading, float size) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.defaultText = text; i.align = Panel::CENTRE; i.heading = heading; i.size = size;
		if (owner)
			i.owner = owner;
		L.items.push_back(i);
	};
	auto jack = [&](const char* key, Item::Kind kind, float x, float y, int id,
			const char* name, NVGcolor colour) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = colour;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 7.5f, name, key, false, 0.f);
	};
	auto knob = [&](const char* key, int id, float x, float y, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "knob"; i.x = x; i.y = y;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 8.5f, name, key, true, 8.f);
	};

	auto small = [&](const char* key, int id, float x, float y, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "knob.small"; i.x = x; i.y = y;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 7.f, name, key, true, 7.f);
	};

	// THE PART COMING IN AND THE GUITAR PLAYING IT, at the top: the MPX input on the left, and on
	// the right the three guitars, a lamp group reading down, each named on its left. Choosing
	// the electric also opens its fold-out beside the panel. The name of the instrument on the
	// cable below them.
	jack("in.mpx", Item::PORT_IN, 10.f, 16.f, GuitarModule::I_MPX, "mpx IN", NOTE_CABLE);

	{
		Item kinds;
		kinds.key = "p.guitar"; kinds.kind = Item::PARAM; kinds.id = GuitarModule::P_ELECTRIC;
		kinds.style = "lamps"; kinds.names = {"NYLON", "STEEL", "ELECTRIC"};
		kinds.horizontal = false; kinds.pitch = KIND_PITCH; kinds.labelSide = Panel::LEFT;
		kinds.x = KIND_X; kinds.y = 9.f;
		L.items.push_back(kinds);
	}

	Item what;
	what.key = "d.instrument"; what.kind = Item::DISPLAY;
	what.x = 3.f; what.y = 29.f; what.w = MAIN_W - 6.f; what.h = 5.f;
	L.items.push_back(what);

	// THE GUITAR, on large knobs: the pick, then the strings and the body or the pickup.
	knob("p.pick", GuitarModule::P_PICK, BIG1, 42.f, "PICK");
	knob("p.brightness", GuitarModule::P_BRIGHTNESS, BIG2, 42.f, "BRIGHTNESS");
	knob("p.sustain", GuitarModule::P_SUSTAIN, BIG1, 64.f, "SUSTAIN");
	knob("p.body", GuitarModule::P_BODY, BIG2, 64.f, "BODY");

	// The strings' character, set once for a guitar, on small knobs.
	small("p.sympathy", GuitarModule::P_SYMPATHY, SMALL1, 84.f, "SYMPATHY");
	small("p.finger", GuitarModule::P_FINGER, SMALL2, 84.f, "FINGER NOISE");
	small("p.stiffness", GuitarModule::P_STIFFNESS, SMALL3, 84.f, "STIFFNESS");
	// How the notes ring against one another: high against low, fretted against open.
	small("p.taper", GuitarModule::P_TAPER, MAIN_W / 3.f, 97.f, "TAPER");
	small("p.fretdamping", GuitarModule::P_FRET_DAMPING, MAIN_W * 2.f / 3.f, 97.f, "FRET DAMPING");

	jack("out.l", Item::PORT_OUT, 15.f, 113.f, GuitarModule::O_L, "L", SIG_AUDIO);
	jack("out.r", Item::PORT_OUT, MAIN_W - 15.f, 113.f, GuitarModule::O_R, "R", SIG_AUDIO);
	// THE LEVEL LAMP between the two outputs: a note struck, nearly clipping, clipping.
	{
		Item lamp;
		lamp.key = "lamp.level"; lamp.kind = Item::LIGHT; lamp.style = "light.level";
		lamp.id = GuitarModule::L_LEVEL; lamp.x = MAIN_W / 2.f; lamp.y = 113.f;
		L.items.push_back(lamp);
		label("lamp.level.label", MAIN_W / 2.f, 117.5f, "LEVEL", "lamp.level", false, 0.f);
	}

	// THE ELECTRIC'S FOLD-OUT, one column: the guitar's tone, then the amplifier, set off from
	// the guitar by a rule down its left edge.
	{
		Item rule;
		rule.key = "r.electric"; rule.kind = Item::RULE; rule.horizontal = false;
		rule.x = MAIN_W; rule.y = 9.f; rule.h = 114.f;
		L.items.push_back(rule);
	}
	knob("p.tone", GuitarModule::P_TONE, FOLD_X, 16.f, "TONE");
	knob("p.drive", GuitarModule::P_DRIVE, FOLD_X, 37.f, "DRIVE");
	knob("p.bass", GuitarModule::P_BASS, FOLD_X, 58.f, "BASS");
	knob("p.middle", GuitarModule::P_MIDDLE, FOLD_X, 79.f, "MIDDLE");
	knob("p.treble", GuitarModule::P_TREBLE, FOLD_X, 100.f, "TREBLE");

	L.bindOffsets();
	return L;
}


struct GuitarWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	GuitarWidget(GuitarModule* module) {
		setModule(module);
		layout = guitarLayout();
		layoutApplyUser("mpxGuitar", layout);
		panel = new Panel;
		addChild(panel);
		GuitarDisplay* what = new GuitarDisplay;
		what->module = module;
		layoutPlaceDisplay(this, layout, "d.instrument", what);

		layoutBuild(this, panel, layout);
		applyFold(false);
	}

	/** Whether the fold-out was last shown. */
	int folded = -1;

	/** THE ELECTRIC'S FOLD-OUT SHOWN OR HIDDEN: the module's width, and its knobs and names.
	Widening it pushes the modules to its right along, as Rack does when a module is dropped
	among others; narrowing it leaves them where they are. */
	void applyFold(bool shown) {
		const float width = mm2px(shown ? FULL_W : MAIN_W);
		box.size.x = width;
		if (panel)
			panel->box.size.x = width;
		static const char* KEYS[] = {"r.electric", "p.tone", "p.drive", "p.bass", "p.middle",
			"p.treble", NULL};
		for (int i = 0; KEYS[i]; i++) {
			Item* item = layout.find(KEYS[i]);
			if (!item)
				continue;
			item->hidden = !shown;
			if (item->widget)
				item->widget->visible = shown;
			Item* name = layout.find(std::string(KEYS[i]) + ".label");
			if (name)
				name->hidden = !shown;
		}
		layoutRefreshPanel(panel, layout);
		if (shown && module && APP->scene && APP->scene->rack && parent)
			APP->scene->rack->setModulePosForce(this, box.pos);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxGuitar");
		GuitarModule* m = dynamic_cast<GuitarModule*>(module);
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

	/** Whether the panel was last labelled for the electric. */
	int labelledElectric = -1;

	/** THE BODY KNOB IS THE PICKUP ON THE ELECTRIC, and says so: its name on the panel and in
	its tooltip follow the switch. */
	void relabel(bool electric) {
		Item* label = layout.find("p.body.label");
		if (label)
			label->text = electric ? "PICKUP" : "BODY";
		layoutRefreshPanel(panel, layout);
		if (module)
			module->paramQuantities[GuitarModule::P_BODY]->name =
				electric ? "Pickup position" : "Body";
	}

	/** The MPX link, made here on the main thread, as every MPX input's is. */
	void step() override {
		ModuleWidget::step();
		GuitarModule* m = dynamic_cast<GuitarModule*>(module);
		const bool electric = m
			&& (int) std::lround(m->params[GuitarModule::P_ELECTRIC].getValue()) == 2;
		if ((int) electric != labelledElectric) {
			labelledElectric = electric;
			relabel(electric);
		}
		// THE FOLD-OUT IS THE ELECTRIC'S, so it is open exactly when the electric is playing.
		const bool shown = electric;
		if ((int) shown != folded) {
			folded = shown;
			applyFold(shown);
		}
		if (!m)
			return;
		PortWidget* port = getInput(GuitarModule::I_MPX);
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


Model* modelMpxGuitar = createModel<px::GuitarModule, px::GuitarWidget>("mpxGuitar");
