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
#include "Layout.hpp"

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


/** The side of a remembered picture's thumbnail. Large enough to fill a cell of the chooser's
grid and be recognised as a photograph rather than as a colour. */
static const int THUMB_SIDE = 128;

/** Reduces the square to a thumbnail, by averaging as the squaring does. */
void pictureThumbnail(const std::vector<uint8_t>& rgba, std::vector<uint8_t>& out);


/** THE PICTURES THAT HAVE BEEN LOADED, AND WHERE THEY CAME FROM.

Kept for the whole plugin rather than per module and written to the user folder, so that the folder
you keep pictures in is offered again next time — including next time Rack is started, and
including a module you have only just made. A file chooser that opens somewhere you were not using
is a small thing that has to be corrected every single time.

WHAT IS REMEMBERED IS THE PATH AND A THUMBNAIL, not the picture. The picture itself may be several
hundred kilobytes and is already saved in whatever patch used it; the thumbnail is what makes a
list of twenty worth having, since a row of file names is not how anybody recognises a photograph.
A picture whose file has since moved or gone is still listed, and says so when it cannot be read. */
struct PictureMemory {
	std::string path;
	std::string name;
	/** THE THUMBNAIL'S OWN FILE, in a folder beside the list. Not in the list itself: twenty
	thumbnails this size are more than a megabyte, and a megabyte rewritten every time a picture is
	loaded is a poor way to keep a list of twenty names. */
	std::string thumbFile;
};

/** Reads one thumbnail. False if its file has gone, which costs one cell of the grid. */
bool pictureThumb(const PictureMemory& memory, std::vector<uint8_t>& out);

/** The folder the chooser should open in, or empty for wherever it would have gone. */
std::string pictureFolder();
/** The most recent first, at most twenty. */
const std::vector<PictureMemory>& pictureHistory();
/** Notes a picture that has just been loaded, and writes the list out. */
void pictureRemember(const std::string& path, const std::string& name,
	const std::vector<uint8_t>& rgba);
/** How many the chooser shows, and so how many are kept: five across by four down. */
static const int PICTURE_KEPT = 20;

/** KEEPS A COPY OF A PICTURE'S BYTES where the plugin can find them again, and returns where it
put it. For a picture that arrived by being dropped on the module: it may have come from a browser
and have no file of its own, or from a folder that will be tidied up tomorrow, and a remembered
picture whose file has gone is a blank cell in the chooser. The copies are shared by every module
here that uses pictures. Empty if it could not be written. */
std::string pictureKeepCopy(const std::string& name, const std::vector<uint8_t>& bytes);
/** Reads the list back. Called once, on the first use. */
void pictureMemoryLoad();
/** Empties the list. */
void pictureMemoryClear();


/** Base sixty-four, since a patch is JSON and a picture is bytes. */
std::string bytesToText(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> textToBytes(const std::string& text);


} // namespace px
