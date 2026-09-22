/** mpxMelody — a line drawn against the harmony on the cable, and expanders for two more.

NOT BUILT YET. These are the panels and nothing else: every control exists, is named and is saved
with the patch, and process() does nothing at all. They are here so the arrangement can be
settled in the panel editor before the engine is written, since the panel is the part that has to
be lived with and the engine is the part that can be changed without anyone noticing.

WHAT IT WILL DO, so that the controls make sense while they are being moved.

A RHYTHM SOURCE SAYS WHEN, AND THIS SAYS WHAT. mpxEuclid and anything else that emits notes with
a velocity and a duration and no pitch feeds the rhythm input; this fills the pitch in and passes
the note on. That is why there is an input per voice rather than one input carrying everything:
a note on an MPX cable does not say where it came from, so the port is what identifies the voice,
and the patch says visibly which line is which.

ONE VOICE, AND EXPANDERS FOR MORE. The common case is a single line over a chart, and that should
not cost the panel space of three. The base module is the melody; an expander beside it adds a
bass or an inner voice, and a second expander adds the other.

THE ENGINE STAYS IN THE BASE MODULE. An expander contributes controls, jacks and an output, and
computes nothing. It has to be that way: the voices are resolved in the order bass, melody, inner
— whichever draws first is the least constrained — and each must see what the ones before it just
took, which is impossible if the three are separate engines. So the base reads its expanders'
knobs, draws all the voices in one place, and pushes each voice's notes onto that expander's own
output.

AN EXPANDER ON ITS OWN IS SILENT, AND SAYS SO. Rack will let anybody drop one anywhere, and an
orphan looks exactly like a working one — same panel, same jacks, and a cable out of it that
carries nothing. So it carries a lamp that is lit only while something is driving it. Nothing is
guessed at: it does not go looking for the nearest base module, because a module that reaches
across the rack for a partner is a module whose behaviour cannot be read off the rack.

THE ROLE IS A SWITCH, NOT A POSITION. Otherwise the first expander would always be the bass and
adding them in the other order would quietly give the wrong voice. With a switch, the resolution
order stays musical while the physical order stays whatever is convenient.

THE DRAW IS A CABLE. Each voice's pitch is selected by one number between nought and one, and in
Rack that number is whatever you patch. A slow shape draws a contour; noise scatters. A source
whose period is a whole number of harmony cycles makes the passage recur, which is what makes
generated music sound composed rather than endless. Unpatched, the seed and the beat already on
the cable stand in, so a voice still recurs on its own.

THE HARMONY COMES FROM THE CHART INPUT, not from the rhythm cables — mpxEuclid is a source with
no MPX input, so nothing reaches it to forward. The base module holds the only chart jack; the
expanders take their harmony from it.
*/
#include "plugin.hpp"

#include <algorithm>
#include <ctime>
#include "Layout.hpp"
#include "NoteBus.hpp"
#include "Melodic.hpp"
#include "MelodyVoice.hpp"
#include "ChartLayout.hpp"

using namespace px;


/** THE CONTROLS ONE VOICE HAS. The same set on the base module and on an expander, which is why
it is a list of its own rather than part of either module's parameter enum. */
enum PartParam {
	PP_SMOOTH,
	PP_LOCK,
	PP_REGISTER,
	PP_SPAN,
	PP_ARTIC,
	PP_ACCENT,
	PP_LEADING,
	PP_SCALE,
	PP_BREATH,
	PP_END,
	PP_COUNT,
};

/** WHICH LINE A VOICE IS. Also the order they are resolved in — bass first, because the
foundation should not be pushed around by anything, then the melody, which is the line anybody
will actually remember, then the inner voice, which is the one that can afford to be constrained
by both. */
/** How many voices one master will drive. More than anybody has asked for; the walk stops at
the first neighbour that is not one, so the number is a guard rather than a limit anybody meets. */
static const int MAX_VOICES = 8;

enum PartRole {
	ROLE_BASS,
	ROLE_MELODY,
	ROLE_INNER,
	NUM_ROLES,
};

/** What a voice comes up as. GXW's three styles, which are points in the same two-knob space
this panel exposes: smoothness sets the window and the leap aversion, chord lock sets the pull
toward chord tones against the allowance for passing ones. */
struct Preset {
	const char* name;
	float smooth, lock;
	float centre, span;
	float artic, accent;
	float leading;
	float breath, endAtChange;
};
static const Preset& rolePreset(int role) {
	static const Preset presets[NUM_ROLES] = {
		// centre is a MIDI note: 45 is A2, low in a bass's range; 72 is C5, an octave above
		// middle C; 62 is D4, between them. The leading figures are GXW's own: a bass leans
		// hardest into the next root, a melody less, an inner voice less again.
		{"Bass",   0.55f, 0.85f,  45.f, 19.f,  0.55f, -0.20f, 2.5f, 0.00f, 0.50f},
		{"Melody", 0.75f, 0.55f,  72.f, 24.f,  0.00f,  0.30f, 1.8f, 1.00f, 0.60f},
		{"Inner",  0.70f, 0.70f,  62.f, 20.f, -0.10f,  0.00f, 1.4f, 0.50f, 0.70f},
	};
	return presets[role];
}

/** A PARAMETER THAT READS AS A NOTE. The value is a MIDI note number, which is what the engine
wants; what it shows is C4, F#2, A#5 — which is what a musician wants, and the two have no
business being the same thing.

C4 IS MIDDLE C, which is Rack's own convention: nought volts is C4 and every module in the rack
agrees with it. The other convention in the world calls that note C3, and choosing it here would
put this module a whole octave out of step with everything it is patched to.

SHARPS AND NOT FLATS, because a pitch class has no key to be spelt in until it is in one. B flat
and A sharp are the same key and this control is choosing a key. */
struct NoteQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		static const char* NAMES[12] = {"C", "C#", "D", "D#", "E", "F",
			"F#", "G", "G#", "A", "A#", "B"};
		const int note = (int) std::round(getValue());
		const int pc = ((note % 12) + 12) % 12;
		return std::string(NAMES[pc]) + std::to_string(note / 12 - 1);
	}
};

/** Names and describes one voice's worth of parameters, wherever they sit in a module's list. */
static void configPart(Module* m, int base, int role) {
	const Preset& d = rolePreset(role);

	// THE TWO MACRO CONTROLS THE WHOLE PROFILE COMES FROM. Everything the weighting does — how
	// far a note may leap, how nearly a tritone is forbidden, how hard a chord tone pulls on a
	// strong beat, how much a passing tone is allowed — is generated from these two numbers.
	// They are on the panel because they are the two worth a hand during a take.
	m->configParam(base + PP_SMOOTH, 0.f, 1.f, d.smooth, "Smoothness", "%", 0.f, 100.f);
	m->configParam(base + PP_LOCK, 0.f, 1.f, d.lock, "Chord lock", "%", 0.f, 100.f);

	// WHERE THE LINE LIVES, and how much room it has. Two knobs rather than a low and a high,
	// because moving a line up an octave is one gesture and widening it is another, and a
	// low-and-high pair makes both of them two.
	// THE NOTE THE LINE IS CENTRED ON, named rather than numbered. It ran from minus thirty-six
	// to plus thirty-six semitones from middle C, which is a true description of the same thing
	// and a useless one to anybody thinking about where a bass should sit.
	//
	// C1 TO C7, which is six octaves and everything a line in a rack is likely to want. The
	// whole MIDI range would be a hundred and twenty-eight entries in the list, most of them
	// below or above anything anybody would centre a melody on.
	m->configParam<NoteQuantity>(base + PP_REGISTER, 24.f, 96.f, d.centre, "Register");
	m->paramQuantities[base + PP_REGISTER]->snapEnabled = true;
	m->configParam(base + PP_SPAN, 7.f, 36.f, d.span, "Span", " semitones");
	m->paramQuantities[base + PP_SPAN]->snapEnabled = true;

	// BIPOLAR, WITH THE RHYTHM'S OWN VALUE AT THE CENTRE. The rhythm source owns the duration
	// and the level; these say how far the melody may move them, and which way. Centre is hands
	// off. To the right a chord tone is held, a phrase is slurred, and at the end of the sweep a
	// note runs past the next one, which is legato — audible as phrasing because the notes
	// between phrases are not slurred. To the left the same reading shortens instead: passing
	// tones clipped hardest.
	//
	// One knob and not two, because an amount and a direction are one thing here, and a separate
	// depth control would let you set a direction that does nothing.
	m->configParam(base + PP_ARTIC, -1.f, 1.f, d.artic, "Articulation");
	m->configParam(base + PP_ACCENT, -1.f, 1.f, d.accent, "Accent");

	// HOW HARD THIS LINE IS PULLED TOWARD THE NEXT CHORD as the change approaches. Per voice,
	// because it is what separates a bass that walks to the next root from an inner voice
	// content to stay where it is.
	m->configParam(base + PP_LEADING, 0.f, 3.f, d.leading, "Voice leading");

	// THE VOICE'S OWN SCALE. The key comes from the chart; this is what the line is allowed to
	// draw from within it, which is how a lead on a minor pentatonic over an ordinary chart
	// sounds like a player rather than like a quantizer.
	//
	// A SCALE IS NOT A KEY. Every setting here is built on the chart's tonic, so transposing the
	// chart moves them all; what changes is the palette between the chord tones. A line in the
	// minor pentatonic over a major chart is the blues third, not a mistake, and no reading of
	// the chart will ever produce it.
	//
	// FROM CHORD IS NOT BUILT. It is the setting a player would actually ask for — a scale per
	// chord rather than per song, dorian on a two chord and mixolydian on a five — and it is
	// here so that the choice exists on the panel and in the saved patch from the first build,
	// rather than being appended later where it would read as an afterthought. Until it is
	// written it behaves as KEY.
	m->configSwitch(base + PP_SCALE, 0.f, 4.f, 0.f, "Scale",
		{"Key", "Maj pent", "Min pent", "Blues", "From chord"});

	// HOW LONG THE LINE STOPS AT THE END OF A PHRASE. A singer breathes; a foundation carries
	// on. In beats, and nought is carrying on — which is why this is a knob and not the switch
	// it began as. GXW has it as a flag because it was a style setting rather than something a
	// hand reaches for, and a flag can only rest the last beat: half a beat, or two, is as
	// musical a choice as whether to breathe at all, and it is the length that makes a breath
	// sound like phrasing rather than like a dropped note.
	//
	// Notes are DROPPED, not shortened. A note shortened to nothing is silence arrived at by
	// arithmetic; a rest is a decision, and the next note after it starts the next phrase.
	//
	// Needs the phrase position on the cable, which mpxChart does not publish yet — see the
	// note at the foot of this file.
	m->configParam(base + PP_BREATH, 0.f, 2.f, d.breath, "Breath", " beats");

	// A NOTE STILL SOUNDING WHEN THE CHORD CHANGES UNDER IT. Turned up, more of them are ended
	// at the change; at the top, every one of them is.
	//
	// NAMED FOR THE ENDING RATHER THAN THE HOLDING, and turning the other way, because ending is
	// the thing the module does TO a note and holding is what happens when it does not. The
	// other way round the knob was asking the reader to think in negatives, and its top end was
	// a promise it could not keep — "always hold" is mechanical and mostly wrong.
	//
	// A DEGREE AND NOT A STATE, because a player ends some and holds others, and which is not
	// arbitrary. What the knob governs is how far down a list of cases the module is willing to
	// hold, and how often within them. Three cases, in order of how safe holding is, and the
	// module can tell them apart because the harmony block gives it the new chord at the moment
	// of the change.
	//
	//   A COMMON TONE — the note is also a tone of the new chord. Holding it is not a suspension
	//   at all; it is the thing good voice leading does, and ending it is what sounds
	//   mechanical. The last case to be given up as the knob comes up.
	//
	//   A SUSPENSION — the note is in the scale but not in the new chord. Musical only if it
	//   RESOLVES, which means the next note must step down onto a chord tone. Given up earlier,
	//   and it costs something: see the note at the foot of this file.
	//
	//   OUTSIDE THE SCALE — ended, at any setting, including nought. There is no reading in
	//   which it was meant.
	//
	// So nothing anywhere on the sweep holds a note that has no reading, and the bottom of the
	// sweep is not "everything holds": it is "everything that could musically hold, does".
	m->configParam(base + PP_END, 0.f, 1.f, d.endAtChange, "End notes at changes");
}


