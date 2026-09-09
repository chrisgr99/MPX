#include "plugin.hpp"

namespace px {


// The same face the other Dreamer Development modules use, so a rack with both in it does not
// look like two racks. No artwork ships with the plugin: every panel is drawn here.
const NVGcolor PANEL_BG = nvgRGB(0x1b, 0x1f, 0x26);
const NVGcolor PANEL_INK = nvgRGB(0xe6, 0xe8, 0xec);
const NVGcolor PANEL_DIM = nvgRGB(0x9a, 0xa3, 0xaf);
const NVGcolor PANEL_EDGE = nvgRGB(0x3d, 0xd6, 0x8c);

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
	nvgBeginPath(args.vg);
	nvgRect(args.vg, 4.f, 4.f, box.size.x - 8.f, 25.f);
	nvgFillColor(args.vg, nvgRGB(0x24, 0x2a, 0x33));
	nvgFill(args.vg);

	nvgFontFaceId(args.vg, (face && face->handle >= 0) ? face->handle : font->handle);
	nvgFillColor(args.vg, PANEL_INK);
	if (titleAbove.empty()) {
		nvgFontSize(args.vg, 15);
		nvgText(args.vg, box.size.x / 2, 16, title.c_str(), NULL);
	}
	else {
		nvgFontSize(args.vg, 8.f);
		nvgFillColor(args.vg, PANEL_INK);
		nvgText(args.vg, box.size.x / 2, 9, titleAbove.c_str(), NULL);
		nvgFillColor(args.vg, PANEL_INK);
		nvgFontSize(args.vg, 14);
		nvgText(args.vg, box.size.x / 2, 21, title.c_str(), NULL);
	}

	for (float y : rules) {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 9.f, y);
		nvgLineTo(args.vg, box.size.x - 9.f, y);
		nvgStrokeColor(args.vg, nvgRGB(0x35, 0x3c, 0x47));
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
				nvgText(args.vg, sc.x + dx * sc.textRadius, sc.y + dy * sc.textRadius,
					sc.marks[i].c_str(), NULL);
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
			nvgText(args.vg, label.x, top + step * (float) i, lines[i].c_str(), NULL);
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

void drawRaisedButton(NVGcontext* vg, math::Vec size, bool down, bool on) {
	const float w = size.x, h = size.y;
	const float r = std::fmin(w, h) * 0.28f;

	// The shadow it casts, which is most of what says "raised".
	if (!down) {
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 1.f, 2.f, w - 2.f, h - 1.f, r);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 0x99));
		nvgFill(vg);
	}

	const NVGcolor bright = on ? nvgRGB(0x8d, 0xf5, 0xc2) : nvgRGB(0x9d, 0xa8, 0xb8);
	const NVGcolor dark = on ? nvgRGB(0x24, 0x9c, 0x67) : nvgRGB(0x4c, 0x55, 0x62);

	nvgBeginPath(vg);
	nvgRoundedRect(vg, 1.f, down ? 1.5f : 0.f, w - 2.f, h - 2.f, r);
	// Lit from above, and from below when it is pressed: a cap going down turns the light over.
	nvgFillPaint(vg, nvgLinearGradient(vg, 0.f, down ? h : 0.f, 0.f, down ? 0.f : h,
		bright, dark));
	nvgFill(vg);

	// The rims. A bright one along the top and a dark one along the bottom is the whole of what
	// an edge catching the light looks like.
	nvgBeginPath(vg);
	nvgRoundedRect(vg, 1.5f, (down ? 1.5f : 0.f) + 0.5f, w - 3.f, h - 3.f, r);
	nvgStrokeWidth(vg, 1.f);
	nvgStrokePaint(vg, nvgLinearGradient(vg, 0.f, 0.f, 0.f, h,
		down ? nvgRGBA(0, 0, 0, 0xaa) : nvgRGBA(0xff, 0xff, 0xff, 0xaa),
		down ? nvgRGBA(0xff, 0xff, 0xff, 0x55) : nvgRGBA(0, 0, 0, 0xaa)));
	nvgStroke(vg);

	// The sheen: a soft light across the upper half, which is what tells the eye the top is
	// curved rather than flat.
	if (!down) {
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 2.5f, 1.5f, w - 5.f, h * 0.42f, r * 0.8f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0.f, 1.5f, 0.f, h * 0.5f,
			nvgRGBA(0xff, 0xff, 0xff, on ? 0x88 : 0x66), nvgRGBA(0xff, 0xff, 0xff, 0x00)));
		nvgFill(vg);
	}
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

