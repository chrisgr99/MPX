/** See BowedModule.hpp. */
#include "BowedModule.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <thread>

namespace px {


/** The most notes of one chord looked at: the top is played, the rest remembered as announced. */
static const int CHORD_MOST = 16;

/** HOW FAR THE BRIGHTNESS KNOB TURNS THE HIGHS DOWN, fully counter-clockwise, dB. */
static const float BRIGHT_MOST_DB = 12.f;

/** The level lamp's thresholds and how long each colour is held. */
static const float CLIP_VOLTS = 10.f, NEAR_VOLTS = 7.07f;
static const float CLIP_HOLD = 1.f, NEAR_HOLD = 0.3f, NOTE_HOLD = 0.06f;


BowedModule::BowedModule(const BowedKind& k) : kind(k), habits(k.habits) {
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
	recorder.stop();
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
	setup.habits = habits;
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
	json_object_set_new(rootJ, "diagnostics", json_boolean(wantDiagnostics.load()));
	return rootJ;
}


void BowedModule::dataFromJson(json_t* rootJ) {
	if (json_t* j = json_object_get(rootJ, "economy"))
		wantEconomy.store(json_boolean_value(j));
	if (json_t* j = json_object_get(rootJ, "fidelity"))
		setRehearsing((int) json_integer_value(j) - 1);
	if (json_t* j = json_object_get(rootJ, "diagnostics"))
		wantDiagnostics.store(json_boolean_value(j));
}


int BowedModule::missed() const {
	// Read once: the audio thread may take up a new engine meanwhile, and the one it gives back is
	// only taken apart on the main thread, which is where this is asked.
	const BowedEngine* e = engine;
	return e ? e->missed() : 0;
}


std::vector<double> BowedModule::missedTimes() const {
	const BowedEngine* e = engine;
	std::vector<double> t;
	for (int i = 0; e && i < std::min(e->missed(), BowedPlayer::MISSED_KEPT); i++)
		t.push_back(e->missedAt(i));
	return t;
}


void BowedModule::onSampleRateChange(const SampleRateChangeEvent& e) {
	wantRate.store(e.sampleRate);
}


void BowedModule::onReset() {
	if (engine) {
		engine->withdraw();
		announcedCount = 0;
	}
}


/** Volts, an octave to the volt, with nought at middle C. */
static float hzOf(float volts) {
	return 261.6256f * std::pow(2.f, volts);
}


BowedPlayer::Note BowedModule::noteOf(const Event& e) const {
	BowedPlayer::Note n;
	n.hz = hzOf(e.pitch + (float) partOctaves);
	n.level = e.level;
	n.seconds = e.duration > 0.f ? e.duration : 0.25f;
	n.slurred = (e.technique & Event::SLURRED) != 0;
	n.staccato = (e.technique & Event::STACCATO) != 0;
	n.tenuto = (e.technique & Event::TENUTO) != 0;
	n.accent = (e.technique & (Event::ACCENT | Event::HEAVY_ACCENT)) != 0;
	// THE + ON A BOWED STRING IS THE LEFT HAND PLUCKING, whether or not pizz. is written with it.
	n.pizzicato = (e.technique & (Event::PIZZICATO | Event::STOPPED)) != 0;
	n.leftHand = (e.technique & Event::STOPPED) != 0;
	n.vibrato = e.vibrato == Event::VIBRATO_WIDE ? BowedPlayer::Note::MOLTO
		: (e.vibrato == Event::VIBRATO_SLIGHT ? BowedPlayer::Note::ORDINARY : BowedPlayer::Note::UNSAID);
	return n;
}


/** PLAYED FROM THE PORTS, as a monophonic synthesiser is played: a note while any of the gate's
channels is up, at the highest pitch among those that are; a move of more than half a semitone while
it stays up is the next note slurred in the same bow, smaller ones, a vibrato or a bend, left alone;
all down, the note is let go. The level is read as each note begins, nought to ten volts from ppp to
fff, mf where nothing is patched. Nothing is known ahead, so each note is played as it comes, at
whatever Fidelity, as the live level plays it. Not while an MPX cable is in: then the ports, the
vibrato's too, stand aside. */
void BowedModule::playFromPorts() {
	// NOT WHILE A CABLE IS IN: the part comes from the cable, and the ports stand aside.
	if (!inputs[I_GATE].isConnected() || inputs[I_MPX].isConnected()) {
		if (gateSounding) {
			engine->endNow();
			gateSounding = false;
		}
		return;
	}
	const int channels = std::min(16, inputs[I_GATE].getChannels());
	bool any = false;
	int top = 0;
	float pitch = 0.f;
	for (int c = 0; c < 16; c++) {
		const float g = c < channels ? inputs[I_GATE].getVoltage(c) : 0.f;
		gateUp[c] = gateUp[c] ? g > 0.5f : g >= 1.f;
		if (!gateUp[c])
			continue;
		const float p = inputs[I_PITCH].getPolyVoltage(c);
		if (!any || p > pitch) {
			pitch = p;
			top = c;
			any = true;
		}
	}
	if (!any) {
		if (gateSounding) {
			engine->endNow();
			gateSounding = false;
		}
		return;
	}
	const bool slur = gateSounding && std::fabs(pitch - gatePitch) > 0.5f / 12.f;
	if (gateSounding && !slur)
		return;
	BowedPlayer::Note n;
	n.hz = hzOf(pitch);
	n.level = inputs[I_LEVEL].isConnected()
		? math::clamp(inputs[I_LEVEL].getPolyVoltage(top) / 10.f, 0.f, 1.f) : 4.f / 7.f;
	// Drawn as for a note of a second; held until the gate falls.
	n.seconds = 1.f;
	n.held = true;
	n.fromSlur = slur;
	engine->playNow(n);
	gateSounding = true;
	gatePitch = pitch;
	noteHold = NOTE_HOLD;
}


/** A PART ON THE CABLE IS ONE MUSICIAN'S: notes starting together are one player's chord, given to
the player as its highest note with up to three under it, the highest of the rest. The player puts
them on strings side by side, as a double stop or a chord broken as players break them, or leaves out
what no hand could reach (BowedPlayer::addAt). Announced ahead, or played now. */
void BowedModule::playTop(const Coming* notes, int n, int64_t now, bool announced) {
	if (n <= 0)
		return;
	int top = 0;
	for (int i = 1; i < n; i++)
		if (notes[i].note.pitch > notes[top].note.pitch)
			top = i;
	const Coming& c = notes[top];
	BowedPlayer::Note note = noteOf(c.note);
	{
		float under[CHORD_MOST];
		int count = 0;
		for (int i = 0; i < n; i++)
			if (i != top)
				under[count++] = notes[i].note.pitch;
		std::sort(under, under + count, [](float a, float b) { return a > b; });
		for (int i = 0; i < count && i < 3 && chordsPlayed; i++)
			note.lower[note.lowerCount++] = hzOf(under[i] + (float) partOctaves);
	}
	if (announced) {
		note.slurSeconds = c.slurSeconds;
		note.downbeat = c.downbeat;
		engine->announce(note, c.frame - now);
	}
	else {
		engine->playNow(note);
		liveHandle = c.note.handle;
	}
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
		// The new engine starts from silence: a gate still up sounds again on it.
		gateSounding = false;
		// And its player's notes are counted from its own first, its clock starting where the
		// recording has got to.
		notesLogged = 0;
		clockOffset = (double) recordedSamples / (double) args.sampleRate;
	}
	if (relink.exchange(false)) {
		// Another source, or none: what the last one said about its part is not this one's.
		instrumentChange = 0;
		partOutOfRange = false;
		partOctaves = 0;
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
		announcedCount = 0;
		liveHandle = -1;
	}
	attachedWas = attached;

