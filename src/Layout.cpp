#include "Layout.hpp"

#include <cmath>
#include <cstdio>
#include <set>

namespace px {


Item* Layout::find(const std::string& key) {
	for (Item& item : items) {
		if (item.key == key)
			return &item;
	}
	return NULL;
}

void Layout::bindOffsets() {
	// Whatever the code has just said each label is called, before any saved file is read.
	for (Item& item : items) {
		if (item.kind == Item::LABEL)
			item.defaultText = item.text;
	}

	for (Item& item : items) {
		if (item.owner.empty())
			continue;
		Item* owner = find(item.owner);
		if (!owner)
			continue;
		item.dx = item.x - owner->x;
		item.dy = item.y - owner->y;
	}
}

void Layout::resolve() {
	// One level deep on purpose. A label belongs to a control; nothing belongs to a label, and
	// allowing a chain would mean deciding what to do about a loop.
	for (Item& item : items) {
		if (item.owner.empty())
			continue;
		Item* owner = find(item.owner);
		if (!owner)
			continue;
		item.x = owner->x + item.dx;
		item.y = owner->y + item.dy;
	}
}


// ---- the saved file --------------------------------------------------------------------------

/** WHERE SAVED PANELS LIVE, under the host's user folder. Named here rather than written into
the path, because Layout and Panel are meant to be dropped into another plugin unchanged and a
plugin's own slug is the one thing in them that would not travel. */
std::string layoutFolder = "DreamerMPX";

std::string layoutUserPath(const std::string& slug) {
	return asset::user(layoutFolder + "/layout/" + slug + ".json");
}

bool layoutHasUser(const std::string& slug) {
	return system::isFile(layoutUserPath(slug));
}

void layoutPlaceDisplay(ModuleWidget* mw, Layout& layout, const std::string& key,
	widget::Widget* display) {

	if (!display)
		return;
	Item* item = layout.find(key);
	if (!item) {
		// Not in the layout: the module still gets its widget, wherever it put it.
		mw->addChild(display);
		return;
	}
	display->box.pos = mm2px(math::Vec(item->x, item->y));
	display->box.size = mm2px(math::Vec(item->w, item->h));
	mw->addChild(display);
	item->widget = display;
}


void layoutApplyUser(const std::string& slug, Layout& layout) {
	FILE* file = std::fopen(layoutUserPath(slug).c_str(), "r");
	if (!file)
		return;
	json_error_t error;
	json_t* rootJ = json_loadf(file, 0, &error);
	std::fclose(file);
	if (!rootJ)
		return;

	// THE WIDTH FIRST, because everything else is placed inside it.
	json_t* hpJ = json_object_get(rootJ, "hp");
	if (json_is_number(hpJ)) {
		const float hp = (float) std::round(json_number_value(hpJ));
		if (hp >= 2.f && hp <= 200.f)
			layout.hp = hp;
	}

	json_t* itemsJ = json_object_get(rootJ, "items");
	if (json_is_object(itemsJ)) {
		const char* key;
		json_t* valueJ;
		json_object_foreach(itemsJ, key, valueJ) {
			Item* item = layout.find(key);
			// A key the module no longer has is skipped rather than treated as an error: a
			// saved file outliving a control it named is ordinary, not broken.
			if (!item)
				continue;
			if (item->owner.empty()) {
				json_t* xJ = json_object_get(valueJ, "x");
				json_t* yJ = json_object_get(valueJ, "y");
				if (json_is_number(xJ))
					item->x = json_number_value(xJ);
				if (json_is_number(yJ))
					item->y = json_number_value(yJ);
			}
			else {
				json_t* dxJ = json_object_get(valueJ, "dx");
				json_t* dyJ = json_object_get(valueJ, "dy");
				if (json_is_number(dxJ))
					item->dx = json_number_value(dxJ);
				if (json_is_number(dyJ))
					item->dy = json_number_value(dyJ);
			}
			if (item->kind == Item::LABEL) {
				json_t* textJ = json_object_get(valueJ, "text");
				if (json_is_string(textJ))
					item->text = layoutTextFromUser(json_string_value(textJ));
				json_t* hiddenJ = json_object_get(valueJ, "hidden");
				if (json_is_boolean(hiddenJ))
					item->hidden = json_boolean_value(hiddenJ);
			}

			// WHATEVER ELSE THE PROPERTIES MENU WAS USED ON. A property that is in the file was
			// deliberately set, so it is applied and remembered as the user's; one that is not
			// is left to the module, which is free to keep improving it.
			auto took = [&](const char* prop) { item->userProps.insert(prop); };
			if (json_t* j = json_object_get(valueJ, "style")) {
				if (json_is_string(j)) { item->style = json_string_value(j); took("style"); }
			}
			if (json_t* j = json_object_get(valueJ, "ticks")) {
				if (json_is_integer(j)) { item->ticks = (int) json_integer_value(j); took("ticks"); }
			}
			if (json_t* j = json_object_get(valueJ, "marks")) {
				if (json_is_array(j)) {
					item->tickMarks.clear();
					size_t k; json_t* v;
					json_array_foreach(j, k, v)
						if (json_is_string(v))
							item->tickMarks.push_back(json_string_value(v));
					took("marks");
				}
			}
			if (json_t* j = json_object_get(valueJ, "names")) {
				if (json_is_array(j)) {
					item->names.clear();
					size_t k; json_t* v;
					json_array_foreach(j, k, v)
						if (json_is_string(v))
							item->names.push_back(layoutTextFromUser(json_string_value(v)));
					took("names");
				}
			}
			if (json_t* j = json_object_get(valueJ, "pitch")) {
				if (json_is_number(j)) { item->pitch = json_number_value(j); took("pitch"); }
			}
			if (json_t* j = json_object_get(valueJ, "side")) {
				if (json_is_integer(j)) {
					item->labelSide = (Panel::Align) json_integer_value(j); took("side");
				}
			}
			if (json_t* j = json_object_get(valueJ, "horizontal")) {
				if (json_is_boolean(j)) {
					item->horizontal = json_boolean_value(j); took("horizontal");
				}
			}
			if (json_t* j = json_object_get(valueJ, "size")) {
				if (json_is_number(j)) { item->size = json_number_value(j); took("size"); }
			}
			if (json_t* j = json_object_get(valueJ, "chars")) {
				if (json_is_integer(j)) { item->chars = (int) json_integer_value(j); took("chars"); }
			}
			if (json_t* j = json_object_get(valueJ, "height")) {
				if (json_is_number(j)) { item->h = json_number_value(j); took("height"); }
			}
			if (json_t* j = json_object_get(valueJ, "diameter")) {
				if (json_is_number(j)) { item->diameter = json_number_value(j); took("diameter"); }
			}
			if (json_t* j = json_object_get(valueJ, "nameSize")) {
				if (json_is_number(j)) { item->nameSize = json_number_value(j); took("nameSize"); }
			}
			if (json_t* j = json_object_get(valueJ, "align")) {
				if (json_is_integer(j)) {
					item->align = (Panel::Align) json_integer_value(j); took("align");
				}
			}
		}
	}
	json_decref(rootJ);
	// Owners may have moved, so everything that follows one is put back beside it.
	layout.resolve();
}

void layoutSaveUser(const std::string& slug, const Layout& layout) {
	json_t* itemsJ = json_object();
	for (const Item& item : layout.items) {
		json_t* itemJ = json_object();
		if (item.owner.empty()) {
			json_object_set_new(itemJ, "x", json_real(item.x));
			json_object_set_new(itemJ, "y", json_real(item.y));
		}
		else {
			// The offset, not the position: an item that follows a control has no position of
			// its own worth saving, and saving one would fix it in place the next time the
			// control moved in the code.
			json_object_set_new(itemJ, "dx", json_real(item.dx));
			json_object_set_new(itemJ, "dy", json_real(item.dy));
		}
		// Only labels carry text, and only their text is worth saving: everything else about
		// an item is what the module says it is.
		if (item.kind == Item::LABEL) {
			// WRITTEN THE WAY IT WAS TYPED, slashes and all, rather than with the newlines it
			// became. The file is something you read and edit by hand, and a line break in it
			// should look like the thing you would type; it also means a slash written into a
			// file before this rule existed starts obeying the rule rather than sitting there
			// as a slash nothing will ever convert.
			json_object_set_new(itemJ, "text",
				json_string(layoutTextToUser(item.text).c_str()));
			if (item.hidden)
				json_object_set_new(itemJ, "hidden", json_true());
		}
		// AND WHATEVER ELSE WAS DELIBERATELY CHANGED. Only what the properties menu was used
		// on, so a default the module later improves still reaches a panel somebody has edited.
		if (item.userSet("style"))
			json_object_set_new(itemJ, "style", json_string(item.style.c_str()));
		if (item.userSet("ticks"))
			json_object_set_new(itemJ, "ticks", json_integer(item.ticks));
		if (item.userSet("marks")) {
			json_t* a = json_array();
			for (const std::string& m : item.tickMarks)
				json_array_append_new(a, json_string(m.c_str()));
			json_object_set_new(itemJ, "marks", a);
		}
		if (item.userSet("names")) {
			json_t* a = json_array();
			for (const std::string& n : item.names)
				json_array_append_new(a, json_string(layoutTextToUser(n).c_str()));
			json_object_set_new(itemJ, "names", a);
		}
		if (item.userSet("pitch"))
			json_object_set_new(itemJ, "pitch", json_real(item.pitch));
		if (item.userSet("side"))
			json_object_set_new(itemJ, "side", json_integer((int) item.labelSide));
		if (item.userSet("horizontal"))
			json_object_set_new(itemJ, "horizontal", json_boolean(item.horizontal));
		if (item.userSet("size"))
			json_object_set_new(itemJ, "size", json_real(item.size));
		if (item.userSet("diameter"))
			json_object_set_new(itemJ, "diameter", json_real(item.diameter));
		if (item.userSet("nameSize"))
			json_object_set_new(itemJ, "nameSize", json_real(item.nameSize));
		if (item.userSet("chars"))
			json_object_set_new(itemJ, "chars", json_integer(item.chars));
		if (item.userSet("height"))
			json_object_set_new(itemJ, "height", json_real(item.h));
		if (item.userSet("align"))
			json_object_set_new(itemJ, "align", json_integer((int) item.align));
		json_object_set_new(itemsJ, item.key.c_str(), itemJ);
	}
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "hp", json_real(layout.hp));
	json_object_set_new(rootJ, "items", itemsJ);

