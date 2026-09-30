/** See GuitarPro.hpp. */
#include "GuitarPro.hpp"

#include <cstdio>
#include <cstring>
#include <cmath>

namespace px {


// ---- DEFLATE ---------------------------------------------------------------------------------
//
// A zip's entries are deflated, and this is the whole of what is needed to read one. Written here
// rather than linked, because this file is built both into the plugin and into a command-line
// program, and a dependency that has to be present in both places is a dependency in the way.
//
// EVERY READ IS BOUNDED. A malformed stream returns false; nothing reads past the end of the
// input or writes past the end of the output.

namespace {

struct Bits {
	const uint8_t* data;
	size_t size;
	size_t at = 0;        /**< Which byte. */
	int bit = 0;          /**< Which bit of it, from the bottom. */
	bool bad = false;

	int get() {
		if (at >= size) {
			bad = true;
			return 0;
		}
		const int b = (data[at] >> bit) & 1;
		if (++bit == 8) {
			bit = 0;
			at++;
		}
		return b;
	}

	/** `n` bits, least significant first, which is how DEFLATE writes its numbers. */
	uint32_t get(int n) {
		uint32_t v = 0;
		for (int i = 0; i < n; i++)
			v |= (uint32_t) get() << i;
		return v;
	}

	void align() {
		if (bit) {
			bit = 0;
			at++;
		}
	}
};

/** A canonical Huffman table, as DEFLATE describes one: the code lengths, and nothing else. */
struct Huff {
	static const int MAX_BITS = 15;
	int count[MAX_BITS + 1];
	std::vector<int> symbols;

	bool build(const uint8_t* lengths, int n) {
		for (int i = 0; i <= MAX_BITS; i++)
			count[i] = 0;
		for (int i = 0; i < n; i++) {
			if (lengths[i] > MAX_BITS)
				return false;
			count[lengths[i]]++;
		}
		count[0] = 0;
		int offsets[MAX_BITS + 2];
		offsets[1] = 0;
		for (int i = 1; i <= MAX_BITS; i++)
			offsets[i + 1] = offsets[i] + count[i];
		symbols.assign((size_t) n, 0);
		for (int i = 0; i < n; i++) {
			if (lengths[i])
				symbols[(size_t) offsets[lengths[i]]++] = i;
		}
		return true;
	}

	int decode(Bits& in) const {
		int code = 0, first = 0, index = 0;
		for (int len = 1; len <= MAX_BITS; len++) {
			code |= in.get();
			if (in.bad)
				return -1;
			const int n = count[len];
			if (code - first < n)
				return symbols[(size_t) (index + (code - first))];
			index += n;
			first = (first + n) << 1;
			code <<= 1;
		}
		return -1;
	}
};

const int LENGTH_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43,
	51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int LENGTH_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
	4, 4, 4, 4, 5, 5, 5, 5, 0};
const int DIST_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
	513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
	9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflateBlock(Bits& in, const Huff& lit, const Huff& dist, std::vector<uint8_t>& out) {
	for (;;) {
		const int sym = lit.decode(in);
		if (sym < 0)
			return false;
		if (sym < 256) {
			out.push_back((uint8_t) sym);
			continue;
		}
		if (sym == 256)
			return true;
		const int l = sym - 257;
		if (l >= 29)
			return false;
		const int length = LENGTH_BASE[l] + (int) in.get(LENGTH_EXTRA[l]);
		const int d = dist.decode(in);
		if (d < 0 || d >= 30)
			return false;
		const int distance = DIST_BASE[d] + (int) in.get(DIST_EXTRA[d]);
		if (in.bad || distance <= 0 || (size_t) distance > out.size())
			return false;
		const size_t from = out.size() - (size_t) distance;
		for (int i = 0; i < length; i++)
			out.push_back(out[from + (size_t) i]);
	}
}

} // namespace


bool gpInflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
	Bits in;
	in.data = data;
	in.size = size;
	out.clear();

	for (;;) {
		const int last = in.get();
		const uint32_t kind = in.get(2);
		if (in.bad)
			return false;

		if (kind == 0) {
			// Stored: a length, its complement, and the bytes.
			in.align();
			if (in.at + 4 > in.size)
				return false;
			const uint32_t len = (uint32_t) in.data[in.at] | ((uint32_t) in.data[in.at + 1] << 8);
			in.at += 4;
			if (in.at + len > in.size)
				return false;
			out.insert(out.end(), in.data + in.at, in.data + in.at + len);
			in.at += len;
		}
		else if (kind == 1) {
			// The fixed tables, which DEFLATE spells out.
			uint8_t litLengths[288], distLengths[30];
			for (int i = 0; i < 288; i++)
				litLengths[i] = (uint8_t) (i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
			for (int i = 0; i < 30; i++)
				distLengths[i] = 5;
			Huff lit, dist;
			if (!lit.build(litLengths, 288) || !dist.build(distLengths, 30))
				return false;
			if (!inflateBlock(in, lit, dist, out))
				return false;
		}
		else if (kind == 2) {
			// The tables are in the stream, themselves Huffman-coded.
			const int nLit = (int) in.get(5) + 257;
			const int nDist = (int) in.get(5) + 1;
			const int nLen = (int) in.get(4) + 4;
			if (in.bad || nLit > 288 || nDist > 30)
				return false;
			static const int ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13,
				2, 14, 1, 15};
			uint8_t lenLengths[19];
			std::memset(lenLengths, 0, sizeof(lenLengths));
			for (int i = 0; i < nLen; i++)
				lenLengths[ORDER[i]] = (uint8_t) in.get(3);
			Huff lenHuff;
			if (in.bad || !lenHuff.build(lenLengths, 19))
				return false;

			uint8_t lengths[288 + 30];
			std::memset(lengths, 0, sizeof(lengths));
			int have = 0;
			while (have < nLit + nDist) {
				const int sym = lenHuff.decode(in);
				if (sym < 0)
					return false;
				if (sym < 16) {
					lengths[have++] = (uint8_t) sym;
				}
				else if (sym == 16) {
					if (have == 0)
						return false;
					const int n = 3 + (int) in.get(2);
					const uint8_t v = lengths[have - 1];
					for (int i = 0; i < n && have < nLit + nDist; i++)
						lengths[have++] = v;
				}
				else {
					const int n = (sym == 17) ? 3 + (int) in.get(3) : 11 + (int) in.get(7);
					for (int i = 0; i < n && have < nLit + nDist; i++)
						lengths[have++] = 0;
				}
				if (in.bad)
					return false;
			}
			Huff lit, dist;
			if (!lit.build(lengths, nLit) || !dist.build(lengths + nLit, nDist))
				return false;
			if (!inflateBlock(in, lit, dist, out))
				return false;
		}
		else {
			return false;
		}

		if (last)
			return true;
		if (in.bad)
			return false;
	}
}


// ---- the zip a .gp file is --------------------------------------------------------------------

namespace {

uint32_t le32(const uint8_t* p) {
	return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16)
		| ((uint32_t) p[3] << 24);
}

uint16_t le16(const uint8_t* p) {
	return (uint16_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8));
}

/** ONE NAMED ENTRY OUT OF A ZIP.

FROM THE CENTRAL DIRECTORY AT THE END, not by walking the local headers from the front. The files
Guitar Pro writes set the third flag on every entry, which means the sizes in the local header are
zeros and the real ones follow the data — so the front walk cannot tell where one entry stops and
the next begins without decoding everything on the way. The directory at the end has the sizes and
the offsets.

The walk from the front is kept for a file whose directory is missing or wrong, which happens to
files that have been through several tools and a download. */
bool zipFromDirectory(const std::vector<uint8_t>& zip, const std::string& want,
		std::vector<uint8_t>& out) {
	// The end record is the last thing in the file, after a comment of up to 64k.
	if (zip.size() < 22)
		return false;
	size_t end = 0;
	bool found = false;
	const size_t lowest = (zip.size() > 66000) ? zip.size() - 66000 : 0;
	for (size_t i = zip.size() - 22 + 1; i-- > lowest;) {
		if (le32(zip.data() + i) == 0x06054b50u) {
			end = i;
			found = true;
			break;
		}
	}
	if (!found)
		return false;

	const uint16_t count = le16(zip.data() + end + 10);
	const uint32_t dirSize = le32(zip.data() + end + 12);
	const uint32_t dirAt = le32(zip.data() + end + 16);
	if ((size_t) dirAt + dirSize > zip.size())
		return false;

	size_t at = dirAt;
	for (int i = 0; i < (int) count; i++) {
		if (at + 46 > zip.size())
			return false;
		const uint8_t* h = zip.data() + at;
		if (le32(h) != 0x02014b50u)
			return false;
		const uint16_t method = le16(h + 10);
		const uint32_t compressed = le32(h + 20);
		const uint32_t plain = le32(h + 24);
		const uint16_t nameLen = le16(h + 28);
		const uint16_t extraLen = le16(h + 30);
		const uint16_t commentLen = le16(h + 32);
		const uint32_t localAt = le32(h + 42);
		if (at + 46 + nameLen > zip.size())
			return false;
		const std::string name((const char*) h + 46, nameLen);
		at += 46 + nameLen + extraLen + commentLen;

		if (name != want)
			continue;
		// The local header again, for the name and extra lengths as IT wrote them: they may
		// differ from the directory's.
		if ((size_t) localAt + 30 > zip.size())
			return false;
		const uint8_t* l = zip.data() + localAt;
		if (le32(l) != 0x04034b50u)
			return false;
		const size_t dataAt = (size_t) localAt + 30 + le16(l + 26) + le16(l + 28);
		if (dataAt + compressed > zip.size())
			return false;
		if (method == 0) {
			out.assign(zip.begin() + (long) dataAt, zip.begin() + (long) (dataAt + compressed));
			return true;
		}
		if (method != 8)
			return false;
		if (!gpInflate(zip.data() + dataAt, compressed, out))
			return false;
		return plain == 0 || out.size() == plain;
	}
	return false;
}

