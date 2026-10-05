/** mpxPattern — rhythm written as text, eight patterns at a time.

WHAT IT IS. A grid of characters, one per beat, saying where the notes fall. A full stop is a
rest, an x is a beat, a digit from two to nine is that beat struck that many times. Bar lines are
drawn, not typed. The module chooses no pitches of its own: it sends notes with a level and a
length, which is what mpxMelody's rhythm input takes.

A CHARACTER IS ALWAYS ONE BEAT. It was once a settable note value — a bar, an eighth, a triplet —
and that is gone. Typing a bar of sixteenths as sixteen characters is tedious and it put bar lines
in places that depended on a control rather than on the metre. Faster rhythm comes from the cycle
speeds instead: the same grid of beats played at two is in eighths, at four in sixteenths. See
docs/pattern.md.

EIGHT TABS. Nearly everything on the panel belongs to one pattern, so one pattern's worth is shown
and the tabs say whose — the same arrangement as mpxSprites, and for the same reason. Every note
carries its tab number in `Event::part`, so a receiver that is itself one of several knows which
notes are its own.

AND A TAB IS A VOICE. The chord voicing on the cable carries up to eight voices, chosen once for
the whole chain, which is why there are eight tabs: tab three is voice three. With a voicing
present a tab sounds its voice's pitch and can drive a synth directly; without one it sends notes
with no pitch, as it always did.

NO CLOCK. The beat, the bar and the metre are on the cable.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace px {


/** EIGHT, BECAUSE THE VOICING HAS EIGHT. Not a round number picked for the panel: ChordVoicing
carries up to eight pitches, and a tab is one of them. */
static const int MAX_TABS = 8;

/** FOUR BARS TO A ROW, always. The grid is read by eye, and four bars is what the eye groups —
it is what the chord chart uses for its own rows, so a pattern laid against a chart lines up with
how the chart is written. A row is therefore never longer than four bars and a phrase never
wider than a row. */
static const int BARS_PER_ROW = 4;
/** As many rows as the box will ever hold: eight phrases in Manual, or a long chart in Harmony. */
static const int MAX_ROWS = 64;
/** A bar can be as long as a metre gets before the grid stops being readable. */
static const int MAX_CELLS_PER_BAR = 16;
static const int MAX_CELLS = BARS_PER_ROW * MAX_CELLS_PER_BAR;
/** How many numbers a cycle-speeds list may hold. */
static const int MAX_SPEEDS = 16;

/** WHERE THE PATTERN COMES FROM.

MANUAL: the rows are phrases, typed, laid end to end.
HARMONY: the rows are the CHART's bars, four to a row, so a tune can be given a different rhythm
in every bar of it. The speeds are locked to one, because the beats are locked to the chart.
EUCLIDEAN: the rows are generated from a count and a rotation, and are read-only apart from
ratcheting a beat that is already there. */
enum PatternMode { MODE_MANUAL, MODE_HARMONY, MODE_EUCLIDEAN, NUM_MODES };
static const char* MODE_NAMES[NUM_MODES] = {"Manual", "Harmony", "Euclidean"};
static const char* MODE_NOTES[NUM_MODES] = {
	"The rows are phrases you type, played one after another",
	"The rows are the chart's own bars, four to a row",
	"The rows are generated from a count of beats and a rotation",
};


/** ONE TAB AS THE ENGINE NEEDS IT: a number per cell rather than a character. Compiled on the
main thread when the text changes and handed over whole, so the engine never reads a row that is
being typed into. */
struct CompiledTab {
	bool on = false;
	int mode = MODE_MANUAL;
	/** Nought is a rest, one is a beat, two to nine are that many strokes in the one cell. */
	uint8_t hit[MAX_ROWS][MAX_CELLS] = {};
	int rows = 0;
	/** How many cells each row holds: bars in the row times beats in a bar. */
	int cells = 0;
	/** Nought to nine, cycling against the grid rather than lining up with it. */
	uint8_t strength[MAX_CELLS * 4] = {};
	int strengths = 0;
	float speed[MAX_SPEEDS] = {};
	int speeds = 0;

	/** Every cell of the whole pattern, which is one trip. */
	int total() const { return rows * cells; }
};

struct Compiled {
	CompiledTab tab[MAX_TABS];
};


/** A BAR LINE IS LAYOUT, not pattern: it is drawn where the metre says, and is never stored. */
static bool isBarLine(char c) {
	return c == '|';
}

/** The character a cell holds, or nought for one that is not a cell at all. A rest is the full
stop or a space; a beat is x or a comma, which sits beside the full stop so a rhythm can be
tapped in with one hand; a digit from two to nine ratchets that beat. */
static char cellFor(int codepoint, bool ratchetOnly) {
	const char c = (char) codepoint;
	if (c == 'x' || c == 'X' || c == ',')
		return 'x';
	if (c >= '2' && c <= '9')
		return c;
	if (ratchetOnly)
		return 0;
	if (c == '.' || c == ' ')
		return '.';
	return 0;
}

/** THE EUCLIDEAN DISTRIBUTION, by the modulo formula: cell i fires when (i times k) modulo n is
below k, for k beats among n cells. That places the first beat on cell nought, which is how a
musician reads these — three in eight is the tresillo, x..x..x. — and rotation is then said
separately by the shift rather than being baked into the formula. */
static void euclidean(int n, int k, int shift, char* out) {
	k = math::clamp(k, 0, n);
	for (int i = 0; i < n; i++)
		out[i] = '.';
	if (n <= 0 || k <= 0)
		return;
	for (int i = 0; i < n; i++) {
		if ((i * k) % n < k)
			out[i] = 'x';
	}
	// THE SHIFT ROTATES WHAT WAS GENERATED, positive delaying every beat.
	if (shift != 0) {
		std::string spun((size_t) n, '.');
		for (int i = 0; i < n; i++)
			spun[(size_t) (((i + shift) % n + n) % n)] = out[i];
		for (int i = 0; i < n; i++)
			out[i] = spun[(size_t) i];
	}
}


struct PatternModule : Module, NoteSource, NoteSink {
	enum ParamId {
		P_MODE,
		P_MEASURES,
		P_PHRASES,
		P_EUC_BEATS,
		P_EUC_SHIFT,
		NUM_PARAMS,
	};
	enum InputId { I_CHART, NUM_INPUTS };
	enum OutputId { O_NOTES, NUM_OUTPUTS };
	enum LightId { L_STEP, NUM_LIGHTS };

	/** THE SETTINGS A TAB OWNS, in the order they are stored. The five params above show whichever
	tab is in front; these are where the other seven tabs' values live while they are not. */
	enum TabValue { TV_ON, TV_MODE, TV_MEASURES, TV_PHRASES, TV_EUC_BEATS, TV_EUC_SHIFT, NUM_TV };

	PatternModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(P_MODE, 0.f, (float) (NUM_MODES - 1), 0.f, "Pattern type",
			{MODE_NAMES[0], MODE_NAMES[1], MODE_NAMES[2]});
		configSwitch(P_MEASURES, 1.f, (float) BARS_PER_ROW, 2.f, "Measures",
			{"1", "2", "3", "4"});
		configSwitch(P_PHRASES, 1.f, 8.f, 1.f, "Phrases",
			{"1", "2", "3", "4", "5", "6", "7", "8"});
		configParam(P_EUC_BEATS, 1.f, (float) MAX_CELLS, 5.f, "Active beats");
		configParam(P_EUC_SHIFT, -32.f, 32.f, 0.f, "Beat shift");
		// WHOLE NUMBERS ONLY: these count beats, and half a beat is not a thing to count.
		if (ParamQuantity* q = getParamQuantity(P_EUC_BEATS))
			q->snapEnabled = true;
		if (ParamQuantity* q = getParamQuantity(P_EUC_SHIFT))
			q->snapEnabled = true;
		configInput(I_CHART, "MPX chart");
		configOutput(O_NOTES, "MPX note");

		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		slot = busClaim(&generation);

		for (int t = 0; t < MAX_TABS; t++) {
			val[t][TV_ON].store(t == 0 ? 1.f : 0.f);
			val[t][TV_MODE].store(0.f);
			val[t][TV_MEASURES].store(2.f);
			val[t][TV_PHRASES].store(1.f);
			val[t][TV_EUC_BEATS].store(5.f);
			val[t][TV_EUC_SHIFT].store(0.f);
			text[t].row[0] = "x..x..x.";
			text[t].strength = "9535";
			text[t].speeds = "1";
		}
		recompile();
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

	// ---- the cable -------------------------------------------------------------------------

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

	// ---- the tabs --------------------------------------------------------------------------