	// THE PART'S COMPASS, from the cable when it says: the strings made ready for its highest note,
	// or for the end of the fingerboard where it goes past it; and whether it goes anywhere the
	// instrument cannot.
	Instrument in;
	const bool told = attached && reader.instrument(in);
	if (told && in.change != instrumentChange) {
		instrumentChange = in.change;
		instrument = in;
		auto hzOfMidi = [](int m) { return 440.f * std::pow(2.f, ((float) m - 69.f) / 12.f); };
		const float low = kind.design.lowestHz() * 0.9995f, high = kind.design.highestHz() * 1.0005f;
		const float bottom = in.lowest >= 0 ? hzOfMidi(in.lowest) : 0.f;
		const float top = in.highest >= 0 ? hzOfMidi(in.highest) : 0.f;
		partOutOfRange = (bottom > 0.f && bottom < low) || (top > 0.f && top > high);
		// THE WHOLE PART MOVED BY OCTAVES, where that brings it all within reach or nearer it.
		partOctaves = 0;
		if (bottom > 0.f)
			while (bottom * std::pow(2.f, (float) partOctaves) < low
					&& (top <= 0.f || top * std::pow(2.f, (float) (partOctaves + 1)) <= high))
				partOctaves++;
		if (top > 0.f && partOctaves == 0)
			while (top * std::pow(2.f, (float) partOctaves) > high
					&& (bottom <= 0.f || bottom * std::pow(2.f, (float) (partOctaves - 1)) >= low))
				partOctaves--;
		if (top > 0.f)
			wantHighest.store(std::min(top * std::pow(2.f, (float) partOctaves), kind.design.highestHz()));
	}
	if (!told) {
		partOutOfRange = false;
		partOctaves = 0;
		instrumentChange = 0;
	}
	// THE SOURCE TAKEN BACK TO ITS START, as its rewind does: the count of unrehearsed notes starts
	// again, so it counts one playing through. Read from the beat the source publishes, a few hundred
	// times a second.
	if (attached && engine && args.frame % 256 == 0) {
		// THE BEAT, NOT THE HARMONY: a score has a beat and no chord names, and `harmony` answers for
		// the chords alone.
		Harmony h;
		if (reader.beat(h) && h.timingValid) {
			if (h.beat < 0.25 && lastBeat > 0.5) {
				engine->clearMissed();
				engine->retakeBow();
			}
			lastBeat = h.beat;
			pieceSeconds.store(h.seconds, std::memory_order_relaxed);
		}
	}
	else if (!attached)
		pieceSeconds.store(-1.f, std::memory_order_relaxed);
	// SAID TO THE SOURCE every tenth of a second while it is so: see Bus::cannotPlayAt.
	if (partOutOfRange && args.frame % (int64_t) std::max(1.f, 0.1f * args.sampleRate) == 0)
		reader.cannotPlay();

