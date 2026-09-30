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



// ---- what has been loaded before ---------------------------------------------------------

/** Box-filtered down from the square, the same way the square itself was made: point sampling a
photograph down to forty-eight pixels loses whatever it was of. */
void pictureThumbnail(const std::vector<uint8_t>& rgba, std::vector<uint8_t>& out) {
	out.assign((size_t) THUMB_SIDE * THUMB_SIDE * 4, 0);
	if (rgba.size() < (size_t) PICTURE_SIDE * PICTURE_SIDE * 4)
		return;
	const int block = PICTURE_SIDE / THUMB_SIDE;
	for (int y = 0; y < THUMB_SIDE; y++) {
		for (int x = 0; x < THUMB_SIDE; x++) {
			uint32_t sum[4] = {0, 0, 0, 0};
			uint32_t count = 0;
			for (int sy = y * block; sy < (y + 1) * block && sy < PICTURE_SIDE; sy++) {
				const uint8_t* row = &rgba[((size_t) sy * PICTURE_SIDE + x * block) * 4];
				for (int sx = 0; sx < block; sx++) {
					sum[0] += row[0]; sum[1] += row[1]; sum[2] += row[2]; sum[3] += row[3];
					row += 4;
					count++;
				}
			}
			if (count == 0)
				count = 1;
			uint8_t* to = &out[((size_t) y * THUMB_SIDE + x) * 4];
			for (int c = 0; c < 4; c++)
				to[c] = (uint8_t) (sum[c] / count);
		}
	}
}


static const size_t PICTURE_HISTORY = PICTURE_KEPT;
static std::vector<PictureMemory> gHistory;
static std::string gFolder;
static bool gMemoryRead = false;

static std::string memoryPath() {
	return asset::user(layoutFolder + "/pictures.json");
}

static std::string keptFolder() {
	return asset::user(layoutFolder + "/pictures");
}

std::string pictureKeepCopy(const std::string& name, const std::vector<uint8_t>& bytes) {
	if (bytes.empty())
		return "";
	system::createDirectories(keptFolder());
	std::string base = name.empty() ? "picture" : name;
	// Anything a file system would rather not see, taken out.
	for (char& c : base) {
		if (c == '/' || c == '\\' || c == ':')
			c = '-';
	}
	std::string path = keptFolder() + "/" + base;
	// A SECOND PICTURE OF THE SAME NAME IS A SECOND PICTURE, so it is numbered rather than being
	// written over — two photographs both called image.jpg is the commonest thing in the world.
	if (system::isFile(path)) {
		const std::string stem = system::getStem(base);
		const std::string ext = system::getExtension(base);
		for (int n = 2; n < 1000; n++) {
			const std::string tryPath = keptFolder() + "/" + stem + string::f(" %d", n) + ext;
			if (!system::isFile(tryPath)) {
				path = tryPath;
				break;
			}
		}
	}
	std::FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
		return "";
	const size_t put = std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
	if (put != bytes.size()) {
		system::remove(path);
		return "";
	}
	return path;
}

static std::string thumbFolder() {
	return asset::user(layoutFolder + "/thumbs");
}

/** A NAME FOR A THUMBNAIL'S FILE, made from the picture's path. The same picture always gets the
same name, so re-loading it overwrites its own thumbnail rather than leaving another behind. */
static std::string thumbName(const std::string& path) {
	uint64_t h = 1469598103934665603ull;
	for (char c : path) {
		h ^= (uint64_t) (unsigned char) c;
		h *= 1099511628211ull;
	}
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%016llx.rgba", (unsigned long long) h);
	return buf;
}

bool pictureThumb(const PictureMemory& memory, std::vector<uint8_t>& out) {
	if (memory.thumbFile.empty())
		return false;
	std::FILE* f = std::fopen((thumbFolder() + "/" + memory.thumbFile).c_str(), "rb");
	if (!f)
		return false;
	out.assign((size_t) THUMB_SIDE * THUMB_SIDE * 4, 0);
	const size_t got = std::fread(out.data(), 1, out.size(), f);
	std::fclose(f);
	if (got != out.size()) {
		out.clear();
		return false;
	}
	return true;
}

