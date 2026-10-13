#pragma once
/** THE TABLE OF STARTS: by situation, the start that rehearsal found to come out well most often.
See docs/bowed-strings.md, phase 8, and test/starttable.py, which writes it from the player's log of
every decision it rehearsed.

A PLAYER DOES NOT REHEARSE EACH NOTE. A string player has learnt which stroke works where — from
silence, onto a string still ringing, at a bow change — and plays it. This is that learning, taken
from thousands of rehearsed decisions: for each situation, the one start that came out well most
often across all the times it was met, rather than the one that happened to suit one occasion. The
player plays from it where it cannot rehearse (BowedPlayer, "quick"), or everywhere when it is told
not to rehearse at all.

A SITUATION is the kind of start — from the string, onto a new one, or a bow change — the string,
the note in steps of three semitones above the open string, the marking, and for a start from the
string how strongly the string still rings. The file has one line a situation; the instrument is
named on each, with its mode ("violin", "violin-economy"), and only the instrument's own are kept.

No Rack in here. */
#include <cstdint>
#include <string>
#include <unordered_map>

namespace px {


struct BowedTable {
	enum Kind : uint8_t { FRESH, CROSS, CHANGE };
	enum Ringing : uint8_t { SILENT, FAINT, RINGING, NOT_ASKED };

	/** A START: a bite — its place as a share of the planned one, its force over the note's, and its
	acceleration as twelve times the logarithm of its share of what Guettler's limits allow; a glide —
	its length in seconds and the share of the force it starts from; or a bow change — the press and
	how long the bow takes to turn, seconds. */
	struct Start {
		char kind = 0;     /**< 'b' a bite, 'g' a glide, 'c' a change */
		float a = 0.f, b = 0.f, c = 0.f;
	};

	/** The lines of `path` for `instrument`. False where the file cannot be read or has none. */
	bool load(const std::string& path, const std::string& instrument);
	const Start* find(Kind kind, int string, int semitone, int level, Ringing ringing) const;
	bool empty() const { return cells.empty(); }
	size_t size() const { return cells.size(); }

	/** HOW STRONGLY A STRING RINGS, from the mean square of its push on the bridge, newtons squared:
	the classes test/starttable.py counts in. */
	static Ringing ringingOf(double meanSquare);

private:
	std::unordered_map<uint32_t, Start> cells;
	static uint32_t key(int kind, int string, int step, int level, int ringing) {
		return (uint32_t) (((((kind & 3) * 8 + (string & 7)) * 32 + (step & 31)) * 16 + (level & 15)) * 4 + (ringing & 3));
	}
};


} // namespace px
