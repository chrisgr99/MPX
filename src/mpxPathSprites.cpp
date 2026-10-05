/** mpxPathSprites — eight reading heads running drawn paths over a picture.

WHAT IT IS. A picture, and eight sprites that run along a line or a circle drawn on it. What each
one is passing over becomes a voltage. There is no harmony in it, no pitch and no beats: a sprite
is a reading head and the picture is the data.

A MODULATION SOURCE, AND AT SPEED AN OSCILLATOR. Run a circle round once a bar and it is an LFO
whose shape is drawn rather than chosen from a list. Run it at two hundred a second and the
picture's values become a waveform — a wavetable oscillator whose table is a circular slice of an
image, retuned by dragging the circle somewhere else.

WHY IT DOES NOT ALIAS. A reading is the MEAN over the arc the head covers in one sample, not the
value at its far end: a box filter whose width follows the rate. See Paths.hpp, which also holds
the geometry, and test/pathread.cpp, which measures it.

WHAT COMES LATER. Notes: beats arriving from mpxPattern by tab number, the pitch taken from the
chord sounding then, and the position along the path worked out from where the music has reached
so that the material recurs exactly. See docs/path-sprites.md.

WHETHER THAT IS A MODE OF THIS MODULE OR A MODULE OF ITS OWN IS OPEN, and nothing here assumes
either. What the two would share — the path geometry and its reading, the picture and its library
— is already outside this file, in Paths and Picture, so a second module costs no duplication.
*/
#include "plugin.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"
#include "PathSprites.hpp"

#include <osdialog.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace px {


/** WHERE THE SPEED COMES FROM. Free is a number of revolutions a second and is what makes this an
oscillator; the chart's beat is what makes it an LFO that stays with the music. */
enum RateFrom { RATE_FREE, RATE_BEAT, NUM_RATE_FROM };
static const char* RATE_FROM_NAMES[NUM_RATE_FROM] = {"Free", "The beat"};

/** WHAT A SPRITE READS. The picture's own numbers come from the path table; the rest are about
where the sprite IS rather than what is under it, and need no table at all. */
enum CvSource {
	CV_BRIGHT, CV_SAT, CV_HUE_RC, CV_HUE_YB, CV_RED, CV_GREEN, CV_BLUE,
	CV_ALONG, CV_ACROSS, CV_HEIGHT,
	NUM_CV_SOURCES
};
static const char* CV_SOURCE_NAMES[NUM_CV_SOURCES] = {
	"Brightness", "Saturation", "Hue R-C", "Hue Y-B", "Red", "Green", "Blue",
	"Along", "Across", "Height",
};
static const char* CV_SOURCE_NOTES[NUM_CV_SOURCES] = {
	"How light what it is passing over is",
	"How much colour it has, grey to vivid",
	"Where the colour sits between red and cyan",
	"Where the colour sits between yellow and blue",
	"How much red", "How much green", "How much blue",
	"How far along its path it is, whatever is underneath",
	"How far across the picture it is",
	"How far up the picture it is",
};

/** Whether a source is read from the picture along the path, or is about the sprite itself. */
static bool sourceUsesTable(int source) {
	return source <= CV_BLUE;
}

/** The reading a picture source needs its table built from. */
static int readingFor(int source) {
	switch (source) {
		case CV_SAT:    return READ_SAT;
		case CV_HUE_RC: return READ_HUE_RC;
		case CV_HUE_YB: return READ_HUE_YB;
		case CV_RED:    return READ_RED;
		case CV_GREEN:  return READ_GREEN;
		case CV_BLUE:   return READ_BLUE;
		default: break;
	}
	return READ_VALUE;
}


struct PathSpritesModule : Module, NoteSink, PathHost {
	enum ParamId {
		P_KIND, P_DIAMETER, P_SOURCE, P_RATE, P_RATE_FROM, P_LEVEL, P_OFFSET,
		P_RUN,
		// APPENDED, NEVER INSERTED: a patch stores a parameter by its position in this list.
		P_SMOOTH,
		NUM_PARAMS,
	};
	enum InputId { I_CHART, NUM_INPUTS };
	enum OutputId { ENUMS(O_CV, MAX_PATHS), O_POLY, NUM_OUTPUTS };
	enum LightId { NUM_LIGHTS };