void pictureMemoryLoad() {
	if (gMemoryRead)
		return;
	gMemoryRead = true;
	FILE* file = std::fopen(memoryPath().c_str(), "r");
	if (!file)
		return;
	json_error_t error;
	json_t* rootJ = json_loadf(file, 0, &error);
	std::fclose(file);
	if (!rootJ)
		return;
	if (const char* folder = json_string_value(json_object_get(rootJ, "folder")))
		gFolder = folder;
	json_t* listJ = json_object_get(rootJ, "recent");
	if (json_is_array(listJ)) {
		size_t i;
		json_t* oneJ;
		json_array_foreach(listJ, i, oneJ) {
			PictureMemory m;
			if (const char* v = json_string_value(json_object_get(oneJ, "path")))
				m.path = v;
			if (const char* v = json_string_value(json_object_get(oneJ, "name")))
				m.name = v;
			if (const char* v = json_string_value(json_object_get(oneJ, "thumb")))
				m.thumbFile = v;
			if (!m.path.empty() && gHistory.size() < PICTURE_HISTORY)
				gHistory.push_back(m);
		}
	}
	json_decref(rootJ);
}

static void memorySave() {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "folder", json_string(gFolder.c_str()));
	json_t* listJ = json_array();
	for (const PictureMemory& m : gHistory) {
		json_t* oneJ = json_object();
		json_object_set_new(oneJ, "path", json_string(m.path.c_str()));
		json_object_set_new(oneJ, "name", json_string(m.name.c_str()));
		if (!m.thumbFile.empty())
			json_object_set_new(oneJ, "thumb", json_string(m.thumbFile.c_str()));
		json_array_append_new(listJ, oneJ);
	}
	json_object_set_new(rootJ, "recent", listJ);
	system::createDirectories(asset::user(layoutFolder));
	FILE* file = std::fopen(memoryPath().c_str(), "w");
	if (file) {
		json_dumpf(rootJ, file, JSON_INDENT(2));
		std::fclose(file);
	}
	json_decref(rootJ);
}

std::string pictureFolder() {
	pictureMemoryLoad();
	return gFolder;
}

const std::vector<PictureMemory>& pictureHistory() {
	pictureMemoryLoad();
	return gHistory;
}

void pictureRemember(const std::string& path, const std::string& name,
		const std::vector<uint8_t>& rgba) {
	pictureMemoryLoad();
	gFolder = system::getDirectory(path);
	// THE SAME PICTURE TWICE IS ONE ENTRY, moved to the front. A list that fills up with four
	// copies of the picture you are working on is a list of one picture.
	for (size_t i = 0; i < gHistory.size(); i++) {
		if (gHistory[i].path == path) {
			gHistory.erase(gHistory.begin() + i);
			break;
		}
	}
	PictureMemory m;
	m.path = path;
	m.name = name.empty() ? system::getFilename(path) : name;
	std::vector<uint8_t> thumb;
	pictureThumbnail(rgba, thumb);
	if (!thumb.empty()) {
		system::createDirectories(thumbFolder());
		m.thumbFile = thumbName(path);
		std::FILE* f = std::fopen((thumbFolder() + "/" + m.thumbFile).c_str(), "wb");
		if (f) {
			std::fwrite(thumb.data(), 1, thumb.size(), f);
			std::fclose(f);
		}
		else {
			m.thumbFile.clear();
		}
	}
	gHistory.insert(gHistory.begin(), m);
	if (gHistory.size() > PICTURE_HISTORY)
		gHistory.resize(PICTURE_HISTORY);
	memorySave();
}

void pictureMemoryClear() {
	pictureMemoryLoad();
	for (const PictureMemory& m : gHistory) {
		if (!m.thumbFile.empty())
			system::remove(thumbFolder() + "/" + m.thumbFile);
	}
	gHistory.clear();
	memorySave();
}


} // namespace px
