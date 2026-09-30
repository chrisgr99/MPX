/** A Guitar Pro file, played. See docs/guitar-pro.md.

ONE CABLE IS ONE INSTRUMENT. Each row of the panel is one track of the song on its own MPX
output, because everything downstream of a cable in this plugin reads it as one instrument:
mpxVoicing decides where the voices sit, mpxArp arpeggiates the notes, mpxOut turns the cable
into control voltage, and none of them asks which part a note belongs to. A band on one cable
would have all of them playing every track at once.

THE FILE IS READ ONCE, ON THE MAIN THREAD, and what comes out of it — the bars in the order they
sound, every note with the moment it starts and the moment it stops — is fixed from then on. The
audio thread reads it and nothing else. Two copies, with a pointer swapped between them, so
loading a song while one is playing cannot be heard.

WHAT IS NOT HERE YET. There are no chord symbols on the cable: working the harmony out from the
notes comes later, and until it does the cable carries the beat, the bar and the metre but no
chords. See docs/guitar-pro.md.
*/
#include "plugin.hpp"
#include "GpTimeline.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"

#include <osdialog.h>

#include <atomic>
#include <cctype>
#include <cstring>

namespace px {


/** HOW MANY ROWS THE MODULE HAS. Six are drawn; the rest exist from the moment the module is
made, because Rack fixes a module's ports when it is created and a row that appeared later would
have no port behind it. The second band of six is drawn in the next phase. */
static const int ROWS = 12;
static const int SHOWN = 6;

/** A note's length is the file's, within reason: a note held to the end of a long let-ring
passage is still a note and not a drone. */
static const float MIN_SECONDS = 0.01f;
static const float MAX_SECONDS = 30.f;


/** A song as it is played, with the rows' choice of track. Whole, so that swapping to a new one
is one store. */
struct Playing {
	GpSong song;
	GpTimeline line;
	/** Which track each row carries, or -1 for a row carrying nothing. */
	int track[ROWS] = {};
	std::string title;
	/** Moves when the song or a row's choice of track changes, so the instruments on the cables
	are published again and only then. */
	uint32_t partChange = 1;
	/** The tempo the song opens at, in quarter notes a minute. What the tempo control is set to
	when a song is loaded, and what it is measured against afterwards. */
	float openingBpm = 120.f;

	Playing() {
		for (int r = 0; r < ROWS; r++)
			track[r] = -1;
	}
};


struct SongModule : Module, NoteSource {
	enum ParamId {
		P_PLAY,
		P_REWIND,
		P_TEMPO,
		P_MUTE,
		NUM_PARAMS = P_MUTE + ROWS
	};
	enum InputId {
		I_CLOCK,
		I_RESET,
		NUM_INPUTS
	};
	enum OutputId {
		O_TRACK,
		NUM_OUTPUTS = O_TRACK + ROWS
	};
	enum LightId {
		L_BEAT,
		NUM_LIGHTS
	};

	/** ONE BUS PER ROW THAT CARRIES SOMETHING. There are sixty-four buses in the whole rack, so
	a module holding twelve whether or not it used them would be taking a sixth of them for
	nothing. They are claimed when a song is loaded, which is on the main thread, so no row is
	ever waiting for a bus while the music plays. */
	int slot[ROWS];
	uint32_t generation[ROWS] = {};

	Playing loaded[2];
	std::atomic<int> live{0};
	std::atomic<bool> haveSong{false};
	/** The file as it arrived, kept so the patch carries the song rather than a path to it: a
	patch that pointed at a file would open to silence on a machine that never had it. */
	std::string fileBytes;
	std::string filePath;
	/** For the panel, which reads from the main thread while the audio thread plays. */
	std::atomic<int> playedBar{0};
	std::atomic<float> soundingBpm{120.f};

	/** WHERE THE MUSIC IS, in quarter notes from the start of the played timeline. */
	double quarters = 0.0;
	/** How far each row has got through its track's notes. */
	size_t cursor[ROWS] = {};
	int barIndex = 0;
	uint32_t epoch = 0;
	uint32_t partChange = 0;

	dsp::SchmittTrigger clockTrigger, resetTrigger, rewindTrigger;
	float sinceLastPulse = 0.f;
	float quarterSeconds = 0.5f;
	bool awaitPulse = false;
	bool wasRunning = false;
	float beatLamp = 0.f;


	SongModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(P_PLAY, 0.f, 1.f, 1.f, "Play", {"Stopped", "Playing"});
		//? Starts the song again from its first bar. It does not stop it.
		configButton(P_REWIND, "Rewind");
		//? The tempo to play at, in quarter notes a minute. Set to the song's own when one is
		//? loaded; changing it moves every tempo in the song by the same proportion, so a song
		//? that speeds up or slows down still does. A clock overrides it.
		configParam(P_TEMPO, 20.f, 400.f, 120.f, "Tempo", " BPM");
		for (int r = 0; r < ROWS; r++) {
			//? Silences this row without unpatching it, so what is downstream keeps its cable
			//? and its settings.
			configSwitch(P_MUTE + r, 0.f, 1.f, 0.f, string::f("Mute %d", r + 1),
				{"Playing", "Muted"});
			// THE NAME SAYS MPX, and has to. Clarity colours a jack by its name where it has
			// no entry for the module, and its first rule is that a name carrying MPX is an
			// MPX jack. Named for the track alone, these came out orange — the colour of a
			// control voltage. When the name becomes the instrument's, it keeps the word.
			configOutput(O_TRACK + r, string::f("MPX track %d", r + 1));
		}
		configInput(I_CLOCK, "Clock");
		configInput(I_RESET, "Reset");

		for (int r = 0; r < ROWS; r++)
			slot[r] = -1;
	}

	~SongModule() {
		for (int r = 0; r < ROWS; r++)
			busRelease(slot[r]);
	}

	int busSlotFor(int outputId, uint32_t* gen) override {
		const int r = outputId - O_TRACK;
		if (r < 0 || r >= ROWS)
			return -1;
		if (gen)
			*gen = generation[r];
		return slot[r];
	}

	void onReset() override {
		rewind();
	}

	void rewind() {
		quarters = 0.0;
		barIndex = 0;
		for (int r = 0; r < ROWS; r++)
			cursor[r] = 0;
	}

	// ---- loading a song ------------------------------------------------------------------------

	/** A bus for every row that carries a track, and none for the rest. Main thread. */
	void claimBuses(const Playing& L) {
		for (int r = 0; r < ROWS; r++) {
			const bool wanted = L.track[r] >= 0;
			if (wanted && slot[r] < 0)
				slot[r] = busClaim(&generation[r]);
			else if (!wanted && slot[r] >= 0) {
				busRelease(slot[r]);
				slot[r] = -1;
			}
		}
	}

	/** Reads a file and puts it on the spare copy. Main thread. Returns a reason on failure, and
	leaves whatever was playing alone. */
	bool setSongBytes(const std::vector<uint8_t>& bytes, const std::string& path,
			std::string* why) {
		const int spare = 1 - live.load();
		Playing next;
		if (!gpReadBytes(bytes, next.song, why))
			return false;
		if (!gpBuildTimeline(next.song, next.line, why))
			return false;

		// WHICH TRACKS THE ROWS TAKE. A song has more tracks than there are rows more often than
		// not — nineteen is an ordinary transcription — and what is wanted out of it is four or
		// five. The ones carrying the most notes are a reasonable first guess, and the next phase
		// puts the choice on the panel.
		std::vector<std::pair<int, int> > byNotes;   // Notes, then track, so a sort ranks them.
		for (size_t t = 0; t < next.line.tracks.size(); t++)
			byNotes.push_back(std::make_pair(-(int) next.line.tracks[t].size(), (int) t));
		std::sort(byNotes.begin(), byNotes.end());
		for (int r = 0; r < ROWS && r < (int) byNotes.size(); r++)
			next.track[r] = byNotes[(size_t) r].second;

		next.openingBpm = (float) (60.0 / std::fmax(0.0001,
			gpSecondsPerQuarter(next.line, 0.0)));
		// WHAT TO CALL IT. Plenty of files carry no title at all — the Oasis one has an empty
		// title field and the tabber's name where the artist goes — and the file's own name is
		// what somebody typed to say what the song is, so it is a better answer than a word
		// saying there is no answer.
		next.title = next.song.title;
		if (!next.title.empty() && !next.song.artist.empty())
			next.title += " — " + next.song.artist;
		if (next.title.empty()) {
			std::string name = path;
			const size_t slash = name.find_last_of("/\\");
			if (slash != std::string::npos)
				name = name.substr(slash + 1);
			// Every extension it has, since these files arrive named things like
			// "... score.gpif.xml".
			static const char* ENDS[] = {".xml", ".gpif", ".gpx", ".gp5", ".gp4", ".gp3",
				".gp", NULL};
			for (bool again = true; again; ) {
				again = false;
				for (int i = 0; ENDS[i]; i++) {
					const size_t n = std::strlen(ENDS[i]);
					if (name.size() <= n)
						continue;
					std::string tail = name.substr(name.size() - n);
					for (size_t k = 0; k < tail.size(); k++)
						tail[k] = (char) std::tolower((unsigned char) tail[k]);
					if (tail == ENDS[i]) {
						name = name.substr(0, name.size() - n);
						again = true;
						break;
					}
				}
			}
			next.title = name.empty() ? "(untitled)" : name;
		}

		next.partChange = loaded[live.load()].partChange + 1;
		loaded[spare] = next;
		live.store(spare);
		claimBuses(next);
		haveSong.store(!loaded[spare].line.bars.empty());
		fileBytes.assign((const char*) bytes.data(), bytes.size());
		filePath = path;
		params[P_TEMPO].setValue(math::clamp(next.openingBpm, 20.f, 400.f));
		rewind();
		return true;
	}

