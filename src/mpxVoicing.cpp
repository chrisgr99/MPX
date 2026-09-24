/** mpxVoicing — where the notes of the chord go, decided once for the whole chain.

WHAT IT IS FOR. A chord on the cable is a degree and a quality: a set of tones, not a set of
notes. Deciding which of those tones are worth playing, how many voices play them, where they sit
and how little they move between chords is a job of its own, and every module that plays the
harmony was doing it again — mpxComp carefully, mpxArp not at all, which is why a line out of the
arp sits wrong against a chart the comp is playing well.

So it is done here, once, and published on the cable beside the harmony and the pedals. Anything
downstream that wants the notes takes them and is in step with everything else that took them;
anything that wants the chord still has it. See docs/players.md.

IT PLAYS NOTHING. Every event, the harmony and the pedals pass through untouched. What it adds is
state: where the voices are now.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"
#include "Voicing.hpp"
#include "NoteLog.hpp"

#include <cmath>

namespace px {


static const char* SPREAD_NAMES[VOICING_SPREADS] = {"Close", "Drop two", "Open"};
static const char* COLOUR_NAMES[VOICING_COLOURS] = {"Triad", "Sevenths", "Extensions"};

/** WHAT TO DO WITH A CHORD THAT IS GONE IN A BEAT. A turnaround puts three chords in a bar, and
a player does not answer each of them with the full chord: the hands thin out, and at the fastest
they play the two notes that say which chord it is — the third and the seventh — and nothing else.
Voiced at full weight instead, a turnaround lurches, however good the voice leading is.

The threshold is not a setting. Under two beats is short, which is a fact about how a bar is
counted rather than a matter of taste. */
enum ShortChord {
	SHORT_FULL,      /**< every chord voiced alike, however brief */
	SHORT_THIN,      /**< a voice or two fewer while the harmony is moving quickly */
	SHORT_GUIDE,     /**< the third and the seventh, and nothing else */
	SHORT_WAYS,
};
static const char* SHORT_NAMES[SHORT_WAYS] = {"Full", "Thin", "Guide tones"};

/** How long a chord has to last to be voiced in full, in beats. */
static const float SHORT_BEATS = 2.f;


struct VoicingModule : Module, NoteSource, NoteSink {
	enum ParamId {
		P_VOICES,
		P_CENTRE,
		P_SPAN,
		P_SPREAD,
		P_COLOUR,
		P_BASS,
		P_LEAD,
		P_RECORD,
		P_SHORT,
		NUM_PARAMS
	};
	enum InputId { I_MPX, NUM_INPUTS };
	enum OutputId { O_MPX, NUM_OUTPUTS };
	enum LightId { L_CHANGE, NUM_LIGHTS };

	VoicingModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(P_VOICES, 0.f, (float) (VOICING_MAX - 1), 3.f, "Voices",
			{"1", "2", "3", "4", "5", "6"});
		configParam(P_CENTRE, -24.f, 24.f, 0.f, "Register", " semitones from middle C");
		configSwitch(P_SPAN, 0.f, 2.f, 1.f, "Span", {"1 octave", "2 octaves", "3 octaves"});
		configSwitch(P_SPREAD, 0.f, (float) (VOICING_SPREADS - 1), 0.f, "Spread",
			{SPREAD_NAMES[0], SPREAD_NAMES[1], SPREAD_NAMES[2]});
		configSwitch(P_COLOUR, 0.f, (float) (VOICING_COLOURS - 1), (float) VOICING_EXTENSIONS,
			"Chord tones", {COLOUR_NAMES[0], COLOUR_NAMES[1], COLOUR_NAMES[2]});
		configSwitch(P_BASS, 0.f, 1.f, 0.f, "Root", {"Rootless", "With root"});
		configParam(P_LEAD, 0.f, 1.f, 1.f, "Voice leading", "%", 0.f, 100.f);