/** The ink a transport symbol is cut in: dark, so it reads as a mark ON the cap rather than as
another thing beside it. */
static const NVGcolor TRANSPORT_INK = nvgRGB(0x14, 0x18, 0x1e);

void drawPlayGlyph(NVGcontext* vg, math::Vec size, bool playing) {
	const float w = size.x, h = size.y;
	nvgFillColor(vg, TRANSPORT_INK);
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
	nvgFillColor(vg, TRANSPORT_INK);
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
	float t = 0.5f;
	if (ParamQuantity* pq = getParamQuantity()) {
		const float lo = pq->getMinValue(), hi = pq->getMaxValue();
		if (hi > lo)
			t = math::clamp((pq->getValue() - lo) / (hi - lo), 0.f, 1.f);
	}
	const float a = minAngle + (maxAngle - minAngle) * t;
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
	box.size = mm2px(math::Vec((float) wide * perFigure + FIGURE_SURROUND,
		figureMM + FIGURE_SURROUND));
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
	if (ParamQuantity* pq = getParamQuantity())
		text = pq->getDisplayValueString();
	nvgFontFaceId(args.vg, face->handle);
	// The size that makes a capital exactly as tall as the figure height asked for.
	nvgFontSize(args.vg, mm2px(math::Vec(0, figureMM)).y / FIGURE_CAP);
	nvgFillColor(args.vg, nvgRGB(0x3d, 0xe0, 0x7a));
	nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	nvgText(args.vg, w / 2.f, h / 2.f, text.c_str(), NULL);
}

void Readout::openList() {
	ParamQuantity* pq = getParamQuantity();
	if (!pq)
		return;
	const int lo = (int) std::round(pq->getMinValue());
	const int hi = (int) std::round(pq->getMaxValue());
	// A LIST, NOT A RANGE. Anything with more steps than a menu can hold is not a thing to
	// choose from a list, and a knob is the right control for it.
	if (hi - lo > 128)
		return;
	const int now = (int) std::round(pq->getValue());
	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel(pq->getLabel()));
	for (int v = lo; v <= hi; v++) {
		// Asked of the parameter rather than printed here, so a switch lists its names and a
		// count lists its counts, each with whatever unit the module gave it.
		const float was = pq->getValue();
		pq->setValue((float) v);
		const std::string text = pq->getDisplayValueString() + pq->getUnit();
		pq->setValue(was);
		ParamQuantity* q = pq;
		menu->addChild(createCheckMenuItem(text, "", [=]() { return v == now; },
			[=]() { q->setValue((float) v); }));
	}
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

void Lamps::fit() {
	const int n = std::max(1, (int) names.size());
	const float along = 2.f * LAMP_R + (n - 1) * pitch;
	const float names_w = lampsNamesWidth(names, nameSize);
	const bool sided = (labelSide == Panel::RIGHT)
		|| (!horizontal && labelSide == Panel::LEFT);
	const float across = 2.f * LAMP_R + (sided ? names_w : 0.f);
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
		nvgFillColor(args.vg, on ? nvgRGB(0xe8, 0x38, 0x28) : nvgRGB(0x2e, 0x34, 0x3d));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, on ? nvgRGB(0xff, 0xa0, 0x92) : nvgRGB(0x4a, 0x52, 0x5e));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		// A lit lamp glows a little, which is what makes it read as lit rather than as merely
		// a different colour — the same cue a panel lamp gives.
		if (on) {
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, c.x, c.y, LAMP_R * 2.4f);
			NVGpaint glow = nvgRadialGradient(args.vg, c.x, c.y, LAMP_R, LAMP_R * 2.4f,
				nvgRGBA(0xe8, 0x38, 0x28, 0x60), nvgRGBA(0xe8, 0x38, 0x28, 0x00));
			nvgFillPaint(args.vg, glow);
			nvgFill(args.vg);
		}

		if (!font || font->handle < 0)
			continue;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, nameSize);
		nvgFillColor(args.vg, on ? PANEL_INK : PANEL_DIM);
		// A NAME MAY BE SEVERAL LINES, split on the newlines the slash rule writes, and set
		// about the lamp's own line rather than downward from it. Short lines beside a lamp
		// read better than one long one that pushes the panel wider than it needs to be.
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
			nvgText(args.vg, tx, top + step * (float) k, lines[k].c_str(), NULL);
	}
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