	bool setSongFile(const std::string& path, std::string* why) {
		FILE* f = std::fopen(path.c_str(), "rb");
		if (!f) {
			if (why)
				*why = "that file could not be opened";
			return false;
		}
		std::vector<uint8_t> bytes;
		uint8_t buf[65536];
		size_t n = 0;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
			bytes.insert(bytes.end(), buf, buf + n);
		std::fclose(f);
		return setSongBytes(bytes, path, why);
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		if (!fileBytes.empty()) {
			json_object_set_new(rootJ, "file", json_string(string::toBase64(
				(const uint8_t*) fileBytes.data(), fileBytes.size()).c_str()));
			json_object_set_new(rootJ, "path", json_string(filePath.c_str()));
		}
		const Playing& L = loaded[live.load()];
		json_t* rowsJ = json_array();
		for (int r = 0; r < ROWS; r++)
			json_array_append_new(rowsJ, json_integer(L.track[r]));
		json_object_set_new(rootJ, "rows", rowsJ);
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* fileJ = json_object_get(rootJ, "file");
		if (!json_is_string(fileJ))
			return;
		const std::vector<uint8_t> bytes = string::fromBase64(json_string_value(fileJ));
		json_t* pathJ = json_object_get(rootJ, "path");
		std::string why;
		if (!setSongBytes(bytes, json_is_string(pathJ) ? json_string_value(pathJ) : "", &why))
			return;
		// The rows as they were left, over the guess the loading made.
		json_t* rowsJ = json_object_get(rootJ, "rows");
		if (!json_is_array(rowsJ))
			return;
		Playing& L = loaded[live.load()];
		for (int r = 0; r < ROWS && r < (int) json_array_size(rowsJ); r++) {
			const int t = (int) json_integer_value(json_array_get(rowsJ, r));
			L.track[r] = (t >= 0 && t < (int) L.line.tracks.size()) ? t : -1;
		}
		claimBuses(L);
	}

	// ---- playing -----------------------------------------------------------------------------