// ---- the expander --------------------------------------------------------------------------

/** The linking a module does for one MPX input: what the widget found, handed across to the
audio thread without either of them waiting for the other.

WHY IT IS NOT A DIRECT WRITE. The widget walks the cables once a frame on the main thread, and
the reader is used every sample on the audio thread. A reader rebuilt underneath a drain is a
reader with a cursor pointing into a bus it is no longer reading, so the want is published as
atomics and taken up by the audio thread at a moment of its own choosing. */
struct Upstream {
	std::atomic<int> slots[MAX_UPSTREAM];
	std::atomic<uint32_t> generations[MAX_UPSTREAM];
	std::atomic<int> count{0};
	std::atomic<bool> relink{false};

	Upstream() {
		for (int i = 0; i < MAX_UPSTREAM; i++) {
			slots[i].store(-1);
			generations[i].store(0);
		}
	}

	/** Main thread. Says what the cables are now; does nothing if they have not changed, so an
	unchanged frame does not reset a cursor and replay nothing. */
	void want(const int* s, const uint32_t* g, int n) {
		bool same = (n == count.load());
		for (int i = 0; same && i < n; i++)
			same = (s[i] == slots[i].load()) && (g[i] == generations[i].load());
		if (same)
			return;
		for (int i = 0; i < n && i < MAX_UPSTREAM; i++) {
			slots[i].store(s[i]);
			generations[i].store(g[i]);
		}
		count.store(std::min(n, MAX_UPSTREAM));
		relink.store(true);
	}

	/** Audio thread. Takes up a change if one has been asked for. */
	void settle(BusReader& reader) {
		if (!relink.exchange(false))
			return;
		reader.clear();
		const int n = count.load();
		for (int i = 0; i < n; i++) {
			const int slot = slots[i].load();
			if (slot >= 0)
				reader.add(slot, generations[i].load());
		}
	}
};


struct VoiceModule : Module, NoteSource, NoteSink {
	enum ParamId {
		P_PART = 0,
		/** WHICH LINE THIS IS, and so where it falls in the resolution order. Changing it does
		NOT reset the knobs: a role is what the voice is for, and somebody who has set a line up
		the way they want it should not lose that by saying what it is. */
		P_ROLE = PP_COUNT,
		/** APPENDED, NEVER INSERTED. How often the line stays on the note it is on. Outside the
		part block because the part block's numbers are fixed by patches already saved; see
		voiceRepeatWeight for what the knob's travel means. */
		P_REPEAT,
		/** APPENDED. How surely a restated rhythm brings its pitches back, and how hard each
		breath group is pulled into a sung line's shape. */
		P_MOTIF,
		P_CONTOUR,
		PARAMS_LEN,
	};
	enum InputId { I_RHYTHM, I_RAND, I_PROFILE, INPUTS_LEN };
	enum OutputId { O_PART, OUTPUTS_LEN };
	enum LightId {
		L_PART,
		/** WHETHER ANYTHING IS DRIVING THIS. An expander dragged away from its module, or added
		on its own, looks exactly like one that is working — same panel, same jacks, and a cable
		out of it that carries nothing. */
		L_LINKED,
		LIGHTS_LEN,
	};

	/** THE BUS THIS VOICE WRITES TO. Its own, not the master's: a cable is drawn from THIS
	panel's jack, so this is the module a listener downstream has to be able to find. The master
	does the thinking and pushes the notes here, which is why the engine can live in one place
	while every voice still has an output of its own. */
	int slot = -1;
	uint32_t generation = 0;

	/** What is patched into this voice's rhythm input, and where it has read up to. */
	Upstream upstream;
	BusReader rhythm;

	/** THE NOTE THIS LINE PLAYED LAST, which is the whole of its memory.

	PER VOICE, AND IT HAS TO BE. Two lines sharing one previous note would each choose as though
	the other's note were its own, and the result would look like a bug and sound like a mess.

	Cleared when nothing has been played yet, so the first note of a line is chosen by the
	register and the harmony rather than by whatever the last patch left behind. */
	int previous = -1;
	/** The note before that, so the line knows which way it is going. */
	int beforePrevious = -1;
	/** Every note this voice has chosen, for a figure coming back to find. See VoiceMemory. */
	VoiceMemory memory;

	/** Where this voice fell in the drawing order this sample. Only for the record. */
	int order = 0;

	/** WHAT THIS VOICE'S SETTINGS WERE WHEN LAST WRITTEN DOWN, so a change can be noticed.
	Nought-to-something is always a change: NaN never equals anything. */
	float lastSettings[PARAMS_LEN];

	/** A note this voice has sounded and not yet ended. One at a time: a voice is a line, and a
	line plays one note. Simultaneous notes are what the other voices are for. */
	int64_t sounding = 0;
	bool isSounding = false;
	float fade = 0.f;

	/** THE VOICE ENDS ITS OWN NOTES. What the rhythm sent says when a note would end; the voice
	may shorten it, hold it, slur it into the next or cut it at a chord change, so it keeps its
	own count rather than waiting for the rhythm's. */
	int ownLeft = 0;
	bool slur = false;
	/** A note handed over to the next and still sounding under it, for a moment. */
	bool tailing = false;
	int64_t tailHandle = 0;
	int tailLeft = 0;

	VoiceModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		slot = busClaim(&generation);
		for (int i = 0; i < PARAMS_LEN; i++)
			lastSettings[i] = NAN;
		configPart(this, P_PART, ROLE_BASS);
		configSwitch(P_ROLE, 0.f, (float) (NUM_ROLES - 1), (float) ROLE_BASS, "Voice",
			{"Bass", "Melody", "Inner"});
		// A QUARTER, WHICH IS THE JAZZ SOLOS' FIGURE: about one interval in twenty a repeated
		// note. Songs repeat far more, and that is a setting, not a default.
		configParam(P_REPEAT, 0.f, 1.f, 0.25f, "Repeated notes", "%", 0.f, 100.f);
		configParam(P_MOTIF, 0.f, 1.f, 0.5f, "Motif: a restated rhythm brings back its pitches", "%",
			0.f, 100.f);
		configParam(P_CONTOUR, 0.f, 1.f, 0.5f, "Contour: each line rises early and falls to its end",
			"%", 0.f, 100.f);

		configInput(I_RHYTHM, "Rhythm in — MPX");
		// THE WORD CV IS IN THE NAME ON PURPOSE. Clarity colours every port in the rack, and
		// where it has no entry for a module it falls back to reading the port's name — "MPX"
		// makes a port magenta, "CV" makes it orange, "CLOCK" makes it blue. Without the word,
		// these two were left uncoloured, which is the bare silver jack, while the MPX ports
		// came out right only because their names happen to say MPX.
		//
		// The name is doing two jobs, and it should: a port called "Random draw" that carries a
		// control voltage ought to say so in its tooltip for a reader as much as for a rule.
		configInput(I_RAND, "Random draw — CV");
		configInput(I_PROFILE, "Profile CV — moves the register");
		configOutput(O_PART, "Notes out — MPX");
	}

	~VoiceModule() {
		busRelease(slot);
	}

	int busSlotFor(int outputId, uint32_t* gen) override {
		if (outputId != O_PART)
			return -1;
		if (gen)
			*gen = generation;
		return slot;
	}

	bool isMPXInputId(int inputId) override {
		return inputId == I_RHYTHM;
	}

	/** THE ONE THING AN EXPANDER DOES FOR ITSELF: say whether it is attached. Everything else is
	read and driven by the base module.

	THE MODULE ON THE LEFT AND NOTHING ELSE. Either it is the melody module, or it is another
	voice expander that is itself attached — which is what lets two of them chain. */
	void process(const ProcessArgs& args) override {
		Module* left = leftExpander.module;
		bool linked = false;
		if (left) {
			if (left->model == modelMpxMelody)
				linked = true;
			else if (left->model == modelMpxMelodyVoice)
				linked = left->lights[L_LINKED].getBrightness() > 0.5f;
		}
		lights[L_LINKED].setBrightness(linked ? 1.f : 0.f);
	}
};


// ---- recording what the engine decided --------------------------------------------------------

