/** mpxPattern — a rhythm written as text.

WHAT IT IS. Three typed rows: where the notes fall, how strong each is, and how high it aims. The
module chooses no pitches at all — it sends notes with a level, a length and a contour and no
pitch, which is what mpxMelody's rhythm input takes. The same rows over a different chart give the
same phrasing on different harmony, which is what makes a pattern a phrase rather than a part.

IT TAKES THE PLACE OF mpxPhrase, which generates a rhythm from the form. This is the same slot
with the rhythm written by hand.

EACH ROW CYCLES AT ITS OWN LENGTH. Eleven hits against seven strengths come back into step every
seventy-seven steps, so what is heard is longer than anything typed. See docs/pattern.md.

NO CLOCK. The beat, the bar and the metre are on the cable.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

namespace px {


/** As many steps as a row may hold. A sixteen-bar phrase of sixteenths is two hundred and
fifty-six, which is past what anybody types and comfortably inside what a panel can show. */
static const int MAX_STEPS = 256;

/** THE ROWS, in the order they are typed and saved. */
enum PatternRow { ROW_HITS, ROW_STRENGTH, ROW_CONTOUR, NUM_ROWS };

static const char* ROW_KEYS[NUM_ROWS] = {"hits", "strengths", "contour"};

/** WHAT A CHARACTER IS WORTH, as a fraction of a beat. A bar is nought: whatever the metre says
one is, which cannot be a number here because a chart may change metre while it plays. */
enum StepRate { STEP_BAR, STEP_HALF, STEP_BEAT, STEP_EIGHTH, STEP_TRIPLET, STEP_SIXTEENTH,
	NUM_RATES };
static const char* RATE_NAMES[NUM_RATES] = {"A bar", "Two beats", "A beat", "An eighth",
	"A triplet eighth", "A sixteenth"};
static const float RATE_BEATS[NUM_RATES] = {0.f, 2.f, 1.f, 0.5f, 1.f / 3.f, 0.25f};

/** Where the contour comes from. */
enum ContourSource { CONTOUR_TYPED, CONTOUR_LIVE, CONTOUR_HELD, NUM_CONTOUR_WAYS };
static const char* CONTOUR_NAMES[NUM_CONTOUR_WAYS] = {"Typed", "From the jack", "Captured"};


/** ONE READING OF THE THREE ROWS, as the engine needs them: a number per step rather than a
character, and a length each. Compiled on the main thread when the text changes and handed to the
audio thread whole, so the engine never reads a string that is being edited. */
struct CompiledRows {
	/** Nought is a rest; one is a stroke; two to nine are that many strokes in the one slot. */
	uint8_t hit[MAX_STEPS] = {0};
	/** Nought to nine. */
	uint8_t strength[MAX_STEPS] = {0};
	/** Nought to nine, or ten meaning "carry on from the last". */
	uint8_t contour[MAX_STEPS] = {0};
	int hits = 0, strengths = 0, contours = 0;
};


/** A BAR LINE AND A SPACE ARE LAYOUT. They are what makes a long row readable and they mean
nothing to the pattern, so they are taken out before it is read. */
static bool isLayout(char c) {
	return c == '|' || c == ' ' || c == '\t' || c == '\n' || c == '\r';
}


struct PatternModule : Module, NoteSource, NoteSink {
	enum ParamId {
		P_RATE,
		P_GATE,
		P_LEVEL,
		P_HUMAN,
		P_CONTOUR_WAY,
		P_CAPTURE,
		NUM_PARAMS
	};
	enum InputId { I_CHART, I_CONTOUR, NUM_INPUTS };
	enum OutputId { O_NOTES, NUM_OUTPUTS };
	enum LightId { L_STEP, NUM_LIGHTS };

	PatternModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(P_RATE, 0.f, (float) (NUM_RATES - 1), (float) STEP_EIGHTH, "A character is",
			{RATE_NAMES[0], RATE_NAMES[1], RATE_NAMES[2], RATE_NAMES[3], RATE_NAMES[4],
			RATE_NAMES[5]});
		configParam(P_GATE, 0.05f, 1.5f, 0.9f, "Note length", "% of a step", 0.f, 100.f);
		configParam(P_LEVEL, 0.f, 1.f, 0.8f, "Level", "%", 0.f, 100.f);
		configParam(P_HUMAN, 0.f, 1.f, 0.f, "Humanise", "%", 0.f, 100.f);
		configSwitch(P_CONTOUR_WAY, 0.f, (float) (NUM_CONTOUR_WAYS - 1), 0.f, "Contour from",
			{CONTOUR_NAMES[0], CONTOUR_NAMES[1], CONTOUR_NAMES[2]});
		configButton(P_CAPTURE, "Capture the contour");

		configInput(I_CHART, "MPX chart");
		// A CONTOUR NEED NOT BE TYPED. Anything that moves slowly — a wave, a wandering voltage,
		// a fractal one — can be sampled at each step and used as the shape of the line, which is
		// how a contour is found rather than written. Captured, a pass of it is kept and repeats.
		configInput(I_CONTOUR, "Contour CV");
		configOutput(O_NOTES, "MPX note");

		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		slot = busClaim(&generation);
		setRow(ROW_HITS, "x..x..x.|x..x..x.");
		setRow(ROW_STRENGTH, "9535");
		setRow(ROW_CONTOUR, "4567654321234567");
	}

	~PatternModule() {
		busRelease(slot);
	}

	int busSlotFor(int outputId, uint32_t* gen) override {
		if (outputId != O_NOTES)
			return -1;
		if (gen)
			*gen = generation;
		return slot;
	}

	bool isMPXInputId(int inputId) override {
		return inputId == I_CHART;
	}

	// ---- the cable ----

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

	// ---- the rows ----

	/** THE TEXT AS TYPED, which is what is shown and what is saved. Touched only by the main
	thread. */
	std::string text[NUM_ROWS];
	/** TWO READINGS OF IT, one being used and one being written. The audio thread reads whichever
	`live` names, so a row can be recompiled while the pattern plays without a lock and without a
	half-written row ever being read. */
	CompiledRows compiled[2];
	std::atomic<int> live{0};

	void setRow(int row, const std::string& s) {
		if (row < 0 || row >= NUM_ROWS)
			return;
		text[row] = s;
		recompile();
	}

	void recompile() {
		const int spare = 1 - live.load();
		CompiledRows& c = compiled[spare];
		c = CompiledRows();
		for (int row = 0; row < NUM_ROWS; row++) {
			uint8_t* into = (row == ROW_HITS) ? c.hit
				: (row == ROW_STRENGTH) ? c.strength : c.contour;
			int n = 0;
			for (char ch : text[row]) {
				if (isLayout(ch) || n >= MAX_STEPS)
					continue;
				uint8_t v = 0;
				if (ch >= '0' && ch <= '9')
					v = (uint8_t) (ch - '0');
				else if (ch == 'x' || ch == 'X')
					v = (row == ROW_HITS) ? 1 : 0;
				else if (ch == '.' || ch == '-')
					v = (row == ROW_CONTOUR) ? 10 : 0;   // a contour holds; the others rest
				else
					continue;
				into[n++] = v;
			}
			if (row == ROW_HITS)
				c.hits = n;
			else if (row == ROW_STRENGTH)
				c.strengths = n;
			else
				c.contours = n;
		}
		live.store(spare);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		for (int r = 0; r < NUM_ROWS; r++)
			json_object_set_new(rootJ, ROW_KEYS[r], json_string(text[r].c_str()));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		for (int r = 0; r < NUM_ROWS; r++) {
			if (const char* s = json_string_value(json_object_get(rootJ, ROW_KEYS[r])))
				text[r] = s;
		}
		recompile();
	}

	// ---- playing ----

	/** Where the last step fell, in beats, so a step is played once. */
	double playedTo = -1e9;
	int64_t stepIndex = 0;
	/** The notes this module has sounding, each ended when its length is up. */
	struct Sounding {
		int64_t handle = 0;
		float endIn = -1.f;
	};
	Sounding sounding[16];
	float stepLight = 0.f;
	/** THE METRE, AS THE CHART GIVES IT. The typing grid draws a bar line every so many cells,
	and how many that is depends on how long a bar is — which is the chart's to say, and may
	change while it plays. Read by the panel, written by the engine. */
	std::atomic<int> barBeats{4};
	uint32_t noise = 0x2545f491u;
	/** The contour captured from the jack, a step at a time, and how much of it is filled. */
	uint8_t caught[MAX_STEPS] = {0};
	int caughtLen = 0;
	bool catching = false;
	bool captureWas = false;

	float dice() {
		noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
		return (float) (noise >> 8) / 16777216.f;
	}

	void strike(float level, float seconds, float contour) {
		if (slot < 0)
			return;
		int place = 0;
		for (int i = 0; i < 16; i++) {
			if (sounding[i].endIn < 0.f) {
				place = i;
				break;
			}
		}
		Sounding& s = sounding[place];
		if (s.endIn >= 0.f && s.handle != 0) {
			Event off;
			off.kind = Event::OFF;
			off.handle = s.handle;
			busPush(slot, off);
		}
		Event e;
		e.kind = Event::ON;
		e.handle = s.handle = mintHandle();
		// NO PITCH. The note is a place in time with a weight and a shape; mpxMelody fills the
		// pitch in from the harmony.
		e.pitch = 0.f;
		e.level = math::clamp(level, 0.f, 1.f);
		e.duration = math::clamp(seconds, 0.005f, 30.f);
		e.contour = contour;
		busPush(slot, e);
		s.endIn = seconds;
		stepLight = 1.f;
	}

	void endDue(float dt) {
		for (int i = 0; i < 16; i++) {
			if (sounding[i].endIn < 0.f)
				continue;
			sounding[i].endIn -= dt;
			if (sounding[i].endIn <= 0.f) {
				if (slot >= 0 && sounding[i].handle != 0) {
					Event off;
					off.kind = Event::OFF;
					off.handle = sounding[i].handle;
					busPush(slot, off);
				}
				sounding[i].endIn = -1.f;
				sounding[i].handle = 0;
			}
		}
	}

	void process(const ProcessArgs& args) override {
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}

		outputs[O_NOTES].setChannels(1);
		outputs[O_NOTES].setVoltage(busFlashVolts(slot));

		// THE CHART PASSES THROUGH, so the module can sit in the chain rather than beside it:
		// what it publishes is the chart's own state plus the notes it writes.
		Event in;
		while (reader.next(in))
			busPush(slot, in);
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

		// The capture button: pressing it starts a pass, which fills as the pattern plays.
		const bool pressed = params[P_CAPTURE].getValue() > 0.5f;
		if (pressed && !captureWas) {
			catching = true;
			caughtLen = 0;
		}
		captureWas = pressed;

		const CompiledRows& rows = compiled[live.load()];
		if (haveHarmony && h.valid && rows.hits > 0) {
			const int rate = (int) std::round(params[P_RATE].getValue());
			const int beatsInBar = std::max(1, (int) h.barBeats);
			barBeats.store(beatsInBar);
			const float stepBeats = (RATE_BEATS[rate] > 0.f)
				? RATE_BEATS[rate] : (float) beatsInBar;
			const double pos = h.beat / (double) stepBeats;
			const int64_t step = (int64_t) std::floor(pos);
			// HOW LONG A BEAT IS, followed from the chart's own beat rather than from a tempo
			// knob: a gate and a ratchet are then in time whatever the tempo is doing, including
			// while it changes.
			clock += args.sampleTime;
			if (lastBeat >= 0.0 && h.beat > lastBeat && h.beat - lastBeat < 1.0) {
				const double seconds = clock - lastBeatTime;
				const double beats = h.beat - lastBeat;
				if (seconds > 0.0 && beats > 0.0) {
					const float now = (float) (seconds / beats);
					// Followed rather than jumped to, so one odd frame does not shorten a note.
					secondsPerBeat += (math::clamp(now, 0.02f, 4.f) - secondsPerBeat) * 0.05f;
				}
			}
			lastBeat = h.beat;
			lastBeatTime = clock;

			// EVERY STEP THE BEAT HAS CROSSED, and only once each.
			if (step != stepIndex) {
				stepIndex = step;
				playStep(step, h, stepBeats, args.sampleRate);
			}
		}

		pendingTick(args.sampleTime);
		endDue(args.sampleTime);
		stepLight = std::fmax(0.f, stepLight - args.sampleTime * 6.f);
		lights[L_STEP].setBrightness(stepLight);
	}

	void playStep(int64_t step, const Harmony& h, float stepBeats, float sampleRate) {
		(void) h;
		const CompiledRows& rows = compiled[live.load()];
		if (rows.hits <= 0)
			return;
		const int64_t at = ((step % rows.hits) + rows.hits) % rows.hits;
		const int count = rows.hit[at];

		// THE CONTOUR, from wherever it is coming from. Typed is the row; from the jack is the
		// voltage now; captured is a pass of that voltage kept and repeated.
		const int way = (int) std::round(params[P_CONTOUR_WAY].getValue());
		uint8_t contourDigit = 10;
		if (way == CONTOUR_TYPED && rows.contours > 0) {
			contourDigit = rows.contour[((step % rows.contours) + rows.contours) % rows.contours];
		}
		else if (way != CONTOUR_TYPED) {
			const float volts = inputs[I_CONTOUR].getVoltage();
			const uint8_t digit = (uint8_t) math::clamp((int) std::lround(volts * 0.9f), 0, 9);
			if (way == CONTOUR_LIVE) {
				contourDigit = digit;
			}
			else {
				if (catching && caughtLen < MAX_STEPS) {
					caught[caughtLen++] = digit;
					contourDigit = digit;
				}
				else if (caughtLen > 0) {
					contourDigit = caught[((step % caughtLen) + caughtLen) % caughtLen];
				}
			}
		}
		if (contourDigit <= 9)
			lastContour = contourDigit;
		const float contour = (lastContour <= 9) ? (float) lastContour / 9.f : -1.f;

		if (count <= 0)
			return;

		// THE STRENGTH IS THE LEVEL AND THE WEIGHT. Nought is a rest, so either row can thin the
		// pattern; a row shorter than the hits floats against it, which is the point of it.
		const int strength = (rows.strengths > 0)
			? rows.strength[((step % rows.strengths) + rows.strengths) % rows.strengths] : 9;
		if (strength <= 0)
			return;

		const float human = params[P_HUMAN].getValue();
		const float level = math::clamp((float) strength / 9.f * params[P_LEVEL].getValue()
			* (1.f - dice() * human * 0.3f), 0.f, 1.f);

		// How long a step lasts, from the beat the chart is keeping.
		const float stepSeconds = secondsPerBeat * stepBeats;
		const float length = params[P_GATE].getValue() * stepSeconds;

		// A RATCHET IS THAT MANY STROKES IN THE SLOT: they are queued a fraction of the step
		// apart, which is what a roll or a drag is.
		for (int i = 0; i < count && i < 8; i++) {
			if (i == 0) {
				strike(level, length / (float) count, contour);
			}
			else {
				// The later strokes of a ratchet are queued by time rather than sent now.
				Pending& p = pending[pendingCount % 16];
				p.in = stepSeconds * (float) i / (float) count;
				p.level = level * (0.75f + 0.25f * (float) i / (float) count);
				p.length = length / (float) count;
				p.contour = contour;
				p.waiting = true;
				pendingCount++;
			}
		}
		(void) sampleRate;
	}

	/** A stroke of a ratchet that has not fallen yet. */
	struct Pending {
		bool waiting = false;
		float in = 0.f;
		float level = 0.f;
		float length = 0.f;
		float contour = -1.f;
	};
	Pending pending[16];
	int pendingCount = 0;
	uint8_t lastContour = 10;
	/** Seconds in a beat, followed from the chart's own beat so a ratchet and a gate are in time
	whatever the tempo is doing. */
	float secondsPerBeat = 0.5f;
	double lastBeat = -1.0;
	double lastBeatTime = 0.0;
	double clock = 0.0;

	void pendingTick(float dt) {
		for (int i = 0; i < 16; i++) {
			if (!pending[i].waiting)
				continue;
			pending[i].in -= dt;
			if (pending[i].in <= 0.f) {
				pending[i].waiting = false;
				strike(pending[i].level, pending[i].length, pending[i].contour);
			}
		}
	}
};


