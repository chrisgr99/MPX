/** mpxTrombone — an MPX part played on a physically modelled tenor trombone: phase 7 of docs/brass.md.

NOTHING TO PATCH BUT THE PART AND THE MIXER. The air column, the lips, the slide, the breath and the tongue
are all in here (BrassInstrument.hpp), and the player decides where on the slide each note is played, on
which resonance, how hard it is blown, and how it is started or joined to the note before (BrassPlayer.hpp).
This is the cable and the panel around them.

THE PLAYER READS AHEAD, as a player reading a part does: a score on the cable announces each note some
seconds before it sounds (Coming in NoteBus.hpp), and the player tries ways of starting it, or of slurring
into it, on copies of the trombone while the music plays; for that it runs ahead of what is heard on a
thread of its own (BrassEngine.hpp). A note nobody announced, from a keyboard or the ports, is played as it
arrives.

TUNING BY EAR. Each note's tongued start, its tone and its lips can be set by ear, pitch by pitch and marking
by marking, and kept: how fast the breath rises, how hard it is blown against what its marking asks, the
lips' tension — their own frequency against the resonance's, a player's muscle tone — their gap at rest and
their damping (BrassPlayer::Note::rise, breath, tunedLips, gap, quality). The lips' mass is the player's, one
for the whole trombone. For each note the breath is found for its marking with those lips, and the slide
moved to sound it in tune at their tension and the breath set, as a player moves it (BrassPlayer::tuneFor).
With TUNE
on, every note from the cable is played as it arrives, untried and with nothing rendered ahead, from the
knobs, so a turn is heard on the next note; the knobs go to the note's kept settings, or to the best of the
starts a quick search finds for it with the knobs' lips (BrassPlayer::startsFor), which TRY steps through;
SAVE keeps what the knobs say for the pitch and marking playing; and while REF is held each note is the Iowa
recording of its pitch at its marking instead, as loud as the model plays it there. With TUNE off, the
settings kept are played wherever a note has them, with the mass they were found for. They are kept in the
Rack user folder, under DreamerMPX/trombone-tuning.json, for every patch, the mass with them.

ONE TROMBONE, ONE PLAYER: a chord on the cable is played as its top note, and the ports play the highest of
the gate's channels that are up. */
#include "plugin.hpp"
#include "BowedModule.hpp"
#include "BrassEngine.hpp"
#include "BrassPlayer.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace px {


/** The most notes of one chord looked at: the top is played, the rest remembered as announced. */
static const int CHORD_MOST = 16;
/** HOW FAR THE BRIGHTNESS KNOB TURNS THE HIGHS DOWN, fully counter-clockwise, dB, and from where, hertz:
the middle of the trombone's brightest band at ff. */
static const float BRIGHT_MOST_DB = 12.f, BRIGHT_CORNER_HZ = 2000.f;
/** FROM THE SOUND A METRE AWAY TO RACK'S VOLTS. A loud trombone's peaks stand far over its level — the shock
fronts that make it brassy: at ff, its loudness brought to the recordings' (BrassPlayer, HOW LOUD), they
reach 5 to 13 Pa where its root mean square is 1.3 to 1.9, at fff 9 to 20 (make brassplay ARGS=peaks) —
so the loudest fff peaks are set at 10 V, where the soft clip ends. */
static const float VOLTS_PER_PASCAL = 0.5f;
/** The level lamp's thresholds and how long each colour is held. */
static const float CLIP_VOLTS = 10.f, NEAR_VOLTS = 7.07f;
static const float CLIP_HOLD = 1.f, NEAR_HOLD = 0.3f, NOTE_HOLD = 0.06f;
/** THE KNOBS' FIRST POSITIONS: the breath rising over 140 ms, the lips at the note, the breath as the marking
asks, and the trombone's own breath noise. */
static const float RISE_FIRST = 0.14f, LIPS_FIRST = 0.f, BREATH_FIRST = 0.f, AIR_FIRST = 0.15f;
/** THE LIPS' GAP AT REST, millimetres: the trombone's own, just touching; from 0.2 pressed together to 0.5
apart. */
static const float GAP_FIRST = 0.f;
/** THE LIPS' KNOBS: their mass, the trombone's own 0.9, from 0.6 to 2.5; their damping, one over their
quality open, the trombone's own a third, from a quarter to 0.83 — damped further, they did not sound
(make brasstimbre); and their tension, their own frequency as a share of the resonance's, from 0.80 to 1.10
— the tables' lips are 0.88 to 1.04 for nine notes in ten, and E2's at mass 1.2 sounded from 0.85 to 1.00. */
static const float MASS_FIRST = 0.9f, DAMP_FIRST = 1.f / 3.f, TENSION_FIRST = 0.95f;
/** THE MARKINGS, ppp to fff, as their names. */
static const char* const MARKINGS[8] = {"ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"};
/** The recordings' three dynamics, and which of them each marking takes. */
static const char* const RECORDED[3] = {"pp", "mf", "ff"};
static const int RECORDED_FOR[8] = {0, 0, 0, 1, 1, 1, 2, 2};


/** A NOTE'S START AND TONE SET BY EAR: the breath's rise, seconds; the breath against what the marking asks,
dB; and the breath noise's share (BrassPlayer::Note). */
struct TromboneSetting {
	float rise = RISE_FIRST, breath = BREATH_FIRST, air = AIR_FIRST;
	/** The lips' tension, gap and damping; and the breath and the slide found for them (BrassPlayer::tuneFor),
	with the mass they were found for, nought or less where none were found. */
	float tension = TENSION_FIRST, gap = GAP_FIRST, damp = DAMP_FIRST, mass = MASS_FIRST, tunedMouth = -1.f, slideCents = 0.f;
};


/** ONE IOWA RECORDING at the engine's rate: its samples, where it is first heard, and its level over its
steady part, 0.3 to 0.8 s from there, as a root mean square. */
struct TromboneReference {
	std::vector<float> x;
	size_t onset = 0;
	double steady = 0.0;
};


struct TromboneModule : Module, NoteSink {
	enum ParamId {
		/** BRIGHTNESS: fully clockwise the trombone as it is, turned counter-clockwise its highs turned
		down, by up to BRIGHT_MOST_DB. See BowedShelf. */
		P_BRIGHT,
		/** THE TUNING: on or off; the settings; and TRY, SAVE and REF. */
		P_TUNE,
		P_RISE,
		/** How sharp the lips started: no longer on the panel, GAP in its place. */
		P_LIPS,
		P_BREATH,
		P_AIR,
		P_TRY,
		P_SAVE,
		P_REF,
		/** The lips' mass, the whole trombone's, and their damping. AIR, before them, is no longer on the
		panel. */
		P_MASS,
		P_DAMP,
		/** The lips' tension, and their gap at rest. */
		P_TENSION,
		P_GAP,
		NUM_PARAMS
	};
	enum InputId {
		I_MPX,
		/** PLAYED WITHOUT A CABLE: a gate, its pitch and its level. */
		I_GATE,
		I_PITCH,
		I_LEVEL,
		NUM_INPUTS
	};
	enum OutputId {
		O_OUT,
		NUM_OUTPUTS
	};
	enum LightId {
		/** The level lamp: green, orange and red. */
		ENUMS(L_LEVEL, 3),
		/** BUSY: lit while the tuning is working — finding a note's breath and slide for the knobs' lips, or its
		starts — and while the lips' knobs are settling. */
		L_BUSY,
		NUM_LIGHTS
	};

	TromboneModule();
	~TromboneModule() override;

	bool isMPXInputId(int id) override { return id == I_MPX; }
	void link(const int* slots, const uint32_t* generations, int n);
	/** A NEW ENGINE where the rate or a setting has changed, made ready for the audio thread to take up;
	and the one it gave back taken apart. Main thread. */
	void build();
	void onSampleRateChange(const SampleRateChangeEvent& e) override { wantRate.store(e.sampleRate); }
	void onReset() override;
	void process(const ProcessArgs& args) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* rootJ) override;

	/** FIDELITY, levels 1 to 3 as 0 to 2: every way of each start and slur tried ahead, a third of them, or
	nothing tried and nothing rendered ahead (BrassEngine::Fidelity). The engine is built again for it. */
	int fidelity() const { return wantFidelity.load(); }
	void setFidelity(int f) { wantFidelity.store(std::max(0, std::min(2, f))); }
	/** ECONOMY: less computing, less brassiness at the loudest (BrassEngine::Setup). */
	bool economy() const { return wantEconomy.load(); }
	void setEconomy(bool on) { wantEconomy.store(on); }
	/** DIAGNOSTICS SHOWN: the count of notes played untried for want of time, on the panel. */
	bool diagnostics() const { return wantDiagnostics.load(); }
	void setDiagnostics(bool on) { wantDiagnostics.store(on); }
	/** Notes played without trying because the player was short of time, and samples played as silence
	because it was behind, since the engine was built. Main thread. */
	int hurried() const {
		const BrassEngine* e = engine;
		return e ? e->hurried() : 0;
	}
	uint64_t silent() const {
		const BrassEngine* e = engine;
		return e ? e->silent() : 0;
	}

	/** THE TUNING, MAIN THREAD: kept up from the widget's step — the knobs set for each new note, TRY and SAVE
	carried out, the search and the recordings asked for — and what the display says. */
	void tuningStep();
	std::string displayFirst, displaySecond;

