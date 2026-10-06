#pragma once
/** THE KEYS: turning a cable's notes into a pianist's hands. Part of phase 7, brought forward.

NO RACK IN HERE, which is the point of it being its own thing. This is the bookkeeping between a
note bus and an instrument — which handle is on which key, how hard, and when the key comes up
again — and it is exactly the part that was wrong in a way no measurement of the instrument could
have caught. The strings, the hammers, the dampers and the board were all right; the module holding
them simply never let a key up. So it lives here, where test/pianorender.cpp can play it.

WHAT IT HAS TO GET RIGHT, and each of these is a thing that went wrong or nearly did:

	A NOTE MAY NAME ITS OWN LENGTH, and many do. The cable's protocol lets a source say how long a
	note is at the moment it starts it, instead of sending a note-off later; a source reading from
	a score knows the length and uses it, and mpxScore and everything else built on the chart send
	a duration and nothing afterwards. A receiver that waits for a note-off from one of those
	waits for ever, and every key it is given stays down for the rest of the session — which on
	this instrument means every damper it ever lifted stays lifted, and the whole piece piles into
	one chord.

	BEING TOLD TAKES PRECEDENCE OVER BEING TIMED. A source that sends both a duration and a
	note-off means the note-off: it is the later information.

	TWO NOTES CAN LAND ON ONE KEY — a doubling, or an arpeggio overtaking itself — and letting the
	first of them up must not damp a string the second is still holding down.

	AND A LEVEL BECOMES A SPEED EXPONENTIALLY. The useful range of a hammer is about a fifth of a
	metre per second, which is as quietly as a note can be made to speak, up to five or six, which
	is as hard as anyone can hit it. That is a factor of thirty, and the ear hears it as an even
	spread of loudness because loudness is heard in ratios; spread proportionally, half the
	dynamic range would crowd into the top quarter of the controller. */
#include "PianoBoard.hpp"

#include <cmath>

namespace px {


struct PianoKeys {
	/** More than any ten fingers and both pedals can account for. */
	static const int MAX_KEYS = 128;

	/** A level, nought to one, as a hammer speed in metres per second. */
	static float speedOf(float level) {
		const float v = (level < 0.f) ? 0.f : ((level > 1.f) ? 1.f : level);
		return 0.2f * std::pow(30.f, v);
	}

	/** Where on the keyboard a pitch in volts falls. A piano has keys and not a continuum, so a
	pitch between two of them is played on the nearer. */
	static int keyOf(float pitchV) {
		return (int) std::lround(60.f + 12.f * pitchV);
	}

	void setSampleRate(float hz) {
		rate = (hz > 0.f) ? (double) hz : 48000.0;
	}

	/** A key down. `duration` in seconds, or nought for "somebody will say when". Returns false
	when there was no room — which is a key that will stay down, and worth complaining about. */
	bool down(PianoBoard& board, int64_t handle, float pitchV, float level, float duration) {
		const int midi = keyOf(pitchV);
		if (!board.inCompass(midi))
			return true;
		for (int i = 0; i < MAX_KEYS; i++) {
			if (held[i].used)
				continue;
			held[i].used = true;
			held[i].handle = handle;
			held[i].midi = midi;
			held[i].endsAt = (duration > 0.f) ? now + (int64_t) ((double) duration * rate) : 0;
			board.hit(midi, speedOf(level));
			return true;
		}
		board.hit(midi, speedOf(level));
		return false;
	}

	/** A key up, named by its handle. Takes precedence over whatever duration it was given. */
	void up(PianoBoard& board, int64_t handle) {
		for (int i = 0; i < MAX_KEYS; i++) {
			if (held[i].used && held[i].handle == handle) {
				lift(board, i);
				return;
			}
		}
	}

	/** ONE SAMPLE ON, and the keys whose time is up come up. Every sample and not every buffer: a
	damper landing a buffer late is audibly late at the top of the compass, where a note is over
	in a tenth of a second. */
	void step(PianoBoard& board) {
		now++;
		for (int i = 0; i < MAX_KEYS; i++) {
			if (held[i].used && held[i].endsAt != 0 && now >= held[i].endsAt)
				lift(board, i);
		}
	}

	/** EVERY KEY UP, for a cable pulled out. A note ends when its note-off arrives and a cable
	that has gone never sends one, so whatever was sounding would ring on with its key down.
	Unplugging a piano is not a way to hold a chord. */
	void allUp(PianoBoard& board) {
		for (int i = 0; i < MAX_KEYS; i++) {
			if (held[i].used)
				board.release(held[i].midi);
			held[i] = Key();
		}
	}

	int holding() const {
		int n = 0;
		for (int i = 0; i < MAX_KEYS; i++)
			n += held[i].used ? 1 : 0;
		return n;
	}

private:
	struct Key {
		int64_t handle = 0;
		int64_t endsAt = 0;
		int midi = 0;
		bool used = false;
	};

	Key held[MAX_KEYS];
	double rate = 48000.0;
	int64_t now = 0;

	void lift(PianoBoard& board, int which) {
		const int midi = held[which].midi;
		held[which].used = false;
		for (int i = 0; i < MAX_KEYS; i++) {
			if (held[i].used && held[i].midi == midi)
				return;
		}
		board.release(midi);
	}
};


} // namespace px
