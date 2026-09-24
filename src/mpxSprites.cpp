/** mpxSprites — notes from things moving over a picture.

A picture is loaded and sprites move over it, pushed about by a force field read from its colours,
each sprite sending a note when its own timer fires. The picture is on the panel and the sprites
are watched moving across it, which is half the point: a path that looks interesting sounds
interesting. See docs/sprites.md.

THIS IS THE THIRD STAGE: the picture, one to four sprites moving over it under a force field read
from its colours, a transport that can rewind them to retrace a path exactly, and the drift.
Nothing sounds yet: the note timers come in the stage after this.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"
#include "Picture.hpp"

#include <osdialog.h>

#include <cmath>

namespace px {


static const int MAX_SPRITES = 4;

/** HOW OFTEN THE PHYSICS RUNS, in ticks a second, and the same however the engine is set. A
sprite's motion has to be smooth whether it is sending a note twice a second or fifty times, so it
is stepped on a clock of its own rather than on anything musical. Five hundred is far more than the
eye needs and cheap enough not to matter. */
static const float PHYSICS_HZ = 500.f;

/** THE SPEED LIMIT, in square widths a second, before the speed knob scales it. A sprite crossing
the picture in about three seconds is fast enough to be interesting and slow enough to watch — and
a limit is needed anyway, since a sprite that could cross more than a wall's worth in one tick
could pass through the wall. */
static const float MAX_SPEED = 0.35f;

/** HOW HARD THE DRIFT PUSHES at the knob's full turn, as a fraction of the speed limit gained per
root second. Deliberately tiny: see the note where it is applied. */
static const float DRIFT_SCALE = 0.06f;

/** HOW HARD THE PICTURE CAN PUSH at the force knob's full turn, in square widths a second a
second. At two, a region pushing flat out takes a sprite from standing to the speed limit in about
a sixth of a second, which is enough for the picture to throw a sprite about rather than to nudge
it. */
static const float FORCE_MAX = 2.4f;

/** SLOPES ARE SMALL NUMBERS. The difference between two neighbouring pixels of a photograph is a
few parts in a hundred even at an edge, so the reading is multiplied up to sit in the same range as
the colour reading and the one force knob then means the same thing in either mode. */
static const float SLOPE_GAIN = 14.f;

/** WHICH OF A PIXEL'S NUMBERS IS BEING READ. Hue, saturation and value are the defaults, because
they are what the eye reads: a region's hue is what a person would call its colour, and its
saturation is how much colour it has. The plain three are there for pictures where one of them
happens to carry the shape. */
enum Channel { CH_HUE, CH_SAT, CH_VAL, CH_RED, CH_GREEN, CH_BLUE, NUM_CHANNELS };
static const char* CHANNEL_NAMES[NUM_CHANNELS] = {
	"Hue", "Saturation", "Value", "Red", "Green", "Blue"
};

/** HOW THE FIELD IS READ FROM THE PICTURE. */
enum FieldMode { FIELD_COLOUR, FIELD_SLOPE, NUM_FIELDS };


static void toHsv(float r, float g, float b, float* h, float* s, float* v) {
	const float hi = std::max(r, std::max(g, b));
	const float lo = std::min(r, std::min(g, b));
	const float d = hi - lo;
	*v = hi;
	*s = (hi > 0.f) ? d / hi : 0.f;
	if (d <= 0.f) {
		*h = 0.f;
		return;
	}
	float hue;
	if (hi == r)
		hue = (g - b) / d;
	else if (hi == g)
		hue = 2.f + (b - r) / d;
	else
		hue = 4.f + (r - g) / d;
	hue /= 6.f;
	if (hue < 0.f)
		hue += 1.f;
	*h = hue;
}


/** One of a pixel's numbers, nought to one, at a place on the square. */
static float channelAt(const Picture& picture, float x, float y, int which) {
	uint8_t px[4];
	picture.at(x, y, px);
	const float r = px[0] / 255.f, g = px[1] / 255.f, b = px[2] / 255.f;
	switch (which) {
		case CH_RED: return r;
		case CH_GREEN: return g;
		case CH_BLUE: return b;
		default: break;
	}
	float h = 0.f, s = 0.f, v = 0.f;
	toHsv(r, g, b, &h, &s, &v);
	if (which == CH_HUE) return h;
	if (which == CH_SAT) return s;
	return v;
}


/** THE COLOURS THE SPRITES ARE DRAWN IN. Four that hold their own over a photograph of anything,
and that are told apart by someone who does not see red and green as different. */
static const NVGcolor SPRITE_INK[MAX_SPRITES] = {
	nvgRGB(0xff, 0xf1, 0x4a),
	nvgRGB(0x4a, 0xd2, 0xff),
	nvgRGB(0xff, 0x6a, 0xc8),
	nvgRGB(0x8c, 0xff, 0x8c),
};


/** ONE SPRITE'S STATE, all of it. Held as plain numbers because only the audio thread writes them;
what the panel needs to draw is published separately. */
struct Sprite {
	/** Where it is on the square, nought to one in each direction. */
	float x = 0.5f, y = 0.5f;
	/** How fast it is going, in square widths a second. */
	float vx = 0.f, vy = 0.f;
	/** THE WALL TRICK, one sign for each axis. The force read from the picture is multiplied by
	these before it is applied and a bounce flips the one for that axis, so a region pushing
	rightwards pushes a sprite away from the right-hand wall it has just left rather than pinning
	it there. Nothing reads them until the field arrives in the next stage; they are kept and saved
	from the start so that adding the field is a field and not a rewrite. */
	float signX = 1.f, signY = 1.f;
};


struct SpritesModule : Module, NoteSource, NoteSink {
	enum ParamId { P_COUNT, P_SPEED, P_DRIFT, P_RUN, P_REWIND,
		P_FIELD, P_FORCE, P_BLEND, P_TURN, NUM_PARAMS };
	enum InputId { I_CHART, NUM_INPUTS };
	enum OutputId { O_NOTES, NUM_OUTPUTS };
	enum LightId { NUM_LIGHTS };

