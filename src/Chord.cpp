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
};

static const char* QUALITY_SUFFIX[NUM_QUALITIES] = {
	"", "m", "7", "maj7", "m7", "°", "ø7", "+", "sus4",
};

/** Whether the numeral is written in lower case: the third is minor, so the eye can tell a two
from a five without reading the suffix. */
static bool minorNumeral(int quality) {
	return quality == Q_MINOR || quality == Q_MIN7 || quality == Q_DIM || quality == Q_HALFDIM;
}

static const char* ROMAN_UPPER[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
static const char* ROMAN_LOWER[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
static const char* PITCH_NAMES[12] = {
	"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};


const char* pitchClassName(int pc) {
	return PITCH_NAMES[((pc % 12) + 12) % 12];
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
	return s + QUALITY_SUFFIX[q];
}

std::string chordLetter(const Chord& chord, const Key& key) {
	const int q = clamp((int) chord.quality, 0, NUM_QUALITIES - 1);
	return std::string(pitchClassName(chordRootPitchClass(chord, key))) + QUALITY_SUFFIX[q];
}


} // namespace px
