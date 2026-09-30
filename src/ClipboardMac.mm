/** A PICTURE OFF THE CLIPBOARD, on macOS.

Copying a picture in a web browser and pasting it here is the shortest way from seeing something to
hearing it, and it needs no windows arranged side by side. Dragging needs the source and the
destination both on the screen at once; copying needs neither.

WHAT A COPIED PICTURE ACTUALLY IS varies by where it came from. A browser may put PNG data on the
pasteboard, or TIFF, or only the address it was fetched from, or a reference to a file. All of them
are asked for here, in the order of most useful first, and whatever is found is handed back AS PNG:
the decoder this plugin carries reads PNG and does not read TIFF, so the conversion is done while
the system's own image code is still in reach rather than by adding a second decoder.

THE ADDRESS IS RETURNED SEPARATELY and not fetched. Downloading belongs to the part of the plugin
that knows about the network, and a function called from a menu should not be waiting on one.
*/
// COCOA FIRST, BEFORE ANY RACK HEADER. MacTypes.h declares a Rect of its own, and Rack declares
// one in its maths; whichever is second, the name is then ambiguous inside this file. Cocoa has to
// win, because it is the one the system's own headers go on to use.
#import <Cocoa/Cocoa.h>

#include "Clipboard.hpp"

namespace px {


/** Re-encodes whatever the system made of the data as PNG. Empty if it was not an image. */
static bool asPng(NSData* data, std::vector<uint8_t>& out) {
	if (!data)
		return false;
	NSBitmapImageRep* rep = [NSBitmapImageRep imageRepWithData:data];
	if (!rep)
		return false;
	NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
	if (!png || [png length] == 0)
		return false;
	const uint8_t* bytes = (const uint8_t*) [png bytes];
	out.assign(bytes, bytes + [png length]);
	return true;
}


bool clipboardPicture(std::vector<uint8_t>& out, std::string& name, std::string& url) {
	out.clear();
	name.clear();
	url.clear();
	@autoreleasepool {
		NSPasteboard* board = [NSPasteboard generalPasteboard];
		if (!board)
			return false;

		// A FILE ON THE PASTEBOARD FIRST, because it carries a name as well as a picture, and a
		// name is what the list of recent pictures is read by.
		NSArray* urls = [board readObjectsForClasses:@[[NSURL class]] options:nil];
		for (NSURL* one in urls) {
			if (![one isFileURL])
				continue;
			NSData* data = [NSData dataWithContentsOfURL:one];
			if (asPng(data, out)) {
				name = [[one lastPathComponent] UTF8String];
				return true;
			}
		}

		// THEN THE PICTURE ITSELF. PNG before TIFF only because it needs no conversion; either
		// works.
		NSData* data = [board dataForType:NSPasteboardTypePNG];
		if (!data)
			data = [board dataForType:NSPasteboardTypeTIFF];
		if (asPng(data, out)) {
			name = "pasted.png";
			return true;
		}

		// FAILING BOTH, WHATEVER ADDRESS IT CAME FROM. Some browsers put only this on the
		// pasteboard for a picture in a page. It is handed back for the caller to fetch.
		for (NSURL* one in urls) {
			if ([one isFileURL])
				continue;
			url = [[one absoluteString] UTF8String];
			if (!url.empty()) {
				name = [[one lastPathComponent] UTF8String];
				return false;
			}
		}
		NSString* text = [board stringForType:NSPasteboardTypeString];
		if (text) {
			const std::string maybe = [text UTF8String];
			if (maybe.compare(0, 7, "http://") == 0 || maybe.compare(0, 8, "https://") == 0) {
				url = maybe;
				const size_t slash = url.find_last_of('/');
				name = (slash == std::string::npos) ? "pasted" : url.substr(slash + 1);
			}
		}
	}
	return false;
}


} // namespace px