		configSwitch(P_RECORD, 0.f, 1.f, 0.f, "Record what it decides", {"Off", "Recording"});
		configSwitch(P_SHORT, 0.f, (float) (SHORT_WAYS - 1), (float) SHORT_THIN, "Short chords",
			{SHORT_NAMES[0], SHORT_NAMES[1], SHORT_NAMES[2]});
		configInput(I_MPX, "MPX note");
		configOutput(O_MPX, "MPX note");

		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		slot = busClaim(&generation);
	}

	~VoicingModule() {
		busRelease(slot);
	}

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

	// ---- the cables, as the widget finds them ----

	int slot = -1;
	uint32_t generation = 0;
	BusReader reader;

	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
	std::atomic<bool> relink{false};

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

	// ---- state ----

	/** Where each voice sits, in volts. Kept between chords: this is the memory that makes the
	voices move as little as they can rather than rebuilding the chord each time. */
	float voice[VOICING_MAX] = {0.f};
	int voices = 0;
	/** The chord last voiced, so a change can be recognised. */
	int hadClasses[MAX_CHORD_TONES] = {0};
	int hadCount = -1;
	/** The settings the last voicing was made under, as one number: all that is ever asked is
	whether any of them moved. */
	float hadSig = 0.f;
	/** Bumped whenever the notes change, so a reader can tell a new voicing from the same one
	read again. */
	uint32_t change = 0;
	float lightFade = 0.f;

	/** WHAT IT DECIDED, WRITTEN DOWN while a take is running — see NoteLog.hpp. This module
	plays nothing, so what goes in the log is the chord it was handed and where it put the
	voices. */
	NoteLog noteLog;
	std::atomic<bool> logging{false};
	SettingsWatcher settingsWatch;
	double logSeconds = 0.0;

	void writeToLog(const Harmony& h, bool haveHarmony) {
		if (!logging.load(std::memory_order_relaxed))
			return;
		LogNote n;
		n.kind = LogNote::VOICING;
		n.seconds = logSeconds;
		n.count = voices;
		for (int i = 0; i < voices && i < 8; i++)
			n.pitches[i] = voice[i];
		if (haveHarmony && h.valid) {
			n.haveChord = true;
			n.key = h.key;
			n.chord = h.current;
			n.beat = h.beat;
			const int barBeats = std::max(1, (int) h.barBeats);
			n.bar = (int) std::floor(h.beat / (double) barBeats);
			n.beatInBar = (float) (h.beat - (double) n.bar * barBeats);
		}
		noteLog.push(n);
	}

	void publish() {
		ChordVoicing v;
		v.valid = (voices > 0);
		v.count = voices;
		for (int i = 0; i < voices && i < 8; i++)
			v.pitch[i] = voice[i];
		v.change = change;
		busPublishVoicing(slot, v);
	}

	void process(const ProcessArgs& args) override {
		if (logging.load(std::memory_order_relaxed)) {
			logSeconds += args.sampleTime;
			float values[NUM_PARAMS];
			for (int i = 0; i < NUM_PARAMS; i++)
				values[i] = params[i].getValue();
			settingsWatch.step(noteLog, values, NUM_PARAMS, logSeconds);
		}
		else {
			logSeconds = 0.0;
			settingsWatch.reset();
		}
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}

		// EVERYTHING PASSES THROUGH. The events, the harmony and the pedals are the upstream's
		// and are forwarded unchanged; this module adds one thing and takes nothing away.
		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(0.f);
		Event e;
		while (reader.next(e))
			busPush(slot, e);
		Harmony h;
		const bool haveHarmony = reader.harmony(h);
		if (haveHarmony)
			busPublishHarmony(slot, h);
		{
			float sustain = 0.f, soft = 0.f;
			reader.pedals(sustain, soft);
			busPublishPedals(slot, sustain, soft);
		}

		// ---- the voicing itself ----
		ChordTone tones[MAX_CHORD_TONES];
		int count = 0;
		if (haveHarmony && h.valid)
			count = chordVoicingTones(h.current, h.key, tones);

		bool chordChanged = (count != hadCount);
		for (int i = 0; !chordChanged && i < count; i++)
			chordChanged = (tones[i].pc != hadClasses[i]);

		// ONE NUMBER FOR THE SETTINGS, so that moving any of them voices the chord again without
		// a copy of each being kept.
		const float sig = params[P_VOICES].getValue()
			+ 16.f * params[P_CENTRE].getValue()
			+ 4096.f * params[P_SPAN].getValue()
			+ 32768.f * params[P_SPREAD].getValue()
			+ 262144.f * params[P_COLOUR].getValue()
			+ 1048576.f * params[P_BASS].getValue()
			+ 2097152.f * params[P_LEAD].getValue();
		const bool settingsChanged = (sig != hadSig);
		hadSig = sig;

		if (count > 0 && (chordChanged || settingsChanged)) {
			hadCount = count;
			for (int i = 0; i < count; i++)
				hadClasses[i] = tones[i].pc;

			int want = (int) std::round(params[P_VOICES].getValue()) + 1;
			// A CHORD THAT IS GONE IN A BEAT is played with fewer notes, or with the two that
			// say which chord it is. See ShortChord.
			const int shortWay = (int) std::round(params[P_SHORT].getValue());
			const bool brief = haveHarmony && h.valid && h.beatsToNext > 0.f
				&& h.beatsToNext < SHORT_BEATS;
			if (brief && shortWay == SHORT_THIN)
				want = std::max(2, want - 2);
			else if (brief && shortWay == SHORT_GUIDE)
				want = 2;
			const float centre = params[P_CENTRE].getValue() / 12.f;
			const float span = std::round(params[P_SPAN].getValue()) + 1.f;
			const bool ownBass = params[P_BASS].getValue() > 0.5f;
			const int colour = (int) std::round(params[P_COLOUR].getValue());

			int pcs[VOICING_MAX];
			// GUIDE TONES ARE THE THIRD AND THE SEVENTH, asked for as such rather than arrived
			// at by taking two notes off the top: a triad's ranking would hand back the root.
			const int useColour = (brief && shortWay == SHORT_GUIDE)
				? VOICING_SEVENTHS : colour;
			const bool useBass = ownBass && !(brief && shortWay == SHORT_GUIDE);
			const int n = voiceChooseTones(tones, count, want, useBass, useColour, pcs);
			if (n > 0) {
				VoicingRequest req;
				req.pcs = pcs;
				req.count = n;
				req.held = voice;
				req.heldCount = voices;
				req.lo = centre - span / 2.f;
				req.hi = centre + span / 2.f;
				// WHILE THE HARMONY IS MOVING QUICKLY, MOVEMENT MATTERS MORE. A run of short
				// chords solved one at a time can leap on every one of them; leaning the search
				// towards the notes already sounding is what makes a turnaround read as one
				// gesture rather than three.
				req.lead = brief ? std::fmax(params[P_LEAD].getValue(), 0.85f)
					: params[P_LEAD].getValue();
				req.spread = (int) std::round(params[P_SPREAD].getValue());
				// The root is first in stack order, so a part carrying its own bottom pins the
				// lowest voice to tone nought.
				req.bassTone = useBass ? 0 : -1;
				voices = voicePlace(req, voice);
				change++;
				writeToLog(h, haveHarmony);
				if (chordChanged)
					lightFade = 1.f;
			}
		}
		else if (count == 0) {
			voices = 0;
			hadCount = -1;
		}

		publish();

		lightFade = std::fmax(0.f, lightFade - args.sampleTime * 4.f);
		lights[L_CHANGE].setBrightness(lightFade);
	}
};


