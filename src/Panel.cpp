#include "plugin.hpp"

#include <algorithm>
#include <cmath>

namespace px {


// The same face the other Dreamer Development modules use, so a rack with both in it does not
// look like two racks. No artwork ships with the plugin: every panel is drawn here.
const NVGcolor PANEL_BG = nvgRGB(0x1b, 0x1f, 0x26);
const NVGcolor PANEL_INK = nvgRGB(0xe6, 0xe8, 0xec);
const NVGcolor PANEL_DIM = nvgRGB(0x9a, 0xa3, 0xaf);
const NVGcolor PANEL_EDGE = nvgRGB(0x3d, 0xd6, 0x8c);
/** THE DIVIDING LINES, half as bright again as they were. At 0x35,0x3c,0x47 a rule between two
groups of controls was there if you looked for it and invisible if you did not, which is no use
for a line whose whole job is to say that what is on one side of it is not what is on the other. */
const NVGcolor PANEL_RULE = nvgRGB(0x50, 0x5a, 0x6a);

// COLOUR SAYS WHAT KIND OF SIGNAL, and it is the same code here as in DreamRack and in
// Clarity: yellow for audio, orange for control voltage, blue for gates, green for pitch.
// Learning it once should be enough.
const NVGcolor SIG_AUDIO = nvgRGB(0xf3, 0xc4, 0x0b);
const NVGcolor SIG_CV = nvgRGB(0xff, 0x73, 0x00);
const NVGcolor SIG_GATE = nvgRGB(0x5a, 0xa0, 0xe6);
const NVGcolor SIG_PITCH = nvgRGB(0x39, 0xa8, 0x5a);

// MPX ports are drawn in this, and so is any cable landing on one, so the domain shows in a
// patch without anybody having to remember what was plugged in where. Magenta: no signal family
// uses it, and it is not a colour Rack offers from its own palette either.
const NVGcolor NOTE_CABLE = nvgRGB(0xff, 0x3c, 0xc8);


static std::shared_ptr<window::Font> bodyFont() {
	return APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
}

/** THE MUSIC FONT, for a note symbol a plate shows in place of a word: Petaluma, as the chart
uses. */
static std::shared_ptr<window::Font> musicFont() {
	return APP->window->loadFont(asset::plugin(pluginInstance, "res/Petaluma.otf"));
}

/** A NOTE SYMBOL, as Readout::glyphs writes it, drawn to fit a height and centred on `cx`. The
note's head sits on the baseline and its stem rises about seven eighths of an em above it, so an
em is the whole height. A dotted note has the augmentation dot after the head; a triplet has a
small 3 over the note. */
static void drawNoteSymbol(NVGcontext* vg, float cx, float top, float height, std::string g,
	NVGcolor colour) {
	std::shared_ptr<window::Font> music = musicFont();
	if (!music || music->handle < 0 || g.empty())
		return;
	bool dotted = false, triplet = false;
	if (g.back() == '.') {
		dotted = true;
		g.pop_back();
	}
	else if (g.back() == '3') {
		triplet = true;
		g.pop_back();
	}
	// A TRIPLET'S NOTE IS SMALLER, to leave the 3 room above it inside the same height.
	const float size = triplet ? height * 0.72f : height;
	const float baseline = top + height - 0.125f * size;
	nvgFontFaceId(vg, music->handle);
	nvgFontSize(vg, size);
	nvgFillColor(vg, colour);
	const float noteW = nvgTextBounds(vg, 0.f, 0.f, g.c_str(), NULL, NULL);
	static const char* DOT = "\uE1E7";      // augmentationDot
	static const char* THREE = "\uE883";    // tuplet3
	const float dotW = dotted ? 0.45f * size : 0.f;
	const float left = cx - (noteW + dotW) / 2.f;
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
	nvgText(vg, left, baseline, g.c_str(), NULL);
	if (dotted)
		nvgText(vg, left + noteW + 0.12f * size, baseline, DOT, NULL);
	if (triplet) {
		nvgFontSize(vg, height * 0.42f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		nvgText(vg, left + noteW * 0.5f, top - height * 0.02f, THREE, NULL);
	}
}

static std::shared_ptr<window::Font> titleFont() {
	return APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
}

static int alignFlag(Panel::Align a) {
	if (a == Panel::LEFT)
		return NVG_ALIGN_LEFT;
	if (a == Panel::RIGHT)
		return NVG_ALIGN_RIGHT;
	return NVG_ALIGN_CENTER;
}


/** Text as the panel draws it: as it is, or crisp — see Panel::crisp.

PLACED ON A WHOLE PIXEL. The transform gives where the point lands on the screen; rounding that and
mapping it back puts the text's origin exactly on a pixel boundary, so its edges fall on the pixel
grid far more often and fewer edge pixels are left half covered. Panels are never rotated, so the
transform's scale and offset are all there is to invert.

DRAWN TWICE. A pixel along an edge that one pass leaves half covered is three-quarters covered after
two, which shortens the grey fringe round each letter without thickening it. Three passes were
tried and made the letters too heavy. */
static void panelText(NVGcontext* vg, float x, float y, const char* text, bool crisp) {
	if (!crisp) {
		nvgText(vg, x, y, text, NULL);
		return;
	}
	float t[6];
	nvgCurrentTransform(vg, t);
	if (t[0] != 0.f && t[3] != 0.f) {
		x = (std::round(t[0] * x + t[4]) - t[4]) / t[0];
		y = (std::round(t[3] * y + t[5]) - t[5]) / t[3];
	}
	for (int i = 0; i < 2; i++)
		nvgText(vg, x, y, text, NULL);
}


/** Where a point lands on a whole screen pixel, mapped back into the current coordinates. */
static void snapToPixel(NVGcontext* vg, float& x, float& y) {
	float t[6];
	nvgCurrentTransform(vg, t);
	if (t[0] != 0.f && t[3] != 0.f) {
		x = (std::round(t[0] * x + t[4]) - t[4]) / t[0];
		y = (std::round(t[3] * y + t[5]) - t[5]) / t[3];
	}
}

/** Returns where the text ended, as nvgText does, so a run of pieces can be set one after another. */
float crispText(NVGcontext* vg, float x, float y, const char* text, const char* end) {
	snapToPixel(vg, x, y);
	nvgText(vg, x, y, text, end);
	return nvgText(vg, x, y, text, end);
}

void crispTextBox(NVGcontext* vg, float x, float y, float width, const char* text, const char* end) {
	snapToPixel(vg, x, y);
	for (int i = 0; i < 2; i++)
		nvgTextBox(vg, x, y, width, text, end);
}


void Panel::draw(const DrawArgs& args) {
	nvgBeginPath(args.vg);
	nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
	nvgFillColor(args.vg, PANEL_BG);
	nvgFill(args.vg);

	std::shared_ptr<window::Font> font = bodyFont();
	std::shared_ptr<window::Font> face = titleFont();
	if (!font || font->handle < 0)
		return;

	nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

	// The title band sits inside the border rather than under it: drawn to the panel's edge it
	// covered the line along the top, which read as a border someone had forgotten to finish.
	//
	// SHALLOWER WITH NOTHING ABOVE THE NAME. The band was tall enough for two lines because it
	// carried the maker's name over the module's; with the module's name alone it is as tall as
	// that name needs, and the panel below it gains what is left.
	const float bandH = titleAbove.empty() ? 17.f : 25.f;
	// The whole width unless the module has asked for a column of it.
	const float bandRight = (titleBandWidth > 0.f)
		? std::min(titleBandWidth, box.size.x) : box.size.x;
	const float bandW = std::max(8.f, bandRight - 8.f);
	const float bandMid = 4.f + bandW / 2.f;
	nvgBeginPath(args.vg);
	nvgRect(args.vg, 4.f, 4.f, bandW, bandH);
	nvgFillColor(args.vg, nvgRGB(0x24, 0x2a, 0x33));
	nvgFill(args.vg);

	nvgFontFaceId(args.vg, (face && face->handle >= 0) ? face->handle : font->handle);
	nvgFillColor(args.vg, PANEL_INK);
	// AS LARGE AS FITS, up to the size it would otherwise be. A narrow band is the point of the
	// setting, so a name that will not go in it is shrunk rather than cut off or spilled.
	auto sized = [&](float want, const std::string& text) {
		nvgFontSize(args.vg, want);
		if (text.empty())
			return;
		float bounds[4] = {0.f, 0.f, 0.f, 0.f};
		nvgTextBounds(args.vg, 0.f, 0.f, text.c_str(), NULL, bounds);
		const float wide = bounds[2] - bounds[0];
		const float room = bandW - 4.f;
		if (wide > room && wide > 0.f)
			nvgFontSize(args.vg, std::max(6.f, want * room / wide));
	};
	if (titleAbove.empty()) {
		sized(13.5f, title);
		panelText(args.vg, bandMid, 4.f + bandH / 2.f, title.c_str(), crisp);
	}
	else {
		sized(8.f, titleAbove);
		nvgFillColor(args.vg, PANEL_INK);
		panelText(args.vg, bandMid, 9, titleAbove.c_str(), crisp);
		nvgFillColor(args.vg, PANEL_INK);
		sized(14.f, title);
		panelText(args.vg, bandMid, 21, title.c_str(), crisp);
	}

	for (float y : rules) {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 9.f, y);
		nvgLineTo(args.vg, box.size.x - 9.f, y);
		nvgStrokeColor(args.vg, PANEL_RULE);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
	}

	for (const Rule& r : lines) {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, r.x, r.y);
		if (r.horizontal)
			nvgLineTo(args.vg, r.x + r.len, r.y);
		else
			nvgLineTo(args.vg, r.x, r.y + r.len);
		nvgStrokeColor(args.vg, PANEL_RULE);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
	}

