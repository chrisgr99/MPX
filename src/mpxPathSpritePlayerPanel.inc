/** mpxPathSpritePlayer's panel. The picture and the tabs come from PathSpritesUI.inc; this is the
module's own arrangement and nothing else. */

namespace px {


static const float PL_PANEL_H = 128.5f;
static const float PL_PANEL_HP = 42.f;
static const float PL_PANEL_W = PL_PANEL_HP * 5.08f;
static const float PL_INSET = 1.5f;
static const float PL_SQUARE = PL_PANEL_H - 2.f * PL_INSET;
static const float PL_COLUMN = PL_PANEL_W - PL_SQUARE - 2.f * PL_INSET;


static Layout playerLayout() {
	Layout L;
	L.hp = PL_PANEL_HP;
	L.title = "mpxPathSpritePlayer";
	L.titleWidth = PL_COLUMN;

	const float left = PL_INSET;
	const float mid = PL_COLUMN / 2.f;

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
			const std::string& name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "readout";
		i.x = x; i.y = y; i.chars = chars; i.h = 3.0f;
		L.items.push_back(i);
		label(key + ".label", x, y + 5.0f, name, 0.f, key);
	};

	auto display = [&](const std::string& key, float x, float y, float w, float h) {
		Item i;
		i.key = key; i.kind = Item::DISPLAY; i.x = x; i.y = y; i.w = w; i.h = h;
		L.items.push_back(i);
	};

	auto jack = [&](const std::string& key, Item::Kind kind, float x, float y, int id,
			const std::string& name) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = NOTE_CABLE;
		L.items.push_back(i);
		label(key + ".label", x, y - 6.2f, name, 7.f, key);
	};

	display("d.picture", PL_COLUMN + PL_INSET, PL_INSET, PL_SQUARE, PL_SQUARE);
	display("d.tabs", left, 14.f, PL_COLUMN - 2.f * PL_INSET, 6.f);

	{
		Item i;
		i.key = "p.kind"; i.kind = Item::PARAM; i.id = PlayerModule::P_KIND; i.style = "lamps";
		i.x = left + 3.f; i.y = 26.f; i.names = {"LINE", "CIRCLE"}; i.horizontal = true;
		i.pitch = 24.f; i.labelSide = Panel::RIGHT;
		L.items.push_back(i);
		label("p.kind.group", left + 13.f, 21.8f, "PATH", 0.f, "p.kind");
	}
	knob("p.diameter", mid, 38.f, PlayerModule::P_DIAMETER, "DIAMETER");

	plate("p.source", mid, 53.f, PlayerModule::P_SOURCE, 10, "READS");

	// HOW MUCH OF THE PICTURE IS WORTH PLAYING. How fast the path is walked is not here: it is
	// the pattern's own length, which the cable carries.
	knob("p.drop", mid, 68.f, PlayerModule::P_DROP, "DROP");

	// AND THE THREE THAT DECIDE WHAT THE NOTE IS.
	knob("p.register", mid - 20.f, 90.f, PlayerModule::P_REGISTER, "REGISTER");
	knob("p.range", mid, 90.f, PlayerModule::P_RANGE, "RANGE");
	knob("p.length", mid + 20.f, 90.f, PlayerModule::P_LENGTH, "LENGTH");

	jack("in.chart", Item::PORT_IN, left + 10.f, 118.f, PlayerModule::I_CHART, "mpx\nIN");
	jack("out.notes", Item::PORT_OUT, PL_COLUMN - 10.f, 118.f, PlayerModule::O_NOTES, "mpx\nOUT");

	L.bindOffsets();
	return L;
}


struct PlayerWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;

	PlayerWidget(PlayerModule* module) {
		setModule(module);
		layout = playerLayout();
		layoutApplyUser("mpxPathSpritePlayer", layout);
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
		layoutAppendMenu(menu, this, panel, &layout, "mpxPathSpritePlayer");
	}

	void step() override {
		ModuleWidget::step();
		PlayerModule* m = dynamic_cast<PlayerModule*>(module);
		if (!m)
			return;
		// THE TABLES ARE BUILT HERE, on the main thread: building one walks a thousand pixels.
		m->tick();

		int slots[MAX_UPSTREAM];
		uint32_t generations[MAX_UPSTREAM];
		int n = 0;
		if (PortWidget* port = getInput(PlayerModule::I_CHART)) {
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
