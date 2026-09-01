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

std::string layoutUserPath(const std::string& slug) {
	return asset::user("DreamerMPX/layout/" + slug + ".json");
}

bool layoutHasUser(const std::string& slug) {
	return system::isFile(layoutUserPath(slug));
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
					item->text = json_string_value(textJ);
				json_t* hiddenJ = json_object_get(valueJ, "hidden");
				if (json_is_boolean(hiddenJ))
					item->hidden = json_boolean_value(hiddenJ);
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
			json_object_set_new(itemJ, "text", json_string(item.text.c_str()));
			if (item.hidden)
				json_object_set_new(itemJ, "hidden", json_true());
		}
		json_object_set_new(itemsJ, item.key.c_str(), itemJ);
	}
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "items", itemsJ);

	system::createDirectories(asset::user("DreamerMPX/layout"));
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

void layoutRefreshPanel(Panel* panel, Layout& layout) {
	panel->labels.clear();
	panel->rings.clear();
	for (const Item& item : layout.items) {
		if (item.kind == Item::LABEL) {
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
		else if ((item.kind == Item::PORT_IN || item.kind == Item::PORT_OUT) && item.ring.a > 0.f) {
			Panel::Ring r;
			r.x = mm2px(math::Vec(item.x, 0)).x;
			r.y = mm2px(math::Vec(0, item.y)).y;
			r.color = item.ring;
			panel->rings.push_back(r);
		}
	}
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
				ParamWidget* p = NULL;
				if (item.style == "lamps") {
					Lamps* lamps = createParam<Lamps>(pos, module, item.id);
					lamps->box.size = mm2px(math::Vec(item.w, item.h));
					lamps->pitch = item.horizontal
						? mm2px(math::Vec(item.pitch, 0)).x : mm2px(math::Vec(0, item.pitch)).y;
					lamps->names = item.names;
					lamps->horizontal = item.horizontal;
					lamps->labelSide = item.labelSide;
					p = lamps;
					// A lamp list is placed by its CORNER, because it is a list rather than a
					// point; everything else on a panel is placed by its centre.
					lamps->box.pos = pos;
				}
				else if (item.style == "knob.huge")
					p = createParamCentered<RoundHugeBlackKnob>(pos, module, item.id);
				else if (item.style == "knob.large")
					p = createParamCentered<RoundLargeBlackKnob>(pos, module, item.id);
				else
					p = createParamCentered<RoundBlackKnob>(pos, module, item.id);
				mw->addParam(p);
				item.widget = p;
			} break;
			case Item::PORT_IN: {
				PortWidget* p = createInputCentered<PJ301MPort>(pos, module, item.id);
				mw->addInput(p);
				item.widget = p;
			} break;
			case Item::PORT_OUT: {
				PortWidget* p = createOutputCentered<PJ301MPort>(pos, module, item.id);
				mw->addOutput(p);
				item.widget = p;
			} break;
			case Item::LIGHT: {
				widget::Widget* l = createLightCentered<SmallLight<GreenLight>>(pos, module, item.id);
				mw->addChild(l);
				item.widget = l;
			} break;
			case Item::LABEL:
				break;
		}
	}
	layoutRefreshPanel(panel, layout);
}


// ---- the editor -------------------------------------------------------------------------------

/** What a drag snaps to. Half a millimetre is fine enough that nothing feels stuck and coarse
enough that two things meant to line up actually do. */
static const float GRID_MM = 0.5f;
/** How close two items have to be on an axis before a guide says they are aligned. */
static const float GUIDE_MM = 0.35f;