	for (const Bracket& b : brackets) {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, b.x + b.arm, b.y);
		nvgLineTo(args.vg, b.x, b.y);
		nvgLineTo(args.vg, b.x, b.y + b.h);
		nvgLineTo(args.vg, b.x + b.arm, b.y + b.h);
		nvgStrokeColor(args.vg, PANEL_DIM);
		nvgStrokeWidth(args.vg, 1.2f);
		nvgLineCap(args.vg, NVG_BUTT);
		nvgLineJoin(args.vg, NVG_MITER);
		nvgStroke(args.vg);
	}

	// THE MARKS ROUND A KNOB, drawn under the knob itself since the panel is painted first.
	//
	// THE SWEEP IS 0.83 PI EITHER WAY, which is what RoundKnob sets minAngle and maxAngle to —
	// two hundred and ninety-nine degrees, not the two hundred and seventy a knob looks like it
	// turns. Marks that disagree with the pointer are worse than no marks at all, so this number
	// is read from componentlibrary.hpp rather than assumed.
	for (const Scale& sc : scales) {
		const int n = std::max(2, sc.count);
		for (int i = 0; i < n; i++) {
			const float t = (float) i / (float) (n - 1);
			const float a = (-0.83f + 1.66f * t) * M_PI;
			const float dx = std::sin(a);
			const float dy = -std::cos(a);
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, sc.x + dx * sc.radius, sc.y + dy * sc.radius);
			nvgLineTo(args.vg, sc.x + dx * (sc.radius + sc.length),
				sc.y + dy * (sc.radius + sc.length));
			nvgStrokeColor(args.vg, PANEL_DIM);
			nvgStrokeWidth(args.vg, 1.2f);
			nvgLineCap(args.vg, NVG_ROUND);
			nvgStroke(args.vg);

			if (i < (int) sc.marks.size() && font && font->handle >= 0) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, sc.textSize);
				nvgFillColor(args.vg, PANEL_INK);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				// THE TOP OF THE CIRCLE IS FLATTENED, and this is the reason.
				//
				// Every mark sits the same distance from the knob's CENTRE, which is not the same
				// as sitting the same distance from anything a reader can see. The ends of the
				// sweep are at 0.83 of a half turn, so they stand 0.861 of that radius above or
				// below the centre — while a mark at the top of the sweep stands the whole radius
				// above it. A three-mark scale therefore put its middle word a seventh further
				// out than the two beside it, which looked like a mistake and, on a column of
				// knobs, reached up into the name of the knob above.
				//
				// So the vertical offset is limited to what the ends of the sweep use. Sideways
				// the radius is untouched: it is only the top and bottom of the circle that have
				// further to reach.
				const float limit = 0.861f * sc.textRadius;
				const float ty = math::clamp(dy * sc.textRadius, -limit, limit);
				panelText(args.vg, sc.x + dx * sc.textRadius, sc.y + ty, sc.marks[i].c_str(), crisp);
			}
		}
	}

	for (const Label& label : labels) {
		// A deleted label is gone, in the editor as well as out of it. A ghost of it was
		// meant to make the deletion reversible and instead made the panel impossible to
		// judge, which is the one thing the editor is for.
		if (label.hidden)
			continue;
		nvgFontFaceId(args.vg, label.heading && face && face->handle >= 0
			? face->handle : font->handle);
		nvgFontSize(args.vg, label.size > 0.f ? label.size : (label.heading ? 10.f : 8.f));
		// ALL OF IT WHITE. Half the names on a panel were set in a grey two thirds as bright as
		// the rest, which made a panel look like two panels and made the smaller names the
		// hardest thing on it to read. A name is a name.
		nvgFillColor(args.vg, PANEL_INK);
		nvgTextAlign(args.vg, alignFlag(label.align) | NVG_ALIGN_MIDDLE);
		// A NAME MAY BE SEVERAL LINES, split on the newlines the properties menu writes when a
		// slash is typed. Set about the point it is placed at rather than downward from it, so
		// that turning one line into two grows the name in both directions and leaves it still
		// looking centred on whatever it names.
		const float size = label.size > 0.f ? label.size : (label.heading ? 10.f : 8.f);
		const float step = panelLineStep(size);
		std::vector<std::string> lines;
		size_t start = 0;
		while (true) {
			const size_t brk = label.text.find('\n', start);
			lines.push_back(label.text.substr(start,
				brk == std::string::npos ? std::string::npos : brk - start));
			if (brk == std::string::npos)
				break;
			start = brk + 1;
		}
		const float top = label.y - step * (float) (lines.size() - 1) / 2.f;
		for (size_t i = 0; i < lines.size(); i++)
			panelText(args.vg, label.x, top + step * (float) i, lines[i].c_str(), crisp);
	}

	// Drawn last so nothing sits on top of it.
	nvgBeginPath(args.vg);
	nvgRoundedRect(args.vg, 1.f, 1.f, box.size.x - 2.f, box.size.y - 2.f, 3.f);
	nvgStrokeColor(args.vg, PANEL_EDGE);
	nvgStrokeWidth(args.vg, 1.5f);
	nvgStroke(args.vg);

	Widget::draw(args);
}