static Layout voicingLayout() {
	Layout L;
	L.hp = 10.f;
	L.title = "mpxChordVoicing";

	/** Half the height of a ten-point name, and the height a plate's figures are set to. */
	static const float NAME_MM = 2.44f;
	static const float LAMP_MM = 6.5f / 2.9528f;

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			bool heading = true, float size = 0.f, const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = Panel::CENTRE; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};

	auto knob = [&](const std::string& key, float x, float y, int id, const std::string& name,
			int ticks) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.ticks = ticks;
		L.items.push_back(i);
		label(key + ".label", x, y + 8.02f, name, true, 0.f, key);
	};

	/** A column of lamps, placed by the centre of the lamps themselves, its name above. */
	auto col = [&](const std::string& key, float x, float y, int id, const std::string& group,
			const std::vector<std::string>& names) {
		const float step = 2.f * LAMP_MM + 1.f;
		const float h = 2.f * LAMP_MM + step * (float) (names.size() - 1);
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "lamps";
		i.x = x - LAMP_MM; i.y = y - h / 2.f; i.names = names; i.horizontal = false;
		i.pitch = step; i.labelSide = Panel::RIGHT;
		L.items.push_back(i);
		label(key + ".group", x, i.y - 0.5f - NAME_MM / 2.f, group, true, 0.f, key);
	};

	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = NOTE_CABLE;
		L.items.push_back(i);
		label(key + ".label", x, y + 6.99f, name, true, 7.f, key);
	};

	// A record button in the title bar: see layoutAddRecordButton.
	layoutAddRecordButton(L, VoicingModule::P_RECORD);

	knob("p.voices", 12.f, 24.f, VoicingModule::P_VOICES, "VOICES", 6);
	knob("p.centre", 38.f, 24.f, VoicingModule::P_CENTRE, "REGISTER", 3);

	col("p.span", 10.f, 50.f, VoicingModule::P_SPAN, "SPAN", {"1", "2", "3"});
	col("p.spread", 28.f, 50.f, VoicingModule::P_SPREAD, "SPREAD",
		{"CLOSED", "DROP 2", "OPEN"});

	col("p.colour", 10.f, 76.f, VoicingModule::P_COLOUR, "COMPLEXITY",
		{"TRIAD", "7THS", "EXT"});
	col("p.bass", 34.f, 76.f, VoicingModule::P_BASS, "ROOT", {"ROOTLESS", "WITH ROOT"});

	// PUT WHERE THERE IS ROOM, to be placed properly in the panel editor.
	col("p.short", 10.f, 97.f, VoicingModule::P_SHORT, "SHORT CHORDS",
		{"FULL", "THIN", "GUIDE"});

	knob("p.lead", 40.f, 97.f, VoicingModule::P_LEAD, "VOICE LEADING", 2);

	jack("in.mpx", Item::PORT_IN, 12.f, 114.f, VoicingModule::I_MPX, "mpx\nIN");
	jack("out.mpx", Item::PORT_OUT, 39.f, 114.f, VoicingModule::O_MPX, "mpx\nOUT");

	Item lamp;
	lamp.key = "lamp.change"; lamp.kind = Item::LIGHT; lamp.id = VoicingModule::L_CHANGE;
	lamp.x = 25.5f; lamp.y = 114.f;
	L.items.push_back(lamp);
	return L;
}


