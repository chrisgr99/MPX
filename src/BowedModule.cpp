/** See BowedModule.hpp. */
#include "BowedModule.hpp"

#include <thread>

namespace px {


/** The level lamp's thresholds and how long each colour is held. */
static const float CLIP_VOLTS = 10.f, NEAR_VOLTS = 7.07f;
static const float CLIP_HOLD = 1.f, NEAR_HOLD = 0.3f, NOTE_HOLD = 0.06f;


BowedModule::BowedModule(const BowedKind& k) : kind(k) {
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	for (int i = 0; i < MAX_UPSTREAM; i++) {
		wantSlots[i].store(-1);
		wantGenerations[i].store(0);
	}
	wantRate.store(APP->engine->getSampleRate());
	wantHighest.store(kind.unsaidHighestHz);
	build();
	engine = next.exchange(nullptr);
}


BowedModule::~BowedModule() {
	delete engine;
	delete next.exchange(nullptr);
	delete retired.exchange(nullptr);
}


void BowedModule::link(const int* slots, const uint32_t* generations, int n) {
	bool same = (n == wantCount.load());
	for (int i = 0; i < n && same; i++)
		same = wantSlots[i].load() == slots[i] && wantGenerations[i].load() == generations[i];
	if (same)
		return;
	for (int i = 0; i < n && i < MAX_UPSTREAM; i++) {
		wantSlots[i].store(slots[i]);
		wantGenerations[i].store(generations[i]);
	}
	wantCount.store((n < MAX_UPSTREAM) ? n : MAX_UPSTREAM);
	relink.store(true);
}


void BowedModule::build() {
	// TAKEN APART ON A THREAD OF ITS OWN: a planner in the middle of rehearsing a note can take a
	// second or two to stop.
	if (BowedEngine* old = retired.exchange(nullptr))
		std::thread([old]() { delete old; }).detach();
	const float rate = wantRate.load(), highest = wantHighest.load();
	const bool economical = wantEconomy.load();
	const int rehearsal = wantRehearsing.load();
	if ((rate == builtRate && highest == builtHighest && economical == builtEconomy && rehearsal == builtRehearsing)
			|| next.load())
		return;
	BowedEngine* e = new BowedEngine;
	BowedEngine::Setup setup;
	setup.design = kind.design;
	setup.habits = kind.habits;
	setup.engineRate = rate;
	setup.highestHz = highest;
	setup.announcedAhead = MPX_ANNOUNCE_AHEAD;
	setup.economy = economical;
	setup.rehearsing = (BowedPlayer::Rehearsing) rehearsal;
	e->setup(setup);
	e->setWatching(false);
	builtRate = rate;
	builtHighest = highest;
	builtEconomy = economical;
	builtRehearsing = rehearsal;
	next.store(e);
}


json_t* BowedModule::dataToJson() {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "economy", json_boolean(wantEconomy.load()));
	json_object_set_new(rootJ, "fidelity", json_integer(wantRehearsing.load() + 1));
	return rootJ;
}


void BowedModule::dataFromJson(json_t* rootJ) {
	if (json_t* j = json_object_get(rootJ, "economy"))
		wantEconomy.store(json_boolean_value(j));
	if (json_t* j = json_object_get(rootJ, "fidelity"))
		setRehearsing((int) json_integer_value(j) - 1);
}


void BowedModule::onSampleRateChange(const SampleRateChangeEvent& e) {
	wantRate.store(e.sampleRate);
}


void BowedModule::onReset() {
	if (engine)
		engine->withdraw();
}


BowedPlayer::Note BowedModule::noteOf(const Event& e) {
	BowedPlayer::Note n;
	// Volts, an octave to the volt, with nought at middle C.
	n.hz = 261.6256f * std::pow(2.f, e.pitch);
	n.level = e.level;
	n.seconds = e.duration > 0.f ? e.duration : 0.25f;
	n.slurred = (e.technique & Event::SLURRED) != 0;
	n.staccato = (e.technique & Event::STACCATO) != 0;
	n.tenuto = (e.technique & Event::TENUTO) != 0;
	n.accent = (e.technique & (Event::ACCENT | Event::HEAVY_ACCENT)) != 0;
	n.vibrato = e.vibrato == Event::VIBRATO_WIDE ? BowedPlayer::Note::MOLTO
		: (e.vibrato == Event::VIBRATO_SLIGHT ? BowedPlayer::Note::ORDINARY : BowedPlayer::Note::UNSAID);
	return n;
}