	/** THE SETTINGS A TAB OWNS. The params above show whichever tab is in front; these are where
	the other seven live while they are not. The same arrangement as mpxSprites and mpxPattern. */
	enum TabValue { TV_ON, TV_KIND, TV_DIAMETER, TV_SOURCE, TV_RATE, TV_RATE_FROM,
		TV_LEVEL, TV_OFFSET, TV_SMOOTH, NUM_TV };

	PathSpritesModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(P_KIND, 0.f, 1.f, 1.f, "Path", {"Line", "Circle"});
		configParam(P_DIAMETER, 0.02f, 1.4f, 0.3f, "Diameter", " of the picture", 0.f, 1.f);
		{
			std::vector<std::string> names;
			std::vector<std::string> notes;
			for (int i = 0; i < NUM_CV_SOURCES; i++) {
				names.push_back(CV_SOURCE_NAMES[i]);
				notes.push_back(CV_SOURCE_NOTES[i]);
			}
			configSwitch(P_SOURCE, 0.f, (float) (NUM_CV_SOURCES - 1), 0.f, "Reads", names);
		}
		// A WIDE SWEEP, AND IT HAS TO BE. One end is a revolution every half a minute, which is a
		// slow sweep across a picture; the other is two hundred a second, which is a tone. A knob
		// covering both can only do it as a power, so the middle of its travel is about a cycle a
		// second and each quarter turn is roughly a factor of twenty.
		configParam(P_RATE, -5.f, 7.7f, 0.f, "Rate");
		configSwitch(P_RATE_FROM, 0.f, (float) (NUM_RATE_FROM - 1), 0.f, "Rate from",
			{RATE_FROM_NAMES[0], RATE_FROM_NAMES[1]});
		configParam(P_LEVEL, -10.f, 10.f, 5.f, "Level", " V");
		configParam(P_OFFSET, -10.f, 10.f, 0.f, "Offset", " V");
		configSwitch(P_RUN, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
		// SMOOTHING, WHICH IS NOT THE SAME AS THE ANTI-ALIASING. The arc average removes what the
		// rate cannot carry; this removes what the PICTURE has and the ear does not want — the
		// grain of a photograph, a speck of dust, the hard edge of a window frame. One is set by
		// the rate and is not a choice; this is a choice, because how much of a picture's detail
		// belongs in the sound is a musical question and not a technical one.
		configParam(P_SMOOTH, 0.f, 1.f, 0.f, "Smoothing", "%", 0.f, 100.f);
		configInput(I_CHART, "MPX chart");
		for (int i = 0; i < MAX_PATHS; i++)
			configOutput(O_CV + i, string::f("Sprite %d", i + 1));
		configOutput(O_POLY, "All eight");

		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		for (int i = 0; i < MAX_PATHS; i++) {
			val[i][TV_ON].store(i == 0 ? 1.f : 0.f);
			val[i][TV_KIND].store(1.f);
			val[i][TV_DIAMETER].store(0.3f + 0.07f * (float) i);
			val[i][TV_SOURCE].store(0.f);
			val[i][TV_RATE].store(0.f);
			val[i][TV_RATE_FROM].store(0.f);
			val[i][TV_LEVEL].store(5.f);
			val[i][TV_OFFSET].store(0.f);
			val[i][TV_SMOOTH].store(0.f);
			path[i].cx = path[i].cy = 0.5f;
			path[i].diameter = 0.3f + 0.07f * (float) i;
			path[i].moved();
		}
	}

	bool isMPXInputId(int inputId) override {
		return inputId == I_CHART;
	}

	// ---- the cable, for the beat ---------------------------------------------------------------

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

	// ---- the picture and the paths -------------------------------------------------------------