private:
	float clipHold = 0.f, nearHold = 0.f, noteHold = 0.f;
	BowedShelf shelf;
	/** THE TROMBONE'S COMPASS, hertz, from its tables: a note outside it is played an octave or more in. */
	float lowestHz = 82.f, highestHz = 700.f;

	BusReader reader;
	uint32_t instrumentChange = 0;
	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
	std::atomic<bool> relink{false};
	bool attachedWas = false;
	/** Whether the part on the cable goes where the trombone cannot. */
	bool partOutOfRange = false;

	BrassEngine* engine = nullptr;
	std::atomic<BrassEngine*> next{nullptr};
	std::atomic<BrassEngine*> retired{nullptr};
	float builtRate = 0.f;
	int builtFidelity = -1;
	bool builtEconomy = false;
	std::atomic<float> wantRate{48000.f};
	std::atomic<int> wantFidelity{BrassEngine::FEWER_WAYS};
	std::atomic<bool> wantEconomy{false};
	std::atomic<bool> wantDiagnostics{true};

	/** THE NOTES ANNOUNCED, by handle, whose ON is then not played again when it comes. */
	static const int ANNOUNCED_MOST = 512;
	int64_t announced[ANNOUNCED_MOST] = {};
	int announcedFirst = 0, announcedCount = 0;
	/** The note from a keyboard sounding, for its OFF. */
	int64_t liveHandle = -1;
	/** THE GATE'S CHANNELS, each up or down with a little hysteresis, whether a note from them is sounding,
	and at what pitch, volts. */
	bool gateUp[16] = {};
	bool gateSounding = false;
	float gatePitch = 0.f;

	void playFromPorts();
	void playTop(const Coming* notes, int n, int64_t now, bool announcedAhead);
	BrassPlayer::Note noteOf(const Event& e) const;
	/** A pitch moved by octaves into the trombone's compass. */
	double inReach(double hz) const;
	bool wasAnnounced(int64_t handle);
	void rememberAnnounced(int64_t handle);

	// ---- the tuning ----
	/** THE SETTINGS KEPT, by MIDI note and marking; written on the main thread, read on both. And the lips'
	mass, the whole trombone's, as the file has it: the MASS knob is set to it as the module is made or loaded,
	and it is written when the knob is turned and let go. */
	TromboneSetting settings[128][8];
	std::atomic<bool> kept[128][8];
	float keptMass = -1.f;
	void loadSettings();
	void saveSettings();
	/** A note's own start and tone: the knobs' while tuning, else what is kept for it, else none. */
	void giveSettings(BrassPlayer::Note& n, bool tuning);
	static int midiOf(double hz) { return std::max(0, std::min(127, (int) std::lround(69.0 + 12.0 * std::log2(hz / 440.0)))); }
	static int markingOf(double level) { return std::max(0, std::min(7, (int) std::lround(7.0 * level))); }
	/** Whether tuning, for the engine: it plays live while tuning. */
	std::atomic<bool> wantTune{false};
	/** THE NOTE PLAYING, from the audio thread: its MIDI note and marking. */
	std::atomic<int> nowMidi{-1}, nowMarking{-1};
	/** TRY and SAVE pressed, from the audio thread to the main. */
	std::atomic<bool> tryPressed{false}, savePressed{false};
	bool tryWas = false, saveWas = false;
	/** Main thread: the note the knobs were last set for, the starts found for it and which is on the knobs,
	and what the knobs were when last kept. */
	int knobsMidi = -1, knobsMarking = -1;
	std::vector<BrassPlayer::StartTry> starts;
	int startAt = -1;
	bool finding = false;
	/** The search and the loading, on a thread of their own. */
	std::thread worker;
	std::mutex workLock;
	std::condition_variable workWake;
	bool workEnding = false;
	/** The starts asked for: the note, with the lips it was tuned for. */
	int askMidi = -1, askMarking = -1, askLoad = -1;
	BrassPlayer::Note askStarts;
	std::mutex foundLock;
	int foundMidi = -1, foundMarking = -1;
	std::vector<BrassPlayer::StartTry> found;
	std::atomic<uint32_t> foundCount{0};
	uint32_t foundSeen = 0;
	void work();
	/** THE LIPS: the knobs' lips, for a note and marking. */
	struct Lips {
		int midi = -1, marking = -1;
		/** The breath, dB, as BREATH; and the gap, millimetres. */
		float mass = -1.f, damp = -1.f, tension = -1.f, gap = 0.f, breath = 0.f;
		bool operator==(const Lips& o) const {
			return midi == o.midi && marking == o.marking && mass == o.mass && damp == o.damp && tension == o.tension && gap == o.gap
				&& breath == o.breath;
		}
		bool operator!=(const Lips& o) const { return !(*this == o); }
	};
	/** THE NOTE TUNED FOR THE LIPS: asked of the thread; the answer, and for what. */
	Lips askTune;
	std::mutex tunedLock;
	Lips tunedFor;
	BrassPlayer::Tuned tuned;
	/** Main thread: what was last asked, and whether the answer is still awaited; and for the audio thread, the
	breath, lips and slide to play the knobs' lips with, the mouth nought or less while there are none. */
	Lips retune;
	bool retuning = false;
	/** When the lips' knobs last moved: a retune is asked for only once they have been still half a second, so
	that turning one asks for one, not one for every step of the turn. */
	Lips still;
	double stillSince = 0.0;
	std::atomic<float> playMouth{-1.f}, playLips{-1.f}, playSlide{0.f}, playMass{-1.f}, playQuality{-1.f}, playGap{0.f};
	/** The note and marking the breath and lips found are for: played only on that note. And how far out of
	tune the slide left it, cents, main thread. */
	std::atomic<int> playKey{-1};
	float playOff = 0.f;
	/** Whether the knobs' lips sounded the note at none of the pressures tried, main thread. */
	bool unsounding = false;
	/** The starts asked for once the note has been tuned for the knobs' lips, with them: a new note's, or TRY
	pressed while it was being tuned. */
	bool startsWanted = false;
	void askForStarts(int m, int k);
	/** Whether the tuning is working, for BUSY. */
	std::atomic<bool> busy{false};
	/** The knobs' lips, for the note playing. */
	Lips knobLips(int m, int k) {
		Lips l;
		l.midi = m;
		l.marking = k;
		l.mass = params[P_MASS].getValue();
		l.damp = params[P_DAMP].getValue();
		l.tension = params[P_TENSION].getValue();
		l.gap = params[P_GAP].getValue();
		l.breath = params[P_BREATH].getValue();
		return l;
	}
	/** THE RECORDINGS, loaded once each, by MIDI note and recorded dynamic; and which have been asked for. */
	std::atomic<const TromboneReference*> references[128][3];
	bool referencesAsked[128] = {};
	const TromboneReference* loadReference(int midi, int dynamic, double rate) const;
	/** Audio thread: the recording sounding, where in it, how long it has left, and at what gain; and the
	model's level for each note and marking, measured as it plays, for playing the recording as loud. */
	const TromboneReference* refNow = nullptr;
	size_t refAt = 0;
	int64_t refLeft = 0, refFade = 1;
	float refGain = 0.f;
	float modelLevel[128][8] = {};
	int measuring = -1, measuringMarking = -1;
	int64_t measureFrom = 0, measureTo = 0;
	double measureSum = 0.0;
	int64_t measureCount = 0;
	/** A note played now while tuning: the recording while REF is held, else the model from the knobs. */
	void playTuning(const Event& e, int64_t now, float rate);
};


/** Volts, an octave to the volt, with nought at middle C. */
static double hzOf(float volts) {
	return 261.6256 * std::pow(2.0, (double) volts);
}


/** A NOTE'S NAME AS THE DISPLAY SHOWS IT, in capitals, with the flats trombonists read. */
static std::string nameOfMidi(int m) {
	static const char* const NAMES[12] = {"C", "C SHARP", "D", "E FLAT", "E", "F", "F SHARP", "G", "A FLAT", "A", "B FLAT", "B"};
	return std::string(NAMES[((m % 12) + 12) % 12]) + " " + std::to_string(m / 12 - 1);
}


