#pragma once
/** HOW A PIECE'S ORNAMENTS ARE PLAYED, worked out from who wrote it. See docs/ornament-styles.md.

A TRILL IN BACH IS NOT A TRILL IN BRAHMS. Where it starts, how fast it goes, whether it ends with a
turn nobody wrote, how quick a mordent is and how slowly a chord is rolled all changed between 1700
and 1900, and the sources say how. Nothing in a score says which of them it wants — the sign is the
same sign — but the score almost always says who wrote it, and that is enough.

THE STYLE IS GUESSED FROM THE FILE, never set: the composer field, the credits printed on the page,
the work and movement titles, catalogue numbers, and the file's own name, each trusted as far as
it deserves, with misspellings forgiven and arrangers and lyricists set aside. Where no composer is
named, a year or the chord symbols of a lead sheet decide; where nothing does, the style is the
later Romantic one, which is how ornaments are generally played now. A module's menu can override
the guess, and says what was guessed and why.

NO RACK IN HERE: the recogniser and the rules are plain C++, so a bench can run them over every
score in a folder. */
#include <string>
#include <vector>

namespace px {


enum OrnamentStyle {
	STYLE_BAROQUE,          /**< Bach, Handel, Scarlatti, Couperin, Rameau. */
	STYLE_CLASSICAL,        /**< Haydn, Mozart, Clementi. */
	STYLE_BEETHOVEN,
	STYLE_EARLY_ROMANTIC,   /**< Hummel, Schubert, Weber, Mendelssohn. */
	STYLE_CHOPIN,
	STYLE_LATE_ROMANTIC,    /**< Schumann, Liszt, Brahms, Fauré; and the default. */
	STYLE_MODERN,           /**< Debussy, Ravel, Schoenberg and after. */
	STYLE_JAZZ,             /**< Ragtime, jazz and popular song. */
	STYLE_COUNT
};

/** "Baroque", "Classical", "Beethoven" and so on. */
const char* ornamentStyleName(int style);


/** WHAT A STYLE DOES WITH EACH SIGN. The figures and their sources are in docs/ornament-styles.md. */
struct OrnamentRules {
	/** A TRILL STARTS ON THE NOTE ABOVE, unless something written says otherwise: a grace note
	before it, or the note above already sounding and slurred in. */
	bool trillFromAbove = false;
	/** HOW FAST A TRILL GOES, in notes a second, in the middle of the keyboard: a little slower in
	the bass and a little faster at the top. */
	float trillPerSecond = 12.5f;
	/** A TURN ADDED TO THE END OF A TRILL A BEAT OR LONGER that has none written: the note below,
	then the written note. */
	bool trillTurnAdded = false;
	/** HOW LONG EACH QUICK NOTE OF A MORDENT IS, in seconds. Never more than half the note they
	share between them. */
	float mordentSeconds = 0.045f;
	/** And each note of a turn. */
	float turnSeconds = 0.07f;
	/** HOW FAR APART THE NOTES OF A ROLLED CHORD ARE, in seconds. */
	float rollSeconds = 0.035f;
	/** WHEN ONE HAND ROLLS A CHORD AND THE OTHER DOES NOT, the other lands with the roll's top
	note. Otherwise, which is how this plugin plays it unless a source says different, it lands
	with the first. */
	bool rollOthersWithTop = false;
	/** MORDENTS AND TURNS COME BEFORE THE BEAT, landing on the written note: a pickup, as jazz
	plays them. Otherwise they start on it. */
	bool figuresBeforeBeat = false;
};

const OrnamentRules& ornamentRules(int style);


/** ONE PIECE OF TEXT FROM A SCORE THAT MIGHT NAME ITS COMPOSER, and where it came from. */
enum ClueKind {
	CLUE_COMPOSER,   /**< A creator or credit marked as the composer. */
	CLUE_CREATOR,    /**< A creator with no type. */
	CLUE_CREDIT,     /**< Text printed on the page: a title, a subtitle, an untyped credit. */
	CLUE_TITLE,      /**< The work or movement title, or its number. */
	CLUE_FILE,       /**< The file's name. */
	CLUE_LYRICIST,   /**< A lyricist or poet: weak, since lyricists are named as often as composers. */
	CLUE_RIGHTS,     /**< The copyright line. */
};

struct StyleClue {
	std::string text;
	int kind = CLUE_CREDIT;
	StyleClue() {}
	StyleClue(const std::string& text, int kind) : text(text), kind(kind) {}
};

/** WHAT WAS GUESSED, and why, for a menu to say. */
struct StyleGuess {
	int style = STYLE_LATE_ROMANTIC;
	/** True where something in the file decided it, false where it is the default. */
	bool found = false;
	/** The composer recognised, or empty. */
	std::string composer;
	/** A short phrase saying what decided it: `the credits name Ludwig van Beethoven`. */
	std::string because;
};

/** `chordSymbols` is how many chord symbols the score carries, which marks a lead sheet. */
StyleGuess guessStyle(const std::vector<StyleClue>& clues, int chordSymbols);


/** ONE ENTRY OF THE COMPOSER TABLE. See Composers.cpp. */
struct Composer {
	const char* name;
	/** The spellings a score may use, lower case and plain, separated by bars. */
	const char* variants;
	int born, died;
	int style;
};

extern const Composer COMPOSERS[];
extern const int COMPOSER_COUNT;


} // namespace px
