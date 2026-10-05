#pragma once
/** WHAT THE TWO PATH MODULES SHARE.

mpxPathSprites turns what a sprite is passing over into a voltage. mpxPathSpritePlayer turns it
into a note. They are different instruments and deliberately separate modules — one trying to do
both would be too complex — but the picture, the eight paths, the tabs and the whole business of
drawing and dragging a path are the same thing twice, and the same thing twice is one thing.

So that part is here, as a base a module inherits, and the panel pieces that drive it take one of
these rather than either module. Nothing in here knows about voltages or notes.
*/
#include "Paths.hpp"
#include "Picture.hpp"

#include <atomic>

namespace px {


/** EIGHT, matching mpxPattern's eight tabs and the chord voicing's eight voices, so that every
part of a rhythm has somewhere to go. */
static const int MAX_PATHS = 8;

/** THE COLOURS THE PATHS ARE DRAWN IN. Eight that hold their own over a photograph of anything,
and that are told apart by someone who does not see red and green as different: they differ in
lightness as well as in hue, so they are still eight things and not four. */
static const NVGcolor PATH_INK[MAX_PATHS] = {
	nvgRGB(0xff, 0xd8, 0x3a), nvgRGB(0x3a, 0xc8, 0xff), nvgRGB(0xff, 0x7a, 0x3a),
	nvgRGB(0xa8, 0xff, 0x6a), nvgRGB(0xff, 0x6a, 0xc8), nvgRGB(0x6a, 0xe8, 0xd0),
	nvgRGB(0xd8, 0xb0, 0xff), nvgRGB(0xff, 0xff, 0xff),
};


/** A PICTURE, EIGHT PATHS OVER IT, AND WHICH OF THEM THE CONTROLS BELONG TO.

Inherited by both path modules. The settings each module keeps per tab are its own business and
live in its own array; what is here is only what the picture and the tabs need to work. */
struct PathHost {
	virtual ~PathHost() {}

	/** THE PICTURE LIVES IN THE MODULE: it is what is saved with the patch, it is read on the
	audio thread, and a module outlives the widget that shows it. */
	Picture picture;
	Path path[MAX_PATHS];

	std::atomic<int> tab{0};
	std::atomic<int> selection{1};
	/** A tab asked to be switched on or off, acted on by the engine so it happens in order. */
	std::atomic<int> flipEnable{-1};

	/** Where each head is, as a fraction along its path, for the panel to draw. */
	double at[MAX_PATHS] = {};

	virtual bool pathOn(int i) const = 0;

	int frontTab() const {
		const int i = tab.load();
		return (i < 0) ? 0 : (i >= MAX_PATHS) ? MAX_PATHS - 1 : i;
	}
	int chosenMask() const { return selection.load() | (1 << frontTab()); }
};


} // namespace px
