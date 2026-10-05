/** READING A PICTURE, AND NOTHING ELSE.

Apart from Picture.cpp, which loads files, remembers them and needs Rack's folders to do it. This
half needs none of that: a pixel, a colour conversion and an average. Kept separate so that the
reading along a path can be built and tested on its own — see test/pathread.cpp — because a fault
in it is inaudible until it is a tone full of noise.
*/
#include "Picture.hpp"

#include <algorithm>
#include <cmath>

namespace px {


void Picture::at(float x, float y, uint8_t* out) const {
	if (rgba.empty()) {
		out[0] = out[1] = out[2] = 0;
		out[3] = 255;
		return;
	}
	int px = (int) std::floor(x * PICTURE_SIDE);
	int py = (int) std::floor(y * PICTURE_SIDE);
	px = ((px % PICTURE_SIDE) + PICTURE_SIDE) % PICTURE_SIDE;
	py = ((py % PICTURE_SIDE) + PICTURE_SIDE) % PICTURE_SIDE;
	const uint8_t* at = &rgba[((size_t) py * PICTURE_SIDE + px) * 4];
	out[0] = at[0];
	out[1] = at[1];
	out[2] = at[2];
	out[3] = at[3];
}


const char* CHANNEL_NAMES[NUM_CHANNELS] = {
	"Hue", "Saturation", "Value", "Red", "Green", "Blue"
};


void pictureToHsv(float r, float g, float b, float* h, float* s, float* v) {
	const float hi = std::max(r, std::max(g, b));
	const float lo = std::min(r, std::min(g, b));
	const float d = hi - lo;
	*v = hi;
	*s = (hi > 0.f) ? d / hi : 0.f;
	if (d <= 0.f) {
		*h = 0.f;
		return;
	}
	float hue;
	if (hi == r)
		hue = (g - b) / d;
	else if (hi == g)
		hue = 2.f + (b - r) / d;
	else
		hue = 4.f + (r - g) / d;
	hue /= 6.f;
	if (hue < 0.f)
		hue += 1.f;
	*h = hue;
}


float channelAt(const Picture& picture, float x, float y, int which) {
	uint8_t px[4];
	picture.at(x, y, px);
	const float r = px[0] / 255.f, g = px[1] / 255.f, b = px[2] / 255.f;
	switch (which) {
		case CH_RED: return r;
		case CH_GREEN: return g;
		case CH_BLUE: return b;
		default: break;
	}
	float h = 0.f, s = 0.f, v = 0.f;
	pictureToHsv(r, g, b, &h, &s, &v);
	if (which == CH_HUE) return h;
	if (which == CH_SAT) return s;
	return v;
}


float channelAround(const Picture& picture, float x, float y, int which, float radius) {
	if (!(radius > 0.f))
		return channelAt(picture, x, y, which);
	// FIVE READINGS, NOT A DISC OF THEM. The centre and four at the radius, which is enough to
	// stop the answer depending on which pixel row was grazed and cheap enough to do per sample.
	// A hue is an angle and must not be averaged this way; its two axes are averaged instead, by
	// whoever asked for them.
	const float a = channelAt(picture, x, y, which);
	const float b = channelAt(picture, x + radius, y, which);
	const float c = channelAt(picture, x - radius, y, which);
	const float d = channelAt(picture, x, y + radius, which);
	const float e = channelAt(picture, x, y - radius, which);
	return (a + b + c + d + e) * 0.2f;
}



} // namespace px