static Layout patternLayout() {
	Layout L;
	L.hp = 24.f;
	L.title = "mpxPattern";

	static const float NAME_MM = 2.44f;
	static const float LAMP_MM = 6.5f / 2.9528f;

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

	auto col = [&](const std::string& key, float x, float y, int id, const std::string& group,
			const std::vector<std::string>& names) {
		const float step = 2.f * LAMP_MM + 1.f;
		const float h = 2.f * LAMP_MM + step * (float) (names.size() - 1);
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "lamps";
		i.x = x - LAMP_MM; i.y = y - h / 2.f; i.names = names; i.horizontal = false;
		i.pitch = step; i.labelSide = Panel::RIGHT;
		L.items.push_back(i);
		label(key + ".group", x, i.y - 0.5f - NAME_MM / 2.f, group, 0.f, key);
	};

	auto row = [&](const std::string& key, float y, const std::string& name) {
		Item i;
		i.key = key; i.kind = Item::DISPLAY; i.x = 6.f; i.y = y; i.w = 109.f; i.h = 9.f;
		L.items.push_back(i);
		label(key + ".label", 6.f, y - 2.2f, name, 8.f, key);
	};

	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor ring) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = ring;
		L.items.push_back(i);
		label(key + ".label", x, y + 6.99f, name, 7.f, key);
	};

	row("d.hits", 30.f, "HITS");
	row("d.strengths", 48.f, "STRENGTHS");
	row("d.contour", 66.f, "CONTOUR");

	col("p.rate", 12.f, 92.f, PatternModule::P_RATE, "A CHARACTER IS",
		{"BAR", "2 BEATS", "BEAT", "EIGHTH", "TRIPLET", "SIXTEENTH"});
	col("p.contour", 54.f, 92.f, PatternModule::P_CONTOUR_WAY, "CONTOUR FROM",
		{"TYPED", "JACK", "CAPTURED"});

	knob("p.gate", 86.f, 86.f, PatternModule::P_GATE, "LENGTH");
	knob("p.level", 102.f, 86.f, PatternModule::P_LEVEL, "LEVEL");
	knob("p.human", 86.f, 104.f, PatternModule::P_HUMAN, "HUMAN");

	Item capture;
	capture.key = "p.capture"; capture.kind = Item::PARAM; capture.id = PatternModule::P_CAPTURE;
	capture.style = "button"; capture.diameter = 5.5f; capture.x = 102.f; capture.y = 104.f;
	L.items.push_back(capture);
	label("p.capture.label", 102.f, 110.f, "CAPTURE", 0.f, "p.capture");

	jack("in.chart", Item::PORT_IN, 12.f, 118.f, PatternModule::I_CHART, "mpx\nIN", NOTE_CABLE);
	jack("in.contour", Item::PORT_IN, 30.f, 118.f, PatternModule::I_CONTOUR, "contour", SIG_CV);
	jack("out.notes", Item::PORT_OUT, 110.f, 118.f, PatternModule::O_NOTES, "mpx\nOUT",
		NOTE_CABLE);

	Item lamp;
	lamp.key = "lamp.step"; lamp.kind = Item::LIGHT; lamp.id = PatternModule::L_STEP;
	lamp.x = 60.f; lamp.y = 118.f;
	L.items.push_back(lamp);
	// THE LABELS ARE TIED TO WHAT THEY NAME. Each one's offset from its control is taken from
	// the positions above, so that moving a control in the panel editor takes its name with it.
	// Without this every offset is nought, and the first layout anybody saves puts every name
	// underneath the control it belongs to, where it cannot be seen.
	L.bindOffsets();
	return L;
}


