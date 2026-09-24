#pragma once
/** A PICTURE, DECODED AND SQUARED OFF.

Any image file is decoded and resampled to a fixed square, so everything that reads it — the
force field, the sampling, the drawing — works in one coordinate system and never depends on what
shape the photograph was.

THE FILE'S OWN BYTES ARE WHAT IS KEPT. A patch saves the picture rather than a path to it, so a
patch that is sent somewhere else looks and sounds as it did; and it saves the FILE rather than
the square it was turned into, because a photograph is a few hundred kilobytes as a file and three
megabytes as pixels.
*/
#include "plugin.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace px {


/** The side of the square every picture becomes. A thousand and twenty-four is fine enough that
a sprite crossing it reads a different pixel every few frames, and small enough to hold. */
static const int PICTURE_SIDE = 1024;


struct Picture {
	/** The file as it was read, which is what a patch saves. Empty when there is none. */
	std::vector<uint8_t> file;
	/** The square, four bytes a pixel, red green blue and alpha. Empty when there is none. */
	std::vector<uint8_t> rgba;
	/** What it was called, for the panel to say. */
	std::string name;
	/** Moved whenever the picture changes, so a widget can tell that its copy is stale without
	comparing a megabyte. */
	uint32_t generation = 0;

	bool has() const { return !rgba.empty(); }

	/** Reads a file and squares it off. False if it could not be decoded, in which case what was
	there before is left alone. */
	bool load(const std::string& path);
	/** The same from bytes already in hand, which is how a patch brings one back. */
	bool decode(const std::vector<uint8_t>& bytes, const std::string& name);
	void clear();

	/** The pixel at a place on the square, nought to one in each direction. Wraps rather than
	clamping, so a reading at the very edge is defined. */
	void at(float x, float y, uint8_t* out) const;
};


/** GLARE REDUCTION, the same as the accessibility setting in GXW.

A photograph on a screen in a dark room is a lamp, and a large pale region of one is painful to sit
in front of. This takes the brightness out of the large pale regions and leaves everything else
alone.

HOW. The luminance is blurred with an edge-aware filter — the domain transform of Gastál and
Oliveira, which smooths within a region but not across a boundary — and that blurred reading is
what decides how much each pixel is dimmed. Above a threshold the dimming ramps smoothly in, down
to `maxAtten` at full brightness. The multiplier comes from the neighbourhood but is applied to the
pixel, so a small bright thing in a dark surround keeps its brightness and only broad expanses of
white are brought down. Local contrast survives, which a plain dimming does not.

`blurRadius` is how large a region counts as a region, `threshold` is the brightness it starts at,
and `maxAtten` is what a fully bright region is multiplied by — one leaves everything alone.

THE DIMMED COPY IS FOR LOOKING AT AND NOTHING ELSE. Whatever reads the picture for its own purposes
reads the original, as it does in GXW: this changes how a picture looks, never what it means. */
void dimGlare(const std::vector<uint8_t>& rgba, std::vector<uint8_t>& out,
	float blurRadius, float threshold, float maxAtten);


/** Base sixty-four, since a patch is JSON and a picture is bytes. */
std::string bytesToText(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> textToBytes(const std::string& text);


} // namespace px