TromboneModule::TromboneModule() {
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	for (int i = 0; i < MAX_UPSTREAM; i++) {
		wantSlots[i].store(-1);
		wantGenerations[i].store(0);
	}
	for (int m = 0; m < 128; m++) {
		for (int k = 0; k < 8; k++)
			kept[m][k].store(false);
		for (int d = 0; d < 3; d++)
			references[m][d].store(nullptr);
	}
	// THE COMPASS, from where the tables' notes sound halfway up their pressures: E2 to F sharp 5.
	{
		const BrassTable& t = tromboneTable();
		float low = 1e9f, high = 0.f;
		for (int i = 0; i < t.noteCount; i++) {
			const float hz = t.points[t.notes[i].first + t.notes[i].count / 2].hz;
			low = std::min(low, hz);
			high = std::max(high, hz);
		}
		lowestHz = low;
		highestHz = high;
	}

	configParam(P_BRIGHT, 0.f, 1.f, 1.f, "Brightness", "%", 0.f, 100.f);
	//? Turns the trombone's highs down. Fully clockwise, the trombone as it is; turned counter-clockwise,
	//? everything above about 2 kHz is turned down, by up to 12 dB fully counter-clockwise, and nothing
	//? below it is touched. A filter on the output, after the trombone: it changes nothing about how the
	//? notes are blown or started.
	configSwitch(P_TUNE, 0.f, 1.f, 0.f, "Tune", {"Off", "On"});
	//? Tuning by ear. While lit, every note from the cable is played the moment it arrives, from the
	//? knobs beside it, with nothing tried or worked out ahead, so a turn of a knob is heard on the next
	//? note. When the music moves to a pitch and marking, the knobs go to the settings kept for it; or
	//? else they stay as they are, the note is tuned for their lips, and they go to the best of the starts
	//? a quick search then finds for it, which takes several seconds, BUSY lit meanwhile. MASS is the same
	//? for every note. Unlit, the trombone plays as it always does, using the settings kept wherever a
	//? note has them.
	configParam(P_RISE, 0.f, 0.6f, RISE_FIRST, "Rise", " ms", 0.f, 1000.f);
	//? How long the breath takes to rise to its level after the tongue lets go, from just over the
	//? lowest pressure the note sounds at, up to 80 ms less than the note's length. Short starts hard;
	//? long starts soft, the tone opening up as it rises. Nought is a step of breath. Only while tuning,
	//? and only for tongued notes.
	configParam(P_LIPS, -10.f, 20.f, LIPS_FIRST, "Lips start (no longer used)", "% sharp");
	configParam(P_BREATH, -10.f, 4.f, BREATH_FIRST, "Breath", " dB");
	//? How hard the note is blown against what its marking asks — the brightness of the recordings at
	//? that marking in its register: up brighter and louder, down darker and softer. Never under the
	//? lowest pressure the note sounds at. Since the breath pulls the pitch, the slide is found again for
	//? it when it is turned, as for TENSION. Only while tuning.
	configParam(P_AIR, 0.f, 0.4f, AIR_FIRST, "Air (no longer used)", "%", 0.f, 100.f);
	configParam(P_MASS, 0.6f, 2.5f, MASS_FIRST, "Lip mass, the whole trombone's");
	//? How heavy the lips are, against the trombone's own 0.9: heavier lips take longer to come into
	//? vibration, so the note swells in more gradually, and need more breath to sound. One mass for
	//? every note, as a player has: it is kept for every patch as it is let go, and does not change from
	//? note to note. When it is turned, the note playing is tuned again for it, as for TENSION. A kept
	//? note found for another mass is tuned again when it next plays. Only while tuning.
	paramQuantities[P_MASS]->resetEnabled = false;
	paramQuantities[P_MASS]->randomizeEnabled = false;
	configParam(P_DAMP, 0.25f, 0.83f, DAMP_FIRST, "Lip damping");
	//? How much the lips lose of their motion each cycle, against the trombone's own a third: more damped
	//? lips also come into vibration more slowly, and too much and they do not sound at all. When it is
	//? turned, the note playing is tuned again for it, as for TENSION. Only while tuning.
	configParam(P_TENSION, 0.8f, 1.1f, TENSION_FIRST, "Lip tension", "% of the resonance", 0.f, 100.f);
	//? How tight the lips are: their own frequency as a share of the frequency of the air column's
	//? resonance the note is played on. Tighter lips stay open for less of each cycle, looser for more,
	//? which changes the tone; and pull the pitch up or down, which the slide is moved to put right, as a
	//? player moves it. When it is turned, the breath for the note's marking and the slide are found again
	//? for the note playing, which takes a few seconds: BUSY is lit from the moment it is turned until
	//? they are found, about three seconds after it is let go, and meanwhile the notes go on as found
	//? last. Where the slide cannot go far enough the note is left out of tune, and the display says by
	//? how much. Too tight or too loose and the lips do not sound the note at any breath: the display then
	//? says WILL NOT SOUND, and the notes are played as the knobs say all the same. Only while tuning.
	configParam(P_GAP, -0.2f, 0.5f, GAP_FIRST, "Lip gap", " mm");
	//? How far apart the lips are at rest, in millimetres: at nought just touching, as the trombone's own
	//? are, under nought pressed together, over nought apart. It changes how the lips move in each cycle,
	//? and so the tone; and the breath the note needs and its pitch, which are found again when it is
	//? turned, as for TENSION. Too far apart, and the lips may not sound a soft note at all. Only while
	//? tuning.
	configButton(P_TRY, "Try the next start");
	//? Sets RISE to the next of the starts the search found for the note playing, in turn: the breath
	//? rising over 40 to 600 ms, the clean ones, nearest the recording first. On a note with none found
	//? yet, a kept one, the first press runs the search, which takes a few seconds with BUSY lit, and puts
	//? the best on RISE. The display shows which. Only while tuning.
	configButton(P_SAVE, "Keep these settings");
	//? Keeps what the knobs say for the pitch and marking playing, for every patch, and the breath and
	//? slide found for them; the display then shows KEPT. Only while tuning.
	configButton(P_REF, "Reference: the recording");
	//? While held, each note that starts is the Iowa recording of its pitch at its marking, as loud as the
	//? trombone plays it there; let go, the notes are the trombone again. A note goes on as it started
	//? until it ends. Only while tuning, and only once the recordings are in the Rack user folder, under
	//? DreamerMPX/trombone-reference.
	configInput(I_MPX, "MPX note");
	//? One part, played on the trombone: its notes, from mpxScore or any MPX source. From a score each
	//? note is announced ahead, and at Fidelity levels 1 and 2 its start or its slur is worked out before
	//? it sounds.
	configInput(I_GATE, "Gate");
	//? Plays the trombone without a cable: a note while the gate is up, let go when it falls. With a
	//? polyphonic gate, the highest pitch among the channels that are up. While an MPX cable is in, this
	//? port and the two below it are not read.
	configInput(I_PITCH, "1V/oct");
	//? The pitch of the gate's note, a volt an octave, nought at middle C. A move of more than half a
	//? semitone while the gate stays up is the next note, slurred; a smaller one is left alone.
	configInput(I_LEVEL, "Level");
	//? How loud each gate's note is played, read as it begins: nought volts ppp, ten fff, in sevenths
	//? between. With nothing patched, mf.
	configOutput(O_OUT, "Out");
	//? The trombone, in mono. A loud note's peaks stand far above its level: at ff they reach 3V to 6.5V,
	//? at fff up to 10V, the highest of them rounded off by a soft curve from 8.5V.
	configLight(L_BUSY, "Busy");
	//? Lit while the tuning is working: from the moment TENSION, GAP, DAMP, MASS or BREATH is turned
	//? until the breath and the slide for them are found for the note playing, and while a new note is
	//? tuned and its starts found. Out, the notes play as the knobs say.
	configLight(L_LEVEL, "Level");
	//? Flashes green as each note begins, lights orange when the output passes 7.07V, 3 dB under
	//? clipping, and red when it reaches 10V, where Rack's audio interface clips.

	loadSettings();
	if (keptMass > 0.f)
		params[P_MASS].setValue(keptMass);
	wantRate.store(APP->engine->getSampleRate());
	build();
	engine = next.exchange(nullptr);
	worker = std::thread([this]() { work(); });
}


TromboneModule::~TromboneModule() {
	{
		std::lock_guard<std::mutex> hold(workLock);
		workEnding = true;
	}
	workWake.notify_all();
	if (worker.joinable())
		worker.join();
	delete engine;
	delete next.exchange(nullptr);
	delete retired.exchange(nullptr);
	for (int m = 0; m < 128; m++)
		for (int d = 0; d < 3; d++)
			delete references[m][d].exchange(nullptr);
}