void drawJack(NVGcontext* vg, math::Vec c, float r, NVGcolor color, bool isOutput) {
	const float rh = r * 0.53f;

	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, r);
	nvgFillColor(vg, color);
	nvgFill(vg);
	nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 200));
	nvgStrokeWidth(vg, r * 0.1f);
	nvgStroke(vg);

	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, rh);
	nvgFillColor(vg, nvgRGB(0x2f, 0x2f, 0x33));
	nvgFill(vg);

	// DIRECTION, BY SHAPE: an output's dashes hug the outer edge of the coloured band, an
	// input's hug the hole. A third of the band wide, with the count taken from the
	// circumference so the rhythm reads evenly at any size.
	const float band = r - rh;
	if (band <= 0.f)
		return;
	const float w = band / 3.f;
	// Flush against whichever edge it marks, with no colour showing between. The dashes stay
	// legible because the gaps BETWEEN them are the jack's colour, not because of any margin.
	const float ringR = isOutput ? (r * 0.95f - w / 2.f) : (rh + w / 2.f);
	if (ringR <= 0.f)
		return;
	const float circ = 2.f * (float) M_PI * ringR;
	const int n = std::max(6, (int) std::round(circ / (w * 1.6f)));
	const float step = 2.f * (float) M_PI / n;

	nvgStrokeColor(vg, nvgRGB(0, 0, 0));
	nvgStrokeWidth(vg, w);
	nvgLineCap(vg, NVG_BUTT);
	for (int i = 0; i < n; i++) {
		nvgBeginPath(vg);
		nvgArc(vg, c.x, c.y, ringR, i * step, i * step + step / 2.f, NVG_CW);
		nvgStroke(vg);
	}
}


void JackPaint::draw(const DrawArgs& args) {
	for (const Mark& mark : marks) {
		if (!mark.port || !mark.port->isVisible())
			continue;
		const float r = std::fmin(mark.port->box.size.x, mark.port->box.size.y) / 2.f;
		if (r <= 1.f)
			continue;
		drawJack(args.vg, mark.port->box.getCenter(), r, mark.color, mark.isOutput);
	}
	Widget::draw(args);
}


// ---- the lamp list ---------------------------------------------------------------------------

/** In pixels. Large enough to read as a lamp at rack distance rather than as a dot. */
static const float LAMP_R = 6.5f;
/** How far a name under a lamp sits from the lamp's edge: close, so the two read as one. */
static const float NAME_GAP = 1.5f;

/** Room for the longest name, estimated from its characters: a text width wants a font and a
drawing context, and a target that is a little generous costs nothing. */
static float lampsNamesWidth(const std::vector<std::string>& names, float size) {
	size_t longest = 0;
	for (const std::string& name : names) {
		size_t start = 0;
		while (start <= name.size()) {
			const size_t brk = name.find('\n', start);
			const size_t len = (brk == std::string::npos ? name.size() : brk) - start;
			longest = std::max(longest, len);
			if (brk == std::string::npos)
				break;
			start = brk + 1;
		}
	}
	// 0.575 of the size is a character's width in this face; at the old fixed size of eight
	// that came to the 4.6 this used to say, so nothing moves until a size is changed.
	return longest ? (LAMP_R + 5.f + longest * 0.575f * size) : LAMP_R;
}

void drawRaisedButton(NVGcontext* vg, math::Vec size, bool down, bool on,
		const NVGcolor* accent, float round) {
	// ONE PUSH BUTTON ACROSS EVERY MODULE WE MAKE.
	//
	// These were raised square caps, light grey and modelled with a shadow, a bright rim along
	// the top and a dark one along the bottom. They looked like something to press, and two
	// things were wrong with them: on and off were a change of tint on the same light cap,
	// which is hard to read at a glance, and they were nothing like the buttons on the Clarity
	// family, which are a dark dome that turns green.
	//
	// This is that dome. Off is dark with a light grey ring, which is what gives a button
	// nobody has pressed an edge on a dark panel; on is red, lit from above, with a red ring. The difference between them is a colour rather than a shade, so it survives being
	// looked at quickly and out of the corner of the eye.
	//
	// `round` and the square box are kept: a caller that asks for a square footprint still gets
	// the same footprint, and the cap is drawn inside it. `accent` still colours a lit cap, for
	// the record button, which is red rather than green because it is not a setting.
	(void) round;
	const float r = std::fmin(size.x, size.y) / 2.f - 1.5f;
	const float cx = size.x / 2.f, cy = size.y / 2.f;
	const bool lit = on || (down && !on);

	// The rim it sits in, which is what stops a dark cap merging with a dark panel.
	nvgBeginPath(vg);
	nvgCircle(vg, cx, cy, r + 1.5f);
	nvgFillColor(vg, nvgRGB(0x0f, 0x12, 0x17));
	nvgFill(vg);

	NVGcolor bright = nvgRGB(0x4a, 0x50, 0x59);
	NVGcolor dark = nvgRGB(0x2a, 0x2f, 0x36);
	NVGcolor ring = nvgRGBA(0xcf, 0xcf, 0xcf, 0x90);
	if (lit) {
		bright = accent ? *accent : nvgRGB(0xff, 0x8f, 0x80);
		dark = accent
			? nvgRGB((unsigned char) (accent->r * 255.f * 0.45f),
				(unsigned char) (accent->g * 255.f * 0.45f),
				(unsigned char) (accent->b * 255.f * 0.45f))
			: nvgRGB(0xb5, 0x22, 0x16);
		ring = accent ? *accent : nvgRGB(0xe8, 0x38, 0x28);
	}

	// A MOMENTARY CAP GOES DOWN, and a cap going down turns the light over: the dome is lit
	// from below while it is held, which is the whole of what pressed looks like here.
	nvgBeginPath(vg);
	nvgCircle(vg, cx, cy, r);
	nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy + (down ? 1.f : -1.f), 0.5f, r,
		down ? dark : bright, down ? bright : dark));
	nvgFill(vg);
	nvgStrokeColor(vg, ring);
	nvgStrokeWidth(vg, lit ? 1.f : 1.3f);
	nvgStroke(vg);
}


DreamerButton::DreamerButton() {
	// MOMENTARY, which Rack's Switch is not unless it is told. Left as it comes, a press
	// INCREMENTS: one press sets the value to one, the next wraps it back to nought. Anything
	// watching for the rise then fires on every other press, which is exactly what "it takes
	// two clicks" looks like from the front.
	momentary = true;
	box.size = mm2px(math::Vec(6.2f, 6.2f));
}