/** ONE DECISION, WRITTEN DOWN WHOLE: what the engine was asked, what it chose, and the odds it
chose from.

WHY IT EXISTS. A knob that seems to do little by ear may be doing exactly what it should to the
odds, or nothing at all, and those two cannot be told apart by listening. This can: the candidate
shares are the weighting itself, so turning chord lock is visible as the chord tones' share of
the whole, whether or not anybody hears the difference.

PLAIN DATA, NO STRINGS. It is filled on the audio thread and must not allocate there, so the
names — the chord as a letter and a numeral, the notes as C4 — are worked out on the main thread
when the line is written. */
struct NoteRecord {
	/** A NOTE, or A SNAPSHOT OF THE SETTINGS. The settings are written when recording starts
	and again whenever any of them change, so the log says exactly when a knob moved rather than
	leaving it to be inferred from the notes around it — which is guesswork for any knob whose
	effect is subtle, and that is every knob anybody would want to check. */
	enum Kind : uint8_t { NOTE, SETTINGS };
	Kind kind = NOTE;
	float artic = 0.f, accent = 0.f, breath = 0.f, endAtChanges = 0.f, repeats = 0.f;
	float motif = 0.f, contour = 0.f;
	int seed = 0;
	double seconds = 0.0;
	int voice = 0;
	int role = 0;
	int bar = 0;
	float beatInBar = 0.f;
	double beat = 0.0;
	int phrase = 0;
	float toPhraseEnd = 0.f;
	float phraseBeats = 0.f;
	Key key;
	Chord current, next;
	float beatsToNext = 0.f;
	int regist = 0, span = 0, scale = 0;
	float smooth = 0.f, lock = 0.f, leading = 0.f, separation = 0.f;
	float draw = 0.f;
	bool drawFromJack = false;
	bool haveHarmony = false;
	int previous = -1, note = 0;
	int echo = 0, echoPitch = -1;
	float along = -1.f;
	bool strong = false;
	float level = 0.f, duration = 0.f;
	int takenCount = 0;
	int taken[MAX_VOICES];
	MelodyReport report;
};

/** A QUEUE FROM THE AUDIO THREAD TO THE PANEL, one writer and one reader.

NOTHING TOUCHES A FILE FROM THE AUDIO THREAD. Writing to a disk can take any amount of time, and
a sample that waits on a disk is a click. So the engine only copies a record into a fixed ring
and moves an index; the panel, on the main thread, empties the ring to the file at its own pace.

WHEN IT IS FULL, A RECORD IS DROPPED rather than waited for, and the drop is counted and written
into the log, so a gap in the record is visible rather than silent. */
struct RecordQueue {
	static const uint32_t SIZE = 256;
	NoteRecord ring[SIZE];
	std::atomic<uint32_t> head{0}, tail{0};
	std::atomic<uint32_t> dropped{0};

	void push(const NoteRecord& r) {
		const uint32_t h = head.load(std::memory_order_relaxed);
		const uint32_t next = (h + 1) % SIZE;
		if (next == tail.load(std::memory_order_relaxed)) {
			dropped++;
			return;
		}
		ring[h] = r;
		head.store(next, std::memory_order_relaxed);
	}

	bool pop(NoteRecord& out) {
		const uint32_t t = tail.load(std::memory_order_relaxed);
		if (t == head.load(std::memory_order_relaxed))
			return false;
		out = ring[t];
		tail.store((t + 1) % SIZE, std::memory_order_relaxed);
		return true;
	}
};


// ---- the base module -----------------------------------------------------------------------

struct MelodyModule : Module, NoteSink {
	// APPENDED, NEVER INSERTED. Rack saves a parameter by its number, so a new one in the middle
	// would move every one after it and load somebody's saved patch wrong.
	enum ParamId {
		/** HOW HARD THE VOICES KEEP OUT OF EACH OTHER'S WAY. At nought they draw independently
		and collide freely, which is several melodies rather than a texture; at full a voice is
		heavily penalised for taking a pitch class one drawn before it has just taken, and for a
		unison or an octave with it. */
		P_SEPARATION,
		/** THE SEED EVERY RANDOM CHOICE STARTS FROM. */
		P_VARIATION,
		/** WRITES EVERY DECISION TO DISK WHILE IT IS LIT. A button on the panel rather than a
		menu item, because a recording is started and stopped while listening, and the first
		take showed that a tick in a menu is a thing you have to go looking for to turn off. */
		P_RECORD,
		/** APPENDED, NEVER INSERTED. A line that comes round: CYCLE says after how many phrases
		the whole sequence of variations returns, and OWN SEED whether the chart's own seed still
		moves this one. Both mean exactly what they mean on mpxPhrase, so setting the two modules
		alike brings the rhythm and the pitches round together. */
		P_CYCLE,
		P_OWN_SEED,
		PARAMS_LEN,
	};
	enum InputId { I_CHART, INPUTS_LEN };
	enum OutputId { OUTPUTS_LEN };
	enum LightId { LIGHTS_LEN };

	MelodyModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(P_SEPARATION, 0.f, 1.f, 0.6f, "Voice separation", "%", 0.f, 100.f);

		// ADDED TO THE ONE ON THE CABLE, not used instead of it. mpxChart puts a master seed on
		// the cable so that everything downstream starts from the same number and a patch plays
		// again exactly; this is added to that, which keeps the patch reproducible and still
		// lets one chain be varied without disturbing anything else.
		//
		// WHOLE NUMBERS, so a setting can be returned to, and there is no order in them: seed 7
		// is not more varied than seed 3, only different. The figure is a name rather than an
		// amount, which is why it was wrong to call it VARIATION — that word promises a
		// quantity — and why the count was arbitrary either way.
		// NOUGHT TO 999, the same range mpxChart's own seed uses, so two numbers that mean the
		// same kind of thing are written the same way.
		configParam(P_VARIATION, 0.f, 999.f, 0.f, "Random generator seed");
		configSwitch(P_RECORD, 0.f, 1.f, 0.f, "Record notes to disk", {"Off", "Recording"});
		paramQuantities[P_VARIATION]->snapEnabled = true;

		// FOUR PHRASES BY DEFAULT, which is the length of the shortest thing anybody hears as a
		// unit: state, restate, depart, return. At one every phrase is the same line.
		configParam(P_CYCLE, 1.f, 16.f, 4.f, "Cycle", " phrases");
		paramQuantities[P_CYCLE]->snapEnabled = true;
		configSwitch(P_OWN_SEED, 0.f, 1.f, 0.f, "Seed",
			{"Added to the chart's seed", "Used alone"});