	system::createDirectories(asset::user(layoutFolder + "/layout"));
	FILE* file = std::fopen(layoutUserPath(slug).c_str(), "w");
	if (file) {
		json_dumpf(rootJ, file, JSON_INDENT(2) | JSON_SORT_KEYS);
		std::fclose(file);
	}
	json_decref(rootJ);
}

void layoutResetUser(const std::string& slug) {
	const std::string path = layoutUserPath(slug);
	if (system::isFile(path))
		system::remove(path);
}


// ---- building ---------------------------------------------------------------------------------


/** Millimetres from Rack's own pixels. Rack draws at seventy-five to the inch, so this is not
the 96 a screen would use — getting that wrong made every lamp column a quarter wider than the
arithmetic said, and two of them overlapped while the sums claimed they were clear. */
static float mm(float px) { return px / (75.f / 25.4f); }

/** How many lines a piece of text is, and how long its longest one is. */
static size_t textLines(const std::string& s) {
	size_t n = 1;
	for (char c : s)
		if (c == '\n')
			n++;
	return n;
}

static size_t textLongest(const std::string& s) {
	size_t longest = 0, start = 0;
	while (start <= s.size()) {
		const size_t brk = s.find('\n', start);
		longest = std::max(longest, (brk == std::string::npos ? s.size() : brk) - start);
		if (brk == std::string::npos)
			break;
		start = brk + 1;
	}
	return longest;
}

static float knobWidthMM(const std::string& style) {
	if (style == "knob.huge") return 18.24f;
	if (style == "knob.large") return 12.19f;
	if (style == "knob.small") return 7.68f;
	if (style == "knob.trim") return 6.05f;
	return 9.60f;
}

void layoutRefreshPanel(Panel* panel, Layout& layout) {
	panel->labels.clear();
	panel->brackets.clear();
	panel->scales.clear();
	for (const Item& item : layout.items) {
		if (item.kind == Item::PARAM && item.ticks > 0 && item.style != "lamps") {
			// MEASURED FROM THE KNOB'S OWN SIZE, so that a scale stays round its knob when the
			// editor moves it and does not have to be told again how big the knob is. The sizes
			// are the component SVGs' own: RoundBlackKnob is 28.35 px across, which is 9.6 mm,
			// so its edge is 4.8 mm out. Guessing this put the ticks inside the knob.
			const float half = (item.diameter > 0.f ? item.diameter
				: knobWidthMM(item.style)) / 2.f;
			Panel::Scale sc;
			sc.x = mm2px(math::Vec(item.x, 0)).x;
			sc.y = mm2px(math::Vec(0, item.y)).y;
			// THE NUMBERS STAND CLEAR OF THE TICKS. A tick reaches 1.3 mm past the metal and a
			// number at six points is 0.73 mm from its middle to its edge, so a number centred
			// 2.6 mm out leaves a gap of a little over half a millimetre. Centred on the mark,
			// which is why the radius is not simply the gap added on.
			sc.radius = mm2px(math::Vec(half + 0.3f, 0)).x;
			sc.length = mm2px(math::Vec(1.f, 0)).x;
			sc.textRadius = mm2px(math::Vec(half + 2.6f, 0)).x;
			sc.count = item.ticks;
			sc.marks = item.tickMarks;
			sc.textSize = item.nameSize > 0.f ? item.nameSize : 6.f;
			panel->scales.push_back(sc);
		}
		if (item.kind == Item::BRACKET) {
			Panel::Bracket b;
			b.x = mm2px(math::Vec(item.x, 0)).x;
			b.y = mm2px(math::Vec(0, item.y)).y;
			b.h = mm2px(math::Vec(0, item.h)).y;
			b.arm = mm2px(math::Vec(item.w, 0)).x;
			panel->brackets.push_back(b);
		}
		else if (item.kind == Item::LABEL) {
			Panel::Label l;
			l.x = mm2px(math::Vec(item.x, 0)).x;
			l.y = mm2px(math::Vec(0, item.y)).y;
			l.text = item.text;
			l.align = item.align;
			l.heading = item.heading;
			l.size = item.size;
			l.hidden = item.hidden;
			panel->labels.push_back(l);
		}
	}
}

/** MAKING THE CONTROL ITSELF, kept apart from building the panel because the editor has to be
able to make one again: a knob asked to be a different size, or a lamp column given another
position, is a different widget rather than the same widget moved. */
static ParamWidget* makeParam(engine::Module* module, Item& item) {
	const math::Vec pos = mm2px(math::Vec(item.x, item.y));
	if (item.style == "lamps") {
		Lamps* lamps = createParam<Lamps>(pos, module, item.id);
		lamps->pitch = item.horizontal
			? mm2px(math::Vec(item.pitch, 0)).x : mm2px(math::Vec(0, item.pitch)).y;
		lamps->names = item.names;
		lamps->horizontal = item.horizontal;
		lamps->labelSide = item.labelSide;
		if (item.nameSize > 0.f)
			lamps->nameSize = item.nameSize;
		// A lamp list is placed by its CORNER, because it is a list rather than a point;
		// everything else on a panel is placed by its centre.
		lamps->box.pos = pos;
		// AND IT SIZES ITSELF. The box was being set from the item's w and h, which a layout is
		// under no obligation to fill in — and when they are left at nought the box is empty, so
		// the lamps are drawn and can never be clicked. Every lamp control in this plugin was in
		// that state. The widget knows how much room its own lamps and names need.
		lamps->fit();
		return lamps;
	}
	if (item.style == "readout") {
		Readout* r = createParam<Readout>(pos, module, item.id);
		r->setFigures(item.chars, item.h > 0.f ? item.h : 2.8f);
		r->box.pos = pos.minus(r->box.size.div(2.f));
		return r;
	}
	if (item.style == "button" || item.style == "latch") {
		ParamWidget* w = (item.style == "latch")
			? (ParamWidget*) createParamCentered<DreamerLatch>(pos, module, item.id)
			: (ParamWidget*) createParamCentered<DreamerButton>(pos, module, item.id);
		// SIZED LIKE A KNOB IS. A button was one size for ever, which is fine until one of them
		// has to sit in a corner of a display and be found there.
		if (item.diameter > 0.f) {
			const float px = mm2px(item.diameter);
			w->box.pos = pos.minus(math::Vec(px, px).div(2.f));
			w->box.size = math::Vec(px, px);
		}
		return w;
	}
	if (item.style == "transport.play")
		return createParamCentered<DreamerPlay>(pos, module, item.id);
	if (item.style == "transport.rewind")
		return createParamCentered<DreamerRewind>(pos, module, item.id);
	// A KNOB IS DRAWN RATHER THAN LOADED, so that its diameter is a number rather than a choice
	// between the five widths Rack ships. The old style names still mean their old widths, so a
	// layout written before this reads the same.
	DreamerKnob* knob = new DreamerKnob;
	knob->module = module;
	knob->paramId = item.id;
	knob->initParamQuantity();
	knob->setDiameter(item.diameter > 0.f ? item.diameter : knobWidthMM(item.style));
	knob->box.pos = pos.minus(knob->box.size.div(2.f));
	return knob;
}