	/** THE PICTURE LIVES IN THE MODULE rather than in the widget: it is what is saved with the
	patch, it is what the field will be read from on the audio thread, and a module outlives the
	widget that shows it whenever the rack is scrolled. */
	Picture picture;

	/** THE DIMMED COPY, WHICH IS WHAT IS DRAWN, and how many times it has been remade so the panel
	knows when its own copy is stale. Empty when glare reduction is off, and the original is drawn
	instead.

	NOTHING BUT THE DRAWING USES IT. The field reads `picture` as it always did, so turning glare
	reduction on changes what the module looks like and not one note of what it does. */
	std::vector<uint8_t> shown;
	uint32_t shownGeneration = 0;

	/** How much a fully bright region is brought down to, or one for not at all. Three settings
	rather than a slider: this is chosen once for a screen and a room. */
	enum { GLARE_OFF, GLARE_GENTLE, GLARE_NORMAL, GLARE_STRONG, NUM_GLARES };
	int glare = GLARE_NORMAL;

	static float glareAtten(int which) {
		switch (which) {
			case GLARE_GENTLE: return 0.7f;
			case GLARE_STRONG: return 0.32f;
			case GLARE_NORMAL: return 0.5f;
			default: return 1.f;
		}
	}

	/** Remakes the dimmed copy. On the panel's thread: it reads the whole square several times. */
	void remakeShown() {
		shownGeneration++;
		if (glare == GLARE_OFF || !picture.has()) {
			shown.clear();
			return;
		}
		// GXW's own figures for how large a region counts as a region and how bright it has to be
		// before anything is taken off it.
		dimGlare(picture.rgba, shown, 50.f, 0.5f, glareAtten(glare));
	}

	/** What the panel draws: the dimmed copy if there is one, otherwise the picture itself. */
	const std::vector<uint8_t>& forDrawing() const {
		return shown.empty() ? picture.rgba : shown;
	}

	Sprite sprites[MAX_SPRITES];

	/** WHERE THEY START FROM, and the random stream they start it with.

	THE POINT IS THAT A RUN CAN BE REPEATED. Settle the sprites where you want them, set them
	running, and rewind: they go back to exactly where they began and, started again, retrace
	exactly the path they took before. That only works if the drift starts again from the same
	place in its random stream as well, so the stream's state is part of the starting point.

	It is captured whenever a sprite is placed by hand, which is what settling them means. */
	Sprite home[MAX_SPRITES];
	uint32_t homeNoise = 0x9e3779b9u;
	/** Set by the panel when a drag ends, read by the physics. */
	std::atomic<bool> takeHome{false};
	std::atomic<bool> rewindWanted{false};

	/** WHICH OF A PIXEL'S NUMBERS DOES WHICH JOB. Menu settings rather than knobs: which channel
	suits a picture is something to try rather than something to reach for while playing, and the
	column has no room for three more choosers. */
	std::atomic<int> steerChannel{CH_HUE};
	std::atomic<int> pushChannel{CH_SAT};

	/** THE PICTURE'S OWN AVERAGE PUSH, taken off every colour reading.

	Without it, a picture that is mostly blue pushes everything blue-wards for ever and the turn
	knob only changes which way that is. What is wanted is how each region differs from the
	picture's own average, so the average is measured once when the picture arrives and subtracted
	from then on.

	Measured on the thread that loaded the picture, read on the one that moves the sprites. */
	std::atomic<float> meanFX{0.f}, meanFY{0.f};

	/** THE FIELD AT A PLACE, before the force knob and the signs.

	A vector of about unit length where the picture has something to say and nothing where it
	does not. */
	math::Vec fieldAt(float x, float y) {
		if (!picture.has())
			return math::Vec(0.f, 0.f);
		const int mode = (int) std::round(params[P_FIELD].getValue());
		const int steer = steerChannel.load();
		const int push = pushChannel.load();
		math::Vec f;

		if (mode == FIELD_COLOUR) {
			// THE ANGLE IS USED AS AN ANGLE, which is what stops the colour wheel having a seam in
			// it: a hue just past the top of the wheel and one just short of it are a degree
			// apart as directions, however far apart they are as numbers.
			const float angle = channelAt(picture, x, y, steer) * 2.f * (float) M_PI;
			const float length = channelAt(picture, x, y, push);
			f = math::Vec(std::cos(angle) * length, std::sin(angle) * length);
			f = f.minus(math::Vec(meanFX.load(), meanFY.load()));
		}
		else {
			// THE SLOPE: the direction in which the channel changes fastest, taken from the
			// pixels either side. A field of slopes has no average push in it by construction and
			// it follows what is actually in the picture, since a slope is an edge.
			const float e = 1.f / (float) PICTURE_SIDE;
			const float gx = channelAt(picture, x + e, y, push)
				- channelAt(picture, x - e, y, push);
			const float gy = channelAt(picture, x, y + e, push)
				- channelAt(picture, x, y - e, push);
			f = math::Vec(gx, gy).mult(0.5f * SLOPE_GAIN);
		}

		// ACROSS, ALONG, OR THE SPIRAL BETWEEN THEM. Turned a right angle, the same vector takes
		// a sprite along an edge rather than across it: it orbits a bright shape, follows a
		// horizon, and cannot settle in a corner because a field turned like this has no sinks in
		// it. The interesting motion is usually part way between the two.
		const float blend = params[P_BLEND].getValue() * 0.5f * (float) M_PI;
		const float turn = params[P_TURN].getValue() * (float) M_PI / 180.f;
		const float a = blend + turn;
		const float ca = std::cos(a), sa = std::sin(a);
		return math::Vec(f.x * ca - f.y * sa, f.x * sa + f.y * ca);
	}

