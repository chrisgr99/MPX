/** The fretboard library against the chord shapes every guitarist knows. See docs/finger-picker.md.

	make fretboardtest
*/
#include "../src/Fretboard.hpp"
#include <cstdio>
#include <string>
#include <vector>

using namespace px;

static int failures = 0;

static void check(bool ok, const std::string& what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok)
		failures++;
}

static int letterPc(const std::string& name, size_t* used) {
	static const int natural[7] = {9, 11, 0, 2, 4, 5, 7};
	int pc = natural[name[0] - 'A'];
	*used = 1;
	if (name.size() > 1 && name[1] == '#') {
		pc++;
		*used = 2;
	}
	else if (name.size() > 1 && name[1] == 'b') {
		pc--;
		*used = 2;
	}
	return (pc + 12) % 12;
}

/** A chord from its symbol: the root, then m, 7, m7 or maj7. The root, the third and any seventh
are essential; the fifth is not. */
static ChordNotes chordFrom(const std::string& name) {
	size_t used;
	int root = letterPc(name, &used);
	std::string q = name.substr(used);
	int third = 4, seventh = -1;
	if (q == "m")
		third = 3;
	else if (q == "7")
		seventh = 10;
	else if (q == "m7") {
		third = 3;
		seventh = 10;
	}
	else if (q == "maj7")
		seventh = 11;
	ChordNotes c;
	auto add = [&](int interval, bool essential) {
		c.pc[c.count] = (root + interval) % 12;
		c.essential[c.count] = essential;
		c.count++;
	};
	add(0, true);
	add(third, true);
	add(7, false);
	if (seventh >= 0)
		add(seventh, true);
	c.bass = root;
	return c;
}

struct Known {
	const char* name;
	/** The familiar shapes, lowest string first; any of them passes. */
	std::vector<std::string> shapes;
};

int main() {
	Tuning t = Tuning::standard();
	Hand hand;

	std::printf("Notes\n");
	Shape low = Shape::parse("x32010", t);
	check(low.note(t, 4) == 48 && low.note(t, 0) == 64 && low.note(t, 5) == -1,
		"x32010 is C3 on the A string, E4 open on top, the low E muted");
	check(low.written(t) == "x32010", "written back the same");

	std::printf("\nThe hand\n");
	Grip f = gripOf(Shape::parse("133211", t), t, hand);
	check(f.playable && f.barre && f.fingers == 4, "the F barre is four fingers, one of them a barre");
	Grip wide = gripOf(Shape::parse("x3x0x6", t), t, hand);
	check(wide.playable, "frets 3 and 6 are a stretch of four, within reach");
	Grip far = gripOf(Shape::parse("1xxxx5", t), t, hand);
	check(!far.playable, "frets 1 and 5 together are beyond the stretch");
	Grip five = gripOf(Shape::parse("x21342", t), t, hand);
	check(!five.playable, "five stopped strings with no barre are too many fingers");
	Grip openUnder = gripOf(Shape::parse("10x021", t), t, hand);
	check(openUnder.fingers == 3 && openUnder.splitIndex,
		"a string ringing open under the first fret means no barre there");

	std::printf("\nThe search\n");
	int count = 0;
	bool unplayable = false;
	searchShapes(t, hand, [](int, int) { return true; }, [&](const Shape& s, const Grip& g) {
		count++;
		if (!gripOf(s, t, hand).playable)
			unplayable = true;
	});
	std::printf("      %d playable shapes in all\n", count);
	check(!unplayable && count > 10000, "every shape offered is playable");

	std::printf("\nFull chords\n");
	std::vector<Known> known = {
		{"C", {"x32010"}},
		{"G", {"320003", "320033"}},
		{"D", {"xx0232"}},
		{"A", {"x02220"}},
		{"E", {"022100"}},
		{"Am", {"x02210"}},
		{"Em", {"022000"}},
		{"Dm", {"xx0231"}},
		{"F", {"133211"}},
		{"B", {"x24442"}},
		{"Bm", {"x24432"}},
		{"A7", {"x02020", "x02223"}},
		{"B7", {"x21202"}},
		{"C7", {"x32310"}},
		{"D7", {"xx0212"}},
		{"E7", {"020100", "022130"}},
		{"G7", {"320001"}},
		{"Am7", {"x02010"}},
		{"Dm7", {"xx0211"}},
		{"Em7", {"020000", "022030"}},
		{"Cmaj7", {"x32000"}},
		{"Dmaj7", {"xx0222"}},
		{"Fmaj7", {"xx3210", "132210"}},
	};
	for (const Known& k : known) {
		Shape s;
		bool found = bestFullShape(chordFrom(k.name), t, hand, &s);
		std::string got = found ? s.written(t) : "none";
		bool ok = false;
		for (const std::string& want : k.shapes)
			if (got == want)
				ok = true;
		check(ok, std::string(k.name) + " " + got + (ok ? "" : ", wanted " + k.shapes[0]));
	}

	std::printf("\nUp the neck\n");
	Shape s;
	bestFullShape(chordFrom("A"), t, hand, &s, 5);
	check(s.written(t) == "577655", "A near the fifth fret is the E-shape barre: " + s.written(t));
	bestFullShape(chordFrom("C"), t, hand, &s, 8);
	check(s.written(t) == "8(10)(10)988", "C near the eighth fret is the E-shape barre: " + s.written(t));

	std::printf("\n%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