void TromboneModule::link(const int* slots, const uint32_t* generations, int n) {
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


void TromboneModule::build() {
	// TAKEN APART ON A THREAD OF ITS OWN: one in the middle of rehearsing a note can take a second or two to
	// stop.
	if (BrassEngine* old = retired.exchange(nullptr))
		std::thread([old]() { delete old; }).detach();
	const float rate = wantRate.load();
	// WHILE TUNING, LIVE: a turn of a knob is to be heard on the next note, not seconds later.
	const int fidelity = wantTune.load() ? (int) BrassEngine::LIVE : wantFidelity.load();
	const bool economical = wantEconomy.load();
	if ((rate == builtRate && fidelity == builtFidelity && economical == builtEconomy) || next.load())
		return;
	BrassEngine* e = new BrassEngine;
	BrassEngine::Setup setup;
	setup.table = &tromboneTable();
	setup.rate = rate;
	setup.fidelity = (BrassEngine::Fidelity) fidelity;
	setup.economy = economical;
	setup.announcedAhead = MPX_ANNOUNCE_AHEAD;
	e->setup(setup);
	builtRate = rate;
	builtFidelity = fidelity;
	builtEconomy = economical;
	next.store(e);
}


json_t* TromboneModule::dataToJson() {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "fidelity", json_integer(wantFidelity.load() + 1));
	json_object_set_new(rootJ, "economy", json_boolean(wantEconomy.load()));
	json_object_set_new(rootJ, "diagnostics", json_boolean(wantDiagnostics.load()));
	return rootJ;
}


void TromboneModule::dataFromJson(json_t* rootJ) {
	if (json_t* j = json_object_get(rootJ, "fidelity"))
		setFidelity((int) json_integer_value(j) - 1);
	if (json_t* j = json_object_get(rootJ, "economy"))
		wantEconomy.store(json_boolean_value(j));
	if (json_t* j = json_object_get(rootJ, "diagnostics"))
		wantDiagnostics.store(json_boolean_value(j));
	// THE LIPS' MASS is the trombone's, not the patch's: the patch's knobs were set before this.
	if (keptMass > 0.f)
		params[P_MASS].setValue(keptMass);
}


void TromboneModule::onReset() {
	if (engine) {
		engine->withdraw();
		announcedCount = 0;
		liveHandle = -1;
	}
}


double TromboneModule::inReach(double hz) const {
	while (hz < lowestHz * 0.99 && hz > 0.0)
		hz *= 2.0;
	while (hz > highestHz * 1.01)
		hz *= 0.5;
	return hz;
}


BrassPlayer::Note TromboneModule::noteOf(const Event& e) const {
	BrassPlayer::Note n;
	n.hz = inReach(hzOf(e.pitch));
	n.level = e.level;
	n.seconds = e.duration > 0.f ? e.duration : 0.25f;
	n.slurred = (e.technique & Event::SLURRED) != 0;
	n.glissando = (e.technique & Event::LEGATO_SLIDE) != 0;
	n.staccato = (e.technique & Event::STACCATO) != 0;
	n.tenuto = (e.technique & Event::TENUTO) != 0;
	n.accent = (e.technique & Event::ACCENT) != 0;
	n.heavy = (e.technique & Event::HEAVY_ACCENT) != 0;
	n.ghost = (e.technique & Event::GHOST) != 0;
	n.vibrato = e.vibrato == Event::VIBRATO_WIDE ? 2 : e.vibrato == Event::VIBRATO_SLIGHT ? 1 : 0;
	return n;
}


// ---- the tuning ---------------------------------------------------------------------------------

static std::string tuningPath() {
	return asset::user("DreamerMPX/trombone-tuning.json");
}


void TromboneModule::loadSettings() {
	json_error_t error;
	json_t* root = json_load_file(tuningPath().c_str(), 0, &error);
	if (!root)
		return;
	if (json_t* j = json_object_get(root, "mass"))
		keptMass = (float) json_number_value(j);
	json_t* notes = json_object_get(root, "notes");
	size_t i;
	json_t* item;
	json_array_foreach(notes, i, item) {
		const int m = (int) json_integer_value(json_object_get(item, "midi"));
		const char* marking = json_string_value(json_object_get(item, "marking"));
		int k = -1;
		for (int q = 0; q < 8 && marking; q++)
			if (std::strcmp(marking, MARKINGS[q]) == 0)
				k = q;
		if (m < 0 || m > 127 || k < 0)
			continue;
		TromboneSetting& s = settings[m][k];
		s.rise = (float) json_number_value(json_object_get(item, "rise"));
		s.breath = (float) json_number_value(json_object_get(item, "breath"));
		s.air = (float) json_number_value(json_object_get(item, "air"));
		if (json_t* j = json_object_get(item, "mass"))
			s.mass = (float) json_number_value(j);
		if (json_t* j = json_object_get(item, "damp"))
			s.damp = (float) json_number_value(j);
		if (json_t* j = json_object_get(item, "tunedMouth"))
			s.tunedMouth = (float) json_number_value(j);
		if (json_t* j = json_object_get(item, "gap"))
			s.gap = (float) json_number_value(j);
		if (json_t* j = json_object_get(item, "slideCents"))
			s.slideCents = (float) json_number_value(j);
		if (json_t* j = json_object_get(item, "tension"))
			s.tension = (float) json_number_value(j);
		// Kept before the tension: the lips' setting found to sound it in tune with the slide where the tables
		// put it, which is a tension with the slide not moved.
		else if (json_t* j = json_object_get(item, "tunedLips"))
			if (json_number_value(j) > 0.0) {
				s.tension = (float) json_number_value(j);
				s.slideCents = 0.f;
			}
		// Kept before the mass was the trombone's: the mass of a note kept with one.
		if (keptMass <= 0.f && json_object_get(item, "mass"))
			keptMass = s.mass;
		kept[m][k].store(true);
	}
	json_decref(root);
}


void TromboneModule::saveSettings() {
	json_t* root = json_object();
	json_object_set_new(root, "about", json_string("mpxTrombone's starts, tones and lips set by ear. mass: the lips' mass, the whole "
		"trombone's. For each note: rise seconds, breath dB against the marking's, tension the lips' own frequency "
		"as a share of the resonance's, gap the lips' at rest in millimetres (less than nought pressed together), damp the lips' (one "
		"over their quality open); and found for those lips and that breath with the mass given, "
		"tunedMouth pascals, less than nought where none was found, and slideCents, the slide moved out as for a note that many cents "
		"flatter, in where less than nought."));
	if (keptMass > 0.f)
		json_object_set_new(root, "mass", json_real(keptMass));
	json_t* notes = json_array();
	for (int m = 0; m < 128; m++)
		for (int k = 0; k < 8; k++) {
			if (!kept[m][k].load())
				continue;
			const TromboneSetting& s = settings[m][k];
			json_t* item = json_object();
			json_object_set_new(item, "midi", json_integer(m));
			json_object_set_new(item, "note", json_string(nameOfMidi(m).c_str()));
			json_object_set_new(item, "marking", json_string(MARKINGS[k]));
			json_object_set_new(item, "rise", json_real(s.rise));
			json_object_set_new(item, "breath", json_real(s.breath));
			json_object_set_new(item, "tension", json_real(s.tension));
			json_object_set_new(item, "gap", json_real(s.gap));
			json_object_set_new(item, "damp", json_real(s.damp));
			json_object_set_new(item, "mass", json_real(s.mass));
			json_object_set_new(item, "tunedMouth", json_real(s.tunedMouth));
			json_object_set_new(item, "slideCents", json_real(s.slideCents));
			json_array_append_new(notes, item);
		}
	json_object_set_new(root, "notes", notes);
	system::createDirectories(asset::user("DreamerMPX"));
	json_dump_file(root, tuningPath().c_str(), JSON_INDENT(2));
	json_decref(root);
}


void TromboneModule::giveSettings(BrassPlayer::Note& n, bool tuning) {
	if (tuning) {
		n.rise = params[P_RISE].getValue();
		n.breath = std::pow(10.0, params[P_BREATH].getValue() / 20.0);
		// The knobs' lips, once the note has been tuned for them, on the note they were tuned for, with the
		// breath and the slide found for them — or, where none sounded it, with the tables' breath, as they
		// are; until then, the trombone's own.
		if (playLips.load() > 0.f && playKey.load() == midiOf(n.hz) * 8 + markingOf(n.level)) {
			n.mass = playMass.load();
			n.quality = playQuality.load();
			n.tunedLips = playLips.load();
			n.hasGap = true;
			n.gap = 1e-3 * playGap.load();
			if (playMouth.load() > 0.f) {
				n.tunedMouth = playMouth.load();
				n.slideCents = playSlide.load();
			}
		}
		return;
	}
	const int m = midiOf(n.hz), k = markingOf(n.level);
	if (!kept[m][k].load())
		return;
	const TromboneSetting& s = settings[m][k];
	n.rise = s.rise;
	n.breath = std::pow(10.0, s.breath / 20.0);
	// Its lips with the mass they were found for.
	if (s.tunedMouth > 0.f) {
		n.mass = s.mass;
		n.quality = 1.0 / s.damp;
		n.tunedMouth = s.tunedMouth;
		n.tunedLips = s.tension;
		n.slideCents = s.slideCents;
		n.hasGap = true;
		n.gap = 1e-3 * s.gap;
	}
}