void layoutBuild(ModuleWidget* mw, Panel* panel, Layout& layout) {
	engine::Module* module = mw->module;
	mw->box.size = math::Vec(layout.hp * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);
	panel->box.size = mw->box.size;
	panel->title = layout.title;
	panel->titleAbove = layout.titleAbove;

	for (Item& item : layout.items) {
		const math::Vec pos = mm2px(math::Vec(item.x, item.y));
		switch (item.kind) {
			case Item::PARAM: {
				ParamWidget* p = makeParam(module, item);
				mw->addParam(p);
				item.widget = p;
			} break;
			case Item::PORT_IN: {
				PortWidget* p = createInputCentered<MPXPort>(pos, module, item.id);
				mw->addInput(p);
				item.widget = p;
			} break;
			case Item::PORT_OUT: {
				PortWidget* p = createOutputCentered<MPXPort>(pos, module, item.id);
				mw->addOutput(p);
				item.widget = p;
			} break;
			case Item::LIGHT: {
				// A LAMP THAT CAN SAY NO. Two colours where a module has something to report
				// besides yes: green for right, red for wrong, dark for neither.
				widget::Widget* l = (item.style == "light.greenred")
					? (widget::Widget*) createLightCentered<SmallLight<GreenRedLight>>(
						pos, module, item.id)
					: (widget::Widget*) createLightCentered<SmallLight<GreenLight>>(
						pos, module, item.id);
				mw->addChild(l);
				item.widget = l;
			} break;
			case Item::LABEL:
			case Item::BRACKET:
			case Item::DISPLAY:
				// Nothing to build. A label and a bracket are painted by the panel; a display
				// is made by the module and handed over afterwards.
				break;
		}
	}
	// LAST, so it paints over the ports rather than under them. A jack's colour belongs on the
	// jack: a halo behind one is a different mark that happens to be near it.
	JackPaint* paint = new JackPaint;
	paint->box.size = mw->box.size;
	for (Item& item : layout.items) {
		if ((item.kind != Item::PORT_IN && item.kind != Item::PORT_OUT) || !item.widget)
			continue;
		if (item.ring.a <= 0.f)
			continue;
		JackPaint::Mark mark;
		mark.port = item.widget;
		mark.color = item.ring;
		mark.isOutput = (item.kind == Item::PORT_OUT);
		paint->marks.push_back(mark);
	}
	mw->addChild(paint);

	layoutRefreshPanel(panel, layout);
}


// ---- the editor -------------------------------------------------------------------------------

/** What a drag snaps to. Half a millimetre is fine enough that nothing feels stuck and coarse
enough that two things meant to line up actually do. */
static const float GRID_MM = 0.5f;
/** How close two items have to be on an axis before a guide says they are aligned. */
static const float GUIDE_MM = 0.35f;


/** THE SLASH RULE, in one place because it applies to every piece of text on a panel.

A slash in something typed into the properties menu breaks the line there. Two slashes together
are a slash — which is the whole of the escape, and enough: a panel name is one or two words, and
a rule with more to it than that would need explaining every time. */
std::string layoutTextFromUser(const std::string& typed) {
	std::string out;
	for (size_t i = 0; i < typed.size(); i++) {
		if (typed[i] != '/') {
			out += typed[i];
			continue;
		}
		if (i + 1 < typed.size() && typed[i + 1] == '/') {
			out += '/';
			i++;
		}
		else {
			out += '\n';
		}
	}
	return out;
}

/** And back, so the field opens showing what was typed rather than what it became. */
std::string layoutTextToUser(const std::string& text) {
	std::string out;
	for (char c : text) {
		if (c == '\n')
			out += '/';
		else if (c == '/')
			out += "//";
		else
			out += c;
	}
	return out;
}


/** The longest line of the longest name in a lamp column, which is what sets its width. */
static size_t lampsLongest(const std::vector<std::string>& names) {
	size_t longest = 0;
	for (const std::string& n : names) {
		size_t start = 0;
		while (start <= n.size()) {
			const size_t brk = n.find('\n', start);
			const size_t len = (brk == std::string::npos ? n.size() : brk) - start;
			longest = std::max(longest, len);
			if (brk == std::string::npos)
				break;
			start = brk + 1;
		}
	}
	return longest;
}

/** HOW BIG A THING ACTUALLY LOOKS, which is not the same as how big a thing is to click.

The hit target is generous on purpose; this is the ink. Everything that arranges one item against
another — how far a name sits from its knob, above all — has to use this one, and the numbers in
it are the component SVGs' own rather than anybody's estimate:

    RoundSmallBlackKnob 22.68 px   7.68 mm
    RoundBlackKnob      28.35 px   9.60 mm
    RoundLargeBlackKnob 36.00 px  12.19 mm
    RoundHugeBlackKnob  53.86 px  18.24 mm
    Trimpot             17.86 px   6.05 mm
    PJ301M              23.70 px   8.03 mm

A knob's MARKS are not part of it. They are hairlines and numbers set outside the metal, and a
name measured from the far edge of a tick reads as a millimetre and a half too far — which is
what a name measured from them looked like. */

/** Half the height of text set at this size, and the width of a string of it. Estimated from the
face's proportions, since a real measurement needs a font and a drawing context, and this is
used for arranging rather than for drawing. */
static float textHeightMM(float size) { return mm(0.72f * size); }
static float textWidthMM(float size, size_t chars) { return mm(0.55f * size * chars); }

static math::Rect itemVisual(const Item& item) {
	float w = 8.f, h = 8.f;
	switch (item.kind) {
		case Item::PARAM:
			if (item.style == "lamps") {
				const float along = mm(13.f) + (std::max((size_t) 1, item.names.size()) - 1)
					* item.pitch;
				const float size = item.nameSize > 0.f ? item.nameSize : 8.f;
				const float across = mm(13.f + 5.f
					+ 0.575f * size * (float) lampsLongest(item.names));
				return math::Rect(math::Vec(item.x, item.y),
					item.horizontal ? math::Vec(along, across) : math::Vec(across, along));
			}
			if (item.style == "readout") {
				// The same arithmetic the widget uses, so the outline is the plate.
				const float fig = item.h > 0.f ? item.h : 2.8f;
				// Worked out here rather than read off the widget, because the width is a number
				// in the layout now and no longer changes once the plate exists. Nought means
				// two, as it does in the widget.
				const int wide = item.chars > 0 ? item.chars : 2;
				w = (float) wide * fig * FIGURE_ADVANCE / FIGURE_CAP + FIGURE_SURROUND;
				h = fig + FIGURE_SURROUND;
				return math::Rect(math::Vec(item.x - w / 2.f, item.y - h / 2.f),
					math::Vec(w, h));
			}
			if (item.style == "button" || item.style == "latch"
				|| item.style == "transport.play" || item.style == "transport.rewind")
				w = h = item.diameter > 0.f ? item.diameter : 6.6f;
			else {
				w = h = item.diameter > 0.f ? item.diameter : knobWidthMM(item.style);
				// A KNOB WEARING A SCALE IS BIGGER THAN ITS METAL, and the scale is part of the
				// control rather than decoration near it — you line a knob up by where its
				// numbers fall. The same arithmetic layoutRefreshPanel lays the scale out with:
				// the numbers are centred 2.6 mm past the metal's edge, and a number reaches
				// half its own height beyond that.
				if (item.ticks > 0) {
					const float textSize = item.nameSize > 0.f ? item.nameSize : 6.f;
					w = h = w + 2.f * (2.6f + textHeightMM(textSize) / 2.f);
				}
			}
			break;
		case Item::PORT_IN:
		case Item::PORT_OUT:
			w = h = 8.03f;
			break;
		case Item::LIGHT:
			w = h = 3.f;
			break;
		case Item::BRACKET:
			return math::Rect(math::Vec(item.x, item.y), math::Vec(item.w, item.h));
		case Item::DISPLAY:
			return math::Rect(math::Vec(item.x, item.y), math::Vec(item.w, item.h));
		case Item::LABEL: {
			const float size = item.size > 0.f ? item.size : (item.heading ? 10.f : 8.f);
			w = textWidthMM(size, std::max((size_t) 1, textLongest(item.text)));
			h = textHeightMM(size) + (float) (textLines(item.text) - 1) * mm(panelLineStep(size));
			float left = item.x - w / 2.f;
			if (item.align == Panel::LEFT) left = item.x;
			else if (item.align == Panel::RIGHT) left = item.x - w;
			return math::Rect(math::Vec(left, item.y - h / 2.f), math::Vec(w, h));
		}
	}
	return math::Rect(math::Vec(item.x - w / 2.f, item.y - h / 2.f), math::Vec(w, h));
}