	if (!engine) {
		outputs[O_OUT].setVoltage(0.f);
		return;
	}
	// THE VIBRATO, the knob and its port: five volts add as much as the knob's middle. The port stands
	// aside while an MPX cable is in.
	const float vibratoIn = inputs[I_MPX].isConnected() ? 0.f : inputs[I_VIBRATO].getVoltage() / 5.f;
	engine->setVibrato(math::clamp(params[P_VIBRATO].getValue() + vibratoIn, 0.f, 2.f));

	// THE NOTES COMING. A chord is played as its top note: of notes announced for one moment, the
	// highest is given to the player and the rest are only remembered, so that their ONs are not
	// played as notes nobody announced.
	const int64_t now = APP->engine->getFrame();
	{
		Coming c;
		Coming chord[CHORD_MOST];
		int count = 0;
		while (reader.nextComing(c)) {
			if (c.kind == Coming::WITHDRAW) {
				playTop(chord, count, now, true);
				count = 0;
				engine->withdraw();
				announcedCount = 0;
				continue;
			}
			rememberAnnounced(c.note.handle);
			if (count > 0 && c.frame != chord[0].frame) {
				playTop(chord, count, now, true);
				count = 0;
			}
			if (count < CHORD_MOST)
				chord[count++] = c;
		}
		playTop(chord, count, now, true);
	}