/** A WAV, mono, 16 bits, as floats; its rate into `rate`. Empty where it is anything else. */
static std::vector<float> readWav(const std::string& path, double& rate) {
	std::vector<float> x;
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return x;
	std::vector<uint8_t> b;
	uint8_t chunk[65536];
	size_t got;
	while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0)
		b.insert(b.end(), chunk, chunk + got);
	std::fclose(f);
	auto u32 = [&](size_t i) { return (uint32_t) b[i] | (uint32_t) b[i + 1] << 8 | (uint32_t) b[i + 2] << 16 | (uint32_t) b[i + 3] << 24; };
	for (size_t i = 12; i + 8 <= b.size();) {
		const uint32_t size = u32(i + 4);
		if (!std::memcmp(&b[i], "fmt ", 4))
			rate = (double) u32(i + 12);
		if (!std::memcmp(&b[i], "data", 4))
			for (size_t j = i + 8; j + 1 < std::min(b.size(), (size_t) (i + 8 + size)); j += 2)
				x.push_back((float) (int16_t) (b[j] | b[j + 1] << 8) / 32768.f);
		i += 8 + size + (size & 1);
	}
	return x;
}


const TromboneReference* TromboneModule::loadReference(int midi, int dynamic, double rate) const {
	double fileRate = 44100.0;
	const std::vector<float> x = readWav(asset::user("DreamerMPX/trombone-reference/" + std::to_string(midi) + " " + RECORDED[dynamic] + ".wav"), fileRate);
	if (x.empty())
		return nullptr;
	TromboneReference* r = new TromboneReference;
	// TO THE ENGINE'S RATE, by a windowed sinc of 32 taps.
	const size_t n = (size_t) ((double) x.size() * rate / fileRate);
	const double cut = 0.95 * std::min(1.0, rate / fileRate);
	r->x.assign(n, 0.f);
	for (size_t i = 0; i < n; i++) {
		const double t = (double) i * fileRate / rate;
		const long c = (long) std::floor(t);
		double sum = 0.0;
		for (long k = c - 15; k <= c + 16; k++) {
			if (k < 0 || k >= (long) x.size())
				continue;
			const double d = t - (double) k;
			const double sinc = std::fabs(d) < 1e-9 ? cut : std::sin(M_PI * cut * d) / (M_PI * d);
			sum += x[(size_t) k] * sinc * (0.5 + 0.5 * std::cos(M_PI * d / 16.0));
		}
		r->x[i] = (float) sum;
	}
	// Where it is first heard: the first 2 ms within 30 dB of its loudest 50 ms; and its steady level.
	auto rmsOver = [&](size_t a, size_t b) {
		double s = 0.0;
		for (size_t i = a; i < b && i < n; i++)
			s += (double) r->x[i] * r->x[i];
		return std::sqrt(s / (double) std::max<size_t>(1, b - a));
	};
	const size_t w50 = (size_t) (0.05 * rate), w2 = (size_t) (0.002 * rate);
	double loud = 0.0;
	for (size_t i = 0; i + w50 < n; i += w2)
		loud = std::max(loud, rmsOver(i, i + w50));
	for (size_t i = 0; i + w2 < n; i += w2)
		if (rmsOver(i, i + w2) > loud * 0.0316) {
			r->onset = i;
			break;
		}
	r->steady = rmsOver(r->onset + (size_t) (0.3 * rate), r->onset + (size_t) (0.8 * rate));
	return r;
}


/** THE SEARCH AND THE LOADING: waits for the main thread to ask, then tunes a note for the knobs' lips, finds
its starts, best first, or loads a pitch's recordings. */
void TromboneModule::work() {
	crewStandsBack();
	for (;;) {
		int midi = -1, marking = -1, load = -1;
		BrassPlayer::Note startsNote;
		Lips tune;
		{
			std::unique_lock<std::mutex> hold(workLock);
			workWake.wait(hold, [&]() { return workEnding || askMidi >= 0 || askLoad >= 0 || askTune.midi >= 0; });
			if (workEnding)
				return;
			midi = askMidi;
			marking = askMarking;
			load = askLoad;
			startsNote = askStarts;
			askMidi = askMarking = askLoad = -1;
			tune = askTune;
			askTune = Lips();
		}
		if (tune.midi >= 0) {
			BrassInstrument::Design design;
			design.rate = wantRate.load();
			BrassPlayer::Note n;
			n.hz = 440.0 * std::pow(2.0, (tune.midi - 69) / 12.0);
			n.level = tune.marking / 7.0;
			n.seconds = 0.75;
			n.mass = tune.mass;
			n.quality = 1.0 / tune.damp;
			n.tunedLips = tune.tension;
			n.hasGap = true;
			n.gap = 1e-3 * tune.gap;
			n.breath = std::pow(10.0, tune.breath / 20.0);
			std::shared_ptr<Crew> crew = brassSharedCrew();
			const BrassPlayer::Tuned t = BrassPlayer::tuneFor(design, tromboneTable(), BrassPlayer::Habits(), n, crew.get());
			std::lock_guard<std::mutex> hold(tunedLock);
			tunedFor = tune;
			tuned = t;
		}
		const double rate = wantRate.load();
		if (load >= 0)
			for (int d = 0; d < 3; d++)
				if (!references[load][d].load())
					references[load][d].store(loadReference(load, d, rate));
		if (midi >= 0) {
			BrassInstrument::Design design;
			design.rate = rate;
			std::shared_ptr<Crew> crew = brassSharedCrew();
			std::vector<BrassPlayer::StartTry> tries = BrassPlayer::startsFor(design, tromboneTable(), BrassPlayer::Habits(), startsNote, crew.get());
			// The clean; or the best three where none is clean.
			std::vector<BrassPlayer::StartTry> keep;
			for (const BrassPlayer::StartTry& t : tries)
				if (t.clean)
					keep.push_back(t);
			for (size_t i = 0; keep.empty() && i < tries.size() && i < 3; i++)
				keep.push_back(tries[i]);
			std::lock_guard<std::mutex> hold(foundLock);
			foundMidi = midi;
			foundMarking = marking;
			found = keep;
			foundCount.fetch_add(1);
		}
	}
}


void TromboneModule::askForStarts(int m, int k) {
	// The note as the knobs play it: their lips, with the breath and slide found for them where they sounded it.
	BrassPlayer::Note n;
	n.hz = 440.0 * std::pow(2.0, (m - 69) / 12.0);
	n.level = k / 7.0;
	n.seconds = 0.75;
	n.breath = std::pow(10.0, retune.breath / 20.0);
	if (playMouth.load() > 0.f && playKey.load() == m * 8 + k) {
		n.hasGap = true;
		n.gap = 1e-3 * playGap.load();
		n.mass = playMass.load();
		n.quality = playQuality.load();
		n.tunedLips = playLips.load();
		n.tunedMouth = playMouth.load();
		n.slideCents = playSlide.load();
	}
	{
		std::lock_guard<std::mutex> hold(workLock);
		askMidi = m;
		askMarking = k;
		askStarts = n;
	}
	finding = true;
	workWake.notify_all();
}


