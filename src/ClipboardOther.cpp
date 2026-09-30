/** See Clipboard.hpp. Everywhere that is not macOS, where reading a picture off the clipboard
would need that platform's own window system and no such module has been asked for yet. */
#include "Clipboard.hpp"
#include "plugin.hpp"

#if !defined ARCH_MAC

namespace px {


bool clipboardPicture(std::vector<uint8_t>& out, std::string& name, std::string& url) {
	out.clear();
	name.clear();
	url.clear();
	return false;
}


} // namespace px

#endif
