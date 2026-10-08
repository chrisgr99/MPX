#pragma once
/** WHERE A FILE CHOOSER SHOULD OPEN.

RACK OPENS IN THE FOLDER THE PATCH WAS LAST SAVED TO, which is right for a patch and wrong for
everything else. Saving a patch therefore moves the starting folder of every chooser in the rack
— so a score fetched from the same folder a dozen times is still a walk from wherever the patch
happens to live, and the walk gets longer the better organised the patches are.

ONE FOLDER PER KIND OF FILE, not one for everything. Scores, chord charts and pictures are kept
in different places by anybody who keeps them anywhere, and a single remembered folder would have
the three of them fighting over it. The kind is a short word chosen by the caller.

KEPT WHERE THE PICTURE LIST IS KEPT, in the plugin's own folder, so it outlives the patch and is
there for a module placed fresh. mpxSprites has done this for pictures since it was written — see
pictureFolder — and this is the same thing for everything else.

A MODULE THAT ALREADY HOLDS A FILE SHOULD PREFER ITS OWN. The folder that file came from is a
better answer than the last one used anywhere, and it needs no remembering: see lastFolderFor. */
#include <string>
#include <vector>

namespace px {


/** The folder a chooser of this kind should open in, or empty for wherever it would have gone. */
std::string lastFolder(const std::string& kind);

/** Notes the folder a file of this kind has just been taken from, and puts the file at the top
of the recent list for that kind. Give it the file's path. */
void rememberFolder(const std::string& kind, const std::string& path);

/** HOW MANY FILES OF ONE KIND ARE REMEMBERED. Twenty is what the picture list keeps, and it is
about as many as anybody will read down a menu. */
static const int LAST_FILES = 20;

/** The files of this kind most recently loaded, newest first. Paths, which may since have been
moved or deleted — a menu showing them should say so rather than pretending. */
const std::vector<std::string>& recentFiles(const std::string& kind);

/** Takes a file out of the list, for one that has gone. */
void forgetFile(const std::string& kind, const std::string& path);

/** Empties the list for this kind. */
void forgetAll(const std::string& kind);

/** THE ANSWER A MODULE SHOULD ACTUALLY USE: the folder of the file it already holds, where it
holds one, and otherwise the last folder used for this kind. `path` is what the module loaded, or
empty. Returns empty when there is nothing to go on, which leaves the chooser where it would have
opened anyway. */
std::string lastFolderFor(const std::string& kind, const std::string& path);


} // namespace px