		configInput(I_CHART, "Chart in \u2014 MPX");
	}

	bool isMPXInputId(int inputId) override {
		return inputId == I_CHART;
	}

	/** What is patched into the chart input, and where it has read up to. */
	Upstream upstream;
	BusReader chart;

	/** WHETHER DECISIONS ARE BEING WRITTEN DOWN, and where they go on their way. Off unless
	somebody turns it on: it is a way of looking inside, not a thing the music needs. */
	std::atomic<bool> recording{false};
	RecordQueue records;
	/** Seconds since recording began, counted in samples so it is exact. */
	double recordClock = 0.0;

	/** ASKS FOR A FULL SNAPSHOT of every voice's settings, at the start of a take. Set by the
	panel when it opens a file, taken by the audio thread. */
	std::atomic<bool> snapshotWanted{false};

	/** HOW OFTEN THE SETTINGS ARE LOOKED AT, in seconds. Not every sample: a knob being turned
	changes every sample, and a line per sample would fill the queue in a moment and bury the
	notes. Twenty times a second still follows a sweep closely enough to see its shape. */
	static constexpr double SETTINGS_EVERY = 0.05;
	double settingsTimer = 0.0;
	float lastSeparation = -1.f, lastSeed = -1.f;

	/** WHETHER A VOICE IS ATTACHED, read by the panel so that it can say when one is not.

	Set in process rather than worked out while drawing, because the answer belongs to the
	module and the drawing happens on another thread. */
	bool hasVoice = false;

	/** THE VOICES BESIDE THIS MODULE, in the order they are to be resolved.

	GATHERED EVERY SAMPLE rather than cached, because a module can be dragged away between one
	sample and the next and a stale pointer is a crash. Walking three neighbours costs nothing.

	RESOLVED LOWEST FIRST, by register rather than by position in the rack or by the role
	switch. The foundation should not be pushed around by anything above it, and a register is
	a fact where a role is a label — two voices both set to melody at different registers have
	a sensible order this way and would have been a tie the other. */
	int gatherVoices(VoiceModule** out, int max) {
		int n = 0;
		Module* at = rightExpander.module;
		while (at && at->model == modelMpxMelodyVoice && n < max) {
			out[n++] = static_cast<VoiceModule*>(at);
			at = at->rightExpander.module;
		}
		// THE DRAWING ORDER IS THE ROLE: bass first, because every other line stands on it; then
		// the melody, because the inner voice fills round it; then the inner voice. Two voices of
		// one role go lowest first. It was the register alone, which put a low melody under a
		// high bass and ignored the switch that says which is which.
		auto rank = [](VoiceModule* v) {
			const int role = (int) std::lround(v->params[VoiceModule::P_ROLE].getValue());
			const int order = role == ROLE_BASS ? 0 : role == ROLE_MELODY ? 1 : 2;
			return (float) order * 1000.f + v->params[VoiceModule::P_PART + PP_REGISTER].getValue();
		};
		for (int i = 1; i < n; i++) {
			VoiceModule* lift = out[i];
			const float key = rank(lift);
			int j = i - 1;
			while (j >= 0 && rank(out[j]) > key) {
				out[j + 1] = out[j];
				j--;
			}
			out[j + 1] = lift;
		}
		return n;
	}

	/** WRITES A SETTINGS LINE FOR ONE VOICE if anything has changed since the last one, or
	unconditionally when `force` is set. The master's own two knobs belong to every voice, so a
	change to either is written into each voice's line. */
	void snapshot(VoiceModule* v, const Harmony& h, bool force) {
		const int base = VoiceModule::P_PART;
		const float sep = params[P_SEPARATION].getValue();
		const float seed = params[P_VARIATION].getValue();
		bool changed = force || sep != lastSeparation || seed != lastSeed;
		for (int i = 0; i < VoiceModule::PARAMS_LEN; i++) {
			const float now = v->params[i].getValue();
			if (!(now == v->lastSettings[i])) {
				changed = true;
				v->lastSettings[i] = now;
			}
		}
		if (!changed)
			return;
		NoteRecord r;
		r.kind = NoteRecord::SETTINGS;
		r.seconds = recordClock;
		r.voice = v->order;
		r.role = (int) std::lround(v->params[VoiceModule::P_ROLE].getValue());
		r.bar = h.bar;
		r.beatInBar = h.beatInBar;
		r.beat = h.beat;
		r.regist = (int) std::lround(v->params[base + PP_REGISTER].getValue());
		r.span = (int) std::lround(v->params[base + PP_SPAN].getValue());
		r.scale = (int) std::lround(v->params[base + PP_SCALE].getValue());
		r.smooth = v->params[base + PP_SMOOTH].getValue();
		r.lock = v->params[base + PP_LOCK].getValue();
		r.leading = v->params[base + PP_LEADING].getValue();
		r.artic = v->params[base + PP_ARTIC].getValue();
		r.accent = v->params[base + PP_ACCENT].getValue();
		r.breath = v->params[base + PP_BREATH].getValue();
		r.endAtChanges = v->params[base + PP_END].getValue();
		r.repeats = v->params[VoiceModule::P_REPEAT].getValue();
		r.motif = v->params[VoiceModule::P_MOTIF].getValue();
		r.contour = v->params[VoiceModule::P_CONTOUR].getValue();
		r.separation = sep;
		r.seed = (int) std::lround(seed);
		records.push(r);
	}

	/** THE WHOLE ENGINE RUNS HERE, in the master, for every voice.

	NOT IN THE VOICES THEMSELVES, and this is the reason the master exists. The voices are
	resolved in order and each must see what the ones before it have just taken — for separation
	now and for a following voice later — which cannot be done by three modules each minding
	its own business. It also means Rack may process the expanders in any order it likes, or on
	another thread, without any of it mattering: their own process does nothing that this reads.
	*/
	/** A RECORDING NEVER STARTS BY ITSELF. The button is a parameter, and Rack saves parameters
	with the patch — so without this, opening a patch saved mid-take would quietly start writing
	files again. Starting a recording is a decision somebody makes, every time. */
	void fromJson(json_t* rootJ) override {
		Module::fromJson(rootJ);
		params[P_RECORD].setValue(0.f);
	}

	/** Where the transport was last seen, so a rewind or a loop can be told from playing on. */
	double transportBeat = -1.0;
	uint32_t transportEpoch = 0;

	/** BEATS A SECOND, MEASURED FROM THE CHART'S BEAT, because the rhythm sends lengths in seconds
	and a breath or a chord change is measured in beats. Smoothed, and deaf to the jump of a
	rewind or of a clock pulse snapping the beat into step — the same rule as mpxPhrase. */
	float beatsPerSecond = 2.f;
	/** RECORD on the chart, as last seen, so only a change of it moves this module's switch. */
	bool chartRecordWas = false;
	/** STYLE on the chart, as last seen; the first sighting is only noted, as on mpxPhrase. */
	int chartStyleWas = -1;

	void process(const ProcessArgs& args) override {
		VoiceModule* voices[MAX_VOICES];
		const int n = gatherVoices(voices, MAX_VOICES);
		hasVoice = n > 0;

		upstream.settle(chart);
		Harmony h;
		const bool haveHarmony = chart.harmony(h) && h.valid;
		// RECORD ON THE CHART starts and stops this module's log too: a change of it is followed,
		// so this module's own switch still works between changes.
		if (haveHarmony && h.record != chartRecordWas) {
			params[P_RECORD].setValue(h.record ? 1.f : 0.f);
			chartRecordWas = h.record;
		}
		recording.store(params[P_RECORD].getValue() > 0.5f, std::memory_order_relaxed);
		// STYLE ON THE CHART: a change of it sets each MELODY voice's knobs to that style's line.
		// Only the melody: the styles' lines are melodies, and a bass given a melody's register
		// and span would stop being a bass.
		if (haveHarmony && (int) h.style != chartStyleWas) {
			float v[VOICE_STYLE_PARAMS];
			if (chartStyleWas >= 0 && voiceStyle(h.style, v)) {
				for (int i = 0; i < n; i++) {
					VoiceModule* vm = voices[i];
					if ((int) std::lround(vm->params[VoiceModule::P_ROLE].getValue()) != ROLE_MELODY)
						continue;
					for (int k = 0; k < PP_COUNT; k++)
						vm->params[VoiceModule::P_PART + k].setValue(v[k]);
					vm->params[VoiceModule::P_REPEAT].setValue(v[11]);
					vm->params[VoiceModule::P_MOTIF].setValue(v[12]);
					vm->params[VoiceModule::P_CONTOUR].setValue(v[13]);
				}
			}
			chartStyleWas = h.style;
		}

		// WHAT THE VOICES DRAWN SO FAR HAVE TAKEN, in this sample. Emptied each sample rather
		// than carried: two notes are doubling only if they sound together, and a note the bass
		// played a beat ago is not something the melody has to avoid.
		const bool rec = recording.load(std::memory_order_relaxed);
		if (rec)
			recordClock += args.sampleTime;

		int taken[MAX_VOICES];
		int takenCount = 0;
		for (int i = 0; i < n; i++)
			voices[i]->order = i;

		// A REWIND OR A LOOP STARTS THE LINES AGAIN, TOO.
		//
		// A voice's next note depends on its last one, so a line carried across a rewind begins
		// with an interval from the take before it — and from then on it is a different line,
		// however carefully the draws have been made to come round. The whole chain has to start
		// from the same place or none of it does.
		//
		// NOTICED FROM THE MUSIC ITSELF: the beat going backwards, or the pass counter changing.
		// No new signal on the cable, and nothing to keep in step.
		if (haveHarmony && transportBeat >= 0.0) {
			const double moved = h.beat - transportBeat;
			const float instant = (float) (moved / args.sampleTime);
			if (moved > 0.0 && instant < 50.f)
				beatsPerSecond += (instant - beatsPerSecond) * 0.0005f;
		}
		if (haveHarmony) {
			const bool wound = (transportBeat >= 0.0 && h.beat < transportBeat - 0.001)
				|| h.epoch != transportEpoch;
			if (wound) {
				for (int i = 0; i < n; i++) {
					voices[i]->previous = -1;
					voices[i]->beforePrevious = -1;
					// And nothing left sounding from the take before.
					endVoiceNote(voices[i]);
					endVoiceTail(voices[i]);
				}
			}
			transportBeat = h.beat;
			transportEpoch = h.epoch;
		}

		// THE SETTINGS, at the start of a take and whenever they move — BEFORE the notes of this
		// sample, so a line in the log is always preceded by the settings it was drawn with.
		if (rec) {
			const bool force = snapshotWanted.exchange(false);
			settingsTimer += args.sampleTime;
			if (force || settingsTimer >= SETTINGS_EVERY) {
				settingsTimer = 0.0;
				for (int i = 0; i < n; i++)
					snapshot(voices[i], h, force);
				lastSeparation = params[P_SEPARATION].getValue();
				lastSeed = params[P_VARIATION].getValue();
			}
		}

		for (int i = 0; i < n; i++) {
			VoiceModule* v = voices[i];
			v->upstream.settle(v->rhythm);
			drive(v, h, haveHarmony, args, taken, takenCount);
		}
	}

	/** ONE VOICE, FOR ONE SAMPLE: whatever arrived on its rhythm input, answered with a note.

	A RHYTHM SOURCE SAYS WHEN AND HOW LOUD AND FOR HOW LONG; this fills in the pitch and passes
	everything else along untouched. So an ON becomes an ON with a pitch on it, an OFF is
	forwarded as it stands, and a continuing value — a bend, a pressure — is forwarded too,
	because it belongs to a note this module did not invent and has no opinion about.

	THE HANDLE IS KEPT rather than minted. The note travelling on is the same note that arrived,
	and a processor that mints a new handle for a note it is merely passing through breaks every
	message that names it afterwards. */
	void drive(VoiceModule* v, const Harmony& h, bool haveHarmony, const ProcessArgs& args,
			int* taken, int& takenCount) {
		const int base = VoiceModule::P_PART;
		VoiceShapeSettings shape;
		shape.articulation = v->params[base + PP_ARTIC].getValue();
		shape.accent = v->params[base + PP_ACCENT].getValue();
		shape.breath = v->params[base + PP_BREATH].getValue();
		shape.endAtChanges = v->params[base + PP_END].getValue();
		const float bps = std::max(0.1f, beatsPerSecond);

		// THE PEDALS go through to the voice's own output as they arrive on its rhythm input:
		// the melody chooses pitches and has no opinion about the sustain pedal. State, so a copy.
		{
			float sustain, soft;
			v->rhythm.pedals(sustain, soft);
			busPublishPedals(v->slot, sustain, soft);
		}

		Event e;
		while (v->rhythm.next(e)) {
			if (e.kind == Event::ON) {
				e.pitch = pitchFor(v, h, haveHarmony, e, taken, takenCount,
					params[P_SEPARATION].getValue());
				const int note = (int) std::lround(e.pitch * 12.f) + 60;

				// HOW LONG AND HOW HARD, AND WHETHER AT ALL — see voiceShape. Worked in beats,
				// because a breath and a chord change are measured in beats; the rhythm sends
				// seconds, and the tempo the voice measures converts between them.
				if (haveHarmony) {
					const float draw = melodyDraw(h.seed, 0x5eedu, false, 1, 0, 1, h.phrase,
						std::max(0.f, h.phraseBeats - h.beatsToPhraseEnd) + 0.37f);
					const VoiceShape sh = voiceShape(h, shape, note, e.duration * bps, e.level,
						voiceStrongBeat(h), e.flags, draw);
					if (sh.drop)
						continue;
					e.duration = sh.beats / bps;
					e.level = sh.level;
					handOver(v, args.sampleRate);
					v->slur = sh.slur;
				}
				else {
					handOver(v, args.sampleRate);
					v->slur = false;
				}

				// WHAT THIS VOICE HAS JUST TAKEN, for the voices drawn after it. A pitch class
				// rather than a note: two lines an octave apart are doubling as surely as two
				// in unison, and it is the doubling that separation is about.
				if (takenCount < MAX_VOICES)
					taken[takenCount++] = note;
				v->isSounding = true;
				v->sounding = e.handle;
				v->ownLeft = std::max(1, (int) (e.duration * args.sampleRate));
				v->fade = 1.f;
				busPush(v->slot, e);
			}
			else if (e.kind == Event::OFF) {
				// THE RHYTHM'S OWN END is honoured only while the voice is not holding the note
				// longer than the rhythm meant. Held or slurred, the voice ends it; an end for a
				// note the voice did not play — one dropped into a breath, or one already handed
				// over — has nothing to end.
				if (v->isSounding && e.handle == v->sounding && shape.articulation <= 0.f)
					endVoiceNote(v);
			}
			else {
				busPush(v->slot, e);
			}
		}

		if (v->isSounding && --v->ownLeft <= 0)
			endVoiceNote(v);
		if (v->tailing && --v->tailLeft <= 0)
			endVoiceTail(v);

		// THE LAMP FADES rather than switching off with the note, because a sixteenth at speed
		// is two milliseconds of light and nobody sees it.
		v->fade = std::fmax(0.f, v->fade - args.sampleTime * 6.f);
		v->lights[VoiceModule::L_PART].setBrightness(v->fade);
	}

	/** A NEW NOTE ARRIVING WHILE THE LAST STILL SOUNDS: the last is handed over rather than cut,
	sounding under the new one for a moment, so a slurred or legato line stays joined through any
	downstream voice. Two notes sounding together for longer than that would be two lines, and a
	voice is one. */
	static constexpr float HAND_OVER = 0.02f;

	void handOver(VoiceModule* v, float sampleRate) {
		if (!v->isSounding)
			return;
		endVoiceTail(v);
		v->tailing = true;
		v->tailHandle = v->sounding;
		v->tailLeft = std::max(1, std::min(v->ownLeft, (int) (HAND_OVER * sampleRate)));
		v->isSounding = false;
	}

	void endVoiceNote(VoiceModule* v) {
		if (!v->isSounding)
			return;
		Event off;
		off.kind = Event::OFF;
		off.handle = v->sounding;
		busPush(v->slot, off);
		v->isSounding = false;
	}

	void endVoiceTail(VoiceModule* v) {
		if (!v->tailing)
			return;
		Event off;
		off.kind = Event::OFF;
		off.handle = v->tailHandle;
		busPush(v->slot, off);
		v->tailing = false;
	}

	/** WHAT NOTE TO PLAY: the palette, the weighting and the draw, for one voice.

	EVERYTHING THE DECISION NEEDS IS GATHERED HERE and handed to a pure function that knows
	nothing about Rack — see Melodic.hpp. The gathering is the part that belongs to a module:
	which knobs are set where, what is on the cable, which voltage is patched. The choosing is
	arithmetic over chords and intervals, and keeping the two apart is what lets the choosing be
	tested over thousands of chords with no rack running. */
	float pitchFor(VoiceModule* v, const Harmony& h, bool haveHarmony, const Event& e,
			const int* taken, int takenCount, float separation) {
		const float* p = NULL;
		(void) p;
		const int base = VoiceModule::P_PART;
		const int span = (int) std::lround(v->params[base + PP_SPAN].getValue());
		// THE PROFILE: a voltage moving the register while the line plays, five volts either way
		// being half the span. An arch over a phrase, or a slow climb through a chorus.
		int centre = (int) std::lround(v->params[base + PP_REGISTER].getValue());
		if (v->inputs[VoiceModule::I_PROFILE].isConnected()) {
			const float volts = math::clamp(v->inputs[VoiceModule::I_PROFILE].getVoltage(), -5.f, 5.f);
			centre = math::clamp(centre + (int) std::lround(volts / 5.f * (float) span / 2.f),
				24, 108);
		}

		// THE PROFILE, GENERATED FROM TWO KNOBS. The register is given as a low note and a
		// width, so the knob that names the centre is turned into the bottom of the window.
		// THE CHOICE ITSELF IS IN MelodyVoice.cpp, pure, so that a simulation predicting this
		// module calls the same code rather than a copy of it.
		if (!haveHarmony)
			return ((float) centre - 60.f) / 12.f;
		VoiceSettings vs;
		vs.smooth = v->params[base + PP_SMOOTH].getValue();
		vs.lock = v->params[base + PP_LOCK].getValue();
		vs.centre = centre;
		vs.span = span;
		vs.leading = v->params[base + PP_LEADING].getValue();
		vs.scale = (int) std::lround(v->params[base + PP_SCALE].getValue());
		vs.repeats = v->params[VoiceModule::P_REPEAT].getValue();
		vs.motif = v->params[VoiceModule::P_MOTIF].getValue();
		vs.contour = v->params[VoiceModule::P_CONTOUR].getValue();
		const VoiceLine line = v->memory.lineFor(e.echo, e.along);
		const float dice = drawFor(v, h, e);

		const bool rec = recording.load(std::memory_order_relaxed);
		MelodyReport report;

		const int previous = v->previous;
		// HOW LONG IT WILL SOUND, in beats, so an ending held into the next chord can be chosen
		// to fit both.
		const float heldBeats = e.duration * std::max(0.1f, beatsPerSecond);
		const int note = voiceNoteFor(h, vs, previous, dice, taken, takenCount, separation,
			rec ? &report : NULL, e.flags, v->beforePrevious, heldBeats, &line);
		v->memory.remember(note, e.along);
		v->beforePrevious = v->previous;
		v->previous = note;
		const bool strongBeat = [&]() {
			const float inBar = h.beatInBar;
			int whole = (int) std::lround(inBar);
			const float off = std::fabs(inBar - (float) whole);
			if (h.barBeats > 0 && whole >= h.barBeats)
				whole -= h.barBeats;
			return off < 0.25f && (whole % 2) == 0;
		}();

		if (rec) {
			NoteRecord r;
			r.seconds = recordClock;
			r.voice = v->order;
			r.role = (int) std::lround(v->params[VoiceModule::P_ROLE].getValue());
			r.bar = h.bar;
			r.beatInBar = h.beatInBar;
			r.beat = h.beat;
			r.phrase = h.phrase;
			r.toPhraseEnd = h.beatsToPhraseEnd;
			r.phraseBeats = h.phraseBeats;
			r.key = h.key;
			r.current = h.current;
			r.next = h.next;
			r.beatsToNext = h.beatsToNext;
			r.regist = centre;
			r.span = span;
			r.scale = (int) std::lround(v->params[base + PP_SCALE].getValue());
			r.smooth = v->params[base + PP_SMOOTH].getValue();
			r.lock = v->params[base + PP_LOCK].getValue();
			r.leading = v->params[base + PP_LEADING].getValue();
			r.separation = separation;
			r.draw = dice;
			r.drawFromJack = v->inputs[VoiceModule::I_RAND].isConnected();
			r.haveHarmony = true;
			r.previous = previous;
			r.note = note;
			r.echo = e.echo;
			r.echoPitch = line.echoPitch;
			r.along = e.along;
			r.strong = strongBeat;
			r.level = e.level;
			r.duration = e.duration;
			r.takenCount = std::min(takenCount, MAX_VOICES);
			for (int i = 0; i < r.takenCount; i++)
				r.taken[i] = taken[i];
			r.report = report;
			records.push(r);
		}
		// VOLTS, NOT A MIDI NUMBER. Rack's convention: nought volts is middle C, which is MIDI
		// 60, and a semitone is a twelfth of a volt.
		return ((float) note - 60.f) / 12.f;
	}

	/** THE NUMBER THAT CHOOSES, between nought and one.

	A CABLE IF THERE IS ONE. Nought to ten volts maps to the whole draw, which is the ordinary
	unipolar range in Rack, so an envelope, an LFO or a noise source all work without anything
	being scaled by hand.

	AND A DETERMINISTIC STAND-IN WHEN THERE IS NOT. A module that fell silent without a cable
	would make the common case the hard case. So the seed on the cable, the seed knob, the voice
	and the beat are mixed into one number — which means an unpatched line still recurs exactly
	when the chart comes round, and two voices sharing a chart do not move in parallel. */
	float drawFor(VoiceModule* v, const Harmony& h, const Event& e) {
		if (v->inputs[VoiceModule::I_RAND].isConnected())
			return math::clamp(v->inputs[VoiceModule::I_RAND].getVoltage() / 10.f, 0.f, 1.f);
		// THE POSITION IN THE CYCLE, NOT THE PASS COUNT, AND NOT THE NOTE'S NAME — see melodyDraw
		// in Melodic.hpp, where the reasoning and the arithmetic live so that recurrence can be
		// checked from a command line rather than listened for.
		const uint32_t own = (uint32_t) std::lround(params[P_VARIATION].getValue());
		const bool alone = params[P_OWN_SEED].getValue() > 0.5f || h.seed == 0;
		const int cycle = (int) std::lround(params[P_CYCLE].getValue());
		// A PICKUP IS DRAWN AS PART OF THE PHRASE IT LEADS INTO, counted back from that phrase's
		// bar line, so a phrase that comes round comes round with its pickup.
		if ((e.flags & Event::PICKUP) && h.upcoming.valid && h.phraseBeats > 0.f)
			return melodyDraw(h.seed, own, alone, cycle, h.upcoming.epoch, h.phrasesPerPass,
				h.upcoming.phrase, -h.beatsToPhraseEnd);
		const float intoPhrase = h.phraseBeats > 0.f
			? std::max(0.f, h.phraseBeats - h.beatsToPhraseEnd) : (float) h.beat;
		return melodyDraw(h.seed, own, alone, cycle, h.epoch, h.phrasesPerPass, h.phrase,
			intoPhrase);
	}
};


