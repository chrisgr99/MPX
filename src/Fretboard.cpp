#include "Fretboard.hpp"
#include <cstdlib>

namespace px {


int Shape::bassString(const Tuning& t) const {
	for (int s = t.strings - 1; s >= 0; s--)
		if (sounds(s))
			return s;
	return -1;
}


std::string Shape::written(const Tuning& t) const {
	std::string text;
	for (int s = t.strings - 1; s >= 0; s--) {
		if (!sounds(s))
			text += 'x';
		else if (fret[s] < 10)
			text += char('0' + fret[s]);
		else
			text += "(" + std::to_string(fret[s]) + ")";
	}
	return text;
}


Shape Shape::parse(const std::string& text, const Tuning& t) {
	Shape shape;
	int s = t.strings - 1;
	for (size_t i = 0; i < text.size() && s >= 0; i++, s--) {
		char c = text[i];
		if (c == '(') {
			size_t close = text.find(')', i);
			shape.fret[s] = std::atoi(text.substr(i + 1, close - i - 1).c_str());
			i = close;
		}
		else if (c >= '0' && c <= '9')
			shape.fret[s] = c - '0';
	}
	return shape;
}


/** Whether one finger can lie flat at fret `f` across every string stopped there. */
static bool barreHolds(const Shape& shape, const Tuning& t, const Hand& hand, int f) {
	int first = -1, last = -1;
	for (int u = 0; u < t.strings; u++)
		if (shape.fret[u] == f) {
			if (first < 0)
				first = u;
			last = u;
		}
	if (first < 0)
		return false;
	for (int u = first; u <= last; u++) {
		if (shape.fret[u] == Shape::MUTED && hand.barreOverMuted)
			continue;
		if (shape.fret[u] < f)
			return false;
	}
	return true;
}


/** THE FINGERS A SHAPE TAKES. Without a barre, one for each stopped string. With one, a finger
lies flat at some fret across every string from the first stopped there to the last, and those
strings take one finger between them; every string under it must be stopped at that fret or above,
since the finger stops them all, and a string it lies across cannot ring open or be left out. The
fewest over every fret a barre could lie at. */
static int fingersFor(const Shape& shape, const Tuning& t, const Hand& hand, bool* barre) {
	int stopped = 0;
	for (int s = 0; s < t.strings; s++)
		if (shape.fret[s] > 0)
			stopped++;
	int best = stopped;
	*barre = false;
	for (int s = 0; s < t.strings; s++) {
		int f = shape.fret[s];
		if (f <= 0)
			continue;
		// Each fret once: from the first string stopped at it.
		bool seen = false;
		for (int u = 0; u < s; u++)
			if (shape.fret[u] == f)
				seen = true;
		if (seen)
			continue;
		int at = 0;
		for (int u = s; u < t.strings; u++)
			if (shape.fret[u] == f)
				at++;
		if (at < 2 || !barreHolds(shape, t, hand, f))
			continue;
		int fingers = stopped - at + 1;
		if (fingers < best) {
			best = fingers;
			*barre = true;
		}
	}
	return best;
}


Grip gripOf(const Shape& shape, const Tuning& t, const Hand& hand) {
	Grip g;
	int low = 1000, high = 0;
	for (int s = 0; s < t.strings; s++) {
		int f = shape.fret[s];
		if (f == Shape::MUTED)
			continue;
		g.sounding++;
		if (f == 0) {
			g.open++;
			continue;
		}
		if (f < low)
			low = f;
		if (f > high)
			high = f;
	}
	if (high == 0)
		low = 0;
	g.lowFret = low;
	g.highFret = high;
	g.fingers = fingersFor(shape, t, hand, &g.barre);
	if (high > 0) {
		int first = -1, last = -1, at = 0;
		for (int s = 0; s < t.strings; s++)
			if (shape.fret[s] == low) {
				if (first < 0)
					first = s;
				last = s;
				at++;
			}
		// Only with something stopped higher: two fingers side by side at the only fret in use
		// is an ordinary grip, the index free to take either.
		g.splitIndex = high > low && last - first + 1 > at && !barreHolds(shape, t, hand, low);
	}
	g.playable = high - low + 1 <= hand.stretch && g.fingers <= hand.fingers
		&& high <= hand.highestFret;
	return g;
}


/** Each string in turn, from the top: muted, open, or a fret from `low` to `high`. */
static void searchFrom(int s, Shape& shape, int low, int high, const Tuning& t, const Hand& hand,
	const std::function<bool(int, int)>& allowed,
	const std::function<void(const Shape&, const Grip&)>& visit) {
	if (s == t.strings) {
		// The lowest fret must be stopped somewhere, so each shape is offered at one low fret.
		if (low > 0) {
			bool atLow = false;
			for (int u = 0; u < t.strings; u++)
				if (shape.fret[u] == low)
					atLow = true;
			if (!atLow)
				return;
		}
		Grip g = gripOf(shape, t, hand);
		if (g.playable)
			visit(shape, g);
		return;
	}
	if (allowed(s, Shape::MUTED)) {
		shape.fret[s] = Shape::MUTED;
		searchFrom(s + 1, shape, low, high, t, hand, allowed, visit);
	}
	if (allowed(s, 0)) {
		shape.fret[s] = 0;
		searchFrom(s + 1, shape, low, high, t, hand, allowed, visit);
	}
	for (int f = low; f >= 1 && f <= high; f++) {
		if (!allowed(s, f))
			continue;
		shape.fret[s] = f;
		searchFrom(s + 1, shape, low, high, t, hand, allowed, visit);
	}
	shape.fret[s] = Shape::MUTED;
}


void searchShapes(const Tuning& t, const Hand& hand,
	const std::function<bool(int, int)>& allowed,
	const std::function<void(const Shape&, const Grip&)>& visit) {
	Shape shape;
	// Nothing stopped: open and muted strings only.
	searchFrom(0, shape, 0, 0, t, hand, allowed, visit);
	for (int low = 1; low <= hand.highestFret; low++) {
		int high = low + hand.stretch - 1;
		if (high > hand.highestFret)
			high = hand.highestFret;
		searchFrom(0, shape, low, high, t, hand, allowed, visit);
	}
}


bool ChordNotes::contains(int pitchClass) const {
	for (int i = 0; i < count; i++)
		if (pc[i] == pitchClass)
			return true;
	return false;
}


bool ChordNotes::isEssential(int pitchClass) const {
	for (int i = 0; i < count; i++)
		if (pc[i] == pitchClass && essential[i])
			return true;
	return false;
}


static int pitchClassOf(int note) {
	return ((note % 12) + 12) % 12;
}


/** HOW GOOD A FULL SHAPE IS, higher better. The weights were set against the shapes in
fretboardtest.

- Open strings, in the open position: they ring, and the familiar shapes are built on them. Once
  a finger reaches the fourth fret the hand has left that position, and an open string among its
  notes is a different technique; there it counts against.
- More strings, for a fuller strum.
- Fewer fingers, and no second finger crowded in at the index's fret.
- No full four-fret stretch.
- The position: the lowest stopped fret near `nearFret`, the open position when that is nought. */
static float fullShapeScore(const Grip& g, int nearFret) {
	float score = (g.highFret <= 3 ? 3.f : -2.f) * g.open + 2.f * g.sounding - 2.f * g.fingers;
	if (g.splitIndex)
		score -= 6.f;
	if (g.highFret - g.lowFret >= 3)
		score -= 3.f;
	score -= 4.f * std::abs(g.lowFret - nearFret);
	return score;
}


bool bestFullShape(const ChordNotes& chord, const Tuning& t, const Hand& hand, Shape* out,
	int nearFret) {
	bool found = false;
	float best = 0.f;
	searchShapes(t, hand,
		[&](int s, int f) {
			return f == Shape::MUTED || chord.contains(pitchClassOf(t.open[s] + f));
		},
		[&](const Shape& shape, const Grip& g) {
			if (g.sounding < 4)
				return;
			// The sounding strings in one run from the top, nothing muted among them.
			int bass = shape.bassString(t);
			for (int s = 0; s <= bass; s++)
				if (!shape.sounds(s))
					return;
			if (pitchClassOf(shape.note(t, bass)) != chord.bass)
				return;
			for (int i = 0; i < chord.count; i++) {
				if (!chord.essential[i])
					continue;
				bool present = false;
				for (int s = 0; s <= bass; s++)
					if (pitchClassOf(shape.note(t, s)) == chord.pc[i])
						present = true;
				if (!present)
					return;
			}
			float score = fullShapeScore(g, nearFret);
			if (!found || score > best) {
				found = true;
				best = score;
				*out = shape;
			}
		});
	return found;
}


} // namespace px
