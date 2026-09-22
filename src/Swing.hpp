#pragma once
/** How hard the music swings, at the tempo it is playing.

WHAT A SWING RATIO IS. Within one beat divided in two, how long the first half lasts against the
second. One is even; two is a full triplet feel, where the pair is the first and third unit of a
triplet. Everything between is a partial swing.

MEASURED, NOT ASSUMED. These numbers come from 456 transcribed jazz solos — the Weimar Jazz
Database — over 22,574 eighth pairs and 7,717 sixteenth pairs, one figure per solo so a long solo
does not outvote fifty short ones. See `python3 test/swing.py` and the Evidence section of
docs/phrase.md.

TWO THINGS THE CORPUS SETTLED, both of which a fixed ratio would get wrong.

A full two to one is not what players do. The median solo swings its eighths at 1.31, with the
middle half between 1.16 and 1.46.

And it depends on the tempo in both directions. Around 120 to 200 beats a minute the median is
1.43; it falls to 1.36 by 240, 1.24 by 280 and 1.15 by 320, and it is also lower below 120, at
1.22. So the ratio peaks at medium tempos and flattens at both ends — which is why the control is
an AMOUNT and the ratio is worked out from it and the tempo, rather than being set by hand.

SIXTEENTHS ARE A DIFFERENT CURVE, much flatter: median 1.10, middle half 1.02 to 1.18. They are
what slow tempos divide by, which is why measuring only the eighth level made slow tempos look
straight — at sixty-five beats a minute almost no beat is divided in two.
*/

namespace px {

/** A tempo and the ratio measured at it. Between the points the curve is a straight line; outside
them it holds the end value. */
struct SwingPoint { float bpm, ratio; };

/** THE EIGHTH-NOTE LEVEL. Drawn from the per-solo medians by tempo, rounded to where the
measurement is actually confident: the buckets from 120 to 280 hold between 50 and 68 solos each,
the ends fewer. */
inline const SwingPoint* swingEighthCurve(int& n) {
	static const SwingPoint pts[] = {
		{60.f, 1.12f}, {100.f, 1.25f}, {140.f, 1.43f}, {180.f, 1.43f},
		{220.f, 1.36f}, {260.f, 1.27f}, {300.f, 1.15f}, {340.f, 1.05f},
	};
	n = (int) (sizeof(pts) / sizeof(pts[0]));
	return pts;
}

/** THE SIXTEENTH-NOTE LEVEL, which is where slow and medium tempos do their dividing. Flatter
throughout, and flattening further as the tempo rises — at two hundred beats a minute a swung
sixteenth is almost even. */
inline const SwingPoint* swingSixteenthCurve(int& n) {
	static const SwingPoint pts[] = {
		{60.f, 1.06f}, {100.f, 1.19f}, {140.f, 1.09f}, {180.f, 1.04f}, {240.f, 1.02f},
	};
	n = (int) (sizeof(pts) / sizeof(pts[0]));
	return pts;
}

inline float swingLookup(const SwingPoint* pts, int n, float bpm) {
	if (n <= 0)
		return 1.f;
	if (bpm <= pts[0].bpm)
		return pts[0].ratio;
	for (int i = 1; i < n; i++) {
		if (bpm <= pts[i].bpm) {
			const float span = pts[i].bpm - pts[i - 1].bpm;
			const float t = span > 0.f ? (bpm - pts[i - 1].bpm) / span : 0.f;
			return pts[i - 1].ratio + (pts[i].ratio - pts[i - 1].ratio) * t;
		}
	}
	return pts[n - 1].ratio;
}

/** THE RATIOS FOR ONE AMOUNT AT ONE TEMPO. The amount runs from nought, which is even, to one,
which is what the corpus measured at that tempo — never a literal two to one, because that is not
what was played. */
inline void swingRatios(float amount, float bpm, float& eighth, float& sixteenth) {
	if (amount < 0.f)
		amount = 0.f;
	if (amount > 1.f)
		amount = 1.f;
	int n8 = 0, n16 = 0;
	const float full8 = swingLookup(swingEighthCurve(n8), n8, bpm);
	const float full16 = swingLookup(swingSixteenthCurve(n16), n16, bpm);
	eighth = 1.f + amount * (full8 - 1.f);
	sixteenth = 1.f + amount * (full16 - 1.f);
}

} // namespace px