// ---- the panels ----------------------------------------------------------------------------
//
// ONE COLUMN IS ONE VOICE, COMPLETE. Seven knobs, three choice lists, four jacks and a lamp, and
// nothing about a line read anywhere else — because reaching for a knob during a take means
// reaching for the knob under the part you are listening to.

static const float NAME_HALF = 1.22f;
static const float KNOB_EDGE = 4.8f;
static const float PORT_EDGE = 4.01f;
static const float GAP = 2.f;

static void addLabel(Layout& L, const std::string& key, float x, float y,
		const std::string& text, Panel::Align align = Panel::CENTRE, bool heading = false,
		float size = 0.f, const std::string& owner = "") {
	Item i;
	i.key = key; i.kind = Item::LABEL; i.x = x; i.y = y; i.text = text;
	i.align = align; i.heading = heading; i.size = size; i.owner = owner;
	L.items.push_back(i);
}

static void addKnob(Layout& L, const std::string& key, float x, float y, int id,
		const std::string& name, int ticks = 2, const std::vector<std::string>& marks = {}) {
	Item i;
	i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "knob";
	i.ticks = ticks; i.tickMarks = marks;
	L.items.push_back(i);
	const float edge = marks.empty() ? KNOB_EDGE : 7.1f;
	addLabel(L, key + ".label", x, y + edge + GAP + NAME_HALF, name, Panel::CENTRE, true,
		0.f, key);
}

static void addJack(Layout& L, const std::string& key, Item::Kind kind, float x, float y, int id,
		const std::string& name, NVGcolor color, float size = 0.f) {
	Item i;
	i.key = key; i.kind = kind; i.id = id; i.x = x; i.y = y; i.ring = color;
	L.items.push_back(i);
	// ABOVE THE JACK, not below it. A label under a jack at the foot of the panel falls off the
	// bottom edge, and the row has to sit low because a cable needs the room.
	addLabel(L, key + ".label", x, y - PORT_EDGE - GAP - 0.98f, name, Panel::CENTRE,
		size > 0.f, size, key);
}

