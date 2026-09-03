#include "Chord.hpp"

namespace px {


static const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
static const int MINOR[7] = {0, 2, 3, 5, 7, 8, 10};

/** Intervals above the root, in semitones. Three notes or four. */
static const int8_t QUALITY_TONES[NUM_QUALITIES][4] = {
	{0, 4, 7, -1},    // major
	{0, 3, 7, -1},    // minor
	{0, 4, 7, 10},    // dominant seventh
	{0, 4, 7, 11},    // major seventh
	{0, 3, 7, 10},    // minor seventh
	{0, 3, 6, -1},    // diminished
	{0, 3, 6, 10},    // half diminished
	{0, 4, 8, -1},    // augmented
	{0, 5, 7, -1},    // suspended fourth
	{0, 4, 7, 9},     // sixth
	{0, 3, 7, 9},     // minor sixth
	{0, 3, 6, 9},     // diminished seventh
	{0, 3, 7, 11},    // minor major seventh
	{0, 4, 7, 10},    // ninth, taken as its dominant seventh
	{0, 3, 7, 10},    // minor ninth
	{0, 4, 7, 11},    // major ninth
	{0, 4, 7, 10},    // eleventh
	{0, 4, 7, 10},    // thirteenth
	{0, 4, 7, 10},    // altered dominant
	{0, 2, 7, -1},    // suspended second
	{0, 5, 7, 10},    // dominant seventh suspended fourth
	{0, 7, -1, -1},   // fifth, which is a chord with no third at all
};

static const char* QUALITY_SUFFIX[NUM_QUALITIES] = {
	"", "m", "7", "maj7", "m7", "°", "ø7", "+", "sus4",
	"6", "m6", "°7", "mMaj7", "9", "m9", "maj9", "11", "13", "7alt", "sus2", "7sus4", "5",
};

/** Whether the numeral is written in lower case: the third is minor, so the eye can tell a two
from a five without reading the suffix. */
static bool minorNumeral(int quality) {
	switch (quality) {
		case Q_MINOR: case Q_MIN7: case Q_DIM: case Q_HALFDIM:
		case Q_MIN6: case Q_DIM7: case Q_MINMAJ7: case Q_MIN9:
			return true;
		default:
			return false;
	}
}

static const char* ROMAN_UPPER[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
static const char* ROMAN_LOWER[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
static const char* PITCH_NAMES[12] = {
	"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};
static const char* FLAT_NAMES[12] = {
	"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B",
};

/** Whether this key is written with flats. Taken from the circle of fifths: F, B flat, E flat,
A flat, D flat and G flat major carry flats, and a minor key follows its relative major. */
static bool keyUsesFlats(const Key& key) {
	int major = ((key.tonic % 12) + 12) % 12;
	if (key.minor)
		major = (major + 3) % 12;
	switch (major) {
		case 5: case 10: case 3: case 8: case 1: case 6:
			return true;
		default:
			return false;
	}
}


const char* pitchClassName(int pc) {
	return PITCH_NAMES[((pc % 12) + 12) % 12];
}

const char* pitchClassNameIn(int pc, const Key& key) {
	const int i = ((pc % 12) + 12) % 12;
	return keyUsesFlats(key) ? FLAT_NAMES[i] : PITCH_NAMES[i];
}

const char* qualityName(int quality) {
	if (quality < 0 || quality >= NUM_QUALITIES)
		return "";
	return QUALITY_SUFFIX[quality];
}

void scalePitchClasses(const Key& key, int* out) {
	const int* ivs = key.minor ? MINOR : MAJOR;
	for (int i = 0; i < 7; i++)
		out[i] = ((key.tonic + ivs[i]) % 12 + 12) % 12;
}

int chordRootPitchClass(const Chord& chord, const Key& key) {
	const int* ivs = key.minor ? MINOR : MAJOR;
	const int d = clamp((int) chord.degree, 1, 7) - 1;
	return ((key.tonic + ivs[d] + chord.accidental) % 12 + 12) % 12;
}

int chordPitchClasses(const Chord& chord, const Key& key, int* out) {
	const int root = chordRootPitchClass(chord, key);
	const int q = clamp((int) chord.quality, 0, NUM_QUALITIES - 1);
	int n = 0;
	for (int i = 0; i < 4; i++) {
		if (QUALITY_TONES[q][i] < 0)
			break;
		out[n++] = (root + QUALITY_TONES[q][i]) % 12;
	}
	return n;
}

std::string chordRoman(const Chord& chord) {
	const int d = clamp((int) chord.degree, 1, 7) - 1;
	const int q = clamp((int) chord.quality, 0, NUM_QUALITIES - 1);
	std::string s;
	if (chord.accidental < 0)
		s += "b";
	else if (chord.accidental > 0)
		s += "#";
	s += minorNumeral(q) ? ROMAN_LOWER[d] : ROMAN_UPPER[d];
	// The numeral already says the third is minor, so the suffix does not repeat it.
	if (q == Q_MINOR)
		return s;
	if (q == Q_MIN7)
		return s + "7";
	if (q == Q_MIN6)
		return s + "6";
	if (q == Q_MIN9)
		return s + "9";
	if (q == Q_MINMAJ7)
		return s + "Maj7";
	return s + QUALITY_SUFFIX[q];
}

/** Which letter a degree of this key is written on. C major's third is E whatever it has been
altered to, so an altered degree keeps its letter and changes its accidental. */
static int letterIndexOfTonic(const Key& key) {
	static const int LETTER_PC[7] = {0, 2, 4, 5, 7, 9, 11};   // C D E F G A B
	const int pc = ((key.tonic % 12) + 12) % 12;
	// The letter whose natural is at or just below the tonic, choosing the one the key's own
	// spelling uses: A flat is written on A, and G sharp on G.
	int best = 0;
	for (int i = 0; i < 7; i++) {
		const int d = ((pc - LETTER_PC[i]) % 12 + 12) % 12;
		if (d == 0)
			return i;
		if (keyUsesFlats(key) ? (d == 11) : (d == 1))
			best = i;
	}
	return best;
}

/** THE SMuFL CODEPOINTS we use out of Petaluma. Named rather than written as numbers, because
E873 says nothing and csymMajorSeventh says everything. */
static const char* SYM_MAJ7      = "";
static const char* SYM_DIM       = "";
static const char* SYM_HALFDIM   = "";
static const char* SYM_AUG       = "";
static const char* SYM_FLAT      = "";   /**< The chord-symbol flat, cut for this size. */
static const char* SYM_SHARP     = "";

static void addText(ChordText& out, const std::string& text, bool raised) {
	if (text.empty())
		return;
	// Runs of the same kind join, so "sus" and "4" are one call to the text engine.
	if (!out.parts.empty() && !out.parts.back().music
		&& out.parts.back().raised == raised) {
		out.parts.back().text += text;
		return;
	}
	ChordRun run;
	run.text = text;
	run.raised = raised;
	out.parts.push_back(run);
}

static void addSymbol(ChordText& out, const char* symbol, bool raised) {
	ChordRun run;
	run.text = symbol;
	run.music = true;
	run.raised = raised;
	out.parts.push_back(run);
}

/** The quality, set as a chart sets it: the triangle for a major seventh, the small circle for a
diminished, the slashed circle for a half diminished, and real flats and sharps in alterations.
Everything that is a SYMBOL becomes a glyph from the music font; everything that is a NUMBER or a
letter stays in the text face. */
static void engraveQuality(ChordText& out, const std::string& suffix) {
	for (size_t i = 0; i < suffix.size(); i++) {
		if (suffix.compare(i, 4, "maj7") == 0) {
			addSymbol(out, SYM_MAJ7, true);
			addText(out, "7", true);
			i += 3;
			continue;
		}
		if (suffix.compare(i, 4, "Maj7") == 0) {
			addSymbol(out, SYM_MAJ7, true);
			addText(out, "7", true);
			i += 3;
			continue;
		}
		// The degree sign and the slashed circle arrive from QUALITY_SUFFIX as UTF-8 already.
		if (suffix.compare(i, 2, "°") == 0) {
			addSymbol(out, SYM_DIM, true);
			i += 1;
			continue;
		}
		if (suffix.compare(i, 2, "ø") == 0) {
			addSymbol(out, SYM_HALFDIM, true);
			i += 1;
			continue;
		}
		if (suffix[i] == '+') {
			addSymbol(out, SYM_AUG, true);
			continue;
		}
		if (suffix[i] == 'b') {
			addSymbol(out, SYM_FLAT, true);
			continue;
		}
		if (suffix[i] == '#') {
			addSymbol(out, SYM_SHARP, true);
			continue;
		}
		addText(out, std::string(1, suffix[i]), true);
	}
}

ChordText chordTextLetter(const Chord& chord, const Key& key) {
	ChordText out;
	const std::string whole = chordLetter(chord, key);
	const int q = clamp((int) chord.quality, 0, NUM_QUALITIES - 1);
	addText(out, whole.substr(0, 1), false);
	size_t i = 1;
	while (i < whole.size() && (whole[i] == '#' || whole[i] == 'b')) {
		addSymbol(out, whole[i] == 'b' ? SYM_FLAT : SYM_SHARP, true);
		i++;
	}
	engraveQuality(out, QUALITY_SUFFIX[q]);
	return out;
}

ChordText chordTextRoman(const Chord& chord) {
	ChordText out;
	const int d = clamp((int) chord.degree, 1, 7) - 1;
	const int q = clamp((int) chord.quality, 0, NUM_QUALITIES - 1);
	// Roman writes the accidental in FRONT of the numeral: a flat six is a flat, then VI.
	if (chord.accidental < 0)
		addSymbol(out, SYM_FLAT, false);
	else if (chord.accidental > 0)
		addSymbol(out, SYM_SHARP, false);
	addText(out, minorNumeral(q) ? ROMAN_LOWER[d] : ROMAN_UPPER[d], false);
	// The numeral already says the third is minor, so the suffix does not repeat it.
	if (q == Q_MINOR)
		return out;
	if (q == Q_MIN7)
		addText(out, "7", true);
	else if (q == Q_MIN6)
		addText(out, "6", true);
	else if (q == Q_MIN9)
		addText(out, "9", true);
	else if (q == Q_MINMAJ7) {
		addSymbol(out, SYM_MAJ7, true);
		addText(out, "7", true);
	}
	else
		engraveQuality(out, QUALITY_SUFFIX[q]);
	return out;
}

std::string chordLetter(const Chord& chord, const Key& key) {
	static const char* LETTERS = "CDEFGAB";
	static const int LETTER_PC[7] = {0, 2, 4, 5, 7, 9, 11};
	const int q = clamp((int) chord.quality, 0, NUM_QUALITIES - 1);

	// SPELLED FROM THE DEGREE, not from the pitch class. A flattened seventh in C is B flat and
	// never A sharp, and the chord already knows it is a seventh — which is what the degree
	// storage is for. Spelling from the pitch class alone loses that and a musician sees it at
	// once, especially in a chromatic descent.
	const int li = (letterIndexOfTonic(key) + clamp((int) chord.degree, 1, 7) - 1) % 7;
	const int rootPc = chordRootPitchClass(chord, key);
	int delta = ((rootPc - LETTER_PC[li]) % 12 + 12) % 12;
	if (delta > 6)
		delta -= 12;

	std::string s(1, LETTERS[li]);
	for (int i = 0; i < delta && i < 2; i++)
		s += "#";
	for (int i = 0; i > delta && i > -2; i--)
		s += "b";
	return s + QUALITY_SUFFIX[q];
}


} // namespace px
