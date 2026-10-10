/** See OrnamentStyle.hpp and docs/ornament-styles.md. */
#include "OrnamentStyle.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace px {


const char* ornamentStyleName(int style) {
	static const char* NAMES[STYLE_COUNT] = {"Baroque", "Classical", "Beethoven", "Early Romantic",
		"Chopin", "Later Romantic", "Modern", "Jazz"};
	return (style >= 0 && style < STYLE_COUNT) ? NAMES[style] : "Later Romantic";
}


/** THE RULES, ONE ROW A STYLE. Every figure is argued in docs/ornament-styles.md from the sources:
C. P. E. Bach, Quantz, Leopold Mozart, Türk and Clementi for the eighteenth century; Hummel,
Czerny and Taylor's Grove articles for the nineteenth; Newman, Neumann and Rosenblum for Beethoven
and Mozart; Eigeldinger and the Chopin National Edition for Chopin. Where the sources only bound a
figure, the figure is a choice inside the bound, and measured where the local scores spell an
ornament out — the Beethoven sonatas' own engraver played mordents at 45 ms a note. */
static OrnamentRules makeRules(int style) {
	OrnamentRules r;
	switch (style) {
		case STYLE_BAROQUE:
			r.trillFromAbove = true;  r.trillPerSecond = 11.f; r.trillTurnAdded = true;
			r.mordentSeconds = 0.04f; r.turnSeconds = 0.07f;   r.rollSeconds = 0.05f;
			break;
		case STYLE_CLASSICAL:
			r.trillFromAbove = true;  r.trillPerSecond = 11.f; r.trillTurnAdded = true;
			r.mordentSeconds = 0.04f; r.turnSeconds = 0.065f;  r.rollSeconds = 0.04f;
			r.rollOthersWithTop = true;      // Taylor, for Haydn
			break;
		case STYLE_BEETHOVEN:
			r.trillPerSecond = 12.f;
			r.mordentSeconds = 0.045f; r.turnSeconds = 0.065f; r.rollSeconds = 0.04f;
			break;
		case STYLE_EARLY_ROMANTIC:
			r.trillPerSecond = 13.f;
			r.mordentSeconds = 0.045f; r.turnSeconds = 0.07f;  r.rollSeconds = 0.04f;
			break;
		case STYLE_CHOPIN:
			r.trillFromAbove = true;  r.trillPerSecond = 11.f;
			r.mordentSeconds = 0.055f; r.turnSeconds = 0.1f;   r.rollSeconds = 0.045f;
			r.rollOthersWithTop = true;      // the Chopin National Edition
			break;
		case STYLE_MODERN:
			r.trillPerSecond = 16.f;
			r.mordentSeconds = 0.04f; r.turnSeconds = 0.06f;   r.rollSeconds = 0.025f;
			break;
		case STYLE_JAZZ:
			r.trillPerSecond = 16.f;
			r.mordentSeconds = 0.035f; r.turnSeconds = 0.05f;  r.rollSeconds = 0.022f;
			r.figuresBeforeBeat = true;
			break;
		case STYLE_LATE_ROMANTIC:
		default:
			r.trillPerSecond = 14.f;
			r.mordentSeconds = 0.045f; r.turnSeconds = 0.07f;  r.rollSeconds = 0.045f;
			break;
	}
	return r;
}


const OrnamentRules& ornamentRules(int style) {
	static OrnamentRules table[STYLE_COUNT];
	static bool made = false;
	if (!made) {
		for (int s = 0; s < STYLE_COUNT; s++)
			table[s] = makeRules(s);
		made = true;
	}
	return table[(style >= 0 && style < STYLE_COUNT) ? style : STYLE_LATE_ROMANTIC];
}


// ---- reading the text ------------------------------------------------------------------------

/** One code point from UTF-8, advancing `i`; malformed bytes come back as themselves. */
static unsigned nextCodePoint(const std::string& s, size_t& i) {
	const unsigned char c = (unsigned char) s[i++];
	if (c < 0x80)
		return c;
	int more = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
	unsigned cp = c & (0x3F >> more);
	while (more-- > 0 && i < s.size() && ((unsigned char) s[i] & 0xC0) == 0x80)
		cp = (cp << 6) | ((unsigned char) s[i++] & 0x3F);
	return cp;
}

