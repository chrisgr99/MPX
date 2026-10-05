#include "Paths.hpp"

#include <algorithm>
#include <cmath>

namespace px {


void Path::at(double t, float* x, float* y) const {
	if (kind == LINE) {
		const double u = (t < 0.0) ? 0.0 : (t > 1.0) ? 1.0 : t;
		*x = ax + (float) u * (bx - ax);
		*y = ay + (float) u * (by - ay);
		return;
	}
	// COUNTER-CLOCKWISE FROM THREE O'CLOCK. The picture's y runs down the square, so a
	// counter-clockwise turn as a person sees it is a MINUS on the sine.
	const double a = t * 2.0 * M_PI;
	const float r = diameter * 0.5f;
	*x = cx + r * (float) std::cos(a);
	*y = cy - r * (float) std::sin(a);
}


float Path::length() const {
	if (kind == LINE) {
		const float dx = bx - ax, dy = by - ay;
		return std::sqrt(dx * dx + dy * dy);
	}
	return (float) M_PI * std::fabs(diameter);
}


const char* READING_NAMES[NUM_READINGS] = {
	"Brightness", "Saturation", "Hue red-cyan", "Hue yellow-blue", "Red", "Green", "Blue"
};


float pathReadingAt(const Picture& picture, float x, float y, int reading, float radius) {
	switch (reading) {
		case READ_SAT:   return channelAround(picture, x, y, CH_SAT, radius);
		case READ_RED:   return channelAround(picture, x, y, CH_RED, radius);
		case READ_GREEN: return channelAround(picture, x, y, CH_GREEN, radius);
		case READ_BLUE:  return channelAround(picture, x, y, CH_BLUE, radius);
		case READ_HUE_RC: {
			// THE COSINE OF THE HUE, taken at the point. The disc averaging is skipped because
			// averaging hues as numbers is the very thing these two axes exist to avoid; the
			// averaging that matters happens along the path, where the axes add correctly.
			const float a = channelAt(picture, x, y, CH_HUE) * 2.f * (float) M_PI;
			return 0.5f + 0.5f * std::cos(a);
		}
		case READ_HUE_YB: {
			const float a = channelAt(picture, x, y, CH_HUE) * 2.f * (float) M_PI;
			return 0.5f + 0.5f * std::sin(a);
		}
		default: break;
	}
	return channelAround(picture, x, y, CH_VAL, radius);
}


void PathTable::build(const Picture& picture, const Path& path, int which) {
	pictureGeneration = picture.generation;
	pathGeneration = path.generation;
	reading = which;
	circular = (path.kind == Path::CIRCLE);

	total.clear();
	if (!picture.has())
		return;

	// ONE ENTRY PER PICTURE PIXEL ALONG THE PATH. Finer than that reads the same pixel twice and
	// buys nothing; coarser throws away detail the picture has. A circle three hundred pixels
	// across gives about nine hundred and forty entries.
	const float len = path.length();
	int n = (int) std::lround((double) len * (double) PICTURE_SIDE);
	n = std::max(8, std::min(n, 8 * PICTURE_SIDE));

	// THE DISC EACH ENTRY IS AVERAGED OVER is half the spacing between entries, so neighbouring
	// entries overlap slightly and the path is read as a band rather than as a thread.
	const float radius = (n > 0) ? (len / (float) n) * 0.5f : 0.f;

	// THE ENTRIES FIRST, then smoothed, then added up. The running total has to be the integral
	// of exactly the signal a narrow read returns, or the two disagree by half a step — which is
	// what the slow end and the fast end disagreeing by half a step actually was.
	std::vector<float> value((size_t) n);
	for (int i = 0; i < n; i++) {
		// The MIDDLE of each step, not its edge: an entry stands for the stretch around it.
		const double t = ((double) i + 0.5) / (double) n;
		float x = 0.f, y = 0.f;
		path.at(t, &x, &y);
		value[(size_t) i] = pathReadingAt(picture, x, y, which, radius);
	}

	// A LIGHT SMOOTHING ACROSS THREE ENTRIES, wrapped for a circle and held at the ends of a line.
	// An entry is a picture pixel, and stepping from one pixel to the next is a staircase however
	// small the steps; a quarter, a half and a quarter takes the corners off it without blurring
	// anything the picture actually has. This is what keeps a slow sweep smooth; the arc average
	// is what keeps a fast one clean.
	std::vector<float> smooth((size_t) n);
	for (int i = 0; i < n; i++) {
		int a = i - 1, b = i + 1;
		if (circular) {
			a = ((a % n) + n) % n;
			b = ((b % n) + n) % n;
		}
		else {
			a = std::max(0, a);
			b = std::min(n - 1, b);
		}
		smooth[(size_t) i] = 0.25f * value[(size_t) a] + 0.5f * value[(size_t) i]
			+ 0.25f * value[(size_t) b];
	}

	total.resize((size_t) n + 1);
	total[0] = 0.0;
	for (int i = 0; i < n; i++)
		total[(size_t) i + 1] = total[(size_t) i] + (double) smooth[(size_t) i];
}


void PathTable::refresh(const Picture& picture, const Path& path, int which) {
	if (ready() && pictureGeneration == picture.generation
			&& pathGeneration == path.generation && reading == which)
		return;
	build(picture, path, which);
}


/** The running total at a fractional place, in entries from the start. Linear between entries,
which is what makes the box average degenerate to interpolation as its width goes to nothing. */
static double totalAt(const std::vector<double>& total, double at) {
	const int n = (int) total.size() - 1;
	if (n <= 0)
		return 0.f;
	if (at <= 0.0)
		return total[0];
	if (at >= (double) n)
		return total[(size_t) n];
	const int i = (int) at;
	const double f = at - (double) i;
	return total[(size_t) i] + f * (total[(size_t) i + 1] - total[(size_t) i]);
}


/** THE VALUE AT A PLACE, for a read so narrow there is no arc to average: the entries either
side of it, blended. Each entry stands for the middle of its step, which is the half-entry shift.

INTERPOLATED, where a wider read is not. A box average of some width returns the mean of the
entries it covers and so steps from entry to entry as it moves; at no width at all there is
nothing to average and a staircase would be heard as stepping in a slow sweep. The two therefore
differ by up to half an entry, which is half a picture pixel's worth of change — far below
anything the box average is there to deal with. */
static float valueAt(const std::vector<double>& total, double at, bool circular) {
	const int n = (int) total.size() - 1;
	if (n <= 0)
		return 0.f;
	const double p = at - 0.5;
	int i = (int) std::floor(p);
	double f = p - (double) i;
	int j = i + 1;
	if (circular) {
		i = ((i % n) + n) % n;
		j = ((j % n) + n) % n;
	}
	else {
		i = std::max(0, std::min(i, n - 1));
		j = std::max(0, std::min(j, n - 1));
		if (at <= 0.0 || at >= (double) n)
			f = 0.0;
	}
	const double a = total[(size_t) i + 1] - total[(size_t) i];
	const double b = total[(size_t) j + 1] - total[(size_t) j];
	return (float) (a + f * (b - a));
}


float PathTable::read(double from, double to) const {
	const int n = points();
	if (n <= 1)
		return 0.f;

	if (to < from)
		std::swap(from, to);
	const double width = to - from;

	if (circular) {
		// A WHOLE TURN OR MORE IN ONE SAMPLE is the mean of the whole path: there is nothing
		// finer left to say, and the arithmetic below would otherwise wrap over itself.
		if (width >= 1.0)
			return (float) (total[(size_t) n] / (double) n);
		double a = std::fmod(from, 1.0);
		if (a < 0.0)
			a += 1.0;
		const double pa = a * (double) n;
		const double pb = pa + width * (double) n;
		if (pb - pa < 1e-9)
			return valueAt(total, pa, true);
		if (pb <= (double) n)
			return (float) ((totalAt(total, pb) - totalAt(total, pa)) / (pb - pa));
		// IT CROSSES THE SEAM: what is left of the path, plus as much again from its start.
		const double head = totalAt(total, (double) n) - totalAt(total, pa);
		const double tail = totalAt(total, pb - (double) n) - totalAt(total, 0.0);
		return (float) ((head + tail) / (pb - pa));
	}

	// A LINE DOES NOT WRAP. Past either end it holds at that end, which is what a head waiting to
	// be sent back to the start should read.
	const double pa = std::max(0.0, std::min(from, 1.0)) * (double) n;
	const double pb = std::max(0.0, std::min(to, 1.0)) * (double) n;
	if (pb - pa < 1e-9)
		return valueAt(total, pa, false);
	return (float) ((totalAt(total, pb) - totalAt(total, pa)) / (pb - pa));
}


} // namespace px