void DreamerButton::draw(const DrawArgs& args) {
	const bool down = getParamQuantity() && getParamQuantity()->getValue() > 0.5f;
	drawRaisedButton(args.vg, box.size, down, false);
}

DreamerLatch::DreamerLatch() {
	box.size = mm2px(math::Vec(6.2f, 6.2f));
}

void DreamerLatch::draw(const DrawArgs& args) {
	const bool on = getParamQuantity() && getParamQuantity()->getValue() > 0.5f;
	drawRaisedButton(args.vg, box.size, on, on);
}

DreamerRedLatch::DreamerRedLatch() {
	box.size = mm2px(math::Vec(6.2f, 6.2f));
}

void DreamerRedLatch::draw(const DrawArgs& args) {
	const bool on = getParamQuantity() && getParamQuantity()->getValue() > 0.5f;
	// Round, so it is not mistaken for one of the square latches, and red when it is live.
	static const NVGcolor RED = nvgRGB(0xff, 0x5a, 0x50);
	drawRaisedButton(args.vg, box.size, on, on, &RED, 0.5f);
}

/** The ink a transport symbol is cut in: dark, so it reads as a mark ON the cap rather than as
another thing beside it. */
static const NVGcolor TRANSPORT_INK = nvgRGB(0x14, 0x18, 0x1e);
/** And the ink for a cap that is NOT lit, which is now dark: a dark mark on a dark cap is no
mark at all. Light enough to read, not so light that it competes with a lit cap. */
static const NVGcolor TRANSPORT_INK_OFF = nvgRGB(0xc4, 0xc9, 0xd1);

void drawPlayGlyph(NVGcontext* vg, math::Vec size, bool playing) {
	const float w = size.x, h = size.y;
	nvgFillColor(vg, playing ? TRANSPORT_INK : TRANSPORT_INK_OFF);
	if (playing) {
		// Two bars, which is pause: what pressing it now would do.
		const float bw = w * 0.13f;
		const float gap = w * 0.11f;
		nvgBeginPath(vg);
		nvgRect(vg, w / 2.f - gap / 2.f - bw, h * 0.28f, bw, h * 0.44f);
		nvgRect(vg, w / 2.f + gap / 2.f, h * 0.28f, bw, h * 0.44f);
		nvgFill(vg);
		return;
	}
	// A triangle pointing the way the music goes.
	nvgBeginPath(vg);
	nvgMoveTo(vg, w * 0.37f, h * 0.27f);
	nvgLineTo(vg, w * 0.71f, h * 0.50f);
	nvgLineTo(vg, w * 0.37f, h * 0.73f);
	nvgClosePath(vg);
	nvgFill(vg);
}

void drawRewindGlyph(NVGcontext* vg, math::Vec size) {
	const float w = size.x, h = size.y;
	// Rewind is momentary: its cap is dark whenever anybody can see it.
	nvgFillColor(vg, TRANSPORT_INK_OFF);
	for (int i = 0; i < 2; i++) {
		const float x = w * (0.30f + i * 0.24f);
		nvgBeginPath(vg);
		nvgMoveTo(vg, x, h * 0.50f);
		nvgLineTo(vg, x + w * 0.22f, h * 0.28f);
		nvgLineTo(vg, x + w * 0.22f, h * 0.72f);
		nvgClosePath(vg);
		nvgFill(vg);
	}
}

DreamerPlay::DreamerPlay() {
	box.size = mm2px(math::Vec(6.6f, 6.6f));
}

void DreamerPlay::draw(const DrawArgs& args) {
	const bool on = getParamQuantity() && getParamQuantity()->getValue() > 0.5f;
	drawRaisedButton(args.vg, box.size, on, on);
	drawPlayGlyph(args.vg, box.size, on);
}

DreamerRewind::DreamerRewind() {
	momentary = true;
	box.size = mm2px(math::Vec(6.6f, 6.6f));
}

void DreamerRewind::draw(const DrawArgs& args) {
	const bool down = getParamQuantity() && getParamQuantity()->getValue() > 0.5f;
	drawRaisedButton(args.vg, box.size, down, false);
	drawRewindGlyph(args.vg, box.size);
}

DreamerKnob::DreamerKnob() {
	// THE SWEEP RACK'S OWN KNOBS USE, so that a panel mixing this with anything else reads the
	// same, and so that a scale drawn round it lands where the pointer does.
	minAngle = -0.83f * M_PI;
	maxAngle = 0.83f * M_PI;
	setDiameter(9.6f);
}

void DreamerKnob::setDiameter(float mm) {
	diameter = std::fmax(3.f, mm);
	box.size = mm2px(math::Vec(diameter, diameter));
}

void DreamerKnob::draw(const DrawArgs& args) {
	const float w = box.size.x, h = box.size.y;
	const float cx = w / 2.f, cy = h / 2.f;
	const float r = std::fmin(w, h) / 2.f;

	float frac = 0.5f;
	if (ParamQuantity* pq = getParamQuantity()) {
		const float lo = pq->getMinValue(), hi = pq->getMaxValue();
		if (hi > lo)
			frac = math::clamp((pq->getValue() - lo) / (hi - lo), 0.f, 1.f);
	}

	// The shadow under it, which is most of what says the knob stands off the panel.
	nvgBeginPath(args.vg);
	nvgCircle(args.vg, cx, cy + r * 0.06f, r * 0.98f);
	nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 0x88));
	nvgFill(args.vg);

	// The body, lit from above.
	nvgBeginPath(args.vg);
	nvgCircle(args.vg, cx, cy, r * 0.94f);
	nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, cy - r, 0.f, cy + r,
		nvgRGB(0x3d, 0x44, 0x50), nvgRGB(0x1b, 0x1f, 0x26)));
	nvgFill(args.vg);

	// A bright rim along the top and a dark one along the bottom: an edge catching the light.
	nvgBeginPath(args.vg);
	nvgCircle(args.vg, cx, cy, r * 0.94f);
	nvgStrokeWidth(args.vg, std::fmax(1.f, r * 0.06f));
	nvgStrokePaint(args.vg, nvgLinearGradient(args.vg, 0.f, cy - r, 0.f, cy + r,
		nvgRGBA(0xff, 0xff, 0xff, 0x66), nvgRGBA(0, 0, 0, 0xaa)));
	nvgStroke(args.vg);

	// THE POINTER, which is the whole reason anybody looks at a knob. A wedge rather than a
	// hairline, because a hairline on a small knob at a small zoom disappears.
	const float a = minAngle + (maxAngle - minAngle) * frac;
	const float dx = std::sin(a), dy = -std::cos(a);
	nvgBeginPath(args.vg);
	nvgMoveTo(args.vg, cx + dx * r * 0.78f, cy + dy * r * 0.78f);
	nvgLineTo(args.vg, cx + dx * r * 0.22f - dy * r * 0.12f,
		cy + dy * r * 0.22f + dx * r * 0.12f);
	nvgLineTo(args.vg, cx + dx * r * 0.22f + dy * r * 0.12f,
		cy + dy * r * 0.22f - dx * r * 0.12f);
	nvgClosePath(args.vg);
	nvgFillColor(args.vg, nvgRGB(0xe8, 0xee, 0xf6));
	nvgFill(args.vg);
}

