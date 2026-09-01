#include "plugin.hpp"

namespace px {


// The same face the other Dreamer Development modules use, so a rack with both in it does not
// look like two racks. No artwork ships with the plugin: every panel is drawn here.
static const NVGcolor PANEL_BG = nvgRGB(0x1b, 0x1f, 0x26);
static const NVGcolor PANEL_INK = nvgRGB(0xe6, 0xe8, 0xec);
static const NVGcolor PANEL_DIM = nvgRGB(0x8f, 0x98, 0xa5);
static const NVGcolor PANEL_EDGE = nvgRGB(0x3d, 0xd6, 0x8c);

// Note cables are drawn in this, so the domain is one glance rather than a memory of what was
// patched where. Violet: none of the signal families in Clarity uses it, and it is not a colour
// Rack offers from its own palette either.
const NVGcolor NOTE_CABLE = nvgRGB(0xa8, 0x7c, 0xff);


static std::shared_ptr<window::Font> bodyFont() {
	return APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
}

static std::shared_ptr<window::Font> titleFont() {
	return APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf"));
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

	for (const Label& label : labels) {
		nvgFontFaceId(args.vg, label.heading && face && face->handle >= 0
			? face->handle : font->handle);
		nvgFontSize(args.vg, label.heading ? 10.f : 8.f);
		nvgFillColor(args.vg, label.heading ? PANEL_INK : PANEL_DIM);
		nvgTextAlign(args.vg, (label.left ? NVG_ALIGN_LEFT : NVG_ALIGN_CENTER)
			| NVG_ALIGN_MIDDLE);
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


} // namespace px
