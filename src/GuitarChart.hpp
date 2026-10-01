#pragma once
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

TWO MODULES FROM THIS ONE FILE. mpxGuitarChart6 and mpxGuitarChart (the twelve) are this code
compiled twice, by mpxGuitarChart6.cpp and mpxGuitarChart.cpp, each saying how many rows it has
and whether it plays its own tracks. Each copy is in a namespace of its own, so the two copies of
SongModule are two types and not one type defined twice.

A BAND OF ITS OWN, where CHART_AUDIO says so. The rows play through the shared synthesiser
(SoundCore.hpp) on the instruments the file names, each with a gain and a mono output, and the
whole band on a stereo pair. Shown and run only when asked for, from the menu. A row whose MPX
output has a cable in it is the cable's to voice instead: its notes go down the cable and the
synthesiser leaves it alone.
*/
#include "plugin.hpp"
#include "GpTimeline.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"
#if CHART_AUDIO
#include "SoundCore.hpp"
#endif

#include <osdialog.h>

#include <atomic>
#include <cctype>
#include <cstring>

namespace px {
namespace CHART_NS {


/** HOW MANY ROWS THE MODULE HAS: six in one column, or twelve in two columns of six. */
static const int ROWS = CHART_ROWS;
static const int ROWS_PER_COLUMN = 6;
static const int COLUMNS = ROWS / ROWS_PER_COLUMN;


/** THE GENERAL MIDI INSTRUMENTS, by program number, for the line under a track's name. The
standard's own names, since the bank may not be loaded when the panel is drawn. */
static const char* const GM_NAMES[128] = {
	"Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
	"Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
	"Celesta", "Glockenspiel", "Music Box", "Vibraphone",
	"Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
	"Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
	"Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
	"Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar",
	"Muted Guitar", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
	"Acoustic Bass", "Finger Bass", "Picked Bass", "Fretless Bass",
	"Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
	"Violin", "Viola", "Cello", "Contrabass",
	"Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
	"String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2",
	"Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
	"Trumpet", "Trombone", "Tuba", "Muted Trumpet",
	"French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
	"Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
	"Oboe", "English Horn", "Bassoon", "Clarinet",
	"Piccolo", "Flute", "Recorder", "Pan Flute",
	"Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
	"Square Lead", "Sawtooth Lead", "Calliope Lead", "Chiff Lead",
	"Charang Lead", "Voice Lead", "Fifths Lead", "Bass and Lead",
	"New Age Pad", "Warm Pad", "Polysynth Pad", "Choir Pad",
	"Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
	"Rain", "Soundtrack", "Crystal", "Atmosphere",
	"Brightness", "Goblins", "Echoes", "Sci-fi",
	"Sitar", "Banjo", "Shamisen", "Koto",
	"Kalimba", "Bagpipe", "Fiddle", "Shanai",
	"Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
	"Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
	"Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
	"Telephone Ring", "Helicopter", "Applause", "Gunshot",
};

static std::string instrumentName(const GpTrack& track) {
	if (track.percussion)
		return "Drum kit";
	return GM_NAMES[std::max(0, std::min(127, track.midiProgram))];
}

#if CHART_AUDIO
/** From the synthesiser's output to Rack's ten volts peak: a General MIDI bank is quiet on
purpose, so that a whole orchestra fits without clipping. */
static const float AUDIO_LEVEL = 5.f;
#endif

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
		P_GAIN = P_MUTE + ROWS,
#if CHART_AUDIO
		NUM_PARAMS = P_GAIN + ROWS
#else
		NUM_PARAMS = P_GAIN
#endif
	};
	enum InputId {
		I_CLOCK,
		I_RESET,
		NUM_INPUTS
	};
	enum OutputId {
		O_TRACK,
		O_AUDIO = O_TRACK + ROWS,
#if CHART_AUDIO
		O_MIX_L = O_AUDIO + ROWS,
		O_MIX_R,
		NUM_OUTPUTS
#else
		NUM_OUTPUTS = O_AUDIO
#endif
	};
	enum LightId {
		L_BEAT,
		/** One a row, lit by each note the row sends. */
		L_ROW,
		NUM_LIGHTS = L_ROW + ROWS
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

