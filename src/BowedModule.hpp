#pragma once
/** THE BOWED INSTRUMENTS' MODULE, shared by mpxViolin, mpxViola and mpxCello. See docs/bowed-strings.md,
phases 8 and 9.

NOTHING TO PATCH BUT THE PART AND THE MIXER. The strings, the bow, the body and the bowing are all in
here: the strings are digital waveguides bowed through a friction model (BowedString.hpp), on one
bridge and one body (BowedInstrument.hpp), and the player decides which string, which way the bow
goes, how fast, how hard and where, when the bow changes and how, and where the finger vibrates
(BowedPlayer.hpp). This is the cable and the panel around them.

THE PLAYER READS AHEAD, as a string player reading a part does: a score on the cable announces each
note some seconds before it sounds (Coming in NoteBus.hpp), and the player rehearses its start on
threads of its own while the music plays (BowedEngine.hpp). A note nobody announced, from a keyboard
or a generator, is played the moment it arrives, without rehearsing.

ONE INSTRUMENT, ONE PLAYER. A part on the cable is one musician's, a chord in it played as its top
note; the ports play one note at a time, the highest of the gate's channels that are up. A section of
violins is a module for each.

ONE INSTRUMENT A SLUG, each a short file of its own: what instrument it is, and its controls with
their help, which has to sit beside the call that configures each one (helpscan). Everything that
plays is here, once. */
#include "plugin.hpp"
#include "BowedEngine.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"

#include <cstdio>

namespace px {


/** WHICH INSTRUMENT, assembled field by field: what a slug's file says. */
struct BowedKind {
	std::string slug;
	BowedInstrument::Design design;
	BowedPlayer::Habits habits;
	/** FROM THE SOUND A METRE AWAY TO RACK'S VOLTS, set so that the instrument is as loud as mpxPiano
	playing the same markings: measured per instrument, K-weighted bar by bar over a passage, since a
	cello's body puts out three times a violin's and a held note is louder than a struck one at the
	same peak. */
	float voltsPerPascal = 9.5f;
	/** THE HIGHEST NOTE THE STRINGS ARE MADE READY FOR when the part does not say, hertz: the strings
	run as many times faster than the engine as it needs (BowedString::oversampling), and twice as fast
	costs twice as much. */
	float unsaidHighestHz = 1280.f;
	/** WHERE THE BRIGHTNESS KNOB TURNS THE HIGHS DOWN FROM, hertz: the middle of its shelf, a little under
	the instrument's bridge hill, where its brightness is. */
	float brightCornerHz = 2500.f;
};


/** THE BRIGHTNESS KNOB'S FILTER: a second-order high shelf (Bristow-Johnson's), the highs above
`corner` turned down by `db`, nothing below it touched. On the output, after the instrument, so it
changes nothing about how a note is bowed, rehearsed or started. At nought decibels it passes every
sample unchanged. */
struct BowedShelf {
	double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
	double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
	float madeFor = -1.f, rateFor = 0.f;
	void set(float corner, float db, float rate);
	float run(float x) {
		const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
		x2 = x1;
		x1 = x;
		y2 = y1;
		y1 = y;
		return (float) y;
	}
};


/** WHAT THE MODULE PLAYS, WRITTEN DOWN, for finding faults in use: its output to a WAV file, and beside
it a line for every note — how it began, whether it was rehearsed, and how it was heard as played.

THE AUDIO THREAD ONLY HANDS OVER. Samples and notes go into rings of their own, and the main thread
takes them out and writes them, a frame at a time; nothing on the audio thread waits on a disc.
Opened and closed on the main thread too, which the audio thread is told through `open`. */
struct BowedRecorder {
	/** About forty seconds of samples at 48 kHz: the main thread empties it many times a second. */
	static const uint32_t SAMPLES = 1u << 21;
	static const uint32_t NOTES = 512;
	struct NoteLine {
		BowedPlayer::Played played;
		/** Seconds into the recording the note began, and into the piece where the source says. */
		double sinceStart = 0.0;
		float pieceSeconds = -1.f;
	};
	std::vector<float> samples = std::vector<float>(SAMPLES);
	std::vector<NoteLine> notes = std::vector<NoteLine>(NOTES);
	std::atomic<uint32_t> samplesWritten{0}, samplesRead{0}, notesWritten{0}, notesRead{0};
	/** Set by the main thread once the files are open, cleared before they are closed. */
	std::atomic<bool> open{false};
	/** Samples the main thread did not take out in time, which the file is short of. */
	std::atomic<uint32_t> dropped{0};

