#include "Voicing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace px {


/** Every place a chosen tone could sit inside the widest range the panel allows: six tones, each
in as many octaves as three octaves and a little slack can hold. */
static const int MAX_CANDIDATES = 48;
/** THE TOP VOICE IS THE MELODY whether anybody wrote one or not, so a semitone of movement up
there is worth more than a semitone in the middle and the search spends elsewhere first. */
static const float TOP_WEIGHT = 1.6f;
/** How hard the spacing a Spread setting asks for is pushed for, per semitone away from it,
against movement counted at one. Below movement, because a voicing that keeps its lines and
spaces itself a little oddly is better than the reverse. */
static const float SPACE_WEIGHT = 0.5f;
/** And how hard a tight interval low in the register is pushed against. Above the spacing
weight, because that one is a preference and this one is a fault. */
static const float MUD_WEIGHT = 1.2f;
/** Below this, in volts, a close interval smears. An octave under middle C. */
static const float MUD_BELOW = -1.f;
/** Two voices less than a whole tone apart, anywhere in the register. */
static const float CLASH_WEIGHT = 1.2f;
/** The octave that Drop two puts under the rest. Held for harder than the other spacings,
because it is the shape of the voicing rather than a preference about it. */
static const float DROP_WEIGHT = 1.2f;

static const float BIG = 1e18f;


/** Where a plain stack would put each voice: the tones from the bottom of the range upward, a
doubled tone an octave above the one it doubles.

IT IS BUILT IN THE SHAPE THE SPREAD ASKS FOR, because this is not only what Lead at nought means
but the reference every voice is scored against on the first chord, when there is nowhere it has
come from. A close reference under a Drop two setting would charge the search for doing what it
was told, and the first chord of a take would come out close however the panel was set. */
static void plainStack(const VoicingRequest& req, float* plain) {
	float above = req.lo - 1.f;
	for (int v = 0; v < req.count; v++) {
		float p = req.lo + (float) req.pcs[v] / 12.f;
		while (p <= above + 1e-4f)
			p += 1.f;
		plain[v] = p;
		above = p;
	}
	// OPEN IS THE CLOSE STACK WITH EVERY OTHER VOICE AN OCTAVE UP, which is how a player opens
	// a voicing and what puts the gaps at a fifth or a sixth. Insisting instead on a minimum
	// gap while stacking pushes each tone a whole octave whenever it lands a third above the
	// last, and a chord spread over three octaves is not an open voicing, it is a wreck.
	if (req.spread == VOICING_OPEN && req.count >= 3) {
		for (int v = 1; v < req.count; v += 2)
			plain[v] += 1.f;
		std::sort(plain, plain + req.count);
	}
	// Drop two is that stack with its lowest voice an octave down. Where the range has no room
	// underneath, the whole voicing goes up an octave first, and if it cannot it stays close —
	// a two-octave setting cannot hold a drop-two voicing of six notes and should not pretend.
	if (req.spread == VOICING_DROP2 && req.count >= 3) {
		if (plain[0] - 1.f < req.lo - 1e-4f) {
			if (plain[req.count - 1] + 1.f <= req.hi + 1e-4f) {
				for (int v = 0; v < req.count; v++)
					plain[v] += 1.f;
			}
			else
				return;
		}
		plain[0] -= 1.f;
	}
}

