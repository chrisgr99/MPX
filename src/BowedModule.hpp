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


struct BowedModule : Module, NoteSink {
	enum ParamId {
		P_VIBRATO,
		NUM_PARAMS
	};
	enum InputId {
		I_MPX,
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
	/** A NEW ENGINE where the rate or the part's range has changed, made ready for the audio thread to
	take up; and the one it gave back taken apart. Main thread. */
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
	std::atomic<int> wantRehearsing{BowedPlayer::STARTS_GUESSED};
	std::atomic<float> wantRate{48000.f};
	std::atomic<float> wantHighest{1280.f};

	/** THE NOTES ANNOUNCED, by handle, whose ON is then not played again when it comes. */
	static const int ANNOUNCED_MOST = 512;
	int64_t announced[ANNOUNCED_MOST] = {};
	int announcedFirst = 0, announcedCount = 0;
	/** The note from a keyboard sounding, for its OFF. */
	int64_t liveHandle = -1;

	static BowedPlayer::Note noteOf(const Event& e);
	bool wasAnnounced(int64_t handle);
	void rememberAnnounced(int64_t handle);
};


/** THE PANEL, eight HP: the part in at the top, the vibrato, the output and the level lamp. */
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