/** The same, walking the local headers from the front. Only usable where the headers carry their
sizes, which is why it is the fallback rather than the way in. */
bool zipFromFront(const std::vector<uint8_t>& zip, const std::string& want,
		std::vector<uint8_t>& out) {
	size_t at = 0;
	while (at + 30 <= zip.size()) {
		const uint8_t* h = zip.data() + at;
		if (le32(h) != 0x04034b50u)
			break;
		const uint16_t flags = le16(h + 6);
		const uint16_t method = le16(h + 8);
		const uint32_t compressed = le32(h + 18);
		const uint32_t plain = le32(h + 22);
		const uint16_t nameLen = le16(h + 26);
		const uint16_t extraLen = le16(h + 28);
		const size_t nameAt = at + 30;
		if (nameAt + nameLen + extraLen > zip.size())
			return false;
		const std::string name((const char*) zip.data() + nameAt, nameLen);
		const size_t dataAt = nameAt + nameLen + extraLen;
		if ((flags & 0x8) && compressed == 0)
			return false;
		if (dataAt + compressed > zip.size())
			return false;
		if (name == want) {
			if (method == 0) {
				out.assign(zip.begin() + (long) dataAt,
					zip.begin() + (long) (dataAt + compressed));
				return true;
			}
			if (method != 8)
				return false;
			if (!gpInflate(zip.data() + dataAt, compressed, out))
				return false;
			return plain == 0 || out.size() == plain;
		}
		at = dataAt + compressed;
	}
	return false;
}

bool zipEntry(const std::vector<uint8_t>& zip, const std::string& want,
		std::vector<uint8_t>& out) {
	return zipFromDirectory(zip, want, out) || zipFromFront(zip, want, out);
}

} // namespace


// ---- the Guitar Pro 6 container ----------------------------------------------------------------
//
// A .gpx file is two layers around the same gpif XML that a .gp file holds in a zip.
//
// BCFZ is the outer layer: a bit stream of two kinds of chunk. A one bit opens a back reference —
// a four bit word size, then an offset and a length of that many bits each, both written low bit
// first — and the named run is copied from what has already been produced. A nought bit opens a
// literal run of up to three bytes. Nothing else: no entropy coding and no checksum.
//
// BCFS is the inner layer: a filesystem of 4096 byte sectors. A sector whose first int is 2 is a
// directory entry naming one file, giving its length and listing the sectors holding it; every
// other sector is one of those blocks. Everything is placed from the end of the four byte magic,
// so a sector's own position is four bytes past the multiple of 4096.
//
// Ported from alphaTab, which is the only description of either layer.

namespace {

struct BcfzBits {
	const uint8_t* data = nullptr;
	size_t size = 0;
	size_t at = 0;
	int bit = 0;          /**< Which bit of the byte, from the top. */
	bool bad = false;

	int get() {
		if (at >= size) {
			bad = true;
			return 0;
		}
		const int b = (data[at] >> (7 - bit)) & 1;
		if (++bit == 8) {
			bit = 0;
			at++;
		}
		return b;
	}
	/** High bit first, which is how the word size and the literal bytes are written. */
	uint32_t read(int count) {
		uint32_t v = 0;
		for (int i = 0; i < count; i++)
			v = (v << 1) | (uint32_t) get();
		return v;
	}
	/** Low bit first, which is how the offset and the length are written. */
	uint32_t readReversed(int count) {
		uint32_t v = 0;
		for (int i = 0; i < count; i++)
			v |= (uint32_t) get() << i;
		return v;
	}
};

uint32_t le32(const std::vector<uint8_t>& b, size_t at) {
	if (at + 4 > b.size())
		return 0;
	return (uint32_t) b[at] | ((uint32_t) b[at + 1] << 8) | ((uint32_t) b[at + 2] << 16)
		| ((uint32_t) b[at + 3] << 24);
}

bool bcfzInflate(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
	if (in.size() < 8)
		return false;
	const uint32_t expected = le32(in, 4);
	if (expected == 0 || expected > (64u << 20))   // A LENGTH SOMEBODY ELSE WROTE.
		return false;

	BcfzBits bits;
	bits.data = in.data() + 8;
	bits.size = in.size() - 8;
	out.reserve(expected);
	// A chunk that produces nothing would spin here for as long as the stream lasts, so the
	// number of chunks is bounded as well as the input.
	const size_t limit = (size_t) expected * 2 + 1024;
	for (size_t round = 0; round < limit && !bits.bad && out.size() < expected; round++) {
		if (bits.read(1) == 1) {
			const int wordSize = (int) bits.read(4);
			const uint32_t offset = bits.readReversed(wordSize);
			const uint32_t length = bits.readReversed(wordSize);
			if (offset == 0 || offset > out.size())
				return false;
			const size_t from = out.size() - offset;
			const uint32_t run = (offset < length) ? offset : length;
			for (uint32_t i = 0; i < run; i++)
				out.push_back(out[from + i]);
		}
		else {
			const uint32_t count = bits.readReversed(2);
			for (uint32_t i = 0; i < count; i++)
				out.push_back((uint8_t) bits.read(8));
		}
	}
	if (bits.bad || out.size() < expected)
		return false;
	out.resize(expected);
	return true;
}

bool bcfsFile(const std::vector<uint8_t>& fs, const std::string& want, std::vector<uint8_t>& out) {
	const size_t SECTOR = 0x1000;
	const size_t BASE = 4;                 /**< Past the magic; every position is from here. */
	const size_t NAME = 127;
	if (fs.size() < BASE + SECTOR || std::memcmp(fs.data(), "BCFS", 4) != 0)
		return false;

	for (size_t entry = BASE; entry + SECTOR <= fs.size(); entry += SECTOR) {
		if (le32(fs, entry) != 2)
			continue;
		const char* raw = (const char*) fs.data() + entry + 4;
		const size_t len = strnlen(raw, NAME);
		if (std::string(raw, len) != want)
			continue;

		const uint32_t size = le32(fs, entry + 0x8C);
		if (size > (64u << 20))
			return false;
		out.clear();
		out.reserve(size);
		for (size_t p = entry + 0x94; p + 4 <= fs.size() && out.size() < size; p += 4) {
			const uint32_t block = le32(fs, p);
			if (block == 0)
				break;
			const size_t from = BASE + (size_t) block * SECTOR;
			if (from + SECTOR > fs.size())
				return false;
			size_t take = SECTOR;
			if (out.size() + take > size)
				take = size - out.size();
			out.insert(out.end(), fs.begin() + from, fs.begin() + from + take);
		}
		return out.size() == size;
	}
	return false;
}

} // namespace


// ---- the gpif XML ------------------------------------------------------------------------------
//
// Small enough to read with a pull parser of our own: the file is machine-written, so there are no
// entities beyond the five, no namespaces and no processing instructions worth the name. What it
// does have is CDATA around every piece of text, and attributes that matter (ids and refs).

namespace {

/** FIND, BUT ONLY INSIDE THE PIECE BEING LOOKED AT.

The standard find scans to the end of the string. A property a note does not carry therefore cost
a walk over the whole two-megabyte file, and there are a dozen such properties and several
thousand notes: reading one song took twenty-two seconds, nearly all of it spent looking past the
note for something that was never there. Bounded, the same search stops at the end of the note. */
size_t findIn(const std::string& s, const char* needle, size_t from, size_t to) {
	const size_t n = std::strlen(needle);
	if (to > s.size())
		to = s.size();
	if (n == 0 || from >= to || from + n > to)
		return std::string::npos;
	const char* base = s.data();
	const char* at = base + from;
	const char* last = base + to - n;
	while (at <= last) {
		const char* hit = (const char*) std::memchr(at, needle[0], (size_t) (last - at) + 1);
		if (!hit)
			return std::string::npos;
		if (std::memcmp(hit, needle, n) == 0)
			return (size_t) (hit - base);
		at = hit + 1;
	}
	return std::string::npos;
}

size_t findIn(const std::string& s, const std::string& needle, size_t from, size_t to) {
	return findIn(s, needle.c_str(), from, to);
}

struct Xml {
	const std::string& s;
	size_t at = 0;

	explicit Xml(const std::string& text) : s(text) {}

	/** The text of the first element of that name inside [from, to), or empty. */
	static std::string textOf(const std::string& s, size_t from, size_t to,
			const std::string& tag) {
		const std::string open = "<" + tag;
		const size_t a = findIn(s, open, from, to);
		if (a == std::string::npos)
			return "";
		const size_t gt = findIn(s, ">", a, to);
		if (gt == std::string::npos)
			return "";
		if (s[gt - 1] == '/')
			return "";
		const std::string close = "</" + tag + ">";
		const size_t b = findIn(s, close, gt, to);
		if (b == std::string::npos)
			return "";
		std::string text = s.substr(gt + 1, b - gt - 1);
		// CDATA, which is how this format writes every string.
		const size_t c = text.find("<![CDATA[");
		if (c != std::string::npos) {
			const size_t d = text.find("]]>", c);
			if (d != std::string::npos)
				return text.substr(c + 9, d - c - 9);
		}
		// Whitespace around a number or a word is the writer's indentation.
		size_t b0 = text.find_first_not_of(" \t\r\n");
		size_t b1 = text.find_last_not_of(" \t\r\n");
		return (b0 == std::string::npos) ? "" : text.substr(b0, b1 - b0 + 1);
	}

