#pragma once
/** WHAT A NOTE'S START IS LIKE, measured the same way on a recording and on the model: phase 6 of
docs/brass.md.

A trombonist's start is not the fastest one nor the steadiest one; it is the one that sounds like a
trombonist's. So it is described by measuring real ones — the Iowa recordings' tongued notes, every register
and dynamic (make brassref ARGS=starts) — and a start the model tries is judged by how far it is from them.

THE SHAPE. From the moment the sound first comes within 30 dB of the level it settles at — the level over
400 to 450 ms from there — every 5 ms to 350 ms: the level then, over 5 ms, against the settled level, dB;
the pitch, by YIN over 10 ms, against the note's settled pitch, cents; how surely it repeats, YIN's
sureness, nought to one; and its brightness, the spectral centroid over 10 ms, against the settled one, as
a ratio in octaves. The recordings have settled well within 350 ms, so a start still changing after 200 —
its breath rising slowly, its tone opening up as it does, a "wah" by ear — differs from them there. Settled
at 200 to 250 ms and measured to 200, as the shape first was, a breath rising over 300 ms was still opening
up when it was taken as settled: the measure could not see the sweep, and preferred it.

THE TEMPLATE for a register and dynamic is the median of the recordings' shapes at each moment, and how
widely they spread there, half the distance between the quartiles; a start's distance from it is the mean
over the moments it is audible of each measure's difference in those spreads, squared, each held to three,
the four measures together.

NO RACK IN HERE. */
#include <cstddef>
#include <vector>

namespace px {


struct BrassStartShape {
	static const int FRAMES = 71;   ///< 0 to 350 ms, every 5 ms
	bool valid = false;
	float level[FRAMES], cents[FRAMES], sure[FRAMES], bright[FRAMES];
	/** Where the start was found: the sample it was heard from. */
	size_t onset = 0;
	/** A BLIP AND A BLAT at the very start, which by ear the recordings never have and the model's starts
	can: over the first 60 ms, how far the level falls after a peak within 12 dB of settled, dB, the blip;
	and the most it rises from one 5 ms frame to the next onto one within 12 dB, dB, the blat. */
	float blip = 0.f, blat = 0.f;
};

/** THE SHAPE OF THE START of `x` at `rate`, the note's pitch `hz`, looking for its start from sample `from`
on. Invalid where the sound never comes within 30 dB of where it settles, or does not last 450 ms. */
BrassStartShape brassStartShape(const double* x, size_t n, size_t from, double rate, double hz);

/** WHAT THE RECORDINGS' STARTS ARE LIKE, for one register and dynamic: each measure's median and spread at
each moment. */
struct BrassStartTemplate {
	float level[BrassStartShape::FRAMES], cents[BrassStartShape::FRAMES], sure[BrassStartShape::FRAMES],
		bright[BrassStartShape::FRAMES];
	float levelSpread[BrassStartShape::FRAMES], centsSpread[BrassStartShape::FRAMES], sureSpread[BrassStartShape::FRAMES],
		brightSpread[BrassStartShape::FRAMES];
	int count;
};

/** THE TEMPLATES OF THE TENOR TROMBONE, as make brassref ARGS=starts found them in the Iowa recordings:
registers E2 to B2, C3 to B3 and C4 to C5, and dynamics pp, mf and ff, as [register][dynamic]. */
const BrassStartTemplate& tromboneStart(int reg, int dynamic);

/** WHICH TEMPLATE: the register of a pitch, and the dynamic nearest a level on the cable's ladder. */
int brassStartRegister(double hz);
int brassStartDynamic(double level);

/** WHETHER A START IS CLEAN: no more blip and blat than nine in ten of the recordings' starts have (make
brassref ARGS=starts). Past either, a start is not a trombonist's by ear, however near it is otherwise. */
bool brassStartClean(const BrassStartShape& s);

/** HOW FAR A START IS FROM THE RECORDINGS', in their spreads; less is more like them. */
double brassStartDistance(const BrassStartShape& s, const BrassStartTemplate& t, double* parts = nullptr);


} // namespace px