	/** A row given another track, whose place in that track's notes has to be found again. */
	std::atomic<bool> reseek[ROWS];

#if CHART_AUDIO
	SoundCore core;
	/** Whether the audio outputs are shown, and so whether the synthesiser runs. */
	std::atomic<bool> showAudio{false};
	/** The bank a patch asked for, used when the synthesiser is next started. */
	std::string wantBank;
#endif

	dsp::SchmittTrigger clockTrigger, resetTrigger, rewindTrigger;
	float sinceLastPulse = 0.f;
	float quarterSeconds = 0.5f;
	bool awaitPulse = false;
	bool wasRunning = false;
	float beatLamp = 0.f;
	float rowLamp[ROWS] = {};


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

		for (int r = 0; r < ROWS; r++) {
			slot[r] = -1;
			reseek[r] = false;
		}
#if CHART_AUDIO
		for (int r = 0; r < ROWS; r++) {
			//? This track's loudness, on its own output and in the stereo pair.
			configParam(P_GAIN + r, 0.f, 2.f, 1.f, string::f("Gain %d", r + 1), "%", 0.f, 100.f);
			configOutput(O_AUDIO + r, string::f("Track %d audio", r + 1));
		}
		configOutput(O_MIX_L, "Band left");
		configOutput(O_MIX_R, "Band right");
		core.setParts(ROWS);
		core.logName = CHART_SLUG;
		core.followInstrument = true;
		core.readRules();
#endif
	}

#if CHART_AUDIO
	// ---- the band ------------------------------------------------------------------------------

	/** Starts or stops the synthesiser, with the outputs. Main thread. */
	void setShowAudio(bool on) {
		if (on == showAudio.load())
			return;
		showAudio = on;
		if (on)
			startEngine(APP->engine->getSampleRate());
		else
			core.stopEngine();
	}

	void startEngine(float rate) {
		core.startEngine(rate, false, false, wantBank);
	}

	void onAdd(const AddEvent& e) override {
		if (showAudio.load())
			startEngine(APP->engine->getSampleRate());
	}

	void onRemove(const RemoveEvent& e) override {
		core.stopEngine();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		if (showAudio.load())
			startEngine(e.sampleRate);
	}

	/** Whether any audio output has a cable in it, which keeps them from being hidden. */
	bool audioPatched() {
		for (int o = O_AUDIO; o < NUM_OUTPUTS; o++) {
			if (outputs[o].isConnected())
				return true;
		}
		return false;
	}

	/** THE ROWS THE SYNTHESISER PLAYS, every frame: each row with a track and no cable in its MPX
	output. The bus is read exactly as a cable's far end reads it. */
	void playBand(const ProcessArgs& args) {
		if (!showAudio.load() || !core.engine.running()) {
			for (int o = O_AUDIO; o < NUM_OUTPUTS; o++)
				outputs[o].setVoltage(0.f);
			return;
		}
		for (int r = 0; r < ROWS; r++) {
			int s = slot[r];
			uint32_t g = generation[r];
			const bool ours = s >= 0 && !outputs[O_TRACK + r].isConnected();
			core.link(r, &s, &g, ours ? 1 : 0);
		}
		// HUMANISE AT ITS OWN AMOUNT, and no reverb or chorus: those are better done by a
		// processor after the outputs.
		core.step(args.sampleTime, NULL, 1.f, false, false);
		float left = 0.f, right = 0.f;
		for (int r = 0; r < ROWS; r++) {
			const float gain = params[P_GAIN + r].getValue();
			const float l = core.partLeft(r) * gain;
			const float rr = core.partRight(r) * gain;
			left += l;
			right += rr;
			// Both sides as one, at the level a centred sound has on either side.
			outputs[O_AUDIO + r].setVoltage((l + rr) * 0.7071f * AUDIO_LEVEL);
		}
		outputs[O_MIX_L].setVoltage(left * AUDIO_LEVEL);
		outputs[O_MIX_R].setVoltage(right * AUDIO_LEVEL);
	}
#endif

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

	/** The name of the track a row carries, or an empty string when it carries none. Main
	thread. */
	std::string rowName(int r) const {
		if (!haveSong.load())
			return "";
		const Playing& L = loaded[live.load()];
		const int t = L.track[r];
		if (t < 0 || t >= (int) L.song.tracks.size())
			return "";
		std::string name = string::trim(L.song.tracks[(size_t) t].name);
		return name.empty() ? string::f("Track %d", t + 1) : name;
	}

