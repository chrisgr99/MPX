#include "IReal.hpp"

#include <cstdio>
#include <cstring>
#include <cctype>

namespace px {


/** Every scrambled body carries this in front of it. */
static const char* BODY_PREFIX = "1r34LbKcu7";


// ---- decoding -------------------------------------------------------------------------------

static std::string percentDecode(const std::string& in) {
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); i++) {
		if (in[i] == '%' && i + 2 < in.size()) {
			const std::string hex = in.substr(i + 1, 2);
			char* end = NULL;
			const long v = std::strtol(hex.c_str(), &end, 16);
			if (end && *end == '\0') {
				out += (char) v;
				i += 2;
				continue;
			}
		}
		out += (in[i] == '+') ? ' ' : in[i];
	}
	return out;
}


std::string irealPayloadFromHtml(const std::string& html) {
	static const std::string tag = "irealb://";
	const size_t at = html.find(tag);
	if (at == std::string::npos)
		return "";
	const size_t start = at + tag.size();
	// The link runs to the quote that closes the attribute it lives in.
	size_t end = html.size();
	for (size_t i = start; i < html.size(); i++) {
		if (html[i] == '"' || html[i] == '\'') {
			end = i;
			break;
		}
	}
	return percentDecode(html.substr(start, end - start));
}


/** One fifty-character block: characters nought to four swap with forty-nine down to
forty-five, and ten to twenty-three with thirty-nine down to twenty-six. This is the public
de-obfuscation and it is its own inverse. */
static void unscramble50(std::string& b) {
	const std::string in = b;
	for (int i = 0; i < 5; i++) {
		b[i] = in[49 - i];
		b[49 - i] = in[i];
	}
	for (int i = 10; i < 24; i++) {
		b[i] = in[49 - i];
		b[49 - i] = in[i];
	}
}


std::string irealUnscramble(const std::string& body) {
	std::string s = body;
	std::string out;
	out.reserve(s.size());
	// A tail shorter than a whole block is left as it is, which is what the format does.
	while (s.size() > 50) {
		std::string block = s.substr(0, 50);
		unscramble50(block);
		out += block;
		s = s.substr(50);
	}
	return out + s;
}


// ---- chords ---------------------------------------------------------------------------------

static const int MAJOR_STEPS[7] = {0, 2, 4, 5, 7, 9, 11};
static const int MINOR_STEPS[7] = {0, 2, 3, 5, 7, 8, 10};

static int letterPitchClass(char letter) {
	switch (letter) {
		case 'C': return 0;
		case 'D': return 2;
		case 'E': return 4;
		case 'F': return 5;
		case 'G': return 7;
		case 'A': return 9;
		case 'B': return 11;
		default: return -1;
	}
}


Key irealParseKey(const std::string& field) {
	Key key;
	if (field.empty())
		return key;
	int pc = letterPitchClass(field[0]);
	if (pc < 0)
		return key;
	size_t i = 1;
	if (i < field.size() && (field[i] == '#' || field[i] == 'b')) {
		pc += (field[i] == '#') ? 1 : -1;
		i++;
	}
	// A trailing minus is what iReal writes for a minor key.
	key.minor = (i < field.size() && field[i] == '-');
	key.tonic = (int8_t) ((pc % 12 + 12) % 12);
	return key;
}


/** The quality of an iReal symbol, from the characters after the root. Longest match first,
because "-7" must not be read as "-" and "^7" must not be read as "^". Anything unrecognised
falls back to the nearest thing rather than to nothing: an altered dominant nobody has spelled
out is still a dominant, and playing it as one is right where refusing it is not. */
static uint8_t qualityOf(const std::string& q) {
	struct Entry { const char* text; uint8_t quality; };
	static const Entry TABLE[] = {
		{"o7", Q_DIM7}, {"h7", Q_HALFDIM}, {"-^7", Q_MINMAJ7}, {"-^9", Q_MINMAJ7},
		{"^13", Q_MAJ9}, {"^9", Q_MAJ9}, {"^7", Q_MAJ7}, {"^", Q_MAJ7},
		{"-11", Q_MIN9}, {"-9", Q_MIN9}, {"-7", Q_MIN7}, {"-6", Q_MIN6}, {"-69", Q_MIN6},
		{"-", Q_MINOR},
		{"7sus", Q_DOM7SUS4}, {"sus", Q_SUS4},
		{"7alt", Q_DOM7ALT}, {"alt", Q_DOM7ALT},
		{"13", Q_THIRTEEN}, {"11", Q_ELEVEN}, {"9", Q_NINE}, {"7", Q_DOM7},
		{"69", Q_SIX}, {"6", Q_SIX},
		{"o", Q_DIM}, {"h", Q_HALFDIM}, {"+", Q_AUG}, {"5", Q_FIVE}, {"2", Q_SUS2},
	};
	if (q.empty())
		return Q_MAJOR;
	for (const Entry& e : TABLE) {
		if (q.compare(0, std::strlen(e.text), e.text) == 0)
			return e.quality;
	}
	// Alterations written on their own — "7b9", "7#11" — begin with a digit already covered
	// above, so anything left here is a spelling this reader does not know. A major triad is
	// the least wrong answer.
	return Q_MAJOR;
}