bool BowedModule::wasAnnounced(int64_t handle) {
	for (int k = 0; k < announcedCount; k++) {
		const int at = (announcedFirst + k) % ANNOUNCED_MOST;
		if (announced[at] != handle)
			continue;
		// Taken out by moving the first into its place: the order means nothing.
		announced[at] = announced[announcedFirst];
		announcedFirst = (announcedFirst + 1) % ANNOUNCED_MOST;
		announcedCount--;
		return true;
	}
	return false;
}


void BowedModule::rememberAnnounced(int64_t handle) {
	if (announcedCount == ANNOUNCED_MOST) {
		announcedFirst = (announcedFirst + 1) % ANNOUNCED_MOST;
		announcedCount--;
	}
	announced[(announcedFirst + announcedCount) % ANNOUNCED_MOST] = handle;
	announcedCount++;
}


void BowedModule::process(const ProcessArgs& args) {
	if (args.sampleRate != wantRate.load())
		wantRate.store(args.sampleRate);
	if (BowedEngine* e = next.exchange(nullptr)) {
		BowedEngine* old = engine;
		engine = e;
		if (old && !retired.load())
			retired.store(old);
		else
			delete old;
		announcedCount = 0;
		liveHandle = -1;
	}
	if (relink.exchange(false)) {
		reader.clear();
		const int n = wantCount.load();
		for (int i = 0; i < n; i++)
			reader.add(wantSlots[i].load(), wantGenerations[i].load());
	}

	// THE CABLE GOING IS EVERYTHING STOPPING, as on the other instruments.
	const bool attached = reader.attached();
	if (!attached && attachedWas && engine) {
		engine->withdraw();
		engine->endNow();
	}
	attachedWas = attached;

	// THE PART'S COMPASS, from the cable when it says: the strings made ready for its highest note.
	Instrument in;
	if (reader.instrument(in) && in.change != instrumentChange) {
		instrumentChange = in.change;
		instrument = in;
		if (in.highest >= 0)
			wantHighest.store(440.f * std::pow(2.f, ((float) in.highest - 69.f) / 12.f));
	}

	if (!engine) {
		outputs[O_OUT].setVoltage(0.f);
		return;
	}
	engine->setVibrato(params[P_VIBRATO].getValue());

	// THE NOTES COMING. A chord is played as its top note: of notes announced for one moment, the
	// highest is given to the player and the rest are only remembered, so that their ONs are not
	// played as notes nobody announced.
	{
		const int64_t now = APP->engine->getFrame();
		Coming c;
		Coming top;
		bool haveTop = false;
		auto flush = [&]() {
			if (haveTop) {
				BowedPlayer::Note n = noteOf(top.note);
				n.slurSeconds = top.slurSeconds;
				n.downbeat = top.downbeat;
				engine->announce(n, top.frame - now);
			}
			haveTop = false;
		};
		while (reader.nextComing(c)) {
			if (c.kind == Coming::WITHDRAW) {
				flush();
				engine->withdraw();
				announcedCount = 0;
				continue;
			}
			rememberAnnounced(c.note.handle);
			if (haveTop && c.frame == top.frame) {
				if (c.note.pitch > top.note.pitch)
					top = c;
				continue;
			}
			flush();
			top = c;
			haveTop = true;
		}
		flush();
	}

	// THE NOTES ARRIVING. One announced is already the player's; one nobody announced is played now,
	// the top of any that arrive together.
	{
		Event e;
		Event top;
		bool haveTop = false;
		while (reader.next(e)) {
			if (e.kind == Event::ON) {
				noteHold = NOTE_HOLD;
				if (wasAnnounced(e.handle))
					continue;
				if (!haveTop || e.pitch > top.pitch) {
					top = e;
					haveTop = true;
				}
			}
			else if (e.kind == Event::OFF && e.handle == liveHandle) {
				engine->endNow();
				liveHandle = -1;
			}
		}
		if (haveTop) {
			engine->playNow(noteOf(top));
			liveHandle = top.handle;
		}
	}

	// ONE INSTRUMENT, ONE OUTPUT: where it sits is the mixer's to say.
	const float sound = engine->process() * kind.voltsPerPascal;
	outputs[O_OUT].setVoltage(sound);

	const float peak = std::fabs(sound);
	if (peak >= CLIP_VOLTS)
		clipHold = CLIP_HOLD;
	else if (peak >= NEAR_VOLTS)
		nearHold = NEAR_HOLD;
	const float dt = args.sampleTime;
	clipHold = std::fmax(0.f, clipHold - dt);
	nearHold = std::fmax(0.f, nearHold - dt);
	noteHold = std::fmax(0.f, noteHold - dt);
	const bool red = clipHold > 0.f, orange = !red && nearHold > 0.f;
	lights[L_LEVEL + 0].setBrightness(!red && !orange && noteHold > 0.f ? 1.f : 0.f);
	lights[L_LEVEL + 1].setBrightness(orange ? 1.f : 0.f);
	lights[L_LEVEL + 2].setBrightness(red ? 1.f : 0.f);
}