void TromboneModule::tuningStep() {
	const bool tuning = params[P_TUNE].getValue() > 0.5f;
	busy.store(tuning && (retuning || finding));
	if (!tuning) {
		displayFirst = "TUNE OFF";
		displaySecond = "";
		knobsMidi = knobsMarking = -1;
		return;
	}
	const int m = nowMidi.load(), k = nowMarking.load();
	auto setKnobs = [&](float rise, float breath) {
		params[P_RISE].setValue(rise);
		params[P_BREATH].setValue(breath);
	};
	if (m < 0) {
		displayFirst = "TUNING";
		displaySecond = "WAITING FOR A NOTE";
		return;
	}
	// A NEW PITCH OR MARKING: its kept settings on the knobs, the mass left as it is, the whole trombone's; or
	// else the knobs left as they are, the note tuned for their lips, and then its starts found with them.
	if (m != knobsMidi || k != knobsMarking) {
		knobsMidi = m;
		knobsMarking = k;
		starts.clear();
		startAt = -1;
		finding = false;
		startsWanted = !kept[m][k].load();
		{
			std::lock_guard<std::mutex> hold(workLock);
			if (!referencesAsked[m]) {
				askLoad = m;
				referencesAsked[m] = true;
			}
		}
		workWake.notify_all();
		if (kept[m][k].load()) {
			const TromboneSetting& s = settings[m][k];
			setKnobs(s.rise, s.breath);
			params[P_TENSION].setValue(s.tension);
			params[P_GAP].setValue(s.gap);
			params[P_DAMP].setValue(s.damp);
			// What was found for its lips, kept with them, played at once, where found for this mass.
			if (s.tunedMouth > 0.f && s.mass == params[P_MASS].getValue()) {
				retune = knobLips(m, k);
				retuning = false;
				playMass.store(s.mass);
				playQuality.store(1.f / s.damp);
				playLips.store(s.tension);
				playSlide.store(s.slideCents);
				playGap.store(s.gap);
				playMouth.store(s.tunedMouth);
				playKey.store(m * 8 + k);
				playOff = 0.f;
				unsounding = false;
			}
		}
	}
	// THE KNOBS' LIPS: the note tuned again for them when they or the note change — once the knobs have been
	// still half a second; meanwhile the notes go on as found last.
	{
		const Lips want = knobLips(m, k);
		Lips knobs = want;
		knobs.midi = knobs.marking = -1;
		const double now = system::getTime();
		if (knobs != still) {
			still = knobs;
			stillSince = now;
		}
		// Waiting for the knobs to settle shows as busy too.
		if (want != retune)
			retuning = true;
		if (now - stillSince >= 0.5 && want != retune) {
			retune = want;
			{
				std::lock_guard<std::mutex> hold(workLock);
				askTune = want;
			}
			workWake.notify_all();
			// THE MASS, the whole trombone's, kept for every patch as it is let go.
			if (want.mass != keptMass) {
				keptMass = want.mass;
				saveSettings();
			}
		}
		if (retuning) {
			bool answered = false;
			BrassPlayer::Tuned t;
			{
				std::lock_guard<std::mutex> hold(tunedLock);
				if (tunedFor == retune) {
					answered = true;
					t = tuned;
				}
			}
			if (answered) {
				retuning = false;
				// Played as found; or, where nothing sounded it, as the knobs say all the same, to be heard so.
				unsounding = t.mouth <= 0.0;
				playMass.store(retune.mass);
				playQuality.store(1.f / retune.damp);
				playLips.store(retune.tension);
				playSlide.store(unsounding ? 0.f : (float) t.slideCents);
				playGap.store(retune.gap);
				playMouth.store(unsounding ? -1.f : (float) t.mouth);
				playKey.store(retune.midi * 8 + retune.marking);
				playOff = unsounding ? 0.f : (float) t.offCents;
				// A NEW NOTE'S STARTS, found with the lips it has been tuned for.
				if (startsWanted) {
					startsWanted = false;
					askForStarts(m, k);
				}
			}
		}
	}
	// THE STARTS FOUND, the best on the knobs.
	const uint32_t count = foundCount.load();
	if (count != foundSeen) {
		foundSeen = count;
		std::lock_guard<std::mutex> hold(foundLock);
		if (foundMidi == knobsMidi && foundMarking == knobsMarking && finding) {
			starts = found;
			finding = false;
			if (!starts.empty()) {
				startAt = 0;
				params[P_RISE].setValue((float) starts[0].rise);
			}
		}
	}
	// TRY: the next start found; or, none found yet — a note kept, or one whose search found none — the search
	// asked for, once the note is tuned for the knobs.
	if (tryPressed.exchange(false)) {
		if (!starts.empty()) {
			startAt = (startAt + 1) % (int) starts.size();
			params[P_RISE].setValue((float) starts[(size_t) startAt].rise);
		}
		else if (retuning)
			startsWanted = true;
		else if (!finding)
			askForStarts(m, k);
	}
	// SAVE: what the knobs say kept, and what was found for their lips.
	if (savePressed.exchange(false)) {
		TromboneSetting& s = settings[m][k];
		s.rise = params[P_RISE].getValue();
		s.breath = params[P_BREATH].getValue();
		s.tension = params[P_TENSION].getValue();
		s.gap = params[P_GAP].getValue();
		s.damp = params[P_DAMP].getValue();
		s.mass = params[P_MASS].getValue();
		const bool found = playMouth.load() > 0.f && !retuning && playKey.load() == m * 8 + k;
		s.tunedMouth = found ? playMouth.load() : -1.f;
		s.slideCents = found ? playSlide.load() : 0.f;
		kept[m][k].store(true);
		saveSettings();
	}
	// WHAT THE DISPLAY SAYS: the note and marking; then kept, and whether the knobs have moved since, or which
	// start is on the knobs, or the search; and how far out of tune it is left, where the slide cannot reach.
	displayFirst = nameOfMidi(m) + "   " + MARKINGS[k];
	for (char& c : displayFirst)
		c = (char) std::toupper((unsigned char) c);
	const bool here = playKey.load() == m * 8 + k;
	const bool off = !retuning && std::fabs(playOff) >= 5.f && here;
	if (retuning)
		displaySecond = "RETUNING";
	else if (unsounding && here)
		displaySecond = "WILL NOT SOUND";
	else if (finding)
		displaySecond = "FINDING STARTS";
	else if (off)
		displaySecond = std::to_string((int) std::lround(std::fabs(playOff))) + (playOff > 0.f ? " CENTS SHARP" : " CENTS FLAT");
	else if (kept[m][k].load()) {
		const TromboneSetting& s = settings[m][k];
		const bool same = std::fabs(s.rise - params[P_RISE].getValue()) < 1e-4f && std::fabs(s.gap - params[P_GAP].getValue()) < 1e-4f
			&& std::fabs(s.breath - params[P_BREATH].getValue()) < 1e-4f && std::fabs(s.tension - params[P_TENSION].getValue()) < 1e-4f
			&& std::fabs(s.damp - params[P_DAMP].getValue()) < 1e-4f && std::fabs(s.mass - params[P_MASS].getValue()) < 1e-4f;
		displaySecond = same ? "KEPT" : "CHANGED SINCE KEPT";
	}
	else if (startAt >= 0)
		displaySecond = "START " + std::to_string(startAt + 1) + " OF " + std::to_string(starts.size());
	else
		displaySecond = "NO STARTS FOUND";
	busy.store(retuning || finding);
}


void TromboneModule::playTuning(const Event& e, int64_t now, float rate) {
	BrassPlayer::Note n = noteOf(e);
	const int m = midiOf(n.hz), k = markingOf(n.level);
	nowMidi.store(m);
	nowMarking.store(k);
	const TromboneReference* ref = params[P_REF].getValue() > 0.5f ? references[m][RECORDED_FOR[k]].load() : nullptr;
	if (ref) {
		// THE RECORDING, from 20 ms before it is heard, as loud over its steady part as the trombone plays the
		// note there, or as a mezzo-forte trombone where it has not been heard yet; faded at its written end.
		const float model = modelLevel[m][k] > 0.f ? modelLevel[m][k] : 0.7f;
		refNow = ref;
		refAt = ref->onset > (size_t) (0.02 * rate) ? ref->onset - (size_t) (0.02 * rate) : 0;
		refLeft = (int64_t) ((double) n.seconds * rate);
		refFade = std::max<int64_t>(1, (int64_t) (0.03 * rate));
		refGain = ref->steady > 0.0 ? (float) (model / ref->steady) : 0.f;
		return;
	}
	refNow = nullptr;
	giveSettings(n, true);
	engine->playNow(n);
	// ITS LEVEL MEASURED, from 0.25 s into it to near its end, for playing the recording as loud.
	measuring = m;
	measuringMarking = k;
	measureFrom = now + (int64_t) (0.25 * rate);
	measureTo = now + (int64_t) (0.9 * (double) n.seconds * rate);
	measureSum = 0.0;
	measureCount = 0;
}


/** PLAYED FROM THE PORTS, as a monophonic synthesiser is played: a note while any of the gate's channels is
up, at the highest pitch among those that are; a move of more than half a semitone while it stays up is the
next note, slurred; all down, the note is let go. The level is read as each note begins. Not while an MPX
cable is in. */
void TromboneModule::playFromPorts() {
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
	BrassPlayer::Note n;
	n.hz = inReach(hzOf(pitch));
	n.level = inputs[I_LEVEL].isConnected() ? math::clamp(inputs[I_LEVEL].getPolyVoltage(top) / 10.f, 0.f, 1.f) : 4.f / 7.f;
	// Held until the gate falls: as long as anyone holds a note.
	n.seconds = 600.0;
	n.fromSlur = slur;
	giveSettings(n, false);
	engine->playNow(n);
	gateSounding = true;
	gatePitch = pitch;
	noteHold = NOTE_HOLD;
}


void TromboneModule::playTop(const Coming* notes, int n, int64_t now, bool announcedAhead) {
	if (n <= 0)
		return;
	int top = 0;
	for (int i = 1; i < n; i++)
		if (notes[i].note.pitch > notes[top].note.pitch)
			top = i;
	const Coming& c = notes[top];
	BrassPlayer::Note note = noteOf(c.note);
	giveSettings(note, false);
	if (announcedAhead)
		engine->announce(note, c.frame - now);
	else {
		engine->playNow(note);
		liveHandle = c.note.handle;
	}
}


