/** mpxSprites — notes from things moving over a picture.

A picture is loaded and sprites move over it, pushed about by a force field read from its colours,
each sprite sending a note when its own timer fires. The picture is on the panel and the sprites
are watched moving across it, which is half the point: a path that looks interesting sounds
interesting. See docs/sprites.md.

THIS IS THE FOURTH STAGE: the picture, the sprites and the field that drives them, the transport
that can rewind a run so it retraces exactly, and now the notes — a timer on each sprite, its rate
between two limits and moved by what the sprite reads, the reading carried as a contour and turned
into a pitch by the harmony on the cable.

STILL TO COME: a sprite taking a voice of the chord instead of a contour, and per-sprite controls
that follow a selection.
*/
#include "plugin.hpp"
#include "NoteBus.hpp"
#include "Layout.hpp"
#include "Picture.hpp"
#include "Clipboard.hpp"

#include <osdialog.h>

#include <algorithm>
#include <cmath>
#include <map>

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

/** HOW A DUE NOTE FINDS ITS TICK when a clock is patched. */
enum SyncMode { SYNC_AFTER, SYNC_NEAREST, SYNC_DIVIDE, NUM_SYNCS };


/** WHAT A NOTE IS MADE FROM.

EVERY ONE OF THESE IS A NUMBER FROM NOUGHT TO ONE, read at the moment a note begins, and any of
them can drive any of the four things a note decides: its pitch, how hard it is struck, how often
it comes and how long it lasts. The knobs beside each of those go on meaning what they meant —
where the pitch sits and how wide, the two rate limits, the level, the duration — and this says
only what moves them.

THEY FALL INTO THREE KINDS. What the sprite is standing on, which is what the module read before
and nothing else. Where it is, which turns the picture into a score anybody can read by looking at
it: height for pitch is the oldest idea in written music. And how it is moving, which is where the
module stops being a picture reader and starts being a thing with behaviour — a sprite that accents
its corners is phrasing. */
enum Source {
	SRC_BRIGHT, SRC_HUE_RC, SRC_HUE_YB, SRC_SAT, SRC_EDGE, SRC_ODD,
	SRC_HEIGHT, SRC_ACROSS, SRC_OUT,
	SRC_SPEED, SRC_HEAD_EW, SRC_HEAD_NS, SRC_CORNER, SRC_PUSH,
	SRC_NEAR, SRC_WHICH, SRC_CHANCE,
	NUM_SOURCES
};

