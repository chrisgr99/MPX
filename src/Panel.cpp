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

// Voice cables are drawn in this, and so are the jacks at each end, so the domain shows in a
// patch without anybody having to remember what was plugged in where. Violet: no signal family
// uses it, and it is not a colour Rack offers from its own palette either.
const NVGcolor NOTE_CABLE = nvgRGB(0xa8, 0x7c, 0xff);


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
		nvgFillColor(args.vg, PANEL_DIM);
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

	// The rings, under the ports. A dark disc first so the ring reads as a ring around a hole
	// rather than as a spot of colour the port happens to sit on.
	for (const Ring& ring : rings) {
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, ring.x, ring.y, mm2px(math::Vec(4.6f, 0)).x);
		nvgFillColor(args.vg, nvgRGB(0x12, 0x15, 0x1a));
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, ring.x, ring.y, mm2px(math::Vec(4.1f, 0)).x);
		nvgStrokeColor(args.vg, ring.color);
		nvgStrokeWidth(args.vg, mm2px(math::Vec(0.9f, 0)).x);
		nvgStroke(args.vg);
	}

	for (const Label& label : labels) {
		nvgFontFaceId(args.vg, label.heading && face && face->handle >= 0
			? face->handle : font->handle);
		nvgFontSize(args.vg, label.size > 0.f ? label.size : (label.heading ? 10.f : 8.f));
		nvgFillColor(args.vg, label.heading ? PANEL_INK : PANEL_DIM);
		nvgTextAlign(args.vg, alignFlag(label.align) | NVG_ALIGN_MIDDLE);
		nvgText(args.vg, label.x, label.y, label.text.c_str(), NULL);
	}

	// Drawn last so nothing sits on top of it.
	nvgBeginPath(args.vg);
	nvgRoundedRect(args.vg, 1.f, 1.f, box.size.x - 2.f, box.size.y - 2.f, 3.f);
	nvgStrokeColor(args.vg, PANEL_EDGE);
	nvgStrokeWidth(args.vg, 1.5f);
	nvgStroke(args.vg);

	Widget::draw(args);
}


// ---- the lamp list ---------------------------------------------------------------------------

static const float LAMP_R = 4.2f;

math::Vec Lamps::lampPos(int i) {
	return horizontal ? math::Vec(LAMP_R + i * pitch, box.size.y / 2.f)
		: math::Vec(box.size.x / 2.f, LAMP_R + i * pitch);
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
		nvgFontSize(args.vg, 8.f);
		nvgFillColor(args.vg, on ? PANEL_INK : PANEL_DIM);
		if (labelSide == Panel::RIGHT) {
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, c.x + LAMP_R + 5.f, c.y, names[i].c_str(), NULL);
		}
		else {
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
			nvgText(args.vg, c.x - LAMP_R - 5.f, c.y, names[i].c_str(), NULL);
		}
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