	/** The span of the nth element of that name, as [open, close) offsets. */
	static bool spanOf(const std::string& s, const std::string& tag, size_t from, size_t to,
			size_t* start, size_t* end) {
		const std::string open = "<" + tag;
		size_t a = from;
		for (;;) {
			a = findIn(s, open, a, to);
			if (a == std::string::npos)
				return false;
			const char after = (a + open.size() < s.size()) ? s[a + open.size()] : '\0';
			if (after == ' ' || after == '>' || after == '/')
				break;
			a += open.size();
		}
		const std::string close = "</" + tag + ">";
		const size_t b = findIn(s, close, a, to);
		if (b == std::string::npos)
			return false;
		*start = a;
		*end = b + close.size();
		return true;
	}

	static std::string attr(const std::string& s, size_t from, const std::string& name) {
		const size_t gt = findIn(s, ">", from, std::min(s.size(), from + 8192));
		if (gt == std::string::npos)
			return "";
		const std::string key = name + "=\"";
		const size_t a = findIn(s, key, from, gt);
		if (a == std::string::npos)
			return "";
		const size_t b = findIn(s, "\"", a + key.size(), gt);
		if (b == std::string::npos)
			return "";
		return s.substr(a + key.size(), b - a - key.size());
	}
};

int toInt(const std::string& s, int fallback = 0) {
	if (s.empty())
		return fallback;
	return (int) std::strtol(s.c_str(), NULL, 10);
}

float toFloat(const std::string& s, float fallback = 0.f) {
	if (s.empty())
		return fallback;
	return (float) std::strtod(s.c_str(), NULL);
}

} // namespace


// ---- reading a score.gpif ----------------------------------------------------------------------

namespace {

/** How long a note value is, in quarter notes. */
float quartersOf(const std::string& name) {
	if (name == "Whole") return 4.f;
	if (name == "Half") return 2.f;
	if (name == "Quarter") return 1.f;
	if (name == "Eighth") return 0.5f;
	if (name == "16th") return 0.25f;
	if (name == "32nd") return 0.125f;
	if (name == "64th") return 0.0625f;
	if (name == "128th") return 0.03125f;
	if (name == "256th") return 0.015625f;
	return 1.f;
}

/** ppp through fff, as a level between nought and one. */
float dynamicOf(const std::string& name) {
	if (name == "PPP") return 0.1f;
	if (name == "PP") return 0.2f;
	if (name == "P") return 0.35f;
	if (name == "MP") return 0.5f;
	if (name == "MF") return 0.62f;
	if (name == "F") return 0.75f;
	if (name == "FF") return 0.88f;
	if (name == "FFF") return 1.f;
	return 0.62f;
}

struct GpifRhythm {
	float quarters = 1.f;
};

} // namespace