bool TromboneModule::wasAnnounced(int64_t handle) {
	for (int k = 0; k < announcedCount; k++) {
		const int at = (announcedFirst + k) % ANNOUNCED_MOST;
		if (announced[at] != handle)
			continue;
		announced[at] = announced[announcedFirst];
		announcedFirst = (announcedFirst + 1) % ANNOUNCED_MOST;
		announcedCount--;
		return true;
	}
	return false;
}


void TromboneModule::rememberAnnounced(int64_t handle) {
	if (announcedCount == ANNOUNCED_MOST) {
		announcedFirst = (announcedFirst + 1) % ANNOUNCED_MOST;
		announcedCount--;
	}
	announced[(announcedFirst + announcedCount) % ANNOUNCED_MOST] = handle;
	announcedCount++;
}


void TromboneModule::process(const ProcessArgs& args) {
	if (args.sampleRate != wantRate.load())
		wantRate.store(args.sampleRate);
	const bool tuning = params[P_TUNE].getValue() > 0.5f;
	if (tuning != wantTune.load())
		wantTune.store(tuning);
	// TRY AND SAVE, as they are pressed, for the main thread.
	{
		const bool t = params[P_TRY].getValue() > 0.5f, s = params[P_SAVE].getValue() > 0.5f;
		if (t && !tryWas)
			tryPressed.store(true);
		if (s && !saveWas)
			savePressed.store(true);
		tryWas = t;
		saveWas = s;
	}
	if (BrassEngine* e = next.exchange(nullptr)) {
		BrassEngine* old = engine;
		engine = e;
		if (old && !retired.load())
			retired.store(old);
		else
			delete old;
		announcedCount = 0;
		liveHandle = -1;
		gateSounding = false;
		refNow = nullptr;
	}
	if (relink.exchange(false)) {
		instrumentChange = 0;
		partOutOfRange = false;
		reader.clear();
		const int n = wantCount.load();
		for (int i = 0; i < n; i++)
			reader.add(wantSlots[i].load(), wantGenerations[i].load());
	}

	// THE CABLE GOING IS EVERYTHING STOPPING.
	const bool attached = reader.attached();
	if (!attached && attachedWas && engine) {
		engine->withdraw();
		announcedCount = 0;
		liveHandle = -1;
		refNow = nullptr;
	}
	attachedWas = attached;

	// WHETHER THE PART GOES WHERE THE TROMBONE CANNOT, from the compass the cable gives: said to the source
	// every tenth of a second while it is so (Bus::cannotPlayAt), its notes there played an octave in.
	Instrument in;
	if (attached && reader.instrument(in)) {
		if (in.change != instrumentChange) {
			instrumentChange = in.change;
			auto hzOfMidi = [](int m) { return 440.f * std::pow(2.f, ((float) m - 69.f) / 12.f); };
			partOutOfRange = (in.lowest >= 0 && hzOfMidi(in.lowest) < lowestHz * 0.99f)
				|| (in.highest >= 0 && hzOfMidi(in.highest) > highestHz * 1.01f);
		}
	}
	else {
		partOutOfRange = false;
		instrumentChange = 0;
	}
	if (partOutOfRange && args.frame % (int64_t) std::max(1.f, 0.1f * args.sampleRate) == 0)
		reader.cannotPlay();

	if (!engine) {
		outputs[O_OUT].setVoltage(0.f);
		return;
	}

	const int64_t now = APP->engine->getFrame();
	// WHILE TUNING, EVERY NOTE AS IT ARRIVES: nothing announced is played ahead, so that the knobs and REF are
	// read as each note begins.
	if (tuning && engine->live()) {
		Coming c;
		while (reader.nextComing(c)) {
		}
		Event e;
		Event top;
		bool have = false;
		while (reader.next(e)) {
			if (e.kind == Event::ON) {
				noteHold = NOTE_HOLD;
				if (!have || e.pitch > top.pitch) {
					top = e;
					have = true;
				}
			}
		}
		if (have)
			playTuning(top, now, args.sampleRate);
	}
	else {
		// THE NOTES COMING: of notes announced for one moment, the highest is given to the player and the rest
		// only remembered, so that their ONs are not played as notes nobody announced.
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
		// THE NOTES ARRIVING: one announced is already the player's; one nobody announced is played now.
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
	}

	// THE BRIGHTNESS, a shelf on what the trombone puts out; fully clockwise it passes the sound unchanged.
	{
		const float bright = params[P_BRIGHT].getValue();
		if (bright != shelf.madeFor || args.sampleRate != shelf.rateFor) {
			shelf.set(BRIGHT_CORNER_HZ, -BRIGHT_MOST_DB * (1.f - math::clamp(bright, 0.f, 1.f)), args.sampleRate);
			shelf.madeFor = bright;
		}
	}
	float pascals = engine->process();
	// THE MODEL'S LEVEL, as each note tuned plays, for the recording.
	if (measuring >= 0 && now >= measureFrom) {
		if (now < measureTo) {
			measureSum += (double) pascals * pascals;
			measureCount++;
		}
		else {
			if (measureCount > 0)
				modelLevel[measuring][measuringMarking] = (float) std::sqrt(measureSum / (double) measureCount);
			measuring = -1;
		}
	}
	// THE RECORDING, while one sounds.
	if (refNow) {
		if (refLeft > 0 && refAt < refNow->x.size()) {
			const float fade = std::min(1.f, (float) refLeft / (float) refFade);
			pascals += refGain * fade * refNow->x[refAt];
			refAt++;
			refLeft--;
		}
		else
			refNow = nullptr;
	}
	// ONE OUTPUT, nothing above ten volts: the last volt and a half is a curve (mpxSoftClip). The lamp reads
	// the level before it, so red says the curve is rounding peaks.
	const float raw = shelf.run(pascals * VOLTS_PER_PASCAL);
	outputs[O_OUT].setVoltage(mpxSoftClip(raw));

	const float peak = std::fabs(raw);
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
	lights[L_BUSY].setBrightness(busy.load() ? 1.f : 0.f);
}


// ---- the panel ---------------------------------------------------------------------------------

/** TWENTY HP: the playing column of eight on the left, as it was, and the tuning beside it — the TUNE switch
and the display along the top, the knobs across in two rows, the start's and the lips', and TRY, SAVE and
REF under them. */
static const float WIDTH = 101.6f;
/** The playing column's middle, eight HP wide. */
static const float COLUMN = 20.32f;