	/** WHICH TAB THE CONTROLS BELONG TO, and which tabs are selected so that a change reaches
	more than one at a time. The same arrangement as mpxSprites. */
	std::atomic<int> tab{0};
	std::atomic<int> selection{1};
	/** A tab asked to be switched on or off, acted on by the engine so it happens in order. */
	std::atomic<int> flipEnable{-1};

	std::atomic<float> val[MAX_TABS][NUM_TV];

	bool enabled(int i) const {
		return (i >= 0 && i < MAX_TABS) && val[i][TV_ON].load() > 0.5f;
	}

	/** THE TABS AN EDIT REACHES: those selected, and always the one in front — which is the only
	one whose values the controls are showing, so it cannot be left out of its own edit. */
	int chosenMask() const {
		return selection.load() | (1 << math::clamp(tab.load(), 0, MAX_TABS - 1));
	}

	/** THE TEXT AS TYPED, which is what is shown and what is saved. Main thread only. */
	struct TabText {
		/** Cells WITHOUT bar lines: the lines are drawn where the metre says, so storing them
		would freeze a pattern into the metre it was typed in. */
		std::string row[MAX_ROWS];
		std::string strength;
		std::string speeds;
	};
	TabText text[MAX_TABS];

	Compiled compiled[2];
	std::atomic<int> live{0};

	/** WHICH TAB THE PARAMS ARE SHOWING, so that moving to another tab writes the one being left
	before loading the one arrived at. Engine side, like mpxSprites. */
	int showing = 0;
	bool loaded = false;

	// ---- the metre, which the chart says -----------------------------------------------------

	/** Beats in a bar, from the cable. The grid draws a bar line every so many cells and how
	many that is is the chart's to say. Read by the panel, written by the engine. */
	std::atomic<int> barBeats{4};
	/** How many bars the chart is playing — the whole chart, or the section or span of bars
	chosen on mpxChart, since the chart narrows its own timeline to what it is playing. Nought
	when there is no chart. Harmony mode sizes its grid from this. */
	std::atomic<int> chartBars{0};
	/** Where the music is, for the box to follow and highlight: which row and which cell. */
	std::atomic<int> playingRow{-1};
	std::atomic<int> playingCell{-1};

	// ---- compiling -------------------------------------------------------------------------

	/** How many cells a row holds, which is bars in the row times beats in a bar. */
	int cellsPerRow(int t) const {
		const int cpb = math::clamp(barBeats.load(), 1, MAX_CELLS_PER_BAR);
		const int mode = (int) std::lround(val[t][TV_MODE].load());
		const int bars = (mode == MODE_HARMONY)
			? BARS_PER_ROW
			: math::clamp((int) std::lround(val[t][TV_MEASURES].load()), 1, BARS_PER_ROW);
		return math::clamp(bars * cpb, 1, MAX_CELLS);
	}

	/** How many rows a tab has. Manual and Euclidean are told; Harmony takes it from the chart. */
	int rowCount(int t) const {
		const int mode = (int) std::lround(val[t][TV_MODE].load());
		if (mode == MODE_HARMONY) {
			const int bars = chartBars.load();
			if (bars <= 0)
				return 1;
			return math::clamp((bars + BARS_PER_ROW - 1) / BARS_PER_ROW, 1, MAX_ROWS);
		}
		return math::clamp((int) std::lround(val[t][TV_PHRASES].load()), 1, 8);
	}

	/** THE CELLS A ROW PLAYS: what was typed into it, or, for a row left empty, what the nearest
	filled row above it has — which is how one phrase typed once covers all of them until another
	is wanted. Padded with rests to a whole row. */
	std::string playedRow(int t, int r) const {
		const int n = cellsPerRow(t);
		std::string cells;
		for (int k = r; k >= 0; k--) {
			if (!text[t].row[k].empty()) {
				cells = text[t].row[k];
				break;
			}
		}
		if ((int) cells.size() > n)
			cells.resize((size_t) n);
		if ((int) cells.size() < n)
			cells.append((size_t) (n - (int) cells.size()), '.');
		return cells;
	}

	/** COMPILING IS THE MAIN THREAD'S WORK, always.

	It reads the typed text, which is std::string and belongs to the main thread — the panel is
	writing into it as the user types. Doing it on the audio thread meant one thread reading a
	string while another assigned to it, which is undefined behaviour and in practice leaves a row
	compiled from nothing: the pattern plays until the first edit and is silent afterwards, with
	the cursor still running because the cursor does not depend on the text.

	The audio thread only ever sets `wantCompile` and reads whichever buffer `live` names. */
	void recompile() {
		const int spare = 1 - live.load();
		Compiled& c = compiled[spare];
		for (int t = 0; t < MAX_TABS; t++) {
			CompiledTab& ct = c.tab[t];
			// CLEARED IN PLACE. Assigning a fresh Compiled built a quarter-megabyte temporary,
			// which is not a thing to put on a stack.
			std::memset(ct.hit, 0, sizeof(ct.hit));
			std::memset(ct.strength, 0, sizeof(ct.strength));
			std::memset(ct.speed, 0, sizeof(ct.speed));
			ct.rows = ct.cells = ct.strengths = ct.speeds = 0;
			ct.on = enabled(t);
			ct.mode = (int) std::lround(val[t][TV_MODE].load());
			ct.cells = cellsPerRow(t);
			ct.rows = rowCount(t);

			if (ct.mode == MODE_EUCLIDEAN) {
				// GENERATED, AND THE SAME IN EVERY ROW. The count and the rotation describe one
				// pattern; the rows repeat it, and a ratchet typed over a beat is kept.
				char made[MAX_CELLS];
				euclidean(ct.cells, (int) std::lround(val[t][TV_EUC_BEATS].load()),
					(int) std::lround(val[t][TV_EUC_SHIFT].load()), made);
				for (int r = 0; r < ct.rows; r++) {
					const std::string typed = text[t].row[r];
					for (int i = 0; i < ct.cells; i++) {
						char ch = made[i];
						// A ratchet the user typed over a generated beat survives; a rest the
						// generator made cannot be turned into a beat here.
						if (ch == 'x' && i < (int) typed.size()
							&& typed[(size_t) i] >= '2' && typed[(size_t) i] <= '9')
							ch = typed[(size_t) i];
						ct.hit[r][i] = (ch == 'x') ? 1
							: (ch >= '2' && ch <= '9') ? (uint8_t) (ch - '0') : 0;
					}
				}
			}
			else {
				for (int r = 0; r < ct.rows; r++) {
					const std::string cells = playedRow(t, r);
					for (int i = 0; i < ct.cells; i++) {
						const char ch = cells[(size_t) i];
						ct.hit[r][i] = (ch == 'x' || ch == 'X') ? 1
							: (ch >= '2' && ch <= '9') ? (uint8_t) (ch - '0') : 0;
					}
				}
			}

			int n = 0;
			for (char ch : text[t].strength) {
				if (n >= (int) (sizeof(ct.strength) / sizeof(ct.strength[0])))
					break;
				if (ch >= '0' && ch <= '9')
					ct.strength[n++] = (uint8_t) (ch - '0');
				else if (ch == '.')
					ct.strength[n++] = 9;   // nothing said, so full
			}
			ct.strengths = n;

			ct.speeds = 0;
			if (ct.mode == MODE_HARMONY) {
				// LOCKED TO ONE. The beats are the chart's bars, so a speed other than one would
				// unlock the grid from the thing it is mirroring.
				ct.speed[0] = 1.f;
				ct.speeds = 1;
			}
			else {
				const char* s = text[t].speeds.c_str();
				while (*s && ct.speeds < MAX_SPEEDS) {
					while (*s == ' ' || *s == '\t')
						s++;
					if (!*s)
						break;
					char* end = NULL;
					const float v = std::strtof(s, &end);
					if (end == s) {
						s++;
						continue;
					}
					ct.speed[ct.speeds++] = math::clamp(v, -16.f, 16.f);
					s = end;
				}
				if (ct.speeds == 0) {
					ct.speed[0] = 1.f;
					ct.speeds = 1;
				}
			}
		}
		live.store(spare);
	}