	/** WHAT THE FILE SAID ABOUT HOW THE NOTE IS PLAYED, onto the note going out. The flags say
	what it is; nothing here decides what it sounds like — that is the performer's business, so
	that two renderers of the same note agree and either can be varied. */
	static void describe(Event& e, const GpPlayedNote& n, bool letRingThroughout) {
		const GpNote& g = n.note;
		e.string = (int8_t) math::clamp(g.string, 0, 127);
		e.fret = (int8_t) math::clamp(g.fret, -1, 127);

		uint32_t t = 0;
		// HAMMER-ON OR PULL-OFF: the file writes one mark for both, and which it is depends on
		// whether the hand went up the string or down it.
		if (g.hammer)
			t |= (n.fromMidi >= 0 && g.midi < n.fromMidi) ? Event::PULL_OFF : Event::HAMMER_ON;
		if (g.slideFlags & 0x01) t |= Event::SHIFT_SLIDE;
		if (g.slideFlags & 0x02) t |= Event::LEGATO_SLIDE;
		if (g.slideFlags & 0x04) t |= Event::SLIDE_OUT_DOWN;
		if (g.slideFlags & 0x08) t |= Event::SLIDE_OUT_UP;
		if (g.slideFlags & 0x10) t |= Event::SLIDE_IN_BELOW;
		if (g.slideFlags & 0x20) t |= Event::SLIDE_IN_ABOVE;
		// A slide the file marked without saying which kind: to the next note, which is what a
		// slide usually is.
		if (g.slide && g.slideFlags == 0)
			t |= Event::SHIFT_SLIDE;
		if (g.letRing || letRingThroughout) t |= Event::LET_RING;
		if (g.palmMute) t |= Event::PALM_MUTE;
		if (g.dead) t |= Event::DEAD_NOTE;
		if (g.ghost) t |= Event::GHOST;
		if (g.accent) t |= Event::ACCENT;
		if (g.heavyAccent) t |= Event::HEAVY_ACCENT;
		if (g.harmonic) t |= Event::HARMONIC;
		if (g.artificialHarmonic) t |= Event::ARTIFICIAL_HARMONIC;
		if (g.tapped) t |= Event::TAPPED;   // From the beat: the hand does one thing at a time.
		if (g.staccato) t |= Event::STACCATO;
		if (g.tremolo) t |= Event::TREMOLO;
		e.technique = t;

		e.vibrato = g.vibrato ? Event::VIBRATO_SLIGHT : Event::VIBRATO_NONE;
		e.grace = (n.grace == 2) ? Event::GRACE_ON
			: (n.grace == 1 ? Event::GRACE_BEFORE : Event::GRACE_NONE);

		e.strum = (int8_t) ((n.brush > 0) ? 1 : (n.brush < 0 ? -1 : 0));
		e.strumIndex = n.strumIndex;
		e.strumMs = (uint8_t) math::clamp(n.brushMs, 0, 255);

		e.bendCount = (uint8_t) std::min<size_t>(4, g.bend.size());
		for (int i = 0; i < (int) e.bendCount; i++) {
			e.bendPoints[i].at =
				(uint8_t) math::clamp((int) std::lround(g.bend[(size_t) i].first * 100.f), 0, 100);
			e.bendPoints[i].cents =
				(int16_t) math::clamp((int) std::lround(g.bend[(size_t) i].second), -4800, 4800);
		}
	}

