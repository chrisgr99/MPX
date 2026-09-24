#pragma once
/** WHAT A MODULE PLAYED, WRITTEN DOWN, so that what it decided can be read afterwards rather than
guessed at from the sound.

One line per note, in the order the notes were sent, with the beat and the bar they fell on and
the chord that was in force. That is enough to say whether a note belongs to its chord, whether a
voice crossed another, whether a leap was needed, and where the rhythm actually fell.

NOTHING TOUCHES A FILE FROM THE AUDIO THREAD. Writing to a disk can take any amount of time, and a
sample that waits on a disk is a click. The engine copies a record into a fixed ring; the panel,
on the main thread, empties the ring to the file at its own pace. A ring that fills drops records
and counts them, and the count is written into the log, so a gap in it is visible.

mpxMelody and mpxPhrase have logs of their own, older and richer: they record the reasoning behind
each note as well as the note. This is the plain version, for the modules where what is wanted is
what came out. */
#include "plugin.hpp"
#include "Chord.hpp"

#include <atomic>
#include <cstdio>
#include <string>

namespace px {


/** ONE LINE OF THE LOG: either a note the module played, or the chord it was handed to play it
from. Both are wanted — a note that sits wrong may be a bad choice from a good chord or a good
choice from a bad one, and the log has to say which. Pitch is in volts, as everything on the cable
is. */
struct LogNote {
	enum Kind { NOTE, VOICING, SETTINGS };
	Kind kind = NOTE;
	/** For a VOICING line: where the voices were placed. For a SETTINGS line: the value of every
	control, in the order the module declares them. */
	float pitches[8] = {0.f};
	int count = 0;
	/** For a SETTINGS line: what each control was set to when the take began, and again whenever
	any of them moved. Without it a log says what was played and not what it was played under,
	which is half an answer to every question worth asking of it. */
	float values[24] = {0.f};
	int valueCount = 0;
	double seconds = 0.0;
	double beat = 0.0;
	int bar = 0;
	float beatInBar = 0.f;
	/** Which voice of the part played it, or nought where the module has one line. */
	int voice = 0;
	float pitch = 0.f;
	float level = 0.f;
	float duration = 0.f;
	/** The chord in force when it was sent, and the key it is a degree of. */
	bool haveChord = false;
	Key key;
	Chord chord;
};


struct NoteLog {
	static const uint32_t SIZE = 512;
	LogNote ring[SIZE];
	std::atomic<uint32_t> head{0}, tail{0};
	std::atomic<uint32_t> dropped{0};

	/** Audio thread. */
	void push(const LogNote& n) {
		const uint32_t h = head.load(std::memory_order_relaxed);
		const uint32_t next = (h + 1) % SIZE;
		if (next == tail.load(std::memory_order_relaxed)) {
			dropped++;
			return;
		}
		ring[h] = n;
		head.store(next, std::memory_order_release);
	}

	/** Main thread. False when there is nothing waiting. */
	bool pop(LogNote& out) {
		const uint32_t t = tail.load(std::memory_order_relaxed);
		if (t == head.load(std::memory_order_acquire))
			return false;
		out = ring[t];
		tail.store((t + 1) % SIZE, std::memory_order_release);
		return true;
	}
};


/** THE PANEL'S HALF OF IT: opens the file, empties the ring into it and closes it again. One per
module widget. `name` is what the file is called before the date — "comp", "arp". */
/** WATCHES A MODULE'S CONTROLS and pushes a settings line when a take starts and whenever any of
them moves. Kept here rather than in each module, since every module wants exactly this. */
struct SettingsWatcher {
	float was[24] = {0.f};
	int count = 0;
	bool started = false;

	void reset() { started = false; }

	/** Audio thread. `values` is the module's parameters, `n` how many of them. */
	void step(NoteLog& log, const float* values, int n, double seconds) {
		n = (n > 24) ? 24 : n;
		bool moved = !started || (n != count);
		for (int i = 0; !moved && i < n; i++)
			moved = (values[i] != was[i]);
		if (!moved)
			return;
		started = true;
		count = n;
		for (int i = 0; i < n; i++)
			was[i] = values[i];
		LogNote rec;
		rec.kind = LogNote::SETTINGS;
		rec.seconds = seconds;
		rec.valueCount = n;
		for (int i = 0; i < n; i++)
			rec.values[i] = values[i];
		log.push(rec);
	}
};


struct NoteLogWriter {
	std::FILE* file = NULL;
	std::string path;
	uint32_t droppedSeen = 0;

	~NoteLogWriter() { close(); }

	/** Starts a take. The file is named for the module and the moment, so one take never writes
	over another. */
	void open(const std::string& name);
	void close();
	bool writing() const { return file != NULL; }
	/** Writes whatever is waiting. Called every frame while a take is running. `names` is one
	control name per value, in the module's own order, so the file reads as words rather than as
	a row of numbers. */
	void drain(NoteLog& log, const char* const* names = NULL, int nameCount = 0);
	/** Where the last take went, or the folder they go in when there has been none. */
	std::string where() const;
};


} // namespace px