/** WHICH SIDE A NAME SITS ON. Four, because those are the four a panel ever uses. */
enum Place { PLACE_BELOW, PLACE_ABOVE, PLACE_LEFT, PLACE_RIGHT, NUM_PLACES };
static const char* PLACE_NAMES[NUM_PLACES] = {"below", "above", "left", "right"};

/** Where a name sits now, read from where it actually is rather than remembered. Nothing has to
be stored: the side is whichever way it lies from the middle of what it names, and the distance
is the gap between the two. */
static int placeOf(const Item& owner, const Item& lab) {
	const math::Rect o = itemVisual(owner), l = itemVisual(lab);
	const float dx = (l.pos.x + l.size.x / 2.f) - (o.pos.x + o.size.x / 2.f);
	const float dy = (l.pos.y + l.size.y / 2.f) - (o.pos.y + o.size.y / 2.f);
	if (std::fabs(dx) > std::fabs(dy))
		return dx < 0.f ? PLACE_LEFT : PLACE_RIGHT;
	return dy < 0.f ? PLACE_ABOVE : PLACE_BELOW;
}

static float gapOf(const Item& owner, const Item& lab) {
	const math::Rect o = itemVisual(owner), l = itemVisual(lab);
	switch (placeOf(owner, lab)) {
		case PLACE_ABOVE: return o.pos.y - (l.pos.y + l.size.y);
		case PLACE_LEFT:  return o.pos.x - (l.pos.x + l.size.x);
		case PLACE_RIGHT: return l.pos.x - (o.pos.x + o.size.x);
		default:          return l.pos.y - (o.pos.y + o.size.y);
	}
}

/** Puts a name on one side of what it names, a given distance from its edge. The distance is
between the two things you can see, which is the only measure anybody means by it. */
static void placeLabel(const Item& owner, Item& lab, int place, float gap) {
	const math::Rect o = itemVisual(owner), l = itemVisual(lab);
	const float ocx = o.pos.x + o.size.x / 2.f, ocy = o.pos.y + o.size.y / 2.f;
	float cx = ocx, cy = ocy;
	switch (place) {
		case PLACE_ABOVE: cy = o.pos.y - gap - l.size.y / 2.f; break;
		case PLACE_LEFT:  cx = o.pos.x - gap - l.size.x / 2.f; break;
		case PLACE_RIGHT: cx = o.pos.x + o.size.x + gap + l.size.x / 2.f; break;
		default:          cy = o.pos.y + o.size.y + gap + l.size.y / 2.f; break;
	}
	// An item's x is its centre, its left edge or its right edge depending on how it is set,
	// so the centre we want has to be turned back into whichever of those this label uses.
	float x = cx;
	if (lab.align == Panel::LEFT) x = cx - l.size.x / 2.f;
	else if (lab.align == Panel::RIGHT) x = cx + l.size.x / 2.f;
	lab.dx = x - owner.x;
	lab.dy = cy - owner.y;
}


/** How big each kind of thing is, FOR HIT-TESTING, which is a different question from how big
it looks.

A target wants a little air round it — a control you have to hit exactly is a control you miss —
but only a little. This used to round every button up to nine millimetres whatever its real size,
and to give a knob with a scale six millimetres of margin, which was a target half as wide again
as the thing inside it: two controls near each other and the wrong one answered.

A millimetre and a bit, measured from the METAL rather than from the scale around it. The scale
belongs to the outline, which is about seeing; a hand reaching for a knob reaches for the knob. */
static math::Rect itemRect(const Item& item) {
	float w = 8.f, h = 8.f;
	switch (item.kind) {
		case Item::PARAM:
			if (item.style == "button" || item.style == "latch"
				|| item.style == "transport.play" || item.style == "transport.rewind") {
				w = h = (item.diameter > 0.f ? item.diameter : 6.6f) + 1.2f;
			}
			else if (item.style == "lamps") {
				// Exactly what the widget gives itself, so what can be clicked is what can be
				// seen — worked out in the same millimetres rather than in an estimate.
				const math::Rect v = itemVisual(item);
				return v;
			}
			else {
				// The metal with a little air to grab by, and NOT the scale: itemVisual now
				// includes a knob's numbers, and grabbing at where a number falls is grabbing
				// several millimetres from anything you can see to press.
				w = h = (item.diameter > 0.f ? item.diameter : knobWidthMM(item.style)) + 1.6f;
			}
			break;
		case Item::PORT_IN:
		case Item::PORT_OUT:
			w = h = 9.2f;   // the jack, plus a little air to grab it by
			break;
		case Item::LIGHT:
			w = h = 5.f;
			break;
		case Item::BRACKET:
			// Placed by its top-left corner, like the lamp list, because it is an extent
			// rather than a point.
			return math::Rect(math::Vec(item.x - 1.f, item.y), math::Vec(item.w + 2.f, item.h));
		case Item::DISPLAY:
			// An area, by its corner: exactly the rectangle it draws in.
			return math::Rect(math::Vec(item.x, item.y), math::Vec(item.w, item.h));
		case Item::LABEL: {
			const float size = item.size > 0.f ? item.size : (item.heading ? 10.f : 8.f);
			// Estimated rather than measured: a text width needs a font and a context, and a
			// hit target that is a little generous costs nothing.
			w = std::fmax(6.f, textLongest(item.text) * size * 0.145f);
			h = 4.5f + (float) (textLines(item.text) - 1) * mm(panelLineStep(size));
			float x = item.x;
			if (item.align == Panel::LEFT) x = item.x + w / 2.f;
			else if (item.align == Panel::RIGHT) x = item.x - w / 2.f;
			return math::Rect(math::Vec(x - w / 2.f, item.y - h / 2.f), math::Vec(w, h));
		}
	}
	return math::Rect(math::Vec(item.x - w / 2.f, item.y - h / 2.f), math::Vec(w, h));
}

/** Sits on top of the module while the mode is on, and takes every click before the controls
underneath can. A knob you are dragging to a new place must not also turn while you do it. */
struct PanelEditor : widget::OpaqueWidget {
	ModuleWidget* mw = NULL;
	Panel* panel = NULL;
	Layout* layout = NULL;
	std::string slug;
	/** The item the pointer took hold of. Everything selected moves with it. */
	int grabbed = -1;
	/** Where in the item the pointer took hold, so it does not jump to the centre. */
	math::Vec grabOffset;
	std::set<int> selection;
	/** While a marquee is being dragged, in millimetres. */
	bool marquee = false;
	math::Vec marqueeFrom, marqueeTo;
	bool dirty = false;
	/** Set while a drag lines this item up with another, in millimetres. */
	float guideX = -1.f, guideY = -1.f;

	/** WHICH EDGE IS BEING DRAGGED, -1 for the left, +1 for the right, 0 for neither. A panel's
	width is a thing you judge by looking at it, so it is a thing you should be able to drag. */
	int resizing = 0;
	/** The width and the pointer when the drag started, so the new width is worked out from
	where the pointer has got to rather than accumulated frame by frame.

	THE POINTER IS REMEMBERED IN THE SCENE'S OWN COORDINATES, not the panel's. Dragging the left
	edge moves the panel, which moves the frame the panel measures in, so a start position held
	in panel millimetres shifts by exactly as much as the panel grew — and the drag chases its
	own tail across the rack. */
	float resizeHP = 0.f;
	float resizeFromScene = 0.f;

	/** How near an edge counts as being on it. */
	static constexpr float EDGE_MM = 2.5f;