bool mpxCompatible(engine::Module* outModule, int outId, engine::Module* inModule, int inId) {
	const bool outIsMPX = (noteBusOf(outModule, outId, NULL) >= 0);
	const bool inIsMPX = isMPXInput(inModule, inId);
	// Both, or neither. One of each is a cable that would look right and do nothing.
	return outIsMPX == inIsMPX;
}

/** The far end of a cable, whichever end this port is. */
static bool cableIsGood(engine::Cable* cable) {
	return cable && mpxCompatible(cable->outputModule, cable->outputId,
		cable->inputModule, cable->inputId);
}

void MPXPort::step() {
	PJ301MPort::step();
	if (!module)
		return;
	// TAKEN AWAY ON THE NEXT FRAME. A cable dropped onto somebody else's jack is made before we
	// are asked anything, and a patch loaded from disk makes its cables without going near a
	// widget at all; both arrive here, and both go the same way.
	for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(this)) {
		if (cableIsGood(cw->getCable()))
			continue;
		APP->scene->rack->removeCable(cw);
		delete cw;
		break;   // the list is now stale; whatever is left is caught next frame
	}
}

void MPXPort::onDragDrop(const DragDropEvent& e) {
	// WHAT IS BEING DRAGGED, and what it would become if this drop were allowed. Refusing here
	// rather than undoing it afterwards means there is nothing to undo and nothing flickers.
	if (module) {
		for (CableWidget* cw : APP->scene->rack->getIncompleteCables()) {
			engine::Cable* cable = cw->getCable();
			if (!cable)
				continue;
			engine::Module* outModule = cable->outputModule;
			int outId = cable->outputId;
			engine::Module* inModule = cable->inputModule;
			int inId = cable->inputId;
			// Whichever end is loose is the end this jack would fill.
			if (type == engine::Port::INPUT) {
				inModule = module;
				inId = portId;
			}
			else {
				outModule = module;
				outId = portId;
			}
			if (outModule && inModule
				&& !mpxCompatible(outModule, outId, inModule, inId))
				return;   // not consumed, and no cable made
		}
	}
	PJ301MPort::onDragDrop(e);
}

Readout::Readout() {
	setFigures(2, 2.8f);
}

/** ASKED OF THE PARAMETER, over every value it can take. Bounded, because a continuous parameter
has no list to walk — for one of those the two ends are what decides the width. */
/** The longest the parameter can print, in characters. Nothing uses it to SET a width any more —
that is a layout's business — but it is the right answer when you want to choose one, so it stays
for the panel editor to offer. */
int Readout::widestValue() {
	ParamQuantity* pq = getParamQuantity();
	if (!pq)
		return 2;
	const float lo = pq->getMinValue(), hi = pq->getMaxValue();
	const float was = pq->getValue();
	size_t longest = 1;
	const int steps = (int) std::round(hi - lo);
	if (steps >= 1 && steps <= 128) {
		for (int i = 0; i <= steps; i++) {
			pq->setValue(lo + (float) i);
			longest = std::max(longest, pq->getDisplayValueString().size());
		}
	}
	else {
		for (int i = 0; i < 2; i++) {
			pq->setValue(i ? hi : lo);
			longest = std::max(longest, pq->getDisplayValueString().size());
		}
	}
	pq->setValue(was);
	return (int) longest;
}

/** NO STEP. A readout used to work its own width out here, once the parameter was there to ask
about its range — which sounds helpful and is not. It meant the plate changed size AFTER it had
been placed, so it never sat where the layout put it and a caption centred underneath came out
crooked; and it meant a panel's arrangement depended on a parameter's range, so widening a range
from 99 to 100 quietly moved a plate.

A width is now a number in the layout, set once when the widget is made and never touched again.
Predictable is worth more than clever here. */

void Readout::setFigures(int n, float fig) {
	// WHERE IT IS CENTRED IS KEPT, and this is the whole of a bug that looked like a caption
	// problem. A plate grows from its top-left corner, and one asked to work its own width out
	// cannot do that until the parameter is there to ask — which is after it has been placed. So
	// it was placed centred at one figure wide and then widened to three, and its middle walked
	// half a figure to the right of where the layout put it. The caption underneath, centred
	// correctly all along, then looked offset to the left.
	const math::Vec centre = box.pos.plus(box.size.div(2.f));

	chars = std::max(0, n);
	figureMM = std::fmax(1.f, fig);
	// THE PLATE HUGS THE FIGURES: as tall as one and a millimetre, as wide as all of them and a
	// millimetre. A figure of a given height needs a font size larger than itself, since a
	// capital is only 0.7041 of the size it is set at, and a digit is 0.6011 of that size across.
	const float perFigure = figureMM * FIGURE_ADVANCE / FIGURE_CAP;
	// Two if nobody said, which holds anything up to 99 and is the commonest case by far.
	const int wide = (chars > 0) ? chars : 2;
	// A PLATE OF NOTE SYMBOLS IS TALLER, NOT WIDER: a note is mostly stem, and drawn to a
	// figure's height its head was a speck. Its width still follows the figures, for the word a
	// value without a symbol shows.
	const float tall = glyphs.empty() ? figureMM + FIGURE_SURROUND : GLYPH_PLATE_MM;
	box.size = mm2px(math::Vec((float) wide * perFigure + FIGURE_SURROUND, tall));
	box.pos = centre.minus(box.size.div(2.f));
}

void Readout::draw(const DrawArgs& args) {
	const float w = box.size.x, h = box.size.y;
	const float r = std::fmin(w, h) * 0.18f;

	// SUNK RATHER THAN RAISED, which is what says a thing is read rather than pressed — the
	// opposite of the buttons, and the same cue a recessed window gives on any panel.
	nvgBeginPath(args.vg);
	nvgRoundedRect(args.vg, 0.f, 0.f, w, h, r);
	nvgFillPaint(args.vg, nvgLinearGradient(args.vg, 0.f, 0.f, 0.f, h,
		nvgRGB(0x0d, 0x10, 0x14), nvgRGB(0x17, 0x1c, 0x23)));
	nvgFill(args.vg);
	nvgBeginPath(args.vg);
	nvgRoundedRect(args.vg, 0.5f, 0.5f, w - 1.f, h - 1.f, r);
	nvgStrokeWidth(args.vg, 1.f);
	nvgStrokePaint(args.vg, nvgLinearGradient(args.vg, 0.f, 0.f, 0.f, h,
		nvgRGBA(0, 0, 0, 0xcc), nvgRGBA(0xff, 0xff, 0xff, 0x3a)));
	nvgStroke(args.vg);

	// THE SAME FIGURES THE CHART SHOWS: the title face, this green, at 0.72 of the plate. One
	// readout in the plugin should look like every other readout in the plugin.
	std::shared_ptr<window::Font> face = titleFont();
	if (!face || face->handle < 0)
		return;
	// THE NUMBER, WITHOUT ITS UNIT. The parameter keeps the unit — it belongs in the tooltip and
	// in Rack's own menu, where "120" alone means nothing — but on a plate an inch wide the unit
	// takes room from the figures and repeats what the caption underneath already says.
	std::string text = "--";
	if (ParamQuantity* pq = getParamQuantity()) {
		text = pq->getDisplayValueString();
		// A SYMBOL IN PLACE OF THE WORD, where the value has one.
		const int i = (int) std::round(pq->getValue() - pq->getMinValue());
		if (i >= 0 && i < (int) glyphs.size() && !glyphs[i].empty()) {
			const float gh = h * GLYPH_FILL;
			drawNoteSymbol(args.vg, w / 2.f, (h - gh) / 2.f, gh, glyphs[i], nvgRGB(0x3d, 0xe0, 0x7a));
			return;
		}
	}
	nvgFontFaceId(args.vg, face->handle);
	// The size that makes a capital exactly as tall as the figure height asked for.
	nvgFontSize(args.vg, mm2px(math::Vec(0, figureMM)).y / FIGURE_CAP);
	nvgFillColor(args.vg, nvgRGB(0x3d, 0xe0, 0x7a));
	nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	panelText(args.vg, w / 2.f, h / 2.f, text.c_str(), true);
}