	/** The tables are built on the main thread, from the panel's step, and read on the audio
	thread. One per sprite, since each may be reading a different thing. */
	PathTable table[MAX_PATHS];
	std::atomic<float> val[MAX_PATHS][NUM_TV];

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
			case TV_RATE: return P_RATE;
			case TV_RATE_FROM: return P_RATE_FROM;
			case TV_LEVEL: return P_LEVEL;
			case TV_OFFSET: return P_OFFSET;
			case TV_SMOOTH: return P_SMOOTH;
			default: break;
		}
		return -1;
	}

	/** Moving between tabs: the params are written back to the tab being left, then loaded from
	the one arrived at. A change made while a tab is in front reaches every selected tab. */
	void followTab() {
		const int flip = flipEnable.exchange(-1);
		if (flip >= 0 && flip < MAX_PATHS)
			val[flip][TV_ON].store(enabled(flip) ? 0.f : 1.f);

		const int want = math::clamp(tab.load(), 0, MAX_PATHS - 1);
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

	/** Main thread, from the panel's step: the paths follow their settings, and a table is rebuilt
	when the picture, the path or what it reads has changed. Never on the audio thread — building
	one walks a thousand pixels. */
	void tick() {
		for (int i = 0; i < MAX_PATHS; i++) {
			const int kind = (val[i][TV_KIND].load() > 0.5f) ? Path::CIRCLE : Path::LINE;
			const float d = val[i][TV_DIAMETER].load();
			if (path[i].kind != kind || std::fabs(path[i].diameter - d) > 1e-6f) {
				path[i].kind = kind;
				path[i].diameter = d;
				path[i].moved();
			}
			const int source = (int) std::lround(val[i][TV_SOURCE].load());
			if (sourceUsesTable(source))
				table[i].refresh(picture, path[i], readingFor(source));
		}
	}

	// ---- running -------------------------------------------------------------------------------

	/** The smoothed reading, one a sprite: a single pole, which is the gentlest thing that is
	still a filter and leaves no ringing on a step. */
	float held[MAX_PATHS] = {};
	float secondsPerBeat = 0.5f;
	double lastBeat = -1.0;
	double lastBeatTime = 0.0;
	double clock = 0.0;

	/** Revolutions a second, from the knob. A power, so one knob covers a sweep taking half a
	minute and a tone at two hundred a second. */
	static float revolutionsPerSecond(float knob) {
		return std::pow(2.f, knob) * 0.03125f;
	}

	void process(const ProcessArgs& args) override {
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}
		followTab();

		// THE BEAT, FOR THE SPRITES THAT FOLLOW IT. Taken from the chart's own beat rather than
		// from a tempo setting, so a sprite locked to the beat stays with it while it changes.
		Harmony h;
		const bool haveHarmony = reader.harmony(h);
		bool running = params[P_RUN].getValue() > 0.5f;
		if (haveHarmony && (h.valid || h.timingValid)) {
			clock += args.sampleTime;
			if (lastBeat >= 0.0 && h.beat > lastBeat && h.beat - lastBeat < 1.0) {
				const double seconds = clock - lastBeatTime;
				const double beats = h.beat - lastBeat;
				if (seconds > 0.0 && beats > 0.0)
					secondsPerBeat += (math::clamp((float) (seconds / beats), 0.02f, 4.f)
						- secondsPerBeat) * 0.05f;
			}
			lastBeat = h.beat;
			lastBeatTime = clock;
			if (h.holding)
				running = false;
		}

		float poly[MAX_PATHS] = {};
		for (int i = 0; i < MAX_PATHS; i++) {
			if (!enabled(i)) {
				outputs[O_CV + i].setVoltage(0.f);
				poly[i] = 0.f;
				continue;
			}

			// HOW FAR IT MOVES THIS SAMPLE, as a fraction of its path.
			const int from = (int) std::lround(val[i][TV_RATE_FROM].load());
			float rev = revolutionsPerSecond(val[i][TV_RATE].load());
			if (from == RATE_BEAT) {
				// THE KNOB IS THEN REVOLUTIONS PER BEAT rather than per second, so a sprite stays
				// with the music whatever the tempo. The same power curve, so the knob reads the
				// same way: a revolution every thirty-two beats at the bottom.
				rev = rev / std::fmax(0.02f, secondsPerBeat);
			}
			const double step = running ? (double) rev * (double) args.sampleTime : 0.0;

			const double was = at[i];
			double now = was + step;
			const int kind = (val[i][TV_KIND].load() > 0.5f) ? Path::CIRCLE : Path::LINE;
			if (kind == Path::CIRCLE) {
				if (now >= 1.0)
					now -= std::floor(now);
			}
			else if (now >= 1.0) {
				// BACK TO THE START AND THE SAME WAY AGAIN, never reversed: every sweep is in the
				// same order, so what is heard recurs rather than coming back inside out.
				now -= std::floor(now);
			}
			at[i] = now;

			const int source = (int) std::lround(val[i][TV_SOURCE].load());
			float read = 0.f;
			if (sourceUsesTable(source)) {
				// THE MEAN OVER THE ARC COVERED, which is what keeps a fast sprite clean. `was`
				// and the unwrapped end, so an arc that crosses the start of a circle is still
				// one arc.
				//
				// HALF, NOT NOUGHT, WHERE THERE IS NO TABLE — no picture yet, or one still being
				// read. Half is the middle of the range and leaves the output sitting on its
				// offset, which is plainly nothing happening; nought would be a definite voltage
				// a long way from centre and would look like a reading.
				read = table[i].ready() ? table[i].read(was, was + step) : 0.5f;
			}
			else {
				float x = 0.f, y = 0.f;
				path[i].at(now, &x, &y);
				read = (source == CV_ALONG) ? (float) now
					: (source == CV_ACROSS) ? math::clamp(x, 0.f, 1.f)
					: math::clamp(1.f - y, 0.f, 1.f);
			}

			// SMOOTHED, IF IT IS ASKED FOR. The knob is a time, not a fraction: squared, up to a
			// third of a second, so the bottom half of its travel takes the grain off and the top
			// of it turns a picture into a slow drift.
			const float smooth = val[i][TV_SMOOTH].load();
			if (smooth > 0.001f) {
				const float tau = smooth * smooth * 0.33f;
				const float k = 1.f - std::exp(-args.sampleTime / std::fmax(1e-5f, tau));
				held[i] += (read - held[i]) * k;
				read = held[i];
			}
			else {
				held[i] = read;
			}

			// A READING IS NOUGHT TO ONE; what leaves is whatever the thing downstream wants.
			// Centred on the offset so that a level turned down leaves it where it was rather
			// than dragging it to nothing.
			// LEVEL IS THE PEAK, so five gives a swing of five volts either side of the offset
			// and a reading of a half sits exactly on it.
			const float v = math::clamp(
				val[i][TV_OFFSET].load() + (read - 0.5f) * 2.f * val[i][TV_LEVEL].load(),
				-10.f, 10.f);
			outputs[O_CV + i].setVoltage(v);
			poly[i] = v;
		}

		outputs[O_POLY].setChannels(MAX_PATHS);
		for (int i = 0; i < MAX_PATHS; i++)
			outputs[O_POLY].setVoltage(poly[i], i);
	}

	// ---- saving ---------------------------------------------------------------------------------

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
			static const char* KEYS[NUM_TV] = {"on", "kind", "diameter", "source", "rate",
				"rateFrom", "level", "offset", "smooth"};
			for (int v = 0; v < NUM_TV; v++)
				json_object_set_new(oneJ, KEYS[v], json_real(val[i][v].load()));
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
		if (json_t* v = json_object_get(rootJ, "tab")) {
			tab.store(math::clamp((int) json_integer_value(v), 0, MAX_PATHS - 1));
			selection.store(1 << tab.load());
		}
		if (json_t* v = json_object_get(rootJ, "picture")) {
			const char* s = json_string_value(v);
			const size_t n = json_string_length(v);
			if (s && n) {
				std::vector<uint8_t> bytes((const uint8_t*) s, (const uint8_t*) s + n);
				const char* nameJ = json_string_value(json_object_get(rootJ, "pictureName"));
				picture.decode(bytes, nameJ ? nameJ : "");
			}
		}
		json_t* tabsJ = json_object_get(rootJ, "tabs");
		if (json_is_array(tabsJ)) {
			static const char* KEYS[NUM_TV] = {"on", "kind", "diameter", "source", "rate",
				"rateFrom", "level", "offset", "smooth"};
			for (int i = 0; i < MAX_PATHS && i < (int) json_array_size(tabsJ); i++) {
				json_t* oneJ = json_array_get(tabsJ, (size_t) i);
				if (!json_is_object(oneJ))
					continue;
				for (int v = 0; v < NUM_TV; v++)
					if (json_t* j = json_object_get(oneJ, KEYS[v]))
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
		// THE PARAMS ARE OURS, NOT RACK'S. Rack restores a parameter by its position in the list,
		// so a patch saved against an older version of this module would otherwise put that
		// version's values into whatever now occupies those places. Everything a tab holds is
		// saved above under its own name.
		showing = math::clamp(tab.load(), 0, MAX_PATHS - 1);
		for (int v = 0; v < NUM_TV; v++)
			if (paramOf(v) >= 0)
				params[paramOf(v)].setValue(val[showing][v].load());
		loaded = true;
	}
};


} // namespace px

#include "PathSpritesUI.inc"
#include "mpxPathSpritesPanel.inc"

Model* modelMpxPathSprites =
	createModel<px::PathSpritesModule, px::PathSpritesWidget>("mpxPathSprites");
