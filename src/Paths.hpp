#pragma once
/** A PATH DRAWN ON A PICTURE, AND READ ALONG WITHOUT ALIASING.

WHAT A PATH IS. A line — two ends — or a circle — a centre and a diameter. Everything is in
picture units, nought to one across the square, so a path means the same thing whatever size the
picture is drawn at and survives a different picture being loaded under it.

READING IT. A reading head runs along the path and what is underneath becomes a number. Point-
sampling that is the oldest mistake in wavetable synthesis: a table of a thousand points read at
two hundred places a revolution folds everything above half the sample rate back down as
inharmonic noise. So the path is resampled once into a table, the running total of that table is
kept, and a read returns the MEAN over the arc the head covered in one sample — an exact box
filter whose width follows the rate, costing the same few additions however wide it is.

AND IT IS THE SAME CODE AT BOTH ENDS. Read the running total at fractional places and the box
average becomes ordinary linear interpolation as its width goes to nothing, so a head crawling
round once every few bars gives a smooth sweep with no stair-stepping at pixel edges, and one
running at audio rate gives a clean tone, with nothing to switch between them.

THE TABLE IS BUILT WHEN THE PATH MOVES, not per sample: dragging a path rebuilds it, and nothing
else does.
*/
#include "Picture.hpp"

#include <vector>

namespace px {


/** A path on the picture, in picture units. */
struct Path {
	enum Kind { LINE, CIRCLE };
	int kind = CIRCLE;

	/** LINE: its two ends. A line has a direction — the head runs from the first end to the
	second — so which end is which is not arbitrary. */
	float ax = 0.35f, ay = 0.5f, bx = 0.65f, by = 0.5f;

	/** CIRCLE: its centre, and how far across it is. Nought is at three o'clock and it runs
	counter-clockwise, which is the convention in the app this comes from and is kept so that a
	figure moved between them behaves the same way. */
	float cx = 0.5f, cy = 0.5f, diameter = 0.3f;

	/** Where the head is when it is a fraction `t` of the way along, `t` wrapping for a circle
	and running nought to one along a line. */
	void at(double t, float* x, float* y) const;

	/** How long it is, in picture units — a line's length, or a circle's circumference. What the
	table's size and the read's width are both worked out from. */
	float length() const;

	/** Moved whenever anything about the path changes, so the table knows it is stale without
	comparing the path field by field. */
	uint32_t generation = 0;
	void moved() { generation++; }
};


/** THE PATH RESAMPLED, AND ITS RUNNING TOTAL.

One entry per picture pixel along the path, give or take, and each entry the mean of a small disc
rather than one pixel — a path has no thickness and a picture is a grid, so a single-pixel reading
depends on exactly which row the path grazes.

`total` holds the running sum: the mean between two places is the difference of two of its values
divided by the distance between them, which is why the width of a read costs nothing. */
struct PathTable {
	/** The running total, one longer than the number of entries; total[0] is nought.

	DOUBLE, NOT FLOAT, and this is not fussiness. The total over a long path reaches the hundreds,
	while a narrow read subtracts two of its values a millionth apart — which single precision
	cannot represent at all, so the difference comes out as noise and, divided by that tiny width,
	as a number tens of times larger than the signal. The slower the head moves the worse it gets,
	which is exactly backwards. */
	std::vector<double> total;
	/** Which picture and which path shape this was built from, so staleness is one comparison. */
	uint32_t pictureGeneration = 0;
	uint32_t pathGeneration = 0;
	int channel = -1;
	bool circular = false;

	int points() const { return (int) total.size() - 1; }
	bool ready() const { return points() > 1; }

	/** Builds it. `channel` is which of the picture's numbers is read; see Picture.hpp. */
	void build(const Picture& picture, const Path& path, int channel);

	/** Rebuilds only if the picture, the path or the channel has changed. */
	void refresh(const Picture& picture, const Path& path, int channel);

	/** THE MEAN BETWEEN TWO PLACES ALONG THE PATH, each a fraction of the way along.

	`from` and `to` are where the head was at the start and end of this sample. Their order does
	not matter — a head running backwards reads the same arc — and for a circle they wrap. When
	they are the same place the answer is the interpolated value there, which is what makes this
	one piece of code serve a slow sweep and a fast tone alike. */
	float read(double from, double to) const;
};


} // namespace px