/** THE LIST A PLATE OPENS.

OVER THE PLATE, NOT BESIDE IT. Rack's menus hang down and to the right of wherever they were asked
for, which is right for a menu of commands and wrong for a chooser: the value in force ends up
somewhere other than where it was being read, and the eye has to find it again. This puts the
current value exactly where the plate's own text is, with the others above and below it — so
choosing what is already chosen means no movement at all, and the distance to any other value is
the distance you would expect from looking at the list.

SHIFTED WHEN IT MUST BE, and no further. A list longer than the room below it is slid up until it
fits; the current value is then no longer over the plate, but it is still the highlighted one, and
that is the part worth keeping.

EACH LINE IS A NAME AND A FEW WORDS. The name is what the plate shows; the words after the dash are
what the name cannot say. Both are drawn, and the whole line is put up as a note so that holding
Option reads it out — which is the only way the words reach somebody who cannot read the list. */
/** THE LIST IS THE LIST, AND NOT THE WHOLE WINDOW.

It used to be a widget the size of the scene with the list drawn inside it, so that a click
anywhere else could close it. That shape is how an OVERLAY is told from a WINDOW by anything else
looking at the scene — Clarity decides whether something floating over the rack owns its own
clicks that way, since its own overlays cover everything and a thing covering everything cannot
own a particular point. So Clarity did not stand back, and its click-to-move-cables took the press
on the jack behind the list: choosing a style pulled out a cable.

It is now a plain widget the size of the list, inside one of Rack's own menu overlays. The
overlay closes it on a click anywhere else and on Escape, which is what the full-window box was
for, and every plugin that already stands back from a menu stands back from this. */
struct ReadoutList : widget::OpaqueWidget {
	ParamQuantity* pq = NULL;
	std::vector<std::string> names;
	std::vector<std::string> notes;
	std::vector<std::string> glyphs;
	int current = 0;
	int hovered = -1;
	float lineH = 0.f;
	WeakPtr<ui::Tooltip> note;

	static constexpr float PAD = 6.f;
	/** The column the symbols sit in, when the plate has any. */
	static constexpr float GLYPH_W = 28.f;

	~ReadoutList() {
		dropNote();
	}

	/** THE NOTE GOES BY REQUEST, never by removing and deleting it here. This is called from the
	destructor, which Rack runs while it is walking the scene's children deleting the overlay — and
	the note is the overlay's neighbour in that list. Taking it out of the list there pulled the
	ground from under the walk, and Rack crashed on the next step. Asked to delete, it goes when
	the walk reaches it. */
	void dropNote() {
		if (ui::Tooltip* t = note)
			t->requestDelete();
		note = NULL;
	}

	/** The pointer leaving the list takes its note with it, rather than leaving it standing. */
	void onLeave(const LeaveEvent& e) override {
		hovered = -1;
		dropNote();
		widget::OpaqueWidget::onLeave(e);
	}

	int lineAt(math::Vec pos) {
		if (lineH <= 0.f || !box.zeroPos().contains(pos))
			return -1;
		const int i = (int) ((pos.y - PAD) / lineH);
		return (i >= 0 && i < (int) names.size()) ? i : -1;
	}

	/** The words for one line, which are also what is spoken. */
	std::string lineText(int i) const {
		if (i < 0 || i >= (int) names.size())
			return "";
		if (i < (int) notes.size() && !notes[i].empty())
			return names[i] + " — " + notes[i];
		return names[i];
	}

	void onHover(const HoverEvent& e) override {
		const int was = hovered;
		hovered = lineAt(e.pos);
		if (hovered != was)
			showNote();
		e.consume(this);
	}

	/** THE LINE UNDER THE POINTER, PUT UP AS A NOTE. Rack draws it, and Dreamer Help reads out
	whatever note Rack is showing while Option is held — so the words reach the ear through the
	same channel a module's description does, and this widget needs to know nothing about speech. */
	void showNote() {
		dropNote();
		if (hovered < 0)
			return;
		ui::Tooltip* t = new ui::Tooltip;
		t->text = lineText(hovered);
		APP->scene->addChild(t);
		note = t;
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action != GLFW_PRESS) {
			widget::OpaqueWidget::onButton(e);
			return;
		}
		e.consume(this);
		const int i = lineAt(e.pos);
		if (i >= 0 && pq)
			pq->setValue((float) (i + (int) std::round(pq->getMinValue())));
		close();
	}

	/** A CLICK ANYWHERE ELSE, AND ESCAPE, ARE THE OVERLAY'S. Both delete it, and this with it. */
	void close() {
		dropNote();
		if (parent)
			parent->requestDelete();
		else
			requestDelete();
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = bodyFont();
		if (!font || font->handle < 0)
			return;

		// The ground, with a rim: a list floating over a panel needs an edge or it reads as part
		// of whatever is behind it.
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 4.f);
		nvgFillColor(args.vg, nvgRGBA(0x1b, 0x1f, 0x26, 0xf8));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, PANEL_EDGE);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 11.f);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

		for (size_t i = 0; i < names.size(); i++) {
			const float y = PAD + lineH * (float) i;
			const bool under = ((int) i == hovered);
			const bool chosen = ((int) i == current);
			if (under || chosen) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg, 2.f, y, box.size.x - 4.f, lineH, 2.f);
				nvgFillColor(args.vg, under ? nvgRGBA(0x3d, 0xd6, 0x8c, 0x55)
					: nvgRGBA(0xff, 0xff, 0xff, 0x14));
				nvgFill(args.vg);
			}
			// THE NAME IN THE PANEL'S INK AND THE WORDS AFTER IT DIMMER, so the line reads as one
			// thing said twice rather than as two columns.
			nvgFillColor(args.vg, chosen ? PANEL_EDGE : PANEL_INK);
			float x = PAD;
			// THE SYMBOL FIRST, in a column of its own, then the name.
			if (!glyphs.empty()) {
				if (i < glyphs.size() && !glyphs[i].empty())
					drawNoteSymbol(args.vg, x + GLYPH_W / 2.f, y + 1.5f, lineH - 3.f, glyphs[i],
						chosen ? PANEL_EDGE : PANEL_INK);
				x += GLYPH_W;
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, 11.f);
				nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
				nvgFillColor(args.vg, chosen ? PANEL_EDGE : PANEL_INK);
			}
			x = crispText(args.vg, x, y + lineH / 2.f, names[i].c_str(), NULL);
			if (i < notes.size() && !notes[i].empty()) {
				nvgFillColor(args.vg, PANEL_DIM);
				const std::string rest = "  —  " + notes[i];
				crispText(args.vg, x, y + lineH / 2.f, rest.c_str(), NULL);
			}
		}
	}
};