	// ---- saving ----------------------------------------------------------------------------

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "tab", json_integer(tab.load()));
		json_t* tabsJ = json_array();
		for (int t = 0; t < MAX_TABS; t++) {
			json_t* oneJ = json_object();
			json_object_set_new(oneJ, "on", json_boolean(enabled(t)));
			json_object_set_new(oneJ, "mode", json_integer((int) std::lround(val[t][TV_MODE].load())));
			json_object_set_new(oneJ, "measures", json_integer((int) std::lround(val[t][TV_MEASURES].load())));
			json_object_set_new(oneJ, "phrases", json_integer((int) std::lround(val[t][TV_PHRASES].load())));
			json_object_set_new(oneJ, "beats", json_integer((int) std::lround(val[t][TV_EUC_BEATS].load())));
			json_object_set_new(oneJ, "shift", json_integer((int) std::lround(val[t][TV_EUC_SHIFT].load())));
			json_t* rowsJ = json_array();
			for (int r = 0; r < MAX_ROWS; r++)
				json_array_append_new(rowsJ, json_string(text[t].row[r].c_str()));
			json_object_set_new(oneJ, "rows", rowsJ);
			json_object_set_new(oneJ, "strength", json_string(text[t].strength.c_str()));
			json_object_set_new(oneJ, "speeds", json_string(text[t].speeds.c_str()));
			json_array_append_new(tabsJ, oneJ);
		}
		json_object_set_new(rootJ, "tabs", tabsJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* v = json_object_get(rootJ, "tab")) {
			tab.store(math::clamp((int) json_integer_value(v), 0, MAX_TABS - 1));
			selection.store(1 << tab.load());
		}
		json_t* tabsJ = json_object_get(rootJ, "tabs");
		if (json_is_array(tabsJ)) {
			for (int t = 0; t < MAX_TABS && t < (int) json_array_size(tabsJ); t++) {
				json_t* oneJ = json_array_get(tabsJ, (size_t) t);
				if (!json_is_object(oneJ))
					continue;
				if (json_t* v = json_object_get(oneJ, "on"))
					val[t][TV_ON].store(json_boolean_value(v) ? 1.f : 0.f);
				if (json_t* v = json_object_get(oneJ, "mode"))
					val[t][TV_MODE].store((float) json_integer_value(v));
				if (json_t* v = json_object_get(oneJ, "measures"))
					val[t][TV_MEASURES].store((float) json_integer_value(v));
				if (json_t* v = json_object_get(oneJ, "phrases"))
					val[t][TV_PHRASES].store((float) json_integer_value(v));
				if (json_t* v = json_object_get(oneJ, "beats"))
					val[t][TV_EUC_BEATS].store((float) json_integer_value(v));
				if (json_t* v = json_object_get(oneJ, "shift"))
					val[t][TV_EUC_SHIFT].store((float) json_integer_value(v));
				json_t* rowsJ = json_object_get(oneJ, "rows");
				if (json_is_array(rowsJ)) {
					for (int r = 0; r < MAX_ROWS && r < (int) json_array_size(rowsJ); r++) {
						if (const char* s = json_string_value(json_array_get(rowsJ, (size_t) r)))
							text[t].row[r] = s;
					}
				}
				if (const char* s = json_string_value(json_object_get(oneJ, "strength")))
					text[t].strength = s;
				if (const char* s = json_string_value(json_object_get(oneJ, "speeds")))
					text[t].speeds = s;
			}
		}
		// THE PARAMS ARE OURS, NOT RACK'S, and they are set here rather than left to the engine.
		//
		// Rack saves and restores a module's parameters BY INDEX, so a patch saved with an older
		// version of this module puts that version's values into whatever now occupies those
		// indices. A patch from before this rewrite had the rate control at index nought, set to
		// "an eighth", which is 3; the pattern type is at index nought now and has three values,
		// so the 3 clamped to the last of them and every such patch opened in Euclidean — which
		// is read-only, so nothing could be typed into the grid at all.
		//
		// Everything a tab holds is in `val`, saved above under its own name, so the params are
		// simply written from there. Done here, on the main thread, rather than left to the next
		// process() — a patch opened with the engine paused would otherwise keep the wrong values
		// for as long as it stayed paused.
		static const int PARAM_OF[NUM_TV] = {-1, P_MODE, P_MEASURES, P_PHRASES, P_EUC_BEATS,
			P_EUC_SHIFT};
		showing = math::clamp(tab.load(), 0, MAX_TABS - 1);
		for (int v = 0; v < NUM_TV; v++)
			if (PARAM_OF[v] >= 0)
				params[PARAM_OF[v]].setValue(val[showing][v].load());
		loaded = true;
		recompile();
	}

	// ---- playing ---------------------------------------------------------------------------

	/** Where each tab has reached, in cells from the top of its pattern, and which entry of its
	speeds list the trip it is on is using. */
	double at[MAX_TABS] = {};
	int64_t cellIndex[MAX_TABS] = {};
	int speedAt[MAX_TABS] = {};

	struct Sounding {
		int64_t handle = 0;
		float endIn = -1.f;
	};
	Sounding sounding[32];
	float stepLight = 0.f;

	/** A stroke of a ratchet that has not fallen yet. */
	struct Pending {
		bool waiting = false;
		float in = 0.f, level = 0.f, length = 0.f, pitch = 0.f;
		bool pitched = false;
		uint8_t part = 0;
	};
	Pending pending[32];
	int pendingCount = 0;

	float secondsPerBeat = 0.5f;
	double lastBeat = -1.0;
	double lastBeatTime = 0.0;
	double clock = 0.0;

	void strike(float level, float seconds, bool pitched, float pitch, uint8_t part) {
		if (slot < 0)
			return;
		int place = 0;
		for (int i = 0; i < 32; i++) {
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
		// THE VOICE'S PITCH WHERE THERE IS ONE. Without a voicing on the cable the note is a
		// place in time with a weight and a shape, and mpxMelody fills the pitch in.
		e.pitch = pitched ? pitch : 0.f;
		e.level = math::clamp(level, 0.f, 1.f);
		e.duration = math::clamp(seconds, 0.005f, 30.f);
		e.part = part;
		busPush(slot, e);
		s.endIn = seconds;
		stepLight = 1.f;
	}

	void endDue(float dt) {
		for (int i = 0; i < 32; i++) {
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

	void pendingTick(float dt) {
		for (int i = 0; i < 32; i++) {
			if (!pending[i].waiting)
				continue;
			pending[i].in -= dt;
			if (pending[i].in <= 0.f) {
				pending[i].waiting = false;
				strike(pending[i].level, pending[i].length, pending[i].pitched,
					pending[i].pitch, pending[i].part);
			}
		}
	}

	/** MOVING BETWEEN TABS. The params show one tab; arriving at another writes what the params
	hold back into the tab being left, then loads the one arrived at. Done on the audio thread so
	it happens once and in order — the same as mpxSprites. */
	void followTab() {
		static const int PARAM_OF[NUM_TV] = {-1, P_MODE, P_MEASURES, P_PHRASES, P_EUC_BEATS,
			P_EUC_SHIFT};
		const int want = math::clamp(tab.load(), 0, MAX_TABS - 1);
		if (loaded && want == showing) {
			// A PLATE CHANGED SETS THAT SETTING ON EVERY SELECTED TAB, and only that setting:
			// the others keep their own values, so tabs that differ elsewhere still differ. This
			// is what selecting several tabs is for — setting one field the same across all of
			// them without visiting each in turn.
			const int chosen = chosenMask();
			for (int v = 0; v < NUM_TV; v++) {
				if (PARAM_OF[v] < 0)
					continue;
				const float now = params[PARAM_OF[v]].getValue();
				if (val[showing][v].load() == now)
					continue;
				for (int t = 0; t < MAX_TABS; t++)
					if (chosen & (1 << t))
						val[t][v].store(now);
				wantCompile.store(true);
			}
			return;
		}
		if (loaded) {
			for (int v = 0; v < NUM_TV; v++)
				if (PARAM_OF[v] >= 0)
					val[showing][v].store(params[PARAM_OF[v]].getValue());
		}
		showing = want;
		for (int v = 0; v < NUM_TV; v++)
			if (PARAM_OF[v] >= 0)
				params[PARAM_OF[v]].setValue(val[showing][v].load());
		loaded = true;
		wantCompile.store(true);
	}

	std::atomic<bool> wantCompile{false};

	/** Called from the panel's step, which is the main thread. */
	void tick() {
		if (wantCompile.exchange(false))
			recompile();
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

		// THE CHART PASSES THROUGH, so the module sits in the chain rather than beside it.
		Event in;
		while (reader.next(in))
			busPush(slot, in);
		Harmony h;
		const bool haveHarmony = reader.harmony(h);
		if (haveHarmony)
			busPublishHarmony(slot, h);
		ChordVoicing v;
		const bool haveVoicing = reader.voicing(v) && v.valid && v.count > 0;
		if (haveVoicing)
			busPublishVoicing(slot, v);
		{
			float sustain = 0.f, soft = 0.f;
			reader.pedals(sustain, soft);
			busPublishPedals(slot, sustain, soft);
		}

		// THE TABS' SPEEDS, FOR WHOEVER IS PLACING SOMETHING ALONG A PATH. A module that moves
		// between notes needs the speed at every sample, not only when a beat falls, so this is
		// state on the cable rather than a number on a note. Harmony mode is locked to one,
		// because its beats are the chart's bars.
		{
			const Compiled& c = compiled[live.load()];
			PartMotion sp;
			sp.valid = true;
			for (int t = 0; t < MAX_TABS; t++) {
				const CompiledTab& ct = c.tab[t];
				sp.speed[t] = (!ct.on || ct.speeds <= 0) ? 0.f
					: (ct.mode == MODE_HARMONY) ? 1.f
					: ct.speed[speedAt[t] % ct.speeds];
				// A CELL IS A BEAT, so the whole pattern's length in beats is simply how many
				// cells it has: its rows times the cells in a row.
				sp.beats[t] = ct.on ? (float) ct.total() : 0.f;
			}
			busPublishMotion(slot, sp);
		}

		const int flip = flipEnable.exchange(-1);
		if (flip >= 0 && flip < MAX_TABS) {
			val[flip][TV_ON].store(enabled(flip) ? 0.f : 1.f);
			wantCompile.store(true);
		}
		followTab();

		if (haveHarmony && (h.valid || h.timingValid)) {
			const int beatsInBar = math::clamp((int) h.barBeats, 1, MAX_CELLS_PER_BAR);
			if (barBeats.load() != beatsInBar) {
				barBeats.store(beatsInBar);
				wantCompile.store(true);
			}
			const int bars = (h.cycleBeats > 0.f)
				? math::clamp((int) std::lround(h.cycleBeats / (float) beatsInBar), 0, MAX_ROWS * BARS_PER_ROW)
				: 0;
			if (chartBars.load() != bars) {
				chartBars.store(bars);
				wantCompile.store(true);
			}
		}
		// NOT COMPILED HERE. The flag is left for the main thread to act on; see recompile.

		if (haveHarmony && (h.valid || h.timingValid)) {
			// HOW LONG A BEAT IS, followed from the chart's own beat rather than from a tempo
			// knob, so a ratchet and a note's length stay in time while the tempo changes.
			clock += args.sampleTime;
			if (lastBeat >= 0.0 && h.beat > lastBeat && h.beat - lastBeat < 1.0) {
				const double seconds = clock - lastBeatTime;
				const double beats = h.beat - lastBeat;
				if (seconds > 0.0 && beats > 0.0) {
					const float now = (float) (seconds / beats);
					secondsPerBeat += (math::clamp(now, 0.02f, 4.f) - secondsPerBeat) * 0.05f;
				}
			}
			const double beatStep = (lastBeat >= 0.0 && h.beat >= lastBeat
				&& h.beat - lastBeat < 1.0) ? (h.beat - lastBeat) : 0.0;
			lastBeat = h.beat;
			lastBeatTime = clock;

			const Compiled& c = compiled[live.load()];
			const int front = math::clamp(tab.load(), 0, MAX_TABS - 1);
			for (int t = 0; t < MAX_TABS; t++) {
				const CompiledTab& ct = c.tab[t];
				if (!ct.on || ct.total() <= 0)
					continue;

				// THE SPEED THIS CELL IS BEING PLAYED AT, which decides how long it lasts and so
				// how long its note is held and how a ratchet inside it is spaced.
				float speedNow = 1.f;
				if (ct.mode == MODE_HARMONY) {
					// THE CHART IS THE CLOCK. The grid mirrors the chart's bars, so the cell
					// playing is simply where the chart has reached.
					at[t] = h.beat;
				}
				else {
					speedNow = ct.speed[ct.speeds > 0 ? (speedAt[t] % ct.speeds) : 0];
					at[t] += beatStep * (double) speedNow;
				}

				const int total = ct.total();
				int64_t cell = (int64_t) std::floor(at[t]);
				// A TRIP IS THE WHOLE PATTERN ONCE, and each trip takes the next speed in the
				// list, so "1 2 -1" is a pass as written, one at double time, then one backwards.
				if (ct.mode != MODE_HARMONY && ct.speeds > 1) {
					const int64_t trip = (cell >= 0) ? (cell / total) : ((cell + 1) / total - 1);
					const int want = (int) (((trip % ct.speeds) + ct.speeds) % ct.speeds);
					speedAt[t] = want;
				}
				const int64_t wrapped = ((cell % total) + total) % total;

				if (t == front) {
					playingRow.store((int) (wrapped / ct.cells));
					playingCell.store((int) (wrapped % ct.cells));
				}
				if (cell == cellIndex[t])
					continue;
				cellIndex[t] = cell;
				playCell(t, ct, (int) wrapped, haveVoicing, v, speedNow);
			}
		}

		pendingTick(args.sampleTime);
		endDue(args.sampleTime);
		stepLight = std::fmax(0.f, stepLight - args.sampleTime * 6.f);
		lights[L_STEP].setBrightness(stepLight);
	}

	void playCell(int t, const CompiledTab& ct, int wrapped, bool haveVoicing,
			const ChordVoicing& voicing, float cellSpeed) {
		const int row = wrapped / ct.cells;
		const int cell = wrapped % ct.cells;
		if (row < 0 || row >= ct.rows)
			return;
		const int count = ct.hit[row][cell];
		if (count <= 0)
			return;

		// THE STRENGTH IS THE LEVEL. Nought is a rest, so the strength row can thin the pattern
		// on its own; a strength shorter than the grid floats against it, which is the point of
		// it being a separate field.
		const int strength = (ct.strengths > 0)
			? ct.strength[((wrapped % ct.strengths) + ct.strengths) % ct.strengths] : 9;
		if (strength <= 0)
			return;
		const float level = math::clamp((float) strength / 9.f, 0.f, 1.f);

		// A TAB IS A VOICE. Tab three takes voice three of the voicing, if the voicing has one.
		bool pitched = false;
		float pitch = 0.f;
		if (haveVoicing && t < voicing.count) {
			pitched = true;
			pitch = voicing.pitch[t];
		}

		// HOW LONG A CELL LASTS, AT THE SPEED IT IS BEING PLAYED AT.
		//
		// A cell is one beat of the grid, but the cycle speed decides how fast the grid is walked:
		// at two, a cell goes by in half a beat. Taking a beat as the answer gave every note very
		// nearly a beat of length while the next one began half a beat later, so every note
		// overlapped its successor and two notes sounded at once throughout — and a ratchet
		// spread its strokes over a whole beat, landing the later ones after the following cell
		// had already fired.
		const float speed = std::fmax(0.05f, std::fabs(cellSpeed));
		const float cellSeconds = secondsPerBeat / speed;
		const float length = 0.9f * cellSeconds;
		const uint8_t part = (uint8_t) (t + 1);

		// A RATCHET IS THAT MANY STROKES IN THE CELL, queued a fraction of it apart, which is
		// what a roll or a drag is.
		for (int i = 0; i < count && i < 8; i++) {
			if (i == 0) {
				strike(level, length / (float) count, pitched, pitch, part);
			}
			else {
				Pending& p = pending[pendingCount % 32];
				p.in = cellSeconds * (float) i / (float) count;
				p.level = level * (0.75f + 0.25f * (float) i / (float) count);
				p.length = length / (float) count;
				p.pitched = pitched;
				p.pitch = pitch;
				p.part = part;
				p.waiting = true;
				pendingCount++;
			}
		}
	}
};


// ---- the panel -------------------------------------------------------------------------------

/** THE GRID'S COLOURS, taken from the editor this is modelled on so that a pattern moved between
the two reads the same way. The box is the ground the text sits on; the fields inside it have no
fill and no border of their own, because the frame and the rules supply all the structure. */
static const NVGcolor GRID_BOX = nvgRGB(0x47, 0x47, 0x47);
static const NVGcolor GRID_FRAME = nvgRGB(0x78, 0x96, 0x78);
static const NVGcolor GRID_RULE = nvgRGB(0x6f, 0x8f, 0x6f);
static const NVGcolor GRID_TEXT = nvgRGB(0xff, 0xff, 0xff);
static const NVGcolor GRID_GHOST = nvgRGB(0x9a, 0x9a, 0x9a);
static const NVGcolor GRID_LABEL = nvgRGB(0xd2, 0xdb, 0xe4);

/** HOW BIG A CHARACTER IS AT MOST, in pixels at Rack's own scale. Larger than the editor this
follows, which uses about fifteen: the grid is the one thing on this panel that is read rather
than glanced at, and the room came from the knobs that are no longer here.

AT MOST, because the panel is only as wide as the grid and the grid must always show four bars.
A bar is as long as the metre says, so four bars of six-eight is half again as many characters as
four bars of four-four — and rather than carry a third of a panel of empty space so that the rare
case fits at full size, the type shrinks to fit when the bar is long. Four-four gets the whole
twenty-four pixels; six-eight gets about eighteen, which is still the size the editor this
follows uses throughout. */
static const float GRID_PX_MAX = 24.f;
/** The smallest it is allowed to become, below which the grid stops being readable at all. */
static const float GRID_PX_MIN = 11.f;
/** The room above and below a character in its row. Tight: a row is the character and a little,
not the character and half as much again. */
static const float GRID_LEAD = 6.f;
static const float GRID_NUM_W = 16.f;
static const float GRID_PAD = 4.f;
/** What the scroll bar and its gap take out of the width, counted whether or not one is showing
so that the type does not change size when a row is added. */
static const float GRID_BAR_W = 11.f;


/** THE TABS: which pattern the controls below them belong to. The same control as mpxSprites',
eight wide instead of four, each with its own light at its left-hand end so that every tab's
state is visible at once rather than only the one in front. */
struct TabStrip : widget::OpaqueWidget {
	PatternModule* module = NULL;

	float tabsFrom() const { return box.size.y + 3.f; }
	/** THE TABS SPREAD ACROSS THE FACE, a share of it each. On a panel sized to the grid there is
	no width going spare, so a tab is as wide as an eighth of what is left after the ALL button
	rather than a fixed size with a gap after it. */
	float tabWidth() const { return (box.size.x - tabsFrom()) / MAX_TABS; }
	float stripW() const { return box.size.x; }

	static const int ALL_BUTTON = -2;
	int at(math::Vec pos) {
		if (box.size.x <= 0.f || pos.x >= stripW())
			return -1;
		if (pos.x < box.size.y)
			return ALL_BUTTON;
		if (pos.x < tabsFrom())
			return -1;
		return math::clamp((int) ((pos.x - tabsFrom()) / tabWidth()), 0, MAX_TABS - 1);
	}

	/** SMALLER THAN THE SPRITE MODULE'S, because eight numbers have to fit where four did and the
	number is what the tab is found by. It is still the whole left-hand third of the tab, which is
	a large enough target to hit. */
	math::Rect lampRect(int i) const {
		const float w = tabWidth();
		const float r = std::min(w * 0.145f, box.size.y * 0.28f);
		return math::Rect(math::Vec(tabsFrom() + i * w + 2.5f, box.size.y / 2.f - r),
			math::Vec(2.f * r, 2.f * r));
	}

	void onButton(const ButtonEvent& e) override {
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			const int i = at(e.pos);
			const int mods = e.mods & RACK_MOD_MASK;
			const int all = (1 << MAX_TABS) - 1;
			if (i == ALL_BUTTON) {
				module->selection.store(all);
				e.consume(this);
				return;
			}
			if (i >= 0) {
				const int chosen = module->selection.load() | (1 << module->tab.load());
				const int bit = 1 << i;
				if (mods == GLFW_MOD_SHIFT) {
					int next = chosen ^ bit;
					if (next == 0)
						next = bit;
					module->selection.store(next);
					if (!(next & (1 << module->tab.load())))
						for (int k = 0; k < MAX_TABS; k++)
							if (next & (1 << k)) {
								module->tab.store(k);
								break;
							}
				}
				// THE LIGHT SWITCHES THE PATTERN ON OR OFF, and brings it to the front only when
				// it switches it ON. What you do straight after turning one on is set it up;
				// turning one off means you have finished with it, and being moved to its tab
				// would take you away from whatever you were working on.
				else if (lampRect(i).contains(e.pos)) {
					const bool turningOn = !module->enabled(i);
					module->flipEnable.store(i);
					if (turningOn) {
						module->selection.store(bit);
						module->tab.store(i);
					}
				}
				else {
					module->selection.store(bit);
					module->tab.store(i);
				}
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = gridFont();
		const int shown = module ? math::clamp(module->tab.load(), 0, MAX_TABS - 1) : 0;
		const int chosen = module ? (module->selection.load() | (1 << shown)) : 1;
		const int all = (1 << MAX_TABS) - 1;

		{
			const float s = box.size.y;
			const bool lit = chosen == all;
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0.75f, 0.75f, s - 1.5f, s - 1.5f, 2.f);
			nvgFillColor(args.vg, lit ? nvgRGB(0xd8, 0xde, 0xe6) : nvgRGB(0x2a, 0x30, 0x3a));
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(0xd8, 0xde, 0xe6, lit ? 255 : 150));
			nvgStrokeWidth(args.vg, lit ? 1.4f : 0.8f);
			nvgStroke(args.vg);
			if (font && font->handle >= 0) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 7.f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFillColor(args.vg, lit ? nvgRGB(0x14, 0x18, 0x1e) : nvgRGB(0xc8, 0xd0, 0xd8));
				nvgText(args.vg, s / 2.f, s / 2.f, "ALL", NULL);
			}
		}

		const float w = tabWidth();
		for (int i = 0; i < MAX_TABS; i++) {
			const float x = tabsFrom() + i * w;
			const bool on = (chosen & (1 << i)) != 0;
			const bool primary = (i == shown);

			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x + 0.75f, 0.75f, w - 1.5f, box.size.y - 1.5f, 2.f);
			nvgFillColor(args.vg, on ? GRID_FRAME : nvgRGB(0x2a, 0x30, 0x3a));
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, primary && chosen != (1 << i)
				? nvgRGB(0xff, 0xff, 0xff)
				: nvgRGBA(0x78, 0x96, 0x78, on ? 255 : 150));
			nvgStrokeWidth(args.vg, primary ? 1.6f : (on ? 1.2f : 0.8f));
			nvgStroke(args.vg);

			{
				const math::Rect r = lampRect(i);
				const bool liveNow = module && module->enabled(i);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, r.pos.x + r.size.x / 2.f, r.pos.y + r.size.y / 2.f,
					r.size.x / 2.f);
				nvgFillColor(args.vg, liveNow ? nvgRGB(0xff, 0x4a, 0x40) : nvgRGB(0x6a, 0x74, 0x82));
				nvgFill(args.vg);
				nvgStrokeColor(args.vg, liveNow ? nvgRGBA(0xff, 0xc0, 0xb8, 0xdd)
					: nvgRGBA(0xc0, 0xc8, 0xd2, 0xcc));
				nvgStrokeWidth(args.vg, 1.2f);
				nvgStroke(args.vg);
			}

			if (font && font->handle >= 0) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 9.f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFillColor(args.vg, on ? nvgRGB(0x14, 0x18, 0x1e) : nvgRGB(0xc8, 0xd0, 0xd8));
				// In the room the lamp leaves, centred in it rather than at a fraction of the
				// whole tab — which with a smaller lamp would sit it off to one side.
				const float from = lampRect(i).pos.x + lampRect(i).size.x;
				nvgText(args.vg, (from + x + w - 2.f) / 2.f, box.size.y / 2.f,
					string::f("%d", i + 1).c_str(), NULL);
			}
		}
		widget::OpaqueWidget::draw(args);
	}
};