static bool readGpif(const std::string& xml, GpSong& out, std::string* why) {
	using X = Xml;

	// The score's own particulars.
	size_t a = 0, b = 0;
	if (X::spanOf(xml, "Score", 0, xml.size(), &a, &b)) {
		out.title = X::textOf(xml, a, b, "Title");
		out.artist = X::textOf(xml, a, b, "Artist");
		out.album = X::textOf(xml, a, b, "Album");
		out.words = X::textOf(xml, a, b, "Words");
		out.music = X::textOf(xml, a, b, "Music");
		out.copyright = X::textOf(xml, a, b, "Copyright");
		out.tabber = X::textOf(xml, a, b, "Tabber");
	}

	// The tempo, which lives in automations rather than in the header. Value is the count and
	// the note it counts — "163 2" is 163 half notes a minute. A Linear automation ramps to the
	// next one; it is taken as a step here, which is the right tempo either side of the change
	// and wrong only across it.
	{
		size_t p = 0, q = 0;
		size_t from = 0;
		while (X::spanOf(xml, "Automation", from, xml.size(), &p, &q)) {
			from = q;
			if (X::textOf(xml, p, q, "Type") != "Tempo")
				continue;
			const std::string value = X::textOf(xml, p, q, "Value");
			char* rest = NULL;
			const float bpm = (float) std::strtod(value.c_str(), &rest);
			if (bpm <= 0.f)
				continue;
			const int unit = rest ? (int) std::strtol(rest, NULL, 10) : 0;
			GpTempo tempo;
			tempo.bar = toInt(X::textOf(xml, p, q, "Bar"), 0);
			tempo.position = toFloat(X::textOf(xml, p, q, "Position"), 0.f);
			tempo.bpm = bpm;
			tempo.unit = (unit > 0) ? unit : 2;
			out.tempos.push_back(tempo);
			if (out.tempos.size() == 1)
				out.tempo = tempo.bpm;
		}
	}

	// The rhythms, by id: every beat points at one of these.
	std::vector<GpifRhythm> rhythms;
	{
		size_t p = 0, q = 0;
		if (X::spanOf(xml, "Rhythms", 0, xml.size(), &p, &q)) {   // only ever a collection
			size_t from = p;
			size_t r0 = 0, r1 = 0;
			while (X::spanOf(xml, "Rhythm", from, q, &r0, &r1)) {
				const int id = toInt(X::attr(xml, r0, "id"), (int) rhythms.size());
				GpifRhythm r;
				r.quarters = quartersOf(X::textOf(xml, r0, r1, "NoteValue"));
				// BOTH OF THESE ARE SELF-CLOSING ELEMENTS, so they are found by looking for
				// the tag rather than for a span with a closing tag. Written as a span first,
				// which found neither, and every triplet in the file came out a third too long.
				//
				// A dot is half as long again; two dots three quarters again.
				const size_t dot = findIn(xml, "<AugmentationDot", r0, r1);
				if (dot != std::string::npos) {
					const int dots = toInt(X::attr(xml, dot, "count"), 1);
					float add = r.quarters;
					for (int i = 0; i < dots && i < 3; i++) {
						add *= 0.5f;
						r.quarters += add;
					}
				}
				// A tuplet: so many notes in the time of so many.
				const size_t tup = findIn(xml, "<PrimaryTuplet", r0, r1);
				if (tup != std::string::npos) {
					const int num = toInt(X::attr(xml, tup, "num"), 0);
					const int den = toInt(X::attr(xml, tup, "den"), 0);
					if (num > 0 && den > 0)
						r.quarters = r.quarters * (float) den / (float) num;
				}
				if ((int) rhythms.size() <= id)
					rhythms.resize((size_t) id + 1);
				rhythms[(size_t) id] = r;
				from = r1;
			}
		}
	}

	// EVERY COLLECTION IS A LIST OF ELEMENTS WITH IDS, and everything else points at them by id.
	// Each is read once into a span — where it starts and stops in the text — and looked at when
	// something refers to it.
	struct Span { size_t a = 0, b = 0; };
	// THE COLLECTION, NOT THE LIST OF THE SAME NAME. Every one of these words is used twice in
	// this format: <Bars> inside a master bar is a list of bar numbers, and <Bars> at the top
	// level is the collection of bars themselves; the same for voices, beats, notes and tracks.
	// They are told apart by what is inside them — a collection holds elements with ids.
	auto groupOf = [&](const char* group, const char* item, size_t* a, size_t* b) {
		const std::string mark = std::string("<") + item + " id=";
		size_t from = 0, g0 = 0, g1 = 0;
		while (X::spanOf(xml, group, from, xml.size(), &g0, &g1)) {
			if (findIn(xml, mark, g0, g1) != std::string::npos) {
				*a = g0;
				*b = g1;
				return true;
			}
			from = g1;
		}
		return false;
	};

	auto collect = [&](const char* group, const char* item) {
		std::vector<Span> spans;
		size_t g0 = 0, g1 = 0;
		if (!groupOf(group, item, &g0, &g1))
			return spans;
		size_t from = g0, i0 = 0, i1 = 0;
		while (X::spanOf(xml, item, from, g1, &i0, &i1)) {
			const int id = toInt(X::attr(xml, i0, "id"), (int) spans.size());
			if (id >= 0 && id < 100000) {
				if ((int) spans.size() <= id)
					spans.resize((size_t) id + 1);
				Span sp;
				sp.a = i0;
				sp.b = i1;
				spans[(size_t) id] = sp;
			}
			from = i1;
		}
		return spans;
	};

	const std::vector<Span> bars = collect("Bars", "Bar");
	const std::vector<Span> voices = collect("Voices", "Voice");
	const std::vector<Span> beats = collect("Beats", "Beat");
	const std::vector<Span> notes = collect("Notes", "Note");

	// A LIST OF NUMBERS, as this format writes one: "0 1 2 3", with -1 for nothing there.
	auto numbers = [](const std::string& text) {
		std::vector<int> out;
		const char* p = text.c_str();
		while (*p) {
			while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
				p++;
			if (!*p)
				break;
			char* end = NULL;
			const long v = std::strtol(p, &end, 10);
			if (end == p)
				break;
			out.push_back((int) v);
			p = end;
		}
		return out;
	};

	// ---- the tracks, and their instruments -----------------------------------------------------
	{
		size_t g0 = 0, g1 = 0;
		if (groupOf("Tracks", "Track", &g0, &g1)) {
			size_t from = g0, t0 = 0, t1 = 0;
			while (X::spanOf(xml, "Track", from, g1, &t0, &t1)) {
				GpTrack track;
				track.name = X::textOf(xml, t0, t1, "Name");
				track.shortName = X::textOf(xml, t0, t1, "ShortName");
				// The tuning, as the MIDI note of each open string, lowest first in the file.
				const size_t tune = findIn(xml, "<Property name=\"Tuning\"", t0, t1);
				if (tune != std::string::npos) {
					const std::vector<int> pitches = numbers(X::textOf(xml, tune, t1, "Pitches"));
					track.tuning.assign(pitches.rbegin(), pitches.rend());
				}
				track.letRingThroughout =
					findIn(xml, "<LetRingThroughout", t0, t1) != std::string::npos;
				const size_t capo = findIn(xml, "<Property name=\"CapoFret\"", t0, t1);
				if (capo != std::string::npos)
					track.capo = toInt(X::textOf(xml, capo, t1, "Fret"), 0);
				// Percussion is named in the instrument set rather than by a flag.
				const std::string kind = X::textOf(xml, t0, t1, "Type");
				track.percussion = kind.find("drum") != std::string::npos
					|| kind.find("Drum") != std::string::npos
					|| kind.find("percussion") != std::string::npos;
				out.tracks.push_back(track);
				from = t1;
			}
		}
	}
	if (out.tracks.empty()) {
		if (why)
			*why = "the file has no tracks in it";
		return false;
	}

	// ---- one note ------------------------------------------------------------------------------
	auto readNote = [&](const Span& sp, GpNote& note) {
		const size_t a = sp.a, b = sp.b;
		if (b <= a)
			return false;
		// NOT EVERY FILE SAYS WHAT NOTE IT IS. Left below nought here where it does not, and
		// worked out from the string, the fret and the tuning once the track is known.
		note.midi = -1;
		const size_t midi = findIn(xml, "<Property name=\"Midi\"", a, b);
		if (midi != std::string::npos)
			note.midi = toInt(X::textOf(xml, midi, b, "Number"), 60);
		const size_t fret = findIn(xml, "<Property name=\"Fret\"", a, b);
		if (fret != std::string::npos)
			note.fret = toInt(X::textOf(xml, fret, b, "Fret"), -1);
		const size_t str = findIn(xml, "<Property name=\"String\"", a, b);
		if (str != std::string::npos)
			note.string = toInt(X::textOf(xml, str, b, "String"), 0);
		// A tie carries the note before it rather than striking again; only the destination
		// end matters here.
		const size_t tie = findIn(xml, "<Tie ", a, b);
		if (tie != std::string::npos)
			note.tied = X::attr(xml, tie, "destination") == "true";
		const size_t slide = findIn(xml, "<Property name=\"Slide\"", a, b);
		if (slide != std::string::npos) {
			note.slide = true;
			// The kind of slide is a bit field, and every one of them sounds different: kept so
			// the player can make a legato slide legato and a slide out fall away.
			note.slideFlags = (uint8_t) toInt(X::textOf(xml, slide, b, "Flags"), 0);
		}
		note.palmMute = findIn(xml, "<Property name=\"PalmMuted\"", a, b) != std::string::npos;
		note.letRing = findIn(xml, "<Property name=\"LetRing\"", a, b) != std::string::npos;
		const size_t harm = findIn(xml, "<Property name=\"HarmonicType\"", a, b);
		if (harm != std::string::npos) {
			note.harmonic = true;
			const std::string kind = X::textOf(xml, harm, b, "HType");
			note.artificialHarmonic = (kind != "Natural" && !kind.empty());
		}
		note.tapped = findIn(xml, "<Property name=\"LeftHandTapped\"", a, b) != std::string::npos
			|| findIn(xml, "<Property name=\"Tapped\"", a, b) != std::string::npos;
		note.vibrato = findIn(xml, "<Vibrato>", a, b) != std::string::npos;
		note.hammer = findIn(xml, "<Property name=\"HopoOrigin\"", a, b) != std::string::npos
			|| findIn(xml, "<Property name=\"HopoDestination\"", a, b) != std::string::npos;
		note.ghost = findIn(xml, "<Property name=\"Ghost\"", a, b) != std::string::npos;
		const int accent = toInt(X::textOf(xml, a, b, "Accent"), 0);
		note.staccato = (accent & 1) != 0;
		note.heavyAccent = (accent & 4) != 0;
		note.accent = (accent & 8) != 0 || note.heavyAccent;
		note.dead = findIn(xml, "<Property name=\"Muted\"", a, b) != std::string::npos;
		// A bend, as four points the file gives in per cent of a whole tone and of the note's
		// length. Kept as they are; what a variation does with them is its own business.
		if (findIn(xml, "<Property name=\"Bended\"", a, b) != std::string::npos) {
			static const char* WHEN[4] = {"BendOriginOffset", "BendMiddleOffset1",
				"BendMiddleOffset2", "BendDestinationOffset"};
			static const char* HOW[4] = {"BendOriginValue", "BendMiddleValue",
				"BendMiddleValue", "BendDestinationValue"};
			for (int i = 0; i < 4; i++) {
				const size_t w = findIn(xml, std::string("<Property name=\"") + WHEN[i] + "\"",
					a, b);
				const size_t h = findIn(xml, std::string("<Property name=\"") + HOW[i] + "\"",
					a, b);
				if (w == std::string::npos || h == std::string::npos)
					continue;
				const float when = toFloat(X::textOf(xml, w, b, "Float"), -1.f);
				const float how = toFloat(X::textOf(xml, h, b, "Float"), 0.f);
				// This format counts height in per cent of a WHOLE TONE, so a hundred is two
				// hundred cents. A different unit from the binary formats, and the same
				// mistake twice over until it was measured: bends came out half as deep.
				if (when >= 0.f)
					note.bend.push_back(std::make_pair(when / 100.f, how * 2.f));
			}
		}
		return true;
	};

	// ---- one beat ------------------------------------------------------------------------------
	auto readBeat = [&](const Span& sp, GpBeat& beat) {
		const size_t a = sp.a, b = sp.b;
		if (b <= a)
			return false;
		beat.dynamic = dynamicOf(X::textOf(xml, a, b, "Dynamic"));
		const size_t r = findIn(xml, "<Rhythm ", a, b);
		if (r != std::string::npos) {
			const int id = toInt(X::attr(xml, r, "ref"), -1);
			if (id >= 0 && id < (int) rhythms.size())
				beat.quarters = rhythms[(size_t) id].quarters;
		}
		const size_t brush = findIn(xml, "<Property name=\"Brush\"", a, b);
		if (brush != std::string::npos) {
			beat.brush = (X::textOf(xml, brush, b, "Direction") == "Up") ? -1 : 1;
			beat.brushMs = toInt(X::textOf(xml, brush, b, "Duration"), 0);
		}
		beat.slapped = findIn(xml, "<Property name=\"Slapped\"", a, b) != std::string::npos;
		beat.popped = findIn(xml, "<Property name=\"Popped\"", a, b) != std::string::npos;
		// THE WHAMMY BAR, four points like a bend's and in the same units: per cent of a whole
		// tone, turned into cents.
		if (findIn(xml, "<Property name=\"WhammyBar\"", a, b) != std::string::npos) {
			static const char* WHEN[4] = {"WhammyBarOriginOffset", "WhammyBarMiddleOffset1",
				"WhammyBarMiddleOffset2", "WhammyBarDestinationOffset"};
			static const char* HOW[4] = {"WhammyBarOriginValue", "WhammyBarMiddleValue",
				"WhammyBarMiddleValue", "WhammyBarDestinationValue"};
			for (int i = 0; i < 4; i++) {
				const size_t w = findIn(xml, std::string("<Property name=\"") + WHEN[i] + "\"",
					a, b);
				const size_t h = findIn(xml, std::string("<Property name=\"") + HOW[i] + "\"",
					a, b);
				if (w == std::string::npos || h == std::string::npos)
					continue;
				const float when = toFloat(X::textOf(xml, w, b, "Float"), -1.f);
				const float how = toFloat(X::textOf(xml, h, b, "Float"), 0.f);
				if (when >= 0.f)
					beat.whammy.push_back(std::make_pair(when / 100.f, how * 2.f));
			}
		}
		const std::string grace = X::textOf(xml, a, b, "GraceNotes");
		if (grace == "BeforeBeat")
			beat.grace = 1;
		else if (grace == "OnBeat")
			beat.grace = 2;

		const std::vector<int> ids = numbers(X::textOf(xml, a, b, "Notes"));
		for (size_t i = 0; i < ids.size(); i++) {
			const int id = ids[i];
			if (id < 0 || id >= (int) notes.size())
				continue;
			GpNote note;
			if (readNote(notes[(size_t) id], note))
				beat.notes.push_back(note);
			else
				out.skipped++;
		}
		beat.rest = beat.notes.empty();
		return true;
	};

	// ---- the master bars, and each track's bar of the same number -------------------------------
	{
		size_t g0 = 0, g1 = 0;
		if (!X::spanOf(xml, "MasterBars", 0, xml.size(), &g0, &g1)) {
			if (why)
				*why = "the file has no bars in it";
			return false;
		}
		size_t from = g0, m0 = 0, m1 = 0;
		while (X::spanOf(xml, "MasterBar", from, g1, &m0, &m1)) {
			GpMasterBar master;
			const std::string time = X::textOf(xml, m0, m1, "Time");
			const size_t slash = time.find('/');
			if (slash != std::string::npos) {
				master.beatsPerBar = toInt(time.substr(0, slash), 4);
				master.beatUnit = toInt(time.substr(slash + 1), 4);
			}
			master.keyAccidentals = toInt(X::textOf(xml, m0, m1, "AccidentalCount"), 0);
			master.minor = X::textOf(xml, m0, m1, "Mode") == "Minor";
			master.sectionLetter = X::textOf(xml, m0, m1, "Letter");
			master.section = X::textOf(xml, m0, m1, "Text");
			master.doubleBar = findIn(xml, "<DoubleBar", m0, m1) != std::string::npos;
			const size_t rep = findIn(xml, "<Repeat ", m0, m1);
			if (rep != std::string::npos) {
				master.repeatOpen = X::attr(xml, rep, "start") == "true";
				master.repeatCount = toInt(X::attr(xml, rep, "count"), 0);
				// A closing repeat that does not say how many times is played twice.
				if (X::attr(xml, rep, "end") == "true" && master.repeatCount < 2)
					master.repeatCount = 2;
			}
			const std::string alt = X::textOf(xml, m0, m1, "AlternateEndings");
			if (!alt.empty())
				master.endings = numbers(alt);
			// The navigation marks: a Target is a place to come back to, a Jump is the
			// instruction that sends you there. Both are kept, since the timeline needs both.
			{
				size_t d0 = 0, d1 = 0;
				if (X::spanOf(xml, "Directions", m0, m1, &d0, &d1)) {
					const char* KINDS[2] = {"Target", "Jump"};
					for (int k = 0; k < 2; k++) {
						size_t t0 = 0, t1 = 0, at = d0;
						while (X::spanOf(xml, KINDS[k], at, d1, &t0, &t1)) {
							const std::string name = X::textOf(xml, t0, t1, KINDS[k]);
							if (!name.empty())
								master.marks.push_back(name);
							at = t1;
						}
					}
				}
			}
			out.masterBars.push_back(master);

			// Each track's bar for this master bar, in track order.
			const std::vector<int> barIds = numbers(X::textOf(xml, m0, m1, "Bars"));
			for (size_t t = 0; t < out.tracks.size(); t++) {
				GpBar bar;
				const int barId = (t < barIds.size()) ? barIds[t] : -1;
				if (barId >= 0 && barId < (int) bars.size()) {
					const Span& bs = bars[(size_t) barId];
					const std::vector<int> voiceIds = numbers(X::textOf(xml, bs.a, bs.b,
						"Voices"));
					// EVERY VOICE OF THE BAR, laid on one timeline. A part written in two
					// voices is one part played by one instrument, and what this reads is what
					// sounds.
					for (size_t v = 0; v < voiceIds.size(); v++) {
						const int voiceId = voiceIds[v];
						if (voiceId < 0 || voiceId >= (int) voices.size())
							continue;
						const Span& vs = voices[(size_t) voiceId];
						const std::vector<int> beatIds = numbers(X::textOf(xml, vs.a, vs.b,
							"Beats"));
						float at = 0.f;
						for (size_t e = 0; e < beatIds.size(); e++) {
							const int beatId = beatIds[e];
							if (beatId < 0 || beatId >= (int) beats.size()) {
								out.skipped++;
								continue;
							}
							GpBeat beat;
							if (!readBeat(beats[(size_t) beatId], beat)) {
								out.skipped++;
								continue;
							}
							beat.start = at;
							at += beat.quarters;
							bar.beats.push_back(beat);
						}
					}
				}
				// THE STRING NUMBER, AND THE PITCH IT MAKES. This format counts strings from
				// the lowest up and from nought; everywhere else in here one is the highest, so
				// the number is turned round as soon as it is known which track's bar this is —
				// a beat is shared material until then, and the same fret is a different note
				// on a differently tuned instrument.
				//
				// Many files carry no pitch at all, only the string and the fret, so where that
				// is so it is worked out from the tuning and the capo.
				{
					const GpTrack& track = out.tracks[t];
					const int strings = (int) track.tuning.size();
					for (size_t e = 0; e < bar.beats.size(); e++) {
						std::vector<GpNote>& notes = bar.beats[e].notes;
						for (size_t k = 0; k < notes.size(); k++) {
							GpNote& note = notes[k];
							const int from0 = note.string;      // Nought is the lowest string.
							if (from0 < 0 || from0 >= strings) {
								if (note.midi < 0) {
									note.midi = 60;             // A drum kit, most likely.
									out.skipped++;
								}
								note.string = 0;
								continue;
							}
							if (note.midi < 0) {
								note.midi = track.tuning[(size_t) (strings - 1 - from0)]
									+ ((note.fret > 0) ? note.fret : 0) + track.capo;
							}
							note.string = strings - from0;
						}
					}
				}
				out.tracks[t].bars.push_back(bar);
			}
			from = m1;
		}
	}

	return true;
}