/** The controls in the order the module declares them, so a settings line reads as words. */
static const char* VOICING_PARAM_NAMES[] = {
	"voices", "register", "span", "spread", "colour", "root", "lead", "record", "short",
};


struct VoicingWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	VoicingWidget(VoicingModule* module) {
		setModule(module);
		layout = voicingLayout();
		layoutApplyUser("mpxVoicing", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	NoteLogWriter writer;

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxVoicing");
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(writer.where()));
	}

	void step() override {
		ModuleWidget::step();
		VoicingModule* m = dynamic_cast<VoicingModule*>(module);
		if (!m)
			return;
		const bool want = m->params[VoicingModule::P_RECORD].getValue() > 0.5f;
		if (want != writer.writing()) {
			if (want)
				writer.open("voicing");
			else
				writer.close();
			m->logging.store(want);
		}
		writer.drain(m->noteLog, VOICING_PARAM_NAMES,
			(int) (sizeof(VOICING_PARAM_NAMES) / sizeof(VOICING_PARAM_NAMES[0])));
		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(VoicingModule::I_MPX)) {
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

		if (PortWidget* out = getOutput(VoicingModule::O_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(out)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}
};

} // namespace px


Model* modelMpxVoicing = createModel<px::VoicingModule, px::VoicingWidget>("mpxVoicing");