/** THE GRID: one framed box whose rows are the pattern, with a bar line drawn after every
complete bar and a rule between the rows.

TYPING OVERWRITES, NEVER INSERTS, which is the whole of why this is not an ordinary text field.
A character is a beat and the column it sits in is where in the bar it falls, so anything that
pushed the rest of the row along would move every later beat off its place. At the very end of a
row, typing appends a bar and backspace removes one.

SCROLLS, because a chart can be longer than the box, and follows the beat while it plays unless
the scroll bar has just been touched. */
struct GridBox : widget::OpaqueWidget {
	PatternModule* module = NULL;
	/** The caret, as a row and a cell within it. */
	int caretRow = 0, caretCell = 0;
	/** The first row shown, and how long to leave the playing beat alone after a manual scroll. */
	float scroll = 0.f;
	float leaveAlone = 0.f;
	bool dragging = false;

	int tabNow() const {
		return module ? math::clamp(module->tab.load(), 0, MAX_TABS - 1) : 0;
	}
	int cells() const { return module ? module->cellsPerRow(tabNow()) : 4; }
	int rows() const { return module ? module->rowCount(tabNow()) : 1; }
	int barCells() const {
		return module ? math::clamp(module->barBeats.load(), 1, MAX_CELLS_PER_BAR) : 4;
	}
	bool readOnly() const {
		if (!module)
			return true;
		return (int) std::lround(module->val[tabNow()][PatternModule::TV_MODE].load())
			== MODE_EUCLIDEAN;
	}

