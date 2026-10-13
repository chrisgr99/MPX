/** See BowedModule.hpp. */
#include "BowedModule.hpp"

#include <algorithm>
#include <thread>

namespace px {


/** The level lamp's thresholds and how long each colour is held. */
static const float CLIP_VOLTS = 10.f, NEAR_VOLTS = 7.07f;
static const float CLIP_HOLD = 1.f, NEAR_HOLD = 0.3f, NOTE_HOLD = 0.06f;
/** WHEN A PLAYER IS FREE FOR A NOTE, seconds: its last note ending no later than this after the new one
starts, as a slur's notes overlap a little from a keyboard; and its line carried on by the new note
where its last ended no more than this before, as the player itself counts notes joined. */
static const double OVERLAP = 0.05, CONTINUES = 0.03;


float BowedSection::centsOf(int p) {
	// Spread by the golden angle, so that however many play, their tunings are spread evenly.
	return p == 0 ? 0.f : 4.f * std::sin(2.39996f * (float) p);
}


BowedModule::BowedModule(const BowedKind& k) : kind(k) {
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	for (int i = 0; i < MAX_UPSTREAM; i++) {
		wantSlots[i].store(-1);
		wantGenerations[i].store(0);
	}
	wantRate.store(APP->engine->getSampleRate());
	wantHighest.store(kind.unsaidHighestHz);
	build();
	section = next.exchange(nullptr);
}


BowedModule::~BowedModule() {
	delete section;
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
	if (BowedSection* old = retired.exchange(nullptr))
		std::thread([old]() { delete old; }).detach();
	const float rate = wantRate.load(), highest = wantHighest.load();
	const bool economical = wantEconomy.load();
	const int rehearsal = wantRehearsing.load();
	if ((rate == builtRate && highest == builtHighest && economical == builtEconomy && rehearsal == builtRehearsing)
			|| next.load())
		return;
	BowedSection* section = new BowedSection;
	BowedEngine::Setup setup;
	setup.design = kind.design;
	setup.habits = kind.habits;
	setup.engineRate = rate;
	setup.highestHz = highest;
	setup.announcedAhead = MPX_ANNOUNCE_AHEAD;
	setup.economy = economical;
	setup.rehearsing = (BowedPlayer::Rehearsing) rehearsal;
	for (int p = 0; p < BowedSection::MOST; p++) {
		// Each player's own small variations: its vibrato's pace.
		setup.habits.seed = kind.habits.seed + (uint32_t) p;
		section->player[p].setup(setup);
		section->player[p].setWatching(false);
	}
	builtRate = rate;
	builtHighest = highest;
	builtEconomy = economical;
	builtRehearsing = rehearsal;
	next.store(section);
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
	if (section)
		withdrawAll(APP->engine->getFrame());
}


/** Volts, an octave to the volt, with nought at middle C, in the player's own tuning. */
static float hzOf(float volts, int player) {
	return 261.6256f * std::pow(2.f, volts + BowedSection::centsOf(player) / 1200.f);
}


BowedPlayer::Note BowedModule::noteOf(const Event& e, int player) {
	BowedPlayer::Note n;
	n.hz = hzOf(e.pitch, player);
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


/** PLAYED FROM THE PORTS, ONE CHANNEL ONE PLAYER: channel c of the gate, the pitch, the level and the
vibrato are all the player c's, and no other's. Each is played as a monophonic synthesiser is played:
a note while its gate is up; a move of more than half a semitone while it stays up is the next note
slurred in the same bow, smaller ones, a vibrato or a bend, left alone; down, the note is let go. As
many players as the gate has channels; a one-channel cable into another port is read by every one, as
Rack's polyphonic cables are. The level is read as each note begins, nought to ten volts from ppp to fff, mf where
nothing is patched. Nothing is known ahead, so each note is played as it comes, at whatever Fidelity,
as the live level plays it. Not while an MPX cable is in: then the ports, the vibrato's too, stand
aside. */
void BowedModule::playFromPorts() {
	// NOT WHILE A CABLE IS IN: the part comes from the cable, and the ports stand aside.
	const bool read = inputs[I_GATE].isConnected() && !inputs[I_MPX].isConnected();
	const int channels = read ? std::min(BowedSection::MOST, inputs[I_GATE].getChannels()) : 0;
	for (int c = 0; c < BowedSection::MOST; c++) {
		BowedEngine& player = section->player[c];
		const float g = c < channels ? inputs[I_GATE].getVoltage(c) : 0.f;
		gateUp[c] = gateUp[c] ? g > 0.5f : g >= 1.f;
		if (!gateUp[c]) {
			if (gateSounding[c]) {
				player.endNow();
				gateSounding[c] = false;
			}
			continue;
		}
		const float pitch = inputs[I_PITCH].getPolyVoltage(c);
		const bool slur = gateSounding[c] && std::fabs(pitch - gatePitch[c]) > 0.5f / 12.f;
		if (gateSounding[c] && !slur)
			continue;
		BowedPlayer::Note n;
		n.hz = hzOf(pitch, c);
		n.level = inputs[I_LEVEL].isConnected()
			? math::clamp(inputs[I_LEVEL].getPolyVoltage(c) / 10.f, 0.f, 1.f) : 4.f / 7.f;
		// Drawn as for a note of a second; held until the gate falls.
		n.seconds = 1.f;
		n.held = true;
		n.fromSlur = slur;
		player.playNow(n);
		gateSounding[c] = true;
		gatePitch[c] = pitch;
		noteHold = NOTE_HOLD;
	}
}


/** WHO TAKES A NOTE: a player whose notes have ended by then, or end within a little after; of those,
one whose line the note carries on, its last note ending just as this one starts, the nearest in
pitch; else the first of those resting. Where every player is busy, the one whose note ends soonest
gives it up. So a line is played by one player throughout, and the notes of chords by players in
order from the highest down. */
int BowedModule::seatFor(int64_t frame, float pitch, const bool* taken) const {
	const double rate = APP->engine->getSampleRate();
	const int64_t overlap = (int64_t) (OVERLAP * rate), continues = (int64_t) (CONTINUES * rate);
	int best = -1, soonest = -1;
	float bestCost = 0.f;
	for (int p = 0; p < BowedSection::MOST; p++) {
		if (taken[p])
			continue;
		const Seat& s = seats[p];
		if (soonest < 0 || s.until < seats[soonest].until)
			soonest = p;
		if (s.until > frame + overlap)
			continue;
		const float cost = s.until >= frame - continues ? std::fabs(pitch - s.pitch) : 100.f + (float) p;
		if (best < 0 || cost < bestCost) {
			best = p;
			bestCost = cost;
		}
	}
	return best >= 0 ? best : soonest;
}


void BowedModule::shareOut(Coming* notes, int n, int64_t now, bool announced) {
	std::sort(notes, notes + n, [](const Coming& a, const Coming& b) { return a.note.pitch > b.note.pitch; });
	bool taken[BowedSection::MOST] = {};
	const double rate = APP->engine->getSampleRate();
	for (int i = 0; i < n; i++) {
		const Coming& c = notes[i];
		const int p = seatFor(c.frame, c.note.pitch, taken);
		// MORE NOTES AT ONCE THAN PLAYERS: the lowest go unplayed.
		if (p < 0)
			return;
		taken[p] = true;
		BowedPlayer::Note note = noteOf(c.note, p);
		Seat& s = seats[p];
		s.pitch = c.note.pitch;
		s.until = c.frame + (int64_t) (note.seconds * rate);
		if (announced) {
			note.slurSeconds = c.slurSeconds;
			note.downbeat = c.downbeat;
			section->player[p].announce(note, c.frame - now);
			s.liveHandle = -1;
		}
		else {
			section->player[p].playNow(note);
			s.liveHandle = c.note.handle;
		}
	}
}


void BowedModule::withdrawAll(int64_t now) {
	for (int p = 0; p < BowedSection::MOST; p++) {
		section->player[p].withdraw();
		seats[p].until = std::min(seats[p].until, now);
	}
	announcedCount = 0;
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
	if (BowedSection* e = next.exchange(nullptr)) {
		BowedSection* old = section;
		section = e;
		if (old && !retired.load())
			retired.store(old);
		else
			delete old;
		announcedCount = 0;
		// The new section starts from silence: a gate still up sounds again on it.
		for (int p = 0; p < BowedSection::MOST; p++) {
			seats[p] = Seat();
			gateSounding[p] = false;
		}
	}
	if (relink.exchange(false)) {
		reader.clear();
		const int n = wantCount.load();
		for (int i = 0; i < n; i++)
			reader.add(wantSlots[i].load(), wantGenerations[i].load());
	}

	// THE CABLE GOING IS EVERYTHING STOPPING, as on the other instruments.
	const bool attached = reader.attached();
	if (!attached && attachedWas && section) {
		withdrawAll(APP->engine->getFrame());
		for (int p = 0; p < BowedSection::MOST; p++) {
			section->player[p].endNow();
			seats[p].liveHandle = -1;
		}
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

	if (!section) {
		outputs[O_OUT].setVoltage(0.f);
		return;
	}
	// THE VIBRATO, the knob and its port, a channel a player: five volts add as much as the knob's
	// middle. The port stands aside while an MPX cable is in.
	{
		const float knob = params[P_VIBRATO].getValue();
		const bool port = !inputs[I_MPX].isConnected() && inputs[I_VIBRATO].isConnected();
		for (int p = 0; p < BowedSection::MOST; p++) {
			const float in = port ? inputs[I_VIBRATO].getPolyVoltage(p) / 5.f : 0.f;
			section->player[p].setVibrato(math::clamp(knob + in, 0.f, 2.f));
		}
	}

	// THE NOTES COMING, given out as each moment's are all in: notes announced for one moment are a
	// chord, shared among the players, and remembered, so that their ONs are not played as notes nobody
	// announced.
	const int64_t now = APP->engine->getFrame();
	{
		Coming c;
		Coming chord[BowedSection::MOST];
		int count = 0;
		while (reader.nextComing(c)) {
			if (c.kind == Coming::WITHDRAW) {
				shareOut(chord, count, now, true);
				count = 0;
				withdrawAll(now);
				continue;
			}
			rememberAnnounced(c.note.handle);
			if (count > 0 && c.frame != chord[0].frame) {
				shareOut(chord, count, now, true);
				count = 0;
			}
			if (count < BowedSection::MOST)
				chord[count++] = c;
		}
		shareOut(chord, count, now, true);
	}

	// THE NOTES ARRIVING. One announced is already a player's; those nobody announced are played now,
	// shared among the players as a chord is; an OFF lets go the player holding its note.
	{
		Event e;
		Coming chord[BowedSection::MOST];
		int count = 0;
		while (reader.next(e)) {
			if (e.kind == Event::ON) {
				noteHold = NOTE_HOLD;
				if (wasAnnounced(e.handle) || count == BowedSection::MOST)
					continue;
				chord[count].note = e;
				chord[count].frame = now;
				count++;
			}
			else if (e.kind == Event::OFF) {
				for (int p = 0; p < BowedSection::MOST; p++) {
					if (seats[p].liveHandle != e.handle)
						continue;
					section->player[p].endNow();
					seats[p].liveHandle = -1;
					seats[p].until = std::min(seats[p].until, now);
				}
			}
		}
		shareOut(chord, count, now, false);
	}

	playFromPorts();

	// THE SECTION, ONE OUTPUT: where it sits is the mixer's to say.
	float pascals = 0.f;
	for (int p = 0; p < BowedSection::MOST; p++)
		pascals += section->player[p].process();
	const float sound = pascals * kind.voltsPerPascal;
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

/** FOUR HP. The widest thing on the panel is the knob's name, VIBRATO, 11.4 mm: three HP held it with
2 mm either side and looked crowded, and the module's name in the title band was shrunk to 6 px. */
static const float WIDTH = 20.32f;


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

	// ONE COLUMN, down the middle. The jacks 15.5 mm apart: a jack is 8 mm across and its name sits
	// 7.5 mm below its centre, which leaves 3 mm between one name and the next jack. The knob, 9.6 mm
	// across with its name 8.5 mm down, keeps about the same 3 mm above and below it.
	const float x = WIDTH / 2.f, pitch = 15.5f;

	// THE PART COMING IN, at the top.
	jack("in.mpx", Item::PORT_IN, x, 16.f, BowedModule::I_MPX, "mpx IN", NOTE_CABLE);

	// PLAYED WITHOUT A CABLE.
	jack("in.gate", Item::PORT_IN, x, 16.f + pitch, BowedModule::I_GATE, "GATE", SIG_GATE);
	jack("in.pitch", Item::PORT_IN, x, 16.f + 2.f * pitch, BowedModule::I_PITCH, "V/OCT", SIG_PITCH);
	jack("in.level", Item::PORT_IN, x, 16.f + 3.f * pitch, BowedModule::I_LEVEL, "LEVEL", SIG_CV);
	jack("in.vibrato", Item::PORT_IN, x, 16.f + 4.f * pitch, BowedModule::I_VIBRATO, "VIB", SIG_CV);

	// HOW IT IS PLAYED.
	knob("p.vibrato", BowedModule::P_VIBRATO, x, 94.5f, "VIBRATO");

	// THE LEVEL LAMP, unnamed, just above the output it reports on.
	{
		Item lamp;
		lamp.key = "lamp.level"; lamp.kind = Item::LIGHT; lamp.style = "light.level";
		lamp.id = BowedModule::L_LEVEL; lamp.x = x; lamp.y = 108.f;
		L.items.push_back(lamp);
	}
	jack("out", Item::PORT_OUT, x, 114.5f, BowedModule::O_OUT, "OUT", SIG_AUDIO);

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
		 "Level 4: live, nothing tried ahead or needed ahead, the least computing (default)"},
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