static void addRadio(Layout& L, const std::string& key, float x, float y, int id,
		const std::string& group, const std::vector<std::string>& names, float pitch) {
	addLabel(L, key + ".group", x, y - GAP - NAME_HALF, group, Panel::LEFT, true, 0.f, key);
	Item i;
	i.key = key; i.kind = Item::PARAM; i.id = id; i.x = x; i.y = y; i.style = "lamps";
	i.names = names; i.horizontal = false; i.pitch = pitch;
	i.labelSide = Panel::RIGHT;
	L.items.push_back(i);
}

static void addLamp(Layout& L, const std::string& key, float x, float y, int id) {
	Item i;
	i.key = key; i.kind = Item::LIGHT; i.id = id; i.x = x; i.y = y;
	L.items.push_back(i);
}


/** A DIVIDING LINE, placed by the end it starts from. An item like any other, so the panel
editor can move it, stretch it by its far end, turn it, or take it off — see Layout.hpp. */
static void addRule(Layout& L, const std::string& key, float x, float y, float len,
		bool horizontal) {
	Item i;
	i.key = key; i.kind = Item::RULE; i.x = x; i.y = y; i.horizontal = horizontal;
	if (horizontal) i.w = len; else i.h = len;
	L.items.push_back(i);
}

/** THE VOICE'S CONTROLS, at the positions arrived at in the panel editor and folded back in
here so that they are what the module ships with.

THE ROWS ARE NOT REGULAR, and that is the point. A layout laid out by arithmetic sits on a grid
whether or not the grid means anything; this one was moved by hand until it read properly.

THE THREE BANDS ARE STILL THERE, as rows rather than as blocks: the three plates across the top
are what may be played, the row under them is which note is played, and the two rows under the
rule are how it is played. */
static void addVoiceControls(Layout& L, int param0,
		int inRhythm, int inRand, int inProfile, int out, int lamp) {
	// ---- 1. what may be played ---------------------------------------------------------------
	//
	// THREE PLATES RATHER THAN KNOBS, because each is a value you choose rather than a quantity
	// you sweep. The wheel steps one, and clicking it offers the whole range as a list. Their
	// captions sit above them, which is what makes the three read as one kind of control.
	auto plate = [&](const std::string& key, float x, float y, int id, int chars,
			const std::string& name) {
		Item i;
		// h IS THE FIGURE HEIGHT IN MILLIMETRES, AND IT MUST BE SET. Item's h defaults to 30,
		// which is sensible for the bracket it was written for and disastrous here: the plate
		// takes it as the size of its figures, so the word KEY was once drawn three centimetres
		// tall across the foot of the panel.
		i.key = key; i.kind = Item::PARAM; i.id = id;
		i.x = x; i.y = y; i.style = "readout"; i.chars = chars; i.h = 3.4f;
		L.items.push_back(i);
		addLabel(L, key + ".label", x, y - 5.22f, name, Panel::CENTRE, true, 0.f, key);
	};
	plate("v.scale", 19.f, 17.f, param0 + PP_SCALE, 10, "SCALE");
	plate("v.reg", 40.f, 17.f, param0 + PP_REGISTER, 3, "REGISTER");
	plate("v.span", 58.f, 17.f, param0 + PP_SPAN, 2, "SPAN");

	// ---- 2. which note is played -------------------------------------------------------------
	//
	// The weighting of the draw, consulted every time a note arrives, with the voltage that
	// samples it at the head of the row.
	addJack(L, "v.rand", Item::PORT_IN, 7.5f, 34.5f, inRand, "RANDOM\nDRAW", SIG_CV, 5.4f);
	addKnob(L, "v.lead", 20.5f, 34.f, param0 + PP_LEADING, "VOICE\nLEADING", 3, {"0", "", "3"});
	addKnob(L, "v.smooth", 36.5f, 34.5f, param0 + PP_SMOOTH, "SMOOTHNESS", 3);
	addKnob(L, "v.lock", 54.f, 34.5f, param0 + PP_LOCK, "CHORD\nLOCK", 3);

	// ---- 3. how it is played -----------------------------------------------------------------
	//
	// The note is already chosen; these four change what happens to it. The centre mark on the
	// bipolar pair is where the rhythm's own value is, and there is no way to find that point by
	// ear while the music is running, so it is printed.
	addKnob(L, "v.artic", 14.f, 60.5f, param0 + PP_ARTIC, "ARTICULATION", 3,
		{"SHORT", "AS SENT", "LEGATO"});

	// NOUGHT AND ONE, not words. Nought is not strictly never — a note outside the new scale is
	// ended there whatever the knob says — but two figures are read at a glance where two words
	// have to be read, and they take a third of the room.
	addKnob(L, "v.end", 32.5f, 60.f, param0 + PP_END, "END NOTES\nAT CHANGES", 3, {"0", "", "1"});
	addKnob(L, "v.breath", 54.f, 60.f, param0 + PP_BREATH, "BREATH", 3, {"0", "1", "2"});
	addKnob(L, "v.accent", 53.5f, 83.5f, param0 + PP_ACCENT, "ACCENT", 3,
		{"EVEN", "AS SENT", "STRONG"});

	// ---- what comes in and what goes out -----------------------------------------------------
	//
	// NAMED FOR WHAT THEY CARRY — the colour already says which are MPX cables, so spending a
	// label on saying so again wastes the one place that could tell you what to patch there.
	addJack(L, "v.rhythm", Item::PORT_IN, 19.f, 122.f, inRhythm, "RHYTHM\nIN", NOTE_CABLE, 5.4f);
	addJack(L, "v.profile", Item::PORT_IN, 41.f, 122.f, inProfile, "PROFILE", SIG_CV, 5.4f);
	addJack(L, "v.out", Item::PORT_OUT, 57.5f, 122.f, out, "NOTES\nOUT", NOTE_CABLE, 5.4f);

	// THE LAMP SITS WITH THE JACK IT REPORTS ON: it flashes when this voice sounds a note, and
	// what leaves by that jack is that note.
	addLamp(L, "v.lamp", 60.5f, 114.f, lamp);

	// The lines, where they were put.
	addRule(L, "rule.strip.b", 10.5f, 21.5f, 48.04f, true);
	addRule(L, "rule.band1", 8.f, 49.f, 49.9f, true);
	addRule(L, "rule.strip.a", 10.5f, 74.5f, 48.04f, true);
	addRule(L, "rule.band2", 6.f, 126.f, 48.04f, true);
	addRule(L, "v.jackrule", 63.f, 47.f, 99.f, false);
}

/** THE MASTER, AS NARROW AS THE NAME ALLOWS. Four HP: three holds every control, and the fourth
is what the word "melody" needs across the top.

IT HOLDS NO VOICE. Everything here belongs to the whole chain rather than to a line — how hard
the voices avoid each other, the number their random choices start from, and the chart they are
all played against. A voice module beside it is what makes a sound.

THAT IS WHY IT EXISTS. With the shared controls on a module that also had a voice, that voice
was fixed as the melody and had no role switch, so a patch wanting one bass line had to place a
melody it did not want and ignore it. Every voice is an expander now, and every voice can be any
of the three. */
static Layout melodyLayout() {
	Layout L;
	// FOUR HP, WHICH IS WHAT THE NAME COSTS. Three fits every control on it — a knob is 9.6 mm
	// and the panel is 15.2 — but not the word: "melody" at the size every title in this plugin
	// is set needs about 50 pixels and 3 HP gives 45. One more HP buys 15 of them.
	//
	// WORTH IT. A module with no name on it is a module you have to click to identify, and this
	// one is deliberately sparse enough that there is nothing else to recognise it by.
	L.hp = 4.f;
	L.title = "melody";

	// CENTRED, all three, on the one line down the middle of the panel — which at this width is
	// the only place anything can go.
	const float mid = 4.f * 5.08f / 2.f;

	// VOICE SEPARATION, IN TWO LINES AND A SMALLER FACE. The word on its own says distance in
	// pitch, which is not what the knob sets — two voices a third apart are perfectly separate.
	// What it sets is how much the voices avoid each other, and naming the voices says so.
	//
	// BROKEN WITH A HYPHEN RATHER THAN SET SMALLER. Four HP is 61 pixels across and SEPARATION
	// at the panel's own ten fills about 55 of them, which leaves a word with a module round it
	// — and every size small enough to fit was a size smaller than every other caption in the
	// plugin. A hyphen is what typesetting has always done with a word too long for its column,
	// and it costs a line rather than a point size.
	// RECORD AT THE TOP, above everything that shapes the music, because it is the one thing
	// on this panel that is operated during a take rather than set before one — and a place a
	// hand goes without looking is the place a thing that must be turned off again belongs.
	{
		Item i;
		i.key = "g.rec"; i.kind = Item::PARAM; i.id = MelodyModule::P_RECORD;
		i.x = mid; i.y = 22.f; i.style = "latch"; i.diameter = 6.6f;
		L.items.push_back(i);
		addLabel(L, "g.rec.label", mid, 15.5f, "RECORD", Panel::CENTRE, true, 0.f, "g.rec");
	}

	addKnob(L, "g.sep", mid, 34.f, MelodyModule::P_SEPARATION, "VOICE\nSEPAR-\nATION", 3);
	// LOWER THAN A ONE-LINE CAPTION WOULD SIT. A name is set about the point it is given and
	// grows in both directions as lines are added, so a third line would otherwise reach up
	// into the metal of the knob it names.
	L.items.back().y = 45.5f;
	// A PLATE RATHER THAN A KNOB, and exactly as wide as three figures. A seed is a name, not a
	// quantity — there is nothing between 41 and 42 — so a knob to sweep it is the wrong shape
	// of control, and the plate shows the number instead of making you hover for it.
	//
	// THE WHEEL STEPS IT. Clicking offers the whole range as a list where the range is short
	// enough to be a list; a thousand entries is not, so this one is turned by the wheel.
	{
		Item i;
		i.key = "g.var"; i.kind = Item::PARAM; i.id = MelodyModule::P_VARIATION;
		i.x = mid; i.y = 62.f; i.style = "readout"; i.chars = 3; i.h = 3.4f;
		L.items.push_back(i);
		// THREE LINES, SAYING WHAT THE NUMBER IS THE SEED OF. "Random seed" leaves a reader to
		// guess what is being seeded; the generator is the thing every random choice in the
		// chain comes out of, and naming it is what makes the word "seed" mean something.
		// Higher up the panel than a one-line caption would sit, because a name set about the
		// point it is given grows in both directions as lines are added.
		addLabel(L, "g.var.label", mid, 55.f, "RANDOM\nGENERATOR\nSEED", Panel::CENTRE, true,
			0.f, "g.var");
	}

	// HOW OFTEN THE LINE COMES ROUND, and whether the chart's seed still moves it. Under the
	// seed because that is what they qualify: the seed says which line, the cycle says how often
	// it returns, and LOCK SEED whether the chart has a hand in it.
	//
	// PLACED AROUND THE ARRANGEMENT ALREADY MADE in the panel editor, which put the seed plate at
	// seventy and a half: the captions here are measured to clear it by a millimetre, and to
	// clear the chart jack's name below by more than that.
	{
		Item i;
		i.key = "g.cycle"; i.kind = Item::PARAM; i.id = MelodyModule::P_CYCLE;
		i.x = mid; i.y = 82.5f; i.style = "readout"; i.chars = 2; i.h = 3.4f;
		L.items.push_back(i);
		addLabel(L, "g.cycle.label", mid, 76.5f, "PHRASES\nRECYCLE", Panel::CENTRE, true, 0.f,
			"g.cycle");
	}
	{
		Item i;
		i.key = "g.own"; i.kind = Item::PARAM; i.id = MelodyModule::P_OWN_SEED;
		i.x = mid; i.y = 90.5f; i.style = "latch"; i.diameter = 6.6f;
		L.items.push_back(i);
		addLabel(L, "g.own.label", mid, 97.5f, "LOCK\nSEED", Panel::CENTRE, true, 0.f, "g.own");
	}

	// AT THE FOOT, below the knobs, where a cable leaves the panel without crossing anything.
	addJack(L, "in.chart", Item::PORT_IN, mid, 112.f, MelodyModule::I_CHART, "CHART\nIN",
		NOTE_CABLE, 5.4f);

	L.bindOffsets();
	return L;
}

