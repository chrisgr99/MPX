#include "Layout.hpp"

#include <cmath>
#include <cstdio>

namespace px {


Item* Layout::find(const std::string& key) {
	for (Item& item : items) {
		if (item.key == key)
			return &item;
	}
	return NULL;
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
			json_t* xJ = json_object_get(valueJ, "x");
			json_t* yJ = json_object_get(valueJ, "y");
			if (json_is_number(xJ))
				item->x = json_number_value(xJ);
			if (json_is_number(yJ))
				item->y = json_number_value(yJ);
			json_t* textJ = json_object_get(valueJ, "text");
			if (item->kind == Item::LABEL && json_is_string(textJ))
				item->text = json_string_value(textJ);
		}
	}
	json_decref(rootJ);
}

void layoutSaveUser(const std::string& slug, const Layout& layout) {
	json_t* itemsJ = json_object();
	for (const Item& item : layout.items) {
		json_t* itemJ = json_object();
		json_object_set_new(itemJ, "x", json_real(item.x));
		json_object_set_new(itemJ, "y", json_real(item.y));
		// Only labels carry text, and only their text is worth saving: everything else about
		// an item is what the module says it is.
		if (item.kind == Item::LABEL)
			json_object_set_new(itemJ, "text", json_string(item.text.c_str()));
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
	int grabbed = -1;
	/** Where in the item the pointer took hold, so it does not jump to the centre. */
	math::Vec grabOffset;
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

	void moveTo(int index, math::Vec posMM) {
		Item& item = layout->items[index];
		float x = posMM.x, y = posMM.y;
		guideX = guideY = -1.f;
		if (!(APP->window->getMods() & GLFW_MOD_SHIFT)) {
			// Lining up with something else beats lining up with the grid: a row of jacks
			// should agree with each other rather than each agree with a ruler.
			for (int i = 0; i < (int) layout->items.size(); i++) {
				if (i == index)
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
		item.x = x;
		item.y = y;
		if (item.widget) {
			const math::Vec pos = mm2px(math::Vec(x, y));
			if (item.style == "lamps")
				item.widget->box.pos = pos;
			else
				item.widget->box.pos = pos.minus(item.widget->box.size.div(2.f));
		}
		layoutRefreshPanel(panel, *layout);
		dirty = true;
	}

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			const math::Vec mm = toMM(e.pos);
			grabbed = itemAt(mm);
			if (grabbed >= 0)
				grabOffset = math::Vec(layout->items[grabbed].x - mm.x,
					layout->items[grabbed].y - mm.y);
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
			// Anything else falls through to the module's own menu, so the mode can be turned
			// off from where it was turned on.
			widget::OpaqueWidget::onButton(e);
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
		if (grabbed >= 0)
			moveTo(grabbed, toMM(localMouse()).plus(grabOffset));
		widget::OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		grabbed = -1;
		guideX = guideY = -1.f;
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
			nvgStrokeColor(args.vg, i == grabbed
				? nvgRGBA(0x3d, 0xd6, 0x8c, 0xff) : nvgRGBA(0x3d, 0xd6, 0x8c, 0x50));
			nvgStrokeWidth(args.vg, 1.f);
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

	void onSelectKey(const SelectKeyEvent& e) override {
		if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
			editor->layout->items[index].text = text;
			layoutRefreshPanel(editor->panel, *editor->layout);
			editor->dirty = true;
			ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>();
			if (overlay)
				overlay->requestDelete();
			e.consume(this);
			return;
		}
		ui::TextField::onSelectKey(e);
	}
};

void PanelEditor::editText(int index) {
	ui::Menu* menu = createMenu();
	menu->addChild(createMenuLabel("Label text"));
	LabelField* field = new LabelField;
	field->editor = this;
	field->index = index;
	field->text = layout->items[index].text;
	field->selectAll();
	field->box.size.x = 180.f;
	menu->addChild(field);
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
			}
			editor->visible = !editor->visible;
		}));

	if (editing) {
		menu->addChild(createMenuItem("Save layout", "", [mw, layout, slug]() {
			layoutSaveUser(slug, *layout);
			PanelEditor* editor = editorOf(mw);
			if (editor)
				editor->dirty = false;
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