/** THE PLAIN LETTER AN ACCENTED ONE STANDS FOR, for Latin-1 and Latin Extended-A, or nought. The
two-letter ones — ß, æ, œ, þ, ĳ — are handled before this is asked. */
static char plainLetter(unsigned cp) {
	// À to ÿ, one character each; a space where there is no letter.
	static const char* LATIN1 = "aaaaaa ceeeeiiiidnooooo ouuuuy  aaaaaa ceeeeiiiidnooooo ouuuuy y";
	if (cp >= 0xC0 && cp <= 0xFF)
		return LATIN1[cp - 0xC0] == ' ' ? 0 : LATIN1[cp - 0xC0];
	// Ā to ſ, in pairs of capital and small.
	static const char* EXT_A = "aaaaaaccccccccddddeeeeeeeeeegggggggghhhhiiiiiiiiii  jjkkklllllll"
		"lllnnnnnnnnnoooooo  rrrrrrssssssssttttttuuuuuuuuuuuuwwyyyzzzzzzs";
	if (cp >= 0x100 && cp <= 0x17F) {
		const char c = EXT_A[cp - 0x100];
		return c == ' ' ? 0 : c;
	}
	return 0;
}

/** LOWER CASE, PLAIN LETTERS, PUNCTUATION KEPT for the clause splitter: accents folded away, the
spaces typesetters letter-space a credit with taken out — the Entertainer file spells its composer
B Y S C O T T J O P L I N in hair spaces — and a space put between a word and a number run
together. Anything outside the Latin alphabets becomes a space, since no spelling in the table
could match it. */
static std::string fold(const std::string& in) {
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); ) {
		const unsigned cp = nextCodePoint(in, i);
		if (cp == 0x200A || cp == 0x2009 || cp == 0x2006 || cp == 0x2008 || cp == 0x202F
				|| cp == 0x200B || cp == 0xAD)
			continue;
		if (cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2013 || cp == 0x2014 || cp == 0x2212) {
			out += '-';
			continue;
		}
		if (cp < 0x80) {
			out += (char) ((cp >= 'A' && cp <= 'Z') ? cp + 32 : cp);
			continue;
		}
		if (cp == 0xDF) { out += "ss"; continue; }
		if (cp == 0xC6 || cp == 0xE6) { out += "ae"; continue; }
		if (cp == 0x152 || cp == 0x153) { out += "oe"; continue; }
		if (cp == 0xDE || cp == 0xFE) { out += "th"; continue; }
		if (cp == 0x132 || cp == 0x133) { out += "ij"; continue; }
		const char c = plainLetter(cp);
		out += c ? c : ' ';
	}
	// A letter and a digit run together are two words: "op11" is "op 11".
	std::string spaced;
	for (size_t i = 0; i < out.size(); i++) {
		if (i > 0) {
			const bool a = std::isalpha((unsigned char) out[i - 1]) != 0, b = std::isdigit((unsigned char) out[i]) != 0;
			const bool c = std::isdigit((unsigned char) out[i - 1]) != 0, d = std::isalpha((unsigned char) out[i]) != 0;
			if ((a && b) || (c && d))
				spaced += ' ';
		}
		spaced += out[i];
	}
	return spaced;
}