/** ONE ROW, TYPED ON A GRID.

A BAR LINE IS NOT A CHARACTER OF THE PATTERN. The value is cells; the bar lines are drawn in every
so many cells and never move. Typing OVERWRITES the cell the caret is on and steps to the next
one, so a row keeps its shape while it is edited and a bar stays where the eye left it. Typing in
the gap past the last cell appends a whole measure, its first cell the character typed and the
rest rests. Backspace and Delete clear a cell to a rest, or remove the last measure when the caret
is at the end.

This is how the same field works in GXW — see src/inspectorFields.js there — and the behaviour is
worth copying exactly: anything that inserts rather than overwrites pushes every later beat off
its place in the bar.

DARK, LIKE THE PANEL. Rack's own text field is a white slab, which on a dark panel is a lamp in
the face of anybody reading it. This is Rack's display field instead: unlit ground, lit lettering,
monospaced so a character sits over the step it names, with a thin frame to say where it is. */
struct RowField : LedDisplayTextField {
	PatternModule* module = NULL;
	int row = 0;

	RowField() {
		multiline = false;
		bgColor = nvgRGB(0x0d, 0x10, 0x14);
		color = nvgRGB(0x3d, 0xe0, 0x7a);
		textOffset = math::Vec(3.f, 2.f);
	}