	/** The General MIDI instrument of the track a row carries, or an empty string. Main
	thread. */
	std::string rowInstrument(int r) const {
		if (!haveSong.load())
			return "";
		const Playing& L = loaded[live.load()];
		const int t = L.track[r];
		if (t < 0 || t >= (int) L.song.tracks.size())
			return "";
		return instrumentName(L.song.tracks[(size_t) t]);
	}

	/** THE WHOLE NAME IS IN THE TOOLTIP, since the panel has room for only two short lines of
	it. The word MPX stays, for the reason given where the outputs are configured. */
	void nameOutputs() {
		for (int r = 0; r < ROWS; r++) {
			const std::string name = rowName(r);
			outputInfos[O_TRACK + r]->name = name.empty()
				? string::f("MPX track %d", r + 1)
				: string::f("MPX track %d: %s (%s)", r + 1, name.c_str(),
					rowInstrument(r).c_str());
		}
	}

	/** GIVES A ROW ANOTHER OF THE SONG'S TRACKS, or none at -1. Main thread. The row's place in
	its new track is found on the audio thread, and the instrument is published again. */
	void chooseTrack(int r, int t) {
		if (r < 0 || r >= ROWS || !haveSong.load())
			return;
		Playing& L = loaded[live.load()];
		if (t < -1 || t >= (int) L.line.tracks.size())
			return;
		L.track[r] = t;
		reseek[r] = true;
		claimBuses(L);
		L.partChange++;
		nameOutputs();
	}

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
		nameOutputs();
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
#if CHART_AUDIO
		json_object_set_new(rootJ, "audio", json_boolean(showAudio.load()));
		if (!core.engine.bankPath().empty())
			json_object_set_new(rootJ, "bank", json_string(core.engine.bankPath().c_str()));
#endif
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
#if CHART_AUDIO
		json_t* bankJ = json_object_get(rootJ, "bank");
		if (json_is_string(bankJ))
			wantBank = json_string_value(bankJ);
		// Shown before onAdd, which starts the synthesiser for a patch that had it running.
		showAudio = json_is_true(json_object_get(rootJ, "audio"));
#endif
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
		nameOutputs();
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
		playSong(args);
#if CHART_AUDIO
		playBand(args);
#endif
	}

	void playSong(const ProcessArgs& args) {
		// A note cable carries no voltage: it is a real cable so that Rack owns it, and the
		// notes travel through the bus.
		for (int r = 0; r < ROWS; r++) {
			outputs[O_TRACK + r].setChannels(1);
			outputs[O_TRACK + r].setVoltage(busFlashVolts(slot[r]));
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
					p.pan = track.pan;
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
		// A ROW GIVEN ANOTHER TRACK starts where the music is, not at the track's beginning.
		for (int r = 0; r < ROWS; r++) {
			if (!reseek[r].exchange(false))
				continue;
			const int t = L.track[r];
			cursor[r] = 0;
			if (t >= 0 && t < (int) L.line.tracks.size()) {
				const std::vector<GpPlayedNote>& notes = L.line.tracks[(size_t) t];
				while (cursor[r] < notes.size() && notes[cursor[r]].startQuarters < quarters)
					cursor[r]++;
			}
		}

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
				rowLamp[r] = 1.f;
			}
		}

		beatLamp = struck ? 1.f : std::fmax(0.f, beatLamp - args.sampleTime * 4.f);
		lights[L_BEAT].setBrightness(beatLamp);
		for (int r = 0; r < ROWS; r++) {
			lights[L_ROW + r].setBrightness(rowLamp[r]);
			rowLamp[r] = std::fmax(0.f, rowLamp[r] - args.sampleTime * 4.f);
		}
	}
};


// ---- the panel ---------------------------------------------------------------------------------

