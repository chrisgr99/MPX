/** mpxPathSprites' panel. Included by mpxPathSprites.cpp, which is where the module is.

SEPARATE FROM THE MODULE because the picture, the drawing and the dragging are most of the file
and none of it is what the module does. The module runs heads along paths and puts out voltages;
this is how a path gets drawn and how a picture gets chosen.
*/

namespace px {


/** THE MEASUREMENTS. The picture is a square as tall as the panel, as it is in mpxSprites, and
the width is then chosen for the column beside it — the same 42 HP mpxSprites uses, which leaves
an 85 millimetre column. Narrower crowded the tabs: eight of them across 54 millimetres left each
one six millimetres wide, which is a lamp and a digit with nothing between them. */
static const float PS_PANEL_H = 128.5f;
static const float PS_PANEL_HP = 42.f;
static const float PS_PANEL_W = PS_PANEL_HP * 5.08f;
static const float PS_INSET = 1.5f;
static const float PS_SQUARE = PS_PANEL_H - 2.f * PS_INSET;
static const float PS_COLUMN = PS_PANEL_W - PS_SQUARE - 2.f * PS_INSET;


static Layout pathSpritesLayout() {
	Layout L;
	L.hp = PS_PANEL_HP;
	L.title = "mpxPathSprites";
	// THE NAME GOES OVER THE CONTROLS, not across the top of the panel: the picture takes the
	// whole height and there is no strip along the top to write in.
	L.titleWidth = PS_COLUMN;

	const float left = PS_INSET;
	const float colMid = PS_COLUMN / 2.f;

	auto label = [&](const std::string& key, float x, float y, const std::string& text,
			float size = 0.f, const std::string& owner = "",
			Panel::Align align = Panel::CENTRE) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.align = align; i.heading = true; i.size = size; i.owner = owner;
		L.items.push_back(i);
	};

	auto knob = [&](const std::string& key, float x, float y, int id, const std::string& name,
			float size = 0.f) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.ticks = 2;
		L.items.push_back(i);
		label(key + ".label", x, y + 7.6f, name, size, key);
	};

	auto plate = [&](const std::string& key, float x, float y, int id, int chars,
			const std::string& name, const std::vector<std::string>& notes = {}) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "readout";
		i.x = x; i.y = y; i.chars = chars; i.h = 3.0f; i.notes = notes;
		L.items.push_back(i);
		label(key + ".label", x, y + 5.0f, name, 0.f, key);
	};

	auto lamps = [&](const std::string& key, float x, float y, int id,
			const std::vector<std::string>& names, const std::string& group) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "lamps";
		i.x = x; i.y = y; i.names = names; i.horizontal = true; i.pitch = 24.f;
		i.labelSide = Panel::RIGHT;
		L.items.push_back(i);
		label(key + ".group", x + 10.f, y - 4.2f, group, 0.f, key);
	};

	auto display = [&](const std::string& key, float x, float y, float w, float h) {
		Item i;
		i.key = key; i.kind = Item::DISPLAY; i.x = x; i.y = y; i.w = w; i.h = h;
		L.items.push_back(i);
	};

	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name, NVGcolor ring) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = ring;
		L.items.push_back(i);
		label(key + ".label", x, y - 5.6f, name, 6.4f, key);
	};

	// THE PICTURE, the whole height of the panel on the right.
	display("d.picture", PS_COLUMN + PS_INSET, PS_INSET, PS_SQUARE, PS_SQUARE);

	// THE TABS at the top of the column.
	display("d.tabs", left, 14.f, PS_COLUMN - 2.f * PS_INSET, 6.f);

	lamps("p.kind", left + 3.f, 26.f, PathSpritesModule::P_KIND, {"LINE", "CIRCLE"}, "PATH");
	knob("p.diameter", colMid, 40.f, PathSpritesModule::P_DIAMETER, "DIAMETER");

	plate("p.source", colMid, 56.f, PathSpritesModule::P_SOURCE, 10, "READS",
		std::vector<std::string>(CV_SOURCE_NOTES, CV_SOURCE_NOTES + NUM_CV_SOURCES));

	knob("p.rate", colMid - 11.f, 72.f, PathSpritesModule::P_RATE, "RATE");
	knob("p.smooth", colMid + 11.f, 72.f, PathSpritesModule::P_SMOOTH, "SMOOTH");
	lamps("p.rateFrom", left + 3.f, 84.f, PathSpritesModule::P_RATE_FROM,
		{"FREE", "THE BEAT"}, "RATE FROM");

	knob("p.level", colMid - 11.f, 98.f, PathSpritesModule::P_LEVEL, "LEVEL");
	knob("p.offset", colMid + 11.f, 98.f, PathSpritesModule::P_OFFSET, "OFFSET");

	// THE OUTPUTS: one a sprite, in two rows of four, and the polyphonic one beside the input.
	for (int i = 0; i < MAX_PATHS; i++) {
		const float x = left + 8.f + (float) (i % 4) * 13.f;
		const float y = 112.f + (float) (i / 4) * 9.5f;
		Item o;
		o.key = string::f("out.cv%d", i + 1);
		o.kind = Item::PORT_OUT; o.id = PathSpritesModule::O_CV + i; o.x = x; o.y = y;
		o.ring = PATH_INK[i];
		L.items.push_back(o);
	}
	label("l.outs", left + 8.f, 105.f, "OUT", 7.f, "", Panel::LEFT);

	jack("in.chart", Item::PORT_IN, PS_COLUMN - 9.f, 112.f, PathSpritesModule::I_CHART,
		"mpx", NOTE_CABLE);
	jack("out.poly", Item::PORT_OUT, PS_COLUMN - 9.f, 121.f, PathSpritesModule::O_POLY,
		"ALL", nvgRGB(0x9a, 0xa4, 0xb2));

	Item run;
	run.key = "p.run"; run.kind = Item::PARAM; run.id = PathSpritesModule::P_RUN;
	run.style = "latch"; run.diameter = 5.5f; run.x = left + 8.f; run.y = 98.f;
	L.items.push_back(run);
	label("p.run.label", left + 8.f, 104.f, "RUN", 7.f, "p.run");

	L.bindOffsets();
	return L;
}


struct PathSpritesWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	PathSpritesWidget(PathSpritesModule* module) {
		setModule(module);
		layout = pathSpritesLayout();
		layoutApplyUser("mpxPathSprites", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);

		PictureField* field = new PictureField;
		field->module = module;
		layoutPlaceDisplay(this, layout, "d.picture", field);

		PathTabs* tabs = new PathTabs;
		tabs->module = module;
		layoutPlaceDisplay(this, layout, "d.tabs", tabs);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxPathSprites");
	}

	void step() override {
		ModuleWidget::step();
		PathSpritesModule* m = dynamic_cast<PathSpritesModule*>(module);
		if (!m)
			return;
		// THE TABLES ARE BUILT HERE, on the main thread: building one walks a thousand pixels of
		// a picture, which is not a thing to do between two samples.
		m->tick();

		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(PathSpritesModule::I_CHART)) {
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
	}
};


} // namespace px
