/** mpxPathSpritePlayer — eight sprites running drawn paths, playing the notes a rhythm asks for.

THE OTHER HALF OF mpxPathSprites. That one turns what a sprite is passing over into a voltage;
this one turns it into a note. They share the picture, the eight paths and the drawing, and
nothing else: an instrument that made both would be two instruments in one panel.

WHAT IT DOES. mpxPattern says WHEN — its beats arrive carrying the tab they belong to — and this
says WHAT. When a beat arrives for a sprite, that sprite reads the picture wherever it has got to
along its path and sounds a note: the pitch from the chord sounding at that moment, placed within
a register by what it read; the weight of the beat from the pattern; and the note dropped
altogether where the picture is too dull to be worth sounding.

WHY A PATH AND NOT A FIELD. mpxSprites already takes its rhythm from mpxPattern and reads a
picture, and what it cannot do is come back to the same place. A sprite on a drawn path does: it
is where the music has reached that decides how far along it is, so after `Repeats` passes of the
looped section it has gone exactly once round, and the same bar of the same pass reads the same
part of the picture every time. That recurrence is the whole of why this module exists.

PITCH IS CHORD-TONE INDEXING, not snapping to the nearest. The chord's tones are laid out
ascending across the register and the reading picks one by its POSITION in that list — nought the
lowest, one the highest, the rest spread evenly between. Snapping a pitch to the nearest chord
tone makes a line lumpy and different under every chord, because a triad and a thirteenth leave
different gaps; indexing keeps the shape, so the same path gives the same melodic contour whatever
the harmony underneath is doing. This is how the application this comes from does it, and it is
most of why its lines sound like lines.

See docs/path-sprites.md.
*/
#include "plugin.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"
#include "Chord.hpp"
#include "PathSprites.hpp"

#include <osdialog.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace px {


/** WHAT THE READING DRIVES. Pitch always; the other two take it or leave it. */
enum PlayerReading {
	PR_BRIGHT, PR_SAT, PR_HUE_RC, PR_HUE_YB, PR_RED, PR_GREEN, PR_BLUE, NUM_PLAYER_READINGS
};
static const char* PLAYER_READING_NAMES[NUM_PLAYER_READINGS] = {
	"Brightness", "Saturation", "Hue R-C", "Hue Y-B", "Red", "Green", "Blue"
};
static int playerReadingFor(int which) {
	switch (which) {
		case PR_SAT:    return READ_SAT;
		case PR_HUE_RC: return READ_HUE_RC;
		case PR_HUE_YB: return READ_HUE_YB;
		case PR_RED:    return READ_RED;
		case PR_GREEN:  return READ_GREEN;
		case PR_BLUE:   return READ_BLUE;
		default: break;
	}
	return READ_VALUE;
}


struct PlayerModule : Module, NoteSource, NoteSink, PathHost {
	enum ParamId {
		P_KIND, P_DIAMETER, P_SOURCE, P_REPEATS, P_DROP,
		P_REGISTER, P_RANGE, P_LENGTH,
		NUM_PARAMS,
	};
	enum InputId { I_CHART, NUM_INPUTS };
	enum OutputId { O_NOTES, NUM_OUTPUTS };
	enum LightId { NUM_LIGHTS };

	enum TabValue { TV_ON, TV_KIND, TV_DIAMETER, TV_SOURCE, TV_REPEATS, TV_DROP,
		TV_REGISTER, TV_RANGE, TV_LENGTH, NUM_TV };

	PlayerModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(P_KIND, 0.f, 1.f, 1.f, "Path", {"Line", "Circle"});
		configParam(P_DIAMETER, 0.02f, 1.4f, 0.3f, "Diameter", " of the picture", 0.f, 1.f);
		{
			std::vector<std::string> names;
			for (int i = 0; i < NUM_PLAYER_READINGS; i++)
				names.push_back(PLAYER_READING_NAMES[i]);
			configSwitch(P_SOURCE, 0.f, (float) (NUM_PLAYER_READINGS - 1), 0.f, "Reads", names);
		}
		// HOW MANY TRAVERSALS OF THE PATTERN ONE TRIP ROUND THE PATH TAKES. One is the plain
		// case and the one to start from: the rhythm comes round and the path comes round with
		// it. Above one, each traversal reads a different stretch of the picture, so the same
		// typed rhythm returns with different notes until the trip closes.
		configParam(P_REPEATS, 1.f, 16.f, 1.f, "Repeats", " traversals a trip");
		getParamQuantity(P_REPEATS)->snapEnabled = true;
		configParam(P_DROP, 0.f, 1.f, 0.f, "Drop", "%", 0.f, 100.f);
		configParam<NoteQuantity>(P_REGISTER, 24.f, 96.f, 60.f, "Register");
		configParam(P_RANGE, 2.f, 48.f, 19.f, "Range", " semitones");
		getParamQuantity(P_RANGE)->snapEnabled = true;
		configParam(P_LENGTH, 0.05f, 2.f, 0.9f, "Length", "% of the beat", 0.f, 100.f);
		configInput(I_CHART, "MPX rhythm and chart");
		configOutput(O_NOTES, "MPX note");

		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		slot = busClaim(&generation);

