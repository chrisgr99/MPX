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

int voicePlace(const VoicingRequest& req, float* out) {
	const int n = std::min(req.count, VOICING_MAX);
	if (n <= 0)
		return 0;

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
	if (nc == 0) {
		for (int v = 0; v < n; v++)
			out[v] = plain[v];
		return n;
	}
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
	if (end < 0) {
		// Nothing fits — more voices than the range has places for. The plain stack always does.
		for (int v = 0; v < n; v++)
			out[v] = plain[v];
		return n;
	}

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
	return n;
}


} // namespace px