static Layout tromboneLayout() {
	Layout L;
	L.hp = WIDTH / 5.08f;
	L.title = "MPX TROMBONE";
	auto label = [&](const char* key, float x, float y, const char* text, const char* owner, bool heading, float size) {
		Item i;
		i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
		i.defaultText = text; i.align = Panel::CENTRE; i.heading = heading; i.size = size;
		if (owner)
			i.owner = owner;
		L.items.push_back(i);
	};
	auto jack = [&](const char* key, Item::Kind kind, float x, float y, int id, const char* name, NVGcolor colour) {
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
	auto button = [&](const char* key, int id, const char* style, float x, float y, const char* name) {
		Item i;
		i.key = key; i.kind = Item::PARAM; i.id = id; i.style = style; i.x = x; i.y = y;
		L.items.push_back(i);
		label((std::string(key) + ".label").c_str(), x, y + 7.f, name, key, true, 0.f);
	};

	// THE PLAYING COLUMN, as the bowed strings' panel is: the jacks 14.5 mm apart, each name 7 mm under its jack.
	const float x = COLUMN, pitch = 14.5f, top = 20.5f;
	// THE DIAGNOSTICS, along the top, shown or hidden from the right-click menu: the count of notes played
	// without trying for want of time (see HurriedDisplay).
	{
		Item hurried;
		hurried.key = "d.hurried"; hurried.kind = Item::DISPLAY;
		hurried.x = 2.f; hurried.y = 9.3f; hurried.w = 10.f; hurried.h = 3.4f;
		L.items.push_back(hurried);
	}
	jack("in.mpx", Item::PORT_IN, x, top, TromboneModule::I_MPX, "MPX IN", NOTE_CABLE);
	jack("in.gate", Item::PORT_IN, x, top + pitch, TromboneModule::I_GATE, "GATE", SIG_GATE);
	jack("in.pitch", Item::PORT_IN, x, top + 2.f * pitch, TromboneModule::I_PITCH, "V/OCT", SIG_PITCH);
	jack("in.level", Item::PORT_IN, x, top + 3.f * pitch, TromboneModule::I_LEVEL, "LEVEL", SIG_CV);
	knob("p.bright", TromboneModule::P_BRIGHT, x, 85.f, "BRT");
	{
		Item lamp;
		lamp.key = "lamp.level"; lamp.kind = Item::LIGHT; lamp.style = "light.level";
		lamp.id = TromboneModule::L_LEVEL; lamp.x = x; lamp.y = 108.f;
		L.items.push_back(lamp);
	}
	jack("out", Item::PORT_OUT, x, 114.5f, TromboneModule::O_OUT, "OUT", SIG_AUDIO);

	// THE TUNING: the switch and the display along the top, the knobs across, the buttons under them. The
	// knobs 14.5 mm apart, as the jacks are; the buttons under the gaps between them.
	button("p.tune", TromboneModule::P_TUNE, "latch", 49.f, 20.f, "TUNE");
	{
		Item display;
		display.key = "d.tuning"; display.kind = Item::DISPLAY;
		display.x = 58.f; display.y = 13.f; display.w = 42.f; display.h = 18.f;
		L.items.push_back(display);
	}
	// THE START, THE BREATH AND THE LIPS' MASS, set once, in one row; the lips in the next; the buttons under
	// them; the knobs 16 mm apart, their names 8.5 mm under them. BUSY, which the lips' knobs light, between
	// TENSION and GAP, its name over it, so that it is seen with them close up.
	const float first = 56.f, apart = 16.f, lipsY = 68.f;
	knob("p.rise", TromboneModule::P_RISE, first, 46.f, "RISE");
	knob("p.breath", TromboneModule::P_BREATH, first + apart, 46.f, "BREATH");
	knob("p.mass", TromboneModule::P_MASS, first + 2.f * apart, 46.f, "MASS");
	knob("p.tension", TromboneModule::P_TENSION, first, lipsY, "TENSION");
	knob("p.gap", TromboneModule::P_GAP, first + apart, lipsY, "GAP");
	knob("p.damp", TromboneModule::P_DAMP, first + 2.f * apart, lipsY, "DAMP");
	const float buttonsY = 88.f;
	{
		Item lamp;
		lamp.key = "lamp.busy"; lamp.kind = Item::LIGHT; lamp.style = "light.medium";
		lamp.id = TromboneModule::L_BUSY; lamp.x = first + 0.5f * apart; lamp.y = lipsY;
		L.items.push_back(lamp);
		label("lamp.busy.label", first + 0.5f * apart, lipsY - 6.f, "BUSY", "lamp.busy", true, 0.f);
	}
	button("p.try", TromboneModule::P_TRY, "button", first, buttonsY, "TRY");
	button("p.save", TromboneModule::P_SAVE, "button.green", first + apart, buttonsY, "SAVE");
	button("p.ref", TromboneModule::P_REF, "button", first + 2.f * apart, buttonsY, "REF");
	L.bindOffsets();
	return L;
}


/** HOW MANY NOTES WERE PLAYED WITHOUT TRYING for want of time, though the Fidelity asked for trying: the
player had less than a second in hand. A busy machine is the usual cause; level 2 tries a third as many
ways. Counted from nought whenever the Fidelity or the Economy changes. */
struct HurriedDisplay : widget::Widget {
	TromboneModule* module = nullptr;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/Hack-Regular.ttf"));
		if (!font || font->handle < 0)
			return;
		const std::string text = std::to_string(module ? module->hurried() : 0);
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 10.f);
		nvgFillColor(vg, PANEL_INK);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int pass = 0; pass < 2; pass++)
			nvgText(vg, box.size.x / 2.f, box.size.y / 2.f, text.c_str(), NULL);
	}
};


/** THE TUNING'S DISPLAY: two lines in capitals — the note and marking playing, and below it whether its
settings are kept, which start found is on the knobs, or that the search is running. */
struct TuningDisplay : widget::Widget {
	TromboneModule* module = nullptr;

	void draw(const DrawArgs& args) override {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/Hack-Regular.ttf"));
		if (!font || font->handle < 0)
			return;
		NVGcontext* vg = args.vg;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.f, 0.f, box.size.x, box.size.y, 3.f);
		nvgStrokeColor(vg, PANEL_INK);
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);
		const std::string first = module ? module->displayFirst : "TUNE OFF", second = module ? module->displaySecond : "";
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, 16.f);
		nvgFillColor(vg, PANEL_INK);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		for (int pass = 0; pass < 2; pass++) {
			nvgText(vg, box.size.x / 2.f, box.size.y * 0.3f, first.c_str(), NULL);
			nvgText(vg, box.size.x / 2.f, box.size.y * 0.72f, second.c_str(), NULL);
		}
	}
};


//?module Plays one MPX part on a modelled tenor trombone: an air column with a bell, buzzed by a model of
//? the lips, each note's slide position, breath and start chosen by a model of a player.
//?note From a score, each note is announced to the module four and a half seconds before it sounds. At
//? Fidelity levels 1 and 2, ways of starting it, or of slurring into it, are tried on a copy of the
//? trombone while the music plays, and the best is played. A note from a keyboard or the ports, which
//? nothing announced, is played as it arrives.
//?note One trombone is one player: a chord on the cable is played as its top note. A note below E2, the
//? lowest the slide reaches above the pedal notes, or above F sharp 5, is played an octave or more in,
//? and while the part goes there the plug light on the source's output blinks.
//?note How loud each note is played sets how hard it is blown, and so how bright it is: a loud note is
//? brassy because it is blown hard, not because it is turned up.
//?note Tuning by ear: with TUNE lit, loop a passage on one pitch in mpxScore and turn RISE, BREATH,
//? TENSION, GAP and DAMP until the start and the tone are right, pressing TRY to hear the other starts found
//? for the note and holding REF to hear the Iowa recording of it; then SAVE. The settings are kept for
//? that pitch and marking in the Rack user folder, under DreamerMPX/trombone-tuning.json, and played
//? whenever that note comes, in every patch, with TUNE unlit as well. MASS, the lips' mass, is one
//? setting for the whole trombone, kept there too.
//?note Fidelity, in the right-click menu, sets how much is tried on a copy of the trombone before each
//? note sounds. Level 1 tries every way: sixty starts, and about a hundred ways of making each slur. Level
//? 2, the default, tries a third of them. Level 3 tries nothing: each note is played as it arrives, by
//? what the player has found before in the same place or else by habit, and nothing needs announcing.
//? At levels 1 and 2 the module works three and a half seconds ahead of what is heard.
//?note Enable diagnostics, in the right-click menu, shows the number along the top of the panel; it is on
//? to begin with. It counts the notes played without trying, at levels 1 and 2, because the trying had
//? fallen behind: a busy computer is the usual cause, and level 2 needs a third of the computing. It
//? counts from nought whenever Fidelity or Economy changes.
//?note Economy, in the right-click menu, leaves out the steepening of the sound wave on its way down the
//? bore, for less computing; the loudest notes are then less brassy.
struct TromboneWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;
	int diagnosticsShown = -1;

	TromboneWidget(TromboneModule* module) {
		setModule(module);
		layout = tromboneLayout();
		layoutApplyUser("mpxTrombone", layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);
		HurriedDisplay* hurried = new HurriedDisplay;
		hurried->module = module;
		layoutPlaceDisplay(this, layout, "d.hurried", hurried);
		TuningDisplay* tuning = new TuningDisplay;
		tuning->module = module;
		layoutPlaceDisplay(this, layout, "d.tuning", tuning);
	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, "mpxTrombone");
		TromboneModule* m = dynamic_cast<TromboneModule*>(module);
		if (!m)
			return;
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Fidelity",
			{"Level 1: every way of each start and slur tried ahead, the most computing",
			 "Level 2: a third of the ways tried ahead (default)",
			 "Level 3: live, nothing tried ahead or needed ahead, the least computing"},
			[=]() { return (size_t) m->fidelity(); }, [=](size_t f) { m->setFidelity((int) f); }));
		menu->addChild(createBoolMenuItem("Economy: less computing, less brassy at the loudest", "",
			[=]() { return m->economy(); }, [=](bool on) { m->setEconomy(on); }));
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createBoolMenuItem("Enable diagnostics", "",
			[=]() { return m->diagnostics(); }, [=](bool on) { m->setDiagnostics(on); }));
		if (m->silent() > 0)
			menu->addChild(createMenuLabel(string::f("Silent while behind: %.1f s", (double) m->silent() / (double) APP->engine->getSampleRate())));
		menu->addChild(createMenuItem("Open the folder the tuning is kept in", "", []() {
			const std::string folder = asset::user("DreamerMPX");
			system::createDirectories(folder);
			system::openDirectory(folder);
		}));
	}

	/** The MPX link, made here on the main thread, as every MPX input's is; the engine rebuilt when the rate
	or a setting has changed; the tuning kept up; and the diagnostics shown or hidden. */
	void step() override {
		ModuleWidget::step();
		TromboneModule* m = dynamic_cast<TromboneModule*>(module);
		if (!m)
			return;
		m->build();
		m->tuningStep();
		const bool show = m->diagnostics();
		if ((int) show != diagnosticsShown) {
			diagnosticsShown = show;
			if (Item* item = layout.find("d.hurried"))
				if (item->widget)
					item->widget->visible = show;
		}
		PortWidget* port = getInput(TromboneModule::I_MPX);
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
};


} // namespace px


Model* modelMpxTrombone = createModel<px::TromboneModule, px::TromboneWidget>("mpxTrombone");