bool irealParseChord(const std::string& symbol, const Key& key, Chord& out) {
	if (symbol.empty())
		return false;
	const int rootPc = letterPitchClass(symbol[0]);
	if (rootPc < 0)
		return false;

	size_t i = 1;
	int pc = rootPc;
	// HOW IT WAS WRITTEN, kept, because the chart is the authority on its own spelling. F sharp
	// and G flat are the same pitch and not the same chord, and guessing between them goes
	// wrong in exactly the places a musician looks first.
	int written = 0;
	if (i < symbol.size() && (symbol[i] == '#' || symbol[i] == 'b')) {
		written = (symbol[i] == '#') ? 1 : -1;
		pc += written;
		i++;
	}
	pc = (pc % 12 + 12) % 12;

	// The quality is everything up to a slash bass, with iReal's size hints removed.
	std::string quality;
	for (; i < symbol.size() && symbol[i] != '/'; i++) {
		if (symbol[i] != 's' && symbol[i] != 'l')
			quality += symbol[i];
	}

	// AS A DEGREE OF THE KEY, which is the whole point: the chart then transposes by changing
	// one number, and every rule that asks about function has its answer directly.
	//
	// A NATURAL DEGREE ALWAYS BEATS AN ALTERED ONE. Reading B in C major as a flattened tonic
	// rather than as the seventh is how a chart comes out spelled C flat, which is not a thing
	// anybody writes. After that, an alteration spelled the way the chart spelled it beats the
	// other one.
	const int* steps = key.minor ? MINOR_STEPS : MAJOR_STEPS;
	int degree = 1, accidental = 0;
	int best = 9999;
	for (int d = 0; d < 7; d++) {
		const int natural = ((key.tonic + steps[d]) % 12 + 12) % 12;
		for (int a = -1; a <= 1; a++) {
			if (((natural + a) % 12 + 12) % 12 != pc)
				continue;
			int cost = d;
			if (a != 0)
				cost += (a == written && written != 0) ? 100 : 300;
			if (cost < best) {
				best = cost;
				degree = d + 1;
				accidental = a;
			}
		}
	}

	out.valid = true;
	out.degree = (int8_t) degree;
	out.accidental = (int8_t) accidental;
	out.quality = qualityOf(quality);
	return true;
}


// ---- the tokeniser --------------------------------------------------------------------------

static bool startsWith(const std::string& s, size_t at, const char* text) {
	return s.compare(at, std::strlen(text), text) == 0;
}


/** Reads one chord symbol at `at`, returning its length or zero. The vocabulary is a root of A
to G, or W meaning "the same as the last one", then quality characters, then an optional slash
bass. */
static size_t chordLength(const std::string& s, size_t at) {
	if (at >= s.size())
		return 0;
	const char c = s[at];
	if (!((c >= 'A' && c <= 'G') || c == 'W'))
		return 0;
	size_t i = at + 1;
	static const char* QUALITY_CHARS = "+-^0123456789hob#suadlt";
	while (i < s.size() && std::strchr(QUALITY_CHARS, s[i]) != NULL)
		i++;
	if (i < s.size() && s[i] == '/') {
		size_t j = i + 1;
		if (j < s.size() && s[j] >= 'A' && s[j] <= 'G') {
			j++;
			if (j < s.size() && (s[j] == '#' || s[j] == 'b'))
				j++;
			i = j;
		}
	}
	return i - at;
}