/** Folded, and then everything that is not a letter or a digit a space, the spaces one apiece. */
static std::string norm(const std::string& folded) {
	std::string out;
	bool space = true;
	for (char c : folded) {
		if (std::isalnum((unsigned char) c)) {
			out += c;
			space = false;
		}
		else if (!space) {
			out += ' ';
			space = true;
		}
	}
	while (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}

static std::vector<std::string> words(const std::string& s) {
	std::vector<std::string> w;
	size_t i = 0;
	while (i < s.size()) {
		while (i < s.size() && s[i] == ' ')
			i++;
		size_t j = i;
		while (j < s.size() && s[j] != ' ')
			j++;
		if (j > i)
			w.push_back(s.substr(i, j - i));
		i = j;
	}
	return w;
}

/** Whether `phrase` stands as whole words in `text`, both normalised. */
static size_t findPhrase(const std::string& text, const std::string& phrase, size_t from = 0) {
	for (size_t at = text.find(phrase, from); at != std::string::npos; at = text.find(phrase, at + 1)) {
		const bool before = at == 0 || text[at - 1] == ' ';
		const bool after = at + phrase.size() == text.size() || text[at + phrase.size()] == ' ';
		if (before && after)
			return at;
	}
	return std::string::npos;
}

static void blank(std::string& text, const std::string& phrase) {
	for (size_t at = findPhrase(text, phrase); at != std::string::npos; at = findPhrase(text, phrase, at))
		for (size_t k = at; k < at + phrase.size(); k++)
			text[k] = ' ';
}

/** Optimal string alignment distance, stopping early past `most`. */
static int distance(const std::string& a, const std::string& b, int most) {
	const int la = (int) a.size(), lb = (int) b.size();
	if (std::abs(la - lb) > most)
		return most + 1;
	std::vector<std::vector<int> > d((size_t) la + 1, std::vector<int>((size_t) lb + 1, 0));
	for (int i = 0; i <= la; i++) d[i][0] = i;
	for (int j = 0; j <= lb; j++) d[0][j] = j;
	for (int i = 1; i <= la; i++)
		for (int j = 1; j <= lb; j++) {
			const int c = (a[i - 1] == b[j - 1]) ? 0 : 1;
			d[i][j] = std::min(std::min(d[i - 1][j] + 1, d[i][j - 1] + 1), d[i - 1][j - 1] + c);
			if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
				d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
		}
	return d[la][lb];
}

/** "ffaure" and "faure", "hayden" and "haydn": a doubled letter written once. */
static std::string undouble(const std::string& w) {
	std::string out;
	for (char c : w)
		if (out.empty() || out.back() != c)
			out += c;
	return out;
}


// ---- what a score says about itself ---------------------------------------------------------

/** HOW FAR EACH KIND OF TEXT IS TRUSTED. Never one field alone: the local scores have an arranger
in the composer field, the word "Composer" as a placeholder, a template author's name, and on one
song the wrong composer entirely. */
static float weightOf(int kind) {
	switch (kind) {
		case CLUE_COMPOSER: return 3.f;
		case CLUE_CREATOR: case CLUE_CREDIT: case CLUE_TITLE: case CLUE_FILE: return 2.f;
		case CLUE_LYRICIST: case CLUE_RIGHTS: return 1.f;
		default: return 0.f;
	}
}

static const char* saidIn(int kind) {
	switch (kind) {
		case CLUE_COMPOSER: return "the composer field names";
		case CLUE_CREATOR: return "the creator field names";
		case CLUE_CREDIT: return "the credits name";
		case CLUE_TITLE: return "the title names";
		case CLUE_FILE: return "the file's name names";
		case CLUE_LYRICIST: return "the lyricist field names";
		case CLUE_RIGHTS: return "the copyright line names";
		default: return "the score names";
	}
}

/** WORKS KNOWN BY THEIR TITLES, with words that must also appear where the title alone could mean
something else: "tempest" is Beethoven's only beside a sonata or Op. 31. */
struct Title {
	const char* phrase;
	const char* composer;
	const char* confirm;   // bar-separated, or NULL
};

static const Title TITLES[] = {
	{"sonate au clair de lune", "Ludwig van Beethoven", NULL},
	{"moonlight", "Ludwig van Beethoven", "sonata|sonate|op 27|opus 27"},
	{"mondscheinsonate", "Ludwig van Beethoven", NULL},
	{"tempest", "Ludwig van Beethoven", "sonata|sonate|op 31|opus 31"},
	{"waldstein", "Ludwig van Beethoven", NULL},
	{"appassionata", "Ludwig van Beethoven", NULL},
	{"pathetique", "Ludwig van Beethoven", "sonata|sonate|op 13|opus 13"},
	{"fur elise", "Ludwig van Beethoven", NULL},
	{"fuer elise", "Ludwig van Beethoven", NULL},
	{"for elise", "Ludwig van Beethoven", NULL},
	{"pour elise", "Ludwig van Beethoven", NULL},
	{"well tempered", "Johann Sebastian Bach", NULL},
	{"wohltemperierte", "Johann Sebastian Bach", NULL},
	{"clavier bien tempere", "Johann Sebastian Bach", NULL},
	{"openwtc", "Johann Sebastian Bach", NULL},
	{"opengoldberg", "Johann Sebastian Bach", NULL},
	{"goldberg", "Johann Sebastian Bach", NULL},
	{"maple leaf rag", "Scott Joplin", NULL},
	{"the entertainer", "Scott Joplin", NULL},
	{"take five", "Paul Desmond", NULL},
	{"blue rondo a la turk", "Dave Brubeck", NULL},
	{"rondo alla turca", "Wolfgang Amadeus Mozart", NULL},
	{"autumn leaves", "Joseph Kosma", NULL},
	{"les feuilles mortes", "Joseph Kosma", NULL},
	{"misty", "Erroll Garner", NULL},
	{"fly me to the moon", "Bart Howard", NULL},
	{"basin street blues", "Spencer Williams", NULL},
	{"samba de orfeu", "Luiz Bonfá", NULL},
	{"gentle rain", "Luiz Bonfá", NULL},
	{"angel eyes", "Matt Dennis", NULL},
	{"stormy weather", "Harold Arlen", NULL},
	{"come rain or come shine", "Harold Arlen", NULL},
	{"i got rhythm", "George Gershwin", NULL},
	{"a foggy day", "George Gershwin", NULL},
	{"all that jazz", "John Kander", NULL},
	{"east of the sun", "Brooks Bowman", NULL},
	{"bluesette", "Toots Thielemans", NULL},
	{"summer samba", "Marcos Valle", NULL},
	{"samba de verao", "Marcos Valle", NULL},
	{"so nice", "Marcos Valle", NULL},
	{"i fall in love too easily", "Jule Styne", NULL},
	{"all i do is dream of you", "Nacio Herb Brown", NULL},
	{"layla", "Derek and the Dominos", NULL},
	{"all my loving", "The Beatles", NULL},
	{"the swan", "Camille Saint-Saëns", NULL},
	{"le cygne", "Camille Saint-Saëns", NULL},
	{"pavane pour une infante defunte", "Maurice Ravel", NULL},
	{"pavane", "Gabriel Fauré", "faure|op 50"},
	{"le tombeau de couperin", "Maurice Ravel", NULL},
	{"clair de lune", "Claude Debussy", NULL},
	{"fantaisie impromptu", "Frédéric Chopin", NULL},
	{"fantasie impromptu", "Frédéric Chopin", NULL},
};

/** CATALOGUE NUMBERS: the code and then a number. Not name spellings — a creator named Woo Jin is
not Beethoven. */
struct Catalogue {
	const char* code;
	const char* composer;
};

static const Catalogue CATALOGUES[] = {
	{"bwv", "Johann Sebastian Bach"},
	{"hwv", "George Frideric Handel"},
	{"kv", "Wolfgang Amadeus Mozart"},
	{"hob", "Joseph Haydn"},
	{"woo", "Ludwig van Beethoven"},
	{"rv", "Antonio Vivaldi"},
	{"twv", "Georg Philipp Telemann"},
	{"buxwv", "Dieterich Buxtehude"},
	{"wq", "Carl Philipp Emanuel Bach"},
};

/** WORDS THAT INTRODUCE SOMEBODY OTHER THAN THE COMPOSER. A clause from one of these onward is
dropped: "Trascrizione di Xaluc Vanistia", "English Lyrics by Norman Gimbel", "arr. Thomas L.S.
Pang". */
static const char* ROLE_WORDS[] = {"arr", "arranged", "arrangement", "arranger", "arrangiert",
	"bearbeitung", "bearb", "transcribed", "transcription", "transcr", "trascrizione", "trascr",
	"transcrit", "transcrite", "edited", "edition", "editor", "engraved", "typeset", "sequenced",
	"reformatted", "lyrics", "lyric", "words", "text", "texte", "paroles", "testo", "letra", "poem",
	"poet", "worte", "dedicated", "dedie", "fingering", "doigte", "performed", "recorded", "heard"};

/** AND PHRASES THAT INTRODUCE THE COMPOSER, which keep what follows them. */
static const char* COMPOSER_PHRASES[] = {"words and music by", "music and words by", "music by",
	"composed by", "komponiert von", "musique de", "musica di", "musica de"};

/** A NAME AFTER ONE OF THESE IS THE SUBJECT AND NOT THE COMPOSER: variations on a theme of Haydn,
hommage à Rameau. */
static const char* MASK_BEFORE[] = {"theme of", "theme by", "theme de", "thema von", "variations on",
	"hommage a", "homage to", "tombeau de", "after", "nach", "d apres", "a la maniere de",
	"in the style of", "style of", "in memory of", "to the memory of", "sur le nom d", "on the name of"};

static const char* PLACEHOLDERS[] = {"composer", "compositor", "komponist", "compositeur", "lyricist",
	"arranger", "title", "subtitle", "untitled", "traditional", "trad", "anonymous", "anon", "unknown",
	"public domain", "engraved lead sheet template stock"};


/** EVERY SPELLING IN THE TABLE, as words, with the composer it names; longest first. */
struct Spelling {
	std::vector<std::string> words;
	int composer;
};

static const std::vector<Spelling>& spellings() {
	static std::vector<Spelling> all;
	if (all.empty()) {
		for (int c = 0; c < COMPOSER_COUNT; c++) {
			const std::string v = COMPOSERS[c].variants;
			size_t at = 0;
			while (at <= v.size()) {
				const size_t bar = v.find('|', at);
				const std::string one = v.substr(at, (bar == std::string::npos) ? std::string::npos : bar - at);
				if (!one.empty())
					all.push_back({words(one), c});
				if (bar == std::string::npos)
					break;
				at = bar + 1;
			}
		}
		std::stable_sort(all.begin(), all.end(), [](const Spelling& a, const Spelling& b) {
			return a.words.size() > b.words.size();
		});
	}
	return all;
}

static int composerNamed(const char* name) {
	for (int c = 0; c < COMPOSER_COUNT; c++)
		if (std::strcmp(COMPOSERS[c].name, name) == 0)
			return c;
	return -1;
}


/** THE TALLY: points for each composer, and the strongest single piece of evidence for each, to
say why. */
struct Tally {
	std::map<int, float> points;
	std::map<int, std::pair<float, std::string> > best;

	void add(int composer, float amount, const std::string& because) {
		if (composer < 0 || amount <= 0.f)
			return;
		points[composer] += amount;
		std::pair<float, std::string>& b = best[composer];
		if (amount > b.first)
			b = std::make_pair(amount, because);
	}
};


/** NAMES IN ONE CLAUSE, longest spellings first, each word used once. A spelling may be off by a
letter or two where it is long enough to stand it — "Schoemberg", "FFaure" — but a one-word
misspelling counts only when it is the whole clause, so that ordinary words near a short name are
not taken for it. */
static void findNames(const std::string& clause, int kind, bool fuzzy, float weight, Tally& tally) {
	const std::vector<std::string> toks = words(clause);
	std::vector<bool> used(toks.size(), false);
	const std::vector<Spelling>& all = spellings();
	for (int len = 4; len >= 1; len--) {
		for (int k = 0; k + len <= (int) toks.size(); k++) {
			bool taken = false;
			for (int q = k; q < k + len; q++)
				taken = taken || used[(size_t) q];
			if (taken)
				continue;
			int hit = -1;
			float quality = 0.f;
			int bestDistance = 1000;
			for (const Spelling& s : all) {
				if ((int) s.words.size() != len)
					continue;
				bool exact = true, undoubled = true;
				for (int q = 0; q < len; q++) {
					exact = exact && s.words[(size_t) q] == toks[(size_t) (k + q)];
					undoubled = undoubled && undouble(s.words[(size_t) q]) == undouble(toks[(size_t) (k + q)]);
				}
				if (exact) { hit = s.composer; quality = 1.f; break; }
				if (undoubled && quality < 0.9f) { hit = s.composer; quality = 0.9f; continue; }
				if (!fuzzy || quality >= 0.9f)
					continue;
				int total = 0;
				bool ok = true;
				for (int q = 0; q < len && ok; q++) {
					const std::string& w = s.words[(size_t) q];
					const int allow = (w.size() >= 9) ? 2 : (w.size() >= 6) ? 1 : 0;
					const int d = distance(toks[(size_t) (k + q)], w, allow);
					if (d > allow)
						ok = false;
					total += d;
				}
				if (ok && total > 0 && total < bestDistance && !(len == 1 && toks.size() > 1)) {
					bestDistance = total;
					hit = s.composer;
					quality = (total == 1) ? 0.8f : 0.6f;
				}
			}
			if (hit < 0)
				continue;
			for (int q = k; q < k + len; q++)
				used[(size_t) q] = true;
			tally.add(hit, weight * quality, std::string(saidIn(kind)) + " " + COMPOSERS[hit].name);
		}
	}
}

/** A clause with the roles taken out: what is left after "arr.", up to "music by", and so on. */
static std::string withoutRoles(std::string n) {
	const std::vector<std::string> w = words(n);
	bool keeps = false;
	for (const char* p : COMPOSER_PHRASES)
		if (findPhrase(n, p) != std::string::npos)
			keeps = true;
	// A ROLE WORD ENDING THE CLAUSE drops the whole of it: "Thomas arranged".
	if (!w.empty() && !keeps)
		for (const char* r : ROLE_WORDS)
			if (w.back() == r)
				return "";
	for (const char* p : COMPOSER_PHRASES)
		blank(n, p);
	// FROM A ROLE WORD ONWARD, gone.
	const std::vector<std::string> left = words(n);
	std::string kept;
	for (size_t i = 0; i < left.size(); i++) {
		bool role = false;
		for (const char* r : ROLE_WORDS)
			if (left[i] == r)
				role = true;
		// "from" counts only at the start: "(From Kenny Dorham Sings)".
		if (i == 0 && left[i] == "from")
			role = true;
		if (role)
			break;
		kept += (kept.empty() ? "" : " ") + left[i];
	}
	// A NAME AFTER "THEME OF", "HOMMAGE A" and the like is masked with the next word or two.
	for (const char* m : MASK_BEFORE) {
		size_t at = findPhrase(kept, m);
		while (at != std::string::npos) {
			size_t end = at + std::strlen(m);
			for (int skip = 0; skip < 2 && end < kept.size(); skip++) {
				size_t next = kept.find(' ', end + 1);
				end = (next == std::string::npos) ? kept.size() : next;
			}
			for (size_t k = at; k < end; k++)
				kept[k] = ' ';
			at = findPhrase(kept, m);
		}
	}
	return norm(kept);
}

/** The clauses of one field: split at line breaks, semicolons, commas, slashes and brackets, at
" - ", and at "and", "et", "und" and "&" — after protecting the table's own spellings that contain
those words, so that "Derek and the Dominos" survives. */
static std::vector<std::string> clausesOf(const std::string& folded) {
	std::vector<std::string> out;
	std::string cur;
	for (size_t i = 0; i < folded.size(); i++) {
		const char c = folded[i];
		if (c == '\n' || c == ';' || c == ',' || c == '(' || c == ')' || c == '/' || c == '&') {
			out.push_back(cur);
			cur.clear();
			continue;
		}
		if (c == '-' && i > 0 && i + 1 < folded.size() && folded[i - 1] == ' ' && folded[i + 1] == ' ') {
			out.push_back(cur);
			cur.clear();
			continue;
		}
		cur += c;
	}
	out.push_back(cur);
	std::vector<std::string> split;
	for (const std::string& raw : out) {
		std::string n = norm(raw);
		// Protected: the joined spellings, and "words and music by".
		static const char* KEEP_JOINED[] = {"words and music by", "music and words by",
			"derek and the dominos", "simon and garfunkel", "gilbert and sullivan",
			"rodgers and hammerstein", "rodgers and hart", "lerner and loewe", "kander and ebb",
			"lennon and mccartney", "andersson and ulvaeus", "jagger and richards", "goffin and king"};
		for (const char* p : KEEP_JOINED) {
			std::string glued = p;
			std::replace(glued.begin(), glued.end(), ' ', '_');
			for (size_t at = findPhrase(n, p); at != std::string::npos; at = findPhrase(n, p))
				n.replace(at, std::strlen(p), glued);
		}
		std::string piece;
		for (const std::string& w : words(n)) {
			if (w == "and" || w == "et" || w == "und") {
				split.push_back(piece);
				piece.clear();
				continue;
			}
			std::string unglued = w;
			std::replace(unglued.begin(), unglued.end(), '_', ' ');
			piece += (piece.empty() ? "" : " ") + unglued;
		}
		split.push_back(piece);
	}
	return split;
}

/** The years in a text: life spans, as born and died, and lone years. */
static void yearsIn(const std::string& folded, std::vector<std::pair<int, int> >& spans,
		std::vector<int>& lone) {
	std::vector<std::pair<size_t, int> > found;
	for (size_t i = 0; i + 4 <= folded.size(); i++) {
		if (!std::isdigit((unsigned char) folded[i]) || (i > 0 && std::isalnum((unsigned char) folded[i - 1])))
			continue;
		size_t j = i;
		while (j < folded.size() && std::isdigit((unsigned char) folded[j]))
			j++;
		if (j - i == 4) {
			const int y = std::atoi(folded.substr(i, 4).c_str());
			if (y >= 1550 && y <= 2030)
				found.push_back(std::make_pair(i, y));
		}
		i = j;
	}
	std::vector<bool> paired(found.size(), false);
	for (size_t k = 0; k + 1 < found.size(); k++) {
		const std::string between = folded.substr(found[k].first + 4, found[k + 1].first - found[k].first - 4);
		const std::string mid = norm(between);
		const bool dash = between.find('-') != std::string::npos || mid == "to";
		const int span = found[k + 1].second - found[k].second;
		if (dash && span >= 15 && span <= 105 && (mid.empty() || mid == "to")) {
			spans.push_back(std::make_pair(found[k].second, found[k + 1].second));
			paired[k] = paired[k + 1] = true;
		}
	}
	for (size_t k = 0; k < found.size(); k++)
		if (!paired[k])
			lone.push_back(found[k].second);
}

static int styleOfYear(int year) {
	if (year < 1750) return STYLE_BAROQUE;
	if (year < 1800) return STYLE_CLASSICAL;
	if (year < 1840) return STYLE_EARLY_ROMANTIC;
	if (year < 1900) return STYLE_LATE_ROMANTIC;
	return STYLE_MODERN;
}


StyleGuess guessStyle(const std::vector<StyleClue>& clues, int chordSymbols) {
	StyleGuess g;
	Tally tally;

	std::vector<std::string> folded(clues.size()), normal(clues.size());
	std::string all;
	for (size_t i = 0; i < clues.size(); i++) {
		folded[i] = fold(clues[i].text);
		normal[i] = norm(folded[i]);
		all += normal[i] + " ";
	}
	all = norm(all);

	// TITLES FIRST, longest first, on everything at once — and then taken out, so that a name
	// inside a title is not read as the composer: Le Tombeau de Couperin is Ravel's.
	std::vector<const Title*> titles;
	for (const Title& t : TITLES)
		titles.push_back(&t);
	std::stable_sort(titles.begin(), titles.end(), [](const Title* a, const Title* b) {
		return std::strlen(a->phrase) > std::strlen(b->phrase);
	});
	for (const Title* t : titles) {
		if (findPhrase(all, t->phrase) == std::string::npos)
			continue;
		bool confirmed = t->confirm == NULL;
		if (!confirmed) {
			const std::string c = t->confirm;
			size_t at = 0;
			while (!confirmed && at <= c.size()) {
				const size_t bar = c.find('|', at);
				const std::string one = c.substr(at, (bar == std::string::npos) ? std::string::npos : bar - at);
				confirmed = findPhrase(all, one) != std::string::npos;
				if (bar == std::string::npos)
					break;
				at = bar + 1;
			}
		}
		if (!confirmed)
			continue;
		const int c = composerNamed(t->composer);
		tally.add(c, 1.5f, std::string("the title is ") + t->phrase + ", by " + (c >= 0 ? COMPOSERS[c].name : ""));
		blank(all, t->phrase);
	}
	for (std::string& n : normal)
		for (const Title& t : TITLES)
			blank(n, t.phrase);

	// CATALOGUE NUMBERS, a code and then a number. K is Mozart's unless it is Scarlatti's.
	{
		const std::vector<std::string> w = words(all);
		const bool scarlatti = findPhrase(all, "scarlatti") != std::string::npos
			|| findPhrase(all, "kk") != std::string::npos || findPhrase(all, "longo") != std::string::npos;
		for (size_t i = 0; i + 1 < w.size(); i++) {
			const bool number = std::isdigit((unsigned char) w[i + 1][0]) != 0
				|| (w[i] == "hob" && w[i + 1].find_first_not_of("ivxlc") == std::string::npos);
			if (!number)
				continue;
			const char* who = NULL;
			for (const Catalogue& cat : CATALOGUES)
				if (w[i] == cat.code)
					who = cat.composer;
			if (w[i] == "k")
				who = scarlatti ? "Domenico Scarlatti" : "Wolfgang Amadeus Mozart";
			if (who) {
				const int c = composerNamed(who);
				tally.add(c, 1.5f, std::string("its catalogue number is ") + w[i] + " " + w[i + 1]
					+ ", " + (c >= 0 ? COMPOSERS[c].name : "") + "'s");
			}
		}
	}

	// THE NAMES, clause by clause, each field as far as it is trusted.
	for (size_t i = 0; i < clues.size(); i++) {
		const float weight = weightOf(clues[i].kind);
		if (weight <= 0.f)
			continue;
		const bool fuzzy = clues[i].kind == CLUE_COMPOSER || clues[i].kind == CLUE_CREATOR
			|| clues[i].kind == CLUE_FILE;
		std::string f = folded[i];
		for (const std::string& clause : clausesOf(f)) {
			std::string n = norm(clause);
			bool placeholder = n.empty();
			for (const char* p : PLACEHOLDERS)
				placeholder = placeholder || n == p;
			if (placeholder)
				continue;
			n = withoutRoles(n);
			for (const Title& t : TITLES)
				blank(n, t.phrase);
			n = norm(n);
			if (!n.empty())
				findNames(n, clues[i].kind, fuzzy, weight, tally);
		}
	}

	// A LIFE SPAN THAT IS SOMEBODY'S IN THE TABLE: "(1770 - 1827)".
	std::vector<std::pair<int, int> > spans;
	std::vector<int> lone;
	for (const std::string& f : folded)
		yearsIn(f, spans, lone);
	for (const std::pair<int, int>& s : spans)
		for (int c = 0; c < COMPOSER_COUNT; c++)
			if (COMPOSERS[c].born == s.first && COMPOSERS[c].died == s.second)
				tally.add(c, 2.f, "the dates " + std::to_string(s.first) + " to " + std::to_string(s.second)
					+ " are " + COMPOSERS[c].name + "'s");

	if (!tally.points.empty()) {
		int top = -1;
		float most = 0.f;
		for (const std::pair<const int, float>& p : tally.points)
			if (p.second > most) {
				most = p.second;
				top = p.first;
			}
		g.style = COMPOSERS[top].style;
		g.found = true;
		g.composer = COMPOSERS[top].name;
		g.because = tally.best[top].second;
		return g;
	}

	// NOBODY NAMED: a life span still dates the music, then a year, then chord symbols.
	if (!spans.empty()) {
		g.style = styleOfYear(spans[0].first + 30);
		g.found = true;
		g.because = "a composer's life, " + std::to_string(spans[0].first) + " to "
			+ std::to_string(spans[0].second) + ", is given";
		return g;
	}
	if (chordSymbols > 0) {
		g.style = STYLE_JAZZ;
		g.found = true;
		g.because = "it is a lead sheet, with chord symbols and no composer the table knows";
		return g;
	}
	for (int y : lone) {
		if (y > 1600 && y < 1980) {
			g.style = styleOfYear(y);
			g.found = true;
			g.because = "it is dated " + std::to_string(y);
			return g;
		}
	}
	g.because = "nothing in the file says who wrote it";
	return g;
}


} // namespace px