	/** Measures the picture's average push. Called from the panel's thread whenever the picture or
	the channels change, never from the one that moves the sprites: it reads the whole square. */
	void measure() {
		if (!picture.has()) {
			meanFX.store(0.f);
			meanFY.store(0.f);
			return;
		}
		const int steer = steerChannel.load();
		const int push = pushChannel.load();
		// Every fourth pixel each way. Sixty-five thousand samples say what a million would.
		const int step = 4;
		double sx = 0.0, sy = 0.0;
		int n = 0;
		for (int py = 0; py < PICTURE_SIDE; py += step) {
			for (int px = 0; px < PICTURE_SIDE; px += step) {
				const float x = (px + 0.5f) / PICTURE_SIDE;
				const float y = (py + 0.5f) / PICTURE_SIDE;
				const float angle = channelAt(picture, x, y, steer) * 2.f * (float) M_PI;
				const float length = channelAt(picture, x, y, push);
				sx += std::cos(angle) * length;
				sy += std::sin(angle) * length;
				n++;
			}
		}
		if (n > 0) {
			meanFX.store((float) (sx / n));
			meanFY.store((float) (sy / n));
		}
	}

	/** WHERE THE SPRITES ARE, FOR THE PANEL TO DRAW. The physics runs on the audio thread and the
	drawing on another, so what is drawn is published rather than read: one number at a time, each
	of which is written whole or not at all. A panel a tick behind is a panel nobody can tell is
	behind; a panel that reads half of one position and half of the next draws a sprite that is
	nowhere. */
	std::atomic<float> seenX[MAX_SPRITES];
	std::atomic<float> seenY[MAX_SPRITES];
	std::atomic<float> seenVX[MAX_SPRITES];
	std::atomic<float> seenVY[MAX_SPRITES];

	/** WHAT THE HAND IS DOING, the other way across. A sprite being dragged is held still and put
	where the pointer says; the panel writes these and the physics obeys them. Minus one is nobody
	being dragged. */
	std::atomic<int> heldSprite{-1};
	std::atomic<float> heldX{0.f}, heldY{0.f};

	SpritesModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		// COUNTED FROM NOUGHT, like every other lamp row: a lamp row lights the lamp whose number
		// is the parameter's value, so a parameter that started at one lit the second lamp for
		// one sprite and left the first unreachable. What is shown is one more than what is held.
		configSwitch(P_COUNT, 0.f, (float) (MAX_SPRITES - 1), 0.f, "Sprites",
			{"1", "2", "3", "4"});
		// A CEILING, NOT A SCALING. A sprite below it is not touched by this knob at all — it
		// accelerates and turns exactly as the picture says — and one that reaches it goes no
		// faster. Never quite nought, because a ceiling of nothing would wipe out every velocity
		// the sprites had and leave nothing to raise again.
		configParam(P_SPEED, 0.05f, 1.f, 0.5f, "Top speed", "%", 0.f, 100.f);
		configParam(P_DRIFT, 0.f, 1.f, 0.3f, "Drift", "%", 0.f, 100.f);
		configSwitch(P_RUN, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
		configButton(P_REWIND, "Rewind");
		configSwitch(P_FIELD, 0.f, (float) (NUM_FIELDS - 1), 0.f, "Field",
			{"Colour", "Slope"});
		configParam(P_FORCE, 0.f, 1.f, 0.5f, "Force", "%", 0.f, 100.f);
		configParam(P_BLEND, 0.f, 1.f, 0.f, "Across to along", "%", 0.f, 100.f);
		configParam(P_TURN, -180.f, 180.f, 0.f, "Turn", "°");
		configInput(I_CHART, "MPX chart");
		configOutput(O_NOTES, "MPX note");
		scatter();
		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		slot = busClaim(&generation);
	}