/** The names of the tracks one column of rows carries, drawn beside the jacks. It is part of the
panel rather than of the layout because it changes with the song.

TWO LINES AT MOST, broken between words. What does not fit ends in an ellipsis; the whole name is
in the jack's tooltip. */
struct SongRows : widget::OpaqueWidget {
	SongModule* module = NULL;
	int first = 0;
	float rowY[ROWS_PER_COLUMN] = {};
	/** Where the names start, in pixels from the display's left edge: further right while the
	audio controls are shown in front of them. */
	float textX = 0.f;

	int rowAt(float y) const {
		const float half = (ROWS_PER_COLUMN > 1) ? (rowY[1] - rowY[0]) / 2.f : box.size.y;
		for (int i = 0; i < ROWS_PER_COLUMN; i++) {
			if (std::fabs(y - rowY[i]) < half)
				return i;
		}
		return -1;
	}

	/** A PRESS ON A NAME OPENS THE SONG'S TRACKS, so a row can carry any of them rather than
	the one with the most notes it was given. */
	void onButton(const ButtonEvent& e) override {
		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && module
				&& module->haveSong.load() && e.pos.x >= textX) {
			const int i = rowAt(e.pos.y);
			if (i >= 0) {
				showTracks(first + i);
				e.consume(this);
				return;
			}
		}
		widget::OpaqueWidget::onButton(e);
	}

	void showTracks(int r) {
		SongModule* m = module;
		const Playing& L = m->loaded[m->live.load()];
		ui::Menu* menu = createMenu();
		menu->addChild(createMenuLabel(string::f("Row %d plays", r + 1)));
		menu->addChild(createCheckMenuItem("Nothing", "",
			[=]() { return m->loaded[m->live.load()].track[r] < 0; },
			[=]() { m->chooseTrack(r, -1); }));
		for (size_t t = 0; t < L.song.tracks.size(); t++) {
			const GpTrack& track = L.song.tracks[t];
			std::string name = string::trim(track.name);
			if (name.empty())
				name = string::f("Track %d", (int) t + 1);
			const int which = (int) t;
			menu->addChild(createCheckMenuItem(
				string::f("%d. %s, %s", which + 1, name.c_str(), instrumentName(track).c_str()), "",
				[=]() { return m->loaded[m->live.load()].track[r] == which; },
				[=]() { m->chooseTrack(r, which); }));
		}
	}

	/** As much of the text as fits in the width, with an ellipsis if any was cut. */
	static std::string fit(NVGcontext* vg, const std::string& text, float width) {
		if (nvgTextBounds(vg, 0.f, 0.f, text.c_str(), NULL, NULL) <= width)
			return text;
		static const std::string DOTS = "\u2026";
		std::string cut = text;
		while (!cut.empty()) {
			cut.pop_back();
			// Never leave half of a multi-byte character.
			while (!cut.empty() && ((unsigned char) cut.back() & 0xC0) == 0x80)
				cut.pop_back();
			if (!cut.empty() && ((unsigned char) cut.back() & 0xC0) == 0xC0)
				cut.pop_back();
			const std::string tried = string::trim(cut) + DOTS;
			if (nvgTextBounds(vg, 0.f, 0.f, tried.c_str(), NULL, NULL) <= width)
				return tried;
		}
		return DOTS;
	}

	/** Breaks the name into at most two lines, whole words on the first. */
	static void wrap(NVGcontext* vg, const std::string& text, float width,
			std::string& one, std::string& two) {
		one.clear();
		two.clear();
		std::vector<std::string> words;
		std::string word;
		for (char c : text) {
			if (c == ' ') {
				if (!word.empty())
					words.push_back(word);
				word.clear();
			}
			else
				word += c;
		}
		if (!word.empty())
			words.push_back(word);
		size_t used = 0;
		for (; used < words.size(); used++) {
			const std::string tried = one.empty() ? words[used] : one + " " + words[used];
			if (nvgTextBounds(vg, 0.f, 0.f, tried.c_str(), NULL, NULL) > width)
				break;
			one = tried;
		}
		if (one.empty()) {
			// The first word alone is wider than the column.
			one = fit(vg, text, width);
			return;
		}
		for (size_t w = used; w < words.size(); w++)
			two += (two.empty() ? "" : " ") + words[w];
		if (!two.empty())
			two = fit(vg, two, width);
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font || font->handle < 0)
			return;
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 8.f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		float lineHeight = 0.f;
		nvgTextMetrics(vg, NULL, NULL, &lineHeight);

		for (int i = 0; i < ROWS_PER_COLUMN; i++) {
			std::string name = module ? module->rowName(first + i) : "";
			if (name.empty())
				name = "\u2014";
			std::string one, two;
#if CHART_AUDIO
			// THE TRACK'S NAME, AND UNDER IT ITS INSTRUMENT, each on a line of its own.
			const float width = box.size.x - textX;
			one = fit(vg, name, width);
			if (module)
				two = fit(vg, module->rowInstrument(first + i), width);
#else
			wrap(vg, name, box.size.x, one, two);
#endif
			nvgFillColor(vg, PANEL_INK);
			for (int pass = 0; pass < 2; pass++) {
				if (two.empty())
					nvgText(vg, textX, rowY[i], one.c_str(), NULL);
				else {
					nvgText(vg, textX, rowY[i] - lineHeight / 2.f, one.c_str(), NULL);
					nvgText(vg, textX, rowY[i] + lineHeight / 2.f, two.c_str(), NULL);
				}
			}
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
/** TWO COLUMNS, each half the panel: a mute, the jack beside it, and the name, with a millimetre
between each and at the edges. The jack is 8.03 mm across and the mute 5.6 mm, which leaves the
name about twelve millimetres. */
static const float COLUMN_W = PANEL_W / 2.f;
static const float MUTE_D = 5.6f;
static const float MUTE_DX = 1.5f + MUTE_D / 2.f;
/** The jack a millimetre from the mute. */
static const float JACK_DX = MUTE_DX + MUTE_D / 2.f + 1.f + 8.03f / 2.f;
/** THE ROW'S LAMP, tucked against its jack at forty-five degrees below and to the left, in the
space between the rows rather than in the row: a small light is 2.2 mm across, and set between
the mute and the jack it took that much from every name. */
static const float LAMP_OFFSET = (8.03f / 2.f + 0.6f + 1.1f) * 0.7071f;
static const float LAMP_DX = JACK_DX - LAMP_OFFSET;
static const float LAMP_DY = LAMP_OFFSET;
static const float NAME_DX = JACK_DX + 8.03f / 2.f + 1.f;
static const float NAME_W = COLUMN_W - NAME_DX - 1.f;
static const float ROW_TOP = 57.f;
static const float ROW_STEP = 12.f;

/** SIX ROWS IN ONE COLUMN: a mute, the MPX jack, and — when the audio is shown — the track's gain
and its audio jack, then the names. A millimetre between each. A little closer together than the
twelve's rows, to leave room at the bottom for the band's stereo pair, set apart from them. */
static const float ROW6_TOP = 57.f;
static const float ROW6_STEP = 10.5f;
static const float GAIN6_X = JACK_DX + 8.03f / 2.f + 1.f + 6.05f / 2.f;
static const float AUDIO6_X = GAIN6_X + 6.05f / 2.f + 1.f + 8.03f / 2.f;
/** Where the names start with the audio hidden, and how much further right with it shown. */
static const float NAME6_X = JACK_DX + 8.03f / 2.f + 1.f;
static const float NAME6_SHIFT = AUDIO6_X + 8.03f / 2.f + 1.f - NAME6_X;
static const float MIX6_Y = 121.5f;


static Layout songLayout() {
	Layout L;
	L.hp = 12.f;
	L.title = CHART_TITLE;

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
	// name is written in, which ends seven millimetres down: the band is seventeen PIXELS deep
	// below a four-pixel margin. Read as millimetres, it left a sixth of the panel empty.
	Item title;
	title.key = "d.title"; title.kind = Item::DISPLAY;
	title.x = 3.f; title.y = 10.f; title.w = PANEL_W - 6.f; title.h = 13.f;
	L.items.push_back(title);

	Item where;
	where.key = "d.where"; where.kind = Item::DISPLAY;
	where.x = 4.f; where.y = 24.5f; where.w = PANEL_W - 8.f; where.h = 6.f;
	L.items.push_back(where);

	// The transport, the tempo, and the two jacks that can drive both from outside.
	Item play;
	play.key = "p.play"; play.kind = Item::PARAM; play.id = SongModule::P_PLAY;
	play.style = "transport.play"; play.x = 10.f; play.y = 36.f;
	L.items.push_back(play);
	label("p.play.label", 10.f, 42.f, "PLAY", Panel::CENTRE, true, 0.f, "p.play");

	Item rewind;
	rewind.key = "p.rewind"; rewind.kind = Item::PARAM; rewind.id = SongModule::P_REWIND;
	rewind.style = "transport.rewind"; rewind.x = 21.f; rewind.y = 36.f;
	L.items.push_back(rewind);
	label("p.rewind.label", 21.f, 42.f, "REWIND", Panel::CENTRE, true, 0.f, "p.rewind");

	// A READOUT RATHER THAN A KNOB, as on mpxChart: a tempo is a number people say out loud,
	// and a knob's position is a poor way to read one. Clicking the plate opens the list.
	Item tempo;
	tempo.key = "p.tempo"; tempo.kind = Item::PARAM; tempo.id = SongModule::P_TEMPO;
	tempo.style = "readout"; tempo.x = 34.f; tempo.y = 35.5f; tempo.chars = 3; tempo.h = 6.5f;
	L.items.push_back(tempo);
	label("p.tempo.label", 34.f, 42.f, "BPM", Panel::CENTRE, true, 0.f, "p.tempo");

	jack("in.clock", Item::PORT_IN, 45.f, 35.5f, SongModule::I_CLOCK, "clock", SIG_GATE);
	jack("in.reset", Item::PORT_IN, 54.f, 35.5f, SongModule::I_RESET, "reset", SIG_GATE);

	Item lamp;
	lamp.key = "lamp.beat"; lamp.kind = Item::LIGHT; lamp.id = SongModule::L_BEAT;
	lamp.x = 3.5f; lamp.y = 36.f;
	L.items.push_back(lamp);

#if CHART_ROWS == 6
	// THE ROWS, one column of six. The names are drawn by the module, since they change with the
	// song; the gains, the audio jacks and their headings are shown only with the audio.
	for (int r = 0; r < ROWS; r++) {
		const float y = ROW6_TOP + r * ROW6_STEP;
		Item mute;
		mute.key = string::f("p.mute%d", r + 1);
		mute.kind = Item::PARAM; mute.id = SongModule::P_MUTE + r;
		mute.style = "latch"; mute.diameter = MUTE_D; mute.x = MUTE_DX; mute.y = y;
		L.items.push_back(mute);

		Item lamp;
		lamp.key = string::f("lamp.row%d", r + 1);
		lamp.kind = Item::LIGHT; lamp.id = SongModule::L_ROW + r;
		lamp.x = LAMP_DX; lamp.y = y + LAMP_DY;
		L.items.push_back(lamp);

		Item out;
		out.key = string::f("out.track%d", r + 1);
		out.kind = Item::PORT_OUT; out.id = SongModule::O_TRACK + r;
		out.x = JACK_DX; out.y = y; out.ring = NOTE_CABLE;
		L.items.push_back(out);

		Item gain;
		gain.key = string::f("p.gain%d", r + 1);
		gain.kind = Item::PARAM; gain.id = SongModule::P_GAIN + r;
		gain.style = "knob.trim"; gain.x = GAIN6_X; gain.y = y;
		L.items.push_back(gain);

		Item audio;
		audio.key = string::f("out.audio%d", r + 1);
		audio.kind = Item::PORT_OUT; audio.id = SongModule::O_AUDIO + r;
		audio.x = AUDIO6_X; audio.y = y; audio.ring = SIG_AUDIO;
		L.items.push_back(audio);
	}
	// SEVEN POINTS, each over its own column. At eight, MUTE reached within a quarter of a
	// millimetre of MPX once the jack moved up to the mute.
	const float headY = ROW6_TOP - 7.f;
	label("h.mute", MUTE_DX, headY, "MUTE", Panel::CENTRE, true, 7.f, NULL);
	label("h.out", JACK_DX, headY, "MPX", Panel::CENTRE, true, 7.f, NULL);
	label("h.gain", GAIN6_X, headY, "GAIN", Panel::CENTRE, true, 7.f, NULL);
	label("h.audio", AUDIO6_X, headY, "OUT", Panel::CENTRE, true, 7.f, NULL);

	Item names;
	names.key = "d.rows"; names.kind = Item::DISPLAY;
	names.x = NAME6_X; names.y = ROW6_TOP - ROW6_STEP / 2.f;
	names.w = PANEL_W - NAME6_X - 1.f; names.h = ROWS * ROW6_STEP;
	L.items.push_back(names);

	// THE WHOLE BAND, left and right, under the tracks' own outputs.
	label("h.mix", AUDIO6_X - 8.03f / 2.f - 1.f, MIX6_Y, "MIX", Panel::RIGHT, true, 8.f, NULL);
	Item mixL;
	mixL.key = "out.mixl"; mixL.kind = Item::PORT_OUT; mixL.id = SongModule::O_MIX_L;
	mixL.x = AUDIO6_X; mixL.y = MIX6_Y; mixL.ring = SIG_AUDIO;
	L.items.push_back(mixL);
	label("out.mixl.label", AUDIO6_X + 8.03f / 2.f + 1.f, MIX6_Y, "L", Panel::LEFT, true, 8.f,
		"out.mixl");
	const float rightX = AUDIO6_X + 12.5f;
	Item mixR;
	mixR.key = "out.mixr"; mixR.kind = Item::PORT_OUT; mixR.id = SongModule::O_MIX_R;
	mixR.x = rightX; mixR.y = MIX6_Y; mixR.ring = SIG_AUDIO;
	L.items.push_back(mixR);
	label("out.mixr.label", rightX + 8.03f / 2.f + 1.f, MIX6_Y, "R", Panel::LEFT, true, 8.f,
		"out.mixr");
#else
	// THE ROWS, in two columns of six: a mute, a cable and the name of the instrument on it.
	// The names are drawn by the module, since they change with the song.
	for (int c = 0; c < 2; c++) {
		const float x = c * COLUMN_W;
		for (int i = 0; i < ROWS_PER_COLUMN; i++) {
			const int r = c * ROWS_PER_COLUMN + i;
			const float y = ROW_TOP + i * ROW_STEP;
			Item mute;
			mute.key = string::f("p.mute%d", r + 1);
			mute.kind = Item::PARAM; mute.id = SongModule::P_MUTE + r;
			mute.style = "latch"; mute.diameter = MUTE_D; mute.x = x + MUTE_DX; mute.y = y;
			L.items.push_back(mute);

			Item lamp;
			lamp.key = string::f("lamp.row%d", r + 1);
			lamp.kind = Item::LIGHT; lamp.id = SongModule::L_ROW + r;
			lamp.x = x + LAMP_DX; lamp.y = y + LAMP_DY;
			L.items.push_back(lamp);

			Item out;
			out.key = string::f("out.track%d", r + 1);
			out.kind = Item::PORT_OUT; out.id = SongModule::O_TRACK + r;
			out.x = x + JACK_DX; out.y = y; out.ring = NOTE_CABLE;
			L.items.push_back(out);
		}
		// Seven points rather than the usual ten, each centred over its own column: any larger
		// and MUTE runs into OUT now the jack sits a millimetre from the mute.
		label(c ? "h.mute2" : "h.mute", x + MUTE_DX, ROW_TOP - 8.f, "MUTE", Panel::CENTRE,
			true, 7.f, NULL);
		label(c ? "h.out2" : "h.out", x + JACK_DX, ROW_TOP - 8.f, "OUT", Panel::CENTRE, true,
			7.f, NULL);

		Item names;
		names.key = c ? "d.rows2" : "d.rows"; names.kind = Item::DISPLAY;
		names.x = x + NAME_DX; names.y = ROW_TOP - ROW_STEP / 2.f;
		names.w = NAME_W; names.h = ROWS_PER_COLUMN * ROW_STEP;
		L.items.push_back(names);
	}

#endif

	L.bindOffsets();
	return L;
}


struct SongWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;
	SongTitle* titleDisplay = NULL;
	SongRows* columns[COLUMNS] = {};
	/** Whether the panel was last arranged with the audio shown. */
	int audioShown = -1;

	SongWidget(SongModule* module) {
		setModule(module);
		layout = songLayout();
		layoutApplyUser(CHART_SLUG, layout);
		panel = new Panel;
		addChild(panel);

		titleDisplay = new SongTitle;
		titleDisplay->module = module;
		layoutPlaceDisplay(this, layout, "d.title", titleDisplay);

		SongPosition* where = new SongPosition;
		where->module = module;
		layoutPlaceDisplay(this, layout, "d.where", where);

		// The names line up with the jacks, in each display's own coordinates.
		for (int c = 0; c < COLUMNS; c++) {
			SongRows* rows = new SongRows;
			columns[c] = rows;
			rows->module = module;
			rows->first = c * ROWS_PER_COLUMN;
			const char* key = c ? "d.rows2" : "d.rows";
			const Item* area = layout.find(key);
			for (int i = 0; i < ROWS_PER_COLUMN; i++) {
				const Item* jack = layout.find(string::f("out.track%d", rows->first + i + 1));
				const float y = jack ? jack->y : ROW_TOP + i * ROW_STEP;
				rows->rowY[i] = mm2px(y - (area ? area->y : ROW_TOP - ROW_STEP / 2.f));
			}
			layoutPlaceDisplay(this, layout, key, rows);
		}

		layoutBuild(this, panel, layout);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, CHART_SLUG);
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
#if CHART_AUDIO
		menu->addChild(new ui::MenuSeparator);
		// NOT HIDDEN UNDER A CABLE. Rack draws a cable to wherever its jack is, and a hidden jack
		// with a cable in it leaves the cable pointing at nothing.
		if (m->showAudio.load() && m->audioPatched())
			menu->addChild(createMenuLabel("Unplug the audio cables to hide the audio outputs"));
		else
			menu->addChild(createCheckMenuItem("Show audio outputs", "",
				[=]() { return m->showAudio.load(); },
				[=]() { m->setShowAudio(!m->showAudio.load()); }));
		if (m->showAudio.load()) {
			menu->addChild(createMenuItem("Choose a SoundFont…", "", [=]() {
				if (m->core.busy.load())
					return;
				std::string dir = SoundCore::bankFolder();
				system::createDirectories(dir);
				osdialog_filters* filters = osdialog_filters_parse("SoundFont:sf2,SF2");
				char* path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, filters);
				osdialog_filters_free(filters);
				if (!path)
					return;
				m->wantBank = path;
				m->core.loadBank(path);
				std::free(path);
			}));
			if (!m->core.engine.bankPath().empty())
				menu->addChild(createMenuLabel(m->core.engine.bankPath()));
		}