	void process(const ProcessArgs& args) override {
		// A note cable carries no voltage: it is a real cable so that Rack owns it, and the
		// notes travel through the bus.
		for (int r = 0; r < ROWS; r++) {
			outputs[O_TRACK + r].setChannels(1);
			outputs[O_TRACK + r].setVoltage(0.f);
		}
		if (!haveSong.load())
			return;
		const Playing& L = loaded[live.load()];
		if (L.line.bars.empty() || L.line.quarters <= 0.0)
			return;

		if (resetTrigger.process(inputs[I_RESET].getVoltage(), 0.1f, 1.f))
			rewind();
		if (rewindTrigger.process(params[P_REWIND].getValue(), 0.1f, 1.f))
			rewind();

		// STOPPED MEANS STOPPED, whatever the clock is doing: this is a transport and not a
		// mute. The beat is still published, so anything following the song knows where it was
		// stopped rather than losing it.
		const bool running = params[P_PLAY].getValue() > 0.5f;
		if (running && !wasRunning)
			awaitPulse = true;
		wasRunning = running;

		// WHAT A QUARTER NOTE LASTS. From the song's own tempo map, or from the clock where one
		// is patched — and then the song's tempo changes are the clock's business rather than
		// the file's, which is what patching a clock into a player means.
		sinceLastPulse += args.sampleTime;
		if (inputs[I_CLOCK].isConnected()) {
			const bool pulse = clockTrigger.process(inputs[I_CLOCK].getVoltage(), 0.1f, 1.f);
			if (pulse) {
				if (sinceLastPulse > 0.001f && sinceLastPulse < 10.f)
					quarterSeconds = sinceLastPulse;
				sinceLastPulse = 0.f;
				if (running && awaitPulse)
					awaitPulse = false;
			}
			sinceLastPulse = std::fmin(sinceLastPulse, 10.f);
		}
		else {
			// THE SONG'S OWN TEMPO MAP, MOVED BY THE CONTROL. The whole map is scaled rather
			// than replaced, so a song that speeds up or slows down still does, in proportion.
			const float wanted = params[P_TEMPO].getValue();
			const float scale = (L.openingBpm > 0.f && wanted > 0.f)
				? wanted / L.openingBpm : 1.f;
			quarterSeconds = (float) gpSecondsPerQuarter(L.line, quarters) / scale;
			awaitPulse = false;
		}
		soundingBpm.store(60.f / std::fmax(0.0001f, quarterSeconds));

		if (running && !awaitPulse)
			quarters += args.sampleTime / std::fmax(0.0001f, (double) quarterSeconds);

		// THE END IS THE BEGINNING. A song that stopped dead would take the patch with it, and
		// a loop is what anybody building on top of one wants.
		if (quarters >= L.line.quarters) {
			quarters -= L.line.quarters;
			epoch++;
			barIndex = 0;
			for (int r = 0; r < ROWS; r++)
				cursor[r] = 0;
		}

		// Which played bar the music is in. A walk rather than a search: the position moves by
		// a fraction of a beat between frames, so the answer is almost always where it was.
		if (barIndex < 0 || barIndex >= (int) L.line.bars.size())
			barIndex = 0;
		for (size_t guard = 0; guard < L.line.bars.size(); guard++) {
			const GpPlayedBar& b = L.line.bars[(size_t) barIndex];
			if (quarters >= b.startQuarters && quarters < b.startQuarters + b.quarters)
				break;
			barIndex = (barIndex + 1) % (int) L.line.bars.size();
		}
		const GpPlayedBar& bar = L.line.bars[(size_t) barIndex];
		playedBar.store(barIndex);

		// THE BEAT AND THE BAR ON EVERY CABLE, and no chords: what the harmony is comes from the
		// notes, and that is a later phase. `valid` says so, so nothing takes silence for a
		// chord it can play against.
		//
		// A BEAT HERE IS A QUARTER NOTE. A song changes metre from one bar to the next — White
		// Room has bars of five among its fours — and a count that changed its unit with the
		// signature would jump. The signature itself is published beside it, so a reader that
		// wants bars has them.
		const GpMasterBar& written = L.song.masterBars[(size_t) std::min(bar.written,
			(int) L.song.masterBars.size() - 1)];
		Harmony h;
		h.valid = false;
		h.beat = quarters;
		h.cycleBeats = (float) L.line.quarters;
		h.barBeats = (uint8_t) math::clamp(written.beatsPerBar, 1, 255);
		h.barUnit = (uint8_t) math::clamp(written.beatUnit, 1, 255);
		h.bar = barIndex;
		h.beatInBar = (float) (quarters - bar.startQuarters);
		h.epoch = epoch;
		h.holding = !running || awaitPulse;
		for (int r = 0; r < ROWS; r++) {
			if (slot[r] >= 0)
				busPublishHarmony(slot[r], h);
		}

		// WHAT INSTRUMENT EACH CABLE IS CARRYING, published when it changes rather than every
		// sample: it changes when a song is loaded and at no other time.
		if (partChange != L.partChange) {
			partChange = L.partChange;
			for (int r = 0; r < ROWS; r++) {
				const int t = L.track[r];
				if (slot[r] < 0)
					continue;
				Instrument p;
				if (t >= 0 && t < (int) L.song.tracks.size()) {
					const GpTrack& track = L.song.tracks[(size_t) t];
					p.valid = true;
					p.program = track.midiProgram;
					p.percussion = track.percussion;
					p.capo = track.capo;
					p.stringCount = (uint8_t) std::min<size_t>(Instrument::MAX_STRINGS,
						track.tuning.size());
					for (int k = 0; k < (int) p.stringCount; k++)
						p.tuning[k] = (uint8_t) math::clamp(track.tuning[(size_t) k], 0, 127);
					p.setName(track.name);
				}
				p.change = partChange;
				busPublishInstrument(slot[r], p);
			}
		}

		// EVERY NOTE WHOSE MOMENT HAS COME. A note carries its own length, so there is no
		// note-off to keep track of: the far end is told how long it lasts when it starts.
		bool struck = false;
		for (int r = 0; r < ROWS; r++) {
			const int t = L.track[r];
			if (t < 0 || t >= (int) L.line.tracks.size() || slot[r] < 0)
				continue;
			const std::vector<GpPlayedNote>& notes = L.line.tracks[(size_t) t];
			const bool muted = params[P_MUTE + r].getValue() > 0.5f;
			while (cursor[r] < notes.size() && notes[cursor[r]].startQuarters <= quarters) {
				const GpPlayedNote& n = notes[cursor[r]];
				cursor[r]++;
				if (muted)
					continue;
				Event e;
				e.kind = Event::ON;
				e.handle = mintHandle();
				// Volts, an octave to the volt, with nought at middle C.
				e.pitch = (float) (n.note.midi - 60) / 12.f;
				e.level = math::clamp(n.dynamic, 0.f, 1.f);
				// The length at the tempo it is being played at, which is the clock's when one
				// is patched and the file's when none is.
				e.duration = math::clamp((float) n.lengthQuarters * quarterSeconds,
					MIN_SECONDS, MAX_SECONDS);
				describe(e, n, L.song.tracks[(size_t) t].letRingThroughout);
				busPush(slot[r], e);
				struck = true;
			}
		}

		beatLamp = struck ? 1.f : std::fmax(0.f, beatLamp - args.sampleTime * 4.f);
		lights[L_BEAT].setBrightness(beatLamp);
	}
};