static const char* SOURCE_NAMES[NUM_SOURCES] = {
	"bright", "red–cyan", "yellow–blue", "colour", "edge", "odd",
	"height", "across", "outward",
	"speed", "east", "north", "corner", "push",
	"near", "which", "chance"
};


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
	nvgRGB(0xff, 0xf1, 0x4a),   // yellow
	nvgRGB(0x4a, 0xd2, 0xff),   // blue
	nvgRGB(0xff, 0x6a, 0xc8),   // pink
	// ORANGE, NOT GREEN. The fourth was a pale green, which sat too near the blue: the two
	// differed mostly in hue and a hue difference is the one a good many people do not have. Orange
	// is far from all three of the others in lightness as well as in hue.
	nvgRGB(0xff, 0x9a, 0x2e),
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
	enum ParamId { P_SPEED, P_DRIFT, P_RUN, P_REWIND,
		P_FORCE, P_BLEND, P_TURN,
		P_SLOW, P_FAST, P_DEPTH, P_LEVEL, P_REGISTER, P_DURATION, P_ARTIC, P_RANGE,
		P_SYNC,
		P_SRC_PITCH, P_SRC_LEVEL, P_SRC_RATE, P_SRC_DUR, NUM_PARAMS };
	enum InputId { I_CHART, I_CLOCK, NUM_INPUTS };
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

	/** EVERY SETTING THAT BELONGS TO ONE SPRITE RATHER THAN TO ALL OF THEM.

	Nearly all of them. Four sprites over one picture are only worth having if each can feel a
	different version of it: the same region pushes one east and another north because their turn
	settings differ, one is heavy and slow and another quick and skittish, one plays long notes low
	down while another chatters above it. The shared set is what is left — the transport, how many
	sprites there are, and the picture itself. */
	enum SpriteVal { SV_FIELD, SV_FORCE, SV_BLEND, SV_TURN, SV_SPEED, SV_DRIFT,
		SV_SLOW, SV_FAST, SV_DEPTH, SV_REGISTER, SV_LEVEL, SV_DURATION, SV_ARTIC,
		SV_STEER, SV_PUSH, SV_READ, SV_SPAN, SV_ENABLE,
		SV_SRC_PITCH, SV_SRC_LEVEL, SV_SRC_RATE, SV_SRC_DUR, NUM_SV };

	/** WHICH KNOB SHOWS EACH OF THEM. One set of controls on the panel, four sets of values
	underneath: the tab says which set the controls are looking at.

	WHY NOT FOUR SETS OF KNOBS, one shown at a time. Because the panel editor and the help file
	would then see four of everything stacked in the same place, and the editor is how this panel
	gets laid out. The cost is that a knob is one parameter to Rack rather than four, so a
	controller cannot be mapped to one sprite's force alone. The gates are the per-sprite way in
	from outside, and they are jacks. */
	static int paramOf(int v) {
		switch (v) {
			case SV_FORCE: return P_FORCE;
			case SV_BLEND: return P_BLEND;
			case SV_TURN: return P_TURN;
			case SV_SPEED: return P_SPEED;
			case SV_DRIFT: return P_DRIFT;
			case SV_SLOW: return P_SLOW;
			case SV_FAST: return P_FAST;
			case SV_DEPTH: return P_DEPTH;
			case SV_REGISTER: return P_REGISTER;
			case SV_LEVEL: return P_LEVEL;
			case SV_DURATION: return P_DURATION;
			case SV_ARTIC: return P_ARTIC;
			case SV_SPAN: return P_RANGE;
			case SV_SRC_PITCH: return P_SRC_PITCH;
			case SV_SRC_LEVEL: return P_SRC_LEVEL;
			case SV_SRC_RATE: return P_SRC_RATE;
			case SV_SRC_DUR: return P_SRC_DUR;
			default: return -1;   // the three channel choices live in the menu, not on a knob
		}
	}

	/** Every sprite's values, and which sprite the controls are showing. */
	std::atomic<float> val[MAX_SPRITES][NUM_SV];
	std::atomic<int> tab{0};
	int shownTab = -1;
	/** A sprite the panel has asked to switch on or off, or minus one. Asked for rather than done
	there, because the values are the audio thread's to write. */
	std::atomic<int> flipEnable{-1};

	float valueOf(int sprite, int which) const {
		return val[math::clamp(sprite, 0, MAX_SPRITES - 1)][which].load();
	}

	/** THE TAB'S WORK, done on the audio thread so that it happens once and in order.

	Turning a knob writes into the sprite the tab is showing; changing tabs writes that sprite's
	values back into the knobs. A knob is therefore always displaying the truth about one sprite,
	and the moment of the switch is the only time anything is copied the other way. */
	void followTab() {
		const int flip = flipEnable.exchange(-1);
		if (flip >= 0 && flip < MAX_SPRITES)
			val[flip][SV_ENABLE].store(enabled(flip) ? 0.f : 1.f);
		const int want = math::clamp(tab.load(), 0, MAX_SPRITES - 1);
		if (want != shownTab) {
			shownTab = want;
			for (int v = 0; v < NUM_SV; v++) {
				const int p = paramOf(v);
				if (p >= 0)
					params[p].setValue(val[want][v].load());
			}
			return;
		}
		for (int v = 0; v < NUM_SV; v++) {
			const int p = paramOf(v);
			if (p >= 0)
				val[want][v].store(params[p].getValue());
		}
	}

	Sprite sprites[MAX_SPRITES];

	/** WHERE THEY START FROM, and the random stream they start it with.

	THE POINT IS THAT A RUN CAN BE REPEATED. Settle the sprites where you want them, set them
	running, and rewind: they go back to exactly where they began and, started again, retrace
	exactly the path they took before. That only works if the drift starts again from the same
	place in its random stream as well, so the stream's state is part of the starting point.

	It is captured whenever a sprite is placed by hand, which is what settling them means. */
	Sprite home[MAX_SPRITES];
	float homePhase[MAX_SPRITES] = {0.f, 0.f, 0.f, 0.f};
	uint32_t homeNoise = 0x9e3779b9u;
	/** Set by the panel when a drag ends, read by the physics. */
	std::atomic<bool> takeHome{false};
	std::atomic<bool> rewindWanted{false};

	int spanOf(int i) const {
		const int n = std::abs((int) std::lround(valueOf(i, SV_SPAN)));
		return math::clamp(n, 1, 60);
	}

	/** Whether this sprite's line runs the other way up. */
	bool upsideDown(int i) const {
		return valueOf(i, SV_SPAN) < 0.f;
	}

	/** HUE NAMES THE DIRECTION AND SATURATION THE STRENGTH, and they are no longer a choice.
	Any other pairing can be set but not judged: nobody can look at a photograph and say what its
	brightness would do as a direction, so the choice was one nobody could evaluate. This pairing
	is the one the eye can verify — red areas push one way, blue another, grey areas hardly at
	all — so it is simply how the field is read. */
	/** THE ENDS OF EACH READING, AS THIS PICTURE ACTUALLY USES THEM.

	A photograph whose brightness only ever runs from four tenths to six drives nothing: every
	control downstream is given a tenth of its range and the sprites barely respond, which looks
	exactly like a module that is not working. So each reading is stretched to use the whole range
	the picture has for it.

	PULLED OUT AT THE ENDS, not evened out. Evening the values would spread them furthest, but it
	invents variation where the picture has none — a plain sky becomes a field of small differences
	and a sprite swerves about in it where the eye can see nothing happening, which is the opposite
	of being able to watch the picture cause the motion. Pulling the ends out keeps the picture's
	own shape: flat stays flat, so a sprite coasts where the image is quiet and swings where it
	changes.

	FROM THE SECOND AND NINETY-EIGHTH HUNDREDTHS rather than the true extremes, so one bright speck
	cannot undo the stretch for the whole picture.

	NOT HUE, which is an angle and has no ends. */
	float chLo[NUM_CHANNELS], chHi[NUM_CHANNELS];

	float readOut(float x, float y, int which) {
		const float raw = channelAt(picture, x, y, which);
		const float lo = chLo[which], hi = chHi[which];
		if (hi - lo < 1e-3f)
			return raw;
		return math::clamp((raw - lo) / (hi - lo), 0.f, 1.f);
	}

	/** Measures those ends. On the panel's thread, when a picture arrives. */
	void measureRange() {
		for (int c = 0; c < NUM_CHANNELS; c++) {
			chLo[c] = 0.f;
			chHi[c] = 1.f;
		}
		if (!picture.has())
			return;
		static const int BINS = 256;
		for (int c = 0; c < NUM_CHANNELS; c++) {
			if (c == CH_HUE)
				continue;
			std::vector<int> hist((size_t) BINS, 0);
			int n = 0;
			for (int py = 0; py < PICTURE_SIDE; py += 4) {
				for (int px = 0; px < PICTURE_SIDE; px += 4) {
					const float v = channelAt(picture, (px + 0.5f) / PICTURE_SIDE,
						(py + 0.5f) / PICTURE_SIDE, c);
					hist[(size_t) math::clamp((int) (v * (BINS - 1)), 0, BINS - 1)]++;
					n++;
				}
			}
			if (n <= 0)
				continue;
			const int wantLo = n / 50, wantHi = n - n / 50;
			int seen = 0, lo = 0, hi = BINS - 1;
			for (int b = 0; b < BINS; b++) {
				seen += hist[(size_t) b];
				if (seen >= wantLo) { lo = b; break; }
			}
			seen = 0;
			for (int b = 0; b < BINS; b++) {
				seen += hist[(size_t) b];
				if (seen >= wantHi) { hi = b; break; }
			}
			if (hi > lo) {
				chLo[c] = (float) lo / (float) (BINS - 1);
				chHi[c] = (float) hi / (float) (BINS - 1);
			}
		}
	}

	int steerOf(int) const { return CH_HUE; }
	int pushOf(int) const { return CH_SAT; }
	int readOf(int i) const { return (int) std::lround(valueOf(i, SV_READ)); }

	/** THE PICTURE'S OWN AVERAGE PUSH, taken off every colour reading.

	Without it, a picture that is mostly blue pushes everything blue-wards for ever and the turn
	knob only changes which way that is. What is wanted is how each region differs from the
	picture's own average, so the average is measured once when the picture arrives and subtracted
	from then on.

	ONE AVERAGE PER SPRITE, because each has its own choice of which channel steers and which
	pushes, and the average has to be of the same reading it is subtracted from. */
	std::atomic<float> meanFX[MAX_SPRITES], meanFY[MAX_SPRITES];

	/** THE FIELD AT A PLACE AS ONE SPRITE FEELS IT, before its force and its signs.

	A vector of about unit length where the picture has something to say and nothing where it
	does not. Everything it depends on belongs to the sprite, so two sprites on the same pixel
	can be pushed in quite different directions. */
	math::Vec fieldAt(int i, float x, float y) {
		if (!picture.has())
			return math::Vec(0.f, 0.f);
		const int steer = steerOf(i);
		const int push = pushOf(i);
		math::Vec f;
		{
			// THE ANGLE IS USED AS AN ANGLE, which is what stops the colour wheel having a seam in
			// it: a hue just past the top of the wheel and one just short of it are a degree
			// apart as directions, however far apart they are as numbers.
			// STRETCHED, as everything the notes read already is. A photograph's saturation
			// rarely uses much of its range — a shoreline measured here ran 0.72 to 0.90 in the
			// water and 0.32 to 0.46 on the sand — so the ripples and the wet sand pushed almost
			// identically and the only thing in the picture was the line between the two. Pulled
			// out, each region has structure of its own for a sprite to follow.
			//
			// NOT THE HUE, which is an angle and has no ends to pull out.
			const float angle = channelAt(picture, x, y, steer) * 2.f * (float) M_PI;
			const float length = readOut(x, y, push);
			f = math::Vec(std::cos(angle) * length, std::sin(angle) * length);
			f = f.minus(math::Vec(meanFX[i].load(), meanFY[i].load()));
		}

		// ACROSS, ALONG, OR THE SPIRAL BETWEEN THEM. Turned a right angle, the same vector takes
		// a sprite along an edge rather than across it: it orbits a bright shape, follows a
		// horizon, and cannot settle in a corner because a field turned like this has no sinks in
		// it. The interesting motion is usually part way between the two.
		const float blend = valueOf(i, SV_BLEND) * 0.5f * (float) M_PI;
		const float turn = valueOf(i, SV_TURN) * (float) M_PI / 180.f;
		const float a = blend + turn;
		const float ca = std::cos(a), sa = std::sin(a);
		return math::Vec(f.x * ca - f.y * sa, f.x * sa + f.y * ca);
	}

	/** Measures the picture's average push. Called from the panel's thread whenever the picture or
	the channels change, never from the one that moves the sprites: it reads the whole square. */
	void measure(int i) {
		if (!picture.has()) {
			meanFX[i].store(0.f);
			meanFY[i].store(0.f);
			return;
		}
		const int steer = steerOf(i);
		const int push = pushOf(i);
		// Every fourth pixel each way. Sixty-five thousand samples say what a million would.
		const int step = 4;
		double sx = 0.0, sy = 0.0;
		int n = 0;
		for (int py = 0; py < PICTURE_SIDE; py += step) {
			for (int px = 0; px < PICTURE_SIDE; px += step) {
				const float x = (px + 0.5f) / PICTURE_SIDE;
				const float y = (py + 0.5f) / PICTURE_SIDE;
				const float angle = channelAt(picture, x, y, steer) * 2.f * (float) M_PI;
				// The same reading the field will take, or the average is the average of
				// something else and subtracting it leaves a bias behind.
				const float length = readOut(x, y, push);
				sx += std::cos(angle) * length;
				sy += std::sin(angle) * length;
				n++;
			}
		}
		if (n > 0) {
			meanFX[i].store((float) (sx / n));
			meanFY[i].store((float) (sy / n));
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
		// A CEILING, NOT A SCALING. A sprite below it is not touched by this knob at all — it
		// accelerates and turns exactly as the picture says — and one that reaches it goes no
		// faster. Never quite nought, because a ceiling of nothing would wipe out every velocity
		// the sprites had and leave nothing to raise again.
		configParam(P_SPEED, 0.05f, 1.f, 0.5f, "Top speed", "%", 0.f, 100.f);
		// NEVER QUITE NOUGHT. Drift is what breaks a balance, and a field has balances in it that hold
		// for ever without one: a line between two opposed regions is an unstable equilibrium when
		// the acceleration is reversed, and a sprite set exactly on it with no drift stays there
		// indefinitely. Measured on a photograph of a shoreline — the sprite sat on the waterline
		// for a minute and a half and would have sat there all day.
		configParam(P_DRIFT, 0.04f, 1.f, 0.3f, "Drift", "%", 0.f, 100.f);
		configSwitch(P_RUN, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
		configButton(P_REWIND, "Rewind");
		configParam(P_FORCE, -1.f, 1.f, 0.5f, "Magnitude", "%", 0.f, 100.f);
		configParam(P_BLEND, 0.f, 1.f, 0.f, "Transverse", "%", 0.f, 100.f);
		configParam(P_TURN, -180.f, 180.f, 0.f, "Rotate", "°");
		// THE TWO LIMITS THE TIMER LIVES BETWEEN, in notes a second, set as the logarithm of the
		// rate so that the slow end of each knob is not crowded into a few degrees of its turn.
		configParam(P_SLOW, std::log2(0.05f), std::log2(8.f), std::log2(0.5f), "Min rate");
		configParam(P_FAST, std::log2(0.2f), std::log2(40.f), std::log2(6.f), "Max rate");
		configParam(P_DEPTH, -1.f, 1.f, 0.7f, "Rate from the picture", "%", 0.f, 100.f);
		// AMPLITUDE RATHER THAN LEVEL on the panel. Level means too many things — a signal level, a
		// water level, a level of detail — and the one thing it has to mean here is how hard a note
		// is struck. Velocity would be the standard word for that and is spoken for: a sprite has a
		// velocity of its own, and it is one of the readings in the lists.
		configParam(P_LEVEL, 0.f, 1.f, 0.7f, "Amplitude", "%", 0.f, 100.f);
		configParam(P_REGISTER, -2.f, 2.f, 0.f, "Register", " V");
		// HOW LONG A NOTE LASTS, as a share of the gap to the next one. Past one it overlaps the
		// note after it, which is what makes a line legato rather than a row of separate notes.
		configParam(P_DURATION, 0.05f, 1.4f, 0.85f, "Duration", "%", 0.f, 100.f);
		// AND HOW MUCH THE PICTURE DECIDES IT. The same shape as force and depth: the knob sets
		// the middle and this says how far the reading may move it.
		configParam(P_ARTIC, -1.f, 1.f, 0.4f, "Articulation", "%", 0.f, 100.f);
		// HOW WIDE THIS SPRITE'S LINE IS, in semitones rather than octaves: a line that wants a
		// fifth to play in should be able to have a fifth. Snapped, because half a semitone is
		// not a range anybody means.
		// THE SIGN IS WHICH WAY UP. A negative range is the same width read the other way, so a
		// source can drive the pitch downward without a setting of its own — the same trick as the
		// bipolar depth and articulation, and for the same reason: a reversal you can hear while
		// turning beats one hidden in a menu.
		configParam(P_RANGE, -60.f, 60.f, 24.f, "Range", " semitones");
		getParamQuantity(P_RANGE)->snapEnabled = true;
		configSwitch(P_SYNC, 0.f, 2.f, 0.f, "Sync", {"After", "Nearest", "Divide"});
		// WHAT EACH OF THE FOUR THINGS A NOTE DECIDES IS READ FROM. Everything starts on
		// brightness, which is what the module did before any of this existed, so nothing sounds
		// different until one of them is moved.
		std::vector<std::string> sources;
		for (int i = 0; i < NUM_SOURCES; i++)
			sources.push_back(SOURCE_NAMES[i]);
		configSwitch(P_SRC_PITCH, 0.f, (float) (NUM_SOURCES - 1), (float) SRC_BRIGHT,
			"Pitch parameter", sources);
		configSwitch(P_SRC_LEVEL, 0.f, (float) (NUM_SOURCES - 1), (float) SRC_BRIGHT,
			"Amplitude parameter", sources);
		configSwitch(P_SRC_RATE, 0.f, (float) (NUM_SOURCES - 1), (float) SRC_BRIGHT,
			"Rate parameter", sources);
		configSwitch(P_SRC_DUR, 0.f, (float) (NUM_SOURCES - 1), (float) SRC_BRIGHT,
			"Duration parameter", sources);
		configInput(I_CHART, "MPX chart");
		configInput(I_CLOCK, "Trigger");
		configOutput(O_NOTES, "MPX note");

		resetControls();
		for (int i = 0; i < MAX_UPSTREAM; i++) {
			wantSlots[i].store(-1);
			wantGenerations[i].store(0);
		}
		slot = busClaim(&generation);
	}

	/** EVERY CONTROL BACK TO WHERE IT STARTED, and the picture left alone.

	THIS IS WHAT INITIALISE HAS TO DO HERE. Rack's own initialise puts the parameters back to their
	defaults, which on this panel is one sprite's worth: the knobs show whichever sprite the tab is
	on, and the other three would keep whatever they had been set to. So the array is put back as
	well, or three quarters of the module would survive being reset.

	THE PICTURE IS NOT A CONTROL. It is the material, and wanting a fresh set of settings is not a
	reason to lose it — so it, and the list of pictures used before, are untouched. */
	void resetControls() {
		for (int s = 0; s < MAX_SPRITES; s++) {
			for (int v = 0; v < NUM_SV; v++) {
				const int p = paramOf(v);
				val[s][v].store(p >= 0 ? params[p].getValue() : 0.f);
			}
			val[s][SV_STEER].store((float) CH_HUE);
			val[s][SV_PUSH].store((float) CH_SAT);
			val[s][SV_READ].store((float) CH_VAL);
			val[s][SV_SPAN].store(24.f);   // two octaves
			// ONE SPRITE ON and the rest waiting: four playing at once is not a starting point.
			val[s][SV_ENABLE].store(s == 0 ? 1.f : 0.f);
			// A DIFFERENT TURN EACH, so that four sprites dropped on one picture do not simply
			// follow one another about. It is the cheapest way to make them behave as four things
			// rather than as one thing drawn four times, and it is only a starting point.
			val[s][SV_TURN].store(-135.f + 90.f * (float) s);
			firing[s].store(0.f);
			meanFX[s].store(0.f);
			meanFY[s].store(0.f);
		}
		tab.store(0);
		// The knobs show the first sprite again, whatever they were showing.
		shownTab = -1;
		for (int v = 0; v < NUM_SV; v++) {
			const int p = paramOf(v);
			if (p >= 0)
				params[p].setValue(val[0][v].load());
		}
		scatter();
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

	/** A STARTING ARRANGEMENT: a row across the middle of the picture, evenly spaced, each sprite
	facing somewhere different.

	A ROW BECAUSE IT IS READABLE. Four sprites started from one point sit on top of one another
	until something happens to part them, and four started at the corners say nothing about which
	is which. Evenly spaced along one line, the tab numbers run left to right, and any two sprites
	that end up together got there by the picture rather than by never having left.

	IT IS ONLY A STARTING POINT. Drag a sprite anywhere and that becomes where it starts from —
	rewind brings it back there, and the patch remembers it. */
	void scatter() {
		for (int i = 0; i < MAX_SPRITES; i++) {
			sprites[i].x = ((float) i + 0.5f) / (float) MAX_SPRITES;
			sprites[i].y = 0.5f;
			// Facing four different ways, so that they part rather than running in convoy.
			const float a = (float) i / (float) MAX_SPRITES * 2.f * (float) M_PI + 0.7f;
			sprites[i].vx = 0.5f * MAX_SPEED * std::cos(a);
			sprites[i].vy = 0.5f * MAX_SPEED * std::sin(a);
			sprites[i].signX = sprites[i].signY = 1.f;
			publish(i);
		}
		keepHome();
	}

	/** Initialise puts the sprites back in their row as well as the knobs back to their defaults;
	without this the module would be reset except for the one thing you can see. */
	void onReset(const ResetEvent& e) override {
		// Rack's own first, which puts the parameters back to their defaults, and then the rest of
		// what this module is — which the parameters are only a quarter of.
		Module::onReset(e);
		resetControls();
	}

	/** EVERYTHING THE NEXT RUN DEPENDS ON, not merely where the sprites are.

	A rewind has to give back the same music, which means the same notes at the same moments with
	the same lengths. Where a sprite stands decides which note; how far through its interval its
	timer has got decides when; and the drift's random stream decides where it wanders next. Leave
	any of the three behind and the second run is a near miss rather than a repeat. */
	void keepHome() {
		for (int i = 0; i < MAX_SPRITES; i++) {
			home[i] = sprites[i];
			homePhase[i] = notePhase[i];
		}
		homeNoise = noise;
	}

	void goHome() {
		for (int i = 0; i < MAX_SPRITES; i++) {
			sprites[i] = home[i];
			notePhase[i] = homePhase[i];
			// ANYTHING STILL SOUNDING IS LET GO. A note held over a rewind would be a note the
			// next run never played, and it would end at a moment that belongs to the last one.
			endNote(i);
			lastRead[i] = picture.has()
				? channelAt(picture, sprites[i].x, sprites[i].y, readOf(i)) : 0.f;
			firing[i].store(0.f);
			// The clock's own counting goes back as well: a repeat that started part way through
			// a sprite's division would not be a repeat.
			ticksSince[i] = -1;
			lockCount[i] = 0;
			// The loitering is measured afresh, or a rewind would start with the last run's
			// opinion of where the sprite had been sitting.
			publish(i);
		}
		noise = homeNoise;
		// The part-finished physics tick goes too: it is a fraction of a tick's worth of motion
		// that the next run would otherwise start with and the one before it did not.
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
		// THE ACCELERATION KNOB IS BIPOLAR: nothing at twelve o'clock, the picture's own push to
		// the right of it, and the same push reversed to the left. Reversed, every place that was
		// drawing sprites in pushes them out — which is how a sprite is got out of a well, by hand
		// and while watching, rather than by the module deciding one is stuck and acting on it.
		if (force != 0.f) {
			const math::Vec f = fieldAt(i, s.x, s.y);
			s.vx += f.x * s.signX * force * FORCE_MAX * dt;
			s.vy += f.y * s.signY * force * FORCE_MAX * dt;
			// HOW HARD THE PICTURE IS SHOVING, kept for the sources. Smoothed a little, or it
			// would be a different number every tick and a note would catch whichever it caught.
			pushAt[i] += (math::clamp(f.norm() * std::fabs(force), 0.f, 1.f) - pushAt[i]) * 0.06f;
		}
		else {
			pushAt[i] *= 0.94f;
		}

		// HOW FAST THE HEADING IS TURNING. Near nothing running straight and bursting wherever the
		// picture changes its mind about the sprite — which is what makes it worth having, since
		// it marks the moments rather than the places. Decays rather than being cleared, so a note
		// falling shortly after a turn still hears it.
		{
			const float fast = std::sqrt(s.vx * s.vx + s.vy * s.vy);
			if (fast > 1e-5f) {
				const float heading = std::atan2(s.vy, s.vx);
				float turned = heading - lastHeading[i];
				// Brought back inside half a turn either way, so passing due north is a small
				// change and not a whole circle's worth.
				while (turned > (float) M_PI) turned -= 2.f * (float) M_PI;
				while (turned < -(float) M_PI) turned += 2.f * (float) M_PI;
				lastHeading[i] = heading;
				const float rate = std::fabs(turned) / dt;   // radians a second
				const float now = math::clamp(rate / 12.f, 0.f, 1.f);
				cornerAt[i] = std::max(now, cornerAt[i] * 0.97f);
			}
			else {
				cornerAt[i] *= 0.97f;
			}
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

		// AND A FLOOR UNDER IT: a sprite never quite stops.
		//
		// WHY IT IS NEEDED. The picture's push comes from saturation, so a grey or dark region
		// pushes almost nothing — and reversing almost nothing gives almost nothing, which is why
		// a sprite that settles in a dark patch cannot be freed by the acceleration knob at either
		// extreme. There is no damping in the model, so a sprite that arrives with speed keeps it;
		// but one that arrives slowly, or has just had its speed clipped by the ceiling, has
		// nothing left to carry it out, and the drift is deliberately too small to be what moves
		// anything.
		//
		// WHY A FLOOR RATHER THAN AN ESCAPE. It has no special cases, it knows nothing about wells
		// or walls, and it is symmetric — which the problem is. A sprite always coasts, so it
		// always leaves a region that has nothing to say to it, and a region that does have
		// something to say still has the whole of its say.
		{
			const float now = std::sqrt(s.vx * s.vx + s.vy * s.vy);
			const float floorSpeed = 0.08f * limit;
			if (now < floorSpeed) {
				// With no heading at all to keep, it takes one: anything rather than nothing, and
				// the random stream is already here.
				math::Vec go = (now > 1e-6f) ? math::Vec(s.vx / now, s.vy / now)
					: math::Vec(uniform(), uniform());
				const float len = go.norm();
				go = (len > 1e-6f) ? go.div(len) : math::Vec(1.f, 0.f);
				s.vx = go.x * floorSpeed;
				s.vy = go.y * floorSpeed;
			}
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
		json_t* phaseJ = json_array();
		for (int i = 0; i < MAX_SPRITES; i++)
			json_array_append_new(phaseJ, json_real(homePhase[i]));
		json_object_set_new(rootJ, "homePhase", phaseJ);
		json_object_set_new(rootJ, "glare", json_integer(glare));
		json_object_set_new(rootJ, "tab", json_integer(tab.load()));
		// EVERY SPRITE'S SETTINGS. The knobs save themselves, as Rack saves every parameter, but
		// they only ever hold the sprite the tab is showing; the other three live here.
		json_t* valsJ = json_array();
		for (int sprite = 0; sprite < MAX_SPRITES; sprite++) {
			json_t* oneJ = json_array();
			for (int v = 0; v < NUM_SV; v++)
				json_array_append_new(oneJ, json_real(val[sprite][v].load()));
			json_array_append_new(valsJ, oneJ);
		}
		json_object_set_new(rootJ, "perSprite", valsJ);
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
			if (json_t* phaseJ = json_object_get(rootJ, "homePhase")) {
				for (int i = 0; i < MAX_SPRITES; i++) {
					json_t* n = json_array_get(phaseJ, i);
					if (n)
						homePhase[i] = math::clamp((float) json_number_value(n), 0.f, 1.f);
				}
			}
		}
		if (json_t* v = json_object_get(rootJ, "glare"))
			glare = math::clamp((int) json_integer_value(v), 0, NUM_GLARES - 1);
		if (json_t* v = json_object_get(rootJ, "tab"))
			tab.store(math::clamp((int) json_integer_value(v), 0, MAX_SPRITES - 1));
		if (json_t* valsJ = json_object_get(rootJ, "perSprite")) {
			for (int sprite = 0; sprite < MAX_SPRITES; sprite++) {
				json_t* oneJ = json_array_get(valsJ, sprite);
				if (!oneJ)
					continue;
				for (int v = 0; v < NUM_SV; v++) {
					json_t* n = json_array_get(oneJ, v);
					if (n)
						val[sprite][v].store((float) json_number_value(n));
				}
			}
		}
		// The knobs are loaded from the sprite the tab is on, whatever Rack put in them.
		shownTab = -1;
		// A patch from before the range was counted in semitones said how many octaves it was.
		if (json_t* v = json_object_get(rootJ, "spanOctaves")) {
			const float semis = 12.f * (float) math::clamp((int) json_integer_value(v), 1, 5);
			for (int i = 0; i < MAX_SPRITES; i++)
				val[i][SV_SPAN].store(semis);
		}
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

	/** The fastest a sprite may go, in square widths a second. Its own ceiling. */
	float topSpeed(int i) {
		return math::clamp(valueOf(i, SV_SPEED), 0.05f, 1.f) * MAX_SPEED;
	}

	/** Whether this sprite is switched on. Its own setting, like everything else about it. */
	bool enabled(int i) const {
		return valueOf(i, SV_ENABLE) > 0.5f;
	}

	dsp::SchmittTrigger rewindTrigger;

	/** WHAT EACH SPRITE LAST READ, taken on the physics clock rather than every sample. The
	reading needs a colour conversion and nothing musical happens between two of them. */
	float lastRead[MAX_SPRITES] = {0.f, 0.f, 0.f, 0.f};
	/** WHAT THE MOTION IS DOING, measured on the physics clock because each of these is a
	difference between one tick and the last and means nothing without them. Sampled at the moment
	a note begins, as everything else is. */
	float lastHeading[MAX_SPRITES] = {0.f, 0.f, 0.f, 0.f};
	float cornerAt[MAX_SPRITES] = {0.f, 0.f, 0.f, 0.f};
	float pushAt[MAX_SPRITES] = {0.f, 0.f, 0.f, 0.f};
	float nearAt[MAX_SPRITES] = {0.f, 0.f, 0.f, 0.f};

	/** WHERE A SPRITE HAS BEEN LATELY, and how stuck it is.

	THE PICTURE HAS WELLS IN IT. Wherever the colours turn the right way round a spot, everything
	near that spot is pushed towards it, and a sprite that wanders in is pushed back every time it
	tries to leave. The drift is far too small to free it — deliberately, since the drift is not
	supposed to be what moves anything — so without help a sprite found by a well is lost for good,
	and sprites find them: a wandering thing eventually walks into every trap on the picture.

	HOW IT IS NOTICED. A slow average of where the sprite has been, and how far it currently is
	from that average. A sprite crossing the picture is always a long way from its own recent
	average; a sprite orbiting a well or juddering against it is never far from it, however fast it
	happens to be moving. So the measure catches a sprite going round in circles as well as one
	pinned still, which speed alone would not.

	WHAT IS DONE ABOUT IT, AND WHY IT IS THIS. The temptation is to flick the sprite somewhere, or
	invert the force, or throw a random heading at it — and all three look like the module
	twitching rather than like the sprite moving. Instead a push grows, slowly, directly away from
	the middle of wherever it has been loitering, and fades again once it is travelling. Its
	direction is the one direction that means something — out — and because it ramps in over a
	couple of seconds and out again over one, what is seen is a sprite working its way free rather
	than being rescued. When nothing is stuck it is exactly nothing: the push is zero and no code
	path differs. */
	/** How far through its interval each sprite's timer has got, nought to one. */
	float notePhase[MAX_SPRITES] = {0.f, 0.f, 0.f, 0.f};
	/** The note each sprite has sounding, and how long is left of it. */
	int64_t noteHandle[MAX_SPRITES] = {0, 0, 0, 0};
	float noteEndIn[MAX_SPRITES] = {-1.f, -1.f, -1.f, -1.f};
	std::atomic<float> firing[MAX_SPRITES];   /**< Flashes on a note, for the panel. */
	/** THE CLOCK, if one is patched: its rising edges, how long since the last, and how long the
	last gap was. One clock for all four sprites — they are being put on a common grid, which is
	the whole point of it. */
	dsp::SchmittTrigger clockTrigger;
	float clockPeriod = 0.5f;
	float sinceTick = 0.f;
	/** Clock ticks since each sprite last played, or minus one for one that has not played since
	the clock arrived. It is what the note's length is measured against. */
	int ticksSince[MAX_SPRITES] = {-1, -1, -1, -1};
	/** For DIVIDE: how many ticks this sprite has counted towards its division. */
	int lockCount[MAX_SPRITES] = {0, 0, 0, 0};

	/** HOW OFTEN A SPRITE FIRES, in notes a second.

	BETWEEN A FLOOR AND A CEILING, and geometrically rather than evenly between them: rate is
	heard as a ratio, so halfway between half a note a second and eight should be two and not four
	and a quarter.

	WHERE BETWEEN THEM is what the sprite reads. The middle of the two limits is where the depth
	knob leaves it with nothing to say, and the reading swings it from there by as much as that
	knob allows: a dark region towards the floor and a bright one towards the ceiling. */
	float rateFor(int i) {
		const float slow = std::exp2(valueOf(i, SV_SLOW));
		float fast = std::exp2(valueOf(i, SV_FAST));
		if (fast < slow)
			fast = slow;
		// BIPOLAR, so the picture can be turned upside down without a setting of its own: the
		// negative half of the knob is the same amount inverted, and a dark region quickens.
		const float depth = valueOf(i, SV_DEPTH);
		const float read = sourceOf(i, (int) std::lround(valueOf(i, SV_SRC_RATE)));
		const float where = math::clamp(0.5f + depth * (read - 0.5f) * 2.f, 0.f, 1.f);
		return slow * std::pow(fast / slow, where);
	}

	/** ONE SOURCE, READ NOW, as a number from nought to one.

	ANYTHING ANGULAR IS READ THROUGH A COSINE, never as the angle itself: half a degree either side
	of due north is half a degree apart as a direction and a whole turn apart as a number, and a
	source that leapt there would make every sprite crossing north jump. The cost is that a cosine
	folds — east and west are told apart, but up and down are not — so the heading is offered twice,
	once each way round, and between them the direction is still whole. */
	float sourceOf(int i, int which) {
		const Sprite& s = sprites[i];
		switch (which) {
			case SRC_BRIGHT: return picture.has() ? readOut(s.x, s.y, CH_VAL) : 0.f;
			// HUE THROUGH A COSINE AND A SINE, never as a number. A hue is an angle: just past red
			// and just short of it are a hair apart as colours and a whole turn apart as numbers,
			// so a sprite crossing red would have leapt the entire range. Each of these two is
			// smooth the whole way round, and between them the colour is still fully described.
			case SRC_HUE_RC: {
				if (!picture.has())
					return 0.5f;
				const float a = channelAt(picture, s.x, s.y, CH_HUE) * 2.f * (float) M_PI;
				return 0.5f + 0.5f * std::cos(a);
			}
			case SRC_HUE_YB: {
				if (!picture.has())
					return 0.5f;
				const float a = channelAt(picture, s.x, s.y, CH_HUE) * 2.f * (float) M_PI;
				return 0.5f + 0.5f * std::sin(a);
			}
			case SRC_SAT: return picture.has() ? readOut(s.x, s.y, CH_SAT) : 0.f;

			// HOW FAST THE PICTURE IS CHANGING HERE rather than how bright it is: near nothing on
			// a clear sky, high along a roof line. It answers "is anything happening here".
			case SRC_EDGE: {
				if (!picture.has())
					return 0.f;
				const float e = 1.f / (float) PICTURE_SIDE;
				const float gx = readOut(s.x + e, s.y, CH_VAL) - readOut(s.x - e, s.y, CH_VAL);
				const float gy = readOut(s.x, s.y + e, CH_VAL) - readOut(s.x, s.y - e, CH_VAL);
				return math::clamp(std::sqrt(gx * gx + gy * gy) * SLOPE_GAIN, 0.f, 1.f);
			}

			// HOW UNLIKE THE PICTURE'S AVERAGE this region is. Nought where the picture is being
			// typical of itself and one at its exception, so the same setting finds the red thing
			// in a forest and the green thing in a sunset.
			case SRC_ODD: {
				if (!picture.has())
					return 0.f;
				const float angle = channelAt(picture, s.x, s.y, CH_HUE) * 2.f * (float) M_PI;
				const float length = channelAt(picture, s.x, s.y, CH_SAT);
				const math::Vec here(std::cos(angle) * length, std::sin(angle) * length);
				const math::Vec mean(meanFX[i].load(), meanFY[i].load());
				return math::clamp(here.minus(mean).norm(), 0.f, 1.f);
			}

			// One at the top, which is the way a stave is written and the way anybody expects a
			// picture to sound.
			case SRC_HEIGHT: return math::clamp(1.f - s.y, 0.f, 1.f);
			case SRC_ACROSS: return math::clamp(s.x, 0.f, 1.f);
			case SRC_OUT: {
				const float dx = s.x - 0.5f, dy = s.y - 0.5f;
				return math::clamp(std::sqrt(dx * dx + dy * dy) / 0.7071f, 0.f, 1.f);
			}

			case SRC_SPEED: {
				const float fast = std::sqrt(s.vx * s.vx + s.vy * s.vy);
				return math::clamp(fast / std::max(topSpeed(i), 1e-4f), 0.f, 1.f);
			}
			case SRC_HEAD_EW: {
				const float fast = std::sqrt(s.vx * s.vx + s.vy * s.vy);
				if (fast < 1e-6f)
					return 0.5f;
				return 0.5f + 0.5f * (s.vx / fast);
			}
			case SRC_HEAD_NS: {
				const float fast = std::sqrt(s.vx * s.vx + s.vy * s.vy);
				if (fast < 1e-6f)
					return 0.5f;
				return 0.5f + 0.5f * (-s.vy / fast);
			}
			case SRC_CORNER: return math::clamp(cornerAt[i], 0.f, 1.f);
			case SRC_PUSH: return math::clamp(pushAt[i], 0.f, 1.f);
			case SRC_NEAR: return math::clamp(nearAt[i], 0.f, 1.f);

			// A CONSTANT, different for each sprite: four sprites spread across a register, or
			// across the stereo field, from one setting.
			case SRC_WHICH: return (float) i / (float) (MAX_SPRITES - 1);
			case SRC_CHANCE: return 0.5f + 0.5f * uniform();
			default: return 0.f;
		}
	}

	/** THE PITCH A READING MEANS.

	THE READING IS A CONTOUR, NOT A NOTE. Nought is the bottom of the line's range and one the
	top, and the harmony on the cable says what lies between: the notes of the chord sounding,
	stacked up through however many octaves the span is set to. The same sprite over a different
	chart plays the same shape on different harmony, which is the whole point of reading a picture
	rather than writing a tune.

	WITH NO CHART there is no chord to ask, so it falls back to a major scale from C. A module
	that went silent when nothing was patched to it would be a module nobody could try. */
	float pitchFor(int i, float contour, const Harmony& h) {
		int pcs[16];
		int n = 0;
		if (h.valid)
			n = chordPitchClasses(h.current, h.key, pcs);
		if (n <= 0) {
			static const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
			for (int k = 0; k < 7; k++)
				pcs[k] = MAJOR[k];
			n = 7;
		}
		std::sort(pcs, pcs + n);

		// THE RANGE IS A NUMBER OF SEMITONES, not of octaves. A line that wants a fifth to play in
		// should be able to have a fifth; rounding every range up to a whole octave decides for
		// the player how wide a line may be, and a narrow line is a different instrument from a
		// wide one rather than a worse one.
		//
		// REGISTER IS THE MIDDLE OF IT, so widening the range opens it out both ways rather than
		// climbing.
		if (upsideDown(i))
			contour = 1.f - contour;
		const int semis = spanOf(i);
		const float low = valueOf(i, SV_REGISTER) - (float) semis / 24.f;
		const float high = low + (float) semis / 12.f;

		// EVERY NOTE THE CHORD ALLOWS INSIDE THAT RANGE, in order, and the contour picks among
		// them. Built rather than calculated, because the tones are not evenly spaced and the
		// range does not begin on one of them.
		float ladder[96];
		int steps = 0;
		const int firstOct = (int) std::floor(low) - 1;
		for (int oct = firstOct; oct <= firstOct + 7 && steps < 96; oct++) {
			for (int k = 0; k < n && steps < 96; k++) {
				const float v = (float) oct + (float) pcs[k] / 12.f;
				if (v < low - 1e-4f || v > high + 1e-4f)
					continue;
				ladder[steps++] = v;
			}
		}
		// A range too narrow to hold a single tone of the chord still has to play something, so it
		// plays the nearest one to where it sits.
		if (steps <= 0)
			return std::round((low + high) * 6.f) / 12.f;

		int k = (int) std::floor(math::clamp(contour, 0.f, 1.f) * (float) steps);
		return ladder[math::clamp(k, 0, steps - 1)];
	}

	void endNote(int i) {
		if (slot >= 0 && noteHandle[i] != 0) {
			Event off;
			off.kind = Event::OFF;
			off.handle = noteHandle[i];
			busPush(slot, off);
		}
		noteHandle[i] = 0;
		noteEndIn[i] = -1.f;
	}

	/** One sprite's note. */
	float fire(int i, float seconds, const Harmony& h) {
		if (slot < 0)
			return 0.f;
		endNote(i);
		Event e;
		e.kind = Event::ON;
		e.handle = noteHandle[i] = mintHandle();
		e.contour = math::clamp(sourceOf(i, (int) std::lround(valueOf(i, SV_SRC_PITCH))), 0.f, 1.f);
		e.pitch = pitchFor(i, e.contour, h);

		// WHERE THE SPRITE IS, ACROSS THE PICTURE, IS WHERE THE NOTE IS. No control and no
		// setting: a sprite over there should sound over there, and a mapping that can be switched
		// off is one more reason the picture and the sound might disagree.
		e.pan = math::clamp(sprites[i].x, 0.f, 1.f) * 2.f - 1.f;

		// THE PICTURE ARTICULATES. What each sprite reads decides how long its note is held and
		// how hard it is struck, either side of what the two knobs say. The articulation knob is
		// how far the reading may move them — at nought the knobs alone decide and the line is
		// even, turned up the picture phrases it, and turned the other way it phrases it upside
		// down.
		const float artic = valueOf(i, SV_ARTIC);
		const float awayL = (math::clamp(sourceOf(i,
			(int) std::lround(valueOf(i, SV_SRC_LEVEL))), 0.f, 1.f) - 0.5f) * 2.f;
		const float away = (math::clamp(sourceOf(i,
			(int) std::lround(valueOf(i, SV_SRC_DUR))), 0.f, 1.f) - 0.5f) * 2.f;
		e.level = math::clamp(valueOf(i, SV_LEVEL) * (1.f + artic * awayL * 0.6f), 0.f, 1.f);
		// LONG ENOUGH TO BE HEARD AND SHORT ENOUGH TO GET OUT OF THE WAY, as a share of the gap
		// to the next note — so a quick sprite runs and a slow one sustains — with a lid on it: a
		// sprite firing once every ten seconds should not hold a note for nine of them.
		const float share = math::clamp(valueOf(i, SV_DURATION) * (1.f + artic * away * 0.8f),
			0.02f, 2.f);
		e.duration = math::clamp(seconds * share, 0.01f, 6.f);
		busPush(slot, e);
		// WHO ENDS IT is the caller's business: a note of the timer's own ends after the length
		// worked out here, a gated one when its gate falls. Handing that length back rather than
		// letting the caller guess at it is the whole point — the caller guessing was a bug, and
		// the duration knob did nothing at all while it lasted.
		noteEndIn[i] = -1.f;
		firing[i].store(1.f);
		return e.duration;
	}

	/** THE TIMERS, one a sprite, stepped every sample so a note lands where it should rather than
	on the nearest physics tick.

	OR ON A CLOCK, WHICH IS THE INTERESTING CASE. With a cable in the trigger input the picture
	goes on deciding how OFTEN each sprite wants to play, and the clock decides WHEN it actually
	does. A sprite reading a bright region wants notes faster than the clock and gets one on every
	tick; one reading a dark region wants them slower and gets one every second, third or fourth.
	Four sprites over four regions therefore give four densities against one grid, and any two
	notes that fall on the same tick fall exactly together.

	ONLY THE RISING EDGE IS READ. A trigger does as well as a gate and its length is ignored, so a
	note's length comes from the duration and articulation settings as it does when free-running —
	measured against the gap the clock actually gave this sprite, which is a whole number of ticks.

	HOW A DUE NOTE FINDS ITS TICK is a setting, because the three answers sound different:
	AFTER waits for the first tick past the moment the timer came due, which tracks the rate it
	wanted on average and gives an uneven, picture-led lilt; NEAREST goes to whichever tick is
	closest, so a note may come a little early but never more than half a tick from where it was
	wanted; DIVIDE works out that the sprite wants about every third tick and plays exactly every
	third until the picture changes its mind, which is strictly regular and steps audibly between
	one division and the next. */
	void stepNotes(const ProcessArgs& args, const Harmony& h, bool running) {
		const bool clocked = inputs[I_CLOCK].isConnected();
		bool edge = false;
		if (clocked) {
			sinceTick += args.sampleTime;
			if (clockTrigger.process(inputs[I_CLOCK].getVoltage(), 0.1f, 1.f)) {
				edge = true;
				// The last gap is the period. A clock that changes speed is followed rather than
				// averaged: a tempo change should take effect at once, not over four bars.
				if (sinceTick > 1e-4f && sinceTick < 30.f)
					clockPeriod = sinceTick;
				sinceTick = 0.f;
			}
		}
		else {
			clockTrigger.reset();
		}
		const int mode = (int) std::lround(params[P_SYNC].getValue());

		for (int i = 0; i < MAX_SPRITES; i++) {
			if (firing[i].load() > 0.f)
				firing[i].store(std::max(0.f, firing[i].load() - args.sampleTime * 6.f));

			if (!enabled(i)) {
				// A SPRITE SWITCHED OFF LETS GO OF WHATEVER IT WAS HOLDING rather than leaving a
				// note sounding for ever with nothing left to end it.
				endNote(i);
				ticksSince[i] = -1;
				continue;
			}

			// A note ends after the length it was given, clocked or not: nothing else ends it.
			if (noteEndIn[i] >= 0.f) {
				noteEndIn[i] -= args.sampleTime;
				if (noteEndIn[i] <= 0.f)
					endNote(i);
			}

			if (clocked) {
				if (!edge)
					continue;
				if (ticksSince[i] >= 0)
					ticksSince[i]++;
				const float rate = rateFor(i);
				const float perTick = clockPeriod * rate;   // how much of an interval a tick is
				bool play = false;

				// THE FIRST TICK AFTER THE CLOCK ARRIVES plays, whatever the phase says. A sprite
				// that waited four seconds for its first note because its timer happened to be
				// near the start of an interval would look broken.
				if (ticksSince[i] < 0) {
					play = true;
					notePhase[i] = 0.f;
					lockCount[i] = 0;
				}
				else if (mode == SYNC_DIVIDE) {
					const int every = math::clamp(
						(int) std::lround(1.f / std::max(perTick, 1e-4f)), 1, 64);
					if (++lockCount[i] >= every) {
						play = true;
						lockCount[i] = 0;
					}
				}
				else {
					notePhase[i] += perTick;
					// NEAREST looks half a tick ahead, so a note due just after this tick comes
					// now rather than a whole tick late.
					const float reach = (mode == SYNC_NEAREST) ? 0.5f * perTick : 0.f;
					if (notePhase[i] + reach >= 1.f) {
						play = true;
						notePhase[i] -= 1.f;
						// A rate far faster than the clock would otherwise build up a debt it
						// could never pay off, and every tick would fire for ever afterwards.
						if (notePhase[i] < -1.f || notePhase[i] > 1.f)
							notePhase[i] = 0.f;
					}
				}

				if (play) {
					// THE GAP THE CLOCK ACTUALLY GAVE IT, a whole number of ticks, which is what
					// the duration setting is a share of. So a sprite playing every fourth tick
					// holds a note four ticks long at full duration and the picture articulates
					// it from there.
					const float gap = (ticksSince[i] > 0)
						? (float) ticksSince[i] * clockPeriod
						: 1.f / std::max(rate, 0.01f);
					noteEndIn[i] = fire(i, gap, h);
					ticksSince[i] = 0;
				}
				continue;
			}

			ticksSince[i] = -1;
			if (!running)
				continue;
			const float rate = rateFor(i);
			notePhase[i] += rate * args.sampleTime;
			if (notePhase[i] >= 1.f) {
				notePhase[i] -= std::floor(notePhase[i]);
				noteEndIn[i] = fire(i, 1.f / std::max(rate, 0.01f), h);
			}
		}
	}

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

		const float dt = 1.f / PHYSICS_HZ;
		const int held = heldSprite.load();

		for (int i = 0; i < MAX_SPRITES; i++) {
			if (!enabled(i))
				continue;
			if (i == held) {
				// HELD STILL WHILE IT IS DRAGGED, and put exactly where the pointer says. A sprite
				// that went on moving under the hand could not be placed.
				sprites[i].x = math::clamp(heldX.load(), 0.f, 1.f);
				sprites[i].y = math::clamp(heldY.load(), 0.f, 1.f);
			}
			else if (running) {
				for (int n = 0; n < due; n++)
					tick(i, dt, topSpeed(i), valueOf(i, SV_DRIFT), valueOf(i, SV_FORCE));
			}
			lastRead[i] = picture.has()
				? channelAt(picture, sprites[i].x, sprites[i].y, readOf(i)) : 0.f;

			// HOW CLOSE THE NEAREST OTHER SPRITE IS: one on top of each other, nought as far apart
			// as the square allows. The only reading here that is about more than one sprite, and
			// the only way the four of them notice each other at all.
			float closest = 1.f;
			for (int j = 0; j < MAX_SPRITES; j++) {
				if (j == i || !enabled(j))
					continue;
				const float dx = sprites[i].x - sprites[j].x;
				const float dy = sprites[i].y - sprites[j].y;
				closest = std::min(closest, std::sqrt(dx * dx + dy * dy) / 1.4142f);
			}
			nearAt[i] = 1.f - closest;
			publish(i);
		}
	}

	void process(const ProcessArgs& args) override {
		followTab();
		stepPhysics(args);
		if (relink.exchange(false)) {
			reader.clear();
			const int n = wantCount.load();
			for (int i = 0; i < n; i++)
				reader.add(wantSlots[i].load(), wantGenerations[i].load());
		}
		// THE CHART PASSES THROUGH, and the sprites' own notes go out with it, so this module can
		// sit anywhere in a chain rather than only at the head of one.
		outputs[O_NOTES].setChannels(1);
		outputs[O_NOTES].setVoltage(0.f);
		Event e;
		while (reader.next(e))
			busPush(slot, e);
		Harmony h;
		const bool haveHarmony = reader.harmony(h);
		if (haveHarmony)
			busPublishHarmony(slot, h);
		stepNotes(args, h, params[P_RUN].getValue() > 0.5f);
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

	/** THE CHOOSER: the picture area given over to a grid of pictures to choose from.

	A CLICK ON THE PICTURE OPENS IT. The picture is the largest thing on the panel and choosing
	another is the commonest thing to want from it, so that is what pressing it does. A click that
	lands on a sprite still takes hold of the sprite; only a click on open picture opens this.

	WHAT IT SHOWS is the twenty pictures used before, whose thumbnails are already on disk, and
	nothing else. Browsing a folder here was tried and taken out: twenty cells is the whole grid,
	and the picture somebody wants is as likely to be the sixtieth in a folder as the second, so a
	grid that pages through a folder is a worse file dialogue rather than a better one. Reaching a
	particular picture is what FILE… is for, and dropping one on the module is quicker still. */
	bool choosing = false;
	int hoverCell = -1;

	struct Cell {
		std::string path;
		std::string name;
	};
	std::vector<Cell> cells;
	/** The nanovg handle for each path's thumbnail. Kept by path, so turning a page back does not
	decode everything again. */
	std::map<std::string, int> art;
	/** One picture is decoded per frame at most: twenty photographs decoded in one go is a visible
	stall, and twenty decoded over a third of a second is a grid filling itself in. */
	bool decodedThisFrame = false;

	static bool looksLikePicture(const std::string& path) {
		const std::string ext = string::lowercase(system::getExtension(path));
		static const char* KNOWN[] = {".png", ".jpg", ".jpeg", ".jpe", ".jfif", ".bmp", ".gif",
			".tga", ".psd", ".hdr", ".pic", ".pnm", ".ppm", ".pgm"};
		for (const char* k : KNOWN)
			if (ext == k)
				return true;
		return false;
	}

	void releaseCells() {
		if (APP && APP->window && APP->window->vg) {
			for (auto& pair : art)
				if (pair.second >= 0)
					nvgDeleteImage(APP->window->vg, pair.second);
		}
		art.clear();
		cells.clear();
	}

	void openChooser() {
		releaseCells();
		choosing = true;
		hoverCell = -1;
		fill();
	}

	void fill() {
		cells.clear();
		for (const PictureMemory& m : pictureHistory()) {
			Cell c;
			c.path = m.path;
			c.name = m.name;
			cells.push_back(c);
		}
	}

	static constexpr float STRIP = 26.f;
	static constexpr float PAD = 5.f;

	math::Rect cellRect(int i) const {
		const float w = (box.size.x - PAD * 6.f) / 5.f;
		const float h = (box.size.y - STRIP - PAD * 5.f) / 4.f;
		const int col = i % 5, row = i / 5;
		return math::Rect(math::Vec(PAD + (w + PAD) * col, STRIP + PAD + (h + PAD) * row),
			math::Vec(w, h));
	}

	/** The commands along the top, left to right, each with the code the hit test returns. */
	enum { HIT_NONE = -1, HIT_FILE = -2, HIT_PASTE = -3, HIT_TAKE = -4 };

	math::Rect stripRect(int which) const {
		const float y = 4.f, h = STRIP - 8.f;
		const float w = box.size.x;
		if (which == HIT_FILE)
			return math::Rect(math::Vec(PAD, y), math::Vec(w * 0.20f, h));
		if (which == HIT_PASTE)
			return math::Rect(math::Vec(PAD + w * 0.21f, y), math::Vec(w * 0.14f, h));
		return math::Rect(math::Vec(w - PAD - w * 0.22f, y), math::Vec(w * 0.22f, h));
	}

	int dragging = DRAG_NONE;
	int dragIndex = -1;
	math::Vec dragAt;

	bool enabled(int i) {
		return module ? module->enabled(i) : (i == 0);
	}

	math::Vec placeOf(int i) {
		return math::Vec(module->seenX[i].load() * box.size.x,
			module->seenY[i].load() * box.size.y);
	}

	~PictureFrame() {
		release();
		releaseCells();
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
				crispText(args.vg, box.size.x / 2.f, box.size.y / 2.f - 8.f, "No picture", NULL);
				crispText(args.vg, box.size.x / 2.f, box.size.y / 2.f + 8.f,
					"Right-click to load one", NULL);
			}
		}

		if (choosing)
			drawChooser(args);
		else
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
	/** Whether the pointer is anywhere on this module — the controls as well as the picture. Set
	from the module widget's step, because a widget only hears about the pointer while it is on
	that widget, and what is wanted here is the whole panel: reaching for a knob is as much a
	moment of wanting to know which sprite is which as reaching for a sprite is. */
	bool pointerOver = false;
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
		// WHILE THE POINTER IS ON THE PICTURE, each sprite wears its own colour instead of the
		// picture's. Wearing what it stands on is right for watching the field work and useless
		// for telling one sprite from another — which is exactly what you need to know at the
		// moment you reach for one, and only then. So the swap costs nothing and lasts as long as
		// the hand is there.
		if (pointerOver)
			return SPRITE_INK[i];
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
		for (int i = 0; i < MAX_SPRITES; i++) {
			if (!enabled(i))
				continue;
			const math::Vec at = placeOf(i);
			const NVGcolor worn = colourUnder(i);
			const NVGcolor edge = ringColour(i);

			nvgBeginPath(args.vg);
			nvgCircle(args.vg, at.x, at.y, DOT_R);
			nvgFillColor(args.vg, worn);
			nvgFill(args.vg);
			// THE RING THICKENS WHEN THE SPRITE SOUNDS, and thins again over a sixth of a second.
			// Which sprite played which note is otherwise impossible to tell with four of them
			// wandering about, and it is the one thing the eye needs that the motion does not
			// already say.
			const float flash = module->firing[i].load();
			nvgStrokeColor(args.vg, edge);
			// THE SELECTED SPRITE WEARS A HEAVIER RING, so that the one the controls are pointed
			// at can be picked out of four without having to guess.
			const bool chosen = (i == math::clamp(module->tab.load(), 0, MAX_SPRITES - 1));
			nvgStrokeWidth(args.vg, (chosen ? 3.2f : 2.f) + 3.f * math::clamp(flash, 0.f, 1.f));
			nvgStroke(args.vg);

			// And a thread of its own colour just outside it, which is what ties it to its tab.
			if (chosen) {
				const NVGcolor ink = SPRITE_INK[i];
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, at.x, at.y, DOT_R + 3.f);
				nvgStrokeColor(args.vg, nvgRGBA(ink.r * 255, ink.g * 255, ink.b * 255, 0xcc));
				nvgStrokeWidth(args.vg, 1.4f);
				nvgStroke(args.vg);
			}
		}
	}

	/** A THUMBNAIL FOR ONE CELL, made on demand and kept.

	A remembered picture already has one on disk and costs a file read. Anything else has to be
	decoded from the picture itself, which is why only one of those is done per frame. */
	int artFor(const DrawArgs& args, const std::string& path) {
		auto found = art.find(path);
		if (found != art.end())
			return found->second;

		std::vector<uint8_t> bytes;
		bool got = false;
		for (const PictureMemory& m : pictureHistory()) {
			if (m.path == path) {
				got = pictureThumb(m, bytes);
				break;
			}
		}
		if (!got) {
			if (decodedThisFrame)
				return -1;      // next frame, so the grid fills in rather than stalling
			decodedThisFrame = true;
			Picture one;
			if (one.load(path)) {
				pictureThumbnail(one.rgba, bytes);
				got = !bytes.empty();
			}
		}
		int handle = -1;
		if (got)
			handle = nvgCreateImageRGBA(args.vg, THUMB_SIDE, THUMB_SIDE, 0, bytes.data());
		art[path] = handle;   // a failure is remembered too, or it is retried every frame
		return handle;
	}

	/** THE GRID: five across and four down, of whichever view is showing. */
	void drawChooser(const DrawArgs& args) {
		decodedThisFrame = false;
		std::shared_ptr<window::Font> font = APP->window->loadFont(
			asset::system("res/fonts/DejaVuSans.ttf"));

		// A ground over the picture, so the grid is read as being in front of it rather than on it.
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGBA(0x10, 0x13, 0x18, 0xf2));
		nvgFill(args.vg);

		auto command = [&](int which, const char* text, bool lit, bool chosen) {
			const math::Rect r = stripRect(which);
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, r.pos.x, r.pos.y, r.size.x, r.size.y, 3.f);
			nvgFillColor(args.vg, lit ? nvgRGBA(0x3d, 0xd6, 0x8c, 0x55)
				: chosen ? nvgRGBA(0x3d, 0xd6, 0x8c, 0x28) : nvgRGBA(0x2a, 0x30, 0x3a, 0xff));
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(0x3d, 0xd6, 0x8c, chosen ? 0xff : 0x99));
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);
			if (font && font->handle >= 0) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 10.f);
				nvgFillColor(args.vg, nvgRGB(0xe6, 0xe8, 0xec));
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				crispText(args.vg, r.pos.x + r.size.x / 2.f, r.pos.y + r.size.y / 2.f, text, NULL);
			}
		};
		command(HIT_FILE, "OPEN A FILE…", hoverCell == HIT_FILE, false);
		command(HIT_PASTE, "PASTE", hoverCell == HIT_PASTE, false);
		if (module && module->picture.has())
			command(HIT_TAKE, "TAKE IT OUT", hoverCell == HIT_TAKE, false);

		// WHAT IS BEING LOOKED AT, between the commands: the name under the pointer, or failing
		// that where this view comes from. A name under every cell would be too small to read.
		if (font && font->handle >= 0) {
			std::string say;
			if (hoverCell >= 0 && hoverCell < (int) cells.size())
				say = cells[hoverCell].name;
			else
				say = "used before — or paste one";
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, nvgRGB(0x9a, 0xa3, 0xaf));
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			crispText(args.vg, box.size.x * 0.52f, 4.f + (STRIP - 8.f) / 2.f, say.c_str(), NULL);
		}

		for (int i = 0; i < 20; i++) {
			const math::Rect r = cellRect(i);
			const bool have = i < (int) cells.size();
			nvgBeginPath(args.vg);
			nvgRect(args.vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
			nvgFillColor(args.vg, nvgRGBA(0x1b, 0x1f, 0x26, 0xff));
			nvgFill(args.vg);

			if (have) {
				const int handle = artFor(args, cells[i].path);
				if (handle >= 0) {
					nvgBeginPath(args.vg);
					nvgRect(args.vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
					nvgFillPaint(args.vg, nvgImagePattern(args.vg, r.pos.x, r.pos.y,
						r.size.x, r.size.y, 0.f, handle, 1.f));
					nvgFill(args.vg);
				}
			}

			// THE ONE THAT IS LOADED is marked, so the grid says where you are as well as where
			// you could go.
			const bool current = have && module && module->picture.has()
				&& cells[i].name == module->picture.name;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, r.pos.x + 0.5f, r.pos.y + 0.5f, r.size.x - 1.f, r.size.y - 1.f);
			nvgStrokeColor(args.vg, (i == hoverCell) ? nvgRGBA(0x3d, 0xd6, 0x8c, 0xff)
				: current ? nvgRGBA(0xff, 0xf1, 0x4a, 0xcc) : nvgRGBA(0x50, 0x5a, 0x6a, 0xcc));
			nvgStrokeWidth(args.vg, (i == hoverCell || current) ? 2.f : 1.f);
			nvgStroke(args.vg);
		}
	}

	/** What the pointer is over while the grid is up: a cell, or one of the commands. */
	int chooserAt(math::Vec pos) {
		if (stripRect(HIT_FILE).contains(pos))
			return HIT_FILE;
		if (stripRect(HIT_PASTE).contains(pos))
			return HIT_PASTE;
		if (module && module->picture.has() && stripRect(HIT_TAKE).contains(pos))
			return HIT_TAKE;
		for (int i = 0; i < 20; i++)
			if (cellRect(i).contains(pos))
				return i;
		return HIT_NONE;
	}

	void onHover(const HoverEvent& e) override {
		if (choosing) {
			hoverCell = chooserAt(e.pos);
			e.consume(this);
			return;
		}
		widget::OpaqueWidget::onHover(e);
	}

	void onLeave(const LeaveEvent& e) override {
		hoverCell = HIT_NONE;
		widget::OpaqueWidget::onLeave(e);
	}

	/** Loads one, notes it, and puts the grid away. */
	void take(const std::string& path) {
		if (!module)
			return;
		if (!module->picture.load(path)) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
				"That picture could not be read.");
			return;
		}
		pictureRemember(path, module->picture.name, module->picture.rgba);
	}

	/** THE DIRECT ROUTE: pick one picture and load it, which is what the system's file dialogue is
	for and what most loads are. It is first on the strip because it is what somebody reaching for
	a particular picture wants; the folder view is for looking rather than for fetching. */
	void chooseFile() {
		osdialog_filters* filters = osdialog_filters_parse(
			// EVERY EXTENSION THE DECODER ACTUALLY HANDLES, so nothing openable is greyed out by us.
			"Pictures:png,jpg,jpeg,jpe,jfif,bmp,gif,tga,psd,hdr,pic,pnm,ppm,pgm");
		const std::string start = pictureFolder();
		char* picked = osdialog_file(OSDIALOG_OPEN,
			start.empty() ? NULL : start.c_str(), NULL, filters);
		osdialog_filters_free(filters);
		if (!picked)
			return;
		const std::string path = picked;
		std::free(picked);
		take(path);
	}

	/** A PICTURE DROPPED ON THE MODULE.

	The quickest way in by a distance: no dialogue, no list, drag the thing onto the picture and it
	is loaded. And it is where a picture from a browser has to come in, since one dragged out of a
	web page has no place on the machine of its own.

	A COPY IS KEPT, in a folder this plugin owns, and it is that copy the recent list points at.
	Anything dropped may be temporary — a browser's scratch file, a download about to be tidied
	away, a memory card — and a remembered picture whose file has gone is a blank cell in the
	chooser and a picture that cannot be brought back. The copies are shared by every module here
	that uses pictures. */
	void onPathDrop(const PathDropEvent& e) override {
		if (!module || e.paths.empty())
			return;
		for (const std::string& path : e.paths) {
			if (!looksLikePicture(path))
				continue;
			if (!module->picture.load(path))
				continue;
			// The bytes the module now holds are the file's own, so the copy is made from those
			// rather than by reading the file a second time.
			const std::string kept = pictureKeepCopy(system::getFilename(path),
				module->picture.file);
			pictureRemember(kept.empty() ? path : kept, module->picture.name,
				module->picture.rgba);
			choosing = false;
			releaseCells();
			e.consume(this);
			return;
		}
	}

	/** A PICTURE OFF THE CLIPBOARD.

	THE SHORTEST WAY FROM SEEING SOMETHING TO HEARING IT. Copy a picture in a browser, paste it
	here. Dragging needs the source and the destination both on the screen at once, which means
	arranging windows before you can begin; copying needs neither of them visible at the same time.

	A COPY IS WRITTEN TO DISK, because what is pasted has no file of its own — it exists only on
	the clipboard, and the list of recent pictures has to point at something that will still be
	there tomorrow.

	AN ADDRESS RATHER THAN A PICTURE is what some browsers put on the clipboard, and it is fetched
	rather than refused: the thing you copied was a picture, whatever the browser chose to hand
	over. */
	void pastePicture() {
		if (!module)
			return;
		std::vector<uint8_t> bytes;
		std::string name, url;
		if (!clipboardPicture(bytes, name, url)) {
			if (url.empty()) {
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
					"There is no picture on the clipboard.");
				return;
			}
			// Fetched to a temporary file, since that is what the downloader writes to, and read
			// back from there.
			const std::string temp = system::getTempDirectory() + "/mpxPasted";
			if (!network::requestDownload(url, temp, NULL)) {
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
					"That picture could not be fetched.");
				return;
			}
			if (!module->picture.load(temp)) {
				system::remove(temp);
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
					"What was fetched could not be read as a picture.");
				return;
			}
			bytes = module->picture.file;
			system::remove(temp);
		}
		else if (!module->picture.decode(bytes, name.empty() ? "pasted.png" : name)) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
				"What was on the clipboard could not be read as a picture.");
			return;
		}
		const std::string kept = pictureKeepCopy(name.empty() ? "pasted.png" : name, bytes);
		if (kept.empty()) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
				"The picture was loaded but could not be kept for the list.");
			return;
		}
		pictureRemember(kept, module->picture.name, module->picture.rgba);
	}

	/** WHICH SPRITE THE POINTER IS ON, if any. The topmost wins where two overlap. */
	bool whatIsHere(math::Vec pos, int* which) {
		for (int i = MAX_SPRITES - 1; i >= 0; i--) {
			if (enabled(i) && placeOf(i).minus(pos).norm() <= GRAB) {
				*which = i;
				return true;
			}
		}
		return false;
	}

	void onButton(const ButtonEvent& e) override {
		if (choosing && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			const int at = chooserAt(e.pos);
			if (at == HIT_FILE) {
				chooseFile();
				choosing = false;
				releaseCells();
			}
			else if (at == HIT_PASTE) {
				pastePicture();
				choosing = false;
				releaseCells();
			}
			else if (at == HIT_TAKE) {
				if (module)
					module->picture.clear();
				choosing = false;
				releaseCells();
			}
			else if (at >= 0 && at < (int) cells.size()) {
				take(cells[at].path);
				choosing = false;
				releaseCells();
			}
			else {
				// A click on none of it puts the grid away, which is what clicking off a thing
				// means everywhere else.
				choosing = false;
				releaseCells();
			}
			hoverCell = HIT_NONE;
			e.consume(this);
			return;
		}
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			int which = -1;
			if (whatIsHere(e.pos, &which)) {
				// TOUCHING A SPRITE SELECTS IT, so the controls below follow the hand rather than
				// having to be pointed at a tab first.
				module->tab.store(which);
				dragging = DRAG_MOVE;
				dragIndex = which;
				dragAt = e.pos;
				e.consume(this);
				return;
			}
			// NOT ON A SPRITE, so it is the picture that was pressed, and pressing the picture is
			// how another one is chosen.
			openChooser();
			e.consume(this);
			return;
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


/** THE TABS: which sprite the controls below them belong to.

FOUR TABS RATHER THAN A FIFTH SET OF KNOBS. Nearly every control on this panel belongs to one
sprite, and showing all four sprites' worth at once would be a panel nobody could read. So one set
of controls is shown and the tabs say whose.

EACH IN ITS SPRITE'S OWN COLOUR, which is the only place those four colours are still used now that
the discs wear the picture instead. A tab is therefore recognised without being read. */
struct TabStrip : widget::OpaqueWidget {
	SpritesModule* module = NULL;

	int at(math::Vec pos) {
		if (box.size.x <= 0.f)
			return -1;
		const int i = (int) (pos.x / (box.size.x / MAX_SPRITES));
		return math::clamp(i, 0, MAX_SPRITES - 1);
	}

	/** WHERE A TAB'S OWN LIGHT SITS, at its left-hand end. Switching a sprite on lived on the
	panel as a button, which cost a whole band of height to say one word about one sprite. On the
	tab it costs nothing at all, it is next to the number it belongs to, and all four states are
	visible at once instead of only the one whose tab is showing. */
	math::Rect lampRect(int i) const {
		const float w = box.size.x / MAX_SPRITES;
		const float r = std::min(w * 0.18f, box.size.y * 0.34f);
		return math::Rect(math::Vec(i * w + 4.f, box.size.y / 2.f - r), math::Vec(2.f * r, 2.f * r));
	}

	void onButton(const ButtonEvent& e) override {
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			const int i = at(e.pos);
			if (i >= 0) {
				// THE LIGHT SWITCHES THAT SPRITE ON OR OFF; the rest of the tab selects it. A tab
				// that had to be selected before its sprite could be switched on would make
				// turning the fourth one on a two-click job.
				if (lampRect(i).contains(e.pos))
					module->flipEnable.store(i);
				else
					module->tab.store(i);
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(
			asset::system("res/fonts/DejaVuSans.ttf"));
		const int chosen = module ? math::clamp(module->tab.load(), 0, MAX_SPRITES - 1) : 0;

		const float w = box.size.x / MAX_SPRITES;

		for (int i = 0; i < MAX_SPRITES; i++) {
			const float x = i * w;
			const bool on = (i == chosen);
			// EVERY TAB IS DRAWN AT FULL STRENGTH, whether its sprite is playing or not. The light
			// beside the number already says which are on, and fading the others as well made
			// three of the four numbers hard to read for no information anybody needed twice.
			const float fade = 1.f;
			const NVGcolor ink = SPRITE_INK[i];

			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x + 0.75f, 0.75f, w - 1.5f, box.size.y - 1.5f, 2.f);
			nvgFillColor(args.vg, on
				? nvgRGBA(ink.r * 255, ink.g * 255, ink.b * 255, (unsigned char) (210 * fade))
				: nvgRGBA(0x2a, 0x30, 0x3a, (unsigned char) (255 * fade)));
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(ink.r * 255, ink.g * 255, ink.b * 255,
				(unsigned char) ((on ? 255 : 150) * fade)));
			nvgStrokeWidth(args.vg, on ? 1.4f : 0.8f);
			nvgStroke(args.vg);

			// THE LIGHT: red for a sprite that is playing, dark for one that is not. Red because
			// that is what a reader already takes to mean live, and the same red as the button
			// this replaced.
			{
				const math::Rect r = lampRect(i);
				const bool live = module && module->enabled(i);
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, r.pos.x + r.size.x / 2.f, r.pos.y + r.size.y / 2.f,
					r.size.x / 2.f);
				nvgFillColor(args.vg, live ? nvgRGB(0xff, 0x4a, 0x40) : nvgRGB(0x6a, 0x74, 0x82));
				nvgFill(args.vg);
				// A PALE RIM RATHER THAN A BLACK ONE. Unlit, the lamp was a dark disc ringed in
				// black on a dark tab, which is three ways of being invisible at once. The rim is
				// now lighter than anything behind it, so the lamp is found whether it is on or
				// not — which is the whole point of it being on a tab that is not selected.
				nvgStrokeColor(args.vg, live ? nvgRGBA(0xff, 0xc0, 0xb8, 0xdd)
					: nvgRGBA(0xc0, 0xc8, 0xd2, 0xcc));
				nvgStrokeWidth(args.vg, 1.4f);
				nvgStroke(args.vg);
			}

			if (font && font->handle >= 0) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 9.f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFillColor(args.vg, on ? nvgRGBA(0x14, 0x18, 0x1e, 255)
					: nvgRGBA(0xc8, 0xd0, 0xd8, (unsigned char) (255 * fade)));
				const std::string name = string::f("%d", i + 1);
				// Placed on a whole pixel and drawn twice, as every other piece of lettering on an
				// MPX panel is: see Panel::crisp.
				crispText(args.vg, x + w * 0.62f, box.size.y / 2.f, name.c_str(), NULL);
			}
		}
		widget::OpaqueWidget::draw(args);
	}
};