	/** Main thread. */
	std::FILE* wav = nullptr;
	std::FILE* log = nullptr;
	uint32_t wavSamples = 0;
	float rate = 48000.f;
	std::string path;

	/** Audio thread. */
	void pushSample(float v);
	void pushNote(const NoteLine& n);
	/** Main thread: the files opened, named for the module, the time and where the piece was. */
	bool start(const std::string& slug, float sampleRate, float pieceSeconds);
	/** Main thread: what the audio thread has handed over, written. */
	void drain();
	/** Main thread: the rest written and the files closed, the WAV's lengths filled in. */
	void stop();
};


struct BowedModule : Module, NoteSink {
	enum ParamId {
		P_VIBRATO,
		/** DIAGNOSTICS: what the module plays recorded while it is lit. See BowedRecorder. */
		P_RECORD,
		/** BRIGHTNESS: fully clockwise the instrument as it is, turned counter-clockwise its highs turned
		down, by up to BRIGHT_MOST_DB. See BowedShelf. */
		P_BRIGHT,
		NUM_PARAMS
	};
	enum InputId {
		I_MPX,
		/** PLAYED WITHOUT A CABLE: a gate, its pitch, its level, and the vibrato added to the knob. */
		I_GATE,
		I_PITCH,
		I_LEVEL,
		I_VIBRATO,
		NUM_INPUTS
	};
	enum OutputId {
		O_OUT,
		NUM_OUTPUTS
	};
	enum LightId {
		/** The level lamp: green, orange and red. */
		ENUMS(L_LEVEL, 3),
		NUM_LIGHTS
	};

	/** Configures nothing but the counts: the slug's own module configures each control, with its
	help beside it. */
	explicit BowedModule(const BowedKind& kind);
	~BowedModule() override;

	bool isMPXInputId(int id) override { return id == I_MPX; }
	void link(const int* slots, const uint32_t* generations, int n);
	/** A NEW ENGINE where the rate, the part's range or a setting has changed, made ready for the audio
	thread to take up; and the one it gave back taken apart. Main thread. */
	void build();
	void onSampleRateChange(const SampleRateChangeEvent& e) override;
	void onReset() override;
	void process(const ProcessArgs& args) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* rootJ) override;

	/** ECONOMY: less computing for less fidelity — the strings run half as fast, and a string nobody is
	playing stops when it has rung down (see BowedEngine::setup). From the right-click menu; the engine
	is built again for it. */
	bool economy() const { return wantEconomy.load(); }
	void setEconomy(bool on) { wantEconomy.store(on); }
	/** FIDELITY, how much is rehearsed, levels 1 to 4 as 0 to 3: every start tried many ways, two tried,
	starts guessed with bow changes tried, nothing tried and no planner (see BowedPlayer::Rehearsing).
	From the right-click menu; the engine is built again for it. */
	int rehearsing() const { return wantRehearsing.load(); }
	void setRehearsing(int r) { wantRehearsing.store(std::max(0, std::min(3, r))); }
	/** THE ANNOUNCED NOTES PLAYED WITH NOTHING REHEARSED, though the planner was rehearsing: shown as a
	number on the panel, and for a bench, when, seconds on the player's clock. Main thread. */
	int missed() const;
	std::vector<double> missedTimes() const;
	/** DIAGNOSTICS SHOWN: the count of unrehearsed notes and the record button, on the panel. From the
	right-click menu, on by default; off, recording stops. */
	bool diagnostics() const { return wantDiagnostics.load(); }
	void setDiagnostics(bool on) { wantDiagnostics.store(on); }
	/** THE RECORDING, kept up: started, written and stopped as the button says. Main thread. */
	void recordStep();
	/** The last file recorded to, for saying where it went. */
	std::string recordedTo() const { return recorder.path; }