	/** How many cells a bar holds: how long a bar is, divided by what a character is worth. The
	length of a bar comes from the chart on the cable, so a piece in three or in five is drawn in
	threes or fives without anything being set here. Four until a chart says otherwise. */
	int cellsPerBar() {
		if (!module)
			return 4;
		const int rate = (int) std::round(module->params[PatternModule::P_RATE].getValue());
		if (RATE_BEATS[rate] <= 0.f)
			return 1;   // a character is a whole bar
		const int beats = math::clamp(module->barBeats.load(), 1, 32);
		return math::clamp((int) std::lround((float) beats / RATE_BEATS[rate]), 1, 64);
	}

	/** Whether this row takes a character at all, and what it becomes. Nought means no. */
	char cellFor(int codepoint) {
		const char c = (char) codepoint;
		if (row == ROW_HITS) {
			if (c == 'x' || c == 'X' || c == ',')
				return 'x';
			if (c == '.' || c == ' ')
				return '.';
			if (c >= '2' && c <= '9')
				return c;   // a ratchet
			return 0;
		}
		if (row == ROW_STRENGTH)
			return (c >= '0' && c <= '9') ? c : 0;
		// The contour takes a digit, and a full stop meaning carry on from the last.
		if (c >= '0' && c <= '9')
			return c;
		if (c == '.' || c == ' ')
			return '.';
		return 0;
	}