	/** HOW MANY CHARACTERS A ROW DRAWS: its cells, plus the bar line after each bar. */
	int rowChars() const {
		const int cpb = barCells();
		const int n = cells();
		return n + (n + cpb - 1) / cpb;
	}

	/** WHAT A CHARACTER ACTUALLY ADVANCES, as a fraction of the type size, measured from the font
	by the renderer rather than taken from a constant.

	The constant is the right number and it was still wrong to use: a marker drawn over a
	character has to sit exactly where the renderer put that character, and anything worked out
	independently of the renderer drifts from it — by a fraction of a character at the left of a
	row and by several characters' worth at the right. Measured once per size, from nvgTextBounds,
	which returns the advance the text was actually laid out with. */
	mutable float advRatio = GRID_ADVANCE;
	mutable float advAt = 0.f;

	/** THE TYPE SIZE, which is whatever makes a whole row fit the width it has. */
	float px() const {
		const int chars = rowChars();
		if (chars <= 0)
			return GRID_PX_MAX;
		const float room = box.size.x - GRID_NUM_W - 2.f * GRID_PAD - GRID_BAR_W;
		return math::clamp(room / ((float) chars * advRatio), GRID_PX_MIN, GRID_PX_MAX);
	}
	float rowH() const { return px() + GRID_LEAD; }
	float charW() const { return px() * advRatio; }

