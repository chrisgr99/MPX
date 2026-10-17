#pragma once
/** THE TROMBONE'S TABLES: phase 6 of docs/brass.md.

WHAT A PLAYER KNOWS BEFORE PLAYING. For each slide position and each resonance it can sound there: how the
lips are set to sound the note 1.1 per cent over the resonance, as trombonists play (B2), at each mouth
pressure from the threshold up; and how loud and how bright the note is there, a metre away and 20 degrees
off the bell's axis. With the lips held at one setting the note rises 3 to 5 per cent from pp to ff and loud
notes stop repeating exactly (docs/brass.md, phase 3), so the lips are part of every dynamic, not only of
every note.

Found once on the model at 48 kHz by make brasstables (test/brasstables.cpp), which writes them into
TromboneTable.cpp: the lips tuned by halving at each of a set of pressures a quarter octave apart, the
threshold the lowest at which the note sounds regularly. Between the pressures, everything is read on a
straight line in the logarithm of the pressure.

NO RACK IN HERE. */

namespace px {


/** ONE PRESSURE OF ONE NOTE. */
struct BrassTablePoint {
	float mouth;      ///< pascals
	float lips;       ///< the lips' resonance over the bore's
	float hz;         ///< where the note sounds
	float level;      ///< dB SPL at the listener
	float centroid;   ///< the spectral centroid over the pitch, at the listener
};

/** ONE NOTE: a position and a resonance, and its pressures from the threshold up. */
struct BrassTableNote {
	int position;       ///< 1 to 7
	int resonance;      ///< 1 is the lowest, the pedal's region; 2 is B flat 2 in first position
	float resonanceHz;  ///< the bore's resonance, from BrassBore's calculation
	float threshold;    ///< pascals: the lowest pressure in the table at which it sounds regularly
	int first, count;   ///< its points in BrassTable::points
};

struct BrassTable {
	const BrassTableNote* notes;
	int noteCount;
	const BrassTablePoint* points;
	/** The slide's extension from first position at each position, metres. */
	const float* positions;
	double rate;
};

/** THE TENOR TROMBONE's, as make brasstables found it. */
const BrassTable& tromboneTable();

/** WHAT THE TABLE SAYS OF A NOTE AT A PRESSURE: everything in BrassTablePoint, on straight lines in the
logarithm of the pressure, held at the ends. */
BrassTablePoint brassTableAt(const BrassTable& table, const BrassTableNote& note, double mouth);

/** THE PRESSURE AT WHICH A NOTE REACHES A LEVEL, dB at the listener, held at the table's ends. */
double brassTablePressureFor(const BrassTable& table, const BrassTableNote& note, double level);


} // namespace px
