#pragma once
/** Chords, stored as a degree of the key rather than as a letter.

WHY ROMAN. A chart stored in letters has to be worked out against the key every time anything
asks a question about it, and got wrong at every modulation. Stored as a degree and a quality it
answers the question every rule actually asks — is this the tonic, is this a dominant, what is
this one's function — and transposing is a change of one number.

It is also what makes a reharmoniser possible at all, since its rules are about function.
*/
#include <rack.hpp>
#include <string>

using namespace rack;

namespace px {

enum ChordQuality {
	Q_MAJOR,
	Q_MINOR,
	Q_DOM7,
	Q_MAJ7,
	Q_MIN7,
	Q_DIM,
	Q_HALFDIM,
	Q_AUG,
	Q_SUS4,
	// The rest are what a real chart uses. Nine was enough for a progression somebody types;
	// an iReal export asks for sixths, ninths, elevenths, thirteenths, altered dominants,
	// minor-major sevenths and diminished sevenths within the first dozen songs.
	Q_SIX,
	Q_MIN6,
	Q_DIM7,
	Q_MINMAJ7,
	Q_NINE,
	Q_MIN9,
	Q_MAJ9,
	Q_ELEVEN,
	Q_THIRTEEN,
	Q_DOM7ALT,
	Q_SUS2,
	Q_DOM7SUS4,
	Q_FIVE,
	NUM_QUALITIES,
};

struct Chord {
	bool valid = false;
	/** 1 to 7. */
	int8_t degree = 1;
	/** -1 flat, 0 natural, 1 sharp — so a flat sixth or a sharp fourth can be written. */
	int8_t accidental = 0;
	uint8_t quality = Q_MAJOR;
};

/** A key: a tonic pitch class and a mode. */
struct Key {
	int8_t tonic = 0;
	bool minor = false;
};

/** The pitch class this chord is rooted on. */
int chordRootPitchClass(const Chord& chord, const Key& key);

/** The chord's tones as pitch classes, ascending from the root. Returns how many were written,
which is three or four. */
int chordPitchClasses(const Chord& chord, const Key& key, int* out);

/** The key's scale as pitch classes, ascending from the tonic. Seven of them. */
void scalePitchClasses(const Key& key, int* out);

/** How the chord is written as a degree — "ii7", "V7", "vii°". Lower case where the third
is minor, which is how a Roman analysis is written and how the eye tells them apart. */
std::string chordRoman(const Chord& chord);

/** How the chord is written as a letter in this key — "Dm7", "G7". Sharps rather than flats,
since one spelling has to be chosen and this one needs no key signature to work out. */
std::string chordLetter(const Chord& chord, const Key& key);

/** The quality's suffix on its own, for a panel that shows the two separately. */
const char* qualityName(int quality);

/** A pitch class as a letter, using sharps. */
const char* pitchClassName(int pc);

/** A pitch class as a letter, SPELLED FOR THE KEY. B flat major writes B flat, not A sharp — a
chart spelled the other way is readable but wrong, and a musician notices immediately. */
const char* pitchClassNameIn(int pc, const Key& key);

} // namespace px