// ---- gp3, gp4 and gp5: a sequential binary format -----------------------------------------------
//
// PORTED FROM PyGuitarPro, which reads exactly these three and is itself a port of alphaTab, which
// was a port of TuxGuitar. Porting logic that has been exercised on a great many real files is
// safer than working the format out from a hex dump, and where this differs from that reader it is
// wrong.
//
// THE READING IS THE PARSING. Every field must be consumed in order even when its value is thrown
// away, because the next field's position depends on it — which is why the chord diagrams and the
// mixer changes are read at all.

namespace {

/** TEXT FROM A BINARY FILE, AS UTF-8.

The binary formats write a name in whatever code page the machine that saved it was using, and
say nowhere which one that was. A name written in Russian on a Russian Windows is not broken —
it is Windows-1251 — but read as anything else it is nonsense, and read as UTF-8 it is not text
at all and draws as a row of question marks.

So: text that is already valid UTF-8 is left alone, which covers every file written by anything
modern. Anything else is one of the two code pages these files come in, told apart by a simple
thing that holds in practice — Cyrillic runs, accented Latin does not. "Дорожка" is seven bytes
above 0xBF in a row; a French or Spanish name has one or two among ordinary letters.

It cannot always be right, because the file does not say. It can always produce text somebody
can read, which a row of question marks is not. */
bool isUtf8(const std::string& s) {
	size_t i = 0;
	while (i < s.size()) {
		const unsigned char c = (unsigned char) s[i];
		int more = 0;
		if (c < 0x80) { i++; continue; }
		else if ((c & 0xE0) == 0xC0) more = 1;
		else if ((c & 0xF0) == 0xE0) more = 2;
		else if ((c & 0xF8) == 0xF0) more = 3;
		else return false;
		if (i + (size_t) more >= s.size())
			return false;
		for (int k = 1; k <= more; k++) {
			if (((unsigned char) s[i + (size_t) k] & 0xC0) != 0x80)
				return false;
		}
		i += (size_t) more + 1;
	}
	return true;
}

void appendUtf8(std::string& out, unsigned int code) {
	if (code < 0x80)
		out += (char) code;
	else if (code < 0x800) {
		out += (char) (0xC0 | (code >> 6));
		out += (char) (0x80 | (code & 0x3F));
	}
	else {
		out += (char) (0xE0 | (code >> 12));
		out += (char) (0x80 | ((code >> 6) & 0x3F));
		out += (char) (0x80 | (code & 0x3F));
	}
}

std::string toUtf8(const std::string& s) {
	if (isUtf8(s))
		return s;

	// Windows-1252's own characters, 0x80 to 0x9F. Above that it is Latin-1, code point for byte.
	static const unsigned short CP1252[32] = {
		0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
		0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
		0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
	};
	// Windows-1251, 0x80 to 0xFF: Cyrillic, with punctuation at the bottom of the range.
	static const unsigned short CP1251[128] = {
		0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
		0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
		0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
		0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
		0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
		0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
		0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
		0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
		0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
		0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
		0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
		0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
		0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
		0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
		0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
		0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
	};

	// Three letters of Cyrillic in a row is a word; three accented Latin letters in a row is not.
	bool cyrillic = false;
	int run = 0;
	for (size_t i = 0; i < s.size(); i++) {
		run = ((unsigned char) s[i] >= 0xC0) ? run + 1 : 0;
		if (run >= 3) {
			cyrillic = true;
			break;
		}
	}

	std::string out;
	out.reserve(s.size() * 2);
	for (size_t i = 0; i < s.size(); i++) {
		const unsigned char c = (unsigned char) s[i];
		if (c < 0x80)
			out += (char) c;
		else if (cyrillic)
			appendUtf8(out, CP1251[c - 0x80]);
		else if (c < 0xA0)
			appendUtf8(out, CP1252[c - 0x80]);
		else
			appendUtf8(out, c);
	}
	return out;
}

struct Reader {
	const uint8_t* data;
	size_t size;
	size_t at = 0;
	bool bad = false;

	bool have(size_t n) const { return !bad && at + n <= size; }

	uint8_t u8() {
		if (!have(1)) { bad = true; return 0; }
		return data[at++];
	}
	int8_t i8() { return (int8_t) u8(); }
	int16_t i16() {
		const uint8_t a = u8(), b = u8();
		return (int16_t) ((uint16_t) a | ((uint16_t) b << 8));
	}
	int32_t i32() {
		const uint8_t a = u8(), b = u8(), c = u8(), d = u8();
		return (int32_t) ((uint32_t) a | ((uint32_t) b << 8) | ((uint32_t) c << 16)
			| ((uint32_t) d << 24));
	}
	double f64() {
		double v = 0.0;
		if (!have(8)) { bad = true; return 0.0; }
		std::memcpy(&v, data + at, 8);
		at += 8;
		return v;
	}
	bool boolean() { return u8() != 0; }
	void skip(size_t n) {
		if (!have(n)) { bad = true; at = size; return; }
		at += n;
	}