	float rowsShown() const {
		return std::max(1.f, std::floor((box.size.y - 2.f * GRID_PAD) / rowH()));
	}
	float textLeft() const { return GRID_NUM_W + GRID_PAD; }
	/** The width of the scroll bar, or nought when everything fits. */
	float barW() const { return (rows() > (int) rowsShown()) ? 8.f : 0.f; }

	/** HOW WIDE THE FRAME IS: as wide as what is in it, and no wider.

	The panel is sized for the widest metre it has to hold, which is four bars of six; a piece in
	four-four fills two thirds of that, and a frame drawn to the panel's width would run an empty
	third out to the edge. The editor this follows sizes its box to its content for the same
	reason, so the frame moves with the metre and with the number of measures. */
	float frameW() const {
		const int chars = (int) shown(0).size();
		const float want = GRID_NUM_W + GRID_PAD + (float) chars * charW() + GRID_PAD;
		return math::clamp(want, GRID_NUM_W + 8.f, box.size.x - barW() - (barW() > 0.f ? 3.f : 0.f));
	}

	/** The row a pattern plays, with bar lines put in where the metre says. */
	std::string shown(int r) const {
		if (!module)
			return "";
		const int t = tabNow();
		std::string cells;
		if (readOnly()) {
			const int n = module->cellsPerRow(t);
			char made[MAX_CELLS];
			euclidean(n, (int) std::lround(module->val[t][PatternModule::TV_EUC_BEATS].load()),
				(int) std::lround(module->val[t][PatternModule::TV_EUC_SHIFT].load()), made);
			const std::string typed = module->text[t].row[r];
			cells.assign(made, made + n);
			for (int i = 0; i < n && i < (int) typed.size(); i++)
				if (cells[(size_t) i] == 'x' && typed[(size_t) i] >= '2' && typed[(size_t) i] <= '9')
					cells[(size_t) i] = typed[(size_t) i];
		}
		else {
			cells = module->playedRow(t, r);
		}
		const int cpb = barCells();
		std::string out;
		for (size_t i = 0; i < cells.size(); i++) {
			out += cells[i];
			if ((int) ((i + 1) % (size_t) cpb) == 0)
				out += '|';
		}
		return out;
	}

	/** Whether a row is showing what it inherited rather than anything typed into it, which is
	drawn faint — the same ghost the editor this follows uses. */
	bool ghosted(int r) const {
		return module && !readOnly() && module->text[tabNow()].row[r].empty();
	}

	// ---- editing ---------------------------------------------------------------------------

	/** Writes a row back and recompiles. A row that was inheriting becomes its own the moment
	anything is typed into it, which is why the inherited cells are written out whole. */
	void putRow(int r, const std::string& cells) {
		if (!module || r < 0 || r >= MAX_ROWS)
			return;
		// EVERY SELECTED TAB, as a plate change does: typing one rhythm into several tabs at once
		// is how they are given a common starting point.
		const int chosen = module->chosenMask();
		for (int t = 0; t < MAX_TABS; t++)
			if (chosen & (1 << t))
				module->text[t].row[r] = cells;
		module->wantCompile.store(true);
	}

	std::string cellsOf(int r) const {
		if (!module)
			return "";
		return readOnly() ? module->text[tabNow()].row[r] : module->playedRow(tabNow(), r);
	}

	void onSelectText(const SelectTextEvent& e) override {
		if (!module) {
			e.consume(this);
			return;
		}
		const char c = cellFor(e.codepoint, readOnly());
		if (!c) {
			e.consume(this);
			return;
		}
		const int n = cells();
		std::string cells = cellsOf(caretRow);
		if ((int) cells.size() < n)
			cells.append((size_t) (n - (int) cells.size()), '.');
		if (readOnly()) {
			// ONLY A RATCHET, and only over a beat that is already there: the structure is
			// generated and cannot be typed over.
			const std::string made = shown(caretRow);
			int seen = 0;
			char under = '.';
			for (char ch : made) {
				if (isBarLine(ch))
					continue;
				if (seen++ == caretCell) {
					under = ch;
					break;
				}
			}
			if (under == '.')
				return;
			cells[(size_t) caretCell] = (c == 'x') ? 'x' : c;
		}
		else if (caretCell >= n) {
			return;
		}
		else {
			cells[(size_t) caretCell] = c;
		}
		putRow(caretRow, cells);
		if (caretCell + 1 < n)
			caretCell++;
		e.consume(this);
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (!module || (e.action != GLFW_PRESS && e.action != GLFW_REPEAT)) {
			widget::OpaqueWidget::onSelectKey(e);
			return;
		}
		const int n = cells();
		const int lastRow = rows() - 1;
		switch (e.key) {
			case GLFW_KEY_BACKSPACE:
				if (!readOnly() && caretCell > 0) {
					std::string cells = cellsOf(caretRow);
					cells[(size_t) (caretCell - 1)] = '.';
					putRow(caretRow, cells);
					caretCell--;
				}
				e.consume(this);
				return;
			case GLFW_KEY_DELETE:
				if (!readOnly() && caretCell < n) {
					std::string cells = cellsOf(caretRow);
					cells[(size_t) caretCell] = '.';
					putRow(caretRow, cells);
				}
				e.consume(this);
				return;
			case GLFW_KEY_LEFT:
				caretCell = std::max(0, caretCell - 1);
				e.consume(this);
				return;
			case GLFW_KEY_RIGHT:
				caretCell = std::min(n - 1, caretCell + 1);
				e.consume(this);
				return;
			case GLFW_KEY_UP:
				caretRow = std::max(0, caretRow - 1);
				show(caretRow);
				e.consume(this);
				return;
			case GLFW_KEY_DOWN:
				caretRow = std::min(lastRow, caretRow + 1);
				show(caretRow);
				e.consume(this);
				return;
			case GLFW_KEY_HOME:
				caretCell = 0;
				e.consume(this);
				return;
			case GLFW_KEY_END:
				caretCell = n - 1;
				e.consume(this);
				return;
			case GLFW_KEY_ENTER:
			case GLFW_KEY_KP_ENTER:
				// Nothing to commit: every keystroke has already been applied.
				e.consume(this);
				return;
			// NOT SPACE. It is the rest character, and consuming the KEY event here would stop
			// the text event that actually enters the rest from ever being sent. What kept the
			// space bar from muting Rack was not this but DevMute learning that a widget holding
			// the keyboard is typing, whether or not it is a text field.
			default:
				break;
		}
		widget::OpaqueWidget::onSelectKey(e);
	}