/** ONE ATTEMPT AT THE RANGE IT IS GIVEN. False when the range has nowhere to put the voices,
which is what voicePlace answers by widening it. */
static bool placeOnce(const VoicingRequest& req, float* out) {
	const int n = std::min(req.count, VOICING_MAX);
	if (n <= 0)
		return false;

	float plain[VOICING_MAX];
	plainStack(req, plain);

	// EVERY PLACE A CHOSEN TONE COULD SIT, inside the range, sorted upward. A doubled tone
	// appears as two entries with the same places, which is what doubling is.
	struct Cand { float pitch; int tone; };
	Cand cand[MAX_CANDIDATES];
	int nc = 0;
	for (int t = 0; t < n && nc < MAX_CANDIDATES; t++) {
		const float base = (float) req.pcs[t] / 12.f;
		for (int oct = (int) std::floor(req.lo) - 1; oct <= (int) std::ceil(req.hi) + 1; oct++) {
			const float p = base + (float) oct;
			if (p < req.lo - 1e-4f || p > req.hi + 1e-4f || nc >= MAX_CANDIDATES)
				continue;
			cand[nc].pitch = p;
			cand[nc].tone = t;
			nc++;
		}
	}
	if (nc == 0)
		return false;
	std::sort(cand, cand + nc, [](const Cand& a, const Cand& b) { return a.pitch < b.pitch; });

	// What one voice landing here costs.
	auto voiceCost = [&](int v, float p) {
		const float held = (v < req.heldCount) ? req.held[v] : plain[v];
		const float top = (v == n - 1) ? TOP_WEIGHT : 1.f;
		return req.lead * top * std::fabs(p - held) * 12.f
			+ (1.f - req.lead) * std::fabs(p - plain[v]) * 12.f;
	};
	// What the gap between one voice and the one below it costs.
	auto spacingCost = [&](int pair, float lower, float upper) {
		const float gap = (upper - lower) * 12.f;
		float c = 0.f;
		// MUDDY DOWN THERE. A third low in the register is a smear whatever the voicing is
		// called, so a close interval below the C an octave under middle C is penalised wherever
		// it appears.
		if (lower < MUD_BELOW && gap < 7.f)
			c += (7.f - gap) * MUD_WEIGHT;
		// AND A SEMITONE BETWEEN NEIGHBOURING VOICES IS A CLASH ANYWHERE. A thirteenth chord
		// holds a third and a thirteenth a semitone apart; a player puts an octave between them
		// and every voicing book says so. Without this the search packs them together because
		// close spacing asked it to.
		if (gap < 3.f) {
			// Squared, because a semitone between neighbours is not merely twice as bad as a
			// whole tone. A whole tone is a colour a player might choose; a semitone is a
			// mistake, and a flat penalty per semitone is cheap enough that a few semitones of
			// saved movement buys one.
			const float under = 3.f - gap;
			c += under * under * CLASH_WEIGHT;
		}
		if (req.spread == VOICING_DROP2 && pair == 0)
			c += std::fabs(gap - 12.f) * DROP_WEIGHT;
		else if (req.spread == VOICING_OPEN)
			c += std::fabs(gap - 7.f) * SPACE_WEIGHT * 0.7f;
		else
			c += std::fmax(0.f, gap - 5.f) * SPACE_WEIGHT;
		return c;
	};

	// THE SEARCH. A state is which tones have been used and which candidate the highest voice
	// took; the number of tones used is which voice is being filled. Voices are filled from the
	// bottom upward and each must be above the last, so they cannot cross and voice two is the
	// same line before and after the change.
	const int masks = 1 << n;
	std::vector<float> best((size_t) masks * nc, BIG);
	std::vector<int16_t> from((size_t) masks * nc, (int16_t) -1);
	auto at = [&](int mask, int c) { return (size_t) mask * nc + c; };

	for (int c = 0; c < nc; c++) {
		// The lowest voice takes the tone it was told to, where it was told one.
		if (req.bassTone >= 0 && cand[c].tone != req.bassTone)
			continue;
		const float cost = voiceCost(0, cand[c].pitch);
		const size_t i = at(1 << cand[c].tone, c);
		if (cost < best[i])
			best[i] = cost;
	}

	for (int mask = 1; mask < masks; mask++) {
		int v = 0;
		for (int b = 0; b < n; b++)
			v += (mask >> b) & 1;
		if (v >= n)
			continue;
		for (int c = 0; c < nc; c++) {
			const float have = best[at(mask, c)];
			if (have >= BIG)
				continue;
			for (int d = c + 1; d < nc; d++) {
				const int bit = 1 << cand[d].tone;
				if (mask & bit)
					continue;
				if (cand[d].pitch <= cand[c].pitch + 1e-4f)
					continue;
				const float cost = have + voiceCost(v, cand[d].pitch)
					+ spacingCost(v - 1, cand[c].pitch, cand[d].pitch);
				if (cost < best[at(mask | bit, d)]) {
					best[at(mask | bit, d)] = cost;
					from[at(mask | bit, d)] = (int16_t) c;
				}
			}
		}
	}

	const int full = masks - 1;
	int end = -1;
	float endCost = BIG;
	for (int c = 0; c < nc; c++) {
		if (best[at(full, c)] < endCost) {
			endCost = best[at(full, c)];
			end = c;
		}
	}
	if (end < 0)
		return false;

	int mask = full;
	int c = end;
	for (int v = n - 1; v >= 0; v--) {
		out[v] = cand[c].pitch;
		const int prev = from[at(mask, c)];
		mask ^= 1 << cand[c].tone;
		c = prev;
		if (c < 0)
			break;
	}
	return true;
}