	/** A string in a fixed field: one byte of length, then `count` bytes whatever the length. */
	std::string byteSizeString(int count) {
		if (count < 0 || count > 255) { bad = true; return ""; }
		const int len = u8();
		if (!have((size_t) count)) { bad = true; return ""; }
		const int use = (len < count) ? len : count;
		std::string s((const char*) data + at, (size_t) use);
		at += (size_t) count;
		return toUtf8(s);
	}
	/** A string whose field length is given first, as a four-byte number. */
	std::string intByteSizeString() {
		const int32_t count = i32();
		if (count <= 0 || count > 1 << 20) { if (count < 0) bad = true; return ""; }
		return byteSizeString((count - 1 > 255) ? 255 : count - 1);
	}
	/** A string with no length byte: the four-byte count is the string. */
	std::string intSizeString() {
		const int32_t count = i32();
		if (count < 0 || !have((size_t) count)) { bad = true; return ""; }
		std::string s((const char*) data + at, (size_t) count);
		at += (size_t) count;
		return toUtf8(s);
	}
};

/** How long a beat is, in quarter notes: the file writes the exponent of the note value. */
float durationQuarters(int8_t value, bool dotted, int enters, int times) {
	// value -2 is a whole note, -1 a half, 0 a quarter, 1 an eighth, and so on.
	float quarters = 4.f;
	for (int i = -2; i < (int) value; i++)
		quarters *= 0.5f;
	if (dotted)
		quarters *= 1.5f;
	if (enters > 0 && times > 0)
		quarters = quarters * (float) times / (float) enters;
	return quarters;
}

void tupletOf(int32_t n, int* enters, int* times) {
	*enters = 0;
	*times = 0;
	switch (n) {
		case 3: *enters = 3; *times = 2; break;
		case 5: *enters = 5; *times = 4; break;
		case 6: *enters = 6; *times = 4; break;
		case 7: *enters = 7; *times = 4; break;
		case 9: *enters = 9; *times = 8; break;
		case 10: *enters = 10; *times = 8; break;
		case 11: *enters = 11; *times = 8; break;
		case 12: *enters = 12; *times = 8; break;
		case 13: *enters = 13; *times = 8; break;
		default: break;
	}
}

/** The reading of one file: the version decides a dozen small differences, so it is carried here
rather than passed to everything. */
struct Binary {
	Reader r;
	int major = 3;          /**< 3, 4 or 5. */
	int minor = 0;
	bool clipboard = false;
	GpSong* song = NULL;
	/** The strings of each track, since a beat says which strings it uses rather than which
	notes, and the tuning is what turns a fret into a pitch. */
	std::vector<std::vector<int> > tunings;
	/** Where the reader has got to, so a tempo change inside a beat knows where it belongs. */
	int curBar = 0;
	float curPosition = 0.f;

	bool after500() const { return major > 5 || (major == 5 && minor > 0); }

	/** WHICH PASSES A BAR IS PLAYED ON, from the byte the file writes: one bit per ending,
	the first ending in the lowest bit. */
	static std::vector<int> endingsOf(uint8_t bits) {
		std::vector<int> endings;
		for (int i = 0; i < 8; i++) {
			if (bits & (1 << i))
				endings.push_back(i + 1);
		}
		return endings;
	}

	/** A TEMPO CHANGE FROM A MIX TABLE. The same change is written into every track's copy of
	the bar, so one already recorded at this place is not recorded again. */
	void tempoAt(float bpm) {
		if (!song || bpm <= 0.f)
			return;
		for (size_t i = 0; i < song->tempos.size(); i++) {
			if (song->tempos[i].bar == curBar && song->tempos[i].position == curPosition)
				return;
		}
		GpTempo tempo;
		tempo.bar = curBar;
		tempo.position = curPosition;
		tempo.bpm = bpm;
		song->tempos.push_back(tempo);
	}

	void readChord(int stringCount, std::string* name) {
		const bool newFormat = r.boolean();
		if (!newFormat) {
			*name = r.intByteSizeString();
			const int32_t firstFret = r.i32();
			if (firstFret) {
				for (int i = 0; i < 6; i++)
					r.i32();
			}
			return;
		}
		if (major == 3) {
			r.boolean();            // sharp
			r.skip(3);
			r.i32();                // root
			r.i32();                // type
			r.i32();                // extension
			r.i32();                // bass
			r.i32();                // tonality
			r.boolean();            // add
			*name = r.byteSizeString(22);
			r.i32(); r.i32(); r.i32();       // fifth, ninth, eleventh
			r.i32();                         // first fret
			for (int i = 0; i < 6; i++)
				r.i32();                     // the fret on each string
			r.i32();                         // barre count
			for (int i = 0; i < 6; i++)
				r.i32();                     // barre frets, starts, ends
			for (int i = 0; i < 7; i++)
				r.boolean();                 // omissions
			r.skip(1);
			return;
		}
		// Four and five write the same shape with smaller fields, and add fingerings.
		r.boolean();
		r.skip(3);
		r.u8(); r.u8(); r.u8();     // root, type, extension
		r.i32();                    // bass
		r.i32();                    // tonality
		r.boolean();                // add
		*name = r.byteSizeString(22);
		r.u8(); r.u8(); r.u8();     // fifth, ninth, eleventh
		r.i32();                    // first fret
		for (int i = 0; i < 7; i++)
			r.i32();
		r.u8();                     // barre count
		for (int i = 0; i < 15; i++)
			r.u8();                 // barre frets, starts, ends
		for (int i = 0; i < 7; i++)
			r.boolean();            // omissions
		r.skip(1);
		for (int i = 0; i < 7; i++)
			r.i8();                 // fingerings
		r.boolean();                // show the diagram
		(void) stringCount;
	}

	/** A BEND, OR A WHAMMY BAR, which the file writes the same way. Whichever is handed in gets
	the points; both are nought to one across the note and cents high. */
	void readBend(GpNote* note, std::vector<std::pair<float, float> >* bar = NULL) {
		r.i8();                              // the kind of bend
		r.i32();                             // its height, which the points give again
		const int32_t points = r.i32();
		if (points < 0 || points > 64) {
			r.bad = true;
			return;
		}
		for (int i = 0; i < points; i++) {
			const int32_t position = r.i32();
			const int32_t value = r.i32();
			r.boolean();                     // vibrato on that point
			// The binary formats count position in sixtieths of the note and height in
			// twenty-fifths of a SEMITONE, so twenty-five is a hundred cents. Both are turned
			// into what GpNote says they are: a fraction of the note, and cents.
			const std::pair<float, float> point((float) position / 60.f, (float) value * 4.f);
			if (note)
				note->bend.push_back(point);
			else if (bar)
				bar->push_back(point);
		}
	}

	/** THE GRACE NOTE BEFORE THIS ONE. Three and four always play it before the beat; five says
	which. What is kept is where it is fingered and when it falls, which is what it takes to
	sound one; how hard it was struck is left to the note it leads into. */
	void readGrace(GpNote* note) {
		if (major < 5) {
			const int8_t fret = r.i8();
			r.u8();      // velocity
			r.u8();      // duration
			r.i8();      // transition
			if (note) {
				note->graceFret = fret;
				note->grace = 1;
			}
			return;
		}
		const int8_t fret = r.i8();
		r.u8();          // velocity
		r.u8();          // duration
		r.u8();          // transition
		const uint8_t flags = r.u8();
		if (note) {
			note->graceFret = fret;
			note->grace = (flags & 0x02) ? 2 : 1;
		}
	}

	void readNoteEffects(GpNote* note) {
		if (major == 3) {
			const uint8_t flags = r.u8();
			if (note) {
				note->hammer = (flags & 0x02) != 0;
				note->letRing = (flags & 0x08) != 0;
				note->slide = (flags & 0x04) != 0;
			}
			if (flags & 0x01)
				readBend(note);
			if (flags & 0x10)
				readGrace(note);
			return;
		}
		const uint8_t flags1 = r.u8();
		const uint8_t flags2 = r.u8();
		if (note) {
			note->hammer = (flags1 & 0x02) != 0;
			note->letRing = (flags1 & 0x08) != 0;
			note->palmMute = (flags2 & 0x02) != 0;
			note->vibrato = (flags2 & 0x40) != 0;
		}
		if (flags1 & 0x01)
			readBend(note);
		if (flags1 & 0x10)
			readGrace(note);
		if (flags2 & 0x04) {
			r.i8();                       // how fast it is picked, which the player decides
			if (note)
				note->tremolo = true;
		}
		if (flags2 & 0x08) {
			const uint8_t kinds = r.u8();
			if (note) {
				note->slide = true;
				note->slideFlags = kinds;
			}
		}
		if (flags2 & 0x10) {
			// A harmonic, whose shape depends on which kind it is. One is natural; the rest are
			// made with the right hand somewhere along the string.
			const int8_t kind = r.i8();
			if (note) {
				note->harmonic = true;
				note->artificialHarmonic = (kind != 1);
			}
			if (major >= 5) {
				if (kind == 2) {
					r.u8(); r.i8(); r.u8();
				}
				else if (kind == 3) {
					r.u8();
				}
			}
		}
		if (flags2 & 0x20) {
			r.i8();                       // trill fret
			r.i8();                       // trill period
		}
	}

