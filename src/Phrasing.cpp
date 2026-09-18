/** See Phrasing.hpp. */
#include "Phrasing.hpp"

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

	/** True with probability p, with VARIATION deciding how much chance is involved at all. At
	nought this is simply whether p is more likely than not. */
	bool chance(float p) {
		if (variation <= 0.f)
			return p >= 0.5f;
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

// ---- the styles -------------------------------------------------------------------------------

void phraseStyle(int style, PhraseControls& out) {
	out = PhraseControls();
	if (style == STYLE_JAZZ) {
		// Starts and endings mostly off the beat, busier and more syncopated, shorter holds —
		// the jazz solos' own figures: 55 per cent of phrases started off the beat and 62 per
		// cent ended there, against 10 and 13 per cent on a bar's downbeat.
		out.start = 1.f;
		out.ending = 0.38f;
		out.density = 0.7f;
		out.syncopation = 0.5f;
		out.hold = 0.15f;
		out.groupSeconds = 2.7f;
		out.length = 0.45f;
	}
	else {
		// The lead sheets: pickups and downbeat starts about equally common, endings mostly on
		// a strong beat, the last note held.
		out.start = 0.f;
		out.ending = 0.62f;
		out.density = 0.5f;
		out.syncopation = 0.2f;
		out.hold = 0.55f;
		out.groupSeconds = 3.f;
		out.length = 0.65f;
	}
}

const char* phraseStyleName(int style) {
	return style == STYLE_JAZZ ? "jazz" : "song";
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
	const float target = sounding + std::max(0.f, c.pause);
	int count = 0;
	float at = 0.f;
	starts[count++] = 0.f;
	while (at < ask.phraseBeats - 0.001f && count < maxGroups) {
		float next = at + target;
		// SNAP TO A BAR LINE when one is close, more often than not.
		const float bars = std::max(1.f, ask.barBeats);
		const float nearestBar = std::round(next / bars) * bars;
		// SNAPPED ONLY WHERE IT DOES NOT ROB THE GROUP. Pulling every boundary to the nearest bar
		// line shortened groups by up to a third and broke the length in seconds that the whole
		// design rests on.
		if (std::fabs(nearestBar - next) <= bars * 0.5f
			&& nearestBar - at >= sounding * 0.8f + c.pause
			&& draws.chance(0.7f))
			next = nearestBar;
		// A tail shorter than half a group joins the group before it rather than standing alone.
		if (ask.phraseBeats - next < target * 0.5f)
			break;
		// VARY: the last part of a phrase fragments into shorter groups.
		if (draws.chance(c.vary * (next / std::max(1.f, ask.phraseBeats))))
			next = at + std::max(1.f, target * 0.5f);
		starts[count++] = next;
		at = next;
	}
	return count;
}

// ---- one group's notes ------------------------------------------------------------------------

static void fillGroup(const PhraseAsk& ask, const PhraseControls& c, Draws& draws,
		int group, float groupStart, float groupEnd, bool lastGroup, PhrasePattern& out) {
	const int per = slotsPerBeat(c.subdivision);
	const float slot = 1.f / (float) per;

	// THE PAUSE, carved out of the group's own time: the chart's timeline cannot be stretched, so
	// a longer pause means fewer notes rather than a longer phrase.
	float pause = c.pause + (lastGroup ? c.phrasePause : 0.f);
	pause *= 0.75f + 0.5f * draws.middling();
	pause = std::min(pause, std::max(0.f, (groupEnd - groupStart) - slot));
	const float soundsTo = groupEnd - pause;

	// HOW IT STARTS. A pickup begins before the group, taking its time from the pause before it;
	// a delayed start begins after. Weights from the START mix, with the middle of the range
	// giving the lead sheets' own proportions.
	// THE THREE POINTS ARE MEASURED, and the knob moves between them. In the middle: the lead
	// sheets, pickups and downbeat starts about equally common. At one: the jazz solos, more than
	// half starting off the beat and one in ten on a downbeat. At minus one: mostly pickups, which
	// no corpus showed but which is a real way to play and is what the end of a range is for.
	static const float MIX_PICKUP[3] = {0.80f, 0.37f, 0.12f};
	static const float MIX_DOWNBEAT[3] = {0.15f, 0.37f, 0.33f};
	static const float MIX_AFTER[3] = {0.05f, 0.26f, 0.55f};
	const float s = std::max(-1.f, std::min(1.f, c.start));
	const int lo = s < 0.f ? 0 : 1;
	const float f = s < 0.f ? s + 1.f : s;
	float startWeights[NUM_STARTS];
	startWeights[START_PICKUP] = MIX_PICKUP[lo] * (1.f - f) + MIX_PICKUP[lo + 1] * f;
	startWeights[START_DOWNBEAT] = MIX_DOWNBEAT[lo] * (1.f - f) + MIX_DOWNBEAT[lo + 1] * f;
	startWeights[START_AFTER] = MIX_AFTER[lo] * (1.f - f) + MIX_AFTER[lo + 1] * f;
	const int startKind = draws.pick(startWeights, NUM_STARTS);

	float first = groupStart;
	if (group == 0 && startKind == START_PICKUP) {
		// The phrase's own first group cannot borrow time from a group before it.
		first = groupStart;
	}
	else if (startKind == START_PICKUP) {
		first = groupStart - slot * (1 + (int) (draws.middling() * (float) per * 0.5f));
	}
	else if (startKind == START_AFTER) {
		first = groupStart + slot * (1 + (int) (draws.middling() * (float) per));
	}
	first = std::max(0.f, std::min(first, soundsTo - slot));

	// IS THE LAST NOTE HELD? In most phrases of both corpora it is the longest in its group —
	// seven in ten for songs, more than half for jazz — and songs hold more than solos do, which
	// is what HOLD already says. So the tendency is drawn from HOLD rather than being certain:
	// making it certain gave every group a held ending, which sounds like a rule being obeyed.
	const bool holdLast = draws.chance(0.5f + 0.4f * std::max(0.f, std::min(1.f, c.hold)));

	// HOW IT ENDS: on a strong beat, or off it. ENDING is the mix.
	float endWeights[NUM_ENDS];
	endWeights[END_STRONG] = std::max(0.02f, c.ending);
	endWeights[END_OFFBEAT] = std::max(0.02f, 1.f - c.ending);
	const int endKind = draws.pick(endWeights, NUM_ENDS);

	// THE ONSETS. A slot sounds with a probability from DENSITY and its metric strength, which
	// SYNCOPATION blends toward its inverse. A chord change raises it by ON CHANGES. The group's
	// first slot always sounds, because a group with no notes in it is not a group.
	const int firstNote = out.noteCount;
	for (float t = first; t < soundsTo - 0.001f; t += slot) {
		const float inBar = std::fmod(t, std::max(1.f, ask.barBeats));
		const float m = metricStrength(inBar < 0.f ? inBar + ask.barBeats : inBar, ask.barBeats,
			c.subdivision);
		const float w = (1.f - c.syncopation) * m + c.syncopation * (1.f - m);
		float p = c.density * (0.4f + 1.2f * w);
		const bool onChange = nearChange(ask, t, slot * 0.51f);
		if (onChange)
			p += c.onChanges * (1.f - p);
		const bool sound = (t <= first + 0.001f) || draws.chance(std::min(1.f, p));
		if (!sound)
			continue;
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

	// NO LONG GAPS inside a group: a silence longer than half the group is heard as the line
	// having stopped, which is what the pause is for.
	const float longest = (soundsTo - first) * 0.5f;
	for (int i = firstNote; i + 1 < out.noteCount; i++) {
		const float gap = out.notes[i + 1].offset - out.notes[i].offset;
		if (gap <= longest + 0.001f)
			continue;
		const float mid = out.notes[i].offset + std::round(gap * 0.5f / slot) * slot;
		if (out.noteCount >= PhrasePattern::MAX_NOTES) {
			out.full = true;
			break;
		}
		for (int k = out.noteCount; k > i + 1; k--)
			out.notes[k] = out.notes[k - 1];
		out.noteCount++;
		PhraseNote& n = out.notes[i + 1];
		n.offset = mid;
		n.duration = slot;
		const float inBar = std::fmod(mid, std::max(1.f, ask.barBeats));
		const float m = metricStrength(inBar, ask.barBeats, c.subdivision);
		n.level = std::min(1.f, std::max(0.05f, 0.55f + (m - 0.5f) * c.dynamics));
		n.onChange = nearChange(ask, mid, slot * 0.51f);
		n.group = group;
	}

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
		want = std::max(earliest, std::min(want, soundsTo - slot * 0.5f));
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
		if (isLast && holdLast) {
			// LONGEST BY CONSTRUCTION when the draw asked for a held ending, so the measured share
			// matches the chance that was drawn. Left to arithmetic it came out at 35 per cent
			// against the 56 the jazz solos show, because a last note with little room before the
			// pause can be shorter than an interior note that had a rest after it.
			float longest = 0.f;
			for (int k = firstNote; k < out.noteCount - 1; k++)
				longest = std::max(longest, out.notes[k].duration);
			d = std::max(room + pause * c.hold, longest + slot * 0.25f);
		}
		// A HELD NOTE MAY REACH INTO THE PAUSE, WHICH IS WHAT HOLDING IS, BUT NOT PAST THE GROUP.
		// Making the last note the longest by construction let it run over the group's end, and in
		// the final group that is past the end of the phrase — caught by the census as one note
		// outside its phrase in twenty thousand.
		if (isLast)
			d = std::min(d, groupEnd - out.notes[i].offset);
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

void phraseGenerate(const PhraseAsk& ask, const PhraseControls& c, PhrasePattern& out) {
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
	for (int g = 0; g < groups; g++) {
		const float groupEnd = (g + 1 < groups) ? starts[g + 1] : ask.phraseBeats;
		fillGroup(ask, c, draws, g, starts[g], groupEnd, g + 1 == groups, out);
	}
}

} // namespace px
