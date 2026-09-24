/** See Picture.hpp. */
#include "Picture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

// DECODED HERE AND NOWHERE ELSE. The implementation is compiled into this file and kept to it, so
// the plugin carries its own copy rather than reaching for whatever Rack happens to have linked.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_STDIO
#include <stb_image.h>

namespace px {


static const char* B64 =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


std::string bytesToText(const std::vector<uint8_t>& bytes) {
	std::string out;
	out.reserve((bytes.size() + 2) / 3 * 4);
	size_t i = 0;
	while (i + 2 < bytes.size()) {
		const uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
		out += B64[(n >> 18) & 63];
		out += B64[(n >> 12) & 63];
		out += B64[(n >> 6) & 63];
		out += B64[n & 63];
		i += 3;
	}
	const size_t left = bytes.size() - i;
	if (left == 1) {
		const uint32_t n = bytes[i] << 16;
		out += B64[(n >> 18) & 63];
		out += B64[(n >> 12) & 63];
		out += "==";
	}
	else if (left == 2) {
		const uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8);
		out += B64[(n >> 18) & 63];
		out += B64[(n >> 12) & 63];
		out += B64[(n >> 6) & 63];
		out += '=';
	}
	return out;
}


static int valueOf(char c) {
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}


std::vector<uint8_t> textToBytes(const std::string& text) {
	std::vector<uint8_t> out;
	out.reserve(text.size() / 4 * 3);
	uint32_t n = 0;
	int have = 0;
	for (char c : text) {
		const int v = valueOf(c);
		if (v < 0)
			continue;   // padding, a newline, anything else
		n = (n << 6) | (uint32_t) v;
		have += 6;
		if (have >= 8) {
			have -= 8;
			out.push_back((uint8_t) ((n >> have) & 0xff));
		}
	}
	return out;
}


/** SQUARED OFF BY AVERAGING, not by taking every nth pixel. A photograph reduced by sampling
alone shimmers as anything moves over it and loses the small things entirely; averaging the block
that each new pixel stands for keeps what was there. */
static void square(const uint8_t* src, int w, int h, std::vector<uint8_t>& out) {
	out.assign((size_t) PICTURE_SIDE * PICTURE_SIDE * 4, 0);
	if (w <= 0 || h <= 0)
		return;
	for (int y = 0; y < PICTURE_SIDE; y++) {
		const int y0 = (int) ((int64_t) y * h / PICTURE_SIDE);
		int y1 = (int) ((int64_t) (y + 1) * h / PICTURE_SIDE);
		if (y1 <= y0)
			y1 = y0 + 1;
		for (int x = 0; x < PICTURE_SIDE; x++) {
			const int x0 = (int) ((int64_t) x * w / PICTURE_SIDE);
			int x1 = (int) ((int64_t) (x + 1) * w / PICTURE_SIDE);
			if (x1 <= x0)
				x1 = x0 + 1;
			uint32_t sum[4] = {0, 0, 0, 0};
			uint32_t count = 0;
			for (int sy = y0; sy < y1 && sy < h; sy++) {
				const uint8_t* row = src + ((size_t) sy * w + x0) * 4;
				for (int sx = x0; sx < x1 && sx < w; sx++) {
					sum[0] += row[0];
					sum[1] += row[1];
					sum[2] += row[2];
					sum[3] += row[3];
					row += 4;
					count++;
				}
			}
			uint8_t* to = &out[((size_t) y * PICTURE_SIDE + x) * 4];
			if (count == 0)
				count = 1;
			for (int c = 0; c < 4; c++)
				to[c] = (uint8_t) (sum[c] / count);
		}
	}
}