	void readNote(GpNote* note, int stringNumber, const std::vector<int>& tuning) {
		const uint8_t flags = r.u8();
		note->string = stringNumber;
		note->ghost = (flags & 0x04) != 0;
		note->accent = (flags & 0x40) != 0;
		note->heavyAccent = (flags & 0x02) != 0;

		int type = 1;                     // 1 normal, 2 tied, 3 dead
		if (flags & 0x20)
			type = r.u8();
		if (major < 5 && (flags & 0x01)) {
			r.i8();                       // a duration of its own
			r.i8();                       // and its tuplet
		}
		if (flags & 0x10)
			r.i8();                       // velocity, which the beat's dynamic covers
		int fret = 0;
		if (flags & 0x20)
			fret = r.i8();
		if (flags & 0x80) {
			r.i8(); r.i8();               // which fingers
		}
		if (major >= 5) {
			if (flags & 0x01)
				r.f64();                  // how much of its length it sounds for
			r.u8();                       // flags: accidentals swapped
		}
		if (flags & 0x08)
			readNoteEffects(note);

		note->tied = (type == 2);
		note->dead = (type == 3);
		note->fret = fret;
		// The string is numbered from the top down, one being the highest, and so is the tuning.
		const int index = stringNumber - 1;
		const int open = (index >= 0 && index < (int) tuning.size()) ? tuning[(size_t) index] : 40;
		note->midi = open + fret;
	}

	void readMixTableChange() {
		r.i8();                           // instrument
		if (major >= 5) {
			// An RSE instrument, then a byte in 5.0 only.
			r.i32(); r.i32(); r.i32();
			if (after500())
				r.i32();
			else {
				r.i16();
				r.skip(1);
			}
			if (!after500())
				r.skip(1);
		}
		const int8_t volume = r.i8();
		const int8_t balance = r.i8();
		const int8_t chorus = r.i8();
		const int8_t reverb = r.i8();
		const int8_t phaser = r.i8();
		const int8_t tremolo = r.i8();
		if (major >= 5)
			r.intByteSizeString();        // the tempo's name
		const int32_t tempo = r.i32();
		if (volume >= 0) r.i8();
		if (balance >= 0) r.i8();
		if (chorus >= 0) r.i8();
		if (reverb >= 0) r.i8();
		if (phaser >= 0) r.i8();
		if (tremolo >= 0) r.i8();
		if (tempo >= 0) {
			tempoAt((float) tempo);
			r.i8();
			if (major >= 5 && after500())
				r.boolean();
		}
		if (major >= 4)
			r.i8();                       // which of them apply to every track
		if (major >= 5) {
			r.i8();                       // wah
			if (after500()) {
				r.intByteSizeString();    // effect
				r.intByteSizeString();    // its category
			}
		}
	}

	void readBeatEffects(GpBeat* beat) {
		if (major == 3) {
			const uint8_t flags1 = r.u8();
			if (flags1 & 0x20) {
				const uint8_t hand = r.u8();
				if (beat) {
					beat->tapped = (hand == 1);
					beat->slapped = (hand == 2);
					beat->popped = (hand == 3);
				}
				// Three writes the bar as a single height rather than as points, and only
				// where the hand did nothing else.
				const int32_t value = r.i32();
				if (beat && hand == 0 && value != 0) {
					beat->whammy.push_back(std::make_pair(0.f, 0.f));
					beat->whammy.push_back(std::make_pair(1.f, (float) value * 4.f));
				}
			}
			if (flags1 & 0x40) {
				const int8_t down = r.i8();
				const int8_t up = r.i8();
				if (beat)
					beat->brush = (up > 0) ? -1 : (down > 0) ? 1 : 0;
			}
			return;
		}
		const uint8_t flags1 = r.u8();
		const uint8_t flags2 = r.u8();
		if (flags1 & 0x20) {
			const int8_t hand = r.i8();
			if (beat) {
				beat->tapped = (hand == 1);
				beat->slapped = (hand == 2);
				beat->popped = (hand == 3);
			}
		}
		if (flags2 & 0x04)
			readBend(NULL, beat ? &beat->whammy : NULL);
		if (flags1 & 0x40) {
			const int8_t down = r.i8();
			const int8_t up = r.i8();
			if (beat) {
				// Five writes the two the other way round.
				const int8_t a = (major >= 5) ? up : down;
				const int8_t b = (major >= 5) ? down : up;
				beat->brush = (b > 0) ? -1 : (a > 0) ? 1 : 0;
			}
		}
		if (flags2 & 0x02)
			r.i8();                       // the pick's direction
	}

	/** One beat, returning how long it takes. */
	float readBeat(GpBar* bar, int trackIndex, float start) {
		const uint8_t flags = r.u8();
		GpBeat beat;
		beat.start = start;
		curPosition = start;

		int status = 1;                   // 0 empty, 1 normal, 2 rest
		if (flags & 0x40)
			status = r.u8();

		const int8_t value = r.i8();
		int enters = 0, times = 0;
		if (flags & 0x20)
			tupletOf(r.i32(), &enters, &times);
		beat.quarters = durationQuarters(value, (flags & 0x01) != 0, enters, times);

		if (flags & 0x02) {
			std::string name;
			readChord((int) tunings[(size_t) trackIndex].size(), &name);
			if (!name.empty()) {
				GpChordMark mark;
				mark.bar = (int) song->masterBars.size();
				mark.start = start;
				mark.name = name;
				song->chords.push_back(mark);
			}
		}
		if (flags & 0x04)
			r.intByteSizeString();        // a word written over the beat
		if (flags & 0x08)
			readBeatEffects(&beat);
		if (flags & 0x10)
			readMixTableChange();

		// WHICH STRINGS ARE PLAYED, as one bit each from the top string down.
		const uint8_t strings = r.u8();
		const std::vector<int>& tuning = tunings[(size_t) trackIndex];
		for (int number = 1; number <= (int) tuning.size(); number++) {
			if (!(strings & (1 << (7 - number))))
				continue;
			GpNote note;
			readNote(&note, number, tuning);
			beat.notes.push_back(note);
		}

		// HOW THE BEAT IS DRAWN, written by five only and after the notes: two bytes of flags
		// about beams, brackets and octave marks, and one more byte where the flags ask for it.
		// None of it is heard, and skipping it left every beat after the first misread.
		if (major >= 5) {
			const uint16_t drawn = (uint16_t) r.i16();
			if (drawn & 0x0800)
				r.u8();                   // how far the secondary beams are broken
		}

		beat.rest = beat.notes.empty();
		if (status != 0)
			bar->beats.push_back(beat);
		return (status == 0) ? 0.f : beat.quarters;
	}

	void readVoice(GpBar* bar, int trackIndex, float start) {
		const int32_t beats = r.i32();
		if (beats < 0 || beats > 4096) {
			r.bad = true;
			return;
		}
		float at = start;
		for (int i = 0; i < beats && !r.bad; i++)
			at += readBeat(bar, trackIndex, at);
	}