/** The voice. Thirteen HP, the arrangement folded back in from the editor, and the two things an
expander has that a voice on its own would not: which line it is, and whether anything is driving
it. Both sit where the shared knobs used to, which is the room they left. */
static Layout voiceLayout() {
	Layout L;
	L.hp = 13.f;
	L.title = "mpxVoice";

	addVoiceControls(L, VoiceModule::P_PART,
		VoiceModule::I_RHYTHM, VoiceModule::I_RAND, VoiceModule::I_PROFILE,
		VoiceModule::O_PART, VoiceModule::L_PART);

	// WHICH LINE THIS IS, and so where it falls in the drawing order — not where it sits in the
	// rack. Adding two expanders in either order gives the same music.
	// THIRTEEN MILLIMETRES APART, BECAUSE THE NAMES RUN ACROSS. A lamp's name is drawn to its
	// right, so in a row the pitch has to clear the whole name and not merely the lamp: the
	// lamp is 6.5 pixels, the gap 5, and MELODY about 26, which is 12.8 mm in all. At the 4.6
	// a column wants, every name was written over the next lamp along.
	addRadio(L, "g.role", 8.f, 96.f, VoiceModule::P_ROLE, "VOICE",
		{"BASS", "MELODY", "INNER"}, 13.f);
	L.items.back().horizontal = true;

	// AN ORPHAN LOOKS EXACTLY LIKE A WORKING ONE — same panel, same jacks, and a cable out of it
	// that carries nothing. Lit while something is driving it.
	// REPEATED NOTES: part of which note is played, but that row is full, so it takes the free
	// place beside ACCENT.
	addKnob(L, "v.repeat", 13.5f, 99.5f, VoiceModule::P_REPEAT, "REPEATED\nNOTES", 3,
		{"0", "", "1"});
	// MOTIF AND CONTOUR, between REPEATED NOTES and ACCENT: the room there is for two knobs
	// without printed marks, whose names are one word each.
	addKnob(L, "v.motif", 27.5f, 99.5f, VoiceModule::P_MOTIF, "MOTIF", 3);
	addKnob(L, "v.contour", 39.f, 99.5f, VoiceModule::P_CONTOUR, "CONTOUR", 3);

	addLamp(L, "g.linked", 45.f, 100.f, VoiceModule::L_LINKED);
	addLabel(L, "g.linked.label", 45.f, 105.f, "LINKED", Panel::CENTRE, true, 0.f, "g.linked");

	L.bindOffsets();
	return L;
}


/** WHICH MPX CABLES ARE PATCHED INTO ONE INPUT, resolved on the main thread and handed over.

The engine knows nothing of cables — a bus is found by its slot — so somebody has to walk the
rack and translate. The widget is walked once a frame anyway, which makes it the cheapest place
and the only one that can also colour the cable. */
static void linkInput(ModuleWidget* mw, int inputId, Upstream& upstream) {
	int slots[MAX_UPSTREAM];
	uint32_t generations[MAX_UPSTREAM];
	int n = 0;
	if (PortWidget* port = mw->getInput(inputId)) {
		for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(port)) {
			if (n >= MAX_UPSTREAM)
				break;
			engine::Cable* cable = cw->getCable();
			if (!cable)
				continue;
			uint32_t g = 0;
			const int slot = noteBusOf(cable->outputModule, cable->outputId, &g);
			if (slot < 0)
				continue;
			// PINK ONCE IT HAS TAKEN, and only then: a cable from an MPX jack to something that
			// is not one keeps its ordinary colour and carries nothing, which is the whole
			// point of colouring it here rather than at the jack.
			cw->color = NOTE_CABLE;
			slots[n] = slot;
			generations[n] = g;
			n++;
		}
	}
	upstream.want(slots, generations, n);
}

/** The other end: an MPX output's cable is coloured when it lands somewhere that listens. */
static void colourOutput(ModuleWidget* mw, int outputId) {
	if (PortWidget* out = mw->getOutput(outputId)) {
		for (CableWidget* cw : APP->scene->rack->getCompleteCablesOnPort(out)) {
			engine::Cable* cable = cw->getCable();
			if (cable && isMPXInput(cable->inputModule, cable->inputId))
				cw->color = NOTE_CABLE;
		}
	}
}

/** Both panels are built the same way; only the layout and the slug differ. */
struct MelodyPanelWidget : ModuleWidget {
	Panel* panel = NULL;
	Layout layout;
	std::string slug;

	void setUp(Module* module, Layout made, const std::string& itsSlug) {
		setModule(module);
		slug = itsSlug;
		layout = made;
		layoutApplyUser(slug, layout);
		panel = new Panel;
		addChild(panel);
		layoutBuild(this, panel, layout);

	}

	void appendContextMenu(ui::Menu* menu) override {
		layoutAppendMenu(menu, this, panel, &layout, slug);
	}
};

/** A MIDI note as a musician writes it: C4 is middle C, Rack's own convention. */
static std::string noteName(int note) {
	static const char* NAMES[12] = {"C", "C#", "D", "D#", "E", "F",
		"F#", "G", "G#", "A", "A#", "B"};
	const int pc = ((note % 12) + 12) % 12;
	return std::string(NAMES[pc]) + std::to_string((int) std::floor(note / 12.0) - 1);
}

/** Text safe inside a JSON string. Chord names are plain, but a log one odd chart makes
unreadable is worse than no log. */
static std::string jsonText(const std::string& in) {
	std::string out;
	for (char c : in) {
		if (c == '"' || c == '\\') { out += '\\'; out += c; }
		else if ((unsigned char) c < 0x20) out += ' ';
		else out += c;
	}
	return out;
}

static const char* scaleName(int which) {
	switch (which) {
		case SCALE_MAJOR_PENT: return "major pentatonic";
		case SCALE_MINOR_PENT: return "minor pentatonic";
		case SCALE_BLUES: return "blues";
		case SCALE_FROM_CHORD: return "from chord";
		default: return "key";
	}
}

/** ONE LINE OF THE LOG, as JSON, so it can be read by a program and by eye.

GROUPED THE WAY THE QUESTION IS ASKED: when it was, what the harmony was, what the voice was set
to, what was drawn, what came out — and then the odds, likeliest first, because that is the order
anybody reads them in. */
static std::string recordLine(const NoteRecord& r) {
	static const char* ROLES[3] = {"bass", "melody", "inner"};
	if (r.kind == NoteRecord::SETTINGS) {
		std::string o = "{\"settings\":true";
		o += string::f(",\"t\":%.3f,\"voice\":%d,\"role\":\"%s\"", r.seconds, r.voice,
			ROLES[std::max(0, std::min(2, r.role))]);
		o += string::f(",\"bar\":%d,\"beatInBar\":%.2f", r.bar + 1, r.beatInBar + 1.f);
		o += ",\"register\":\"" + noteName(r.regist) + "\"";
		o += string::f(",\"span\":%d,\"scale\":\"%s\"", r.span, scaleName(r.scale));
		o += string::f(",\"smoothness\":%.3f,\"chordLock\":%.3f,\"voiceLeading\":%.3f",
			r.smooth, r.lock, r.leading);
		o += string::f(",\"articulation\":%.3f,\"accent\":%.3f,\"breath\":%.3f"
			",\"endAtChanges\":%.3f,\"repeatedNotes\":%.3f", r.artic, r.accent, r.breath, r.endAtChanges,
			r.repeats);
		o += string::f(",\"motif\":%.3f,\"contour\":%.3f", r.motif, r.contour);
		o += string::f(",\"separation\":%.3f,\"seed\":%d}", r.separation, r.seed);
		return o;
	}
	int pcs[MAX_CHORD_TONES];
	const int chordCount = r.current.valid ? chordPitchClasses(r.current, r.key, pcs) : 0;
	auto isChordTone = [&](int note) {
		const int pc = ((note % 12) + 12) % 12;
		for (int i = 0; i < chordCount; i++)
			if ((((pcs[i] % 12) + 12) % 12) == pc)
				return true;
		return false;
	};

	// THE CHORD TONES' SHARE OF THE WHOLE, which is the one number that answers "is chord lock
	// doing anything". Summed here rather than left for a reader to add up.
	float chordShare = 0.f;
	for (int i = 0; i < r.report.count; i++)
		if (isChordTone(r.report.notes[i]))
			chordShare += r.report.share[i];

	std::string o = "{";
	o += string::f("\"t\":%.3f,\"voice\":%d,\"role\":\"%s\"", r.seconds, r.voice,
		ROLES[std::max(0, std::min(2, r.role))]);
	o += string::f(",\"bar\":%d,\"beatInBar\":%.2f,\"beat\":%.2f", r.bar + 1,
		r.beatInBar + 1.f, r.beat);
	o += string::f(",\"phrase\":%d,\"phraseLeft\":%.2f,\"phraseBeats\":%.1f",
		r.phrase + 1, r.toPhraseEnd, r.phraseBeats);
	o += ",\"chord\":\"" + jsonText(r.current.valid ? chordLetter(r.current, r.key) : "") + "\"";
	o += ",\"numeral\":\"" + jsonText(r.current.valid ? chordRoman(r.current) : "") + "\"";
	o += ",\"next\":\"" + jsonText(r.next.valid ? chordLetter(r.next, r.key) : "") + "\"";
	o += string::f(",\"beatsToNext\":%.2f", r.beatsToNext);
	o += ",\"register\":\"" + noteName(r.regist) + "\"";
	o += string::f(",\"span\":%d,\"scale\":\"%s\"", r.span, scaleName(r.scale));
	o += string::f(",\"smoothness\":%.2f,\"chordLock\":%.2f,\"voiceLeading\":%.2f",
		r.smooth, r.lock, r.leading);
	o += string::f(",\"separation\":%.2f", r.separation);
	o += string::f(",\"draw\":%.3f,\"drawFrom\":\"%s\"", r.draw, r.drawFromJack ? "jack" : "seed");
	o += ",\"previous\":" + (r.previous >= 0 ? "\"" + noteName(r.previous) + "\"" : std::string("null"));
	o += ",\"note\":\"" + noteName(r.note) + "\"";
	o += ",\"interval\":" + (r.previous >= 0 ? std::to_string(r.note - r.previous) : std::string("null"));
	if (r.echo > 0)
		o += string::f(",\"echoesBack\":%d,\"echoOf\":\"%s\"", r.echo,
			r.echoPitch >= 0 ? noteName(r.echoPitch).c_str() : "");
	if (r.along >= 0.f)
		o += string::f(",\"along\":%.2f", r.along);
	o += string::f(",\"chordTone\":%s,\"strongBeat\":%s",
		isChordTone(r.note) ? "true" : "false", r.strong ? "true" : "false");
	o += string::f(",\"level\":%.2f,\"duration\":%.3f", r.level, r.duration);
	o += string::f(",\"chordToneShare\":%.3f,\"fellBack\":%s", chordShare,
		r.report.fellBack ? "true" : "false");

	o += ",\"taken\":[";
	for (int i = 0; i < r.takenCount; i++)
		o += (i ? ",\"" : "\"") + noteName(r.taken[i]) + "\"";
	o += "]";

	std::vector<int> order;
	for (int i = 0; i < r.report.count; i++)
		order.push_back(i);
	std::sort(order.begin(), order.end(), [&](int a, int b) {
		return r.report.share[a] > r.report.share[b];
	});
	o += ",\"candidates\":[";
	for (size_t i = 0; i < order.size(); i++) {
		const int k = order[i];
		o += string::f("%s[\"%s\",%.3f]", i ? "," : "",
			noteName(r.report.notes[k]).c_str(), r.report.share[k]);
	}
	o += "]}";
	return o;
}

