/** mpxGroove — the rhythm, for everything that plays it.

WHAT IT IS FOR. A rate is a metronome: a step every so many beats, every bar the same. No player
plays that, and two players each keeping their own rate do not lock together — they merely run at
the same speed. This module holds the rhythm itself, in styles, and offers it to the accompaniment
and to the line alike, so that a comp, a guitar and a solo are playing the same groove.

IT KEEPS NO TIME OF ITS OWN. The beat, the bar, the metre and the phrase are on the cable already.
This says what happens inside them and nothing about how fast they go.

THIS IS THE PANEL AND THE CONTROLS. What the styles are, the drums it plays from them and the
rhythm it publishes come next, in the stages set out in docs/players.md.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"
#include "NoteLog.hpp"
#include "GrooveLib.hpp"

#include <cmath>

namespace px {


/** THE STYLES. Each is a set of hits within a bar, at particular positions and with particular
weights — not a division of the bar into equal parts, which is what a rate is. */
static const char* STYLE_NAMES[] = {"Swing", "Bossa", "Funk", "Rock", "Ballad", "Montuno",
	"Skank"};
static const int NUM_STYLES = (int) (sizeof(STYLE_NAMES) / sizeof(STYLE_NAMES[0]));

/** WHERE THE PLAYING SITS AGAINST THE BEAT. Dead on it is a machine; a little ahead is urgent and
a little behind is relaxed, and which of the three a band is doing is most of what a listener
means by its feel. */
static const char* PUSH_NAMES[] = {"Behind", "On the beat", "Ahead"};
static const int NUM_PUSHES = (int) (sizeof(PUSH_NAMES) / sizeof(PUSH_NAMES[0]));

/** WHAT THE END OF A PHRASE DOES. The chart says where a phrase ends; this says whether anything
happens there. */
static const char* FILL_NAMES[] = {"None", "Fill", "Turnaround"};
static const int NUM_FILLS = (int) (sizeof(FILL_NAMES) / sizeof(FILL_NAMES[0]));


/** THE CONTROL SHOWS THE GROOVE'S NAME rather than its number: a plate reading "Latin: Bossa
nova" says what is playing, where a number says nothing at all. The list a click opens is built
from the same names. */
struct GrooveQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		return grooveAt((int) std::round(getValue())).label();
	}
};


struct GrooveModule : Module, NoteSource, NoteSink {
	enum ParamId {
		P_GROOVE,
		P_SWING,
		P_PUSH,
		P_DENSITY,
		P_VARIATION,
		P_FILL,
		P_HUMAN,
		P_LEVEL,
		P_DRUMS,
		P_RECORD,
		NUM_PARAMS
	};
	enum InputId { I_MPX, NUM_INPUTS };
	enum OutputId { O_MPX, NUM_OUTPUTS };
	enum LightId { L_BEAT, NUM_LIGHTS };

	GrooveModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// THE LIBRARY IS A FOLDER, not a list in the source, so the range depends on what was
		// found — see GrooveLib.hpp. The quantity below turns the number into the groove's name.
		configParam<GrooveQuantity>(P_GROOVE, 0.f, (float) std::max(0, grooveCount() - 1), 0.f,
			"Groove");
		getParamQuantity(P_GROOVE)->snapEnabled = true;
		// NOUGHT IS STRAIGHT AND ONE IS TRIPLET, and the useful settings are between: a jazz
		// eighth is nearer two thirds than three quarters, and it loosens as the tempo rises.
		configParam(P_SWING, 0.f, 1.f, 0.f, "Swing", "%", 0.f, 100.f);
		configSwitch(P_PUSH, 0.f, (float) (NUM_PUSHES - 1), 1.f, "Feel",
			{PUSH_NAMES[0], PUSH_NAMES[1], PUSH_NAMES[2]});
		configParam(P_DENSITY, 0.f, 1.f, 0.5f, "Density", "%", 0.f, 100.f);
		configParam(P_VARIATION, 0.f, 1.f, 0.3f, "Variation", "%", 0.f, 100.f);
		configSwitch(P_FILL, 0.f, (float) (NUM_FILLS - 1), 1.f, "Phrase end",
			{FILL_NAMES[0], FILL_NAMES[1], FILL_NAMES[2]});
		configParam(P_HUMAN, 0.f, 1.f, 0.2f, "Humanise", "%", 0.f, 100.f);
		configParam(P_LEVEL, 0.f, 1.f, 0.7f, "Level", "%", 0.f, 100.f);
		// THE DRUMS ARE A SWITCH RATHER THAN A SECOND MODULE: the hits and their weights are
		// here, so a kit is what this already knows. Off, it is a rhythm nobody hears directly.
		configSwitch(P_DRUMS, 0.f, 1.f, 1.f, "Drums", {"Silent", "Playing"});
		configSwitch(P_RECORD, 0.f, 1.f, 0.f, "Record what it plays", {"Off", "Recording"});

