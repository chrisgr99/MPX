#pragma once
/** Placing the voices of a chord.

Separate from the module because it is arithmetic and nothing else: no engine, no panel, no Rack.
Given where the voices are, which tones they are to play and how much room they have, it says
where they go. That makes it testable on its own, which the module is not.
*/

#include "Chord.hpp"

namespace px {

/** HOW MUCH OF THE CHORD TO PLAY. A triad is the degrees a triad has, including the fourth and
the second that a sus chord puts where its third would be; sevenths adds the seventh and the
sixth; extensions is everything the quality implies. */
enum VoicingColour {
	VOICING_TRIAD,
	VOICING_SEVENTHS,
	VOICING_EXTENSIONS,
	VOICING_COLOURS,
};


/** Six voices is what the panel offers and what a hand has room for. */
static const int VOICING_MAX = 6;

/** How the voices are spaced, in the order the panel walks through them. */
enum VoicingSpread {
	VOICING_CLOSE,   /**< every voice in the smallest space that holds them */
	VOICING_DROP2,   /**< the lowest voice an octave under the rest: the pianist's default */
	VOICING_OPEN,    /**< voices spread evenly rather than packed */
	VOICING_SPREADS,
};

struct VoicingRequest {
	/** The tones to play, as pitch classes, IN STACK ORDER: the root first and the rest as they
	rise above it, which is not the same as ascending pitch class. A tone written twice is a tone
	to be doubled. */
	const int* pcs = nullptr;
	int count = 0;

	/** Where the voices are now, in volts, ascending. Empty on the first chord. */
	const float* held = nullptr;
	int heldCount = 0;

	/** The range the voices may use, in volts. */
	float lo = -0.5f;
	float hi = 0.5f;

	/** Nought scores a voice against a plain stack from the bottom of the range, one against
	where it already is, and between the two both pull. */
	float lead = 1.f;
	int spread = VOICING_CLOSE;

	/** WHICH TONE THE LOWEST VOICE MUST TAKE, as an index into pcs, or -1 for none.

	This is what it means for the part to be carrying its own bottom: the root is under the
	chord rather than wherever the search would rather have put it. Without it a voicing with
	the root in it still sounds rootless, because nothing says the root is the note underneath.

	It is a constraint and not a cost, since a root in the middle of the chord is not a slightly
	worse version of a root in the bass. It is a different voicing. */
	int bassTone = -1;
};

/** WHICH TONES GET PLAYED, when there are fewer voices than the chord has tones — which is the
ordinary case rather than the awkward one. Writes the pitch classes in stack order and returns how
many, which is `want` or fewer. `ownBass` says this part carries the root itself, which makes the
root essential rather than the fourth-ranked tone it is when a bass has it. */
int voiceChooseTones(const ChordTone* tones, int count, int want, bool ownBass, int colour,
	int* pcs);

/** Writes one pitch per tone, in volts, ascending, and returns how many. Always writes
something: a request that cannot be satisfied inside the range falls back to the plain stack. */
int voicePlace(const VoicingRequest& req, float* out);


} // namespace px