	int edgeAt(math::Vec posMM) {
		const float w = layout->hp * 5.08f;
		if (posMM.x <= EDGE_MM)
			return -1;
		if (posMM.x >= w - EDGE_MM)
			return 1;
		return 0;
	}

	/** A NEW WIDTH, IN WHOLE HP, because that is the only width a Rack module may have.
	Dragging the left edge moves the panel as well as resizing it, so that what is on it stays
	where it is on the screen and the space appears where the edge was pulled to. */
	void resizeTo(float hp) {
		hp = std::round(hp);
		if (hp < 3.f)
			hp = 3.f;
		if (hp > 100.f)
			hp = 100.f;
		if (hp == layout->hp)
			return;
		const float grew = hp - layout->hp;
		layout->hp = hp;
		if (resizing < 0) {
			for (Item& item : layout->items) {
				if (item.owner.empty())
					item.x += grew * 5.08f;
			}
		}
		mw->box.size.x = hp * RACK_GRID_WIDTH;
		panel->box.size = mw->box.size;
		box.size = mw->box.size;
		layout->resolve();
		for (Item& item : layout->items)
			placeWidget(item);
		layoutRefreshPanel(panel, *layout);
		// The neighbours have to give way, or the panel simply draws over them.
		math::Vec pos = mw->box.pos;
		if (resizing < 0)
			pos.x -= grew * RACK_GRID_WIDTH;
		APP->scene->rack->setModulePosForce(mw, pos);
		dirty = true;
	}

	int itemAt(math::Vec posMM) {
		// Backwards, so the thing drawn last — and so on top — is the thing you grab.
		for (int i = (int) layout->items.size() - 1; i >= 0; i--) {
			if (itemRect(layout->items[i]).contains(posMM))
				return i;
		}
		return -1;
	}

	math::Vec toMM(math::Vec px) {
		return math::Vec(px.x / RACK_GRID_WIDTH * 5.08f, px.y / RACK_GRID_WIDTH * 5.08f);
	}

	/** Puts a widget where its item says it is.

	BY THE CORNER OR BY THE CENTRE, and it has to be the same answer the outline uses. A lamp
	list and a display are areas and are placed by their top-left corner; a knob, a jack and a
	light are points and are placed by their centre.

	Getting this wrong is invisible until something is dragged: the outline is drawn from the
	item and the widget is moved from the same item, so if the two disagree about what x and y
	MEAN, the widget lands half its own size away from its outline. That is what displays did —
	the box was drawn down and to the right of the thing it belonged to, and anyone lining the
	display up by its outline was really putting it half a plate away from where they wanted. */
	static void placeWidget(Item& item) {
		if (!item.widget)
			return;
		const math::Vec pos = mm2px(math::Vec(item.x, item.y));
		if (item.style == "lamps" || item.kind == Item::DISPLAY)
			item.widget->box.pos = pos;
		else
			item.widget->box.pos = pos.minus(item.widget->box.size.div(2.f));
	}

	bool selected(int index) {
		return selection.count(index) > 0;
	}

	/** Whether this item's owner is also in the selection. Such an item is left alone while
	the group moves: it will follow its owner, and moving it as well would move it twice. */
	bool ownerSelected(const Item& item) {
		if (item.owner.empty())
			return false;
		for (int i : selection) {
			if (layout->items[i].key == item.owner)
				return true;
		}
		return false;
	}

	/** Moves everything selected, with the grabbed item leading. Only the grabbed item snaps;
	the rest keep their distance from it exactly. Two things dragged together should not drift
	apart because one of them found a grid line first. */
	void moveSelection(math::Vec posMM) {
		if (grabbed < 0)
			return;
		Item& lead = layout->items[grabbed];
		float x = posMM.x, y = posMM.y;
		guideX = guideY = -1.f;
		// ALT places freely. Shift is spoken for: it adds to the selection, and a modifier
		// that meant two things during one gesture would be a coin toss.
		if (!(APP->window->getMods() & GLFW_MOD_ALT)) {
			// Lining up with something else beats lining up with the grid: a row of jacks
			// should agree with each other rather than each agree with a ruler. Only things
			// staying put are worth lining up with.
			for (int i = 0; i < (int) layout->items.size(); i++) {
				if (selected(i))
					continue;
				if (std::fabs(layout->items[i].x - x) < GUIDE_MM) {
					x = layout->items[i].x;
					guideX = x;
				}
				if (std::fabs(layout->items[i].y - y) < GUIDE_MM) {
					y = layout->items[i].y;
					guideY = y;
				}
			}
			if (guideX < 0.f)
				x = std::round(x / GRID_MM) * GRID_MM;
			if (guideY < 0.f)
				y = std::round(y / GRID_MM) * GRID_MM;
		}
		const float ddx = x - lead.x;
		const float ddy = y - lead.y;
		if (ddx == 0.f && ddy == 0.f)
			return;

		for (int i : selection) {
			Item& item = layout->items[i];
			if (ownerSelected(item))
				continue;
			item.x += ddx;
			item.y += ddy;
			if (!item.owner.empty()) {
				Item* owner = layout->find(item.owner);
				if (owner) {
					item.dx = item.x - owner->x;
					item.dy = item.y - owner->y;
				}
			}
		}
		// Everything that follows a moved control is put back beside it, then every widget is
		// placed from wherever its item now says it is.
		layout->resolve();
		for (Item& item : layout->items)
			placeWidget(item);
		layoutRefreshPanel(panel, *layout);
		dirty = true;
	}

	void selectInMarquee(bool add) {
		if (!add)
			selection.clear();
		const math::Vec lo(std::fmin(marqueeFrom.x, marqueeTo.x),
			std::fmin(marqueeFrom.y, marqueeTo.y));
		const math::Vec hi(std::fmax(marqueeFrom.x, marqueeTo.x),
			std::fmax(marqueeFrom.y, marqueeTo.y));
		for (int i = 0; i < (int) layout->items.size(); i++) {
			// Touched rather than enclosed. A marquee that only takes what it swallows whole
			// means drawing a careful box round a label whose width you cannot see.
			const math::Rect r = itemRect(layout->items[i]);
			if (r.pos.x + r.size.x >= lo.x && r.pos.x <= hi.x
				&& r.pos.y + r.size.y >= lo.y && r.pos.y <= hi.y)
				selection.insert(i);
		}
	}

	/** JACKS ARE HIDDEN WHILE EDITING, and this is not tidiness.

	Clarity's overlay sits at the scene level with first refusal on every click, looks for a
	port under the pointer and takes its cable. It never asks whether anything above that port
	wanted the click, so an editor drawn over the module is not offered it — dragging a jack to
	move it pulled a cable out instead.

	A hidden widget is not found by that search, so hiding the ports settles it for any plugin
	that does the same thing, rather than for that one. Nothing is lost from the picture: the
	panel already draws a dark hole and a coloured ring behind every jack, which is what a jack
	looks like.

	Knobs need none of this. Nothing intercepts them scene-wide, so the editor is offered their
	clicks first in the ordinary way. */
	void setEditing(bool on) {
		visible = on;
		if (on) {
			APP->event->setSelectedWidget(this);
		}
		else {
			// SAVED ON THE WAY OUT, if anything moved. Work that survives only until the
			// module is deleted is work you lose without being told, and this has already
			// happened once. "Forget my layout" is the way back, so nothing here is a trap.
			saveNow();
			selection.clear();
			if (APP->event->getSelectedWidget() == this)
				APP->event->setSelectedWidget(NULL);
		}
		for (Item& item : layout->items) {
			if ((item.kind == Item::PORT_IN || item.kind == Item::PORT_OUT) && item.widget)
				item.widget->visible = !on;
		}
	}

	/** Escape leaves the mode, which is what Escape means everywhere else. Whatever has been
	moved stays moved: this is leaving the mode, not undoing the work. Save it or not. */
	/** Deletes, or brings back, whichever labels are selected. Labels only: a jack or a knob
	still exists in the module whether it is drawn or not, so taking one off the panel would
	leave a control that cannot be reached and a patch that cannot be made. */
	/** Writes the layout if anything has changed since the last time.

	CALLED AT THE END OF EVERY GESTURE, not only when the mode is left. Saving on the way out
	assumed the mode is always left, and quitting Rack with it still on does not leave it — the
	scene is torn down and nothing is asked. A file of a few hundred bytes once per drag is
	nothing; losing an afternoon's arranging is not. */
	void saveNow() {
		if (!dirty)
			return;
		layoutSaveUser(slug, *layout);
		dirty = false;
	}