static std::vector<Cell> tokenise(const std::string& body, const Key& key) {
	std::vector<Cell> cells;
	std::string lastHead;
	size_t i = 0;
	int guard = 0;

	while (i < body.size()) {
		if (++guard > 200000)
			break;
		const char c = body[i];

		// Whitespace and the size hints carry nothing.
		if (c == ' ' || c == 's' || c == 'l') {
			i++;
			continue;
		}

		// Longest tokens first, or a control token is read as a chord.
		if (startsWith(body, i, "XyQ")) {
			Cell cell; cell.kind = Cell::EMPTY; cells.push_back(cell); i += 3; continue;
		}
		if (startsWith(body, i, "Kcl")) {
			Cell cell; cell.kind = Cell::REPEAT_HELD; cells.push_back(cell); i += 3; continue;
		}
		if (startsWith(body, i, "LZ")) {
			Cell cell; cell.kind = Cell::BAR; cells.push_back(cell); i += 2; continue;
		}
		if (c == '*' && i + 1 < body.size()
			&& (std::isalnum((unsigned char) body[i + 1]) || body[i + 1] == '_')) {
			// A LETTER OR A DIGIT after the star, and nothing else. A star followed by
			// punctuation is not a rehearsal mark, and taking it as one gave charts sections
			// labelled with a comma or a bracket. Four charts in two thousand, found by
			// running this against the reader it was ported from.
			Cell cell; cell.kind = Cell::SECTION; cell.label = body[i + 1];
			cells.push_back(cell); i += 2; continue;
		}
		if (c == '<') {
			// A comment. No harmony in it, but iReal writes the NAVIGATION as comments —
			// "D.C. al Coda", "Fine", "3x" — so the text is kept and classified later rather
			// than thrown away here.
			// AND ONLY WHEN IT CLOSES. An opening bracket with no closing one is not a
			// comment, and reading it as one swallowed the whole rest of the chart —
			// "Turnaround" lost a third of its bars that way. Unmatched, the bracket is
			// simply not a token.
			const size_t close = body.find('>', i);
			if (close == std::string::npos) {
				i++;
				continue;
			}
			Cell cell;
			cell.kind = Cell::COMMENT;
			cell.raw = body.substr(i + 1, close - i - 1);
			cells.push_back(cell);
			i = close + 1;
			continue;
		}
		if (c == 'T' && i + 2 < body.size()
			&& std::isdigit((unsigned char) body[i + 1])
			&& std::isdigit((unsigned char) body[i + 2])) {
			Cell cell; cell.kind = Cell::TIME;
			cell.beats = body[i + 1] - '0';
			cell.unit = body[i + 2] - '0';
			cells.push_back(cell); i += 3; continue;
		}
		if (c == 'N' && i + 1 < body.size() && std::isdigit((unsigned char) body[i + 1])) {
			Cell cell; cell.kind = Cell::ENDING; cell.ending = body[i + 1] - '0';
			cells.push_back(cell); i += 2; continue;
		}
		if (c == 'Y') {
			while (i < body.size() && body[i] == 'Y')
				i++;
			continue;
		}

		switch (c) {
			case 'x': { Cell k; k.kind = Cell::REPEAT_BAR; cells.push_back(k); i++; continue; }
			case 'r': { Cell k; k.kind = Cell::REPEAT_TWO; cells.push_back(k); i++; continue; }
			case 'n': {
				Cell k; k.kind = Cell::CHORD; k.noChord = true; k.raw = "n";
				cells.push_back(k); i++; continue;
			}
			case '{': { Cell k; k.kind = Cell::REPEAT_OPEN; cells.push_back(k); i++; continue; }
			case '}': { Cell k; k.kind = Cell::REPEAT_CLOSE; cells.push_back(k); i++; continue; }
			case '[':
			case ']':
			case '|': { Cell k; k.kind = Cell::BAR; cells.push_back(k); i++; continue; }
			case 'Z': { Cell k; k.kind = Cell::END; cells.push_back(k); i++; continue; }
			case ',': { Cell k; k.kind = Cell::DIVIDER; cells.push_back(k); i++; continue; }
			case 'S': { Cell k; k.kind = Cell::SEGNO; cells.push_back(k); i++; continue; }
			case 'Q': { Cell k; k.kind = Cell::CODA; cells.push_back(k); i++; continue; }
			case 'U':
			case 'p': i++; continue;
			default: break;
		}

		const size_t len = chordLength(body, i);
		if (len > 0) {
			std::string symbol = body.substr(i, len);
			if (symbol[0] == 'W') {
				// W means the chord before it, keeping any bass written after the W.
				symbol = lastHead + symbol.substr(1);
			}
			else {
				const size_t slash = symbol.find('/');
				lastHead = (slash == std::string::npos) ? symbol : symbol.substr(0, slash);
			}
			Cell cell;
			cell.kind = Cell::CHORD;
			cell.raw = symbol;
			if (!irealParseChord(symbol, key, cell.chord))
				cell.noChord = true;
			cells.push_back(cell);
			i += len;
			continue;
		}

		// Something this reader does not know. Skipped rather than refused, which is what the
		// reference parser does and what keeps one odd character from losing a whole chart.
		i++;
	}
	return cells;
}