/** THE PANEL'S MEASUREMENTS, all of them following from one decision: the picture is a square as
tall as the panel. A rack module is 128.5 millimetres high, so with a hair off the top and bottom
for the panel's own green border to show, the square is 125.5 across whatever the module's width.

THE WIDTH IS THEN CHOSEN FOR THE COLUMN, not for the picture. Thirty HP left five for the column,
36 left ten, and neither holds what the module turned out to need: ten knobs, two rows of lamps,
two buttons and three jacks. At 42 HP the column is 84.9 millimetres, which takes three knobs
abreast and leaves the rows far enough apart to read. */
static const float PANEL_H = 128.5f;
static const float PANEL_HP = 42.f;
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
			const std::string& name, bool mpx = true) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y;
		if (mpx)
			i.ring = NOTE_CABLE;
		L.items.push_back(i);
		label(key + ".label", x, y + 6.6f, name, 0.f, key);
	};

	// THE SQUARE, the whole height of the panel bar the border, running past the title band and
	// the screws alike. A picture is what this module is for and a picture wants to be large.
	{
		Item i;
		i.key = "d.picture"; i.kind = Item::DISPLAY;
		i.x = PANEL_W - INSET - SQUARE; i.y = INSET; i.w = SQUARE; i.h = SQUARE;
		L.items.push_back(i);
	}


	// SMALLER THAN THE USUAL KNOB, and by as much as the controls need. Twelve of them belong to
	// each sprite and they all have to be on the panel at once; a knob still shows its arc at this
	// size, which a plate carrying digits would not.
	auto knob = [&](const std::string& key, float x, float y, int id, const std::string& name,
			float below = 7.2f) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.ticks = 2;
		i.style = "knob.small";
		L.items.push_back(i);
		// THE PANEL'S OWN SIZE, not a smaller one. The knobs shrank to fit twelve of them in a
		// column; their names did not, because a name too small to read is not a saving. A name
		// set on two lines sits a little further down, so that both lines clear the knob.
		label(key + ".label", x, y + below, name, 0.f, key);
	};

	auto button = [&](const std::string& key, float x, float y, int id, const std::string& style,
			const std::string& name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y;
		i.style = style; i.diameter = 6.6f;
		L.items.push_back(i);
		label(key + ".label", x, y + 6.2f, name, 7.f, key);
	};

	// THE TABS AT THE TOP, under the module's name and above everything else, because everything
	// else in the column belongs to whichever sprite they are pointing at.
	{
		Item i;
		i.key = "d.tabs"; i.kind = Item::DISPLAY;
		i.x = 6.f; i.y = 8.f; i.w = COLUMN - 12.f; i.h = 8.5f;
		L.items.push_back(i);
	}

	// FIVE ABREAST AND SMALLER, which is what thirteen per-sprite knobs and one column cost. The
	// rows are bands: what the picture does to a sprite, how fast it may go and how often it
	// plays, and what the note itself is.
	// EVERY CONTROL UNDER A TITLE SAYING WHAT IT IS FOR.
	//
	// Thirteen knobs and four plates in one undifferentiated block is a panel you read by hunting.
	// Grouped, it is a panel you read by knowing roughly where a thing lives: what the picture
	// does to a sprite, how the sprite then travels, when it plays, and what the note is. A title
	// and a rule cost four millimetres and save the hunting.
	// THE COLUMNS. One for the plates, three for the knobs beside them, and five for the motion
	// band which has no plate and therefore the whole width.
	static const float PLATE_X = 19.f;
	static const float KNOBS_X = 33.f;
	static const float KNOBS_W = 47.f;
	const float kx[3] = {41.f, 57.5f, 74.f};
	const float mx[5] = {12.f, 28.5f, 45.f, 61.5f, 78.f};

	const float wide = COLUMN - 10.f;

	// A GROUP'S TITLE SITS OVER ITS KNOBS, not over the whole width, because the parameter column
	// to its left is not part of that group — it is one column running the height of the panel,
	// with its own heading at the top.
	auto group = [&](const std::string& key, float y, const std::string& title) {
		label("l." + key, KNOBS_X + KNOBS_W / 2.f, y, title);
		Item i;
		i.key = "r." + key; i.kind = Item::RULE; i.horizontal = true;
		// ALL THE WAY ACROSS, the parameter column included. The line says where a group starts,
		// and the parameter on its left belongs to that group as much as the knobs do — stopping
		// the line at the column's edge said the opposite.
		i.x = 5.f; i.y = y + 2.5f; i.w = wide;
		L.items.push_back(i);
	};

	// A FEW WORDS ON EVERY CHOICE. The names on these plates are one word each and several of them
	// — odd, corner, push, near — say nothing at all to somebody meeting them. The list spells
	// them out, and Option held reads the line.
	static const std::vector<std::string> SOURCE_NOTES = {
		"how light or dark the picture is here",
		"how red the colour is against cyan",
		"how yellow it is against blue",
		"how strong that colour is, grey to vivid",
		"how fast the picture is changing here — high on boundaries, nothing on flat ground",
		"how unlike this picture's own average this region is",
		"how far up the picture the sprite is, one at the top",
		"how far across, one at the right",
		"how far from the middle, one at the corners",
		"how fast the sprite is going, against its own ceiling",
		"how eastward it is heading",
		"how northward it is heading",
		"how sharply it is turning — nothing running straight, bursting where the picture swings it",
		"how hard the picture is shoving it at this instant",
		"how close the nearest other sprite is",
		"which sprite this is, a fixed value for each",
		"a fresh random number for every note",
	};

	// A PLATE NEEDS NO NAME OF ITS OWN. It sits in the parameter column, level with the knobs it
	// drives, and those two facts say everything a name would have said — the word PARAMETER once
	// at the head of the column does the work it was doing four times over. Only where a group has
	// two does each say which of the two it is.
	auto plate = [&](const std::string& key, float y, int id, const std::string& which = "") {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = PLATE_X; i.y = y; i.style = "readout";
		i.chars = 8; i.h = 2.8f; i.notes = SOURCE_NOTES;
		L.items.push_back(i);
		if (!which.empty())
			label(key + ".group", PLATE_X, y - 4.2f, which, 0.f, key);
	};

	// THE PARAMETER FIRST, THEN THE KNOBS THAT SHAPE IT.
	//
	// Each group is one subject, and the plate on its left is what drives that subject: the
	// reading the sprite takes. The knobs to the right say how far it drives, between what limits,
	// and whatever else belongs to the same question. Reading a group left to right therefore
	// reads as a sentence — this is driven by that, this much, between here and here.
	//
	// The plates line up at one x and the knobs at three more, so the panel is columns as well as
	// rows whichever group you are looking at.

	// EVERYTHING THE PICTURE AND THE PHYSICS DO TO A SPRITE, in one band. No parameter: none of
	// these is driven by a reading — they are the terms of the motion itself.
	// THE MOTION BAND HAS NO PARAMETER and therefore the whole width: none of these five is driven
	// by a reading, they are the terms of the motion itself.
	{
		label("l.force", 42.43f, 19.f, "SPRITE MOTION");
		Item i;
		i.key = "r.force"; i.kind = Item::RULE; i.horizontal = true;
		i.x = 5.f; i.y = 21.5f; i.w = wide;
		L.items.push_back(i);
	}
	knob("p.force", mx[0], 28.5f, SpritesModule::P_FORCE, "ACCEL-\nERATION", 9.f);
	knob("p.turn", mx[1], 28.5f, SpritesModule::P_TURN, "ACCEL.\nDIRECTION", 9.f);
	knob("p.blend", mx[2], 28.5f, SpritesModule::P_BLEND, "TRANS-\nVERSE", 9.f);
	knob("p.speed", mx[3], 28.5f, SpritesModule::P_SPEED, "PEAK\nSPEED", 9.f);
	knob("p.drift", mx[4], 28.5f, SpritesModule::P_DRIFT, "RANDOM\nDRIFT", 9.f);

	// NOTE TIMING, read left to right: the parameter, then how far it reaches, then the two limits
	// it reaches between. Depth belongs at the head of the row for that reason — it is about the
	// parameter beside it rather than about the rate itself.
	// A LINE UNDER THE MOTION BAND, closing it off: what is above belongs to how a sprite moves
	// and what is below to what it plays, and without a line the two run together.
	{
		Item i;
		i.key = "r.motion"; i.kind = Item::RULE; i.horizontal = true;
		i.x = 5.f; i.y = 41.f; i.w = wide;
		L.items.push_back(i);
	}

	// THE PARAMETER COLUMN: one heading at the top of it, and a line down its right-hand side
	// saying where it ends and the groups begin.
	label("l.param", PLATE_X, 46.5f, "PARAMETERS");
	{
		Item i;
		i.key = "r.param"; i.kind = Item::RULE; i.horizontal = false;
		i.x = 30.5f; i.y = 43.5f; i.h = 74.f;
		L.items.push_back(i);
	}

	group("when", 46.5f, "NOTE TIMING");
	plate("p.srcRate", 57.5f, SpritesModule::P_SRC_RATE);
	knob("p.depth", kx[0], 56.5f, SpritesModule::P_DEPTH, "DEPTH");
	knob("p.slow", kx[1], 56.5f, SpritesModule::P_SLOW, "MIN RATE");
	knob("p.fast", kx[2], 56.5f, SpritesModule::P_FAST, "MAX RATE");

	group("high", 69.f, "NOTE PITCH");
	plate("p.srcPitch", 79.f, SpritesModule::P_SRC_PITCH);
	knob("p.register", kx[0], 78.f, SpritesModule::P_REGISTER, "REGISTER");
	knob("p.range", kx[1], 78.f, SpritesModule::P_RANGE, "RANGE");

	// TWO PARAMETERS IN ONE GROUP, so each says which of the two it drives. Amplitude and duration
	// are one subject — how a note is played rather than which note it is — and articulation moves
	// either of them from note to note, so it belongs with both and the group cannot be split
	// without splitting it.
	group("struck", 90.5f, "HOW PLAYING");
	plate("p.srcLevel", 100.5f, SpritesModule::P_SRC_LEVEL, "AMPLITUDE");
	plate("p.srcDur", 110.5f, SpritesModule::P_SRC_DUR, "DURATION");
	knob("p.level", kx[0], 99.f, SpritesModule::P_LEVEL, "AMPLITUDE");
	knob("p.duration", kx[1], 99.f, SpritesModule::P_DURATION, "DURATION");
	knob("p.artic", kx[2], 99.f, SpritesModule::P_ARTIC, "ARTIC-\nULATION", 9.f);

	// THE FOOT, all of it shared: the transport, and the three jacks. The transport moved down
	// here when the tabs went to the top, which is where it belongs anyway — it is not a per-sprite
	// control and it was the only shared thing left up there.
	const float foot = 120.f;
	button("p.run", 8.f, foot, SpritesModule::P_RUN, "transport.play", "RUN");
	button("p.rewind", 21.f, foot, SpritesModule::P_REWIND, "transport.rewind", "REWIND");
	jack("in.chart", Item::PORT_IN, 38.f, foot, SpritesModule::I_CHART, "MPX IN");
	jack("in.clock", Item::PORT_IN, 54.f, foot, SpritesModule::I_CLOCK, "TRIG", false);
	jack("out.notes", Item::PORT_OUT, 76.f, foot, SpritesModule::O_NOTES, "MPX OUT");

	// THE LABELS ARE TIED TO WHAT THEY NAME. Each one's offset from its control is taken from
	// the positions above, so that moving a control in the panel editor takes its name with it.
	// Without this every offset is nought, and the first layout anybody saves puts every name
	// underneath the control it belongs to, where it cannot be seen.
	L.bindOffsets();
	return L;
}


