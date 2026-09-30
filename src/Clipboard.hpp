#pragma once
/** THE CLIPBOARD, asked for a picture.

One question, answered by the system on macOS and by a stub everywhere else, so nothing above this
has to know which platform it is on.

NO RACK HEADERS HERE, deliberately. The macOS answer is Objective-C++ and has to include Cocoa,
which brings in the system's OpenGL headers; Rack brings in its own, and the two cannot both be
first. Keeping this header to the standard library means the file that talks to Cocoa never sees
Rack at all.
*/
#include <cstdint>
#include <string>
#include <vector>

namespace px {


/** A picture from the clipboard, as PNG bytes in `out`, with whatever it was called in `name`.

Returns true when there is a picture. When it returns false and `url` is not empty, the clipboard
held the address of a picture rather than the picture itself — which is what some browsers put
there — and the caller may fetch it. */
bool clipboardPicture(std::vector<uint8_t>& out, std::string& name, std::string& url);


} // namespace px
