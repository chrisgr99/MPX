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
	/** The bar being played, as the file numbers it, counting from nought. Minus one for a module
	that is not playing anything. */
	std::atomic<int> bar{-1};
	/** How far through that bar, nought to one, for a line that moves rather than a bar that
	lights up. */
	std::atomic<float> through{0.f};
	std::atomic<bool> running{false};

	/** THE BARS TO GO ROUND, written from the window and read by the module. Both minus one for no
	loop. Inclusive: a loop of one bar has the same number at each end. */
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
};


/** Called by a module every sample, which is why it does as little as it does. */
void scoreFollowPublish(int64_t moduleId, int bar, float through, bool running);

/** Where that module has got to. False when nothing has published for it. */
bool scoreFollowRead(int64_t moduleId, int& bar, float& through, bool& running);

/** Called by the window. `from` and `to` are written bars counting from nought, or both minus one
to stop looping. `seek` asks for the music to jump to the start of the loop. */
void scoreFollowSetLoop(int64_t moduleId, int from, int to, bool seek);

/** Read by the module. Returns false where no loop is set. `takeSeek` is true once per request. */
bool scoreFollowLoop(int64_t moduleId, int& from, int& to, bool* takeSeek);

/** Asks for the music to be moved to the start of a written bar. */
void scoreFollowGoTo(int64_t moduleId, int bar);

/** Taken by the module: the bar asked for, or minus one. Answers once per request. */
int scoreFollowTakeGoTo(int64_t moduleId);

/** Forgets a module, so its slot is free for the next one. */
void scoreFollowDrop(int64_t moduleId);


} // namespace px