struct MelodyWidget : MelodyPanelWidget {
	MelodyWidget(MelodyModule* module) { setUp(module, melodyLayout(), "mpxMelody"); }

	/** SAYS WHAT IS MISSING, AND ONLY WHILE IT IS MISSING.

	This module makes no sound by itself and has no output jack, so a patcher who places it
	alone has nothing to go on: no silence to diagnose, no cable that fails to take. The panel
	says so instead.

	IT GOES AWAY WHEN A VOICE ARRIVES rather than greying out. A permanent line saying what the
	module needs becomes furniture — read once, then part of the panel for ever. A line that is
	only there while something is wrong is read every time it appears, because its appearing is
	the information.

	NOT IN THE LAYOUT. A layout label is drawn whatever the patch looks like, and the editor
	would offer this one to be moved and renamed like any other — a caption for a state rather
	than for a control. Drawn here, it cannot be dragged away from the thing it is explaining. */
	/** THE OPEN LOG, while recording is on. A new file each time recording starts, named by
	the moment it started, so a take is never appended to the one before and turning recording
	off and on again cannot lose anything. */
	FILE* log = NULL;
	std::string logPath;
	uint32_t droppedSeen = 0;

	~MelodyWidget() {
		if (log)
			std::fclose(log);
	}

	void step() override {
		MelodyPanelWidget::step();
		MelodyModule* m = dynamic_cast<MelodyModule*>(module);
		if (!m)
			return;
		linkInput(this, MelodyModule::I_CHART, m->upstream);

		const bool on = m->recording.load();
		if (on && !log)
			openLog(m);
		else if (!on && log) {
			std::fclose(log);
			log = NULL;
		}
		if (!log)
			return;

		NoteRecord r;
		while (m->records.pop(r)) {
			const std::string line = recordLine(r);
			std::fwrite(line.data(), 1, line.size(), log);
			std::fputc('\n', log);
		}
		const uint32_t dropped = m->records.dropped.load();
		if (dropped != droppedSeen) {
			std::fprintf(log, "{\"dropped\":%u}\n", (unsigned) (dropped - droppedSeen));
			droppedSeen = dropped;
		}
		// FLUSHED EVERY FRAME, so the file on disk is always what has been decided so far. The
		// point of the log is to be read while the patch is still running.
		std::fflush(log);
	}

	void openLog(MelodyModule* m) {
		const std::string folder = asset::user("DreamerMPX");
		system::createDirectories(folder);
		char stamp[32];
		std::time_t now = std::time(NULL);
		std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
		logPath = folder + "/melody-log-" + stamp + ".jsonl";
		log = std::fopen(logPath.c_str(), "w");
		// Whatever was queued before the file existed belongs to no take.
		NoteRecord discard;
		while (m->records.pop(discard)) {}
		m->recordClock = 0.0;
		droppedSeen = m->records.dropped.load();
		m->snapshotWanted.store(true);
	}

	void appendContextMenu(ui::Menu* menu) override {
		MelodyPanelWidget::appendContextMenu(menu);
		MelodyModule* m = dynamic_cast<MelodyModule*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createCheckMenuItem("Record notes to disk", "",
			[=]() { return m->params[MelodyModule::P_RECORD].getValue() > 0.5f; },
			[=]() {
				Param& p = m->params[MelodyModule::P_RECORD];
				p.setValue(p.getValue() > 0.5f ? 0.f : 1.f);
			}));
		// WHERE IT WENT, so the file can be found without anybody having to know the folder.
		menu->addChild(createMenuLabel(logPath.empty()
			? "writes to " + asset::user("DreamerMPX") : logPath));
	}

	void draw(const DrawArgs& args) override {
		ModuleWidget::draw(args);
		MelodyModule* m = dynamic_cast<MelodyModule*>(module);
		// THE BROWSER PREVIEW HAS NO MODULE, and a preview that shouted about a missing
		// expander would be telling the truth about nothing.
		if (!m || m->hasVoice)
			return;
		std::shared_ptr<window::Font> font =
			APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		// A HANDLE OF NOUGHT IS A VALID FONT. nanovg numbers them from nought and returns -1
		// for failure, so testing the handle for truth throws away the first font loaded.
		if (!font || font->handle < 0)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, 7.f);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		// Amber rather than red: nothing is broken, something is not finished.
		nvgFillColor(args.vg, nvgRGB(0xe8, 0xa8, 0x3c));
		const float mid = box.size.x / 2.f;
		const char* said[] = {"ADD VOICE", "MODULES", "TO THE", "RIGHT"};
		for (int i = 0; i < 4; i++)
			crispText(args.vg, mid, mm2px(80.f + i * 4.6f), said[i], NULL);
	}
};

struct VoiceWidget : MelodyPanelWidget {
	VoiceWidget(VoiceModule* module) { setUp(module, voiceLayout(), "mpxMelodyVoice"); }

	void step() override {
		MelodyPanelWidget::step();
		if (VoiceModule* v = dynamic_cast<VoiceModule*>(module)) {
			linkInput(this, VoiceModule::I_RHYTHM, v->upstream);
			colourOutput(this, VoiceModule::O_PART);
		}
	}
};


Model* modelMpxMelody = createModel<MelodyModule, MelodyWidget>("mpxMelody");
Model* modelMpxMelodyVoice = createModel<VoiceModule, VoiceWidget>("mpxMelodyVoice");


/* STILL TO DECIDE, AND WHERE IT HAS TO HAPPEN.

THE PHRASE IS NOT ON THE CABLE. Breathing at a phrase end, and slurring within one, both need to
know where the phrase is, and the harmony block carries three chords of lookahead — enough to
lead into a change, nowhere near enough to know it is two bars from the end of an eight-bar
phrase. Only mpxChart can work that out, because only mpxChart has the whole progression
unrolled. It wants two fields on the harmony: beats to the end of the current phrase, and a
phrase counter so one phrase can be told from the next.

FROM CHORD IS A PLACEHOLDER. The fifth scale setting exists on the panel and in the saved patch
and behaves as KEY. Writing it means deriving the palette from the chord's quality rather than
from the key — which is chord-scale theory, and is the one place in this module where a table of
musical convention is unavoidable.

A SUSPENSION MUST RESOLVE, AND THAT IS STATE THE NEXT NOTE HAS TO SEE.

Everything else in this module decides a note and forgets it: the only thing carried from one
note to the next is the previous pitch, which the weighting reads to work out how far it may
leap. The hold-over knob breaks that, and it is the only thing that does.

When a note is held over a chord change and is NOT a tone of the new chord, it is a suspension,
and a suspension that does not resolve is just a wrong note held too long. Resolving it means the
NEXT note in that voice steps DOWN, by a semitone or a tone, onto a tone of the new chord.

So the voice carries a pending resolution: which pitch is suspended, and what it must resolve to.
The next draw for that voice is not a draw at all — the note is already determined — and the
weighting is not consulted. That is the point. Leaving it to the weighting, by boosting the
resolution tones and hoping, will resolve most of the time and will sound broken the rest, and
the failures will be intermittent, which is the hardest kind to notice and the hardest to chase.

Three consequences follow, and all three are easy to get wrong:

  THE RESOLUTION IS NOT OPTIONAL, so it happens even where the draw would have leapt away, and
  even where the register or the scale setting would have preferred something else. A constraint
  that yields to a preference is not a constraint.

  IT IS PER VOICE, like the previous-note memory. Two voices suspending at once each resolve
  their own, and neither can see the other's pending note as a previous note of its own.

  IT IS CLEARED ON A REWIND, again like the previous-note memory, or the first note after a
  rewind resolves something nobody heard.

THE ORDER INSIDE A NOTE IS FIXED AND MUST STAY FIXED. The rhythm's duration is one of the inputs
to the pitch decision, because a short note is freed to be a passing tone. The articulation then
adjusts that duration according to what was drawn. The adjusted value must never re-enter the
decision, or the two chase each other. */