/** WHICH TONES GET PLAYED, when there are fewer voices than the chord has tones — which is
the ordinary case rather than the awkward one.

Taking the lowest ranks is the whole rule, because the ranks were written to be taken in
order: the tone that says which quality this is, then the seventh, then the colour, then the
root, and the plain fifth last of all. Three voices under a thirteenth chord therefore play
the third, the seventh and the thirteenth, which is what a pianist plays and what no amount
of arithmetic on a polyphonic cable could have worked out.

MORE VOICES THAN TONES doubles from the same order, so the sixth voice of a triad doubles the
third rather than whatever happened to be first in the list.

Writes the pitch classes IN STACK ORDER — the root first and the rest as they rise above it,
which is what the voicing wants — and returns how many. */
int voiceChooseTones(const ChordTone* tones, int count, int want, bool ownBass, int colour,
		int* pcs) {
	if (count <= 0)
		return 0;
	int rootPc = tones[0].pc;
	for (int i = 0; i < count; i++)
		if (tones[i].degree == 1)
			rootPc = tones[i].pc;

	// HOW MUCH OF THE CHORD TO PLAY. A triad is the degrees a triad has, which includes the
	// fourth and the second a sus chord puts where its third would be; sevenths adds the
	// seventh and the sixth; extensions is everything the quality implies.
	int keep[MAX_CHORD_TONES];
	int n = 0;
	for (int i = 0; i < count; i++) {
		const int d = tones[i].degree;
		bool take = true;
		if (colour == VOICING_TRIAD)
			take = (d == 1 || d == 3 || d == 4 || d == 5 || (d == 9 && tones[i].rank == 0));
		else if (colour == VOICING_SEVENTHS)
			take = (d != 11 && d != 13 && !(d == 9 && tones[i].rank != 0));
		if (take)
			keep[n++] = i;
	}
	// A chord can be left with nothing to play — a fifth chord asked for a third. Whatever
	// the quality does have is better than silence.
	if (n == 0) {
		for (int i = 0; i < count; i++)
			keep[n++] = i;
	}

	int above[MAX_CHORD_TONES];
	int rank[MAX_CHORD_TONES];
	// THE ELEVENTH IS WHY THE THIRD WAS RANKED LAST. Take the eleventh away and the third
	// is an ordinary third again, so the table's ranking has to be undone here rather than
	// leaving an eleventh chord played as a rootless fifth.
	bool hasEleven = false;
	for (int k = 0; k < n; k++)
		hasEleven = hasEleven || (tones[keep[k]].degree == 11);
	for (int k = 0; k < n; k++) {
		const ChordTone& t = tones[keep[k]];
		above[k] = ((t.pc - rootPc) % 12 + 12) % 12;
		// PLAYING ITS OWN BOTTOM MAKES THE ROOT ESSENTIAL. The table ranks it fourth
		// because a bass usually has it; when nothing else does, it is the first tone
		// kept and the first tone doubled.
		if (ownBass && t.degree == 1)
			rank[k] = -1;
		else if (!hasEleven && t.degree == 3 && t.rank > 4)
			rank[k] = 0;
		else
			rank[k] = t.rank;
	}
	count = n;

	// The keep order: rank first, and the lower tone first where two are ranked alike.
	int order[MAX_CHORD_TONES];
	for (int i = 0; i < count; i++)
		order[i] = i;
	std::sort(order, order + count, [&](int a, int b) {
		if (rank[a] != rank[b])
			return rank[a] < rank[b];
		return above[a] < above[b];
	});

	// Take that many, doubling round the same order when there are more voices than tones,
	// then put them back into stack order for the voicing.
	int chosen[VOICING_MAX];
	const int take = std::min(want, VOICING_MAX);
	for (int v = 0; v < take; v++)
		chosen[v] = order[v % count];
	std::sort(chosen, chosen + take, [&](int a, int b) { return above[a] < above[b]; });
	for (int v = 0; v < take; v++)
		pcs[v] = tones[keep[chosen[v]]].pc;
	return take;
}


/** THE RANGE IS WHAT WAS ASKED FOR, AND THEN AS LITTLE MORE AS WILL DO.

Six voices do not fit in one octave. Asked for a range with nowhere to put them, the search used
to give up and hand back a plain stack from the BOTTOM of the range upward — each voice pushed an
octave above the last, with nothing looking at the top of the range at all. So narrowing the span
raised the chord instead of tightening it, which is the opposite of what the control says.

Now the range is widened a half octave at a time, evenly above and below, until the voices fit.
The register is kept — the chord grows around its centre rather than climbing — and a span that
does fit is used exactly as given. */
int voicePlace(const VoicingRequest& req, float* out) {
	const int n = std::min(req.count, VOICING_MAX);
	if (n <= 0)
		return 0;
	const float mid = (req.lo + req.hi) / 2.f;
	const float half = (req.hi - req.lo) / 2.f;
	for (int widen = 0; widen <= 8; widen++) {
		VoicingRequest r = req;
		r.lo = mid - half - 0.5f * (float) widen;
		r.hi = mid + half + 0.5f * (float) widen;
		if (placeOnce(r, out))
			return n;
	}
	// Four octaves either side and still nowhere: the plain stack always writes something.
	plainStack(req, out);
	return n;
}


} // namespace px