void Readout::openList() {
	ParamQuantity* pq = getParamQuantity();
	if (!pq)
		return;
	const int lo = (int) std::round(pq->getMinValue());
	const int hi = (int) std::round(pq->getMaxValue());
	// A LIST, NOT A RANGE. Anything with more steps than a list can hold is not a thing to
	// choose from one, and a knob is the right control for it.
	if (hi - lo > 128)
		return;

	ReadoutList* popup = new ReadoutList;
	popup->pq = pq;
	popup->current = math::clamp((int) std::round(pq->getValue()) - lo, 0, hi - lo);
	for (int v = lo; v <= hi; v++) {
		// Asked of the parameter rather than printed here, so a switch lists its names and a
		// count lists its counts, each with whatever unit the module gave it.
		const float was = pq->getValue();
		pq->setValue((float) v);
		popup->names.push_back(pq->getDisplayValueString() + pq->getUnit());
		pq->setValue(was);
	}
	popup->notes = notes;
	popup->glyphs = glyphs;

	// AS WIDE AS THE WIDEST LINE and as tall as all of them, measured rather than guessed.
	std::shared_ptr<window::Font> font = bodyFont();
	float widest = 60.f;
	if (font && font->handle >= 0) {
		NVGcontext* vg = APP->window->vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 11.f);
		for (size_t i = 0; i < popup->names.size(); i++) {
			std::string line = popup->names[i];
			if (i < popup->notes.size() && !popup->notes[i].empty())
				line += "  —  " + popup->notes[i];
			widest = std::fmax(widest, nvgTextBounds(vg, 0.f, 0.f, line.c_str(), NULL, NULL));
		}
	}
	popup->lineH = glyphs.empty() ? 15.f : 26.f;
	const float w = widest + ReadoutList::PAD * 2.f + (glyphs.empty() ? 0.f : ReadoutList::GLYPH_W);
	const float h = popup->lineH * (float) popup->names.size() + ReadoutList::PAD * 2.f;

	// THE CURRENT LINE OVER THE PLATE'S OWN TEXT. Everything else follows from that.
	const math::Vec plate = getAbsoluteOffset(math::Vec());
	float x = plate.x + box.size.x / 2.f - w / 2.f;
	float y = plate.y + box.size.y / 2.f
		- (ReadoutList::PAD + popup->lineH * ((float) popup->current + 0.5f));
	// Slid back inside the window when it will not fit, which costs the alignment and keeps the
	// list.
	const math::Vec scene = APP->scene->box.size;
	x = math::clamp(x, 2.f, std::fmax(2.f, scene.x - w - 2.f));
	y = math::clamp(y, 2.f, std::fmax(2.f, scene.y - h - 2.f));
	popup->box.pos = math::Vec(x, y);
	popup->box.size = math::Vec(w, h);

	// INSIDE ONE OF RACK'S OWN MENU OVERLAYS. It closes on a click anywhere else and on Escape,
	// and everything that already knows to keep its hands off an open menu keeps them off this.
	ui::MenuOverlay* over = new ui::MenuOverlay;
	over->addChild(popup);
	APP->scene->addChild(over);
}

void Readout::onButton(const ButtonEvent& e) {
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		openList();
		e.consume(this);
		return;
	}
	ParamWidget::onButton(e);
}

void Readout::onHoverScroll(const HoverScrollEvent& e) {
	// THE WHEEL STEPS IT, because the next value along is what is usually wanted and opening a
	// list to get there would be three actions for one step. Gathered rather than counted: the
	// movement is added up and a value spent each time it passes the threshold, so a trackpad's
	// many small movements and a wheel's few large ones come to the same rate.
	ParamQuantity* pq = getParamQuantity();
	if (!pq) {
		ParamWidget::onHoverScroll(e);
		return;
	}
	// GATHERED IN VALUES, not in wheel units. The same arithmetic a Rack knob does — the wheel
	// times the host's sensitivity times the parameter's range — and then a quarter of it, so a
	// readout moves at a quarter the rate of the knob it replaced.
	const float range = pq->getMaxValue() - pq->getMinValue();
	scrolled += e.scrollDelta.y * settings::knobScrollSensitivity * range
		/ READOUT_SCROLL_DIVISOR;
	while (std::fabs(scrolled) >= 1.f) {
		const float step = (scrolled > 0.f) ? 1.f : -1.f;
		scrolled -= step;
		pq->setValue(math::clamp(std::round(pq->getValue()) + step,
			pq->getMinValue(), pq->getMaxValue()));
	}
	e.consume(this);
}

math::Vec Lamps::lampPos(int i) {
	// ANCHORED TO THE CORNER, not to the middle of the box. The box grows to cover the names,
	// and lamps measured from its centre would slide as it grew.
	//
	// WITH THE NAMES ON THE LEFT the lamps sit at the far side, since the names are inside the
	// box and come first. Before this they were drawn outside it: off the widget, so a click on
	// a name chose nothing and a lamp column placed against another control overlapped it.
	const float lead = (!horizontal && labelSide == Panel::LEFT)
		? lampsNamesWidth(names, nameSize) : 0.f;
	return horizontal ? math::Vec(LAMP_R + i * pitch, LAMP_R)
		: math::Vec(lead + LAMP_R, LAMP_R + i * pitch);
}

/** A ROW OF LAMPS WITH ITS NAMES UNDERNEATH, which is what CENTRE means on a horizontal group:
a row of numbered positions reads as a row, with each number under the lamp it belongs to, and
nothing beside the row to make it wider than the lamps themselves. */
bool Lamps::namesBelow() const {
	return horizontal && labelSide == Panel::CENTRE;
}

void Lamps::fit() {
	const int n = std::max(1, (int) names.size());
	const float along = 2.f * LAMP_R + (n - 1) * pitch;
	const float names_w = lampsNamesWidth(names, nameSize);
	const bool sided = (labelSide == Panel::RIGHT)
		|| (!horizontal && labelSide == Panel::LEFT);
	float across = 2.f * LAMP_R + (sided ? names_w : 0.f);
	if (namesBelow()) {
		size_t lines = 1;
		for (const std::string& name : names)
			lines = std::max(lines, (size_t) std::count(name.begin(), name.end(), '\n') + 1);
		across = 2.f * LAMP_R + NAME_GAP + panelLineStep(nameSize) * (float) lines;
	}
	box.size = horizontal ? math::Vec(along, across) : math::Vec(across, along);
}