	~SpritesModule() {
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

	/** A CHEAP RANDOM STREAM for the drift, kept in the module. Rack's own is fine but this is
	asked for eight numbers five hundred times a second and wants nothing more than to be
	unpredictable. */
	uint32_t noise = 0x9e3779b9u;

	float uniform() {
		noise ^= noise << 13;
		noise ^= noise >> 17;
		noise ^= noise << 5;
		return (float) (noise >> 8) * (1.f / 16777216.f) * 2.f - 1.f;
	}

	/** A STARTING ARRANGEMENT: spread across the square rather than stacked in the middle, each
	going somewhere different. Four sprites all started from one point with no velocity would sit
	on top of one another until the drift happened to part them. */
	void scatter() {
		for (int i = 0; i < MAX_SPRITES; i++) {
			const float t = (float) i / (float) MAX_SPRITES;
			sprites[i].x = 0.3f + 0.4f * ((i % 2 == 0) ? 0.f : 1.f);
			sprites[i].y = 0.25f + 0.5f * ((i < 2) ? 0.f : 1.f);
			const float a = t * 2.f * (float) M_PI + 0.7f;
			sprites[i].vx = 0.5f * MAX_SPEED * std::cos(a);
			sprites[i].vy = 0.5f * MAX_SPEED * std::sin(a);
			sprites[i].signX = sprites[i].signY = 1.f;
			publish(i);
		}
		keepHome();
	}

	void keepHome() {
		for (int i = 0; i < MAX_SPRITES; i++)
			home[i] = sprites[i];
		homeNoise = noise;
	}

	void goHome() {
		for (int i = 0; i < MAX_SPRITES; i++) {
			sprites[i] = home[i];
			publish(i);
		}
		noise = homeNoise;
		physicsPhase = 0.f;
	}

	void publish(int i) {
		seenX[i].store(sprites[i].x);
		seenY[i].store(sprites[i].y);
		seenVX[i].store(sprites[i].vx);
		seenVY[i].store(sprites[i].vy);
	}

	/** ONE TICK OF THE PHYSICS for one sprite.

	WITH NOTHING PUSHING IT a sprite travels in a straight line and bounces off the four walls. A
	bounce does nothing else — no note, no trigger — it is a wall, not an event.

	THE DRIFT IS A RANDOM WANDER added to the velocity, so a sprite that has nothing else acting on
	it keeps moving and keeps changing rather than ruling the same line across the picture for
	ever. It is what will stop a sprite sitting in a black region reading the same pixel once the
	field arrives. */
	void tick(int i, float dt, float limit, float drift, float force) {
		Sprite& s = sprites[i];

		// THE PICTURE PUSHES, through the two signs.
		//
		// THE SIGNS ARE THE WALL TRICK. A region pushing rightwards would pin a sprite against the
		// right-hand wall, bouncing it back and being pushed into it again for as long as it took
		// the drift to carry it away. Multiplying the force by a sign that flips on every bounce
		// means that a region which drove a sprite into a wall drives it away from that wall
		// afterwards, and the picture keeps its shape either way: it is the same field read
		// backwards, not a different one.
		if (force > 0.f) {
			const math::Vec f = fieldAt(s.x, s.y);
			s.vx += f.x * s.signX * force * FORCE_MAX * dt;
			s.vy += f.y * s.signY * force * FORCE_MAX * dt;
		}

		if (drift > 0.f) {
			// Scaled by the square root of the step, so the wander is the same however often
			// this runs: it is a walk, and a walk's spread goes as the root of its steps.
			//
			// AND IT IS SMALL. The drift is here to stop a sprite becoming stuck — sitting in a
			// black region with no force on it, reading the same pixel for ever — and for nothing
			// else. It is not what makes a sprite move. At the knob's full turn it shifts the
			// velocity by a few per cent of the speed limit in a second, which is felt over half
			// a minute and not seen over one.
			const float step = drift * DRIFT_SCALE * MAX_SPEED * std::sqrt(dt);
			s.vx += uniform() * step;
			s.vy += uniform() * step;
		}

		// THE CEILING, applied to the whole velocity rather than to each axis, or a sprite going
		// corner to corner would be allowed half as fast again as one going straight across. A
		// sprite below it is left exactly as it was: this puts a lid on the speed and does nothing
		// else to it.
		const float was = std::sqrt(s.vx * s.vx + s.vy * s.vy);
		if (was > limit && was > 0.f) {
			s.vx *= limit / was;
			s.vy *= limit / was;
		}

		s.x += s.vx * dt;
		s.y += s.vy * dt;

		if (s.x < 0.f) { s.x = -s.x; s.vx = -s.vx; s.signX = -s.signX; }
		else if (s.x > 1.f) { s.x = 2.f - s.x; s.vx = -s.vx; s.signX = -s.signX; }
		if (s.y < 0.f) { s.y = -s.y; s.vy = -s.vy; s.signY = -s.signY; }
		else if (s.y > 1.f) { s.y = 2.f - s.y; s.vy = -s.vy; s.signY = -s.signY; }

		// A bounce off a corner at exactly the wrong moment, or a patch loaded with a position
		// from somewhere else, could still leave it outside. Put back rather than trusted.
		s.x = math::clamp(s.x, 0.f, 1.f);
		s.y = math::clamp(s.y, 0.f, 1.f);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_t* spritesJ = json_array();
		for (int i = 0; i < MAX_SPRITES; i++) {
			json_t* sJ = json_object();
			json_object_set_new(sJ, "x", json_real(sprites[i].x));
			json_object_set_new(sJ, "y", json_real(sprites[i].y));
			json_object_set_new(sJ, "vx", json_real(sprites[i].vx));
			json_object_set_new(sJ, "vy", json_real(sprites[i].vy));
			json_object_set_new(sJ, "signX", json_real(sprites[i].signX));
			json_object_set_new(sJ, "signY", json_real(sprites[i].signY));
			json_array_append_new(spritesJ, sJ);
		}
		json_object_set_new(rootJ, "sprites", spritesJ);
		json_t* homeJ = json_array();
		for (int i = 0; i < MAX_SPRITES; i++) {
			json_t* sJ = json_object();
			json_object_set_new(sJ, "x", json_real(home[i].x));
			json_object_set_new(sJ, "y", json_real(home[i].y));
			json_object_set_new(sJ, "vx", json_real(home[i].vx));
			json_object_set_new(sJ, "vy", json_real(home[i].vy));
			json_object_set_new(sJ, "signX", json_real(home[i].signX));
			json_object_set_new(sJ, "signY", json_real(home[i].signY));
			json_array_append_new(homeJ, sJ);
		}
		json_object_set_new(rootJ, "home", homeJ);
		json_object_set_new(rootJ, "homeNoise", json_integer((json_int_t) homeNoise));
		json_object_set_new(rootJ, "glare", json_integer(glare));
		json_object_set_new(rootJ, "steerChannel", json_integer(steerChannel.load()));
		json_object_set_new(rootJ, "pushChannel", json_integer(pushChannel.load()));
		if (!picture.file.empty()) {
			json_object_set_new(rootJ, "picture",
				json_string(bytesToText(picture.file).c_str()));
			json_object_set_new(rootJ, "pictureName", json_string(picture.name.c_str()));
		}
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		auto readSprites = [&](const char* key, Sprite* into) {
			json_t* arrayJ = json_object_get(rootJ, key);
			if (!arrayJ)
				return false;
			for (int i = 0; i < MAX_SPRITES; i++) {
				json_t* sJ = json_array_get(arrayJ, i);
				if (!sJ)
					continue;
				auto num = [&](const char* field, float fallback) {
					json_t* v = json_object_get(sJ, field);
					return v ? (float) json_number_value(v) : fallback;
				};
				into[i].x = math::clamp(num("x", 0.5f), 0.f, 1.f);
				into[i].y = math::clamp(num("y", 0.5f), 0.f, 1.f);
				into[i].vx = math::clamp(num("vx", 0.f), -MAX_SPEED, MAX_SPEED);
				into[i].vy = math::clamp(num("vy", 0.f), -MAX_SPEED, MAX_SPEED);
				into[i].signX = num("signX", 1.f) < 0.f ? -1.f : 1.f;
				into[i].signY = num("signY", 1.f) < 0.f ? -1.f : 1.f;
			}
			return true;
		};
		if (readSprites("sprites", sprites)) {
			for (int i = 0; i < MAX_SPRITES; i++)
				publish(i);
			// A patch from before the transport existed has no rewind point. Its sprites are
			// where they are, so that is where they start from.
			if (!readSprites("home", home))
				keepHome();
			if (json_t* nJ = json_object_get(rootJ, "homeNoise"))
				homeNoise = (uint32_t) json_integer_value(nJ);
		}
		if (json_t* v = json_object_get(rootJ, "glare"))
			glare = math::clamp((int) json_integer_value(v), 0, NUM_GLARES - 1);
		if (json_t* v = json_object_get(rootJ, "steerChannel"))
			steerChannel.store(math::clamp((int) json_integer_value(v), 0, NUM_CHANNELS - 1));
		if (json_t* v = json_object_get(rootJ, "pushChannel"))
			pushChannel.store(math::clamp((int) json_integer_value(v), 0, NUM_CHANNELS - 1));
		const char* text = json_string_value(json_object_get(rootJ, "picture"));
		const char* called = json_string_value(json_object_get(rootJ, "pictureName"));
		if (!text)
			return;
		const std::vector<uint8_t> bytes = textToBytes(text);
		if (!picture.decode(bytes, called ? called : ""))
			WARN("mpxSprites: the picture in this patch could not be read");
	}

	/** How much of a physics tick has gone by. */
	float physicsPhase = 0.f;

	/** The fastest a sprite may go, in square widths a second. */
	float topSpeed() {
		return math::clamp(params[P_SPEED].getValue(), 0.05f, 1.f) * MAX_SPEED;
	}

	/** How many sprites are moving. One more than the parameter, which counts from nought. */
	int spriteCount() {
		return math::clamp((int) std::round(params[P_COUNT].getValue()), 0, MAX_SPRITES - 1) + 1;
	}

	dsp::SchmittTrigger rewindTrigger;

	void stepPhysics(const ProcessArgs& args) {
		// THE TRANSPORT, looked at every sample so a button press is never missed, whatever the
		// physics clock happens to be doing.
		if (rewindTrigger.process(params[P_REWIND].getValue() > 0.5f) || rewindWanted.exchange(false))
			goHome();
		if (takeHome.exchange(false))
			keepHome();
		const bool running = params[P_RUN].getValue() > 0.5f;

		physicsPhase += args.sampleTime * PHYSICS_HZ;
		if (physicsPhase < 1.f)
			return;
		// However many are due, but never a flood: a patch that has been paused and started
		// again, or an engine that stalled, should not make every sprite leap across the picture.
		int due = (int) physicsPhase;
		physicsPhase -= (float) due;
		if (due > 4)
			due = 4;

		const int count = spriteCount();
		const float limit = topSpeed();
		const float drift = params[P_DRIFT].getValue();
		const float force = params[P_FORCE].getValue();
		const float dt = 1.f / PHYSICS_HZ;
		const int held = heldSprite.load();

		for (int i = 0; i < MAX_SPRITES; i++) {
			if (i >= count)
				continue;
			if (i == held) {
				// HELD STILL WHILE IT IS DRAGGED, and put exactly where the pointer says. A sprite
				// that went on moving under the hand could not be placed.
				sprites[i].x = math::clamp(heldX.load(), 0.f, 1.f);
				sprites[i].y = math::clamp(heldY.load(), 0.f, 1.f);
			}
			else if (running) {
				for (int n = 0; n < due; n++)
					tick(i, dt, limit, drift, force);
			}
			publish(i);
		}
	}

	void process(const ProcessArgs& args) override {
		stepPhysics(args);
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}
		// THE CHART PASSES THROUGH while this module has nothing of its own to send: the notes
		// come in the fourth stage.
		outputs[O_NOTES].setChannels(1);
		outputs[O_NOTES].setVoltage(0.f);
		Event e;
		while (reader.next(e))
			busPush(slot, e);
		Harmony h;
		if (reader.harmony(h))
			busPublishHarmony(slot, h);
		ChordVoicing v;
		if (reader.voicing(v))
			busPublishVoicing(slot, v);
		{
			float sustain = 0.f, soft = 0.f;
			reader.pedals(sustain, soft);
			busPublishPedals(slot, sustain, soft);
		}
	}
};


