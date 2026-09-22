/** See Phrasing.hpp. */
#include "Phrasing.hpp"
#include "ChartLayout.hpp"

#include <vector>

#include <algorithm>
#include <cmath>

namespace px {

// ---- the draw ---------------------------------------------------------------------------------

/** A counter turned into something that does not look like one. Three rounds of shift and
multiply: cheap, no state but its argument, and the same argument always gives the same number,
which is what makes a phrase reproducible. */
static uint32_t hash32(uint32_t x) {
	x ^= x >> 16; x *= 0x7feb352du;
	x ^= x >> 15; x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

/** The draws for one phrase. Every choice takes its number from the seed, the phrase's place in
the cycle, and a running count of how many draws have been made — so the same phrase decides the
same way every time, and two phrases in the same cycle position decide alike. */
struct Draws {
	uint32_t base = 0;
	uint32_t n = 0;
	float variation = 1.f;

	float next() {
		return (float) (hash32(base + 0x9e3779b9u * ++n) >> 8) / 16777216.f;
	}

	/** Nought to one, sharpened by VARIATION: at nought every draw returns the middle, so a
	weighted choice takes its likeliest outcome and a jitter adds nothing. */
	float middling() {
		return 0.5f + (next() - 0.5f) * variation;
	}

	/** True with probability p, AND NOT SHARPENED BY VARIATION.

	IT WAS, AND IT MADE A METRONOME. At nought this returned whether p was better than even, so a
	density of 0.5 sounded every slot above the middle and none below — twelve identical quarter
	notes in a row, every phrase the same, and a one-in-six chance of a triplet that never once
	came up. A knob whose nought produces a metronome is a trap, and worse, VARIATION at anything
	below the middle quietly moved every other control's meaning.

	A PROBABILITY IS ALREADY A PROPORTION. Density at a half means half the slots, whatever else
	is set; VARIATION belongs on the jitters — how long a pause is, where a group starts, how far
	the length wanders — and on nothing that is simply a yes or a no. So this is a plain draw, and
	the seed decides it even with VARIATION at nought. */
	bool chance(float p) {
		return next() < p;
	}

	/** One of `count` weighted choices, in the proportions the weights give.

	NO TEMPERATURE HERE, and that was a mistake worth recording. Sharpening these weights by
	VARIATION was tried first, and it wrecked the one thing they are for: START and ENDING are
	mixes measured from real music, and at the default variation an intended 62 per cent came out
	at 85. A mix IS the proportion; VARIATION belongs on the jitters and on the onsets, not on the
	shape a control is asking for.

	AT NOUGHT THE LIKELIEST WINS, so a phrase with no variation is the same whatever the seed. */
	int pick(const float* weights, int count) {
		if (count <= 0)
			return 0;
		int best = 0;
		float total = 0.f;
		for (int i = 0; i < count; i++) {
			total += std::max(0.f, weights[i]);
			if (weights[i] > weights[best])
				best = i;
		}
		if (variation <= 0.01f || !(total > 0.f))
			return best;
		const float target = next() * total;
		float running = 0.f;
		for (int i = 0; i < count; i++) {
			running += std::max(0.f, weights[i]);
			if (running > target)
				return i;
		}
		return count - 1;
	}
};

// ---- the grid ---------------------------------------------------------------------------------

static int slotsPerBeat(int subdivision) {
	switch (subdivision) {
		case SUB_QUARTER: return 1;
		case SUB_SIXTEENTH: return 4;
		case SUB_TRIPLET: return 3;
		default: return 2;
	}
}

/** HOW STRONG A POSITION IS, from one for a bar's downbeat down to a fifth for a sixteenth off
the beat. GXW's values, extended to finer subdivisions: a downbeat, the middle of an even bar,
a beat, an eighth between beats, then anything finer. */
static float metricStrength(float beatsIntoBar, float barBeats, int subdivision) {
	const float eps = 1e-4f;
	if (beatsIntoBar < eps)
		return 1.f;
	const int whole = (int) std::lround(beatsIntoBar);
	const bool onBeat = std::fabs(beatsIntoBar - (float) whole) < eps;
	if (onBeat) {
		const int bars = (int) std::lround(barBeats);
		if (bars % 2 == 0 && whole == bars / 2)
			return 0.7f;
		return 0.5f;
	}
	// Between beats: halfway is stronger than anything finer.
	const float withinBeat = beatsIntoBar - std::floor(beatsIntoBar);
	if (std::fabs(withinBeat - 0.5f) < eps)
		return 0.3f;
	(void) subdivision;
	return 0.2f;
}

static bool nearChange(const PhraseAsk& ask, float offset, float tolerance) {
	for (int i = 0; i < ask.changeCount; i++) {
		if (std::fabs(ask.changes[i] - offset) < tolerance)
			return true;
	}
	return false;
}

/** HOW LONG THE CHORD SOUNDING AT `offset` LASTS, in beats, from the changes inside the phrase.
The chord in force at the phrase's start is counted from the start, which is as much as the phrase
knows; with no changes at all the answer is nought, meaning unknown. */
static float chordLengthAt(const PhraseAsk& ask, float offset) {
	if (ask.changeCount <= 0)
		return 0.f;
	float from = 0.f, to = ask.phraseBeats;
	for (int i = 0; i < ask.changeCount; i++) {
		const float c = ask.changes[i];
		if (c <= offset + 0.001f)
			from = std::max(from, c);
		else
			to = std::min(to, c);
	}
	return std::max(0.25f, to - from);
}

// ---- the styles -------------------------------------------------------------------------------

void phraseStyle(int style, PhraseControls& out) {
	out = PhraseControls();
	// WHAT EVERY PRESET SHARES: pauses in half beats, a line that follows the harmony, a loudness
	// that moves. The styles below differ only where the music does.
	auto common = [&]() {
		out.subdivision = SUB_EIGHTH;
		out.vary = 0.3f;
		out.variation = 0.3f;
		out.onChanges = 0.8f;
		out.silentPhrases = 0.f;
		out.elide = 0.f;
		out.cycle = 4;
	};
	switch (style) {
		case STYLE_POP1:
			// A BALLAD — Michelle, Killing Me Softly: few notes, mostly quarters, lines held at
			// their ends and entered a little late, the loudness arching over each phrase.
			common();
			out.groupSeconds = 5.f; out.start = 0.6f; out.ending = 0.5f;
			out.pause = 1.f; out.phrasePause = 1.5f; out.hold = 0.7f;
			out.density = 0.35f; out.syncopation = 0.2f; out.length = 1.f;
			out.dynamics = 0.6f; out.shape = 0.8f; out.repeat = 0.3f; out.sections = 0.5f;
			out.triplets = 0.f; out.motif = 0.35f;
			break;
		case STYLE_POP2:
			// THIRTY POP SONGS, measured in research/pop: half the phrases lead in from the bar
			// before and one in six starts on the bar line, which is START's middle; two phrases in
			// five end on an anticipation, so ENDING leans off the beat; a line sings for about five
			// beats and breathes for about one; the last note is the longest four times in five.
			// THE PAUSES ARE SHORTER THAN THE MEASURED ONES, by ear: in a record the band plays
			// through the singer's rests, and here nothing does.
			common();
			out.groupSeconds = 3.5f; out.start = 0.f; out.ending = 0.4f;
			out.pause = 1.f; out.phrasePause = 1.f; out.hold = 0.3f;
			out.density = 0.5f; out.syncopation = 0.35f; out.length = 1.f;
			out.dynamics = 0.5f; out.shape = 0.6f; out.repeat = 0.3f; out.sections = 0.5f;
			out.triplets = 0.f; out.motif = 0.35f;
			break;
		case STYLE_POP3:
			// UP-TEMPO POP: busier and pushed — more pickups, more anticipations, shorter holds.
			common();
			out.groupSeconds = 3.f; out.start = -0.3f; out.ending = 0.35f;
			out.pause = 0.5f; out.phrasePause = 1.f; out.hold = 0.2f;
			out.density = 0.65f; out.syncopation = 0.55f; out.length = 0.7f;
			out.dynamics = 0.5f; out.shape = 0.5f; out.repeat = 0.4f; out.sections = 0.6f;
			out.onChanges = 0.7f; out.triplets = 0.f; out.motif = 0.4f;
			break;
		case STYLE_JAZZ1:
			// A JAZZ BALLAD: sparse and lyrical, a few triplets, lines held longer than in a solo.
			common();
			out.groupSeconds = 3.5f; out.start = 0.5f; out.ending = 0.5f;
			out.pause = 1.5f; out.phrasePause = 2.f; out.hold = 0.4f;
			out.density = 0.45f; out.syncopation = 0.35f; out.length = 0.8f;
			out.dynamics = 0.5f; out.shape = 0.3f; out.repeat = 0.2f; out.sections = 0.5f;
			out.onChanges = 0.6f; out.triplets = 0.06f; out.motif = 0.25f;
			break;
		case STYLE_JAZZ2:
			// THE JAZZ SOLOS' OWN FIGURES: 55 per cent of phrases started off the beat and 62 per
			// cent ended there, against 10 and 13 per cent on a bar's downbeat; the last note the
			// longest a little over half the time; loudness falling through each phrase.
			common();
			out.groupSeconds = 2.7f; out.start = 1.f; out.ending = 0.38f;
			out.pause = 2.f; out.phrasePause = 2.f; out.hold = 0.06f;
			out.density = 0.7f; out.syncopation = 0.5f; out.length = 0.45f;
			out.dynamics = 0.5f; out.shape = 0.f; out.repeat = 0.2f; out.sections = 0.3f;
			out.onChanges = 0.5f; out.triplets = 0.08f; out.motif = 0.2f;
			break;
		case STYLE_JAZZ3:
			// UP-TEMPO JAZZ, bebop: dense, off the beat, short notes, more triplets.
			common();
			out.groupSeconds = 2.3f; out.start = 1.f; out.ending = 0.3f;
			out.pause = 1.5f; out.phrasePause = 2.f; out.hold = 0.f;
			out.density = 0.85f; out.syncopation = 0.55f; out.length = 0.4f;
			out.dynamics = 0.5f; out.shape = 0.f; out.repeat = 0.1f; out.sections = 0.3f;
			out.onChanges = 0.5f; out.triplets = 0.12f; out.motif = 0.15f;
			break;
		default:
			// SONG, what the module comes up with: the lead sheets, mostly jazz standards —
			// endings mostly on a strong beat, the last note held.
			out.start = 0.f;
			out.ending = 0.62f;
			out.density = 0.5f;
			out.syncopation = 0.2f;
			out.hold = 0.7f;
			out.groupSeconds = 3.f;
			out.length = 0.65f;
			out.motif = 0.3f;
			break;
	}
}

const char* phraseStyleName(int style) {
	static const char* NAMES[NUM_PHRASE_STYLES] = {
		"Song", "Pop 1", "Pop 2", "Pop 3", "Jazz 1", "Jazz 2", "Jazz 3"};
	return (style >= 0 && style < NUM_PHRASE_STYLES) ? NAMES[style] : "Song";
}

// ---- dividing the phrase ----------------------------------------------------------------------

/** Where the groups fall. Returns how many.

LENGTH IN SECONDS, converted through the tempo and rounded to whole beats. Then boundaries prefer
bar lines, because bars group into strong and weak positions as beats do — a mild preference in
the lead sheets, so a weight rather than a rule. VARY fragments the last group, which is the
sentence shape: an idea, the idea again, then shorter units driving to the close. */
static int divide(const PhraseAsk& ask, const PhraseControls& c, Draws& draws, float* starts,
		int maxGroups) {
	// GROUP IS THE LENGTH OF THE SOUNDING PART, because that is what was measured: the jazz
	// figures run from a phrase's first note to the end of its last, with the silence after it
	// counted separately. So the space between one group's start and the next is the group plus
	// its pause, and a group asked for in seconds sounds for that long.
	const float sounding = std::max(1.f, std::round(c.groupSeconds * ask.beatsPerSecond));

	// EVERY BOUNDARY LANDS ON THE GRID, and that has to be enforced here because two of the
	// things it is made of are not on it: GROUP PAUSE is a knob in beats, so a pause of a beat
	// and a quarter put every group after the first a quarter of a beat off the grid — and with
	// it every onset inside that group, since they are all measured from where the group begins.
	//
	// IT WAS AUDIBLE WITHOUT BEING IDENTIFIABLE: notes neither on the beat nor off it, slightly
	// early or late all the way through a phrase, which reads as a player who cannot keep time
	// rather than as anything intended.
	const float slot = 1.f / (float) slotsPerBeat(c.subdivision);
	const float target = std::max(slot,
		std::round((sounding + std::max(0.f, c.pause)) / slot) * slot);
	int count = 0;
	float at = 0.f;
	starts[count++] = 0.f;
	while (at < ask.phraseBeats - 0.001f && count < maxGroups) {
		float next = at + target;
		// A BREATH FALLS AT A BAR LINE: the group ends in the bar before it and the next line
		// begins in the bar after, on its downbeat or with a pickup into it. Of the two bar lines
		// either side of where the length in seconds would end the group, the nearer — the later
		// when they are as near — as long as the group still sounds for between three fifths and
		// half as long again as asked.
		//
		// IT WAS A PREFERENCE, taken seven times in ten and only where it shortened the group by
		// less than a fifth, so most breaths fell wherever the seconds ran out: in the middle of a
		// bar, with the next line starting on its third beat, which is where a singer never
		// breathes.
		const float bars = std::max(1.f, ask.barBeats);
		{
			// A LINE SHORTER THAN A BAR AND A HALF breathes at the half bar instead — beat three in
			// four — or every line at a slow tempo would be pulled a bar long or cut to nothing.
			const bool halves = sounding < bars * 1.5f && std::fmod(bars, 2.f) < 0.001f
				&& bars >= 4.f;
			const float unit = halves ? bars * 0.5f : bars;
			const float lower = std::floor(next / unit + 0.001f) * unit;
			const float upper = lower + unit;
			auto fits = [&](float b) {
				const float sings = b - at - std::max(0.f, c.pause);
				return b > at + 0.001f && sings >= sounding * 0.6f - 0.001f
					&& sings <= sounding * 1.5f + 0.001f;
			};
			const bool lowerFits = fits(lower), upperFits = fits(upper);
			if (lowerFits && upperFits)
				next = (next - lower < upper - next - 0.001f) ? lower : upper;
			else if (upperFits)
				next = upper;
			else if (lowerFits)
				next = lower;
		}
		// A TAIL THAT WOULD SOUND FOR LESS THAN THREE FIFTHS OF A GROUP JOINS THE GROUP BEFORE IT.
		//
		// Measured by what it would SOUND, not by its span: the last group of a phrase gives up the
		// phrase pause at its end, so a three-beat tail with a two-beat phrase pause played for one
		// beat — a second of music between two silences, heard as a phrase of its own. It is
		// absorbed now, and the group before it runs to the phrase end instead.
		const float tail = ask.phraseBeats - next;
		if (tail - std::max(0.f, c.phrasePause) < sounding * 0.6f)
			break;
		// VARY: the last part of a phrase fragments into shorter groups.
		//
		// NOT INTO SCRAPS, THOUGH. Halving the whole span — the sounding part AND its pause —
		// left the sounding part at a third of what GROUP LENGTH asked for once the pause was
		// taken out of it: two or three notes, then a breath, which the ear hears as a phrase in
		// its own right rather than as a fragment of one. A fragment keeps at least three fifths
		// of the length asked for, and its pause on top.
		if (draws.chance(c.vary * (next / std::max(1.f, ask.phraseBeats))))
			next = at + std::round(std::max(sounding * 0.6f + std::max(0.f, c.pause),
				target * 0.5f) / slot) * slot;
		starts[count++] = next;
		at = next;
	}
	return count;
}

// ---- one group's notes ------------------------------------------------------------------------

/** NO HOLE INSIDE A GROUP MAY BE AS LONG AS THE BREATH THAT ENDS IT.

The ear finds a phrase's end by the silence after it, and it takes the longest silence it hears as
that end. So if the holes DENSITY leaves inside a group are longer than the pause between groups,
a listener hears phrases ending in the middle of groups and running straight through the real
boundaries — which is exactly what happened. The rule was that a hole might be half the group,
two and a half beats at the tempo it was heard at, against a breath of one: three beats of
nothing inside a group, then half a beat between two.

SO THE LIMIT IS SET BY THE BREATH: a hole may be at most three quarters of it, and never more than
a beat and a half — a quarter rest inside a phrase is ordinary, a bar of silence is not. Three
fifths was tried first and it overrode DENSITY: at a half, three slots in four sounded, because the
filling was adding back what the density had taken out. At three quarters a hole is still plainly
shorter than the breath, and a density of a half still means about half. The filling runs until
no hole is over the limit, rather than splitting each once and moving on, which left the first
half of a split still too long.

A FUNCTION, BECAUSE IT HAS TO HOLD TWICE: once when a group is filled, and again after REPEAT has
restated the phrase before, since copying the earlier phrase's silence into a slot can open a hole
the first pass had already closed. */
static void fillHoles(const PhraseAsk& ask, const PhraseControls& c, PhrasePattern& out,
		int group, float pause) {
	const float slot = 1.f / (float) slotsPerBeat(c.subdivision);
	// A HOLE IS SILENCE, NOT THE SPACE BETWEEN ONSETS. A note sounds on for as long as LENGTH lets
	// it — up to a slot and twice LENGTH again — so two quarter notes a beat apart leave no silence
	// at all. Measured from onset to onset, a breath of one beat made every gap over three quarters
	// of a beat a hole, the filling went on until every eighth sounded, and DENSITY stopped doing
	// anything: the last line of every phrase was a solid run of eighth notes whatever it was set to.
	const float sounds = slot * (1.f + 2.f * std::max(0.f, std::min(1.f, c.length)));
	const float limit = std::max(slot, std::min(2.f, 0.75f * std::max(pause, slot) + sounds));
	bool filled = true;
	while (filled) {
		filled = false;
		for (int i = 0; i + 1 < out.noteCount; i++) {
			if (out.notes[i].group != group || out.notes[i + 1].group != group)
				continue;
			const float gap = out.notes[i + 1].offset - out.notes[i].offset;
			if (gap <= limit + 0.001f)
				continue;
			const float mid = out.notes[i].offset
				+ std::max(slot, std::round(gap * 0.5f / slot) * slot);
			if (mid >= out.notes[i + 1].offset - 0.001f)
				continue;
			if (out.noteCount >= PhrasePattern::MAX_NOTES) {
				out.full = true;
				return;
			}
			for (int k = out.noteCount; k > i + 1; k--)
				out.notes[k] = out.notes[k - 1];
			out.noteCount++;
			PhraseNote& n = out.notes[i + 1];
			n.offset = mid;
			n.duration = slot;
			n.triplet = false;
			float inBar = std::fmod(mid, std::max(1.f, ask.barBeats));
			if (inBar < 0.f)
				inBar += std::max(1.f, ask.barBeats);
			const float m = metricStrength(inBar, ask.barBeats, c.subdivision);
			n.level = std::min(1.f, std::max(0.05f, 0.55f + (m - 0.5f) * c.dynamics));
			n.onChange = nearChange(ask, mid, slot * 0.51f);
			n.group = group;
			filled = true;
		}
	}
	// A NOTE BEFORE A FILLED HOLE STILL ENDS WHERE THE NEXT BEGINS. Its length was worked out
	// against the gap it used to have.
	for (int i = 0; i + 1 < out.noteCount; i++) {
		if (out.notes[i].group != group)
			continue;
		const float room = out.notes[i + 1].offset - out.notes[i].offset;
		if (room > 0.f && out.notes[i].duration > room)
			out.notes[i].duration = room;
	}
}


/** HOW A GROUP STARTS, and by how much: a pickup of `beats` before it, a start `beats` after
it, or on it. */
struct Lead {
	int kind = START_DOWNBEAT;
	float beats = 0.f;
};

/** DRAWS HOW A GROUP STARTS, from the START mix.

DRAWN BEFORE THE GROUP BEFORE IT IS FILLED, and that is the point. A pickup sounds in the breath
at the end of the group before, so that group has to know how long the pickup will be and end
that much earlier — or the pickup can only use whatever silence happens to be left, a short
pause leaves none, and a pickup turns into a late start that makes the breath longer still. So a
pause is always the silence heard, and the pickup comes after it: the held last note, the
breath, then the lead-in.

A phrase's own first group leads in further than a group inside a phrase — up to two beats,
against up to a beat — since the breath before a phrase is the longer one. */
static Lead drawLead(const PhraseControls& c, Draws& draws, bool phraseStart) {
	// HOW IT STARTS. A pickup begins before the group, taking its time from the pause before it;
	// a delayed start begins after. Weights from the START mix, with the middle of the range
	// giving the pop songs' own proportions.
	// THE THREE POINTS ARE MEASURED, and the knob moves between them. In the middle: thirty pop
	// songs, where half the phrases lead in from the bar before, a third come in within a beat
	// after the bar line and only one in six starts on it. At one: the jazz solos, more than half
	// starting off the beat and one in ten on a downbeat. At minus one: mostly pickups, which no
	// corpus showed but which is a real way to play and is what the end of a range is for.
	//
	// THE MIDDLE WAS TWENTY-TWO LEAD SHEETS, mostly jazz standards, which put pickups and downbeat
	// starts level at 37 per cent each. The pop songs are the larger sample and are sung, which is
	// what the middle of this knob is for; their downbeat share is less than half of that.
	static const float MIX_PICKUP[3] = {0.80f, 0.51f, 0.12f};
	static const float MIX_DOWNBEAT[3] = {0.15f, 0.17f, 0.33f};
	static const float MIX_AFTER[3] = {0.05f, 0.32f, 0.55f};
	const float s = std::max(-1.f, std::min(1.f, c.start));
	const int lo = s < 0.f ? 0 : 1;
	const float f = s < 0.f ? s + 1.f : s;
	float startWeights[NUM_STARTS];
	startWeights[START_PICKUP] = MIX_PICKUP[lo] * (1.f - f) + MIX_PICKUP[lo + 1] * f;
	startWeights[START_DOWNBEAT] = MIX_DOWNBEAT[lo] * (1.f - f) + MIX_DOWNBEAT[lo + 1] * f;
	startWeights[START_AFTER] = MIX_AFTER[lo] * (1.f - f) + MIX_AFTER[lo + 1] * f;
	Lead lead;
	lead.kind = draws.pick(startWeights, NUM_STARTS);
	const int per = slotsPerBeat(c.subdivision);
	const float slot = 1.f / (float) per;
	if (lead.kind == START_PICKUP)
		lead.beats = std::min(PICKUP_MAX_BEATS,
			slot * (1 + (int) (draws.middling() * (float) per * (phraseStart ? 2.f : 0.5f))));
	else if (lead.kind == START_AFTER)
		lead.beats = slot * (1 + (int) (draws.middling() * (float) per));
	return lead;
}


static void fillGroup(const PhraseAsk& ask, const PhraseControls& c, Draws& draws,
		int group, float groupStart, float groupEnd, bool lastGroup, const Lead& lead,
		float reserve, PhrasePattern& out) {
	const int per = slotsPerBeat(c.subdivision);
	const float slot = 1.f / (float) per;

	// THE PAUSE, carved out of the group's own time: the chart's timeline cannot be stretched, so
	// a longer pause means fewer notes rather than a longer phrase.
	// ELIDE: A PHRASE THAT RUNS INTO THE NEXT. A closing cadence's arrival can also be the first
	// note of what follows, with no breath between them, which is what an elision is. Only at a
	// phrase end, only where the chart says a cadence closes, and as a likelihood rather than a
	// rule — see docs/phrase.md.
	const bool closing = ask.cadence == CADENCE_AUTHENTIC || ask.cadence == CADENCE_PLAGAL
		|| ask.cadence == CADENCE_BACKDOOR || ask.cadence == CADENCE_TRITONE;
	const bool elided = lastGroup && closing && draws.chance(c.elide);

	// ONE PAUSE PER BOUNDARY, and a phrase end is ONE boundary.
	//
	// A group's pause is the breath between it and the next group. A phrase's is the breath
	// between phrases. The last group's end IS the phrase boundary, so it takes the phrase pause
	// and not both: charging it both put three beats of silence at the end of every line where
	// the corpus median between phrases is 1.9, and it made PHRASE PAUSE mean "added to the group
	// pause" rather than what its name says.
	float pause = lastGroup ? c.phrasePause : c.pause;
	if (elided)
		pause = 0.f;
	pause *= 0.75f + 0.5f * draws.middling();
	// A BREATH IS NOT MOST OF A GROUP. The pause was allowed to eat everything but one slot, so a
	// short group could be a beat and a half of notes followed by a beat and a quarter of
	// silence. A third of the group is as much as the breath may take; what is left is music.
	pause = std::min(pause, std::max(0.f, (groupEnd - groupStart) * 0.34f));
	// IN HALF BEATS, after the wander and the limit, so the breath ends where the music has a
	// place for the next line to begin. A pause asked for is never rounded away to nothing.
	{
		const float asked = pause;
		pause = std::floor(pause * 2.f + 0.5f) / 2.f;
		if (asked > 0.f && pause <= 0.f && (groupEnd - groupStart) >= 2.f)
			pause = 0.5f;
	}
	// THE NEXT GROUP'S PICKUP COMES AFTER THE BREATH, so the group stops that much earlier. An
	// elided phrase end has no breath and leaves no room: what follows it begins on its bar line.
	if (elided) {
		reserve = 0.f;
		out.nextLead = START_DOWNBEAT;
		out.nextLeadBeats = 0.f;
	}
	// A LATE START COMES OUT OF THE BREATH, NOT ON TOP OF IT: `reserve` is then negative and this
	// group sounds on by the amount the next one starts late, so the silence between them is the
	// pause asked for. It used to be added — a pause of two and a half beats and a start a beat
	// late left three and a half beats of nothing in the middle of a phrase.
	reserve = std::min(reserve, std::max(0.f, (groupEnd - groupStart) * 0.5f - pause));
	float soundsTo = groupEnd - pause - reserve;
	// Where the next group's pickup begins: nothing of this group may sound past it.
	const float reserved = groupEnd - reserve;

	float first = groupStart;
	// THE PHRASE'S OWN PICKUP borrows from the breath at the end of the phrase before, and only
	// as much of it as that phrase left silent. Up to four eighths, two beats: a phrase
	// leading in is longer than a group leading in, since the breath before it is. With no room —
	// the first phrase, or one after a phrase held to its end — it begins on its bar line.
	// NO EARLIER THAN THE GROUP BEFORE HAS FINISHED, and a slot after it. The group before left
	// room for this one's pickup, but a note it had to place late — an arrival moved onto its
	// cadence chord — may have taken some of it, and the pickup then starts later rather than
	// sounding over it.
	float before = 0.f;
	for (int i = 0; i < out.noteCount; i++)
		before = std::max(before, out.notes[i].offset + out.notes[i].duration);
	const float earliestFirst = (group == 0)
		? -std::floor(std::max(0.f, ask.pickupRoom) / slot + 0.001f) * slot
		: (out.noteCount > 0 ? std::ceil((before + slot) / slot - 0.001f) * slot : 0.f);
	int startKind = lead.kind;
	// A PICKUP WITH NO ROOM COMES IN LATE, NOT ON THE BAR LINE: the first phrase after a start or
	// a rewind, which has nothing before it to have left room. A singer who cannot lead in enters
	// after the beat; landing on the downbeat instead made downbeat starts, which pop phrases
	// seldom use, the commonest start.
	if (group == 0 && startKind == START_PICKUP && earliestFirst >= 0.f)
		startKind = START_AFTER;
	if (startKind == START_PICKUP)
		first = groupStart - lead.beats;
	else if (startKind == START_AFTER)
		first = groupStart + (lead.kind == START_AFTER ? lead.beats : slot);
	// SNAPPED TO THE GRID, because what it is clamped against is not on it: a group's sounding end
	// is its span less a pause, and neither is a whole number of slots. Clamping to that end
	// directly put a group's first onset a fraction off the grid, and a fraction is audible as
	// being wrong without being audible as anything in particular.
	const float lastStart = std::floor((soundsTo - slot) / slot) * slot;
	first = std::max(earliestFirst, std::min(first, lastStart));

	// IS THE LAST NOTE HELD? In most phrases of both corpora it is the longest in its group —
	// seven in ten for songs, more than half for jazz — and songs hold more than solos do, which
	// is what HOLD already says. So the tendency is drawn from HOLD rather than being certain:
	// making it certain gave every group a held ending, which sounds like a rule being obeyed.
	const float hold = std::max(0.f, std::min(1.f, c.hold));
	// HOW OFTEN: half the time at nought, and always from the middle up. It was at most nine in
	// ten, so one line in five or so ended on a half-beat note followed by its breath — clipped,
	// to the ear, and with no setting that would stop it. In the pop songs a line ended on its
	// longest note four times in five, and most of the others ran straight on into the next line
	// rather than breathing, which a group here never does.
	const bool holdLast = draws.chance(0.5f + hold);

	// AND DOES IT SOUND THROUGH THE PAUSE? That is a separate question from being the longest
	// note. Only in the upper half of HOLD: below it, HOLD is how long the held ending is and the
	// breath after it stays a breath; above it, the ending also rings on into the breath, more
	// often the higher it is set. Drawn from HOLD for every setting, this ate the breath of a
	// song whose endings are held for three beats and then breathed after for one and a half,
	// which is what Killing Me Softly does line after line.
	//
	// IT WAS NOT SEPARATE, and that is what made every group end in a long held note whatever
	// HOLD was set to: the last note was allowed to run to the group's end — through the pause —
	// on the same draw that decided it was merely the longest. A phrase generator whose breaths
	// are always filled has no breaths.
	const bool fillPause = draws.chance(std::max(0.f, hold - 0.5f) * 2.f);

	// HOW LONG A HELD ENDING IS: one beat at nought, two at one — a little longer than the notes
	// before it, not a long hold. In the pop songs the last note of a line lasted a beat and a half,
	// median, and the note before it an ordinary half beat. The onsets stop that far before the
	// breath, so the held note has the room.
	//
	// IT WAS UP TO THREE BEATS, and at the settings being played the ending was two beats or more:
	// the long note came one note too soon, where an ordinary note belonged, and the line felt as
	// though it had stopped rather than arrived.
	const float held = 1.f + 1.f * hold;
	const bool heldEnding = holdLast || lastGroup;
	const float onsetsTo = heldEnding
		? std::max(first + slot, std::floor((soundsTo - held) / slot) * slot + slot) : soundsTo;

	// HOW IT ENDS: on a strong beat, or off it. ENDING is the mix.
	float endWeights[NUM_ENDS];
	endWeights[END_STRONG] = std::max(0.02f, c.ending);
	endWeights[END_OFFBEAT] = std::max(0.02f, 1.f - c.ending);
	const int endKind = draws.pick(endWeights, NUM_ENDS);

	// THE ONSETS. A slot sounds with a probability from DENSITY and its metric strength, which
	// SYNCOPATION blends toward its inverse. A chord change raises it by ON CHANGES. The group's
	// first slot always sounds, because a group with no notes in it is not a group.
	//
	// THE RHYTHM FOLLOWS THE HARMONY, as far as ON CHANGES asks — measured in thirty pop songs
	// with their chords (research/pop/harmony_rhythm.py):
	//
	// - QUICK CHORDS GET MORE NOTES: a chord of one beat had two notes under it, one of two to
	//   four beats about a note a beat, one held for two bars about six in ten. So DENSITY is
	//   scaled by how long the chord lasts, around a chord of two beats.
	// - A NOTE THAT LANDS ON A CHANGE IS HELD: a beat, median, against half a beat for other
	//   notes. So after one, the slots up to a beat on are left empty.
	// - A FIFTH OF CHANGES ARE ANTICIPATED: a note half a beat early, held across the change,
	//   with nothing on the change itself.
	const float follow = std::max(0.f, std::min(1.f, c.onChanges));
	float antic[16];
	int anticCount = 0;
	if (per >= 2 && follow > 0.f) {
		for (int i = 0; i < ask.changeCount && anticCount < 16; i++) {
			const float ch = ask.changes[i];
			if (ch > first + slot + 0.001f && ch < onsetsTo - slot - 0.001f
					&& draws.chance(0.3f * follow))
				antic[anticCount++] = ch;
		}
	}
	float holdUntil = -1e9f;
	const int firstNote = out.noteCount;
	for (float t = first; t < onsetsTo - 0.001f; t += slot) {
		const float inBar = std::fmod(t, std::max(1.f, ask.barBeats));
		const float m = metricStrength(inBar < 0.f ? inBar + ask.barBeats : inBar, ask.barBeats,
			c.subdivision);
		const float w = (1.f - c.syncopation) * m + c.syncopation * (1.f - m);
		float p = c.density * (0.4f + 1.2f * w);
		const float chordLength = chordLengthAt(ask, t);
		if (chordLength > 0.f) {
			const float f = std::max(0.5f, std::min(1.5f, std::sqrt(2.f / chordLength)));
			p *= 1.f + follow * (f - 1.f);
		}
		const bool onChange = nearChange(ask, t, slot * 0.51f);
		if (onChange)
			p += c.onChanges * (1.f - p);
		bool anticipates = false, anticipated = false;
		for (int i = 0; i < anticCount; i++) {
			anticipates = anticipates || std::fabs(t - (antic[i] - slot)) < 0.001f;
			anticipated = anticipated || std::fabs(t - antic[i]) < 0.001f;
		}
		// THE HELD ENDING BEGINS WHERE IT WAS PLANNED. Left to DENSITY, the slots before it could
		// all be skipped, and the note before them then held for five or six beats.
		const bool plannedEnd = heldEnding && t >= onsetsTo - slot - 0.001f;
		const bool held = t < holdUntil - 0.001f || anticipated;
		const bool sound = (t <= first + 0.001f) || plannedEnd || anticipates
			|| (!held && draws.chance(std::min(1.f, p)));
		if (!sound)
			continue;
		if (anticipates)
			holdUntil = t + 2.f * slot;
		// Only on a chord long enough to hold into: over a chord of a beat the melody keeps moving,
		// and holding there took the notes away from exactly the quick chords that have the most.
		else if (onChange && chordLength >= 2.f - 0.001f && draws.chance(0.8f * follow))
			holdUntil = t + 1.f;
		if (out.noteCount >= PhrasePattern::MAX_NOTES) {
			out.full = true;
			break;
		}
		PhraseNote& n = out.notes[out.noteCount++];
		n.offset = t;
		n.duration = slot;
		n.level = std::min(1.f, std::max(0.05f,
			0.55f + (w - 0.5f) * c.dynamics + (onChange ? 0.08f : 0.f)));
		n.onChange = onChange;
		n.group = group;
	}

	// NO HOLE INSIDE A GROUP MAY BE AS LONG AS THE BREATH THAT ENDS IT — see fillHoles.
	fillHoles(ask, c, out, group, pause);

	// THE LAST NOTE, MOVED TO THE ENDING THAT WAS DRAWN. A strong ending lands on the nearest
	// beat, an off-beat ending between two — which is what jazz phrases do six times in ten.
	if (out.noteCount > firstNote) {
		PhraseNote& last = out.notes[out.noteCount - 1];
		const float earliest = (out.noteCount - 1 > firstNote)
			? out.notes[out.noteCount - 2].offset + slot : first;
		float want = last.offset;
		if (endKind == END_STRONG)
			want = std::round(last.offset);
		else if (std::fabs(last.offset - std::round(last.offset)) < 0.001f)
			want = last.offset + (per > 1 ? slot : 0.f);
		// CLAMPED TO A SLOT, not to a fraction of one. It was clamped to half a slot before the
		// group's end, which is not a position on the grid: a group whose last note wanted to
		// land past its end got an onset a quarter of a beat in from it, off the grid the whole
		// phrase is built on. The last slot that fits inside the sounding part is what was meant.
		float lastSlot = std::floor((soundsTo - slot * 0.5f) / slot) * slot;
		// A NOTE THAT IS MEANT TO BE THE LONGEST NEEDS ROOM TO BE IT. Left to fall wherever the
		// ending drew it, the last onset often sat a slot from the group's end, so the note that
		// was supposed to be held was the shortest in the group — which is how the corpora's
		// figure of seven in ten came out at four.
		// AND A PHRASE'S LAST NOTE ALWAYS NEEDS IT, held or not: an arrival that begins a slot
		// before the phrase stops sounding is not an arrival, it is a clipped note followed by
		// silence.
		if (heldEnding) {
			const float roomy = std::floor((soundsTo - held) / slot) * slot;
			if (roomy >= earliest)
				lastSlot = std::min(lastSlot, roomy);
		}
		want = std::max(earliest, std::min(want, lastSlot));

		// THE ARRIVAL LANDS ON THE CADENCE CHORD. In the last group of a phrase that ends at a
		// cadence, the last note may not fall before the phrase's last chord change: the chord it
		// arrives on IS the cadence, and a final note sounding over the chord before it — the
		// tonic sung over the dominant — is an ending in the wrong place. Only where the change
		// lies inside what the group plays, and on the grid.
		//
		// THE BREATH GIVES WAY TO IT. Where the chord arrives after the phrase has already begun
		// to breathe — a cadence on the third beat of the last bar, with two beats of breath
		// asked for — the breath is shortened so the arrival can sound on the chord, for at least
		// a beat, with at least a slot of silence after it. An ending in the right place with a
		// shorter breath is an ending; an ending before the chord it resolves to is not.
		if (lastGroup && ask.cadence != 0 && ask.changes && ask.changeCount > 0) {
			const float change = ask.changes[ask.changeCount - 1];
			if (change > want) {
				const float onGrid = std::ceil(change / slot - 0.001f) * slot;
				if (onGrid <= lastSlot + 0.001f) {
					want = onGrid;
				}
				else if (onGrid <= groupEnd - 2.f * slot + 0.001f) {
					want = onGrid;
					soundsTo = std::min(groupEnd - slot, std::max(soundsTo, onGrid + 1.f));
					pause = groupEnd - soundsTo;
				}
			}
		}
		last.offset = want;
	}

	// LENGTHS. A note lasts until the next onset, scaled by LENGTH. The last note of the group is
	// held into the pause by HOLD: at nought it is short and the pause is silence, at one it
	// sustains through it, which is how a singer pauses without stopping the sound.
	for (int i = firstNote; i < out.noteCount; i++) {
		const bool isLast = (i + 1 == out.noteCount);
		const float until = isLast ? soundsTo : out.notes[i + 1].offset;
		float room = std::max(slot * 0.25f, until - out.notes[i].offset);
		float d = room * (0.25f + 0.75f * c.length);

		// A LONG NOTE IS AN ARRIVAL, NOT AN ACCIDENT OF WHERE THE NEXT ONSET FELL.
		//
		// A note's length was simply its gap to the next onset, scaled. So a note in the middle
		// of a line that happened to have two beats of space after it became a two-beat note,
		// and the ear hears that as an ending — in the middle of a phrase, for no reason. Length
		// then had nothing to do with the phrasing, which is exactly what it sounded like.
		//
		// So a note inside a group is capped: it may reach the next onset, but not sprawl. What
		// is left long is the note at a group's end, and longest of all the note a phrase ends
		// on, which is where a held note means something.
		if (!isLast)
			d = std::min(d, slot * (1.f + 2.f * std::max(0.f, std::min(1.f, c.length))));
		if (isLast && holdLast) {
			// LONGEST BY CONSTRUCTION when the draw asked for a held ending, so the measured share
			// matches the chance that was drawn. Left to arithmetic it came out at 35 per cent
			// against the 56 the jazz solos show, because a last note with little room before the
			// pause can be shorter than an interior note that had a rest after it.
			float longest = 0.f;
			for (int k = firstNote; k < out.noteCount - 1; k++)
				longest = std::max(longest, out.notes[k].duration);
			d = std::max(room + (fillPause ? pause * hold : 0.f), longest + slot * 0.25f);
		}
		// A HELD NOTE MAY REACH INTO THE PAUSE, WHICH IS WHAT HOLDING IS, BUT NOT PAST THE GROUP.
		// Making the last note the longest by construction let it run over the group's end, and in
		// the final group that is past the end of the phrase — caught by the census as one note
		// outside its phrase in twenty thousand.
		// THE NOTE A PHRASE ENDS ON IS THE ARRIVAL, and it always sounds to the end of what the
		// phrase plays rather than stopping short and leaving a gap before the breath. This is
		// the one note in a phrase a listener waits for.
		if (isLast && lastGroup) {
			d = std::max(d, room);
			if (fillPause)
				d = std::max(d, room + pause * hold);
		}

		// HOW FAR THE LAST NOTE MAY RING.
		//
		// To where the group stops sounding, so the breath after it is real — unless the pause is
		// being filled, when it may run to the group's end, which is what filling a pause means.
		//
		// A PHRASE'S ARRIVAL GETS A LITTLE MORE THAN THAT. Where the last onset falls close to the
		// end there is no room left for an arrival, and a clipped note followed by silence is not
		// an ending. So at a phrase end the note may ring into half the breath if it needs to; the
		// breath is shortened rather than lost.
		if (isLast) {
			float ceiling = soundsTo;
			if (fillPause)
				ceiling = reserved;
			else if (lastGroup)
				ceiling = std::min(reserved, soundsTo + pause * 0.5f);
			float want = ceiling - out.notes[i].offset;
			// NEVER CLIPPED TO NOTHING by the room kept for the next group's pickup. A last note
			// placed late — onto the cadence chord, or the last slot there was — keeps at least a
			// slot, and an arrival at least a beat, within its own group; the pickup after it is
			// what gives way.
			const float least = std::min(lastGroup ? 1.f : slot, groupEnd - out.notes[i].offset);
			want = std::max(want, least);
			d = std::max(d, least);
			if (lastGroup && !fillPause && d < std::min(1.f, want))
				d = std::min(1.f, want);
			d = std::min(d, want);
		}
		out.notes[i].duration = std::max(slot * 0.25f, d);
	}

	if (group < PhrasePattern::MAX_GROUPS) {
		out.starts[group] = groupStart;
		out.soundsTo[group] = soundsTo;
		out.pauses[group] = pause;
		out.startKind[group] = startKind;
		out.endKind[group] = endKind;
	}
}

// ---- the phrase -------------------------------------------------------------------------------

/** THE MIDDLE UNIT OF THE TRIPLET, which is the whole of what a triplet figure adds.

A swung eighth-note pair is already the first and third unit of a triplet. So a triplet figure is
not another grid: it is the line also sounding the unit in between. That is why this is one
amount rather than a second subdivision setting, and why it works the same whether or not the
music is swinging.

A BEAT AT A TIME, because that is how they happen. Of the runs of consecutive beats carrying one
in the jazz solos, 82 per cent were a single beat, 13 per cent two and 3 per cent three — a figure
that happens, not a mode a line goes into. A beat chosen here has its notes replaced by three
even ones, marked so the swing deformation leaves them alone.
*/
static void addTriplets(const PhraseAsk& ask, const PhraseControls& c, Draws& draws,
		PhrasePattern& out) {
	const float share = std::max(0.f, std::min(1.f, c.triplets));
	if (share <= 0.f || out.noteCount <= 0)
		return;

	// Which beats carry a note at all: a triplet figure replaces notes, it does not invent a beat.
	for (float beat = 0.f; beat < ask.phraseBeats - 0.001f; beat += 1.f) {
		int first = -1, count = 0;
		for (int i = 0; i < out.noteCount; i++) {
			if (out.notes[i].offset >= beat - 0.001f && out.notes[i].offset < beat + 1.f - 0.001f) {
				if (first < 0)
					first = i;
				count++;
			}
		}
		if (first < 0 || count < 1)
			continue;
		if (!draws.chance(share))
			continue;
		// NEVER OVER AN ENDING OR A HELD NOTE. A triplet figure replaces the notes of a beat, and
		// it used to replace whatever was there — so when the beat held a line's last note, the
		// held ending became three notes of a third of a beat each, and a phrase ended on a note
		// too short to be an ending. A figure fills a beat of moving notes, never a note being held.
		bool held = false;
		for (int i = first; i < first + count; i++) {
			const bool groupLast = (i + 1 == out.noteCount) || out.notes[i + 1].group != out.notes[i].group;
			if (groupLast || out.notes[i].duration > 1.f - 0.01f)
				held = true;
		}
		if (held)
			continue;

		// THE THREE UNITS, evenly spaced, taking the level and the group of what was there.
		const PhraseNote model = out.notes[first];
		const int after = first + count;
		const int room = out.noteCount - after;
		if (first + 3 + room > PhrasePattern::MAX_NOTES) {
			out.full = true;
			continue;
		}
		for (int k = room - 1; k >= 0; k--)
			out.notes[first + 3 + k] = out.notes[after + k];
		for (int k = 0; k < 3; k++) {
			PhraseNote& n = out.notes[first + k];
			n = model;
			n.offset = beat + (float) k / 3.f;
			n.duration = 1.f / 3.f;
			n.triplet = true;
			n.onChange = (k == 0) ? model.onChange : false;
			n.level = model.level * (k == 0 ? 1.f : 0.9f);
		}
		out.noteCount = first + 3 + room;

		// THE NOTE BEFORE IT STOPS AT THE FIGURE. A long note from the previous beat used to end
		// where the note it replaced began, and the three even ones start earlier than that.
		if (first > 0) {
			PhraseNote& prev = out.notes[first - 1];
			const float room2 = out.notes[first].offset - prev.offset;
			if (prev.duration > room2)
				prev.duration = std::max(0.01f, room2);
		}
	}
}


/** SWING, APPLIED AFTER THE NOTES ARE PLACED and never before.

Placement stays on an even grid so that the metre, the chord changes and the sub-phrase
boundaries are all still reasoned about in plain beats. This is the last step: it stretches the
first half of each divided beat and shortens the second, which is what a swung line is.

MAPPED, NOT NUDGED. Both the onset and the end of a note go through the same mapping, so a note
that reached the next onset still reaches it and a gap stays a gap. A nudge applied to onsets
alone would lengthen every second note and shorten every other.

THE LEVEL IT ACTS ON depends on the grid the phrase was filled at: eighths swing at the ratio
published for eighths, sixteenths at their own much flatter one, and a quarter-note or triplet
grid does not swing at all. */
static float swung(float t, float unit, float ratio) {
	const float pair = unit * 2.f;
	const float which = std::floor(t / pair);
	const float local = t - which * pair;
	const float firstPart = pair * ratio / (1.f + ratio);
	const float mapped = (local <= unit)
		? local / unit * firstPart
		: firstPart + (local - unit) / unit * (pair - firstPart);
	return which * pair + mapped;
}

static void applySwing(const PhraseAsk& ask, const PhraseControls& c, PhrasePattern& out) {
	float unit = 0.f, ratio = 1.f;
	if (c.subdivision == SUB_EIGHTH) {
		unit = 0.5f;
		ratio = ask.swingEighth;
	}
	else if (c.subdivision == SUB_SIXTEENTH) {
		unit = 0.25f;
		ratio = ask.swingSixteenth;
	}
	if (unit <= 0.f || ratio <= 1.001f)
		return;

	for (int i = 0; i < out.noteCount; i++) {
		PhraseNote& n = out.notes[i];
		if (n.triplet)
			continue;
		const float end = n.offset + n.duration;
		const float a = swung(n.offset, unit, ratio);
		const float b = swung(end, unit, ratio);
		n.offset = a;
		n.duration = std::max(0.01f, b - a);
	}
	// AND NOTHING RUNS THROUGH WHAT FOLLOWS IT. A triplet beat is exempt from a mapping that its
	// neighbours go through, which is the one place this stops being monotonic: a note ending
	// inside such a beat is stretched while the beat's own notes stay where they are.
	for (int i = 0; i + 1 < out.noteCount; i++) {
		PhraseNote& n = out.notes[i];
		const float room = out.notes[i + 1].offset - n.offset;
		if (room > 0.f && n.duration > room)
			n.duration = std::max(0.01f, room);
	}

	// THE GROUP BOUNDARIES MOVE WITH THE NOTES, or a census — and anything downstream reading
	// where a group sounds to — would be comparing swung notes against even boundaries.
	for (int g = 0; g < out.groupCount && g < PhrasePattern::MAX_GROUPS; g++) {
		out.starts[g] = swung(out.starts[g], unit, ratio);
		out.soundsTo[g] = swung(out.soundsTo[g], unit, ratio);
	}
}


float phrasePickupRoom(const PhrasePattern& ending, float endingBeats, int subdivision) {
	if (ending.silent || ending.noteCount <= 0)
		return 0.f;
	float stops = 0.f;
	for (int i = 0; i < ending.noteCount; i++)
		stops = std::max(stops, ending.notes[i].offset + ending.notes[i].duration);
	const float slot = 1.f / (float) slotsPerBeat(subdivision);
	return std::max(0.f, std::min(PICKUP_MAX_BEATS, endingBeats - stops - slot));
}

/** THE LOUDNESS OF EVERY NOTE, set last, from four things in decibels — see docs/phrase.md.

THE PHRASE'S OWN LEVEL. The second phrase of each pair answers a little stronger than the first;
a contrasting section, a chorus or a bridge, is louder than the first section; the music builds a
little across each pass of the form; and each phrase wanders by a draw of its own. The draw is
keyed on the phrase's place in the cycle, so a phrase that comes round comes round as loud.

THE SHAPE ACROSS IT, by SHAPE: falling, or an arch peaking two fifths of the way through.

STRESS. In thirty pop songs a stressed syllable was the long note and the note on a strong beat,
and a quarter of them were anticipations — the eighth before a strong beat, held across it. So a
note of a beat or more and a note anticipating a beat are accented, a note on a strong beat a
little, and the metric pulse under it is light: the beat accent this replaced was six decibels
deep, where the jazz solos put a note on the beat a third of a decibel above one off it.

THE ARRIVAL TAPERS, a little. The last note of a phrase is softer by a decibel: in the jazz solos
it is softer by two and a half, but a pop phrase often ends on its longest note, and taken further
that note was lost.

DYNAMICS scales all of it: at 0.5 the numbers below are as written, at one twice as deep.

DEEPER THAN THE CORPUS AVERAGE, on purpose. The jazz solos' fall is three decibels from start to
end on average over eleven thousand phrases, and an average is flatter than any one phrase in it;
at that depth, heard through an envelope, nobody could hear a phrase rise and fall at all. The
shape here swings six decibels at 0.5 and twelve at one, above the middle; below it, half that.
The middle is half of full level, six decibels down, which leaves the louder phrases room. */
static const float LEVEL_MIDDLE = 0.5f;
/** The quietest a note is ever given: a fifth of full, fourteen decibels down. */
static const float LEVEL_FLOOR = 0.2f;

static void applyDynamics(const PhraseAsk& ask, const PhraseControls& c, PhrasePattern& out) {
	if (out.noteCount <= 0)
		return;
	const float depth = 2.f * std::max(0.f, std::min(1.f, c.dynamics));
	const float s = std::max(0.f, std::min(1.f, c.shape));

	float phraseDb = 0.f;
	if (ask.phraseInSection % 2 == 1)
		phraseDb += 1.5f;
	if (ask.contrasting)
		phraseDb += 2.f;
	phraseDb += 2.f * std::max(0.f, std::min(1.f, ask.intoForm));
	const uint32_t h = hash32(ask.seed * 0x9e3779b9u + (uint32_t) ask.cyclePosition * 7919u + 811u);
	phraseDb += ((float) (h >> 8) / 16777216.f - 0.5f) * 3.f;

	const float from = out.notes[0].offset;
	const float span = std::max(0.5f, out.notes[out.noteCount - 1].offset - from);
	for (int i = 0; i < out.noteCount; i++) {
		PhraseNote& n = out.notes[i];
		const float x = std::max(0.f, std::min(1.f, (n.offset - from) / span));
		const float fall = 1.f - 2.f * x;
		const float d = (x < 0.4f) ? (x - 0.4f) / 0.4f : (x - 0.4f) / 0.6f;
		const float arch = 1.f - 2.f * d * d;
		const float shapeDb = 3.f * ((1.f - s) * fall + s * arch);

		float inBar = std::fmod(n.offset, std::max(1.f, ask.barBeats));
		if (inBar < 0.f)
			inBar += std::max(1.f, ask.barBeats);
		const float m = metricStrength(inBar, ask.barBeats, c.subdivision);
		const float metricDb = (m - 0.5f) * 1.5f;

		const float frac = n.offset - std::floor(n.offset);
		const bool anticipation = frac > 0.01f
			&& n.offset + n.duration > std::floor(n.offset) + 1.f + 0.01f;
		const float stressDb = (n.duration >= 1.f - 0.01f || anticipation) ? 1.5f : 0.f;

		const float arrivalDb = (i + 1 == out.noteCount) ? -1.f : 0.f;
		const float changeDb = n.onChange ? 0.5f : 0.f;

		float db = depth * (phraseDb + shapeDb + metricDb + stressDb + arrivalDb + changeDb);
		// HALF AS DEEP BELOW THE MIDDLE AS ABOVE IT. The swing is there to be heard rising; taken
		// the full depth down, the end of a falling phrase, tapered again as an arrival, sank to a
		// tenth of full level at deep settings and was lost.
		if (db < 0.f)
			db *= 0.5f;
		const float lin = LEVEL_MIDDLE * std::pow(10.f, db / 20.f);
		// A SOFT CEILING rather than a hard one: above the middle the level bends towards full,
		// so the louder phrases keep getting louder instead of all flattening at the same top.
		const float top = (lin <= LEVEL_MIDDLE) ? lin
			: LEVEL_MIDDLE + (1.f - LEVEL_MIDDLE) * std::tanh((lin - LEVEL_MIDDLE) / (1.f - LEVEL_MIDDLE));
		n.level = std::min(1.f, std::max(LEVEL_FLOOR, top));
	}
}

uint32_t phraseHash(uint32_t x) {
	return hash32(x);
}


/** RESTATING THE PHRASE BEFORE, slot by slot.

WHAT REPEAT COPIES is the decision at each slot: whether it sounded, and if it did, how long and
how hard. So at one the onsets are the earlier phrase's exactly, at nought none of them are, and
between the two a phrase is partly the same idea — which is what a restatement with a change in
it actually is.

THE LAST GROUP IS ALWAYS THE NEW ONE. A phrase ends where its own cadence is, and a phrase that
copied its predecessor's ending would arrive in the wrong place as often as not.

SLOTS, NOT NOTES. Two phrases have different group boundaries and different numbers of notes, so
there is no note-to-note correspondence to work from; there is a grid, and both are on it. */
static void applyRepeat(const PhraseAsk& ask, const PhraseControls& c, Draws& draws,
		PhrasePattern& out) {
	const PhrasePattern* prev = ask.previous;
	if (!prev || prev->rawCount <= 0 || c.repeat <= 0.f)
		return;

	const int per = slotsPerBeat(c.subdivision);
	const float slot = 1.f / (float) per;
	const int slots = (int) std::floor(ask.phraseBeats * per + 0.5f);
	if (slots <= 0 || slots > PhrasePattern::MAX_NOTES * 4)
		return;

	// Where the last group begins: everything from there is the new phrase's own.
	const float keepFrom = (out.groupCount > 0) ? out.starts[out.groupCount - 1] : ask.phraseBeats;

	std::vector<const PhraseNote*> mine(slots, (const PhraseNote*) NULL);
	std::vector<const PhraseNote*> theirs(slots, (const PhraseNote*) NULL);
	for (int i = 0; i < out.noteCount; i++) {
		const int k = (int) std::floor(out.notes[i].offset * per + 0.5f);
		if (k >= 0 && k < slots)
			mine[k] = &out.notes[i];
	}
	for (int i = 0; i < prev->rawCount; i++) {
		const int k = (int) std::floor(prev->raw[i].offset * per + 0.5f);
		if (k >= 0 && k < slots)
			theirs[k] = &prev->raw[i];
	}

	// EACH GROUP'S ENDING IS ITS OWN TOO: from its last note of its own to its breath, nothing is
	// copied in. A slot copied there landed inside the held ending and cut it to the length of
	// the gap before the copy, so a line ended on a short note followed by its breath.
	float endingFrom[PhrasePattern::MAX_GROUPS];
	for (int g = 0; g < PhrasePattern::MAX_GROUPS; g++)
		endingFrom[g] = 1e9f;
	for (int i = 0; i < out.noteCount; i++) {
		const int g = out.notes[i].group;
		if (g >= 0 && g < PhrasePattern::MAX_GROUPS
				&& (i + 1 == out.noteCount || out.notes[i + 1].group != g))
			endingFrom[g] = out.notes[i].offset;
	}

	// A PICKUP IS THIS PHRASE'S OWN, like its last group: it was placed in the breath the phrase
	// before actually left, which the phrase being restated knew nothing about.
	PhraseNote built[PhrasePattern::MAX_NOTES];
	int count = 0;
	for (int i = 0; i < out.noteCount && out.notes[i].offset < -0.001f; i++)
		built[count++] = out.notes[i];
	for (int k = 0; k < slots && count < PhrasePattern::MAX_NOTES; k++) {
		const float at = (float) k * slot;
		const bool ownGround = at >= keepFrom - 0.001f;
		// A BREATH IS THIS PHRASE'S, NOT THE ONE BEING RESTATED. Copying slot by slot across the
		// whole phrase put the earlier phrase's notes into this one's pauses, so a group ran
		// straight into the next with a tenth of a beat between them — the breath restated out
		// of existence. What is restated is the notes; where this phrase breathes is its own.
		bool inBreath = false;
		for (int g = 0; g < out.groupCount && g < PhrasePattern::MAX_GROUPS; g++) {
			const float next = (g + 1 < out.groupCount) ? out.starts[g + 1] : ask.phraseBeats;
			if (at >= out.soundsTo[g] - 0.001f && at < next - 0.001f)
				inBreath = true;
		}
		bool inEnding = false;
		for (int g = 0; g < out.groupCount && g < PhrasePattern::MAX_GROUPS; g++) {
			const float next = (g + 1 < out.groupCount) ? out.starts[g + 1] : ask.phraseBeats;
			if (at >= endingFrom[g] - 0.001f && at < next - 0.001f)
				inEnding = true;
		}
		const bool copy = !ownGround && !inBreath && !inEnding && draws.chance(c.repeat);
		const PhraseNote* take = copy ? theirs[k] : mine[k];
		if (copy)
			out.restated++;
		if (!take)
			continue;
		built[count] = *take;
		built[count].offset = at;
		// A NOTE OF THIS PHRASE'S OWN KEEPS ITS GROUP. A pickup starts in the breath before the
		// group it leads into and belongs to that group — reassigning it by position put it in the
		// group BEFORE, where it made a hole of three and a half beats that the filling then
		// closed with notes, and the breath between the two groups vanished.
		//
		// A COPIED NOTE belongs to the group it lands in here, or the pauses and the census would
		// be reading the old phrase's boundaries.
		if (take != mine[k]) {
			int g = 0;
			for (int gi = 0; gi < out.groupCount && gi < PhrasePattern::MAX_GROUPS; gi++)
				if (at >= out.starts[gi] - 0.001f)
					g = gi;
			built[count].group = g;
		}
		count++;
	}

	// NOTHING RUNS PAST WHAT FOLLOWS IT, since a copied note's length came from a phrase whose
	// next note may have been somewhere else.
	for (int i = 0; i < count; i++) {
		const float limit = (i + 1 < count) ? built[i + 1].offset - built[i].offset
			: ask.phraseBeats - built[i].offset;
		if (built[i].duration > limit)
			built[i].duration = std::max(0.01f, limit);
	}

	for (int i = 0; i < count; i++)
		out.notes[i] = built[i];
	out.noteCount = count;
}


/** MOTIF: A GROUP RESTATES THE RHYTHM OF ONE BEFORE IT.

WHAT IS COPIED is the source group's notes up to its ending — onsets, lengths, loudness — moved
by the whole number of beats between the two groups' starts, so each note keeps its place in the
beat. The copy runs from the start of the group until it reaches the group's own ending, or runs
out; from there the group carries on with its own notes. So a figure comes back and the line then
goes somewhere of its own, which is what a restated line does in the pop transcriptions.

THE ENDING AND THE BREATH ARE THIS GROUP'S. A group's ending is placed for its own cadence and a
breath for its own length, as REPEAT keeps them across phrases. And a copied note may not start
before the group before this one has finished sounding.

EACH COPY SAYS WHAT IT COPIES (echoOf), so a melody can bring back the pitches as well: see
Event::echo. */
static void applyMotif(const PhraseAsk& ask, const PhraseControls& c, Draws& draws,
		PhrasePattern& out) {
	for (int i = 0; i < out.noteCount; i++) {
		out.notes[i].id = i;
		out.notes[i].echoOf = -1;
	}
	if (c.motif <= 0.f)
		return;
	const float slot = 1.f / (float) slotsPerBeat(c.subdivision);
	// A COPY IS A NEW NOTE, with a name of its own, so that a note restating it later finds it
	// and not its original.
	int nextId = 10000;

	// THE LINES BEFORE THIS ONE, IN ORDER: the phrase before's groups, then this phrase's. In the
	// pop songs a restated line restates the line just before it half the time, the one before
	// that three times in ten and the one before that twice — and at a ballad's tempo a phrase is
	// often one line, so the line before is usually in the phrase before.
	const PhrasePattern* prev = (ask.previous && !ask.previous->silent) ? ask.previous : NULL;
	const int prevGroups = prev ? std::min(prev->groupCount, (int) PhrasePattern::MAX_GROUPS) : 0;

	for (int g = 0; g < out.groupCount && g < PhrasePattern::MAX_GROUPS; g++) {
		// Drawn for every group whatever the knob says, so turning MOTIF does not move the draws
		// of the groups after this one.
		const bool restate = draws.chance(c.motif);
		const float which = draws.next();
		if (!restate)
			continue;
		// NO FURTHER BACK THAN THERE ARE LINES: the furthest there is, instead.
		const int back = std::min(prevGroups + g, which < 0.5f ? 1 : which < 0.8f ? 2 : 3);
		const int line = prevGroups + g - back;
		if (line < 0)
			continue;
		const bool fromPrev = line < prevGroups;
		const int s = fromPrev ? line : line - prevGroups;
		const PhraseNote* src = fromPrev ? prev->raw : out.notes;
		const int srcCount = fromPrev ? prev->rawCount : out.noteCount;
		const float srcStart = fromPrev ? prev->starts[s] : out.starts[s];

		// THE SOURCE: its notes before its ending note.
		int srcFirst = -1, srcLast = -1;
		for (int i = 0; i < srcCount; i++)
			if (src[i].group == s) {
				if (srcFirst < 0)
					srcFirst = i;
				srcLast = i;
			}
		// THIS GROUP: its first note and its ending note.
		int dstFirst = -1, dstLast = -1;
		for (int i = 0; i < out.noteCount; i++)
			if (out.notes[i].group == g) {
				if (dstFirst < 0)
					dstFirst = i;
				dstLast = i;
			}
		if (srcFirst < 0 || srcLast - srcFirst < 2 || dstFirst < 0 || dstLast <= dstFirst)
			continue;

		// BY WHOLE BEATS, so every note keeps its place in the beat. Phrases are whole beats
		// long, so a group of the phrase before is measured from its own phrase's start as well.
		const float shift = std::round(out.starts[g] - srcStart);
		const float earliest = (g > 0 ? out.soundsTo[g - 1] + slot : out.notes[dstFirst].offset);
		const float before = out.notes[dstLast].offset - slot + 0.001f;

		PhraseNote copy[PhrasePattern::MAX_NOTES];
		int copies = 0;
		for (int i = srcFirst; i < srcLast; i++) {
			PhraseNote n = src[i];
			n.offset += shift;
			if (n.offset < earliest - 0.001f || n.offset >= before)
				continue;
			n.group = g;
			n.echoOf = src[i].id;
			n.id = nextId++;
			n.echoPrev = fromPrev;
			n.triplet = false;
			n.onChange = nearChange(ask, n.offset, slot * 0.51f);
			n.arrival = n.groupEnd = n.approach = false;
			copy[copies++] = n;
		}
		if (copies < 2)
			continue;
		const float copyEnd = copy[copies - 1].offset + std::max(slot, copy[copies - 1].duration);

		// THE GROUP REBUILT: what comes before it, the copy, its own notes after the copy, its
		// ending, and everything after it.
		PhraseNote built[PhrasePattern::MAX_NOTES];
		int count = 0;
		for (int i = 0; i < out.noteCount && count < PhrasePattern::MAX_NOTES; i++) {
			const PhraseNote& n = out.notes[i];
			if (n.group == g && i != dstLast && n.offset >= earliest - 0.001f
					&& n.offset < copyEnd - 0.001f)
				continue;
			built[count++] = n;
		}
		// The copy goes in where its onsets say.
		PhraseNote merged[PhrasePattern::MAX_NOTES];
		int m = 0, a = 0, k = 0;
		while ((a < count || k < copies) && m < PhrasePattern::MAX_NOTES) {
			if (k < copies && (a >= count || copy[k].offset < built[a].offset))
				merged[m++] = copy[k++];
			else
				merged[m++] = built[a++];
		}
		// Nothing runs past what follows it.
		for (int i = 0; i + 1 < m; i++) {
			const float room = merged[i + 1].offset - merged[i].offset;
			if (merged[i].duration > room)
				merged[i].duration = std::max(0.01f, room);
		}
		for (int i = 0; i < m; i++)
			out.notes[i] = merged[i];
		out.noteCount = m;
	}
}

/** HOW MANY NOTES BACK EACH RESTATED NOTE'S ORIGINAL IS, counted in the notes as played, which
is how a melody receiving them one at a time can find it. A note split into a triplet figure keeps
the echo on its first part only. */
static void countEchoes(const PhraseAsk& ask, PhrasePattern& out) {
	for (int i = 0; i < out.noteCount; i++) {
		PhraseNote& n = out.notes[i];
		n.echo = 0;
		if (n.echoOf < 0 || (i > 0 && out.notes[i - 1].id == n.id))
			continue;
		// From the phrase before, counted through the rest of that phrase as it was played.
		const PhraseNote* in = n.echoPrev ? (ask.previous ? ask.previous->notes : NULL) : out.notes;
		const int count = n.echoPrev ? (ask.previous ? ask.previous->noteCount : 0) : i;
		for (int j = 0; in && j < count; j++)
			if (in[j].id == n.echoOf) {
				const int back = n.echoPrev ? i + count - j : i - j;
				if (back <= 255)
					n.echo = back;
				break;
			}
	}
}


void phraseGenerate(const PhraseAsk& ask, const PhraseControls& asked, PhrasePattern& out) {
	// THE PAUSES IN HALF BEATS AND WITHIN A BREATH: two beats at most between the lines of a
	// phrase, a bar between phrases. Here as well as on the knobs, so every caller — the module,
	// the simulation, an old patch — gets the same breaths. See PAUSE_MOST.
	PhraseControls c = asked;
	c.pause = std::max(0.f, std::min(GROUP_PAUSE_MOST, std::round(c.pause * 2.f) / 2.f));
	c.phrasePause = std::max(0.f, std::min(PHRASE_PAUSE_MOST,
		std::round(c.phrasePause * 2.f) / 2.f));
	out = PhrasePattern();
	if (!(ask.phraseBeats > 0.f) || !(ask.beatsPerSecond > 0.f))
		return;

	Draws draws;
	// THE SAME SEED AND THE SAME PLACE IN THE CYCLE ALWAYS DECIDE ALIKE. That is what makes a
	// passage recur: the cycle's position, not the pass count, is what the draws depend on.
	draws.base = hash32(ask.seed * 2654435761u + (uint32_t) ask.cyclePosition * 40503u + 17u);
	draws.variation = std::max(0.f, std::min(1.f, c.variation));

	if (draws.chance(c.silentPhrases)) {
		out.silent = true;
		return;
	}

	float starts[PhrasePattern::MAX_GROUPS + 1];
	const int groups = divide(ask, c, draws, starts, PhrasePattern::MAX_GROUPS);
	out.groupCount = groups;

	// HOW EVERY GROUP STARTS, AND HOW THE NEXT PHRASE DOES, drawn before any group is filled, so
	// each group can leave room for the pickup of the one after it — see drawLead. From a draw of
	// their own, so that deciding them here does not move every other draw in the phrase. The
	// first group's start was drawn by the phrase before, which left room for it; with no phrase
	// before, it is drawn here.
	Draws leads;
	leads.base = hash32(draws.base ^ 0x5bd1e995u);
	leads.variation = draws.variation;
	Lead lead[PhrasePattern::MAX_GROUPS];
	for (int g = 0; g < groups; g++)
		lead[g] = drawLead(c, leads, g == 0);
	if (ask.leadKind >= 0) {
		lead[0].kind = ask.leadKind;
		lead[0].beats = ask.leadBeats;
	}
	const Lead after = drawLead(c, leads, true);
	out.nextLead = after.kind;
	out.nextLeadBeats = after.kind == START_PICKUP || after.kind == START_AFTER ? after.beats : 0.f;

	for (int g = 0; g < groups; g++) {
		const bool last = g + 1 == groups;
		const float groupEnd = !last ? starts[g + 1] : ask.phraseBeats;
		const Lead& following = !last ? lead[g + 1] : after;
		float reserve = following.kind == START_PICKUP ? following.beats : 0.f;
		if (!last && following.kind == START_AFTER)
			reserve = -following.beats;
		fillGroup(ask, c, draws, g, starts[g], groupEnd, last, lead[g], reserve, out);
	}

	// RESTATEMENT, then the feel. Repetition works on the notes as they were decided; the feel is
	// applied to what is about to be played, and is not part of the idea being restated.
	applyRepeat(ask, c, draws, out);
	if (ask.previous && c.repeat > 0.f)
		for (int g = 0; g < out.groupCount && g < PhrasePattern::MAX_GROUPS; g++)
			fillHoles(ask, c, out, g, out.pauses[g]);

	// MOTIF, from a draw of its own, so the knob moves nothing else. After the restatement across
	// phrases, which copies slot by slot and would scatter a figure copied before it.
	Draws motifs;
	motifs.base = hash32(draws.base ^ 0x27d4eb2fu);
	motifs.variation = draws.variation;
	applyMotif(ask, c, motifs, out);

	// KEPT AS DECIDED, for the next phrase to restate.
	out.rawCount = out.noteCount;
	for (int i = 0; i < out.noteCount; i++)
		out.raw[i] = out.notes[i];

	// THE FEEL, LAST. Both of these act on notes that already have their places, so nothing above
	// has to know anything about how the beat is divided.
	addTriplets(ask, c, draws, out);
	applyDynamics(ask, c, out);
	applySwing(ask, c, out);
	countEchoes(ask, out);
	for (int i = 0; i < out.noteCount; ) {
		int j = i;
		while (j + 1 < out.noteCount && out.notes[j + 1].group == out.notes[i].group)
			j++;
		const float from = out.notes[i].offset, span = out.notes[j].offset - from;
		for (int k = i; k <= j; k++)
			out.notes[k].along = span > 0.001f ? (out.notes[k].offset - from) / span : 0.f;
		i = j + 1;
	}

	// WHICH NOTES END SOMETHING, named last so they are the notes as played. A melody downstream
	// cannot work this out — it hears notes one at a time, and the breath comes after the last —
	// so the generator that placed them says which is which. See Event::flags.
	for (int i = 0; i < out.noteCount; i++) {
		out.notes[i].arrival = false;
		out.notes[i].approach = false;
		out.notes[i].groupEnd = (i + 1 == out.noteCount)
			|| out.notes[i + 1].group != out.notes[i].group;
	}
	if (out.noteCount > 0) {
		out.notes[out.noteCount - 1].arrival = true;
		if (out.noteCount > 1
			&& out.notes[out.noteCount - 2].group == out.notes[out.noteCount - 1].group)
			out.notes[out.noteCount - 2].approach = true;
	}
}

} // namespace px