// ---- songs ----------------------------------------------------------------------------------

static std::string trimmed(const std::string& s) {
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char) s[a])) a++;
	while (b > a && std::isspace((unsigned char) s[b - 1])) b--;
	return s.substr(a, b - a);
}


Song irealParseSong(const std::string& chunk) {
	Song song;

	// Fields are separated by RUNS of "=", so an empty field between two of them collapses and
	// the positions stay where they belong.
	std::vector<std::string> fields;
	size_t i = 0;
	while (i <= chunk.size()) {
		size_t at = chunk.find('=', i);
		if (at == std::string::npos) {
			fields.push_back(chunk.substr(i));
			break;
		}
		fields.push_back(chunk.substr(i, at - i));
		while (at < chunk.size() && chunk[at] == '=')
			at++;
		i = at;
	}

	song.title = fields.size() > 0 ? trimmed(fields[0]) : "";
	song.composer = fields.size() > 1 ? trimmed(fields[1]) : "";
	song.style = fields.size() > 2 ? trimmed(fields[2]) : "";
	song.key = irealParseKey(fields.size() > 3 ? trimmed(fields[3]) : "");

	std::string body = fields.size() > 4 ? fields[4] : "";
	if (body.compare(0, std::strlen(BODY_PREFIX), BODY_PREFIX) == 0)
		body = body.substr(std::strlen(BODY_PREFIX));
	song.cells = tokenise(irealUnscramble(body), song.key);

	// A time signature this reader cannot lay out in bars is said aloud rather than played
	// wrongly. Four four and three four cover almost everything in a real export.
	for (const Cell& cell : song.cells) {
		if (cell.kind != Cell::TIME)
			continue;
		song.beats = cell.beats;
		song.unit = cell.unit;
		if (cell.unit != 4 || (cell.beats != 3 && cell.beats != 4)) {
			song.supported = false;
			char buf[64];
			std::snprintf(buf, sizeof(buf), "time signature %d/%d", cell.beats, cell.unit);
			song.why = buf;
		}
		break;
	}
	return song;
}


std::vector<Song> irealParsePlaylist(const std::string& payload, std::string* name) {
	std::vector<Song> songs;
	std::vector<std::string> chunks;
	size_t i = 0;
	while (true) {
		const size_t at = payload.find("===", i);
		if (at == std::string::npos) {
			chunks.push_back(payload.substr(i));
			break;
		}
		chunks.push_back(payload.substr(i, at - i));
		i = at + 3;
	}
	// The last piece is the playlist's name rather than a song.
	if (chunks.size() > 1) {
		if (name)
			*name = trimmed(chunks.back());
		chunks.pop_back();
	}
	for (const std::string& chunk : chunks) {
		if (trimmed(chunk).empty())
			continue;
		songs.push_back(irealParseSong(chunk));
	}
	return songs;
}


// ---- expansion ------------------------------------------------------------------------------

/** A bar as it is being gathered: the chords in it and where its cells fall. */
struct BarBuild {
	std::vector<Chord> chords;
	std::vector<bool> noChords;
	char section = 0;
	bool held = false;      /**< Kcl: hold what was sounding through this bar. */
	int repeatBack = 0;     /**< x is one bar back, r is two. */
};