	void toggleDeleteSelected() {
		bool any = false;
		for (int i : selection) {
			Item& item = layout->items[i];
			if (item.kind != Item::LABEL)
				continue;
			item.hidden = !item.hidden;
			any = true;
		}
		if (any) {
			layoutRefreshPanel(panel, *layout);
			dirty = true;
			saveNow();
		}
	}

	/** True if the key was ours. Shared by both routes below rather than written twice. */
	bool handleKey(int action, int key) {
		if (action != GLFW_PRESS)
			return false;
		if (key == GLFW_KEY_ESCAPE) {
			// One Escape puts down what is held, the next leaves the mode. Leaving with a
			// selection still made would lose it silently, and a selection is work.
			if (!selection.empty())
				selection.clear();
			else
				setEditing(false);
			return true;
		}
		if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) {
			toggleDeleteSelected();
			return true;
		}
		return false;
	}

	/** THE ROUTE THAT ACTUALLY WINS. Rack deletes the module under the pointer on Delete, and
	whether a hovered child gets the key first depends on dispatch order I should not be
	relying on. Keys for the SELECTED widget are offered before hover keys and never reach the
	module, so the editor asks to be the selected widget while the mode is on. */
	void onSelectKey(const SelectKeyEvent& e) override {
		if (handleKey(e.action, e.key)) {
			e.consume(this);
			return;
		}
		widget::OpaqueWidget::onSelectKey(e);
	}

	void step() override {
		remakeDue();
		// Taken back after a menu or a text field has had it, so the keys keep working for the
		// whole session rather than until the first rename. Only when nothing else wants it.
		if (visible && !APP->event->getSelectedWidget())
			APP->event->setSelectedWidget(this);
		widget::OpaqueWidget::step();
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		// Kept as a second way in, for the case where something else holds the selection.
		if (handleKey(e.action, e.key)) {
			e.consume(this);
			return;
		}
		widget::OpaqueWidget::onHoverKey(e);
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			const math::Vec mm = toMM(e.pos);
			// AN EDGE FIRST. Nothing is placed within two and a half millimetres of one, so a
			// press there can only mean the edge, and a control that has been dragged there is
			// still reachable by its middle.
			const int edge = edgeAt(mm);
			if (edge != 0 && itemAt(mm) < 0) {
				resizing = edge;
				resizeHP = layout->hp;
				resizeFromScene = APP->scene->getMousePos().x;
				e.consume(this);
				return;
			}
			const bool add = (e.mods & GLFW_MOD_SHIFT) != 0;
			grabbed = itemAt(mm);
			marquee = false;
			if (grabbed >= 0) {
				if (add) {
					// Shift on something already chosen takes it out again, which is the only
					// way to correct a marquee that caught one thing too many.
					if (selected(grabbed)) {
						selection.erase(grabbed);
						grabbed = -1;
					}
					else {
						selection.insert(grabbed);
					}
				}
				else if (!selected(grabbed)) {
					// Pressing something outside the selection starts a new one. Pressing
					// something already in it keeps the group, so a group can be dragged
					// without shift being held the whole time.
					selection.clear();
					selection.insert(grabbed);
				}
				if (grabbed >= 0)
					grabOffset = math::Vec(layout->items[grabbed].x - mm.x,
						layout->items[grabbed].y - mm.y);
			}
			else {
				if (!add)
					selection.clear();
				marquee = true;
				marqueeFrom = marqueeTo = mm;
			}
			e.consume(this);
			return;
		}
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			const int i = itemAt(toMM(e.pos));
			if (i >= 0) {
				editItem(i);
				e.consume(this);
				return;
			}
			// NOT CONSUMED, on purpose. An opaque widget swallows right-clicks like any other,
			// which left the module's own menu unreachable while the mode was on — and the
			// mode is turned off from that menu. Passing it up means "Stop editing", "Save
			// layout" and everything else stay where they were.
			return;
		}
		widget::OpaqueWidget::onButton(e);
	}

	/** The pointer in this widget's own coordinates. Derived from two known points rather than
	from a zoom factor looked up somewhere: the rack sits inside a zoom widget, so a scene
	position has to be scaled to get back to panel units, and asking the widget where its own
	origin and its own unit vector land answers that whatever the view is doing. */
	math::Vec localMouse() {
		const math::Vec origin = getAbsoluteOffset(math::Vec(0.f, 0.f));
		const math::Vec unit = getAbsoluteOffset(math::Vec(1.f, 0.f));
		const float scale = unit.x - origin.x;
		if (scale <= 0.f)
			return math::Vec();
		return APP->scene->getMousePos().minus(origin).div(scale);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (resizing != 0) {
			// How far the pointer has travelled since it took hold, in HP: the right edge grows
			// with the pointer and the left edge grows against it.
			const math::Vec origin = getAbsoluteOffset(math::Vec(0.f, 0.f));
			const math::Vec unit = getAbsoluteOffset(math::Vec(1.f, 0.f));
			const float scale = unit.x - origin.x;
			if (scale <= 0.f)
				return;
			const float movedHP = ((APP->scene->getMousePos().x - resizeFromScene) / scale)
				/ RACK_GRID_WIDTH;
			resizeTo(resizeHP + (resizing > 0 ? movedHP : -movedHP));
			return;
		}
		if (marquee)
			marqueeTo = toMM(localMouse());
		else if (grabbed >= 0)
			moveSelection(toMM(localMouse()).plus(grabOffset));
		widget::OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (resizing != 0) {
			resizing = 0;
			saveNow();
			widget::OpaqueWidget::onDragEnd(e);
			return;
		}
		if (marquee) {
			selectInMarquee((APP->window->getMods() & GLFW_MOD_SHIFT) != 0);
			marquee = false;
		}
		grabbed = -1;
		guideX = guideY = -1.f;
		saveNow();
		widget::OpaqueWidget::onDragEnd(e);
	}

	void editItem(int index);

	/** Set when a property changed the WIDGET rather than where it sits — a knob's diameter, a
	lamp column's names or spacing. Acted on in step() rather than there and then, because
	deleting the widget whose menu is open frees the thing the click is still travelling
	through. Rack takes a scene change on the next frame quite happily; it does not survive one
	made underneath it. */
	std::set<int> remake;

	void remakeDue() {
		if (!remake.empty() && mw && mw->module) {
			for (int i : remake) {
				if (i < 0 || i >= (int) layout->items.size())
					continue;
				Item& item = layout->items[i];
				if (item.kind != Item::PARAM)
					continue;
				if (item.widget) {
					mw->removeChild(item.widget);
					delete item.widget;
					item.widget = NULL;
				}
				ParamWidget* p = makeParam(mw->module, item);
				mw->addParam(p);
				item.widget = p;
			}
			remake.clear();
			layout->resolve();
			for (Item& item : layout->items)
				placeWidget(item);
			layoutRefreshPanel(panel, *layout);
			saveNow();
		}
		widget::OpaqueWidget::step();
	}

	void draw(const DrawArgs& args) override {
		widget::OpaqueWidget::draw(args);

		// Every item outlined, so what can be moved is visible rather than discovered.
		//
		// THE VISIBLE SIZE, NOT THE GRABBABLE ONE. These were drawn from the hit rectangle,
		// which is deliberately bigger than the thing it belongs to — a knob with marks carries
		// six millimetres of air, a jack more than a millimetre — because a generous target is
		// easier to catch. Drawn, that padding reads as the item being that size, and lining a
		// control up against a box that is not its edge is guesswork.
		//
		// So the outline is the item and the target is still the target. Only the drawing
		// changes here; what answers a click is untouched.
		for (int i = 0; i < (int) layout->items.size(); i++) {
			const math::Rect r = itemVisual(layout->items[i]);
			const math::Vec p = mm2px(r.pos);
			const math::Vec s = mm2px(r.size);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, p.x, p.y, s.x, s.y);
			const bool sel = selected(i);
			if (sel) {
				nvgFillColor(args.vg, nvgRGBA(0x3d, 0xd6, 0x8c, 0x26));
				nvgFill(args.vg);
			}
			nvgStrokeColor(args.vg, sel
				? nvgRGBA(0x3d, 0xd6, 0x8c, 0xff) : nvgRGBA(0x3d, 0xd6, 0x8c, 0x50));
			nvgStrokeWidth(args.vg, sel ? 1.6f : 1.f);
			nvgStroke(args.vg);
		}

		// THE TWO EDGES, so that a panel that can be made wider looks like one. Brighter while
		// one of them is being pulled.
		for (int side = 0; side < 2; side++) {
			const float w = mm2px(math::Vec(EDGE_MM, 0)).x;
			const float x = side == 0 ? 0.f : box.size.x - w;
			const bool live = (resizing == (side == 0 ? -1 : 1));
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x, 0.f, w, box.size.y);
			nvgFillColor(args.vg, live ? nvgRGBA(0xff, 0x73, 0x00, 0x55)
				: nvgRGBA(0x3d, 0xd6, 0x8c, 0x1c));
			nvgFill(args.vg);
		}

		// The guides, drawn only while something is actually lined up with something else.
		nvgStrokeColor(args.vg, nvgRGBA(0xff, 0x73, 0x00, 0xd0));
		nvgStrokeWidth(args.vg, 1.f);
		if (guideX >= 0.f) {
			const float x = mm2px(math::Vec(guideX, 0)).x;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, x, 0);
			nvgLineTo(args.vg, x, box.size.y);
			nvgStroke(args.vg);
		}
		if (guideY >= 0.f) {
			const float y = mm2px(math::Vec(0, guideY)).y;
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 0, y);
			nvgLineTo(args.vg, box.size.x, y);
			nvgStroke(args.vg);
		}

		if (marquee) {
			const math::Vec a = mm2px(marqueeFrom);
			const math::Vec b = mm2px(marqueeTo);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, std::fmin(a.x, b.x), std::fmin(a.y, b.y),
				std::fabs(b.x - a.x), std::fabs(b.y - a.y));
			nvgFillColor(args.vg, nvgRGBA(0x3d, 0xd6, 0x8c, 0x1a));
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, nvgRGBA(0x3d, 0xd6, 0x8c, 0xc0));
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);
		}

		// The border, so a module in this mode cannot be mistaken for one that is not.
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 1.5f, 1.5f, box.size.x - 3.f, box.size.y - 3.f);
		nvgStrokeColor(args.vg, nvgRGB(0xff, 0x73, 0x00));
		nvgStrokeWidth(args.vg, 3.f);
		nvgStroke(args.vg);
	}
};