#endif
	}

#if CHART_AUDIO
	/** THE AUDIO CONTROLS COME AND GO WITH THE MENU ITEM, the names moving over to make room for
	them. */
	void arrange(bool shown) {
		static const char* LABELS[] = {"h.gain", "h.audio", "h.mix", "out.mixl.label",
			"out.mixr.label", NULL};
		for (int i = 0; LABELS[i]; i++) {
			Item* item = layout.find(LABELS[i]);
			if (item)
				item->hidden = !shown;
		}
		std::vector<std::string> controls = {"out.mixl", "out.mixr"};
		for (int r = 0; r < ROWS; r++) {
			controls.push_back(string::f("p.gain%d", r + 1));
			controls.push_back(string::f("out.audio%d", r + 1));
		}
		for (const std::string& key : controls) {
			Item* item = layout.find(key);
			if (item && item->widget)
				item->widget->visible = shown;
		}
		layoutRefreshPanel(panel, layout);
		for (int c = 0; c < COLUMNS; c++) {
			if (columns[c])
				columns[c]->textX = shown ? mm2px(NAME6_SHIFT) : 0.f;
		}
	}
#endif

	/** Magenta means the link works: see mpxIn, which says why at length. */
	void step() override {
		ModuleWidget::step();
#if CHART_AUDIO
		// In the module browser there is no module, and the audio is shown so it can be seen.
		SongModule* sm = dynamic_cast<SongModule*>(module);
		const bool shown = sm ? sm->showAudio.load() : true;
		if ((int) shown != audioShown) {
			audioShown = shown;
			arrange(shown);
		}
#endif
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


} // namespace CHART_NS
} // namespace px


Model* CHART_MODEL = createModel<px::CHART_NS::SongModule, px::CHART_NS::SongWidget>(CHART_SLUG);