struct SpritesWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;
	/** The picture, kept so the module's own menu can reach what it can do. */
	PictureFrame* frame = NULL;

	SpritesWidget(SpritesModule* module) {
		setModule(module);
		layout = spritesLayout();
		layoutApplyUser("mpxSprites", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);

		frame = new PictureFrame;
		frame->module = module;
		layoutPlaceDisplay(this, layout, "d.picture", frame);

		TabStrip* tabs = new TabStrip;
		tabs->module = module;
		layoutPlaceDisplay(this, layout, "d.tabs", tabs);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxSprites");
		SpritesModule* m = dynamic_cast<SpritesModule*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Sync", {"After", "Nearest", "Divide"},
			[=]() { return (int) std::lround(m->params[SpritesModule::P_SYNC].getValue()); },
			[=](int i) { m->params[SpritesModule::P_SYNC].setValue((float) i); }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Reset the controls", "", [=]() {
			// The picture stays: see resetControls.
			m->resetControls();
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Paste a picture", "", [=]() {
			if (frame)
				frame->pastePicture();
		}));
		menu->addChild(createMenuItem("Load a picture…", "", [=]() {
			osdialog_filters* filters = osdialog_filters_parse(
				// EVERY EXTENSION THE DECODER ACTUALLY HANDLES, so nothing openable is greyed out by us.
			"Pictures:png,jpg,jpeg,jpe,jfif,bmp,gif,tga,psd,hdr,pic,pnm,ppm,pgm");
			// OPENS WHERE YOU WERE LAST TIME. Pictures come out of one folder, and a chooser that
			// starts somewhere else is a correction to be made on every single load.
			const std::string folder = pictureFolder();
			char* path = osdialog_file(OSDIALOG_OPEN,
				folder.empty() ? NULL : folder.c_str(), NULL, filters);
			osdialog_filters_free(filters);
			if (!path)
				return;
			const std::string chosen = path;
			std::free(path);
			if (!m->picture.load(chosen)) {
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
					"That file could not be read as a picture.");
				return;
			}
			pictureRemember(chosen, m->picture.name, m->picture.rgba);
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
	}

	/** What the picture's average was last measured for, so it is measured again only when
	something it depends on has changed. */
	uint32_t measuredFor[MAX_SPRITES] = {0, 0, 0, 0};
	int measuredSteer[MAX_SPRITES] = {-1, -1, -1, -1};
	int measuredPush[MAX_SPRITES] = {-1, -1, -1, -1};
	/** The same for the dimmed copy the panel draws. */
	uint32_t dimmedFor = 0;
	int dimmedGlare = -1;

	void step() override {
		ModuleWidget::step();
		// WHILE THE POINTER IS ANYWHERE ON THIS MODULE, the sprites wear their own colours. Asked
		// here rather than in the picture, which only hears about the pointer while it is on the
		// picture itself.
		if (frame && APP->scene && APP->scene->rack)
			frame->pointerOver = box.contains(APP->scene->rack->getMousePos());

		SpritesModule* m = dynamic_cast<SpritesModule*>(module);
		if (!m)
			return;

		// MEASURED ON THIS THREAD, never on the one that moves the sprites: it reads the whole
		// square, which is far too long to spend inside the engine.
		for (int i = 0; i < MAX_SPRITES; i++) {
			const int steer = m->steerOf(i);
			const int push = m->pushOf(i);
			if (m->picture.generation != measuredFor[i] || steer != measuredSteer[i]
					|| push != measuredPush[i]) {
				measuredFor[i] = m->picture.generation;
				measuredSteer[i] = steer;
				measuredPush[i] = push;
				m->measureRange();
				m->measure(i);
			}
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
