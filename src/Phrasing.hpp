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
	/** HOW DEEP THE LOUDNESS MOVES, nought to one: across the phrase by SHAPE, from one phrase
	to the next, and note to note by stress. At nought every note is the same level. */
	float dynamics = 0.5f;
	/** THE SHAPE OF THE LOUDNESS ACROSS A PHRASE. Nought falls from the start to the end, which is
	what the jazz solos do: about a decibel above a player's average at the start, two below by the
	end. One is an arch, rising to a peak a little before the middle and falling to the end, which
	is how a song phrase is sung. Either way the last note is softer. */
	float shape = 0.7f;
	/** MOTIF, nought to one: the chance that a group restates the rhythm of a group before it in
	the same phrase — the one just before, or the one before that — from its start up to its own
	ending, and then goes on in its own way. In thirty pop songs a third of the lines restate the
	rhythm of one of the three lines before them, most often the one just before. */
	float motif = 0.f;
	/** HOW OFTEN A BEAT SOUNDS THE MIDDLE UNIT OF A TRIPLET, as a share of the beats that carry a
	note. On a swung line the eighth-note pair already IS a triplet — the long part is two units
	and the short part the third — so a triplet figure is the line also sounding the unit that
	swing skips. In 456 jazz solos that happened on eight per cent of played beats, and 82 per
	cent of the time on a single beat rather than in a run. */
	float triplets = 0.08f;

	/** HOW CLOSELY A PHRASE RESTATES THE ONE BEFORE IT. At nought each is decided fresh; at one
	the onsets are copied. Between, each slot keeps the earlier decision with that likelihood.
	The last group is always decided afresh, so a restated phrase still ends in its own place. */
	float repeat = 0.f;

	/** AFTER HOW MANY PHRASES THE WHOLE SEQUENCE OF VARIATIONS RETURNS. The caller counts the
	phrases and folds the count by this, so at four the fifth phrase decides exactly as the first
	did. At one every phrase decides alike. */
	int cycle = 4;

	/** HOW MUCH A RETURNING SECTION RETURNS TO WHAT IT PLAYED. Acted on by the caller, which is
	the only part of this that knows what a section is. */
	float sections = 0.f;

	/** HOW OFTEN A CLOSING CADENCE'S ARRIVAL IS ALSO THE NEXT PHRASE'S FIRST NOTE, with no breath
	between them. At nought every phrase breathes. */
	float elide = 0.f;

	/** How far every draw may depart from its likeliest outcome. At nought the pattern is the
	likeliest one the settings allow and the seed makes no difference. */
	float variation = 0.5f;
};

/** THE TWO STYLES, as starting points. The corpora sit at opposite ends for where phrases start
and end, so one default could not serve both. Written into the controls when chosen, not applied
over them afterwards. */
/** THE STARTING POINTS. SONG is what the module comes up with; the others ship as presets, in
three weights of each: 1 a ballad, 2 the corpus's own middle, 3 up-tempo and busy. */
enum PhraseStyle { STYLE_SONG, STYLE_POP1, STYLE_POP2, STYLE_POP3, STYLE_JAZZ1, STYLE_JAZZ2,
	STYLE_JAZZ3, NUM_PHRASE_STYLES };
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

	/** THE PHRASE BEFORE THIS ONE, or nothing at the head of a cycle. What REPEAT copies from,
	and it is the pattern as it was decided rather than as it was played: the feel is applied
	afterwards, so a phrase restated at a different tempo swings by the tempo it is at rather
	than by the one its ancestor was at. */
	const struct PhrasePattern* previous = NULL;

	/** HOW THE BEAT IS DIVIDED, from the chart. One is even; two would be a full triplet feel.
	The pair is published rather than an amount, because the chart has already converted for the
	tempo it is running at — see Swing.hpp. Which of the two applies depends on the subdivision
	the controls are working at. */
	float swingEighth = 1.f;
	float swingSixteenth = 1.f;

	/** HOW FAR BEFORE ITS FIRST BAR LINE THE PHRASE MAY BEGIN, in beats: the silence the phrase
	before it leaves at its end, less a slot so the two do not touch. Nought where nothing is known
	about the phrase before, and then a phrase begins on or after its bar line. See
	phrasePickupRoom. */
	float pickupRoom = 0.f;

	/** HOW THIS PHRASE STARTS, as the phrase before it drew it — a PhraseStart, and the pickup's
	or the late start's length in beats — or below nought to draw it here. The phrase before draws
	it so that it can end early enough to leave the pickup room after its breath. */
	int leadKind = -1;
	float leadBeats = 0.f;

	/** WHERE THE PHRASE SITS IN THE FORM, for its overall loudness: which phrase of its section it
	is, from nought; whether its section is a contrasting one, anything other than the form's
	first section; and how far through one pass of the form it begins, nought to one. */
	int phraseInSection = 0;
	bool contrasting = false;
	float intoForm = 0.f;
};