	/** Brings a row into view if it is not. */
	void show(int r) {
		const float n = rowsShown();
		if ((float) r < scroll)
			scroll = (float) r;
		else if ((float) r >= scroll + n)
			scroll = (float) r - n + 1.f;
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module) {
			APP->event->setSelectedWidget(this);
			const float bw = barW();
			if (bw > 0.f && e.pos.x > frameW()) {
				dragging = true;
				leaveAlone = 4.f;
				dragTo(e.pos.y);
				e.consume(this);
				return;
			}
			// THE CELL UNDER THE POINTER, bar lines skipped: they are drawn, not typed, so there
			// is nothing to put a caret on.
			const int r = math::clamp((int) (scroll + (e.pos.y - GRID_PAD) / rowH()),
				0, rows() - 1);
			const float x = e.pos.x - textLeft();
			const int at = (int) std::floor(x / charW());
			int cell = 0;
			const std::string s = shown(r);
			for (int i = 0; i < at && i < (int) s.size(); i++)
				if (!isBarLine(s[(size_t) i]))
					cell++;
			caretRow = r;
			caretCell = math::clamp(cell, 0, cells() - 1);
			e.consume(this);
			return;
		}
		widget::OpaqueWidget::onButton(e);
	}

	void dragTo(float y) {
		const float n = rowsShown();
		const float span = std::max(1.f, (float) rows() - n);
		const float frac = math::clamp((y - GRID_PAD) / std::max(1.f, box.size.y - 2.f * GRID_PAD),
			0.f, 1.f);
		scroll = frac * span;
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (dragging) {
			leaveAlone = 4.f;
			dragTo(APP->scene->rack->getMousePos().minus(getAbsoluteOffset(math::Vec())).y);
		}
		widget::OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		dragging = false;
		widget::OpaqueWidget::onDragEnd(e);
	}

	/** THE WHEEL SCROLLS THE BOX, not the rack. Rack scrolls its own view when the wheel turns
	over a module, which over a box that has more rows than it can show is the wrong answer. */
	void onHoverScroll(const HoverScrollEvent& e) override {
		if (rows() > (int) rowsShown()) {
			scroll = math::clamp(scroll - e.scrollDelta.y / 40.f, 0.f,
				std::max(0.f, (float) rows() - rowsShown()));
			leaveAlone = 4.f;
			e.consume(this);
			return;
		}
		widget::OpaqueWidget::onHoverScroll(e);
	}

	void step() override {
		// FOLLOWS THE BEAT, unless the scroll bar or the wheel has just been used — being dragged
		// back to the playing row while reading somewhere else is worse than not following.
		if (leaveAlone > 0.f)
			leaveAlone -= APP->window->getLastFrameDuration();
		else if (module) {
			const int r = module->playingRow.load();
			if (r >= 0)
				show(math::clamp(r, 0, rows() - 1));
		}
		scroll = math::clamp(scroll, 0.f, std::max(0.f, (float) rows() - rowsShown()));
		widget::OpaqueWidget::step();
	}

	void draw(const DrawArgs& args) override {
		const float w = frameW(), h = box.size.y;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, w, h, 4.f);
		nvgFillColor(args.vg, GRID_BOX);
		nvgFill(args.vg);

		std::shared_ptr<window::Font> font = gridFont();
		// THE ADVANCE, FROM THE RENDERER, before anything is placed from it.
		//
		// AT A FIXED REFERENCE SIZE, once, not at the size the row happens to be set at: the row's
		// size is worked out FROM this ratio, so measuring at that size would have each feeding
		// the other and the type would hunt between two sizes for ever. The ratio does not depend
		// on the size anyway, beyond a fraction of a pixel of hinting.
		if (advAt == 0.f && font && font->handle >= 0) {
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, 100.f);
			const float adv = nvgTextBounds(args.vg, 0.f, 0.f, "x", NULL, NULL);
			if (adv > 0.f)
				advRatio = adv / 100.f;
			advAt = 1.f;
		}
		const int n = (int) rowsShown();
		const int first = (int) std::floor(scroll);
		const float cw = charW();
		const bool selected = (APP->event->getSelectedWidget() == this);
		const float size = px();
		const float rh = rowH();
		const int playRow = module ? module->playingRow.load() : -1;
		const int playCell = module ? module->playingCell.load() : -1;

		nvgSave(args.vg);
		nvgScissor(args.vg, 1.f, 1.f, w - 2.f, h - 2.f);
		for (int k = 0; k <= n && first + k < rows(); k++) {
			const int r = first + k;
			const float y = GRID_PAD + ((float) r - scroll) * rh;
			if (y + rh < 0.f || y > h)
				continue;

			if (k > 0) {
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 2.f, y);
				nvgLineTo(args.vg, w - 2.f, y);
				nvgStrokeColor(args.vg, GRID_RULE);
				nvgStrokeWidth(args.vg, 2.f);
				nvgStroke(args.vg);
			}
			// The row's number, behind a rule of its own.
			if (font && font->handle >= 0) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, size * 0.55f);
				nvgFillColor(args.vg, GRID_LABEL);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgText(args.vg, GRID_NUM_W / 2.f, y + rh / 2.f,
					string::f("%d", r + 1).c_str(), NULL);
			}
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, GRID_NUM_W, y);
			nvgLineTo(args.vg, GRID_NUM_W, y + rh);
			nvgStrokeColor(args.vg, GRID_RULE);
			nvgStrokeWidth(args.vg, 2.f);
			nvgStroke(args.vg);

			const std::string s = shown(r);
			if (font && font->handle >= 0) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, size);
				nvgFillColor(args.vg, ghosted(r) ? GRID_GHOST : GRID_TEXT);
				nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				nvgText(args.vg, textLeft(), y + rh / 2.f, s.c_str(), NULL);
			}

			// THE SOUNDING CELL: a white outline one character wide, which marks where the music
			// is without hiding what is written there.
			// Guarded against a cell past the end of the row, which happens for one frame when
			// the metre or the measures change and the engine has not been recompiled yet.
			if (r == playRow && playCell >= 0 && playCell < cells()) {
				const float x = textLeft() + cw * (float) screenCol(s, playCell);
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, x - 1.f, y + 2.f, cw + 2.f, rh - 4.f, 2.f);
				nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xcc));
				nvgStrokeWidth(args.vg, 2.f);
				nvgStroke(args.vg);
			}
			// THE CARET, drawn as the same box in the frame's own green so the two are told apart.
			if (selected && r == caretRow) {
				const float x = textLeft() + cw * (float) screenCol(s, caretCell);
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, x - 1.f, y + 2.f, cw + 2.f, rh - 4.f, 2.f);
				nvgStrokeColor(args.vg, GRID_FRAME);
				nvgStrokeWidth(args.vg, 2.f);
				nvgStroke(args.vg);
			}
		}
		nvgRestore(args.vg);

		// The scroll bar, when there is more than fits.
		const float bw = barW();
		if (bw > 0.f) {
			// JUST OUTSIDE THE FRAME, not inside it: inside, it would eat a character's width
			// from a row, and the frame is drawn to exactly the characters it holds.
			const float x = w + 3.f;
			const float n2 = rowsShown();
			const float frac = n2 / (float) rows();
			const float len = std::max(12.f, (h - 2.f * GRID_PAD) * frac);
			const float span = std::max(1.f, (float) rows() - n2);
			const float top = GRID_PAD + (h - 2.f * GRID_PAD - len) * (scroll / span);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x, GRID_PAD, bw, h - 2.f * GRID_PAD, bw / 2.f);
			nvgFillColor(args.vg, nvgRGB(0x24, 0x2a, 0x33));
			nvgFill(args.vg);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x + 1.f, top, bw - 2.f, len, (bw - 2.f) / 2.f);
			nvgFillColor(args.vg, nvgRGB(0x6a, 0x80, 0x6a));
			nvgFill(args.vg);
		}

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 1.f, 1.f, w - 2.f, h - 2.f, 4.f);
		nvgStrokeColor(args.vg, GRID_FRAME);
		nvgStrokeWidth(args.vg, 2.f);
		nvgStroke(args.vg);

		widget::OpaqueWidget::draw(args);
	}

	/** Which column a cell is drawn in, which is past the bar lines already drawn before it. */
	static int screenCol(const std::string& s, int cell) {
		int seen = 0;
		for (int i = 0; i < (int) s.size(); i++) {
			if (isBarLine(s[(size_t) i]))
				continue;
			if (seen == cell)
				return i;
			seen++;
		}
		return (int) s.size();
	}
};


/** A SHORT FIELD IN THE GRID'S OWN FACE: the beat strength and the cycle speeds. Neither lines up
with the grid — they cycle against it, so they are as long as they need to be and no longer.

COMMITS ON EVERY KEYSTROKE, so a speed typed while the pattern plays is heard at once, and a list
half typed never commits: "1 -" holds the last good reading until it is a number again. */
struct ShortField : LedDisplayTextField {
	PatternModule* module = NULL;
	bool speeds = false;

	ShortField() {
		multiline = false;
		bgColor = nvgRGB(0x0f, 0x12, 0x17);
		color = GRID_TEXT;
		textOffset = math::Vec(4.f, 1.f);
	}

	int tabNow() const {
		return module ? math::clamp(module->tab.load(), 0, MAX_TABS - 1) : 0;
	}

	/** The speeds field is inactive in Harmony mode: the beats are locked to the chart's bars, so
	a speed other than one would unlock the grid from the thing it is mirroring. */
	bool usable() const {
		if (!module)
			return false;
		if (!speeds)
			return true;
		return (int) std::lround(module->val[tabNow()][PatternModule::TV_MODE].load())
			!= MODE_HARMONY;
	}