/** ONE VALUE, TYPED. A text field in a menu rather than a dialogue of its own: Rack already
knows how to put a menu where the pointer is and take it away again, and a properties dialogue
that has to be dragged out of the way is worse than one that appears under the hand.

What it does with the text is handed in, so the same field edits a name, a number of marks, a
spacing or a diameter. */
struct ValueField : ui::TextField {
	PanelEditor* editor = NULL;
	std::function<void(const std::string&)> apply;
	bool focused = false;

	/** ASKED FOR ON THE FIRST FRAME. A menu puts a widget on the screen; it does not hand it
	the keyboard, so a field added to one sits there looking ready and receives nothing. */
	void step() override {
		if (!focused) {
			APP->event->setSelectedWidget(this);
			focused = true;
		}
		ui::TextField::step();
	}

	void close() {
		ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
		if (overlay)
			overlay->requestDelete();
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		// Escape leaves the value as it was, which is what Escape means everywhere else here.
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
			close();
			e.consume(this);
			return;
		}
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
			if (apply)
				apply(text);
			editor->layout->resolve();
			for (Item& item : editor->layout->items)
				PanelEditor::placeWidget(item);
			layoutRefreshPanel(editor->panel, *editor->layout);
			editor->dirty = true;
			editor->saveNow();
			close();
			e.consume(this);
			return;
		}
		ui::TextField::onSelectKey(e);
	}
};

/** A property, shown with what it is set to now and opening onto a field to type a new one. */
static void addTyped(ui::Menu* menu, PanelEditor* editor, const std::string& name,
		const std::string& now, std::function<void(const std::string&)> apply) {
	menu->addChild(createSubmenuItem(name, now, [=](ui::Menu* sub) {
		sub->addChild(createMenuLabel("Enter to keep, Escape to leave"));
		ValueField* field = new ValueField;
		field->editor = editor;
		field->apply = apply;
		field->text = now;
		field->selectAll();
		field->box.size.x = 220.f;
		sub->addChild(field);
	}));
}

/** A property that is one of a short list. */
static void addChoice(ui::Menu* menu, PanelEditor* editor, const std::string& name,
		const std::vector<std::string>& options, int now, std::function<void(int)> apply) {
	menu->addChild(createSubmenuItem(name, now >= 0 && now < (int) options.size()
			? options[now] : "", [=](ui::Menu* sub) {
		for (int i = 0; i < (int) options.size(); i++) {
			sub->addChild(createCheckMenuItem(options[i], "", [=]() { return i == now; },
				[=]() {
					apply(i);
					editor->layout->resolve();
					for (Item& item : editor->layout->items)
						PanelEditor::placeWidget(item);
					layoutRefreshPanel(editor->panel, *editor->layout);
					editor->dirty = true;
					editor->saveNow();
				}));
		}
	}));
}

static std::string num(float v) {
	std::string s = string::f("%.2f", v);
	while (s.size() > 1 && s.back() == '0') s.pop_back();
	if (!s.empty() && s.back() == '.') s.pop_back();
	return s;
}

/** A LIST OF NAMES ON ONE LINE, separated by bars, each obeying the slash rule like any other
piece of text. Bars rather than commas because a name may well have a comma in it. */
static std::string joinWords(const std::vector<std::string>& v) {
	std::string out;
	for (size_t i = 0; i < v.size(); i++) {
		if (i)
			out += " | ";
		out += layoutTextToUser(v[i]);
	}
	return out;
}

static std::vector<std::string> splitWords(const std::string& s) {
	std::vector<std::string> out;
	size_t start = 0;
	while (true) {
		const size_t bar = s.find('|', start);
		std::string one = s.substr(start, bar == std::string::npos ? std::string::npos
			: bar - start);
		while (!one.empty() && one.front() == ' ') one.erase(one.begin());
		while (!one.empty() && one.back() == ' ') one.pop_back();
		if (!one.empty())
			out.push_back(layoutTextFromUser(one));
		if (bar == std::string::npos)
			break;
		start = bar + 1;
	}
	return out;
}

