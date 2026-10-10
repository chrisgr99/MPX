#pragma once
/** WHERE THE MUSIC HAS GOT TO, AND WHICH BARS TO GO ROUND.

A score window shows a piece and a module plays it, and each needs one small thing from the other:
the window needs to know which bar is sounding so it can show it, and the module needs to know
which bars the window has asked to loop. Neither should include the other's header to get it — the
window would be pulling in three modules' worth of module, and the module would be pulling in
Verovio — so the two meet here instead, in the same way everything else in this plugin that has
two ends meets: a small piece of shared state with atomics in it.

WRITTEN BARS, NOT PLAYED ONES. The module counts bars as they are played, so a repeated section
comes round again as a different bar; the page has each bar drawn once. So what passes through here
is the bar as the file numbers it, which is the only thing both ends can mean.

THE PLAYHEAD IS WRITTEN FROM THE AUDIO THREAD and read where the drawing happens, which is why
each field is an atomic of its own and nothing here is a structure that has to agree with itself.
A reader that catches the bar from one frame and the position from the next is out by a fraction of
a beat, which nobody can see. */
#include <atomic>
#include <cstdint>

namespace px {


/** HOW MANY SCORES CAN BE FOLLOWED AT ONCE. One window is open at a time today, but the modules
publish whether a window is up or not, so there is a slot for each that might. */
static const int MAX_FOLLOWED = 16;


struct ScoreFollow {
	std::atomic<int64_t> moduleId{-1};
	/** The bar being played, as the page numbers it. Minus one for a module that is not playing
	anything. */
	std::atomic<int> bar{-1};
	/** How far through that bar, nought to one, for a line that moves rather than a bar that
	lights up. */
	std::atomic<float> through{0.f};
	std::atomic<bool> running{false};

	/** THE BARS TO GO ROUND, as the page numbers them. Set from the window and kept by the module
	after the window closes; the module lets them go when it loads a different file, when its own
	file is edited and loses those bars, when it is initialised and when it is deleted. A loop made
	in a window on some other file ends with that window. Both minus one for no loop. Inclusive: a
	loop of one bar has the same number at each end. */
	std::atomic<int> loopFrom{-1};
	std::atomic<int> loopTo{-1};
	/** Raised by the window when it wants the music moved to the start of the loop, and lowered by
	the module when it has done it. A request rather than a position, since the module is the one
	that knows where a written bar falls on its own timeline. */
	std::atomic<bool> wantSeek{false};

	/** A BAR TO GO TO, written from the window and taken by the module, which then puts it back to
	minus one. Clicking a bar is how a passage is found, and finding one is not the same as
	looping it: the two were the same gesture once and a reader who only wanted to hear bar forty
	was left with bar forty repeating. */
	std::atomic<int> goTo{-1};

	/** START OR STOP, ASKED FOR BY THE WINDOW and taken by the module, which puts it back to
	nought. Reading a score is exactly when you want to start it and stop it, and the module may
	be anywhere on the rack — possibly behind the window. One for play, two for stop, three to
	take it back to the top; nought for nothing asked.

	A REQUEST AND NOT A STATE, like the seek above it. The transport is the module's — its button
	is the one that holds the setting, and two places each holding their own idea of whether the
	music is running is a pair that can disagree. */
	std::atomic<int> wantTransport{0};

	/** THE INSTRUMENT THE WINDOW SHOWS for the module's own score: the part of a MusicXML file or
	the track of a Guitar Pro song, counting from nought; minus one for none chosen. Set from the
	window when one is picked, read back when the window opens again, and saved by the module with
	the patch. Let go with the loop when the module loads a different file. */
	std::atomic<int> part{-1};
};


/** Called by a module every sample, which is why it does as little as it does. */
void scoreFollowPublish(int64_t moduleId, int bar, float through, bool running);

/** Where that module has got to. False when nothing has published for it. */
bool scoreFollowRead(int64_t moduleId, int& bar, float& through, bool& running);

/** `from` and `to` are bars as the page numbers them, or both minus one to stop looping. `seek`
asks for the music to jump to the start of the loop. */
void scoreFollowSetLoop(int64_t moduleId, int from, int to, bool seek);

/** Read by the module. Returns false where no loop is set. `takeSeek` is true once per request. */
bool scoreFollowLoop(int64_t moduleId, int& from, int& to, bool* takeSeek);

/** Asks for the music to be moved to the start of a written bar. */
void scoreFollowGoTo(int64_t moduleId, int bar);

/** Asked by the window: 1 play, 2 stop, 3 back to the top. */
void scoreFollowTransport(int64_t moduleId, int what);

/** Taken by the module: what was asked, or nought. Answers once per request. */
int scoreFollowTakeTransport(int64_t moduleId);

/** Taken by the module: the bar asked for, or minus one. Answers once per request. */
int scoreFollowTakeGoTo(int64_t moduleId);

/** The instrument the window last showed for this module's score, or minus one. See
ScoreFollow::part. */
void scoreFollowSetPart(int64_t moduleId, int part);
int scoreFollowPart(int64_t moduleId);

/** Forgets a module, so its slot is free for the next one. */
void scoreFollowDrop(int64_t moduleId);


} // namespace px