/** THE LONGEST PICKUP A PHRASE MAY HAVE, in beats. Half a bar of 4/4: in thirty pop songs a
phrase that began before its bar line began within the last beat and a half of the bar before it
in most cases, and a rhythm generator decides the next phrase this far ahead of its start. */
static const float PICKUP_MAX_BEATS = 2.f;

struct PhraseNote {
	/** Beats from the phrase's start. Below nought for a pickup, which sounds in the breath at
	the end of the phrase before. */
	float offset = 0.f;
	float duration = 0.f;    /**< Beats. */
	float level = 0.f;       /**< Nought to one. */
	bool onChange = false;   /**< Fell on a chord change. For the log and the census. */
	/** One of the three notes of a triplet figure. Such a beat is played evenly and is exempt
	from the swing deformation: below a full swing the long-short pair does not line up with an
	even three-per-beat division, so a triplet drawn over it would be neither one thing nor the
	other. */
	bool triplet = false;
	/** The last note of the phrase, and the last note of its group. Set after everything else,
	so they name the notes as they will actually be played. */
	bool arrival = false;
	bool groupEnd = false;
	/** The note before the arrival, when it is in the same group — the one a line steps from. */
	bool approach = false;
	int group = 0;
	/** MOTIF. Which note this is, kept through the passes that move notes about, and which note
	it restates the rhythm of, or -1; then, once the notes are final, how many notes back that one
	is, which is what goes on the cable (Event::echo) — nought for none. */
	int id = -1;
	int echoOf = -1;
	/** Whether that note is in the phrase before. */
	bool echoPrev = false;
	int echo = 0;
	/** How far through its group the note starts, nought at the group's first note and one at
	its last: see Event::along. */
	float along = -1.f;
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
	/** THE NOTES AS THEY WERE DECIDED, before the triplets and the swing. What a later phrase
	restates: the feel belongs to the moment a phrase is played, not to the idea being restated. */
	PhraseNote raw[MAX_NOTES];
	int rawCount = 0;

	/** How many of this phrase's slots REPEAT took from the phrase before, for the record. */
	int restated = 0;

	/** HOW THE NEXT PHRASE STARTS, drawn here so this phrase could leave room for its pickup.
	Handed to the next phrase as PhraseAsk::leadKind and leadBeats. */
	int nextLead = START_DOWNBEAT;
	float nextLeadBeats = 0.f;

	/** Set when the phrase was too long, or the grid too fine, for MAX_NOTES. Visible rather than
	silent: a phrase that quietly lost its last notes would be a bug nobody could see. */
	bool full = false;
};

/** THE LONGEST BREATHS, in beats: between the lines of a phrase, and between phrases. A longer
silence inside a phrase is heard as the line having stopped. */
static const float GROUP_PAUSE_MOST = 2.f;
static const float PHRASE_PAUSE_MOST = 4.f;

/** Decides one phrase. */
void phraseGenerate(const PhraseAsk& ask, const PhraseControls& controls, PhrasePattern& out);

/** THE ROOM THE PHRASE AFTER `ending` HAS FOR A PICKUP: from where the last of `ending`'s notes
stops sounding to its end, less a slot of the grid, and at most PICKUP_MAX_BEATS. Nought for a
silent phrase or one held to its end. `endingBeats` is its length. Pure, so a simulation asks the
same question the module does. */
float phrasePickupRoom(const PhrasePattern& ending, float endingBeats, int subdivision);

/** The generator's own hash, so a caller can key a decision the same way it does — which is what
a returning section needs. */
uint32_t phraseHash(uint32_t x);

} // namespace px