/** THE SQUARE. The picture is drawn here, and in the stages to come the sprites move over it and
are dragged about it. A square because the field is one: a photograph of any shape is squared off
when it is loaded, so what is shown is what is read. */
struct PictureFrame : widget::OpaqueWidget {
	SpritesModule* module = NULL;
	/** The picture as nanovg holds it, and which reading of it that is, so a new picture replaces
	the handle rather than being drawn over. */
	int handle = -1;
	uint32_t shown = 0;

	/** HOW BIG A SPRITE IS DRAWN, and how near the pointer has to come to take hold of one. The
	grab reaches a little past the dot because a moving target is hard to hit. */
	static constexpr float DOT_R = 10.f;
	static constexpr float GRAB = DOT_R + 3.f;

	/** NOTHING IS DRAWN BUT THE DISCS.

	There was a line out of each sprite showing its heading and its speed, and a handle on the end
	of it for aiming one by hand. Both are gone. Four lines whipping about over a photograph drew
	the eye to the arithmetic rather than to the motion, and the motion is the point: discs
	drifting over a picture are pleasant to watch, and the same discs trailing instruments are
	not. What a sprite is doing is legible from the sprite. */
	enum { DRAG_NONE, DRAG_MOVE };
	int dragging = DRAG_NONE;
	int dragIndex = -1;
	math::Vec dragAt;

	int spriteCount() {
		return module ? module->spriteCount() : 1;
	}

	math::Vec placeOf(int i) {
		return math::Vec(module->seenX[i].load() * box.size.x,
			module->seenY[i].load() * box.size.y);
	}

	~PictureFrame() {
		release();
	}

	void release() {
		if (handle >= 0 && APP && APP->window && APP->window->vg)
			nvgDeleteImage(APP->window->vg, handle);
		handle = -1;
	}

	void draw(const DrawArgs& args) override {
		// The ground, so an empty frame is a dark square rather than a hole in the panel.
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGB(0x0d, 0x10, 0x14));
		nvgFill(args.vg);