// ---- the panel ---------------------------------------------------------------------------------

/** The name of the track each row carries, drawn where a jack's label would be. It is part of
the panel rather than of the layout because it changes with the song. */
struct SongRows : widget::Widget {
	SongModule* module = NULL;
	float rowY[SHOWN] = {};
	float left = 0.f;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 8.f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

		const Playing* L = module ? &module->loaded[module->live.load()] : NULL;
		for (int r = 0; r < SHOWN; r++) {
			std::string name = "—";
			if (L && module->haveSong.load()) {
				const int t = L->track[r];
				if (t >= 0 && t < (int) L->song.tracks.size()) {
					name = L->song.tracks[(size_t) t].name;
					if (name.empty())
						name = string::f("Track %d", t + 1);
				}
			}
			nvgFillColor(vg, PANEL_INK);
			for (int pass = 0; pass < 2; pass++)
				nvgText(vg, left, rowY[r], name.c_str(), NULL);
		}
	}
};


/** The song's name, and a press opens the file chooser. */
struct SongTitle : widget::OpaqueWidget {
	SongModule* module = NULL;

	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			choose();
			e.consume(this);
			return;
		}
		widget::OpaqueWidget::onButton(e);
	}

	void choose() {
		if (!module)
			return;
		osdialog_filters* filters = osdialog_filters_parse(
			"Guitar Pro:gp,gpx,gp5,gp4,gp3,gpif,xml");
		char* path = osdialog_file(OSDIALOG_OPEN, NULL, NULL, filters);
		osdialog_filters_free(filters);
		if (!path)
			return;
		std::string why;
		const bool ok = module->setSongFile(path, &why);
		std::free(path);
		if (!ok) {
			osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK,
				("That file could not be read.\n\n" + why).c_str());
		}
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 3.f);
		nvgFillColor(vg, nvgRGB(0x2a, 0x2f, 0x36));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(0xcf, 0xcf, 0xcf, 0x90));
		nvgStrokeWidth(vg, 1.3f);
		nvgStroke(vg);

		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;
		const bool have = module && module->haveSong.load();
		std::string text = "LOAD A SONG";
		if (have)
			text = module->loaded[module->live.load()].title;

		nvgFontFaceId(vg, font->handle);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		float size = 10.f;
		float bounds[4];
		for (; size > 5.f; size -= 0.5f) {
			nvgFontSize(vg, size);
			nvgTextBoxBounds(vg, 0.f, 0.f, box.size.x - 6.f, text.c_str(), NULL, bounds);
			if (bounds[3] - bounds[1] <= box.size.y - 4.f)
				break;
		}
		nvgFillColor(vg, nvgRGB(0xff, 0xff, 0xff));
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		const float h = bounds[3] - bounds[1];
		for (int pass = 0; pass < 2; pass++)
			nvgTextBox(vg, 3.f, std::round((box.size.y - h) / 2.f), box.size.x - 6.f,
				text.c_str(), NULL);
	}
};


