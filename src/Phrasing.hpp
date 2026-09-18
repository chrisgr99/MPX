#pragma once
/** Deciding one phrase's rhythm, and nothing else.

PURE, AND DELIBERATELY SO. No Rack, no module, no state: the phrase, the metre, the tempo, the
controls and a seed go in, and a pattern of notes comes out. That is what lets it be measured
from a command line over thousands of phrases at several tempos — see test/phrasetest.cpp —
before there is anything to listen to. The melody engine's worst fault was invisible by ear and
obvious in numbers, and this is the same arrangement for the same reason.

A PHRASE IS DECIDED WHOLE, WHEN IT BEGINS. The repetition, the shape of its last group and the
placement of pickups all depend on seeing the whole phrase, and none of them can be decided one
slot at a time. That is why the chart publishes every chord change inside a phrase.

TWO LEVELS. The phrase comes from the chart, cadence to cadence. Inside it, this divides the time
into SUB-PHRASES — breath groups, about three seconds each — and fills them with notes. Measured
in 456 jazz solos and 22 lead sheets; the figures and their sources are in docs/phrase.md.

LENGTH IS IN SECONDS, NOT BARS. Across a fourfold rise in tempo the median jazz phrase went from
3.5 beats to 9.4 while its duration fell only from 3.4 seconds to 2.3: players keep a phrase to
the same few seconds and fit more beats into it. A length fixed in bars gives breathless lines
slowly and choppy ones quickly.
*/
#include <cstddef>
#include <cstdint>

namespace px {

/** The grid a bar is divided into. */
enum PhraseSubdivision {
	SUB_QUARTER,
	SUB_EIGHTH,
	SUB_SIXTEENTH,
	SUB_TRIPLET,        /**< Three to a beat. */
	NUM_SUBDIVISIONS,
};

/** How a group starts, and how it ends. Drawn from the mixes START and ENDING set, because both
are spread in real music rather than habitual: song melodies started on the downbeat or with a
pickup about a third of the time each, jazz phrases off the beat more than half the time. */
enum PhraseStart { START_PICKUP, START_DOWNBEAT, START_AFTER, NUM_STARTS };
enum PhraseEnd { END_STRONG, END_OFFBEAT, NUM_ENDS };

/** Everything a patcher sets. Each is a likelihood or an amount, never a fixed outcome. */
struct PhraseControls {
	int subdivision = SUB_EIGHTH;

	/** A breath group's length, in SECONDS. */
	float groupSeconds = 3.f;
	/** How much groups fragment toward the end of a phrase, or run long. */
	float vary = 0.2f;
	/** The mix of starts: -1 mostly pickups, 0 the measured song mix, +1 mostly off the beat. */
	float start = 0.f;
	/** The mix of endings: 0 mostly off the beat, 1 mostly on a strong beat. */
	float ending = 0.6f;

	/** How long the line stops moving at the end of a group, in beats, and again at the end of a
	phrase. Silence in real lines is frequent and short: the median silence between jazz phrases
	was 1.9 beats, and two bars or more happened about once in a hundred. */
	float pause = 2.f;
	float phrasePause = 2.f;
	/** How the pause is filled: nought all silence, one the last note sustained through it. */
	float hold = 0.3f;
	/** The chance a whole phrase is left unplayed. About one in twenty-five in the lead sheets. */
	float silentPhrases = 0.f;

	/** Inside a group. */
	float density = 0.55f;
	float syncopation = 0.25f;
	/** How much a slot on which the chord changes is favoured. */
	float onChanges = 0.4f;
	/** Note length, from short and separated to reaching the next onset. */
	float length = 0.6f;
	/** How far levels spread by metric weight. */
	float dynamics = 0.5f;

	/** How far every draw may depart from its likeliest outcome. At nought the pattern is the
	likeliest one the settings allow and the seed makes no difference. */
	float variation = 0.5f;
};

/** THE TWO STYLES, as starting points. The corpora sit at opposite ends for where phrases start
and end, so one default could not serve both. Written into the controls when chosen, not applied
over them afterwards. */
enum PhraseStyle { STYLE_SONG, STYLE_JAZZ, NUM_PHRASE_STYLES };
void phraseStyle(int style, PhraseControls& out);
const char* phraseStyleName(int style);

/** What the phrase is, and where the randomness comes from. */
struct PhraseAsk {
	/** The phrase's length and the metre, in beats. */
	float phraseBeats = 16.f;
	float barBeats = 4.f;
	/** The tempo, which is what a group's length in seconds is converted through. */
	float beatsPerSecond = 2.f;
	/** How the phrase ends, as a ChartCadence. Nought is none. */
	int cadence = 0;
	/** Chord changes inside the phrase, in beats from its start. */
	const float* changes = NULL;
	int changeCount = 0;
	/** The seed, and where this phrase falls in the cycle of variations. */
	uint32_t seed = 0;
	int cyclePosition = 0;
};

struct PhraseNote {
	float offset = 0.f;      /**< Beats from the phrase's start. */
	float duration = 0.f;    /**< Beats. */
	float level = 0.f;       /**< Nought to one. */
	bool onChange = false;   /**< Fell on a chord change. For the log and the census. */
	int group = 0;
};

/** One phrase's rhythm. Fixed arrays: this is generated on the audio thread when a phrase begins,
and nothing there may allocate. */
struct PhrasePattern {
	static const int MAX_NOTES = 256;
	static const int MAX_GROUPS = 24;

	PhraseNote notes[MAX_NOTES];
	int noteCount = 0;

	/** Each group: where it begins, where its notes stop, and how long the pause after it is.
	`soundsTo` is the end of the sounding part, so the pause is what lies between it and the next
	group's start. A pickup can begin before `starts`. */
	float starts[MAX_GROUPS] = {};
	float soundsTo[MAX_GROUPS] = {};
	float pauses[MAX_GROUPS] = {};
	int startKind[MAX_GROUPS] = {};
	int endKind[MAX_GROUPS] = {};
	int groupCount = 0;

	/** Whether the whole phrase was left unplayed. */
	bool silent = false;
	/** Set when the phrase was too long, or the grid too fine, for MAX_NOTES. Visible rather than
	silent: a phrase that quietly lost its last notes would be a bug nobody could see. */
	bool full = false;
};

/** Decides one phrase. */
void phraseGenerate(const PhraseAsk& ask, const PhraseControls& controls, PhrasePattern& out);

} // namespace px
