/** IS THE READING ALONG A PATH ACTUALLY DOING WHAT IT CLAIMS?

A reading head running round a circle at audio rate is a wavetable oscillator, and the oldest
fault in one is that point-sampling the table folds everything above half the sample rate back
down as inharmonic noise. The answer here is to average over the arc the head covers in one
sample — a box filter whose width follows the rate — kept cheap with a running total.

Whether those few lines are right cannot be told by listening to a photograph, so this puts a
KNOWN signal under the path and checks four things that have to be true if the arithmetic is:

	1. The average over an arc IS the mean of the signal over that arc.
	2. It attenuates with width exactly as a box filter must — nothing at all when the width is
	   one whole cycle of the tone, since that is the tone's own mean.
	3. At a width of nothing it becomes plain interpolation, so there is no step between the slow
	   behaviour and the fast one and no stair-stepping at pixel edges.
	4. The seam, the direction and a whole turn are all handled.

	make pathread
*/
#include "../src/Paths.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace px;


// The picture's file handling needs Rack; its reading does not. These are the only pieces of it
// the test touches, and they do nothing here.
namespace px {
bool Picture::load(const std::string&) { return false; }
bool Picture::decode(const std::vector<uint8_t>&, const std::string&) { return false; }
void Picture::clear() { rgba.clear(); file.clear(); name.clear(); generation++; }
}


/** A picture whose brightness depends only on the ANGLE round its centre — a pinwheel of `cycles`
light and dark sectors, varying as a sine.

A pinwheel rather than a tone painted on a ring, because the reading averages a small disc across
the path as it must; a one-pixel ring would be read as its tone diluted by whatever surrounds it,
and the test would be measuring the dilution. A pinwheel carries the same value at every radius,
so any circle centred here reads exactly the same tone and what is measured is the filtering. */
static void paintPinwheel(Picture& p, int cycles) {
	p.rgba.assign((size_t) PICTURE_SIDE * PICTURE_SIDE * 4, 255);
	for (int py = 0; py < PICTURE_SIDE; py++) {
		for (int px = 0; px < PICTURE_SIDE; px++) {
			const double dx = (px + 0.5) - 0.5 * PICTURE_SIDE;
			const double dy = 0.5 * PICTURE_SIDE - (py + 0.5);
			// The same angle the path uses: nought at three o'clock, counter-clockwise.
			double a = std::atan2(dy, dx) / (2.0 * M_PI);
			if (a < 0.0)
				a += 1.0;
			const double v = 0.5 + 0.45 * std::sin(a * 2.0 * M_PI * (double) cycles);
			const uint8_t b = (uint8_t) std::lround(v * 255.0);
			uint8_t* at = &p.rgba[((size_t) py * PICTURE_SIDE + px) * 4];
			at[0] = at[1] = at[2] = b;
		}
	}
	p.generation++;
}


/** How far a reading swings as the head goes once round: the depth of the tone that comes out. */
static double swing(const PathTable& table, double width, int steps) {
	double lo = 2.0, hi = -1.0;
	for (int i = 0; i < steps; i++) {
		const double at = (double) i / (double) steps;
		const double v = table.read(at, at + width);
		lo = std::min(lo, v);
		hi = std::max(hi, v);
	}
	return hi - lo;
}


static int failures = 0;

static void check(bool ok, const char* what, double got, double want, double slack) {
	std::printf("  %-58s %8.4f  wanted %7.4f  %s\n", what, got, want,
		ok ? "ok" : "NO");
	if (!ok)
		failures++;
}


