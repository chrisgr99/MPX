#pragma once
/** Panels as data, and an editor for them.

WHY. A panel's positions were C++, so improving one meant editing code, rebuilding and
restarting — three steps between noticing that a knob is in the wrong place and seeing it in the
right one. DreamRack solved this years ago: the layout is data and the panel is built from it.
This is the same move.

WHAT SHIPS. Nothing extra. The layout every module comes with is still written in code, because
a default that lives in a file could fall out of step with the module it describes. What the
file adds is an OVERRIDE: positions the user has moved, saved beside Rack's own settings and
laid over the built-in layout when the module is built.

MERGED BY KEY, NOT SUBSTITUTED. Every item has a name that never changes, and the saved file
supplies positions for the names it knows. A control added to the module later has no entry, so
it appears where the code puts it rather than vanishing because an older file did not mention
it. Replacing the layout wholesale would have made every saved file a time bomb.
*/
#include "plugin.hpp"

#include <string>
#include <vector>

namespace px {

struct Item {
	enum Kind {
		PARAM,
		PORT_IN,
		PORT_OUT,
		LIGHT,
		LABEL,
	};

	/** The name this item is saved under. Stable for the life of the module: rename one and
	every layout anybody has saved forgets where that control went. */
	std::string key;
	Kind kind = LABEL;
	/** The param, port or light id. */
	int id = -1;
	/** Millimetres, and the CENTRE of the thing rather than its corner — which is what every
	position in a Rack panel means, and what a person means when they say where a knob is. */
	float x = 0.f, y = 0.f;

	/** PARAM: which control to make. knob, knob.large, knob.huge, lamps. */
	std::string style = "knob";

	/** PARAM, style lamps. */
	std::vector<std::string> names;
	bool horizontal = false;
	float pitch = 8.f;
	Panel::Align labelSide = Panel::LEFT;
	float w = 6.f, h = 30.f;

	/** PORT: the signal-family ring drawn behind it. Fully transparent means none. */
	NVGcolor ring = nvgRGBA(0, 0, 0, 0);

	/** LABEL. */
	std::string text;
	Panel::Align align = Panel::CENTRE;
	bool heading = false;
	float size = 0.f;

	/** Filled in when the layout is built, so the editor can move the real thing and not only
	its entry. Labels have none: they are drawn by the panel. */
	widget::Widget* widget = NULL;
};

struct Layout {
	float hp = 20.f;
	std::string title, titleAbove;
	std::vector<Item> items;

	Item* find(const std::string& key);
};

/** Lays the user's saved positions over the built-in ones. */
void layoutApplyUser(const std::string& slug, Layout& layout);
void layoutSaveUser(const std::string& slug, const Layout& layout);
void layoutResetUser(const std::string& slug);
std::string layoutUserPath(const std::string& slug);
bool layoutHasUser(const std::string& slug);

/** Creates every param, port and light, and fills the panel's labels and rings. */
void layoutBuild(ModuleWidget* mw, Panel* panel, Layout& layout);
/** Regenerates only what the panel draws. Called after anything moves. */
void layoutRefreshPanel(Panel* panel, Layout& layout);

/** The right-click entries: edit, save, reload, reset. */
void layoutAppendMenu(ui::Menu* menu, ModuleWidget* mw, Panel* panel, Layout* layout,
	const std::string& slug);

} // namespace px