		if (module && module->picture.has()) {
			// Both countings matter: a new picture, or the same one dimmed differently.
			const uint32_t now = module->picture.generation * 1000u + module->shownGeneration;
			if (handle < 0 || shown != now) {
				release();
				handle = nvgCreateImageRGBA(args.vg, PICTURE_SIDE, PICTURE_SIDE, 0,
					module->forDrawing().data());
				shown = now;
			}
			if (handle >= 0) {
				NVGpaint paint = nvgImagePattern(args.vg, 0.f, 0.f, box.size.x, box.size.y, 0.f,
					handle, 1.f);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
				nvgFillPaint(args.vg, paint);
				nvgFill(args.vg);
			}
		}
		else {
			// SAYS WHAT TO DO, rather than sitting there blank: an empty module that explains
			// itself is worth the two lines it costs.
			std::shared_ptr<window::Font> font = APP->window->loadFont(
				asset::system("res/fonts/DejaVuSans.ttf"));
			if (font && font->handle >= 0) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 13.f);
				nvgFillColor(args.vg, nvgRGB(0x5a, 0x66, 0x74));
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f - 8.f, "No picture", NULL);
				nvgText(args.vg, box.size.x / 2.f, box.size.y / 2.f + 8.f,
					"Right-click to load one", NULL);
			}
		}

		drawSprites(args);

		// The frame last, so nothing is drawn over it. In the panel's own quiet line rather than
		// its green: the square now stops a millimetre and a half inside the module's green
		// border, and two green lines that close together read as a mistake.
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f);
		nvgStrokeColor(args.vg, PANEL_RULE);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		widget::OpaqueWidget::draw(args);
	}

	/** WHETHER EACH SPRITE'S RING IS HEADING FOR THE LIGHT TONE, and what tone it is actually
	drawn in at the moment, nought for black and one for white. Both kept from frame to frame:
	see ringTone. */
	bool lightRing[MAX_SPRITES] = {false, false, false, false};
	float ringTone[MAX_SPRITES] = {0.f, 0.f, 0.f, 0.f};

	/** HOW BRIGHT THE PICTURE IS AROUND A SPRITE, nought to one.

	AROUND IT, NOT UNDER IT. What the ring has to stand out against is what the ring is drawn on,
	which is the band of picture just outside the dot; the pixel under the middle of the sprite is
	hidden by the sprite. Sampled on a ring a little wider than the dot, and averaged, so one odd
	pixel does not decide it. */
	float brightAround(int i) {
		if (!module || !module->picture.has())
			return 0.f;
		const std::vector<uint8_t>& from = module->forDrawing();
		if (from.size() < (size_t) PICTURE_SIDE * PICTURE_SIDE * 4)
			return 0.f;
		const float toPicture = (box.size.x > 0.f) ? (float) PICTURE_SIDE / box.size.x : 1.f;
		const float r = (DOT_R + 2.f) * toPicture;
		const float cx = module->seenX[i].load() * PICTURE_SIDE;
		const float cy = module->seenY[i].load() * PICTURE_SIDE;
		float sum = 0.f;
		const int steps = 12;
		for (int k = 0; k < steps; k++) {
			const float a = (float) k / steps * 2.f * (float) M_PI;
			int x = (int) (cx + std::cos(a) * r);
			int y = (int) (cy + std::sin(a) * r);
			x = math::clamp(x, 0, PICTURE_SIDE - 1);
			y = math::clamp(y, 0, PICTURE_SIDE - 1);
			const uint8_t* px = &from[((size_t) y * PICTURE_SIDE + x) * 4];
			sum += (0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2]) / 255.f;
		}
		return sum / steps;
	}

	/** THE RING GOES THE OPPOSITE WAY TO WHAT IS UNDER IT: white on a dark region, black on a
	pale one. A ring of one colour is invisible wherever the picture happens to match it, and a
	picture chosen for its colours contains every shade somewhere.

	ONLY AS FAR AS IT NEEDS TO GO. The ring is not black or white but a grey a fixed distance from
	the brightness it sits on — so on a very dark region it is a soft grey rather than a stark
	white, and it is only as loud as the job requires. Over a middling region it runs out of room
	in one direction and takes what it can get in the other.

	WITH A DEAD BAND IN THE MIDDLE. Which direction it goes is decided with hysteresis: it turns
	towards light only below four tenths and towards dark only above six, and between the two it
	keeps whatever it was heading for. Without that, a sprite crossing a region of middling
	brightness would change its mind from frame to frame as the average wandered across the mark.

	AND IT TRAVELS THERE RATHER THAN JUMPING. The tone eases towards whatever is wanted over about
	a fifth of a second, so when the choice does turn over the ring slides from one to the other
	instead of snapping. A sprite crossing from a bright region into a dark one darkens and then
	lightens through the change, which reads as the sprite passing over something rather than as
	something going wrong with the drawing. */
	float ringToneFor(int i) {
		const float bright = brightAround(i);
		if (bright < 0.4f)
			lightRing[i] = true;
		else if (bright > 0.6f)
			lightRing[i] = false;
		// How far from what it sits on the ring stands. Far enough to separate a disc from the
		// picture, near enough not to shout.
		static const float APART = 0.55f;
		const float want = lightRing[i] ? std::min(1.f, bright + APART)
			: std::max(0.f, bright - APART);
		// About a fifth of a second at sixty frames.
		ringTone[i] += (want - ringTone[i]) * 0.1f;
		return math::clamp(ringTone[i], 0.f, 1.f);
	}

	NVGcolor ringColour(int i) {
		const int tone = (int) std::lround(ringToneFor(i) * 255.f);
		return nvgRGBA(tone, tone, tone, 0xee);
	}

	/** THE COLOUR UNDER A SPRITE, which is the colour the sprite is drawn in. */
	NVGcolor colourUnder(int i) {
		if (!module || !module->picture.has())
			return SPRITE_INK[i];
		// THE COLOUR AS DRAWN, not as stored: a sprite wearing the undimmed colour over a dimmed
		// picture would be a bright disc on a quiet ground, which is the glare back again.
		const std::vector<uint8_t>& from = module->forDrawing();
		int x = (int) (module->seenX[i].load() * PICTURE_SIDE);
		int y = (int) (module->seenY[i].load() * PICTURE_SIDE);
		x = math::clamp(x, 0, PICTURE_SIDE - 1);
		y = math::clamp(y, 0, PICTURE_SIDE - 1);
		const size_t at = ((size_t) y * PICTURE_SIDE + x) * 4;
		if (at + 2 >= from.size())
			return SPRITE_INK[i];
		return nvgRGB(from[at], from[at + 1], from[at + 2]);
	}

	/** THE SPRITES THEMSELVES, and nothing else.

	A SPRITE WEARS THE COLOUR IT IS STANDING ON. What the field does to a sprite follows from the
	colour under it, so showing that colour puts the cause and the effect in the same place: a
	sprite that swings hard as it crosses a boundary changes colour as it does so, and the reason
	for the swing is on the screen rather than only in the motion.

	EVERY ONE IS RINGED, which is the whole reason that can work. A sprite in the colour of what is
	under it would otherwise be invisible by construction. The ring is what separates it — see
	ringToneFor for how its tone is chosen. */
	void drawSprites(const DrawArgs& args) {
		if (!module)
			return;
		const int count = spriteCount();
		for (int i = 0; i < count; i++) {
			const math::Vec at = placeOf(i);
			const NVGcolor worn = colourUnder(i);
			const NVGcolor edge = ringColour(i);

			nvgBeginPath(args.vg);
			nvgCircle(args.vg, at.x, at.y, DOT_R);
			nvgFillColor(args.vg, worn);
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, edge);
			nvgStrokeWidth(args.vg, 2.f);
			nvgStroke(args.vg);
		}
	}

	/** WHICH SPRITE THE POINTER IS ON, if any. The topmost wins where two overlap. */
	bool whatIsHere(math::Vec pos, int* which) {
		const int count = spriteCount();
		for (int i = count - 1; i >= 0; i--) {
			if (placeOf(i).minus(pos).norm() <= GRAB) {
				*which = i;
				return true;
			}
		}
		return false;
	}

	void onButton(const ButtonEvent& e) override {
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			int which = -1;
			if (whatIsHere(e.pos, &which)) {
				dragging = DRAG_MOVE;
				dragIndex = which;
				dragAt = e.pos;
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onButton(e);
	}

	void onDragStart(const DragStartEvent& e) override {
		if (!module || dragging == DRAG_NONE)
			return;
		// HELD STILL WHILE THE HAND IS ON IT. A sprite that went on wandering under the pointer
		// could not be put anywhere.
		module->heldSprite.store(dragIndex);
		module->heldX.store(module->seenX[dragIndex].load());
		module->heldY.store(module->seenY[dragIndex].load());
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (!module || dragging == DRAG_NONE)
			return;
		const float zoom = getAbsoluteZoom();
		dragAt = dragAt.plus(e.mouseDelta.div(zoom > 0.f ? zoom : 1.f));
		module->heldX.store(math::clamp(dragAt.x / box.size.x, 0.f, 1.f));
		module->heldY.store(math::clamp(dragAt.y / box.size.y, 0.f, 1.f));
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (module) {
			module->heldSprite.store(-1);
			// PLACING A SPRITE IS SETTING WHERE IT STARTS FROM, so this is where the rewind point
			// is taken. Aiming one counts too: the direction it was given is part of where it
			// starts.
			module->takeHome.store(true);
		}
		dragging = DRAG_NONE;
		dragIndex = -1;
	}
};