	static std::string cellsOf(const std::string& shown) {
		std::string out;
		for (char c : shown) {
			if (c != '|')
				out += c;
		}
		return out;
	}

	std::string barize(const std::string& cells) {
		const int cpb = cellsPerBar();
		std::string out;
		for (size_t i = 0; i < cells.size(); i++) {
			out += cells[i];
			if ((int) ((i + 1) % (size_t) cpb) == 0)
				out += '|';
		}
		return out;
	}

	/** Where the caret is, counted in cells rather than in characters shown. */
	int caretCell() {
		const int at = math::clamp(cursor, 0, (int) text.size());
		int cell = 0;
		for (int i = 0; i < at && i < (int) text.size(); i++) {
			if (text[i] != '|')
				cell++;
		}
		return cell;
	}

	/** Writes the row back and puts the caret on a cell. `afterBar` lands after a trailing bar
	line, which is what a delete wants so the next one removes another measure. */
	void put(const std::string& cells, int cell, bool afterBar = false) {
		text = barize(cells);
		int at = (int) text.size();
		if (cell >= (int) cells.size()) {
			// Past the last cell: before the trailing bar line, so typing appends a measure.
			if (!afterBar && !text.empty() && text[text.size() - 1] == '|')
				at = (int) text.size() - 1;
		}
		else {
			int count = 0;
			for (int i = 0; i <= (int) text.size(); i++) {
				if (count == cell) {
					at = i;
					break;
				}
				if (i < (int) text.size() && text[i] != '|')
					count++;
			}
		}
		cursor = selection = at;
		if (module)
			module->setRow(row, text);
	}