/** Which bar of the song is sounding, and at what tempo. */
struct SongPosition : widget::Widget {
	SongModule* module = NULL;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0 || !module || !module->haveSong.load())
			return;
		const Playing& L = module->loaded[module->live.load()];
		const int at = math::clamp(module->playedBar.load(), 0, (int) L.line.bars.size() - 1);
		const GpPlayedBar& bar = L.line.bars[(size_t) at];
		std::string text = string::f("BAR %d OF %d", bar.written + 1,
			(int) L.song.masterBars.size());
		if (bar.pass > 1)
			text += string::f("  (%d)", bar.pass);
		text += string::f("      %.0f BPM", module->soundingBpm.load());

		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 8.f);
		nvgFillColor(vg, PANEL_INK);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		for (int pass = 0; pass < 2; pass++)
			nvgText(vg, 0.f, box.size.y / 2.f, text.c_str(), NULL);
	}
};


static const float PANEL_W = 60.96f;      /**< Twelve HP. */
static const float MUTE_X = 8.f;
static const float JACK_X = 20.f;
static const float NAME_X = 28.f;
static const float ROW_TOP = 66.f;
static const float ROW_STEP = 11.f;


static Layout songLayout() {
	Layout L;
	L.hp = 12.f;
	L.title = "mpxGuitarChart";

	auto label = [&](const char* key, float x, float y, const char* text, Panel::Align align,
			bool heading, float size, const char* owner) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.defaultText = text; i.align = align; i.heading = heading; i.size = size;
		if (owner)
			i.owner = owner;
		L.items.push_back(i);
	};
	auto jack = [&](const char* key, Item::Kind kind, float x, float y, int id,
			const char* name, NVGcolor color) {
		Item i;
		i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = color;
		L.items.push_back(i);
		if (name && name[0])
			label((std::string(key) + ".label").c_str(), x, y + 7.5f, name,
				Panel::CENTRE, false, 0.f, key);
	};

	// The song's name, which is also the way one is chosen. Clear of the band the module's own
	// name is written in, which is seventeen millimetres deep.
	Item title;
	title.key = "d.title"; title.kind = Item::DISPLAY;
	title.x = 3.f; title.y = 24.f; title.w = PANEL_W - 6.f; title.h = 13.f;
	L.items.push_back(title);

	Item where;
	where.key = "d.where"; where.kind = Item::DISPLAY;
	where.x = 4.f; where.y = 38.5f; where.w = PANEL_W - 8.f; where.h = 6.f;
	L.items.push_back(where);

	// The transport, the tempo, and the two jacks that can drive both from outside.
	Item play;
	play.key = "p.play"; play.kind = Item::PARAM; play.id = SongModule::P_PLAY;
	play.style = "transport.play"; play.x = 10.f; play.y = 50.f;
	L.items.push_back(play);
	label("p.play.label", 10.f, 56.f, "PLAY", Panel::CENTRE, true, 0.f, "p.play");

	Item rewind;
	rewind.key = "p.rewind"; rewind.kind = Item::PARAM; rewind.id = SongModule::P_REWIND;
	rewind.style = "transport.rewind"; rewind.x = 21.f; rewind.y = 50.f;
	L.items.push_back(rewind);
	label("p.rewind.label", 21.f, 56.f, "REWIND", Panel::CENTRE, true, 0.f, "p.rewind");

	// A READOUT RATHER THAN A KNOB, as on mpxChart: a tempo is a number people say out loud,
	// and a knob's position is a poor way to read one. Clicking the plate opens the list.
	Item tempo;
	tempo.key = "p.tempo"; tempo.kind = Item::PARAM; tempo.id = SongModule::P_TEMPO;
	tempo.style = "readout"; tempo.x = 34.f; tempo.y = 49.5f; tempo.chars = 3; tempo.h = 6.5f;
	L.items.push_back(tempo);
	label("p.tempo.label", 34.f, 56.f, "BPM", Panel::CENTRE, true, 0.f, "p.tempo");

	jack("in.clock", Item::PORT_IN, 45.f, 49.5f, SongModule::I_CLOCK, "clock", SIG_GATE);
	jack("in.reset", Item::PORT_IN, 54.f, 49.5f, SongModule::I_RESET, "reset", SIG_GATE);

	Item lamp;
	lamp.key = "lamp.beat"; lamp.kind = Item::LIGHT; lamp.id = SongModule::L_BEAT;
	lamp.x = 3.5f; lamp.y = 50.f;
	L.items.push_back(lamp);

	// THE ROWS: a mute, a cable and the name of the instrument on it. The names are drawn by
	// the module, since they change with the song.
	for (int r = 0; r < SHOWN; r++) {
		const float y = ROW_TOP + r * ROW_STEP;
		Item mute;
		mute.key = string::f("p.mute%d", r + 1);
		mute.kind = Item::PARAM; mute.id = SongModule::P_MUTE + r;
		mute.style = "latch"; mute.diameter = 6.6f; mute.x = MUTE_X; mute.y = y;
		L.items.push_back(mute);

		Item out;
		out.key = string::f("out.track%d", r + 1);
		out.kind = Item::PORT_OUT; out.id = SongModule::O_TRACK + r;
		out.x = JACK_X; out.y = y; out.ring = NOTE_CABLE;
		L.items.push_back(out);
	}
	label("h.mute", MUTE_X, ROW_TOP - 8.f, "MUTE", Panel::CENTRE, true, 0.f, NULL);
	label("h.out", JACK_X, ROW_TOP - 8.f, "mpx OUT", Panel::CENTRE, true, 0.f, NULL);

	Item names;
	names.key = "d.rows"; names.kind = Item::DISPLAY;
	names.x = NAME_X; names.y = ROW_TOP - 5.5f;
	names.w = PANEL_W - NAME_X - 2.f; names.h = SHOWN * ROW_STEP;
	L.items.push_back(names);

	L.bindOffsets();
	return L;
}


