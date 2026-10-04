#pragma once
/** A WINDOW ON AN ENGRAVED SCORE.

The same furniture as the chart window — a title strip you drag it by, a cross at each end, every
edge resizes, Escape closes it — around a page drawn by Score and ScoreDraw.

ONE SCORE AT A TIME. The window is a child of the scene, so it is always over the rack and there
is nothing to be gained by having two.

FIT TO THE WINDOW'S WIDTH, and the notation's size set separately. The page is always drawn as
wide as the window, so the margins read as margins; how large the notation is on that page is the
one thing worth a control, because it is also how many bars go on a line. Larger notation, fewer
bars, more pages. See docs/notation.md.
*/
#include <cstdint>
#include <string>

namespace px {

struct Score;


/** Opens the window on a MusicXML file, or closes it if it is already showing that file. The
engraving happens on a thread of its own; the window says so while it runs. */
void scoreWindowShow(const std::string& path, const std::string& title, int64_t moduleId);

/** Closes the window if it belongs to this module. Called when the module goes. */
void scoreWindowCloseFor(int64_t moduleId);


} // namespace px
