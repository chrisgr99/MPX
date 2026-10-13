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

A SECTION OF PLAYERS, one instrument each: a line is played by one, and notes sounding together, a
chord on the cable or the channels of a polyphonic gate, by as many as there are notes (BowedSection).

ONE INSTRUMENT A SLUG, each a short file of its own: what instrument it is, and its controls with
their help, which has to sit beside the call that configures each one (helpscan). Everything that
plays is here, once. */
#include "plugin.hpp"
#include "BowedEngine.hpp"
#include "Layout.hpp"
#include "NoteBus.hpp"

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
};


/** THE PLAYERS, a section of them, each an instrument and a player of its own, set up alike but for a
few cents of tuning and the pace of the vibrato, as no two players in a section are quite alike. A
player with nothing to play rests, and costs next to nothing (BowedPlayer::process). */
struct BowedSection {
	static const int MOST = 16;
	BowedEngine player[MOST];
	/** EACH PLAYER'S TUNING, cents from true: none for the first, so that a line played alone is in
	tune; the others spread over four cents either way. */
	static float centsOf(int p);
};


struct BowedModule : Module, NoteSink {
	enum ParamId {
		P_VIBRATO,
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
	/** A NEW SECTION where the rate, the part's range or a setting has changed, made ready for the audio
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

	const BowedKind kind;

private:
	float clipHold = 0.f, nearHold = 0.f, noteHold = 0.f;

	BusReader reader;
	Instrument instrument;
	uint32_t instrumentChange = 0;
	std::atomic<int> wantSlots[MAX_UPSTREAM];
	std::atomic<uint32_t> wantGenerations[MAX_UPSTREAM];
	std::atomic<int> wantCount{0};
	std::atomic<bool> relink{false};
	bool attachedWas = false;

	/** THE SECTION PLAYING, on the audio thread; the next, built on the main thread and taken up at the
	next sample; and the last, handed back for the main thread to take apart. */
	BowedSection* section = nullptr;
	std::atomic<BowedSection*> next{nullptr};
	std::atomic<BowedSection*> retired{nullptr};
	/** What the section was last built for, main thread; and what is wanted, set from the audio
	thread. */
	float builtRate = 0.f, builtHighest = 0.f;
	bool builtEconomy = false;
	std::atomic<bool> wantEconomy{false};
	int builtRehearsing = -1;
	std::atomic<int> wantRehearsing{BowedPlayer::LIVE};
	std::atomic<float> wantRate{48000.f};
	std::atomic<float> wantHighest{1280.f};

	/** THE NOTES ANNOUNCED, by handle, whose ON is then not played again when it comes. */
	static const int ANNOUNCED_MOST = 512;
	int64_t announced[ANNOUNCED_MOST] = {};
	int announcedFirst = 0, announcedCount = 0;
	/** WHAT EACH PLAYER HAS BEEN GIVEN from the cable: until when its notes go on, engine frames; the
	pitch of the last, volts; and the note from a keyboard it holds, for its OFF. */
	struct Seat {
		int64_t until = 0;
		float pitch = 0.f;
		int64_t liveHandle = -1;
	};
	Seat seats[BowedSection::MOST];
	/** THE GATE'S CHANNELS, each played by the player of its number: up or down with a little
	hysteresis, whether a note from it is sounding, and at what pitch, volts. */
	bool gateUp[BowedSection::MOST] = {};
	bool gateSounding[BowedSection::MOST] = {};
	float gatePitch[BowedSection::MOST] = {};
	void playFromPorts();

	/** NOTES STARTING TOGETHER, given out to the players, the highest first: announced ahead, or played
	now. */
	void shareOut(Coming* notes, int n, int64_t now, bool announced);
	/** Which player takes a note starting at `frame`, at `pitch` volts, of those not `taken`; -1 where
	every one is. */
	int seatFor(int64_t frame, float pitch, const bool* taken) const;
	/** Everything announced and not begun, void, for every player. */
	void withdrawAll(int64_t now);
	static BowedPlayer::Note noteOf(const Event& e, int player);
	bool wasAnnounced(int64_t handle);
	void rememberAnnounced(int64_t handle);
};


/** THE PANEL, four HP, one column: the part in at the top, the ports to play it without a cable, the
vibrato, and the level lamp just above the output. */
struct BowedWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;
	std::string slug;

	BowedWidget(BowedModule* module, const std::string& slug);
	void appendContextMenu(ui::Menu* menu) override;
	/** The MPX link, made here on the main thread, as every MPX input's is; and the engine rebuilt
	when the rate or the part's range has changed. */
	void step() override;
};


} // namespace px
