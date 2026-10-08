#include "LastFolder.hpp"
#include "Layout.hpp"

#include <rack.hpp>

#include <map>

using namespace rack;

namespace px {


/** READ ONCE AND KEPT, because a chooser opening is not a moment to be reading a file — and
because every module in the rack shares the one answer, so reading it per module would be
reading it a dozen times for the same string. */
static std::map<std::string, std::string> gFolders;
static bool gRead = false;


static std::string folderPath() {
	return asset::user(layoutFolder + "/folders.json");
}


static void load() {
	if (gRead)
		return;
	gRead = true;
	FILE* file = std::fopen(folderPath().c_str(), "r");
	if (!file)
		return;
	json_error_t error;
	json_t* rootJ = json_loadf(file, 0, &error);
	std::fclose(file);
	if (!rootJ)
		return;
	const char* key;
	json_t* valueJ;
	json_object_foreach(rootJ, key, valueJ) {
		if (const char* v = json_string_value(valueJ))
			gFolders[key] = v;
	}
	json_decref(rootJ);
}


static void save() {
	json_t* rootJ = json_object();
	for (std::map<std::string, std::string>::const_iterator i = gFolders.begin();
			i != gFolders.end(); ++i)
		json_object_set_new(rootJ, i->first.c_str(), json_string(i->second.c_str()));
	system::createDirectories(asset::user(layoutFolder));
	FILE* file = std::fopen(folderPath().c_str(), "w");
	if (file) {
		json_dumpf(rootJ, file, JSON_INDENT(2));
		std::fclose(file);
	}
	json_decref(rootJ);
}


std::string lastFolder(const std::string& kind) {
	load();
	std::map<std::string, std::string>::const_iterator i = gFolders.find(kind);
	return (i == gFolders.end()) ? std::string() : i->second;
}


void rememberFolder(const std::string& kind, const std::string& path) {
	if (path.empty())
		return;
	const std::string folder = system::getDirectory(path);
	if (folder.empty())
		return;
	load();
	if (gFolders[kind] == folder)
		return;                      // Nothing to write: the same folder as last time.
	gFolders[kind] = folder;
	save();
}


std::string lastFolderFor(const std::string& kind, const std::string& path) {
	// THE FILE THIS MODULE ALREADY HOLDS IS THE BEST ANSWER. Somebody fetching a second score
	// into a module that has one is almost always fetching it from beside the first, and that
	// needs no remembering — the module knows where its own file came from.
	if (!path.empty()) {
		const std::string folder = system::getDirectory(path);
		if (!folder.empty() && system::isDirectory(folder))
			return folder;
	}
	const std::string kept = lastFolder(kind);
	return (!kept.empty() && system::isDirectory(kept)) ? kept : std::string();
}


} // namespace px