/** THE PROPERTIES OF ONE THING ON THE PANEL.

Right-clicking anything while the editor is on. What is offered depends on what was clicked, and
every property here is one that used to need the source recompiled: how big a knob is, what its
marks say, which side its name sits on and how far away, what the lamps of a column are called
and how far apart they sit.

Each one records that it was set, so that the file saves it and nothing else — a default the
module later improves still reaches a panel somebody has edited. */
void PanelEditor::editItem(int index) {
	Item& it = layout->items[index];
	PanelEditor* self = this;
	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel(it.key));

	auto touch = [self, index](const char* prop) {
		self->layout->items[index].userProps.insert(prop);
	};

	// ---- where it is ----
	if (it.owner.empty()) {
		addTyped(menu, this, "x", num(it.x), [self, index](const std::string& v) {
			self->layout->items[index].x = std::atof(v.c_str());
		});
		addTyped(menu, this, "y", num(it.y), [self, index](const std::string& v) {
			self->layout->items[index].y = std::atof(v.c_str());
		});
	}

	// ---- a knob ----
	if (it.kind == Item::PARAM && it.style != "lamps" && it.style.compare(0, 5, "knob") == 0) {
		// IN MILLIMETRES, AND ANY OF THEM. Rack ships five widths and the one a panel wants is
		// often between two of them, so the knob is drawn rather than loaded and this is a
		// number like every other measurement here.
		const float dia = it.diameter > 0.f ? it.diameter : knobWidthMM(it.style);
		addTyped(menu, this, "diameter", num(dia), [self, index, touch](const std::string& v) {
			self->layout->items[index].diameter = (float) std::atof(v.c_str());
			touch("diameter");
			// The widget itself has to be made again at the new size, which is a rebuild.
			self->remake.insert(index);
		});
		addTyped(menu, this, "marks", string::f("%d", it.ticks),
			[self, index, touch](const std::string& v) {
				self->layout->items[index].ticks = std::atoi(v.c_str());
				touch("ticks");
			});
		addTyped(menu, this, "numbers", joinWords(it.tickMarks),
			[self, index, touch](const std::string& v) {
				self->layout->items[index].tickMarks = splitWords(v);
				touch("marks");
			});
		addTyped(menu, this, "number size", num(it.nameSize > 0.f ? it.nameSize : 5.f),
			[self, index, touch](const std::string& v) {
				self->layout->items[index].nameSize = (float) std::atof(v.c_str());
				touch("nameSize");
			});
	}

	// ---- a readout ----
	if (it.kind == Item::PARAM && it.style == "readout") {
		addTyped(menu, this, "digits wide (0 works it out)", string::f("%d", it.chars),
			[self, index, touch](const std::string& v) {
				self->layout->items[index].chars = std::atoi(v.c_str());
				touch("chars");
				self->remake.insert(index);
			});
		addTyped(menu, this, "digit size", num(it.h > 0.f ? it.h : 2.8f),
			[self, index, touch](const std::string& v) {
				self->layout->items[index].h = (float) std::atof(v.c_str());
				touch("height");
				self->remake.insert(index);
			});
	}

	// ---- a column of lamps ----
	if (it.kind == Item::PARAM && it.style == "lamps") {
		addTyped(menu, this, "spacing", num(it.pitch), [self, index, touch](const std::string& v) {
			self->layout->items[index].pitch = std::atof(v.c_str());
			touch("pitch");
			self->remake.insert(index);
		});
		addTyped(menu, this, "names", joinWords(it.names),
			[self, index, touch](const std::string& v) {
				self->layout->items[index].names = splitWords(v);
				touch("names");
				self->remake.insert(index);
			});
		addChoice(menu, this, "names on the", {"left", "right"},
			it.labelSide == Panel::RIGHT ? 1 : 0, [self, index, touch](int i) {
				self->layout->items[index].labelSide = i ? Panel::RIGHT : Panel::LEFT;
				touch("side");
				self->remake.insert(index);
			});
		addChoice(menu, this, "runs", {"down", "across"}, it.horizontal ? 1 : 0,
			[self, index, touch](int i) {
				self->layout->items[index].horizontal = (i == 1);
				touch("horizontal");
				self->remake.insert(index);
			});
		addTyped(menu, this, "name size", num(it.nameSize > 0.f ? it.nameSize : 8.f),
			[self, index, touch](const std::string& v) {
				self->layout->items[index].nameSize = (float) std::atof(v.c_str());
				touch("nameSize");
				self->remake.insert(index);
			});
	}

	// ---- a piece of text ----
	if (it.kind == Item::LABEL) {
		addTyped(menu, this, "text", layoutTextToUser(it.text),
			[self, index](const std::string& v) {
				self->layout->items[index].text = layoutTextFromUser(v);
			});
		const float size = it.size > 0.f ? it.size : (it.heading ? 10.f : 8.f);
		addTyped(menu, this, "size", num(size), [self, index, touch](const std::string& v) {
			self->layout->items[index].size = std::atof(v.c_str());
			touch("size");
		});
		addChoice(menu, this, "set", {"centred", "from the left", "from the right"},
			it.align == Panel::LEFT ? 1 : it.align == Panel::RIGHT ? 2 : 0,
			[self, index, touch](int i) {
				self->layout->items[index].align = i == 1 ? Panel::LEFT
					: i == 2 ? Panel::RIGHT : Panel::CENTRE;
				touch("align");
			});
	}

	// ---- the name this control carries ----
	int nameIndex = -1;
	for (int i = 0; i < (int) layout->items.size(); i++) {
		if (layout->items[i].kind == Item::LABEL && layout->items[i].owner == it.key)
			nameIndex = i;
	}
	if (nameIndex >= 0 && it.kind != Item::LABEL) {
		menu->addChild(new ui::MenuSeparator);
		const Item& lab = layout->items[nameIndex];
		menu->addChild(createMenuLabel("The name printed beside it"));
		addTyped(menu, this, "text", layoutTextToUser(lab.text),
			[self, nameIndex](const std::string& v) {
				self->layout->items[nameIndex].text = layoutTextFromUser(v);
			});
		const float lsize = lab.size > 0.f ? lab.size : (lab.heading ? 10.f : 8.f);
		addTyped(menu, this, "size", num(lsize), [self, nameIndex](const std::string& v) {
			self->layout->items[nameIndex].size = (float) std::atof(v.c_str());
			self->layout->items[nameIndex].userProps.insert("size");
		});
		const int place = placeOf(it, lab);
		const float gap = gapOf(it, lab);
		addChoice(menu, this, "sits", {PLACE_NAMES[0], PLACE_NAMES[1], PLACE_NAMES[2],
			PLACE_NAMES[3]}, place, [self, index, nameIndex, gap](int i) {
				placeLabel(self->layout->items[index], self->layout->items[nameIndex], i, gap);
			});
		addTyped(menu, this, "distance", num(gap),
			[self, index, nameIndex, place](const std::string& v) {
				placeLabel(self->layout->items[index], self->layout->items[nameIndex], place,
					(float) std::atof(v.c_str()));
			});
	}

	// ---- and the one thing that is not a property ----
	if (it.kind == Item::LABEL) {
		menu->addChild(new ui::MenuSeparator);
		const bool hidden = it.hidden;
		menu->addChild(createMenuItem(hidden ? "Bring this label back" : "Delete this label", "",
			[self, index]() {
				self->layout->items[index].hidden = !self->layout->items[index].hidden;
				layoutRefreshPanel(self->panel, *self->layout);
				self->dirty = true;
				self->saveNow();
			}));
	}

}


// ---- the menu ---------------------------------------------------------------------------------

static PanelEditor* editorOf(ModuleWidget* mw) {
	for (widget::Widget* child : mw->children) {
		PanelEditor* editor = dynamic_cast<PanelEditor*>(child);
		if (editor)
			return editor;
	}
	return NULL;
}

void layoutAppendMenu(ui::Menu* menu, ModuleWidget* mw, Panel* panel, Layout* layout,
		const std::string& slug) {
	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createMenuLabel("Panel layout"));

	PanelEditor* editor = editorOf(mw);
	const bool editing = editor && editor->visible;

	menu->addChild(createMenuItem(editing ? "Stop editing" : "Edit panel", "",
		[mw, panel, layout, slug]() {
			PanelEditor* editor = editorOf(mw);
			if (!editor) {
				// Added last so it is drawn over the controls and offered their events first.
				editor = new PanelEditor;
				editor->mw = mw;
				editor->panel = panel;
				editor->layout = layout;
				editor->slug = slug;
				editor->box.size = mw->box.size;
				mw->addChild(editor);
				// Created hidden, then turned on below: a new widget is visible by default,
				// and the first choice of "Edit panel" would otherwise have turned it off.
				editor->visible = false;
			}
			editor->setEditing(!editor->visible);
		}));

	if (editing) {
		menu->addChild(createMenuItem("Save layout", "", [mw, layout, slug]() {
			layoutSaveUser(slug, *layout);
			PanelEditor* editor = editorOf(mw);
			if (editor)
				editor->dirty = false;
		}));
	}

	// Deleted labels cannot be pointed at, so this is the only way back to one. Shown only
	// when there is something to bring back, and it says how many so the item is not a
	// question about whether anything happened.
	int deleted = 0;
	for (const Item& item : layout->items) {
		if (item.kind == Item::LABEL && item.hidden)
			deleted++;
	}
	if (deleted > 0) {
		menu->addChild(createMenuItem(
			string::f("Bring back %d deleted label%s", deleted, deleted == 1 ? "" : "s"), "",
			[panel, layout, slug]() {
				for (Item& item : layout->items)
					item.hidden = false;
				layoutRefreshPanel(panel, *layout);
				layoutSaveUser(slug, *layout);
			}));
	}

	// A LAYOUT SAVED BEFORE THE WORDING RULE carries a text for every label, including ones
	// nobody typed, and those override whatever the module now calls them. Nothing in the file
	// can tell the two apart, so this is the way back.
	bool renamed = false;
	for (const Item& item : layout->items) {
		if (item.kind == Item::LABEL && item.text != item.defaultText)
			renamed = true;
	}
	if (renamed) {
		menu->addChild(createMenuItem("Restore the built-in wording", "",
			[panel, layout, slug]() {
				for (Item& item : layout->items)
					item.text = item.defaultText;
				layoutRefreshPanel(panel, *layout);
				layoutSaveUser(slug, *layout);
			}));
	}

	if (layoutHasUser(slug)) {
		menu->addChild(createMenuItem("Forget my layout", "reopen the patch to see it", [slug]() {
			// Takes effect when the module is next created, because rebuilding a module's
			// children under a patch that is running is a good way to lose a cable.
			layoutResetUser(slug);
		}));
		menu->addChild(createMenuLabel(layoutUserPath(slug)));
	}
}


} // namespace px