int main() {
	Picture picture;
	Path path;
	path.kind = Path::CIRCLE;
	path.cx = path.cy = 0.5f;
	path.diameter = 0.6f;

	const int CYCLES = 8;
	paintPinwheel(picture, CYCLES);

	PathTable table;
	table.build(picture, path, READ_VALUE);
	std::printf("a circle 0.6 across is %d points long, carrying %d cycles\n\n",
		table.points(), CYCLES);
	if (!table.ready()) {
		std::printf("the table was not built\n");
		return 1;
	}

	// ---- 1. a narrow read returns the tone itself -------------------------------------------
	//
	// The pinwheel swings 0.9 peak to peak by construction, and a read narrow enough to sit
	// inside one point of the table has nothing to average away.
	std::printf("the tone, read narrowly\n");
	{
		const double got = swing(table, 1e-9, 2048);
		check(std::fabs(got - 0.9) < 0.03, "swing of a read of no width", got, 0.9, 0.03);
	}

	// ---- 2. it attenuates as a box filter must ----------------------------------------------
	//
	// A box average over exactly one cycle of a sine IS that sine's mean, which is nothing. Over
	// half a cycle it is two over pi of the peak — 0.6366 — which is the textbook figure and the
	// one that says this is a box and not something else.
	std::printf("\nattenuation with width, which is what stops the aliasing\n");
	{
		const double oneCycle = 1.0 / (double) CYCLES;
		const double atFull = swing(table, oneCycle, 2048);
		check(atFull < 0.02, "a width of one whole cycle leaves nothing", atFull, 0.0, 0.02);

		const double atHalf = swing(table, oneCycle * 0.5, 2048);
		const double want = 0.9 * 2.0 / M_PI;
		check(std::fabs(atHalf - want) < 0.03, "a width of half a cycle leaves two over pi",
			atHalf, want, 0.03);

		const double atTwo = swing(table, oneCycle * 2.0, 2048);
		check(atTwo < 0.02, "a width of two whole cycles leaves nothing", atTwo, 0.0, 0.02);

		// AND IT NEVER COMES BACK UP ABOVE THE TONE. A filter that overshoots is not a filter.
		double worst = 0.0;
		for (int i = 1; i <= 400; i++)
			worst = std::max(worst, swing(table, oneCycle * (double) i / 40.0, 256));
		check(worst < 0.93, "no width anywhere gives more than the tone itself", worst, 0.9, 0.03);
	}

	// ---- 3. no step between the two behaviours ----------------------------------------------
	//
	// As the width goes to nothing the box average has to become ordinary interpolation. If the
	// two were different pieces of code there would be a jump between them; there is one piece.
	std::printf("\nthe slow end and the fast end are one piece of code\n");
	{
		// HOW MUCH THE SIGNAL MOVES IN ONE ENTRY. Two reads of different widths are two different
		// box filters and must differ a little; what matters is that they differ by less than the
		// picture changes from one point to the next, so there is no step between the two
		// behaviours — only the filtering each of them is for.
		const double perEntry = 0.9 * 2.0 * M_PI * CYCLES / (double) table.points();
		double worst = 0.0;
		for (int i = 0; i < 512; i++) {
			const double at = (double) i / 512.0;
			const double narrow = table.read(at, at + 1e-9);
			const double tiny = table.read(at, at + 1e-5);
			worst = std::max(worst, std::fabs(narrow - tiny));
		}
		check(worst < perEntry, "no width of nothing and a hair differ by over one point",
			worst, 0.0, perEntry);

		// AND NO STAIR-STEPPING. Stepping from point to point of the table must give a smooth
		// ramp, not a staircase: the second difference of a smooth sine is small everywhere.
		const int n = table.points();
		double roughest = 0.0;
		for (int i = 2; i < 400; i++) {
			const double a = table.read((double) (i - 2) / n, (double) (i - 2) / n);
			const double b = table.read((double) (i - 1) / n, (double) (i - 1) / n);
			const double c = table.read((double) i / n, (double) i / n);
			roughest = std::max(roughest, std::fabs(a - 2.0 * b + c));
		}
		check(roughest < 0.005, "point to point is smooth, not a staircase", roughest, 0.0, 0.005);
	}

	// ---- 4. the seam, the direction and a whole turn ----------------------------------------
	std::printf("\nthe seam, the direction and a whole turn\n");
	{
		const double whole = table.read(0.0, 1.0);
		const double again = table.read(0.25, 1.25);
		check(std::fabs(whole - again) < 1e-4, "a whole turn reads the same from anywhere",
			std::fabs(whole - again), 0.0, 1e-4);
		check(std::fabs(whole - 0.5) < 0.02, "and it is the picture's own mean", whole, 0.5, 0.02);

		const double forward = table.read(0.2, 0.3);
		const double backward = table.read(0.3, 0.2);
		check(std::fabs(forward - backward) < 1e-6, "the same arc reads the same either way",
			std::fabs(forward - backward), 0.0, 1e-6);

		// ACROSS THE SEAM is the same arc as one that does not cross it, on a signal that is the
		// same all the way round.
		const double across = table.read(0.97, 1.03);
		const double inside = table.read(0.47, 0.53);
		check(std::fabs(across - inside) < 0.02, "an arc across the seam reads like any other",
			std::fabs(across - inside), 0.0, 0.02);
	}

	// ---- a line, which does not wrap --------------------------------------------------------
	std::printf("\na line, which holds at its ends instead of wrapping\n");
	{
		Path line;
		line.kind = Path::LINE;
		line.ax = 0.2f; line.ay = 0.5f; line.bx = 0.8f; line.by = 0.5f;
		line.moved();
		PathTable lt;
		lt.build(picture, line, READ_VALUE);
		check(lt.points() > 100, "its table was built", (double) lt.points(), 600.0, 1e9);
		const double past = lt.read(1.2, 1.3);
		const double end = lt.read(0.999, 1.0);
		check(std::fabs(past - end) < 0.05, "past the end it holds at the end",
			std::fabs(past - end), 0.0, 0.05);
	}

	std::printf("\n%s\n", failures ? "FAILED" : "all of it holds");
	return failures ? 1 : 0;
}