int Lamps::lampAt(math::Vec pos) {
	for (int i = 0; i < (int) names.size(); i++) {
		// A generous target: the whole band between one lamp and the next, across the widget's
		// full width, so the name is as clickable as the lamp beside it.
		const math::Vec c = lampPos(i);
		if (horizontal) {
			if (std::fabs(pos.x - c.x) <= pitch / 2.f)
				return i;
		}
		else {
			if (std::fabs(pos.y - c.y) <= pitch / 2.f)
				return i;
		}
	}
	return -1;
}

void Lamps::draw(const DrawArgs& args) {
	std::shared_ptr<window::Font> font = bodyFont();
	const int count = (int) names.size();
	if (count == 0)
		return;
	const int value = getParamQuantity()
		? (int) std::round(getParamQuantity()->getValue()) : 0;

	// The track the lamps sit on, which says they are one control with several positions
	// rather than several controls.
	const math::Vec first = lampPos(0);
	const math::Vec last = lampPos(count - 1);
	nvgBeginPath(args.vg);
	nvgMoveTo(args.vg, first.x, first.y);
	nvgLineTo(args.vg, last.x, last.y);
	nvgStrokeColor(args.vg, nvgRGB(0x3a, 0x41, 0x4c));
	nvgStrokeWidth(args.vg, 3.f);
	nvgLineCap(args.vg, NVG_ROUND);
	nvgStroke(args.vg);

	for (int i = 0; i < count; i++) {
		const math::Vec c = lampPos(i);
		const bool on = (i == value);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, LAMP_R);
		// ORANGE FOR A CHOSEN LAMP, now that red is what a button that is on looks like. One
		// colour cannot mean both "this button is switched on" and "this is the choice", and a
		// panel carrying both was asking the eye to tell them apart by shape.
		nvgFillColor(args.vg, on ? nvgRGB(0xf2, 0x84, 0x1f) : nvgRGB(0x2e, 0x34, 0x3d));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, on ? nvgRGB(0xff, 0xcb, 0x8a) : nvgRGB(0x4a, 0x52, 0x5e));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		// A lit lamp glows a little, which is what makes it read as lit rather than as merely
		// a different colour — the same cue a panel lamp gives.
		if (on) {
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, c.x, c.y, LAMP_R * 2.4f);
			NVGpaint glow = nvgRadialGradient(args.vg, c.x, c.y, LAMP_R, LAMP_R * 2.4f,
				nvgRGBA(0xf2, 0x84, 0x1f, 0x60), nvgRGBA(0xf2, 0x84, 0x1f, 0x00));
			nvgFillPaint(args.vg, glow);
			nvgFill(args.vg);
		}

		if (!font || font->handle < 0)
			continue;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, nameSize);
		// EVERY NAME IN THE PANEL'S OWN INK, chosen or not. The unchosen ones were set dimmer,
		// and dim thin text is exactly what goes to a fuzz under magnification; the lit lamp
		// already says which one is chosen, so the name does not have to say it too.
		nvgFillColor(args.vg, PANEL_INK);
		// A NAME MAY BE SEVERAL LINES, split on the newlines the slash rule writes, and set
		// about the lamp's own line rather than downward from it. Short lines beside a lamp
		// read better than one long one that pushes the panel wider than it needs to be.
		if (namesBelow()) {
			// A NAME MAY BE SEVERAL LINES here as well, written down from under the lamp.
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			const float step = panelLineStep(nameSize);
			float ty = c.y + LAMP_R + NAME_GAP + step / 2.f;
			size_t from = 0;
			while (true) {
				const size_t brk = names[i].find('\n', from);
				const std::string line = names[i].substr(from,
					brk == std::string::npos ? std::string::npos : brk - from);
				panelText(args.vg, c.x, ty, line.c_str(), true);
				if (brk == std::string::npos)
					break;
				from = brk + 1;
				ty += step;
			}
			continue;
		}
		const bool onLeft = labelsOutward ? (i == 0) : (labelSide == Panel::LEFT);
		const float tx = onLeft ? c.x - LAMP_R - 5.f : c.x + LAMP_R + 5.f;
		nvgTextAlign(args.vg, (onLeft ? NVG_ALIGN_RIGHT : NVG_ALIGN_LEFT) | NVG_ALIGN_MIDDLE);
		std::vector<std::string> lines;
		size_t start = 0;
		while (true) {
			const size_t brk = names[i].find('\n', start);
			lines.push_back(names[i].substr(start,
				brk == std::string::npos ? std::string::npos : brk - start));
			if (brk == std::string::npos)
				break;
			start = brk + 1;
		}
		const float step = panelLineStep(nameSize);
		const float top = c.y - step * (float) (lines.size() - 1) / 2.f;
		for (size_t k = 0; k < lines.size(); k++)
			panelText(args.vg, tx, top + step * (float) k, lines[k].c_str(), true);
	}
}

void Lamps::onEnter(const EnterEvent& e) {
	(void) e;   // Rack's own is not made: see plugin.hpp
}

void Lamps::onLeave(const LeaveEvent& e) {
	(void) e;
	described = -2;
	dropNote();
}

/** THE LAMP UNDER THE POINTER, DESCRIBED.

Rack's own note for this control would be one long description of every option at once, which is
why it was suppressed. This is the opposite: the one lamp the pointer is on, in a clause. It is put
up as a note so that Dreamer Help reads it out while Option is held — the words have nowhere to be
drawn on a panel this size, so being spoken is the whole of what they are for.

BETWEEN LAMPS, THE GROUP'S OWN LINE, which is what the whole thing is for rather than what one
setting does. */
void Lamps::onHover(const HoverEvent& e) {
	const int i = lampAt(e.pos);
	if (i != described) {
		described = i;
		showNote(i);
	}
	ParamWidget::onHover(e);
}

void Lamps::showNote(int lamp) {
	dropNote();
	std::string text;
	if (lamp >= 0 && lamp < (int) notes.size() && !notes[lamp].empty()) {
		const std::string name = (lamp < (int) names.size()) ? names[lamp] : "";
		text = name.empty() ? notes[lamp] : name + " — " + notes[lamp];
	}
	else if (lamp < 0) {
		text = groupNote;
	}
	if (text.empty())
		return;
	ui::Tooltip* t = new ui::Tooltip;
	t->text = text;
	APP->scene->addChild(t);
	note = t;
}

void Lamps::dropNote() {
	// BY REQUEST, as the list's note is: see ReadoutList::dropNote.
	if (ui::Tooltip* t = note)
		t->requestDelete();
	note = NULL;
}

void Lamps::onButton(const ButtonEvent& e) {
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		const int i = lampAt(e.pos);
		if (i >= 0 && getParamQuantity()) {
			// Through the history, so it undoes like every other control on the panel.
			float before = getParamQuantity()->getValue();
			getParamQuantity()->setValue((float) i);
			if (before != (float) i) {
				history::ParamChange* h = new history::ParamChange;
				h->name = "set " + getParamQuantity()->getLabel();
				h->moduleId = module->id;
				h->paramId = paramId;
				h->oldValue = before;
				h->newValue = (float) i;
				APP->history->push(h);
			}
			e.consume(this);
			return;
		}
	}
	ParamWidget::onButton(e);
}


} // namespace px