	bool read(GpSong& out, std::string* why) {
		song = &out;
		const std::string version = r.byteSizeString(30);
		if (version.find("GUITAR PRO") == std::string::npos) {
			if (why)
				*why = "this is not a Guitar Pro file";
			return false;
		}
		const size_t v = version.find('v');
		if (v != std::string::npos && v + 3 < version.size()) {
			major = version[v + 1] - '0';
			minor = version[v + 3] - '0';
		}
		if (major < 3 || major > 5) {
			if (why)
				*why = "Guitar Pro version " + version.substr(v == std::string::npos ? 0 : v)
					+ " is not read";
			return false;
		}
		clipboard = version.find("CLIPBOARD") != std::string::npos;
		if (clipboard && major >= 4) {
			r.i32(); r.i32(); r.i32(); r.i32();
			if (major >= 5) {
				r.i32(); r.i32(); r.i32();
			}
		}

		out.title = r.intByteSizeString();
		r.intByteSizeString();                      // subtitle
		out.artist = r.intByteSizeString();
		out.album = r.intByteSizeString();
		out.words = r.intByteSizeString();
		if (major >= 5)
			out.music = r.intByteSizeString();
		out.copyright = r.intByteSizeString();
		out.tabber = r.intByteSizeString();
		r.intByteSizeString();                      // instructions
		const int32_t noticeLines = r.i32();
		if (noticeLines < 0 || noticeLines > 4096) {
			if (why)
				*why = "the notice in this file is not readable";
			return false;
		}
		for (int i = 0; i < noticeLines; i++)
			r.intByteSizeString();

		if (major < 5)
			r.boolean();                            // triplet feel
		if (major >= 4) {
			// The lyrics: which track they belong to, then five lines.
			r.i32();
			for (int i = 0; i < 5; i++) {
				r.i32();                            // the bar it starts in
				const std::string line = r.intSizeString();
				if (!line.empty())
					out.lyrics.push_back(line);
			}
		}
		if (major >= 5) {
			if (after500()) {
				r.i32();                            // master volume
				r.i32();                            // reserved
				for (int i = 0; i < 11; i++)
					r.i8();                         // the equaliser
			}
			// The page setup, which is a run of numbers and ten strings.
			r.i32(); r.i32();
			r.i32(); r.i32(); r.i32(); r.i32();
			r.i32();
			r.i16();
			for (int i = 0; i < 10; i++)
				r.intByteSizeString();
			r.intByteSizeString();                  // the tempo's name
		}

		out.tempo = (float) r.i32();
		// WHETHER THE TEMPO IS SHOWN ON THE SCORE. One byte, written only by 5.10 and later,
		// and reading past it put every count after it one byte out — which is what made a good
		// file claim an impossible number of tracks.
		if (major >= 5 && after500())
			r.boolean();
		if (out.tempo > 0.f) {
			GpTempo tempo;                          // The song's own tempo, in quarter notes.
			tempo.bpm = out.tempo;
			out.tempos.push_back(tempo);
		}
		if (major >= 5) {
			r.i8();                                 // key
			r.i32();                                // octave
		}
		else {
			r.i32();                                // key
			if (major == 4)
				r.i8();                             // octave
		}

		for (int i = 0; i < 64; i++) {
			r.i32();                                // instrument
			r.i8(); r.i8(); r.i8(); r.i8(); r.i8(); r.i8();
			r.skip(2);
		}
		// WHERE THE NAVIGATION SIGNS ARE: nineteen bar numbers in a fixed order, one per kind of
		// sign, each the bar it is written on or -1 for a sign the song does not use. Read here
		// and put on the bars once they exist.
		std::vector<std::pair<int, std::string> > directions;
		if (major >= 5) {
			static const char* KINDS[19] = {
				"Coda", "DoubleCoda", "Segno", "SegnoSegno", "Fine",
				"DaCapo", "DaCapoAlCoda", "DaCapoAlDoubleCoda", "DaCapoAlFine",
				"DaSegno", "DaSegnoAlCoda", "DaSegnoAlDoubleCoda", "DaSegnoAlFine",
				"DaSegnoSegno", "DaSegnoSegnoAlCoda", "DaSegnoSegnoAlDoubleCoda",
				"DaSegnoSegnoAlFine", "DaCoda", "DaDoubleCoda",
			};
			for (int i = 0; i < 19; i++) {
				const int bar = r.i16();            // The bar it is on, counting from one.
				if (bar > 0)
					directions.push_back(std::make_pair(bar - 1, std::string(KINDS[i])));
			}
			r.i32();                                // reverb
		}

		const int32_t barCount = r.i32();
		const int32_t trackCount = r.i32();
		if (r.bad || barCount < 0 || barCount > 8192 || trackCount < 0 || trackCount > 256) {
			if (why)
				*why = "this file says it has an impossible number of bars or tracks";
			return false;
		}

		// ---- the bars, as time and structure ---------------------------------------------------
		int beatsPerBar = 4, beatUnit = 4;
		for (int i = 0; i < barCount && !r.bad; i++) {
			if (major >= 5 && i > 0)
				r.skip(1);
			const uint8_t flags = r.u8();
			GpMasterBar master;
			if (flags & 0x01)
				beatsPerBar = r.i8();
			if (flags & 0x02)
				beatUnit = r.i8();
			master.beatsPerBar = beatsPerBar;
			master.beatUnit = beatUnit;
			master.repeatOpen = (flags & 0x04) != 0;
			// HOW MANY TIMES THE SECTION IS PLAYED. Three and four write how many times it goes
			// back; five writes how many times it is played.
			if (flags & 0x08)
				master.repeatCount = (major >= 5) ? r.i8() : (r.i8() + 1);
			// WHICH PASSES THIS BAR IS PLAYED ON, as one bit per ending from the first.
			if (major < 5 && (flags & 0x10))
				master.endings = endingsOf(r.u8());
			if (flags & 0x20) {
				master.section = r.intByteSizeString();
				r.u8(); r.u8(); r.u8(); r.skip(1);  // the marker's colour
			}
			if (flags & 0x40) {
				master.keyAccidentals = r.i8();
				master.minor = r.i8() != 0;
			}
			if (major >= 5) {
				if (flags & 0x10)
					master.endings = endingsOf(r.u8());
				master.doubleBar = (flags & 0x80) != 0;
				if (flags & 0x03) {
					for (int k = 0; k < 4; k++)
						r.u8();                     // how the beams are drawn
				}
				if (!(flags & 0x10))
					r.skip(1);
				r.u8();                             // triplet feel
			}
			else {
				master.doubleBar = (flags & 0x80) != 0;
			}
			out.masterBars.push_back(master);
		}
		for (size_t i = 0; i < directions.size(); i++) {
			const size_t bar = (size_t) directions[i].first;
			if (bar < out.masterBars.size())
				out.masterBars[bar].marks.push_back(directions[i].second);
		}

		// ---- the tracks ------------------------------------------------------------------------
		for (int i = 0; i < trackCount && !r.bad; i++) {
			if (major >= 5 && (i == 0 || !after500()))
				r.skip(1);
			const uint8_t flags = r.u8();
			GpTrack track;
			track.percussion = (flags & 0x01) != 0;
			track.name = r.byteSizeString(40);
			const int32_t stringCount = r.i32();
			std::vector<int> tuning;
			for (int k = 0; k < 7; k++) {
				const int32_t pitch = r.i32();
				if (k < stringCount && stringCount <= 7)
					tuning.push_back(pitch);
			}
			r.i32();                                // port
			const int32_t channel = r.i32();
			r.i32();                                // the effects channel
			r.i32();                                // how many frets
			track.capo = r.i32();
			r.u8(); r.u8(); r.u8(); r.skip(1);      // its colour
			if (channel == 10)
				track.percussion = true;
			if (major >= 5) {
				r.i16();                            // what is shown for this track
				r.u8();                             // accentuation
				r.u8();                             // bank
				r.u8();                             // humanising
				r.i32(); r.i32();                   // clef transposition
				r.i32();
				r.skip(12);
				// The RSE instrument.
				r.i32(); r.i32(); r.i32();
				if (after500())
					r.i32();
				else {
					r.i16();
					r.skip(1);
				}
				if (after500()) {
					for (int k = 0; k < 4; k++)
						r.i8();                     // its equaliser
					r.intByteSizeString();
					r.intByteSizeString();
				}
			}
			// The strings are written from the highest down, and a note names its string from
			// the top. Kept as the file has them, lowest first, which readNote counts back through.
			track.tuning.assign(tuning.rbegin(), tuning.rend());
			tunings.push_back(tuning);
			out.tracks.push_back(track);
		}
		if (major >= 5)
			r.skip(after500() ? 1 : 2);

		if (r.bad || out.tracks.empty()) {
			if (why)
				*why = "the tracks in this file are not readable";
			return false;
		}

		// ---- the bars of each track --------------------------------------------------------------
		for (size_t t = 0; t < out.tracks.size(); t++)
			out.tracks[t].bars.resize((size_t) barCount);

		for (int b = 0; b < barCount && !r.bad; b++) {
			curBar = b;
			for (size_t t = 0; t < out.tracks.size() && !r.bad; t++) {
				GpBar& bar = out.tracks[t].bars[(size_t) b];
				// Three and four write one voice; five writes two and then a line break.
				const int voices = (major >= 5) ? 2 : 1;
				for (int v = 0; v < voices && !r.bad; v++)
					readVoice(&bar, (int) t, 0.f);
				// A byte saying where the line breaks, which five writes after every bar except,
				// in every file seen, the last one. Read only if it is there: reading off the
				// end marked a whole song as damaged when nothing was missing from it.
				if (major >= 5 && r.have(1))
					r.u8();
			}
		}

		if (r.bad) {
			// WHAT WAS READ IS KEPT. A file that runs out part way through is more useful as the
			// bars that were read than as a message, so the failure is counted rather than
			// returned — unless nothing was read at all.
			out.skipped++;
			int notes = 0;
			for (size_t t = 0; t < out.tracks.size(); t++) {
				for (size_t b = 0; b < out.tracks[t].bars.size(); b++)
					notes += (int) out.tracks[t].bars[b].beats.size();
			}
			if (notes == 0) {
				if (why)
					*why = "this file stops before any of its music";
				return false;
			}
		}
		return true;
	}
};

} // namespace


bool gpReadBytes(const std::vector<uint8_t>& bytes, GpSong& out, std::string* why) {
	auto fail = [&](const char* text) {
		if (why)
			*why = text;
		return false;
	};

	if (bytes.size() < 8)
		return fail("the file is too short to be a Guitar Pro file");

	// WHAT IT IS, FROM WHAT IS IN IT rather than from its name: a file downloaded from the
	// internet is as likely as not to carry the wrong extension.
	// A SCORE ON ITS OWN, unzipped: what comes out of a tool that has already opened the file,
	// and what some sites hand you directly.
	if (bytes[0] == '<') {
		const std::string text((const char*) bytes.data(), bytes.size());
		return readGpif(text, out, why);
	}
	if (bytes[0] == 'P' && bytes[1] == 'K') {
		std::vector<uint8_t> xml;
		if (!zipEntry(bytes, "Content/score.gpif", xml))
			return fail("this looks like a Guitar Pro 7 file but has no score in it");
		const std::string text((const char*) xml.data(), xml.size());
		return readGpif(text, out, why);
	}
	if (bytes[0] == 'B' && bytes[1] == 'C' && bytes[2] == 'F') {
		std::vector<uint8_t> fs;
		if (bytes[3] == 'Z') {
			if (!bcfzInflate(bytes, fs))
				return fail("this Guitar Pro 6 file cannot be unpacked");
		}
		else
			fs = bytes;                    // Already unpacked, which some tools leave behind.
		std::vector<uint8_t> xml;
		if (!bcfsFile(fs, "score.gpif", xml))
			return fail("this looks like a Guitar Pro 6 file but has no score in it");
		const std::string text((const char*) xml.data(), xml.size());
		return readGpif(text, out, why);
	}
	// A binary one names itself in its first thirty bytes.
	{
		Binary binary;
		binary.r.data = bytes.data();
		binary.r.size = bytes.size();
		return binary.read(out, why);
	}
}


bool gpRead(const std::string& path, GpSong& out, std::string* why) {
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) {
		if (why)
			*why = "cannot open " + path;
		return false;
	}
	std::vector<uint8_t> bytes;
	uint8_t buf[65536];
	size_t n = 0;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
		bytes.insert(bytes.end(), buf, buf + n);
	std::fclose(f);
	return gpReadBytes(bytes, out, why);
}


} // namespace px