		configInput(I_MPX, "MPX note");
		configOutput(O_MPX, "MPX note");

		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		slot = busClaim(&generation);
	}

	~GrooveModule() {
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

	NoteLog noteLog;
	std::atomic<bool> logging{false};
	SettingsWatcher settingsWatch;
	double logSeconds = 0.0;
	float beatLight = 0.f;
	double lastBeat = -1.0;

	// ---- playing the pattern ----

	/** Where the bar had got to when it was last looked at, so a hit is played once, when the
	beat crosses it. */
	double playedTo = -1.0;
	/** Which bar of the tune the last hit belonged to, so a rewind does not replay it. */
	int64_t barIndex = -1;
	/** The sounding drum strokes, each ended after a moment: a drum is a sound with no length
	worth speaking of, and a sampler downstream decides how long it rings. */
	struct Stroke {
		int64_t handle = 0;
		float endIn = -1.f;
	};
	Stroke strokes[16];
	uint32_t noise = 0x1234567u;

	float dice() {
		noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
		return (float) (noise >> 8) / 16777216.f;
	}

	/** A hit's own random number, the same every time that hit comes round in that bar: a pattern
	that thins differently on each pass is not a pattern. */
	static float hitDice(int64_t bar, int index) {
		uint32_t x = (uint32_t) (bar * 2654435761u) ^ (uint32_t) (index * 40503u);
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		return (float) (x >> 8) / 16777216.f;
	}

	void strike(float pitch, float level, float seconds) {
		if (slot < 0)
			return;
		int place = 0;
		for (int i = 0; i < 16; i++) {
			if (strokes[i].endIn < 0.f) {
				place = i;
				break;
			}
		}
		Stroke& st = strokes[place];
		if (st.endIn >= 0.f && st.handle != 0) {
			Event off;
			off.kind = Event::OFF;
			off.handle = st.handle;
			busPush(slot, off);
		}
		Event e;
		e.kind = Event::ON;
		e.handle = st.handle = mintHandle();
		e.pitch = pitch;
		e.level = math::clamp(level, 0.f, 1.f);
		e.duration = seconds;
		busPush(slot, e);
		st.endIn = seconds;
		writeToLog(e);
	}

	void strokeTick(float dt) {
		for (int i = 0; i < 16; i++) {
			if (strokes[i].endIn < 0.f)
				continue;
			strokes[i].endIn -= dt;
			if (strokes[i].endIn <= 0.f) {
				if (slot >= 0 && strokes[i].handle != 0) {
					Event off;
					off.kind = Event::OFF;
					off.handle = strokes[i].handle;
					busPush(slot, off);
				}
				strokes[i].endIn = -1.f;
				strokes[i].handle = 0;
			}
		}
	}

	void writeToLog(const Event& e) {
		if (!logging.load(std::memory_order_relaxed))
			return;
		LogNote n;
		n.seconds = logSeconds;
		n.pitch = e.pitch;
		n.level = e.level;
		n.duration = e.duration;
		Harmony h;
		if (reader.harmony(h) && h.valid) {
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

	/** WHERE A HIT ACTUALLY FALLS, once the feel has had its say: an offbeat eighth delayed by the
	swing, the whole bar nudged early or late, and a little of neither on purpose. */
	float placeHit(float beat, float swing, int push, float human) {
		float at = beat;
		// SWING DELAYS THE SECOND EIGHTH of each beat and leaves the first where it is. One is
		// the triplet, which puts it two thirds of the way through the beat.
		const float frac = at - std::floor(at);
		if (swing > 0.f && std::fabs(frac - 0.5f) < 0.01f)
			at = std::floor(at) + 0.5f + swing * (2.f / 3.f - 0.5f);
		// AHEAD AND BEHIND ARE SMALL: a sixteenth of a beat either way is the difference between
		// urgent and relaxed, and more than that is simply early or late.
		if (push == 0)
			at += 0.06f;
		else if (push == 2)
			at -= 0.06f;
		at += (dice() - 0.5f) * human * 0.08f;
		return at;
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

		// EVERYTHING PASSES THROUGH, as it does in every module that sits in the middle of a
		// chain: the events, the harmony, the voicing's own state and the pedals are the
		// upstream's. What this adds comes in the next stage.
		outputs[O_MPX].setChannels(1);
		outputs[O_MPX].setVoltage(0.f);
		Event e;
		while (reader.next(e))
			busPush(slot, e);
		Harmony h;
		const bool haveHarmony = reader.harmony(h);
		if (haveHarmony)
			busPublishHarmony(slot, h);
		ChordVoicing v;
		if (reader.voicing(v))
			busPublishVoicing(slot, v);
		{
			float sustain = 0.f, soft = 0.f;
			reader.pedals(sustain, soft);
			busPublishPedals(slot, sustain, soft);
		}

		// ---- the pattern ----
		//
		// EVERY HIT WHOSE PLACE THE BEAT HAS JUST CROSSED. The chart's beat is the clock, so a
		// tempo change, a rewind or a pause need no handling of their own: the bar position says
		// where we are and this plays whatever has fallen due since it was last looked at.
		if (haveHarmony && h.valid) {
			const int barBeats = std::max(1, (int) h.barBeats);
			const int64_t bar = (int64_t) std::floor(h.beat / (double) barBeats);
			const double inBar = h.beat - (double) bar * barBeats;
			// A jump — the chart rewound, or the patch has just started — begins here rather than
			// playing every hit in between.
			if (bar != barIndex || inBar < playedTo - 0.001) {
				barIndex = bar;
				playedTo = inBar;
			}

			const int which = (int) std::round(params[P_GROOVE].getValue());
			const Groove& groove = grooveAt(which);
			const float swing = params[P_SWING].getValue();
			const int push = (int) std::round(params[P_PUSH].getValue());
			const float density = params[P_DENSITY].getValue();
			const float variation = params[P_VARIATION].getValue();
			const float human = params[P_HUMAN].getValue();
			const float level = params[P_LEVEL].getValue();
			const bool drums = params[P_DRUMS].getValue() > 0.5f;
			const int fillWay = (int) std::round(params[P_FILL].getValue());

			// WHICH BAR OF THE GROOVE THIS IS. A groove of two or four bars is what stops one
			// sounding like a loop: the bar of the tune decides which of them is playing.
			const int within = (int) (((bar % groove.bars) + groove.bars) % groove.bars);
			// The groove is written in its own metre and stretched to the chart's.
			const float stretch = (float) barBeats / (float) std::max(1, groove.beats);

			// THE LAST BAR OF A PHRASE, which the chart says by how long its cycle is. A fill
			// belongs there and nowhere else.
			const bool lastBar = (h.cycleBeats > 0.f)
				&& (h.beat >= (double) h.cycleBeats - barBeats - 0.001);
			const bool filling = lastBar && fillWay != 0 && !groove.fill.empty();

			const std::vector<GrooveHit>& hits = filling ? groove.fill : groove.hits;
			for (size_t i = 0; i < hits.size(); i++) {
				const GrooveHit& hit = hits[i];
				if (!filling && hit.bar != within)
					continue;
				const float at = placeHit(hit.beat * stretch, swing, push, human);
				if (at <= playedTo || at > inBar)
					continue;
				// DENSITY KEEPS THE HEAVY STROKES. A groove thinned from the light end still
				// sounds like itself; thinned at random it does not.
				if (hit.weight < 1.f - density * 1.2f)
					continue;
				// AND VARIATION DROPS A FEW MORE, differently each bar but the same on every
				// pass through that bar, so a bar is a bar rather than a shuffle.
				if (variation > 0.f
					&& hitDice(bar, (int) i) < variation * 0.35f * (1.f - hit.weight))
					continue;
				if (!drums)
					continue;
				const float lv = math::clamp(hit.weight * level
					* (1.f - dice() * human * 0.3f), 0.f, 1.f);
				strike(GROOVE_PITCH[hit.part], lv, 0.12f);
			}
			playedTo = inBar;
		}
		strokeTick(args.sampleTime);

		// THE LAMP IS THE BEAT, so a patch that is not yet playing anything still says whether
		// the module is being told the time.
		if (haveHarmony && h.valid) {
			if (std::floor(h.beat) != std::floor(lastBeat))
				beatLight = 1.f;
			lastBeat = h.beat;
		}
		beatLight = std::fmax(0.f, beatLight - args.sampleTime * 6.f);
		lights[L_BEAT].setBrightness(beatLight);
	}
};


static Layout grooveLayout() {
	Layout L;
	L.hp = 14.f;
	L.title = "mpxGroove";

	static const float NAME_MM = 2.44f;
	static const float LAMP_MM = 6.5f / 2.9528f;

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			bool heading = true, float size = 0.f, const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = Panel::CENTRE; i.heading = heading; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};

	auto knob = [&](const std::string& key, float x, float y, int id, const std::string& name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.ticks = 2;
		L.items.push_back(i);
		label(key + ".label", x, y + 8.02f, name, true, 0.f, key);
	};

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
	layoutAddRecordButton(L, GrooveModule::P_RECORD);

	// THE GROOVE IS A PLATE, not a column: the library is a folder and grows, so the control
	// cannot be a lamp for every entry. It shows the name and a click opens the list.
	{
		Item i;
		i.key = "p.groove"; i.kind = Item::PARAM; i.id = GrooveModule::P_GROOVE;
		i.style = "readout"; i.x = 35.f; i.y = 26.f; i.chars = 18; i.h = NAME_MM;
		L.items.push_back(i);
		label("p.groove.group", 35.f, 26.f - (NAME_MM + 1.f) / 2.f - 1.f - NAME_MM / 2.f,
			"GROOVE", true, 0.f, "p.groove");
	}

	col("p.push", 38.f, 30.f, GrooveModule::P_PUSH, "FEEL",
		{"BEHIND", "ON BEAT", "AHEAD"});
	// NAMES THAT FIT THE PANEL. A name is drawn to the right of its lamp, so a long one on a
	// column near the right-hand edge is drawn off the module.
	col("p.fill", 38.f, 52.f, GrooveModule::P_FILL, "PHRASE END",
		{"NONE", "FILL", "TURN"});

	knob("p.swing", 12.f, 72.f, GrooveModule::P_SWING, "SWING");
	knob("p.density", 32.f, 72.f, GrooveModule::P_DENSITY, "DENSITY");
	knob("p.variation", 50.f, 72.f, GrooveModule::P_VARIATION, "VARIATION");

	knob("p.human", 12.f, 92.f, GrooveModule::P_HUMAN, "HUMAN");
	knob("p.level", 30.f, 92.f, GrooveModule::P_LEVEL, "LEVEL");
	col("p.drums", 48.f, 92.f, GrooveModule::P_DRUMS, "DRUMS", {"OFF", "ON"});

	jack("in.mpx", Item::PORT_IN, 12.f, 114.f, GrooveModule::I_MPX, "mpx\nIN");
	jack("out.mpx", Item::PORT_OUT, 59.f, 114.f, GrooveModule::O_MPX, "mpx\nOUT");

	Item lamp;
	lamp.key = "lamp.beat"; lamp.kind = Item::LIGHT; lamp.id = GrooveModule::L_BEAT;
	lamp.x = 35.5f; lamp.y = 114.f;
	L.items.push_back(lamp);
	// THE LABELS ARE TIED TO WHAT THEY NAME. Each one's offset from its control is taken from
	// the positions above, so that moving a control in the panel editor takes its name with it.
	// Without this every offset is nought, and the first layout anybody saves puts every name
	// underneath the control it belongs to, where it cannot be seen.
	L.bindOffsets();
	return L;
}


/** The controls in the order the module declares them, so a settings line reads as words. */
static const char* GROOVE_PARAM_NAMES[] = {
	"groove", "swing", "push", "density", "variation", "fill", "human", "level", "drums",
	"record",
};


struct GrooveWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;
	NoteLogWriter writer;

	GrooveWidget(GrooveModule* module) {
		setModule(module);
		layout = grooveLayout();
		layoutApplyUser("mpxGroove", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxGroove");
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(writer.where()));
	}

	void step() override {
		ModuleWidget::step();
		GrooveModule* m = dynamic_cast<GrooveModule*>(module);
		if (!m)
			return;

		const bool want = m->params[GrooveModule::P_RECORD].getValue() > 0.5f;
		if (want != writer.writing()) {
			if (want)
				writer.open("groove");
			else
				writer.close();
			m->logging.store(want);
		}
		writer.drain(m->noteLog, GROOVE_PARAM_NAMES,
			(int) (sizeof(GROOVE_PARAM_NAMES) / sizeof(GROOVE_PARAM_NAMES[0])));

		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(GrooveModule::I_MPX)) {
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

		if (PortWidget* out = getOutput(GrooveModule::O_MPX)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(out)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}
};

} // namespace px


Model* modelMpxGroove = createModel<px::GrooveModule, px::GrooveWidget>("mpxGroove");