struct SongWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;
	SongTitle* titleDisplay = NULL;

	SongWidget(SongModule* module) {
		setModule(module);
		layout = songLayout();
		layoutApplyUser("mpxGuitarChart", layout);
		panel = new Panel;
		addChild(panel);

		titleDisplay = new SongTitle;
		titleDisplay->module = module;
		layoutPlaceDisplay(this, layout, "d.title", titleDisplay);

		SongPosition* where = new SongPosition;
		where->module = module;
		layoutPlaceDisplay(this, layout, "d.where", where);

		SongRows* rows = new SongRows;
		rows->module = module;
		// The names line up with the jacks, in the display's own coordinates.
		const Item* first = layout.find("out.track1");
		const Item* area = layout.find("d.rows");
		for (int r = 0; r < SHOWN; r++) {
			const float y = (first ? first->y : ROW_TOP) + r * ROW_STEP;
			rows->rowY[r] = mm2px(y - (area ? area->y : ROW_TOP - 5.5f));
		}
		rows->left = 0.f;
		layoutPlaceDisplay(this, layout, "d.rows", rows);

		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxGuitarChart");
		SongModule* m = dynamic_cast<SongModule*>(module);
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuItem("Load a Guitar Pro file…", "", [=]() {
			if (titleDisplay)
				titleDisplay->choose();
		}));
		if (!m->filePath.empty())
			menu->addChild(createMenuLabel(m->filePath));
	}

	/** Magenta means the link works: see mpxIn, which says why at length. */
	void step() override {
		ModuleWidget::step();
		if (!module)
			return;
		for (int r = 0; r < ROWS; r++) {
			PortWidget* port = getOutput(SongModule::O_TRACK + r);
			if (!port)
				continue;
			for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
				engine::Cable* cable = cw->getCable();
				if (cable && isMPXInput(cable->inputModule, cable->inputId))
					cw->color = NOTE_CABLE;
			}
		}
	}
};


} // namespace px


Model* modelMpxGuitarChart =
	createModel<px::SongModule, px::SongWidget>("mpxGuitarChart");