/** THE PANEL'S MEASUREMENTS, all of them following from one decision: the picture is a square as
tall as the panel. A rack module is 128.5 millimetres high, so with a hair off the top and bottom
for the panel's own green border to show, the square is 125.5 across whatever the module's width.

THE WIDTH IS THEN CHOSEN FOR THE COLUMN, not for the picture. At 30 HP the column came to five HP,
which will not hold a rate knob, a floor and a ceiling, a sprite count, a field mode, a blend and
the channel choices. At 36 HP it is 54.4 millimetres, which will. */
static const float PANEL_H = 128.5f;
static const float PANEL_HP = 36.f;
static const float PANEL_W = PANEL_HP * 5.08f;
static const float INSET = 1.5f;
static const float SQUARE = PANEL_H - 2.f * INSET;
static const float COLUMN = PANEL_W - SQUARE - 2.f * INSET;


static Layout spritesLayout() {
	Layout L;
	L.hp = PANEL_HP;
	L.title = "mpxSprites";
	// THE NAME GOES OVER THE CONTROLS, not across the top of the panel: the picture has the whole
	// height and there is no strip along the top left to write in.
	L.titleWidth = COLUMN;

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			float size = 0.f, const std::string& owner = "") {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = Panel::CENTRE; i.heading = true; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};

	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = NOTE_CABLE;
		L.items.push_back(i);
		label(key + ".label", x, y + 6.99f, name, 7.f, key);
	};

	// THE SQUARE, the whole height of the panel bar the border, running past the title band and
	// the screws alike. A picture is what this module is for and a picture wants to be large.
	{
		Item i;
		i.key = "d.picture"; i.kind = Item::DISPLAY;
		i.x = PANEL_W - INSET - SQUARE; i.y = INSET; i.w = SQUARE; i.h = SQUARE;
		L.items.push_back(i);
	}

	static const float LAMP_MM = 6.5f / 2.9528f;
	const float mid = COLUMN / 2.f;

	auto knob = [&](const std::string& key, float x, float y, int id, const std::string& name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.ticks = 2;
		L.items.push_back(i);
		label(key + ".label", x, y + 8.02f, name, 0.f, key);
	};

	auto button = [&](const std::string& key, float x, float y, int id, const std::string& style,
			const std::string& name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y;
		i.style = style; i.diameter = 6.6f;
		L.items.push_back(i);
		label(key + ".label", x, y + 6.2f, name, 7.f, key);
	};

	// THE TRANSPORT AT THE TOP, where a thing you reach for between takes belongs. Run holds, so
	// it says whether the sprites are moving; rewind is a push, being an act rather than a state.
	button("p.run", mid - 10.f, 13.f, SpritesModule::P_RUN, "latch", "run");
	button("p.rewind", mid + 10.f, 13.f, SpritesModule::P_REWIND, "button", "rewind");

	// THE SPRITE COUNT as a row of lamps, one for each. A row rather than a column because the
	// names are single figures and the column is wide enough to take them abreast.
	{
		const float step = 2.f * LAMP_MM + 1.f;
		const float w = 2.f * LAMP_MM + step * (float) (MAX_SPRITES - 1);
		Item i;
		i.key = "p.count"; i.kind = Item::PARAM; i.id = SpritesModule::P_COUNT;
		i.style = "lamps";
		i.x = mid - w / 2.f; i.y = 31.f - LAMP_MM;
		i.names = std::vector<std::string>{"1", "2", "3", "4"};
		i.horizontal = true; i.pitch = step; i.labelSide = Panel::CENTRE;
		L.items.push_back(i);
		label("p.count.group", mid, 25.f, "sprites", 0.f, "p.count");
	}

	// HOW THE FIELD IS READ. Two ways: the colour of a region, which names a direction, or the
	// slope of it, which is an edge. The blend knob below turns a slope reading from across an
	// edge to along it, so "along the slope" is this chooser on Slope and that knob at its full.
	{
		const float step = 2.f * LAMP_MM + 1.f;
		const float h = 2.f * LAMP_MM + step;
		Item i;
		i.key = "p.field"; i.kind = Item::PARAM; i.id = SpritesModule::P_FIELD;
		i.style = "lamps";
		i.x = mid - 13.f; i.y = 47.f - h / 2.f;
		i.names = std::vector<std::string>{"colour", "slope"};
		i.horizontal = false; i.pitch = step; i.labelSide = Panel::RIGHT;
		L.items.push_back(i);
		label("p.field.group", mid, 40.f, "the field", 0.f, "p.field");
	}

	knob("p.force", mid - 12.f, 64.f, SpritesModule::P_FORCE, "force");
	knob("p.blend", mid + 12.f, 64.f, SpritesModule::P_BLEND, "along");
	knob("p.turn", mid - 12.f, 84.f, SpritesModule::P_TURN, "turn");
	knob("p.speed", mid + 12.f, 84.f, SpritesModule::P_SPEED, "speed");
	knob("p.drift", mid, 101.f, SpritesModule::P_DRIFT, "drift");

	// THE JACKS SIDE BY SIDE at the foot of the column.
	jack("in.chart", Item::PORT_IN, mid - 9.f, 117.f, SpritesModule::I_CHART, "mpx\nIN");
	jack("out.notes", Item::PORT_OUT, mid + 9.f, 117.f, SpritesModule::O_NOTES, "mpx\nOUT");
	return L;
}