// ---- the panel ---------------------------------------------------------------------------------

/** Eight HP. */
static const float WIDTH = 40.64f;


static Layout bowedLayout(const std::string& slug) {
	Layout L;
	L.hp = WIDTH / 5.08f;
	L.title = slug;

	auto label = [&](const char* key, float x, float y, const char* text, const char* owner,
			bool heading, float size) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.defaultText = text; i.align = Panel::CENTRE; i.heading = heading; i.size = size;
		if (owner)
			i.owner = owner;
		L.items.push_back(i);
	};
	auto jack = [&](const char* key, Item::Kind kind, float x, float y, int id,
			const char* name, NVGcolor colour) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = colour;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 7.5f, name, key, false, 0.f);
	};
	auto knob = [&](const char* key, int id, float x, float y, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "knob"; i.x = x; i.y = y;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 8.5f, name, key, true, 8.f);
	};

	// THE PART COMING IN, at the top.
	jack("in.mpx", Item::PORT_IN, WIDTH / 2.f, 16.f, BowedModule::I_MPX, "mpx IN", NOTE_CABLE);

	// HOW IT IS PLAYED.
	knob("p.vibrato", BowedModule::P_VIBRATO, WIDTH / 2.f, 50.f, "VIBRATO");

	{
		Item lamp;
		lamp.key = "lamp.level"; lamp.kind = Item::LIGHT; lamp.style = "light.level";
		lamp.id = BowedModule::L_LEVEL; lamp.x = WIDTH / 2.f; lamp.y = 96.f;
		L.items.push_back(lamp);
		label("lamp.level.label", WIDTH / 2.f, 100.5f, "LEVEL", "lamp.level", false, 0.f);
	}
	jack("out", Item::PORT_OUT, WIDTH / 2.f, 113.f, BowedModule::O_OUT, "OUT", SIG_AUDIO);

	L.bindOffsets();
	return L;
}


BowedWidget::BowedWidget(BowedModule* module, const std::string& s) : slug(s) {
	setModule(module);
	layout = bowedLayout(slug);
	layoutApplyUser(slug, layout);
	panel = new Panel;
	addChild(panel);
	layoutBuild(this, panel, layout);
}


void BowedWidget::appendContextMenu(ui::Menu* menu) {
	layoutAppendMenu(menu, this, panel, &layout, slug);
	BowedModule* m = dynamic_cast<BowedModule*>(module);
	if (!m)
		return;
	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createIndexSubmenuItem("Fidelity",
		{"Level 1: every start tried many ways ahead, the most computing",
		 "Level 2: two ways of starting each note tried ahead",
		 "Level 3: starts guessed, bow changes tried ahead",
		 "Level 4: live, nothing tried ahead or needed ahead, the least computing"},
		[=]() { return (size_t) m->rehearsing(); }, [=](size_t r) { m->setRehearsing((int) r); }));
	menu->addChild(createBoolMenuItem("Economy: less computing, less fidelity", "",
		[=]() { return m->economy(); }, [=](bool on) { m->setEconomy(on); }));
}


void BowedWidget::step() {
	ModuleWidget::step();
	BowedModule* m = dynamic_cast<BowedModule*>(module);
	if (!m)
		return;
	m->build();
	PortWidget* port = getInput(BowedModule::I_MPX);
	int slots[MAX_UPSTREAM];
	uint32_t gens[MAX_UPSTREAM];
	int n = 0;
	if (port) {
		for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
			engine::Cable* cable = cw->getCable();
			if (!cable || n >= MAX_UPSTREAM)
				continue;
			uint32_t g = 0;
			const int slot = noteBusOf(cable->outputModule, cable->outputId, &g);
			if (slot >= 0) {
				slots[n] = slot;
				gens[n] = g;
				n++;
				cw->color = NOTE_CABLE;
			}
		}
	}
	m->link(slots, gens, n);
}


} // namespace px