	const BowedKind kind;

private:
	float clipHold = 0.f, nearHold = 0.f, noteHold = 0.f;
	BowedShelf shelf;

	BusReader reader;
	Instrument instrument;
	uint32_t instrumentChange = 0;
	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
	std::atomic<bool> relink{false};
	bool attachedWas = false;

	/** THE ENGINE PLAYING, on the audio thread; the next, built on the main thread and taken up at the
	next sample; and the last, handed back for the main thread to take apart. */
	BowedEngine* engine = nullptr;
	std::atomic<BowedEngine*> next{nullptr};
	std::atomic<BowedEngine*> retired{nullptr};
	/** What the engine was last built for, main thread; and what is wanted, set from the audio
	thread. */
	float builtRate = 0.f, builtHighest = 0.f;
	bool builtEconomy = false;
	std::atomic<bool> wantEconomy{false};
	int builtRehearsing = -1;
	std::atomic<int> wantRehearsing{BowedPlayer::LIVE};
	std::atomic<float> wantRate{48000.f};
	std::atomic<float> wantHighest{1280.f};
	std::atomic<bool> wantDiagnostics{true};

	/** THE NOTES ANNOUNCED, by handle, whose ON is then not played again when it comes. */
	static const int ANNOUNCED_MOST = 512;
	int64_t announced[ANNOUNCED_MOST] = {};
	int announcedFirst = 0, announcedCount = 0;
	/** The note from a keyboard sounding, for its OFF. */
	int64_t liveHandle = -1;
	/** WHETHER THE PART ON THE CABLE GOES WHERE THE INSTRUMENT CANNOT, below its lowest open string or
	past the end of its fingerboard, from the compass the cable gives. Its notes there are played an
	octave in and the source's plug light blinks (BusReader::cannotPlay). */
	bool partOutOfRange = false;
	/** HOW MANY OCTAVES THE WHOLE PART IS MOVED, up where it goes below the lowest open string and the
	move still leaves its top on the fingerboard, down where it goes past the fingerboard and the move
	leaves its bottom on the strings: as a player reading another instrument's part plays it, the line
	kept whole. A note still out of reach after that, or from a part whose compass nobody gave, is
	moved by itself (BowedPlayer::playable). */
	int partOctaves = 0;
	/** Where the source last said the music was, in beats from its start, for telling a rewind; and in
	seconds, for the recording. */
	double lastBeat = 0.0;
	std::atomic<float> pieceSeconds{-1.f};
	/** THE GATE'S CHANNELS, each up or down with a little hysteresis, whether a note from them is
	sounding, and at what pitch, volts. */
	bool gateUp[16] = {};
	bool gateSounding = false;
	float gatePitch = 0.f;
	void playFromPorts();

	/** Of notes starting together on the cable, the highest, to the player: announced ahead, or played
	now. */
	void playTop(const Coming* notes, int n, int64_t now, bool announced);
	BowedPlayer::Note noteOf(const Event& e) const;
	bool wasAnnounced(int64_t handle);
	void rememberAnnounced(int64_t handle);

	/** THE RECORDING: whether the audio thread is handing over, how many of the player's notes it has
	written down, and where its clock stood when the recording began. */
	BowedRecorder recorder;
	bool recordingWas = false;
	size_t notesLogged = 0;
	/** Samples recorded so far, and what to add to the player's clock for seconds into the recording. */
	int64_t recordedSamples = 0;
	double clockOffset = 0.0;
	void recordProcess(float sound);
};


/** THE PANEL, six HP, one column: the diagnostics along the top, the part in, the ports to play it
without a cable, the vibrato, and the level lamp just above the output. */
struct BowedWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;
	std::string slug;
	/** Whether the diagnostics were last shown, so the panel is told only when that changes. */
	int diagnosticsShown = -1;

	BowedWidget(BowedModule* module, const std::string& slug);
	void appendContextMenu(ui::Menu* menu) override;
	/** The MPX link, made here on the main thread, as every MPX input's is; the engine rebuilt when the
	rate or the part's range has changed; the diagnostics shown or hidden; and the recording kept up. */
	void step() override;
};


} // namespace px