/** How big each kind of thing is, for hit-testing. Labels are measured from their text, the
rest are the size of the graphic Rack draws. */
static math::Rect itemRect(const Item& item) {
	float w = 8.f, h = 8.f;
	switch (item.kind) {
		case Item::PARAM:
			if (item.style == "knob.huge") w = h = 20.f;
			else if (item.style == "knob.large") w = h = 14.f;
			else if (item.style == "lamps") {
				w = item.horizontal ? item.pitch * (float) item.names.size() : 10.f;
				h = item.horizontal ? 7.f : item.pitch * (float) item.names.size();
				// Placed by its corner, so its rect starts there rather than straddling it.
				return math::Rect(math::Vec(item.x, item.y), math::Vec(w, h));
			}
			else w = h = 10.f;
			break;
		case Item::PORT_IN:
		case Item::PORT_OUT:
			w = h = 9.2f;
			break;
		case Item::LIGHT:
			w = h = 5.f;
			break;
		case Item::LABEL: {
			const float size = item.size > 0.f ? item.size : (item.heading ? 10.f : 8.f);
			// Estimated rather than measured: a text width needs a font and a context, and a
			// hit target that is a little generous costs nothing.
			w = std::fmax(6.f, item.text.size() * size * 0.145f);
			h = 4.5f;
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

	/** Puts a widget where its item says it is. Lamp lists are placed by their corner because
	they are a list rather than a point; everything else is placed by its centre. */
	static void placeWidget(Item& item) {
		if (!item.widget)
			return;
		const math::Vec pos = mm2px(math::Vec(item.x, item.y));
		if (item.style == "lamps")
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
			if (i >= 0 && layout->items[i].kind == Item::LABEL) {
				editText(i);
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
		if (marquee)
			marqueeTo = toMM(localMouse());
		else if (grabbed >= 0)
			moveSelection(toMM(localMouse()).plus(grabOffset));
		widget::OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (marquee) {
			selectInMarquee((APP->window->getMods() & GLFW_MOD_SHIFT) != 0);
			marquee = false;
		}
		grabbed = -1;
		guideX = guideY = -1.f;
		saveNow();
		widget::OpaqueWidget::onDragEnd(e);
	}

	void editText(int index);

	void draw(const DrawArgs& args) override {
		widget::OpaqueWidget::draw(args);

		// Every item outlined, so what can be moved is visible rather than discovered.
		for (int i = 0; i < (int) layout->items.size(); i++) {
			const math::Rect r = itemRect(layout->items[i]);
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


/** Retyping a label. A text field in a menu rather than a dialogue of its own: it is one value,
and Rack already knows how to put a menu where the pointer is and take it away again. */
struct LabelField : ui::TextField {
	PanelEditor* editor = NULL;
	int index = -1;
	bool focused = false;

	/** ASKED FOR ON THE FIRST FRAME. A menu puts a widget on the screen; it does not hand it
	the keyboard, so a field added to one sits there looking ready and receives nothing. This
	is what was making label editing appear not to work at all. */
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
		// Escape leaves the text as it was, which is what Escape means everywhere else here.
		if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
			close();
			e.consume(this);
			return;
		}
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
			editor->layout->items[index].text = text;
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

void PanelEditor::editText(int index) {
	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel("Label text \u2014 Enter to keep, Escape to leave"));
	LabelField* field = new LabelField;
	field->editor = this;
	field->index = index;
	field->text = layout->items[index].text;
	field->selectAll();
	field->box.size.x = 200.f;
	menu->addChild(field);

	menu->addChild(new ui::MenuSeparator);
	const bool hidden = layout->items[index].hidden;
	PanelEditor* self = this;
	menu->addChild(createMenuItem(hidden ? "Bring this label back" : "Delete this label", "",
		[self, index]() {
			self->layout->items[index].hidden = !self->layout->items[index].hidden;
			layoutRefreshPanel(self->panel, *self->layout);
			self->dirty = true;
			self->saveNow();
		}));
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
		menu->addChild(createMenuItem("Forget my layout", "", [slug]() {
			// Takes effect when the module is next created, because rebuilding a module's
			// children under a patch that is running is a good way to lose a cable.
			layoutResetUser(slug);
		}));
		menu->addChild(createMenuLabel(layoutUserPath(slug)));
	}
}


} // namespace px