	// THE NOTES ARRIVING. One announced is already the player's; one nobody announced is played now,
	// the top of any that arrive together.
	{
		Event e;
		Coming chord[CHORD_MOST];
		int count = 0;
		while (reader.next(e)) {
			if (e.kind == Event::ON) {
				noteHold = NOTE_HOLD;
				if (wasAnnounced(e.handle) || count == CHORD_MOST)
					continue;
				chord[count].note = e;
				chord[count].frame = now;
				count++;
			}
			else if (e.kind == Event::OFF && e.handle == liveHandle) {
				engine->endNow();
				liveHandle = -1;
			}
		}
		playTop(chord, count, now, false);
	}

	playFromPorts();

	// THE BRIGHTNESS, a shelf on what the instrument puts out, made again only when the knob or the
	// rate has moved. Fully clockwise it passes the sound unchanged.
	{
		const float bright = params[P_BRIGHT].getValue();
		if (bright != shelf.madeFor || args.sampleRate != shelf.rateFor) {
			shelf.set(kind.brightCornerHz, -BRIGHT_MOST_DB * (1.f - math::clamp(bright, 0.f, 1.f)), args.sampleRate);
			shelf.madeFor = bright;
		}
	}
	// ONE INSTRUMENT, ONE OUTPUT: where it sits is the mixer's to say.
	const float sound = shelf.run(engine->process() * kind.voltsPerPascal);
	outputs[O_OUT].setVoltage(sound);
	recordProcess(sound);

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


void BowedShelf::set(float corner, float db, float rate) {
	// Bristow-Johnson's high shelf at a slope of one: `corner` the middle of the change.
	const double A = std::pow(10.0, (double) db / 40.0);
	const double w = 2.0 * M_PI * std::min((double) corner, 0.45 * (double) rate) / (double) rate;
	const double c = std::cos(w), alpha = std::sin(w) / 2.0 * std::sqrt(2.0), rootA = std::sqrt(A);
	const double a0 = (A + 1.0) - (A - 1.0) * c + 2.0 * rootA * alpha;
	b0 = A * ((A + 1.0) + (A - 1.0) * c + 2.0 * rootA * alpha) / a0;
	b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0;
	b2 = A * ((A + 1.0) + (A - 1.0) * c - 2.0 * rootA * alpha) / a0;
	a1 = 2.0 * ((A - 1.0) - (A + 1.0) * c) / a0;
	a2 = ((A + 1.0) - (A - 1.0) * c - 2.0 * rootA * alpha) / a0;
	rateFor = rate;
}


// ---- the recording ------------------------------------------------------------------------------

void BowedRecorder::pushSample(float v) {
	const uint32_t w = samplesWritten.load(std::memory_order_relaxed);
	if (w - samplesRead.load(std::memory_order_acquire) >= SAMPLES) {
		dropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	samples[w % SAMPLES] = v;
	samplesWritten.store(w + 1, std::memory_order_release);
}


void BowedRecorder::pushNote(const NoteLine& n) {
	const uint32_t w = notesWritten.load(std::memory_order_relaxed);
	if (w - notesRead.load(std::memory_order_acquire) >= NOTES)
		return;
	notes[w % NOTES] = n;
	notesWritten.store(w + 1, std::memory_order_release);
}


/** A WAV file's header, for a mono file of 32-bit floats `count` samples long. */
static void wavHeader(std::FILE* f, float rate, uint32_t count) {
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	const uint32_t r = (uint32_t) std::lround(rate);
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + 4 * count);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(3);         // IEEE floats
	u16(1);
	u32(r);
	u32(r * 4);
	u16(4);
	u16(32);
	std::fwrite("data", 1, 4, f);
	u32(4 * count);
}


bool BowedRecorder::start(const std::string& slug, float sampleRate, float piece) {
	const std::string folder = asset::user("DreamerMPX/recordings");
	system::createDirectories(folder);
	char when[64];
	const std::time_t now = std::time(nullptr);
	std::strftime(when, sizeof when, "%Y-%m-%d %H.%M.%S", std::localtime(&now));
	std::string name = slug + " " + when;
	if (piece >= 0.f)
		name += string::f(" from %.1f s", piece);
	path = system::join(folder, name + ".wav");
	wav = std::fopen(path.c_str(), "wb");
	if (!wav)
		return false;
	log = std::fopen(system::join(folder, name + ".txt").c_str(), "w");
	rate = sampleRate;
	wavSamples = 0;
	wavHeader(wav, rate, 0);
	if (log)
		std::fprintf(log, "%s, recorded at %.0f Hz%s. A line a note: seconds into the recording, seconds into "
			"the piece, pitch, string, how it began, the bow's way, force, speed, distance from the bridge, "
			"vibrato, whether its start was rehearsed, and for a bow change how long the bow took to turn.\n",
			slug.c_str(), rate, piece >= 0.f ? string::f(", from %.1f s into the piece", piece).c_str() : "");
	// Nothing left over from a recording before.
	samplesRead.store(samplesWritten.load());
	notesRead.store(notesWritten.load());
	dropped.store(0);
	open.store(true, std::memory_order_release);
	return true;
}


void BowedRecorder::drain() {
	if (!wav)
		return;
	const uint32_t w = samplesWritten.load(std::memory_order_acquire);
	uint32_t r = samplesRead.load(std::memory_order_relaxed);
	while (r != w) {
		const uint32_t at = r % SAMPLES;
		const uint32_t n = std::min(w - r, SAMPLES - at);
		std::fwrite(&samples[at], sizeof(float), n, wav);
		wavSamples += n;
		r += n;
	}
	samplesRead.store(r, std::memory_order_release);
	const uint32_t nw = notesWritten.load(std::memory_order_acquire);
	uint32_t nr = notesRead.load(std::memory_order_relaxed);
	static const char* const KIND[] = {"from the string", "bow change", "slurred", "crossing", "plucked"};
	for (; nr != nw; nr++) {
		const NoteLine& line = notes[nr % NOTES];
		const BowedPlayer::Played& p = line.played;
		if (!log)
			continue;
		// PLUCKED: no bow to report, but how hard and where.
		if (p.kind == BowedPlayer::Played::PLUCK) {
			std::fprintf(log, "%8.3f  %7.2f  %7.1f Hz  string %d  %-15s       strength %.2f  %3.0f mm  vibrato %2.0f c%s\n",
				line.sinceStart, line.pieceSeconds, p.hz, p.string + 1, KIND[(int) p.kind], p.force,
				1000.0 * p.distance, p.vibratoCents,
				p.withString >= 0 ? string::f(", a chord down to %.1f Hz", p.withHz).c_str() : "");
			continue;
		}
		std::string how;
		if (p.kind != BowedPlayer::Played::LEGATO)
			how = (p.rehearsed > 0 || p.fromTable) ? "  rehearsed" : "  NOT REHEARSED";
		if (p.kind == BowedPlayer::Played::CHANGE)
			how += string::f(", turn %.0f ms", 1000.0 * p.changeSeconds);
		if (p.settled >= 0.0)
			how += string::f(", settled in %.0f periods", p.settled);
		if (p.hidden)
			how += ", bow changed inside the slur";
		if (p.withString >= 0)
			how += string::f(", with %.1f Hz on string %d", p.withHz, p.withString + 1);
		std::fprintf(log, "%8.3f  %7.2f  %7.1f Hz  string %d  %-15s %-4s  %.2f N  %.2f m/s  %3.0f mm  vibrato %2.0f c%s\n",
			line.sinceStart, line.pieceSeconds, p.hz, p.string + 1, KIND[(int) p.kind],
			p.direction > 0 ? "down" : "up", p.force, std::fabs(p.speed), 1000.0 * p.distance, p.vibratoCents,
			how.c_str());
	}
	notesRead.store(nr, std::memory_order_release);
}


void BowedRecorder::stop() {
	open.store(false, std::memory_order_release);
	drain();
	if (wav) {
		std::fseek(wav, 0, SEEK_SET);
		wavHeader(wav, rate, wavSamples);
		std::fclose(wav);
		wav = nullptr;
	}
	if (log) {
		if (dropped.load() > 0)
			std::fprintf(log, "%u samples could not be written in time and are missing.\n", dropped.load());
		std::fclose(log);
		log = nullptr;
	}
}


/** WHAT IS PLAYED, HANDED OVER while the recorder is open: the sample, and each note of the player's
as it begins, with where it began in the recording and in the piece. Audio thread. */
void BowedModule::recordProcess(float sound) {
	if (!recorder.open.load(std::memory_order_acquire)) {
		recordingWas = false;
		return;
	}
	const BowedPlayer& player = engine->player();
	const double now = player.now();
	if (!recordingWas) {
		recordingWas = true;
		notesLogged = player.played.size();
		recordedSamples = 0;
		clockOffset = -now;
	}
	recorder.pushSample(sound);
	recordedSamples++;
	const float piece = pieceSeconds.load(std::memory_order_relaxed);
	while (notesLogged < player.played.size() && player.played[notesLogged].at <= now) {
		BowedRecorder::NoteLine line;
		line.played = player.played[notesLogged];
		line.sinceStart = line.played.at + clockOffset;
		line.pieceSeconds = piece >= 0.f ? piece - (float) (now - line.played.at) : -1.f;
		recorder.pushNote(line);
		notesLogged++;
	}
}


void BowedModule::recordStep() {
	// HIDDEN, NOT RECORDING: the button goes out with the diagnostics.
	if (!wantDiagnostics.load() && params[P_RECORD].getValue() > 0.5f)
		params[P_RECORD].setValue(0.f);
	const bool want = params[P_RECORD].getValue() > 0.5f;
	if (want && !recorder.open.load()) {
		// A file that will not open puts the button out rather than pretending to record.
		if (!recorder.start(kind.slug, wantRate.load(), pieceSeconds.load()))
			params[P_RECORD].setValue(0.f);
	}
	else if (!want && recorder.open.load())
		recorder.stop();
	else if (want)
		recorder.drain();
}


// ---- the panel ---------------------------------------------------------------------------------

/** SIX HP, for the module's name: the title band shrinks a name that does not fit, and at six HP
MPX VIOLA and MPX CELLO are set at 13.5 px, the size the piano's is set at, and MPX VIOLIN at 12.9.
Four HP held the names of the controls but set the module's at 9 to 10 px. */
static const float WIDTH = 30.48f;


static Layout bowedLayout(const std::string& slug) {
	Layout L;
	L.hp = WIDTH / 5.08f;
	// THE TITLE IN CAPITALS, as every name on the panel is: letters with descenders are harder to
	// read. "MPX VIOLIN" rather than the slug run together, which in capitals loses the change of
	// case that marked where one word ends.
	L.title = "MPX " + slug.substr(3);
	for (char& c : L.title)
		c = (char) std::toupper((unsigned char) c);

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
		label((std::string(key) + ".label").c_str(), x, y + 7.f, name, key, true, 0.f);
	};
	auto knob = [&](const char* key, int id, float x, float y, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = "knob"; i.x = x; i.y = y;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 8.5f, name, key, true, 0.f);
	};

	// ONE COLUMN, down the middle, under a row of diagnostics. EVERY NAME AS THE PIANO SETS ITS NAMES:
	// the heading face at its own size, 10 px. The jacks 14.5 mm apart: a jack is 8 mm across and its
	// name sits 7 mm below its centre, 1.8 mm under the jack and 2.3 mm above the next one, so that it
	// reads as its own jack's. The knob, 9.6 mm across with its name 8.5 mm down, keeps about the same
	// room above and below it.
	const float x = WIDTH / 2.f, pitch = 14.5f, top = 20.5f;

	// THE DIAGNOSTICS, along the top, shown or hidden from the right-click menu: the count of notes that
	// went unrehearsed (see MissedDisplay), and the button that records what is played (BowedRecorder),
	// red while it is.
	{
		Item missed;
		missed.key = "d.missed"; missed.kind = Item::DISPLAY;
		missed.x = 2.f; missed.y = 9.3f; missed.w = 10.f; missed.h = 3.4f;
		L.items.push_back(missed);
		Item record;
		record.key = "p.record"; record.kind = Item::PARAM; record.id = BowedModule::P_RECORD;
		record.style = "latch.red"; record.x = 17.5f; record.y = 11.f;
		L.items.push_back(record);
		label("p.record.label", 25.f, 11.f, "REC", "p.record", true, 0.f);
	}

	// THE PART COMING IN.
	jack("in.mpx", Item::PORT_IN, x, top, BowedModule::I_MPX, "MPX IN", NOTE_CABLE);

	// PLAYED WITHOUT A CABLE.
	jack("in.gate", Item::PORT_IN, x, top + pitch, BowedModule::I_GATE, "GATE", SIG_GATE);
	jack("in.pitch", Item::PORT_IN, x, top + 2.f * pitch, BowedModule::I_PITCH, "V/OCT", SIG_PITCH);
	jack("in.level", Item::PORT_IN, x, top + 3.f * pitch, BowedModule::I_LEVEL, "LEVEL", SIG_CV);
	jack("in.vibrato", Item::PORT_IN, x, top + 4.f * pitch, BowedModule::I_VIBRATO, "VIB", SIG_CV);

	// HOW IT IS PLAYED, two knobs side by side: the vibrato's width and the brightness. Each 9.6 mm across,
	// their centres 12.5 mm apart, 2.9 mm between them and 4.2 mm to each edge.
	knob("p.vibrato", BowedModule::P_VIBRATO, x - 6.25f, 95.f, "VIB");
	knob("p.bright", BowedModule::P_BRIGHT, x + 6.25f, 95.f, "BRT");

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


