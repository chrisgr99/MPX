#pragma once
/** THE PERFORMER: what a note marked hammer-on actually sounds like.

NO RACK IN HERE, and no synthesiser either. Notes and their articulations go in; what comes out is
a handful of voices whose pitch, level, gate and brightness move over time, and a queue of the
moments those things changed. Two modules read that — one puts it on control voltage, the other
sends it to FluidSynth — and because they read the same thing they cannot disagree about what a
hammer-on is. See docs/guitar-player-spec.md.

A VOICE IS A STRING. That is the whole reason this is not a note-per-voice allocator: a bend moves
one string and leaves its neighbours alone, a string is monophonic because a real one is, and let
ring ends when that string is played again rather than after some length. A note with no string —
anything that is not a fretted instrument — gets a voice of its own.

NOTHING IS KNOWN ABOUT THE FUTURE. In a patch the notes arrive as they happen, so a slide into a
note cannot begin before the note does and a grace note cannot be squeezed in ahead of its beat.
Both are handled where they arrive: a slide in reaches its pitch a few tens of milliseconds after
the onset rather than before it, and a grace note is already a note of its own in the file with
its own moment. The alternative is to delay everything by a fixed latency, which is worse than
either.

WHAT IS DECIDED HERE AND WHAT IS NOT. The performer decides timing, pitch movement, level and
brightness. It decides nothing about tone: which sample, which filter, which oscillator is the
renderer's business. Every number it uses comes from a rules table in a file, so the whole
character of the playing can be tuned by ear without rebuilding anything.
*/
#include <cstdint>
#include <string>
#include <vector>

namespace px {


/** EVERY NUMBER THE PERFORMER USES. Read from a text file of `name = value` lines, so it can be
edited while listening — see performRulesRead. The defaults are the starting point the
specification sets out; they are not claimed to be right, and the file is how they get better.

WHY NOT JSON. This has to build without Rack so that it can be tested from a command line, and
the numbers are a flat list of names and values. A file of lines needs no library and reads more
easily than braces do. */
struct PerformRules {
	// Sounded by the left hand: no new attack, the pitch slides to the new note, and it is
	// weaker than a picked note.
	float hammerGlideMs = 8.f;
	float hammerLevel = 0.7f;

	// A slide to the next note. The legato one strikes nothing when it arrives; the shift slide
	// strikes it a little softer than a plain note.
	float slideFraction = 0.6f;      /**< Of the sounding note's length that the slide takes. */
	float slideMaxMs = 250.f;
	float shiftSlideLevel = 0.8f;

	// A slide into a note from below or above, and out of one at its end.
	float slideInSemitones = 3.f;
	float slideInMs = 60.f;
	float slideOutDownSemitones = 5.f;
	float slideOutUpSemitones = 3.f;
	float slideOutMs = 120.f;
	float slideOutFade = 0.3f;       /**< What the level falls to by the end of it. */

	// Palm mute, dead notes, ghost notes, staccato: all of them length and level and brightness.
	// A palm mute's note lasts long enough for a modelled string to die away by itself, about
	// 0.7 s, and is struck nearly as hard as an open one: the hand is on the strings, not holding
	// back the pick.
	float palmLength = 0.35f;
	float palmMaxMs = 600.f;
	float palmLevel = 0.95f;
	float palmTimbre = 0.25f;
	float deadMs = 25.f;
	float deadLevel = 0.6f;
	float deadTimbre = 0.1f;
	float ghostLevel = 0.5f;
	float ghostLength = 0.8f;
	float staccatoLength = 0.5f;
	float accentLevel = 1.18f;
	float heavyAccentLevel = 1.35f;
	float harmonicLevel = 0.8f;
	float harmonicTimbre = 0.95f;
	float openTimbre = 0.6f;         /**< An ordinary note's brightness. */

	// Let ring: until the string is struck again, and no longer than this whatever happens.
	float letRingMaxSeconds = 8.f;

	// Vibrato, which the performer makes rather than the source: it starts after the note has
	// settled and comes in rather than being there from the first moment.
	float vibratoSlightCents = 25.f;
	float vibratoWideCents = 50.f;
	float vibratoHz = 5.5f;
	float vibratoDelayMs = 100.f;
	float vibratoFadeMs = 150.f;

	// A brushed chord: the hand crosses the strings in a few tens of milliseconds, and leans on
	// the first string it reaches.
	float strumDownMs = 18.f;
	float strumUpMs = 12.f;
	float strumLevelFall = 0.04f;    /**< Per string, in order of being struck. */
	float strumUpExtraFall = 0.1f;

	// Tremolo picking, where the file says a note is picked repeatedly rather than held.
	float tremoloHz = 16.f;
	float tremoloLevelVary = 0.05f;

	// Humanising, all of it scaled by one control on the module, and all of it drawn from the
	// seed so that the same patch plays the same way twice.
	float timingMs = 6.f;
	float levelVary = 0.06f;
	float lengthVary = 0.05f;