		for (int i = 0; i < MAX_PATHS; i++) {
			val[i][TV_ON].store(i == 0 ? 1.f : 0.f);
			val[i][TV_KIND].store(1.f);
			val[i][TV_DIAMETER].store(0.3f + 0.07f * (float) i);
			val[i][TV_SOURCE].store(0.f);
			val[i][TV_REPEATS].store(1.f);
			val[i][TV_DROP].store(0.f);
			val[i][TV_REGISTER].store(60.f - 7.f * (float) i);
			val[i][TV_RANGE].store(19.f);
			val[i][TV_LENGTH].store(0.9f);
			path[i].cx = path[i].cy = 0.5f;
			path[i].diameter = 0.3f + 0.07f * (float) i;
			path[i].moved();
		}
	}

	~PlayerModule() { busRelease(slot); }

	int busSlotFor(int outputId, uint32_t* gen) override {
		if (outputId != O_NOTES)
			return -1;
		if (gen)
			*gen = generation;
		return slot;
	}
	bool isMPXInputId(int inputId) override { return inputId == I_CHART; }

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

	// ---- the tabs ----------------------------------------------------------------------------

	std::atomic<float> val[MAX_PATHS][NUM_TV];
	PathTable table[MAX_PATHS];

	bool enabled(int i) const {
		return (i >= 0 && i < MAX_PATHS) && val[i][TV_ON].load() > 0.5f;
	}
	bool pathOn(int i) const override { return enabled(i); }

	int showing = 0;
	bool loaded = false;

	static int paramOf(int v) {
		switch (v) {
			case TV_KIND: return P_KIND;
			case TV_DIAMETER: return P_DIAMETER;
			case TV_SOURCE: return P_SOURCE;
			case TV_REPEATS: return P_REPEATS;
			case TV_DROP: return P_DROP;
			case TV_REGISTER: return P_REGISTER;
			case TV_RANGE: return P_RANGE;
			case TV_LENGTH: return P_LENGTH;
			default: break;
		}
		return -1;
	}

	void followTab() {
		const int flip = flipEnable.exchange(-1);
		if (flip >= 0 && flip < MAX_PATHS)
			val[flip][TV_ON].store(enabled(flip) ? 0.f : 1.f);
		const int want = frontTab();
		if (loaded && want == showing) {
			const int chosen = chosenMask();
			for (int v = 0; v < NUM_TV; v++) {
				const int p = paramOf(v);
				if (p < 0)
					continue;
				const float now = params[p].getValue();
				if (val[showing][v].load() == now)
					continue;
				for (int t = 0; t < MAX_PATHS; t++)
					if (chosen & (1 << t))
						val[t][v].store(now);
			}
			return;
		}
		if (loaded) {
			for (int v = 0; v < NUM_TV; v++)
				if (paramOf(v) >= 0)
					val[showing][v].store(params[paramOf(v)].getValue());
		}
		showing = want;
		for (int v = 0; v < NUM_TV; v++)
			if (paramOf(v) >= 0)
				params[paramOf(v)].setValue(val[showing][v].load());
		loaded = true;
	}

	/** Main thread, from the panel: the paths follow their settings and the tables are rebuilt
	when the picture, a path or what it reads has changed. Never on the audio thread. */
	void tick() {
		for (int i = 0; i < MAX_PATHS; i++) {
			const int kind = (val[i][TV_KIND].load() > 0.5f) ? Path::CIRCLE : Path::LINE;
			const float d = val[i][TV_DIAMETER].load();
			if (path[i].kind != kind || std::fabs(path[i].diameter - d) > 1e-6f) {
				path[i].kind = kind;
				path[i].diameter = d;
				path[i].moved();
			}
			table[i].refresh(picture, path[i],
				playerReadingFor((int) std::lround(val[i][TV_SOURCE].load())));
		}
	}

	// ---- playing -----------------------------------------------------------------------------

	struct Beat {
		bool waiting = false;
		float level = 1.f;
		float seconds = 0.5f;
	};
	Beat beat[MAX_PATHS];
	int64_t noteHandle[MAX_PATHS] = {};
	float noteEndIn[MAX_PATHS] = {-1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f};
	int64_t swallowed[32] = {};
	int swallowedAt = 0;

	float secondsPerBeat = 0.5f;
	double lastBeat = -1.0;
	double lastBeatTime = 0.0;
	double clock = 0.0;

	bool swallowedHandle(int64_t handle) {
		for (int i = 0; i < 32; i++) {
			if (swallowed[i] == handle) {
				swallowed[i] = 0;
				return true;
			}
		}
		return false;
	}

	/** An event from upstream: taken as a beat for one of the sprites, or passed along.

	THE BEAT IS SWALLOWED. The rhythm's note carries no pitch — it is a place in time — so letting
	it through as well as the note this module makes would sound it downstream as whatever a voice
	makes of a pitchless note, which is one fixed note underneath everything. */
	void takeOrPass(const Event& e) {
		const int i = (int) e.part - 1;
		if (i >= 0 && i < MAX_PATHS) {
			if (e.kind == Event::ON) {
				beat[i].waiting = true;
				beat[i].level = e.level;
				beat[i].seconds = (e.duration > 0.f) ? e.duration : 0.5f;
				swallowed[swallowedAt++ % 32] = e.handle;
				return;
			}
			if (e.kind == Event::OFF && swallowedHandle(e.handle))
				return;
		}
		busPush(slot, e);
	}

	void endNote(int i) {
		if (noteHandle[i] != 0 && slot >= 0) {
			Event off;
			off.kind = Event::OFF;
			off.handle = noteHandle[i];
			busPush(slot, off);
		}
		noteHandle[i] = 0;
		noteEndIn[i] = -1.f;
	}

	/** THE PITCH: the chord's tones laid across the register, picked by position.

	CHORD-TONE INDEXING AND NOT SNAPPING. The tones available between the bottom and top of the
	register are listed ascending, and the reading chooses one by where it falls in that LIST —
	nought the lowest, one the highest. Snapping instead would make the same path give a different
	shape under every chord, because a triad and a thirteenth leave different gaps; this way the
	shape is the path's and the notes are the chord's. */
	float pitchFor(int i, float read, const Harmony& h) {
		const float centre = val[i][TV_REGISTER].load();
		const float span = val[i][TV_RANGE].load();
		const int lo = (int) std::ceil(centre - span * 0.5f);
		const int hi = (int) std::floor(centre + span * 0.5f);

		int pcs[16];
		int n = 0;
		if (h.valid)
			n = chordPitchClasses(h.current, h.key, pcs);
		if (n <= 0 && h.pitchesValid) {
			// WHAT IS SOUNDING, strongest first, the weak ones left out: a score has no chord
			// symbols and the weights already say which notes carry the harmony.
			std::pair<int, int> byWeight[12];
			int found = 0;
			for (int pc = 0; pc < 12; pc++)
				if (h.pitchWeight[pc] > 0)
					byWeight[found++] = std::make_pair(-(int) h.pitchWeight[pc], pc);
			std::sort(byWeight, byWeight + found);
			for (int k = 0; k < found && n < 16; k++) {
				if (-byWeight[k].first < 64 && n >= 3)
					break;
				pcs[n++] = byWeight[k].second;
			}
		}
		if (n <= 0 && h.keyValid) {
			static const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
			static const int MINOR[7] = {0, 2, 3, 5, 7, 8, 10};
			const int* steps = h.key.minor ? MINOR : MAJOR;
			for (int k = 0; k < 7; k++)
				pcs[n++] = (steps[k] + h.key.tonic + 120) % 12;
		}
		if (n <= 0) {
			static const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
			for (int k = 0; k < 7; k++)
				pcs[n++] = MAJOR[k];
		}

		// EVERY NOTE OF THE SET INSIDE THE REGISTER, ascending.
		int notes[128];
		int count = 0;
		for (int m = lo; m <= hi && count < 128; m++) {
			if (m < 0 || m > 127)
				continue;
			const int pc = ((m % 12) + 12) % 12;
			for (int k = 0; k < n; k++) {
				if (pcs[k] == pc) {
					notes[count++] = m;
					break;
				}
			}
		}
		if (count <= 0)
			return (centre - 60.f) / 12.f;
		const float where = math::clamp(read, 0.f, 1.f) * (float) (count - 1);
		const int midi = notes[math::clamp((int) std::lround(where), 0, count - 1)];
		return ((float) midi - 60.f) / 12.f;
	}

	void fire(int i, const Harmony& h) {
		if (slot < 0)
			return;
		const double where = at[i];
		const float read = table[i].ready() ? table[i].read(where, where) : 0.5f;

		// DROP: a beat over a dull part of the picture is not played at all. A rhythm that sounds
		// every step regardless of what is underneath it is a rhythm the picture is not in.
		const float drop = val[i][TV_DROP].load();
		if (drop > 0.001f && read < drop) {
			endNote(i);
			return;
		}

		endNote(i);
		Event e;
		e.kind = Event::ON;
		e.handle = noteHandle[i] = mintHandle();
		e.pitch = pitchFor(i, read, h);
		e.level = math::clamp(beat[i].level, 0.f, 1.f);
		const float length = val[i][TV_LENGTH].load() * beat[i].seconds;
		e.duration = math::clamp(length, 0.005f, 30.f);
		// WHERE THE SPRITE IS ACROSS THE PICTURE IS WHERE THE NOTE IS, as in mpxSprites: no
		// control and no setting, because a sprite over there should sound over there.
		float x = 0.f, y = 0.f;
		path[i].at(where, &x, &y);
		e.pan = math::clamp(x, 0.f, 1.f) * 2.f - 1.f;
		e.part = (uint8_t) (i + 1);
		busPush(slot, e);
		noteEndIn[i] = e.duration;
	}

	void process(const ProcessArgs& args) override {
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}
		followTab();

		outputs[O_NOTES].setChannels(1);
		outputs[O_NOTES].setVoltage(busFlashVolts(slot));

		Event in;
		while (reader.next(in))
			takeOrPass(in);

		Harmony h;
		const bool haveHarmony = reader.harmony(h);
		if (haveHarmony)
			busPublishHarmony(slot, h);
		ChordVoicing v;
		if (reader.voicing(v))
			busPublishVoicing(slot, v);
		PartMotion sp;
		const bool haveMotion = reader.motion(sp);
		{
			float sustain = 0.f, soft = 0.f;
			reader.pedals(sustain, soft);
			busPublishPedals(slot, sustain, soft);
		}

		double beatStep = 0.0;
		if (haveHarmony && (h.valid || h.timingValid)) {
			clock += args.sampleTime;
			if (lastBeat >= 0.0 && h.beat > lastBeat && h.beat - lastBeat < 1.0) {
				const double seconds = clock - lastBeatTime;
				const double beats = h.beat - lastBeat;
				if (seconds > 0.0 && beats > 0.0)
					secondsPerBeat += (math::clamp((float) (seconds / beats), 0.02f, 4.f)
						- secondsPerBeat) * 0.05f;
				beatStep = h.beat - lastBeat;
			}
			lastBeat = h.beat;
			lastBeatTime = clock;
		}

		// WHERE EACH SPRITE IS, IN MUSICAL TIME.
		//
		// ONE TRAVERSAL OF THE PART'S PATTERN IS ONE TRIP ROUND THE PATH. That is what ties the
		// two together: the rhythm comes round and the path comes round with it, so the same beat
		// of the pattern reads the same part of the picture every time it is played.
		//
		// THE PATTERN'S LENGTH AND NOT THE CHART'S. This was the chart's loop length, from the
		// design note's "Repeats passes of the looped section", and it is wrong: a pattern's
		// length is its own — its measures times its phrases — and need not divide the chart's at
		// all. Tied to the chart, a pattern of three bars against a loop of four walked the path
		// at a rate that had nothing to do with the rhythm anybody typed.
		//
		// REPEATS IS THEN HOW MANY TRAVERSALS MAKE ONE TRIP, which at one is the plain case and
		// above it spreads the material over a longer journey so that each traversal reads a
		// different stretch of the picture.

		for (int i = 0; i < MAX_PATHS; i++) {
			if (!enabled(i)) {
				endNote(i);
				beat[i].waiting = false;
				continue;
			}
			if (noteEndIn[i] >= 0.f) {
				noteEndIn[i] -= args.sampleTime;
				if (noteEndIn[i] <= 0.f)
					endNote(i);
			}

			const float repeats = std::fmax(1.f, val[i][TV_REPEATS].load());
			// HOW LONG THE PART IS, from the cable. With nothing saying — no pattern patched, or
			// one whose tab is off — a bar of four is assumed, so a sprite still moves and what
			// is wrong is obvious rather than silent.
			const float partBeats = (haveMotion && sp.beats[i] > 0.f) ? sp.beats[i] : 4.f;
			const double trip = (double) repeats * (double) partBeats;
			const float speed = (haveMotion && sp.speed[i] != 0.f) ? sp.speed[i] : 1.f;

			if (trip > 0.0)
				at[i] += beatStep * (double) speed / trip;
			at[i] -= std::floor(at[i]);

			if (beat[i].waiting) {
				beat[i].waiting = false;
				fire(i, h);
			}
		}
	}

	// ---- saving ------------------------------------------------------------------------------

	static const char* keyName(int v) {
		static const char* KEYS[NUM_TV] = {"on", "kind", "diameter", "source", "repeats",
			"drop", "register", "range", "length"};
		return KEYS[v];
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "tab", json_integer(tab.load()));
		if (!picture.file.empty()) {
			json_object_set_new(rootJ, "pictureName", json_string(picture.name.c_str()));
			json_object_set_new(rootJ, "picture",
				json_stringn((const char*) picture.file.data(), picture.file.size()));
		}
		json_t* tabsJ = json_array();
		for (int i = 0; i < MAX_PATHS; i++) {
			json_t* oneJ = json_object();
			for (int v = 0; v < NUM_TV; v++)
				json_object_set_new(oneJ, keyName(v), json_real(val[i][v].load()));
			json_object_set_new(oneJ, "ax", json_real(path[i].ax));
			json_object_set_new(oneJ, "ay", json_real(path[i].ay));
			json_object_set_new(oneJ, "bx", json_real(path[i].bx));
			json_object_set_new(oneJ, "by", json_real(path[i].by));
			json_object_set_new(oneJ, "cx", json_real(path[i].cx));
			json_object_set_new(oneJ, "cy", json_real(path[i].cy));
			json_array_append_new(tabsJ, oneJ);
		}
		json_object_set_new(rootJ, "tabs", tabsJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		if (json_t* j = json_object_get(rootJ, "tab")) {
			tab.store(math::clamp((int) json_integer_value(j), 0, MAX_PATHS - 1));
			selection.store(1 << tab.load());
		}
		if (json_t* j = json_object_get(rootJ, "picture")) {
			const char* s = json_string_value(j);
			const size_t n = json_string_length(j);
			if (s && n) {
				std::vector<uint8_t> bytes((const uint8_t*) s, (const uint8_t*) s + n);
				const char* nameJ = json_string_value(json_object_get(rootJ, "pictureName"));
				picture.decode(bytes, nameJ ? nameJ : "");
			}
		}
		json_t* tabsJ = json_object_get(rootJ, "tabs");
		if (json_is_array(tabsJ)) {
			for (int i = 0; i < MAX_PATHS && i < (int) json_array_size(tabsJ); i++) {
				json_t* oneJ = json_array_get(tabsJ, (size_t) i);
				if (!json_is_object(oneJ))
					continue;
				for (int v = 0; v < NUM_TV; v++)
					if (json_t* j = json_object_get(oneJ, keyName(v)))
						val[i][v].store((float) json_number_value(j));
				if (json_t* j = json_object_get(oneJ, "ax")) path[i].ax = (float) json_number_value(j);
				if (json_t* j = json_object_get(oneJ, "ay")) path[i].ay = (float) json_number_value(j);
				if (json_t* j = json_object_get(oneJ, "bx")) path[i].bx = (float) json_number_value(j);
				if (json_t* j = json_object_get(oneJ, "by")) path[i].by = (float) json_number_value(j);
				if (json_t* j = json_object_get(oneJ, "cx")) path[i].cx = (float) json_number_value(j);
				if (json_t* j = json_object_get(oneJ, "cy")) path[i].cy = (float) json_number_value(j);
				path[i].moved();
			}
		}
		// THE PARAMS ARE OURS, NOT RACK'S: Rack restores a parameter by its position in the list,
		// and everything a tab holds is saved above under its own name.
		showing = frontTab();
		for (int v = 0; v < NUM_TV; v++)
			if (paramOf(v) >= 0)
				params[paramOf(v)].setValue(val[showing][v].load());
		loaded = true;
	}
};


} // namespace px

#include "PathSpritesUI.inc"
#include "mpxPathSpritePlayerPanel.inc"

Model* modelMpxPathSpritePlayer =
	createModel<px::PlayerModule, px::PlayerWidget>("mpxPathSpritePlayer");
