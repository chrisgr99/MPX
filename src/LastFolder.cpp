#include "LastFolder.hpp"
#include "Layout.hpp"

#include <rack.hpp>

#include <map>
#include <set>
#include <vector>

using namespace rack;

namespace px {


/** READ ONCE AND KEPT, because a chooser opening is not a moment to be reading a file — and
because every module in the rack shares the one answer, so reading it per module would be
reading it a dozen times for the same string. */
static std::map<std::string, std::string> gFolders;
static std::map<std::string, std::vector<std::string> > gRecent;
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
		// A STRING IS THE OLD SHAPE, from before the recent list: just the folder.
		if (const char* v = json_string_value(valueJ)) {
			gFolders[key] = v;
			continue;
		}
		if (!json_is_object(valueJ))
			continue;
		if (const char* v = json_string_value(json_object_get(valueJ, "folder")))
			gFolders[key] = v;
		json_t* listJ = json_object_get(valueJ, "recent");
		if (json_is_array(listJ)) {
			size_t i;
			json_t* oneJ;
			json_array_foreach(listJ, i, oneJ) {
				const char* v = json_string_value(oneJ);
				if (v && (int) gRecent[key].size() < LAST_FILES)
					gRecent[key].push_back(v);
			}
		}
	}
	json_decref(rootJ);
}


static void save() {
	json_t* rootJ = json_object();
	std::set<std::string> kinds;
	for (std::map<std::string, std::string>::const_iterator i = gFolders.begin();
			i != gFolders.end(); ++i)
		kinds.insert(i->first);
	for (std::map<std::string, std::vector<std::string> >::const_iterator i = gRecent.begin();
			i != gRecent.end(); ++i)
		kinds.insert(i->first);
	for (std::set<std::string>::const_iterator k = kinds.begin(); k != kinds.end(); ++k) {
		json_t* oneJ = json_object();
		std::map<std::string, std::string>::const_iterator f = gFolders.find(*k);
		if (f != gFolders.end())
			json_object_set_new(oneJ, "folder", json_string(f->second.c_str()));
		json_t* listJ = json_array();
		std::map<std::string, std::vector<std::string> >::const_iterator r = gRecent.find(*k);
		if (r != gRecent.end())
			for (size_t i = 0; i < r->second.size(); i++)
				json_array_append_new(listJ, json_string(r->second[i].c_str()));
		json_object_set_new(oneJ, "recent", listJ);
		json_object_set_new(rootJ, k->c_str(), oneJ);
	}
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
	// THE SAME FILE TWICE IS ONE ENTRY, moved to the front: a list that fills up with four
	// copies of the piece being worked on is a list of one piece.
	std::vector<std::string>& recent = gRecent[kind];
	for (size_t i = 0; i < recent.size(); i++) {
		if (recent[i] == path) {
			recent.erase(recent.begin() + i);
			break;
		}
	}
	recent.insert(recent.begin(), path);
	while ((int) recent.size() > LAST_FILES)
		recent.pop_back();
	gFolders[kind] = folder;
	save();
}


const std::vector<std::string>& recentFiles(const std::string& kind) {
	load();
	return gRecent[kind];
}


void forgetFile(const std::string& kind, const std::string& path) {
	load();
	std::vector<std::string>& recent = gRecent[kind];
	for (size_t i = 0; i < recent.size(); i++) {
		if (recent[i] == path) {
			recent.erase(recent.begin() + i);
			save();
			return;
		}
	}
}


void forgetAll(const std::string& kind) {
	load();
	gRecent[kind].clear();
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