struct SpritesWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	SpritesWidget(SpritesModule* module) {
		setModule(module);
		layout = spritesLayout();
		layoutApplyUser("mpxSprites", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);

		PictureFrame* frame = new PictureFrame;
		frame->module = module;
		layoutPlaceDisplay(this, layout, "d.picture", frame);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxSprites");
		SpritesModule* m = dynamic_cast<SpritesModule*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Load a picture…", "", [=]() {
			osdialog_filters* filters = osdialog_filters_parse(
				"Pictures:png,jpg,jpeg,bmp,gif,tga,psd");
			char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
			osdialog_filters_free(filters);
			if (!path)
				return;
			const std::string chosen = path;
			std::free(path);
			if (!m->picture.load(chosen)) {
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
					"That file could not be read as a picture.");
			}
		}));
		if (m->picture.has()) {
			menu->addChild(createMenuItem("Take the picture out", "", [=]() {
				m->picture.clear();
			}));
			menu->addChild(createMenuLabel(m->picture.name.empty()
				? "a picture" : m->picture.name));
		}

		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Reduce glare",
			{"Off", "Gentle", "Normal", "Strong"},
			[=]() { return m->glare; },
			[=](int i) { m->glare = i; }));

		// WHICH OF A PIXEL'S NUMBERS DOES WHICH JOB. Here rather than on the panel: it is a thing
		// tried once for a picture and then left alone.
		menu->addChild(new MenuSeparator);
		std::vector<std::string> channels;
		for (int i = 0; i < NUM_CHANNELS; i++)
			channels.push_back(CHANNEL_NAMES[i]);
		menu->addChild(createIndexSubmenuItem("Direction from", channels,
			[=]() { return m->steerChannel.load(); },
			[=](int i) { m->steerChannel.store(i); }));
		menu->addChild(createIndexSubmenuItem("Strength from", channels,
			[=]() { return m->pushChannel.load(); },
			[=](int i) { m->pushChannel.store(i); }));
	}

	/** What the picture's average was last measured for, so it is measured again only when
	something it depends on has changed. */
	uint32_t measuredFor = 0;
	int measuredSteer = -1, measuredPush = -1;
	/** The same for the dimmed copy the panel draws. */
	uint32_t dimmedFor = 0;
	int dimmedGlare = -1;

	void step() override {
		ModuleWidget::step();
		SpritesModule* m = dynamic_cast<SpritesModule*>(module);
		if (!m)
			return;

		// MEASURED ON THIS THREAD, never on the one that moves the sprites: it reads the whole
		// square, which is far too long to spend inside the engine.
		const int steer = m->steerChannel.load();
		const int push = m->pushChannel.load();
		if (m->picture.generation != measuredFor || steer != measuredSteer
				|| push != measuredPush) {
			measuredFor = m->picture.generation;
			measuredSteer = steer;
			measuredPush = push;
			m->measure();
		}
		if (m->picture.generation != dimmedFor || m->glare != dimmedGlare) {
			dimmedFor = m->picture.generation;
			dimmedGlare = m->glare;
			m->remakeShown();
		}
		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(SpritesModule::I_CHART)) {
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

		if (PortWidget* out = getOutput(SpritesModule::O_NOTES)) {
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(out)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}
};

} // namespace px


Model* modelMpxSprites = createModel<px::SpritesModule, px::SpritesWidget>("mpxSprites");
