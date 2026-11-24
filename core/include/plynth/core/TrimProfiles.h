#pragma once

#include "Geometry.h"
#include "Status.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace plynth {

// Trim profiles are registry entries: the cross section a skirting board, cornice, casing, sill or railing is
// swept along. A built-in profile is one file in core/src/trim/profiles/ with a TrimProfileRegistrar at
// namespace scope, so adding one never means editing existing code. A custom profile is a file in
// Library/shapes/ (see readTrimProfileFile), registered at run time with registerTrimProfileFolder.
//
// An outline is in the profile's own box: x out from the wall from 0 to depth, y up from 0 to height (for a
// casing, y runs across the wall face away from the opening). It is a simple closed polygon, the first point
// not repeated, anticlockwise with y up, inside the box and touching all four sides of it.

inline constexpr std::string_view kSquareProfileId = "square";
inline constexpr std::string_view kBevelProfileId = "bevel";
inline constexpr std::string_view kRoundedProfileId = "rounded";
inline constexpr std::string_view kClassicProfileId = "classic";
inline constexpr std::string_view kCoveProfileId = "cove";
inline constexpr std::string_view kRoundProfileId = "round";

// The sort key every custom profile gets, after the built-in ones.
inline constexpr int kCustomProfileSortKey = 1000;

// The built-in profiles and their sort keys:
// - square (10): the box with a 3 mm chamfer on its outer top corner (smaller when the box is smaller).
// - bevel (20): the outer face slopes back from 0.3 of the height to half the depth at the top.
// - rounded (30): the outer top corner rounded with a radius of half the smaller side.
// - classic (40): a skirting board: a flat face up to 0.65 of the height, a quarter round, then a thinner top.
// - cove (50): a cornice: a concave quarter ellipse from the wall at the bottom to the ceiling at the front.
// - round (60): the ellipse touching the four sides of the box, for railings.
class TrimProfile {
public:
    virtual ~TrimProfile() = default;

    // Stable id stored in project.json, for example "classic". Lower case letters, digits and "_".
    virtual std::string_view id() const = 0;
    // Short name for the app, for example "Classic".
    virtual std::string_view name() const = 0;
    // Profiles are listed by this key, then by id. Built-in profiles use 10, 20, 30 and so on.
    virtual int sortKey() const = 0;
    // True for profiles compiled into Plynth, false for profiles read from a file.
    virtual bool builtIn() const = 0;
    // The outline at this size, as described above. Height and depth are finite and above zero.
    virtual std::vector<Vec2> outline(double height, double depth) const = 0;
};

// Adds a profile to the registry. Returns false and drops it when its id is already taken or is not a well
// formed lower case name.
bool registerTrimProfile(std::unique_ptr<TrimProfile> profile);

// Registers a profile while the program starts. A profile file holds one at namespace scope, for example
// const TrimProfileRegistrar registrar(std::make_unique<CoveProfile>());
struct TrimProfileRegistrar {
    explicit TrimProfileRegistrar(std::unique_ptr<TrimProfile> profile);
};

// The profile with this id. Null when there is none.
const TrimProfile* findTrimProfile(std::string_view id);

// Every registered profile, sorted by sortKey and then id.
std::vector<const TrimProfile*> allTrimProfiles();

// A custom profile file, Library/shapes/<id>.profile.json, in the format of schema/trim-profile.schema.json:
// {"format": "plynth-trim-profile", "version": 1, "id": "ogee", "name": "Ogee", "outline": [[0, 0], [1, 0], ...]}
// The outline is in a unit box: x from 0 (at the wall) to 1 (out), y from 0 (bottom) to 1 (top). It is
// stretched to the trim's depth and height. The opening shape loader reads only *.shape.json, so both kinds
// of file share the folder.
inline constexpr std::string_view kTrimProfileFormatName = "plynth-trim-profile";
inline constexpr int kTrimProfileFormatVersion = 1;
inline constexpr std::string_view kTrimProfileFileSuffix = ".profile.json";

// What reading a profile file gave. Status has no comparison, so neither has this.
struct TrimProfileFile {
    Status status;
    std::string id;
    std::string name;
    std::vector<Vec2> unitOutline; // made anticlockwise with y up
};

// Reads and checks a profile file, with the rules of readShapeFile (OpeningShapes.h) for its own format name.
TrimProfileFile readTrimProfileFile(const std::filesystem::path& file);

// Makes a registry entry from a checked unit box outline, with sortKey kCustomProfileSortKey and builtIn false.
std::unique_ptr<TrimProfile> makeOutlineProfile(std::string id, std::string name, std::vector<Vec2> unitOutline);

// What registering a folder of profile files did.
struct TrimProfileFolderLoad {
    std::vector<std::string> registered;                       // ids, in file name order
    std::vector<std::pair<std::string, Status>> refused;       // file names that were bad or used a taken id
};

// Reads every file ending in kTrimProfileFileSuffix in a folder (not its subfolders), in file name order, and
// registers each good one. A taken id is refused with AlreadyExists. A missing folder does nothing.
TrimProfileFolderLoad registerTrimProfileFolder(const std::filesystem::path& folder);

}