	/** How far a full bend reaches, for a renderer that has to say so once. */
	float bendRangeSemitones = 12.f;
};

/** Reads a rules file over the defaults, ignoring what it does not recognise: a file written for
a later version is read as far as it goes rather than refused. Returns how many names it set, and
names the first line it could not use. */
int performRulesRead(const std::string& text, PerformRules& rules, std::string* complaint);
/** Writes every rule out, so a starting file can be made from the defaults. */
std::string performRulesWrite(const PerformRules& rules);


/** A NOTE AS THE CABLE DESCRIBES IT. The same fields the MPX event carries, without the bus in
the way, so this library can be driven from a test as easily as from a module. */
struct PerformNote {
	int64_t handle = 0;
	float pitch = 0.f;           /**< Volts, an octave to the volt, nought at middle C. */
	float level = 0.6f;
	float seconds = 0.5f;        /**< How long the note is meant to sound. */
	int string = 0;              /**< 1 is the highest; nought for an instrument with none. */
	int fret = -1;
	uint32_t technique = 0;      /**< The Event::Technique bits. */
	uint8_t vibrato = 0;
	uint8_t grace = 0;
	float pan = 0.f;
	int8_t strum = 0;
	uint8_t strumIndex = 0;
	uint8_t strumMs = 0;
	/** Up to four points: where through the note, and how many cents up. */
	int bendCount = 0;
	float bendAt[4] = {0.f, 0.f, 0.f, 0.f};
	float bendCents[4] = {0.f, 0.f, 0.f, 0.f};
};


/** WHAT A RENDERER READS EVERY SAMPLE. One of these per string, whether or not it is sounding. */
struct PerformVoice {
	bool gate = false;
	int string = 0;
	int64_t handle = 0;
	/** Volts: the pitch as played, with the bend, the slide and the vibrato already in it. */
	float pitch = 0.f;
	/** Cents away from the note it was struck at, for a renderer that wants the two apart. */
	float cents = 0.f;
	float level = 0.f;
	/** Nought closed, one open: palm mutes and dead notes are dark, harmonics bright. */
	float timbre = 0.6f;
	/** WHAT THE SOURCE SAID WHILE THE NOTE SOUNDED, passed through rather than decided here:
	the performer has no opinion about a breath controller. */
	float pressure = 0.f;
	float pan = 0.f;
};


/** WHAT A RENDERER READS WHEN IT WANTS MOMENTS RATHER THAN VALUES. A synthesiser is told things;
it does not read a voltage every sample. */
struct PerformMessage {
	enum Kind : uint8_t {
		ATTACK,      /**< Strike the note. `key` is the MIDI note, `value` the level. */
		RELEASE,
		BEND,        /**< `value` is cents from the note it was struck at. */
		LEVEL,       /**< A note already sounding getting louder or quieter. */
		TIMBRE,
	};
	uint8_t kind = ATTACK;
	int voice = 0;
	int key = 60;
	float value = 0.f;
	/** WHAT KIND OF NOTE, on a strike and on a note the left hand sounds without one: the
	cable's technique bits, unchanged, and the fret it is played at, or -1. A renderer that
	shapes a sound can ignore them, since the level, the length and the brightness already
	carry their effect; one that models a string needs them, because a palm mute is a hand on
	the string and not merely a darker note. */
	uint32_t technique = 0;
	int fret = -1;
};


/** How many strings the performer holds voices for. Twelve, so a twelve-string is one voice per
course and nothing has to be shared. */
static const int PERFORM_VOICES = 12;


struct Performer {
	PerformRules rules;

	/** Everything is drawn from this, so a patch plays the same way twice. */
	void seed(uint32_t s) { random = s ? s : 1u; }
	/** Nought to two: none of the humanising, the rules' own amount, or twice it. */
	void humanise(float amount) { human = amount; }

	/** How many strings the instrument has, so a note with no string of its own gets a voice
	nothing else is using. Nought for an instrument that is not fretted. */
	void strings(int count);

	/** A note arriving now. Nothing is queued: what it does to the voices happens here, and
	what happens over the note's length is decided here too. */
	void note(const PerformNote& note);

	/** Moves everything on by `dt` seconds and fills the message queue. */
	void advance(float dt);

	/** Stops everything, as a cable being pulled out does. */
	void silence();
	/** Stops one voice: a source that ended its own note, which is a string stopped by hand. */
	void silenceVoice(int i);
	/** What the source is saying while the note sounds. `timbre` chooses which of the two. */
	void set(int i, bool timbre, float value);

	const PerformVoice& voice(int i) const { return voices[i]; }
	int voiceCount() const { return PERFORM_VOICES; }

	/** Takes the next message, or false when there are none left this block. */
	bool next(PerformMessage& out);

private:
	struct Plan {
		bool sounding = false;
		float age = 0.f;
		float length = 0.f;        /**< Seconds it sounds for; large for a note left ringing. */
		float base = 0.f;          /**< Volts it was struck at. */
		float level = 0.f;
		float timbre = 0.6f;
		float pan = 0.f;
		float startDelay = 0.f;    /**< Seconds still to wait before it is struck. */
		bool struck = false;
		int key = 60;
		int64_t handle = 0;
		uint32_t technique = 0;
		int fret = -1;
		uint8_t vibrato = 0;
		int bendCount = 0;
		float bendAt[4] = {0.f, 0.f, 0.f, 0.f};
		float bendCents[4] = {0.f, 0.f, 0.f, 0.f};
		/** A slide into the note: where it starts, in cents, decaying to nothing. */
		float slideInCents = 0.f;
		/** A slide the note before it asked for, being carried out on this voice. */
		float glideFrom = 0.f;     /**< Cents, falling to nothing over `glideLeft`. */
		float glideLeft = 0.f;
		float glideSpan = 0.f;
		float sentCents = 0.f, sentLevel = 0.f, sentTimbre = 0.f;
		bool everSent = false;
	};
	Plan plans[PERFORM_VOICES];
	PerformVoice voices[PERFORM_VOICES];
	int stringCount = 0;
	float human = 1.f;
	uint32_t random = 1u;

	PerformMessage queue[64];
	int queued = 0, taken = 0;

	void push(uint8_t kind, int voice, int key, float value, uint32_t technique = 0,
		int fret = -1);
	int voiceFor(const PerformNote& n);
	float noise();               /**< Minus one to one, from the seed. */
	void shape(Plan& p, float dt, int index);
};


} // namespace px