	void onSelectText(const SelectTextEvent& e) override {
		if (!usable()) {
			e.consume(this);
			return;
		}
		const char c = (char) e.codepoint;
		const bool ok = speeds
			? ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == ' ')
			: ((c >= '0' && c <= '9') || c == '.' || c == ' ');
		if (!ok) {
			e.consume(this);
			return;
		}
		LedDisplayTextField::onSelectText(e);
		commit();
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (!usable()) {
			e.consume(this);
			return;
		}
		LedDisplayTextField::onSelectKey(e);
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT)
			commit();
	}

	void commit() {
		if (!module)
			return;
		const int t = tabNow();
		const int chosen = module->chosenMask();
		if (speeds) {
			// HELD UNTIL IT PARSES. A list being typed passes through states that are not lists,
			// and committing those would stop the pattern mid-keystroke.
			bool any = false;
			for (char c : text)
				if (c >= '0' && c <= '9')
					any = true;
			if (!any)
				return;
			if (!text.empty() && (text[text.size() - 1] == '-' || text[text.size() - 1] == '.'))
				return;
			// EVERY SELECTED TAB. Typing one number into the speeds with all the tabs selected is
			// how they are all put back to the same speed, which is the point of the ALL button.
			for (int i = 0; i < MAX_TABS; i++)
				if (chosen & (1 << i))
					module->text[i].speeds = text;
		}
		else {
			for (int i = 0; i < MAX_TABS; i++)
				if (chosen & (1 << i))
					module->text[i].strength = text;
		}
		(void) t;
		module->wantCompile.store(true);
	}

	void draw(const DrawArgs& args) override {
		color = usable() ? GRID_TEXT : nvgRGB(0x86, 0x90, 0x9c);
		LedDisplayTextField::draw(args);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 1.f, 1.f, box.size.x - 2.f, box.size.y - 2.f, 2.f);
		nvgStrokeColor(args.vg, usable() ? GRID_FRAME : nvgRGB(0x5a, 0x63, 0x6e));
		nvgStrokeWidth(args.vg, 2.f);
		nvgStroke(args.vg);
	}

	void step() override {
		const bool typing = (APP->event->getSelectedWidget() == this);
		if (module && !typing) {
			const std::string& want = speeds ? module->text[tabNow()].speeds
				: module->text[tabNow()].strength;
			if (text != want)
				text = want;
		}
		// THE GRID'S FACE, not Rack's: the two sit one above the other and a different face in
		// each would read as two unrelated controls.
		fontPath = asset::plugin(pluginInstance, "res/Hack-Regular.ttf");
		LedDisplayTextField::step();
	}
};


/** THE PANEL. THE GRID SETS THE WIDTH and nothing else is allowed to widen it: the box runs the
width of the face, and the rest of the controls fit underneath. Four bars of four beats is twenty
characters, which at twenty-four pixels is the whole of twenty-four HP. A longer bar keeps the
same four bars and takes a smaller type instead — see GRID_PX_MAX. */
static const float PANEL_HP = 19.f;
static const float PANEL_W = PANEL_HP * 5.08f;

static Layout patternLayout() {
	Layout L;
	L.hp = PANEL_HP;
	L.title = "mpxPattern";

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			float size = 0.f, const std::string& owner = "",
			Panel::Align align = Panel::RIGHT) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = true; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};

	/** A READOUT IS THE DROP-DOWN. It shows the value it holds and opens a list of every value
	over the top of itself, which is the control this panel wants everywhere a choice is made. */
	auto plate = [&](const std::string& key, float x, float y, int id, int chars,
			const std::string& name, const std::vector<std::string>& notes = {}) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "readout";
		i.x = x; i.y = y; i.chars = chars; i.h = 3.4f; i.notes = notes;
		L.items.push_back(i);
		// RIGHT-ALIGNED AND HUGGING ITS FIELD: a label in a column away from what it names makes
		// the reader measure across the panel to pair them up. The gap is from the plate's own
		// edge, which is half of what Readout::setFigures works its width out as — a figure
		// advances 0.8537 of its height, and the plate is the figures and a millimetre.
		const float half = (float) chars * 3.4f * (FIGURE_ADVANCE / FIGURE_CAP) / 2.f + 0.5f;
		label(key + ".label", x - half - 2.f, y, name, 0.f, key, Panel::RIGHT);
	};

	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor ring) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = ring;
		L.items.push_back(i);
		label(key + ".label", x, y - 6.2f, name, 0.f, key, Panel::CENTRE);
	};

	auto display = [&](const std::string& key, float x, float y, float w, float h) {
		Item i;
		i.key = key; i.kind = Item::DISPLAY; i.x = x; i.y = y; i.w = w; i.h = h;
		L.items.push_back(i);
	};

	display("d.tabs", 2.f, 12.f, PANEL_W - 4.f, 6.f);

	// TWO ROWS, because the panel is as wide as the grid and no wider: the five plates and their
	// names do not fit across it. The first row is what every pattern type uses; the second is
	// the two only Euclidean uses, which is also why they sit together and below.
	//
	// "TYPE" RATHER THAN "PATTERN TYPE". A panel label is Hack at ten pixels, and Hack advances
	// 0.602 of its size, so a character is two millimetres and the longer wording is twenty-four
	// millimetres on its own — a quarter of this face for one name. The box below is labelled
	// PATTERN, so TYPE beside the plate says the same thing in a third of the room.
	plate("p.mode", 24.9f, 23.5f, PatternModule::P_MODE, 9, "TYPE",
		{MODE_NOTES[0], MODE_NOTES[1], MODE_NOTES[2]});
	plate("p.measures", 59.7f, 23.5f, PatternModule::P_MEASURES, 1, "MEASURES");
	plate("p.phrases", 81.3f, 23.5f, PatternModule::P_PHRASES, 1, "PHRASES");
	plate("p.beats", 32.5f, 31.5f, PatternModule::P_EUC_BEATS, 2, "ACTIVE BEATS");
	plate("p.shift", 54.5f, 31.5f, PatternModule::P_EUC_SHIFT, 3, "SHIFT");

	label("d.grid.label", 2.f, 38.5f, "PATTERN", 0.f, "", Panel::LEFT);
	display("d.grid", 2.f, 40.5f, PANEL_W - 4.f, 63.5f);

	// THE TWO FIELDS AT THE FOOT, between the jacks rather than above them, which is what puts
	// the whole of the panel between the tabs and here into the grid.
	display("d.strength", 39.5f, 108.5f, 46.f, 6.6f);
	label("d.strength.label", 38.f, 111.8f, "BEAT STRENGTH", 0.f, "", Panel::RIGHT);
	display("d.speeds", 39.5f, 117.5f, 34.f, 6.6f);
	label("d.speeds.label", 38.f, 120.8f, "CYCLE SPEEDS", 0.f, "", Panel::RIGHT);

	jack("in.chart", Item::PORT_IN, 6.5f, 121.f, PatternModule::I_CHART, "mpx\nIN", NOTE_CABLE);
	jack("out.notes", Item::PORT_OUT, 90.5f, 121.f, PatternModule::O_NOTES, "mpx\nOUT",
		NOTE_CABLE);

	Item lamp;
	lamp.key = "lamp.step"; lamp.kind = Item::LIGHT; lamp.id = PatternModule::L_STEP;
	lamp.x = 80.f; lamp.y = 119.f;
	L.items.push_back(lamp);

	L.bindOffsets();
	return L;
}


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

		TabStrip* tabs = new TabStrip;
		tabs->module = module;
		layoutPlaceDisplay(this, layout, "d.tabs", tabs);

		GridBox* grid = new GridBox;
		grid->module = module;
		layoutPlaceDisplay(this, layout, "d.grid", grid);

		ShortField* strength = new ShortField;
		strength->module = module;
		strength->speeds = false;
		if (module)
			strength->text = module->text[0].strength;
		layoutPlaceDisplay(this, layout, "d.strength", strength);

		ShortField* speeds = new ShortField;
		speeds->module = module;
		speeds->speeds = true;
		if (module)
			speeds->text = module->text[0].speeds;
		layoutPlaceDisplay(this, layout, "d.speeds", speeds);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxPattern");
	}

	void step() override {
		ModuleWidget::step();
		PatternModule* m = dynamic_cast<PatternModule*>(module);
		if (!m)
			return;
		m->tick();
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
	}
};


} // namespace px

Model* modelMpxPattern = createModel<px::PatternModule, px::PatternWidget>("mpxPattern");