/** THE EDGE-AWARE BLUR, run over the luminance in place.

Gastál and Oliveira's domain transform, in its recursive form. The trick of it is that the distance
between two neighbouring pixels is stretched by how different they are, so the filter's reach runs
out at a boundary: it smooths a sky without smearing the roof line into it. Three iterations, each
finer than the last, and each pass run in both directions so nothing is smeared one way. */
static void edgeAwareBlur(std::vector<float>& lum, int side, float sigmaS, float sigmaR) {
	const int n = side * side;
	std::vector<float> dRow((size_t) n, 1.f), dCol((size_t) n, 1.f);
	const float ratio = sigmaS / sigmaR;

	for (int y = 0; y < side; y++) {
		for (int x = 1; x < side; x++) {
			const int i = y * side + x;
			dRow[i] = 1.f + ratio * std::fabs(lum[i] - lum[i - 1]);
		}
	}
	for (int y = 1; y < side; y++) {
		for (int x = 0; x < side; x++) {
			const int i = y * side + x;
			dCol[i] = 1.f + ratio * std::fabs(lum[i] - lum[i - side]);
		}
	}

	const int passes = 3;
	const float spread = std::sqrt(std::pow(4.f, (float) passes) - 1.f);
	for (int iter = 1; iter <= passes; iter++) {
		const float sigmaH = sigmaS * std::sqrt(3.f)
			* std::pow(2.f, (float) (passes - iter)) / spread;
		const float a = std::exp(-std::sqrt(2.f) / sigmaH);

		for (int y = 0; y < side; y++) {
			float* row = &lum[(size_t) y * side];
			const float* d = &dRow[(size_t) y * side];
			for (int x = 1; x < side; x++) {
				const float v = std::pow(a, d[x]);
				row[x] += v * (row[x - 1] - row[x]);
			}
			for (int x = side - 2; x >= 0; x--) {
				const float v = std::pow(a, d[x + 1]);
				row[x] += v * (row[x + 1] - row[x]);
			}
		}
		for (int x = 0; x < side; x++) {
			for (int y = 1; y < side; y++) {
				const int i = y * side + x;
				const float v = std::pow(a, dCol[i]);
				lum[i] += v * (lum[i - side] - lum[i]);
			}
			for (int y = side - 2; y >= 0; y--) {
				const int i = y * side + x;
				const float v = std::pow(a, dCol[i + side]);
				lum[i] += v * (lum[i + side] - lum[i]);
			}
		}
	}
}


void dimGlare(const std::vector<uint8_t>& rgba, std::vector<uint8_t>& out,
		float blurRadius, float threshold, float maxAtten) {
	out = rgba;
	const int side = PICTURE_SIDE;
	if (out.size() != (size_t) side * side * 4 || maxAtten >= 1.f)
		return;

	// Rec. 709 weights, on the numbers as they are stored. Not strictly right — they are gamma
	// encoded — but this decides how much to dim a region, not what colour it is.
	std::vector<float> lum((size_t) side * side, 0.f);
	for (size_t p = 0; p < lum.size(); p++) {
		const uint8_t* px = &rgba[p * 4];
		lum[p] = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
	}

	edgeAwareBlur(lum, side, blurRadius, 0.2f * 255.f);

	const float denom = std::max(1e-6f, 1.f - threshold);
	const float range = 1.f - maxAtten;
	for (size_t p = 0; p < lum.size(); p++) {
		const float bright = lum[p] / 255.f;
		if (bright <= threshold)
			continue;
		float t = (bright - threshold) / denom;
		if (t > 1.f)
			t = 1.f;
		// Smoothstep, so the dimming eases in rather than starting with a step at the threshold.
		const float s = t * t * (3.f - 2.f * t);
		const float factor = 1.f - s * range;
		uint8_t* px = &out[p * 4];
		for (int c = 0; c < 3; c++)
			px[c] = (uint8_t) std::lround(px[c] * factor);
	}
}


bool Picture::decode(const std::vector<uint8_t>& bytes, const std::string& called) {
	if (bytes.empty())
		return false;
	int w = 0, h = 0, channels = 0;
	uint8_t* pixels = stbi_load_from_memory(bytes.data(), (int) bytes.size(), &w, &h, &channels,
		4);
	if (!pixels)
		return false;
	square(pixels, w, h, rgba);
	stbi_image_free(pixels);
	file = bytes;
	name = called;
	generation++;
	return true;
}


bool Picture::load(const std::string& path) {
	std::FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> bytes;
	if (size > 0) {
		bytes.resize((size_t) size);
		if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size())
			bytes.clear();
	}
	std::fclose(f);
	if (bytes.empty())
		return false;
	return decode(bytes, system::getFilename(path));
}


void Picture::clear() {
	file.clear();
	rgba.clear();
	name.clear();
	generation++;
}


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


} // namespace px