/** HOW MANY NOTES WENT UNREHEARSED though the Fidelity asked for rehearsing: notes the planner had
not decided by the time they were due, played as level 4 plays every note. A diagnostic, for watching
the rehearsing keep up in use; a busy machine is the usual cause. Counted from nought since the
Fidelity, Economy or the part's range last changed or the source went back to its start, and always
nought at level 4, which rehearses nothing. See BowedModule::missed. */
struct MissedDisplay : widget::Widget {
	BowedModule* module = nullptr;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::plugin(pluginInstance, "res/Hack-Regular.ttf"));
		if (!font || font->handle < 0)
			return;
		const std::string text = std::to_string(module ? module->missed() : 0);
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 10.f);
		nvgFillColor(vg, PANEL_INK);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		// Twice, as the panel's own lettering is drawn: see Panel::crisp.
		for (int pass = 0; pass < 2; pass++)
			nvgText(vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
	}
};


BowedWidget::BowedWidget(BowedModule* module, const std::string& s) : slug(s) {
	setModule(module);
	layout = bowedLayout(slug);
	layoutApplyUser(slug, layout);
	panel = new Panel;
	addChild(panel);
	layoutBuild(this, panel, layout);
	MissedDisplay* missed = new MissedDisplay;
	missed->module = module;
	layoutPlaceDisplay(this, layout, "d.missed", missed);
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
	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createBoolMenuItem("Enable diagnostics", "",
		[=]() { return m->diagnostics(); }, [=](bool on) { m->setDiagnostics(on); }));
	menu->addChild(createMenuItem("Open the recordings folder", "", []() {
		const std::string folder = asset::user("DreamerMPX/recordings");
		system::createDirectories(folder);
		system::openDirectory(folder);
	}));
	if (!m->recordedTo().empty())
		menu->addChild(createMenuLabel("Last recorded: " + system::getFilename(m->recordedTo())));
}


void BowedWidget::step() {
	ModuleWidget::step();
	BowedModule* m = dynamic_cast<BowedModule*>(module);
	if (!m)
		return;
	m->build();
	// THE DIAGNOSTICS SHOWN OR HIDDEN, the panel told only when that changes.
	const bool show = m->diagnostics();
	if ((int) show != diagnosticsShown) {
		diagnosticsShown = show;
		for (const char* key : {"d.missed", "p.record"})
			if (Item* item = layout.find(key))
				if (item->widget)
					item->widget->visible = show;
		if (Item* name = layout.find("p.record.label")) {
			name->hidden = !show;
			layoutRefreshPanel(panel, layout);
		}
	}
	m->recordStep();
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