	void onSelectText(const SelectTextEvent& e) override {
		const char c = cellFor(e.codepoint);
		if (!c) {
			e.consume(this);
			return;
		}
		std::string cells = cellsOf(text);
		int cell = caretCell();
		if (cell >= (int) cells.size()) {
			// A NEW MEASURE, rests after the character typed, so the grid stays whole.
			const int cpb = cellsPerBar();
			cells += c;
			cells.append((size_t) (cpb - 1), '.');
			cell = (int) cells.size() - cpb + 1;
		}
		else {
			cells[cell] = c;
			cell++;
		}
		put(cells, cell);
		e.consume(this);
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action != GLFW_PRESS && e.action != GLFW_REPEAT) {
			LedDisplayTextField::onSelectKey(e);
			return;
		}
		std::string cells = cellsOf(text);
		const int cpb = cellsPerBar();
		int cell = caretCell();
		const bool atEnd = cell >= (int) cells.size();
		switch (e.key) {
			case GLFW_KEY_BACKSPACE:
				if (atEnd && (int) cells.size() >= cpb) {
					cells.erase(cells.size() - cpb);
					put(cells, (int) cells.size(), true);
				}
				else if (cell > 0) {
					cells[cell - 1] = '.';
					put(cells, cell - 1);
				}
				e.consume(this);
				return;
			case GLFW_KEY_DELETE:
				if (atEnd && (int) cells.size() >= cpb) {
					cells.erase(cells.size() - cpb);
					put(cells, (int) cells.size(), true);
				}
				else if (cell < (int) cells.size()) {
					cells[cell] = '.';
					put(cells, cell);
				}
				e.consume(this);
				return;
			case GLFW_KEY_LEFT:
				put(cells, std::max(0, cell - 1));
				e.consume(this);
				return;
			case GLFW_KEY_RIGHT:
				put(cells, std::min((int) cells.size(), cell + 1));
				e.consume(this);
				return;
			case GLFW_KEY_HOME:
				put(cells, 0);
				e.consume(this);
				return;
			case GLFW_KEY_END:
				put(cells, (int) cells.size());
				e.consume(this);
				return;
			case GLFW_KEY_ENTER:
			case GLFW_KEY_KP_ENTER:
				// Nothing to commit: every keystroke has already been applied.
				e.consume(this);
				return;
			default:
				break;
		}
		LedDisplayTextField::onSelectKey(e);
	}

	void draw(const DrawArgs& args) override {
		LedDisplayTextField::draw(args);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 2.f);
		nvgStrokeColor(args.vg, nvgRGBA(0x3d, 0xd6, 0x8c, 0x88));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
	}

	void step() override {
		// The text belongs to the module, so a patch loaded from a file shows what it holds.
		const bool typing = (APP->event->getSelectedWidget() == this);
		if (module && !typing) {
			const std::string want = barize(cellsOf(module->text[row]));
			if (text != want)
				text = want;
		}
		LedDisplayTextField::step();
	}
};


struct PatternWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	PatternWidget(PatternModule* module) {
		setModule(module);
		layout = patternLayout();
		layoutApplyUser("mpxPattern", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);

		static const char* KEYS[NUM_ROWS] = {"d.hits", "d.strengths", "d.contour"};
		for (int r = 0; r < NUM_ROWS; r++) {
			RowField* field = new RowField;
			field->module = module;
			field->row = r;
			if (module)
				field->text = module->text[r];
			layoutPlaceDisplay(this, layout, KEYS[r], field);
		}
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxPattern");
	}

	void step() override {
		ModuleWidget::step();
		PatternModule* m = dynamic_cast<PatternModule*>(module);
		if (!m)
			return;
		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(PatternModule::I_CHART)) {
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

		if (PortWidget* out = getOutput(PatternModule::O_NOTES)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(out)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}
};

} // namespace px


Model* modelMpxPattern = createModel<px::PatternModule, px::PatternWidget>("mpxPattern");