Expansion irealExpand(const Song& song) {
	Expansion out;
	const int beatsPerBar = (song.unit == 4 && song.beats == 3) ? 3 : 4;

	// FIRST, THE BARS AS WRITTEN, with their repeat marks noted. Nothing is unwrapped yet.
	std::vector<BarBuild> bars;
	std::vector<int> repeatOpenAt;      // bar index where a repeat opened
	std::vector<int> repeatCloseAt;     // bar index the repeat closes after
	std::vector<int> endingAt;          // bar index where an ending begins
	std::vector<int> endingNumber;

	BarBuild bar;
	char pendingSection = 0;
	int pendingEnding = 0;
	bool pendingOpen = false;

	auto closeBar = [&]() {
		if (bar.chords.empty() && !bar.held && bar.repeatBack == 0 && bar.section == 0)
			return;
		if (pendingOpen) {
			repeatOpenAt.push_back((int) bars.size());
			pendingOpen = false;
		}
		if (pendingEnding > 0) {
			endingAt.push_back((int) bars.size());
			endingNumber.push_back(pendingEnding);
			pendingEnding = 0;
		}
		if (pendingSection != 0) {
			bar.section = pendingSection;
			pendingSection = 0;
		}
		bars.push_back(bar);
		bar = BarBuild();
	};

	for (const Cell& cell : song.cells) {
		switch (cell.kind) {
			case Cell::CHORD:
				bar.chords.push_back(cell.chord);
				bar.noChords.push_back(cell.noChord);
				break;
			case Cell::BAR:
			case Cell::END:
				closeBar();
				break;
			case Cell::REPEAT_CLOSE:
				closeBar();
				repeatCloseAt.push_back((int) bars.size() - 1);
				break;
			case Cell::REPEAT_OPEN:
				pendingOpen = true;
				break;
			case Cell::SECTION:
				pendingSection = cell.label;
				break;
			case Cell::ENDING:
				pendingEnding = cell.ending;
				break;
			case Cell::REPEAT_HELD:
				bar.held = true;
				break;
			case Cell::REPEAT_BAR:
				bar.repeatBack = 1;
				break;
			case Cell::REPEAT_TWO:
				bar.repeatBack = 2;
				break;
			default:
				break;
		}
	}
	closeBar();

	// SECOND, WALK THE BARS, TAKING THE REPEATS. A repeat is played twice; where endings are
	// written, the first pass takes ending one and the second takes ending two.
	std::vector<int> order;
	int at = 0;
	int guard = 0;
	int pass = 1;
	int openBar = 0;
	bool repeating = false;

	auto endingHere = [&](int b) {
		for (size_t k = 0; k < endingAt.size(); k++) {
			if (endingAt[k] == b)
				return endingNumber[k];
		}
		return 0;
	};
	auto opensHere = [&](int b) {
		for (int o : repeatOpenAt) {
			if (o == b)
				return true;
		}
		return false;
	};
	auto closesHere = [&](int b) {
		for (int c : repeatCloseAt) {
			if (c == b)
				return true;
		}
		return false;
	};

	while (at < (int) bars.size() && ++guard < 4000) {
		if (opensHere(at) && !repeating) {
			openBar = at;
			repeating = true;
			pass = 1;
		}
		const int ending = endingHere(at);
		if (ending > 0 && ending != pass) {
			// An ending belonging to another pass: skip to the next ending or past them.
			int skip = at + 1;
			while (skip < (int) bars.size() && endingHere(skip) == 0)
				skip++;
			at = skip;
			continue;
		}
		order.push_back(at);
		if (closesHere(at) && repeating && pass == 1) {
			pass = 2;
			at = openBar;
			continue;
		}
		if (closesHere(at))
			repeating = false;
		at++;
	}

	// THIRD, GIVE EVERY CHORD ITS TIME. A bar's chords divide it evenly, which is what iReal
	// means by two chords in a bar. A held bar or a repeated one takes what came before it.
	float beat = 0.f;
	std::vector<Chord> previous;
	std::vector<bool> previousNo;
	for (size_t k = 0; k < order.size(); k++) {
		const BarBuild& b = bars[order[k]];
		std::vector<Chord> chords = b.chords;
		std::vector<bool> noChords = b.noChords;

		if (b.repeatBack > 0 || b.held || chords.empty()) {
			// Take the bar this one stands in for. One back for x and for a held bar; two back
			// for r, which stands in for a pair.
			const int back = (b.repeatBack == 2) ? 2 : 1;
			const int from = (int) k - back;
			if (from >= 0) {
				const BarBuild& src = bars[order[from]];
				chords = src.chords;
				noChords = src.noChords;
			}
			if (chords.empty()) {
				chords = previous;
				noChords = previousNo;
			}
		}
		if (chords.empty()) {
			beat += beatsPerBar;
			out.bars++;
			continue;
		}

		const float each = beatsPerBar / (float) chords.size();
		for (size_t c = 0; c < chords.size(); c++) {
			Span span;
			span.chord = chords[c];
			span.noChord = c < noChords.size() ? noChords[c] : false;
			span.startBeat = beat + c * each;
			span.endBeat = span.startBeat + each;
			span.section = (c == 0) ? b.section : 0;
			out.spans.push_back(span);
		}
		previous = chords;
		previousNo = noChords;
		beat += beatsPerBar;
		out.bars++;
	}

	out.totalBeats = beat;
	return out;
}


} // namespace px
