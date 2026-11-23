#pragma once

#include <filesystem>
#include <string>

// Path rules shared by the Blender launch and the add-on copy, so both judge the user's folders the same way.
namespace plynth::blenderpaths {

// The value of an environment variable as text. Empty when it is not set.
std::wstring environmentText(const wchar_t* name);

// Plynth's private Blender folder: PLYNTH_BLENDER_PRIVATE, else LOCALAPPDATA/Plynth/blender-private, else one in
// the temporary folder. Only works out the path, it creates nothing.
std::filesystem::path privateFolder();

// The path with "/" and "\" alike, dots and doubled separators worked out and every separator at the end dropped,
// except a root's own. Nothing else of the text is changed.
std::filesystem::path normalFolder(const std::filesystem::path& path);

// Says whether two paths are the same text after normalFolder, case ignored.
bool sameText(const std::filesystem::path& a, const std::filesystem::path& b);

// The path with links and junctions resolved for the part that exists and the rest added as given. Opens only
// to read attributes, never to read or list. Empty when the path is empty or not absolute.
std::filesystem::path resolvedPath(const std::filesystem::path& path);

// Says whether one path is the other, lies in it or holds it. Compared as text, as text with dots and spaces cut
// from the end of each name, and with links resolved, case ignored, so any one match is enough.
bool overlaps(const std::filesystem::path& a, const std::filesystem::path& b);

// Says whether a name in the path, as given or resolved, is "Blender Foundation" in any case, with dots and
// spaces at the end of the name ignored.
bool namesBlenderFoundation(const std::filesystem::path& path);

// Says whether the path or a folder above it is a link, a junction or another reparse point, by its attributes
// alone. Parts that do not exist are not judged. True when a part that exists cannot be checked.
bool hasLinkOnPath(const std::filesystem::path& path);

// The attributes of a file or folder, without following a link at its end. INVALID_FILE_ATTRIBUTES (all bits
// set) when it is not there or cannot be read.
unsigned long attributesOf(const std::filesystem::path& path);

// The path in a form the file calls accept beyond 260 characters.
std::wstring longPath(const std::filesystem::path& path);

}
