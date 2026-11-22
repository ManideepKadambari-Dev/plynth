#pragma once

#include "Plan.h"
#include "Project.h"
#include "Status.h"

#include <compare>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Exporters are registry entries (DESIGN.md sections 4 and 7.16). An exporter decides which FBX files an
// export writes and which built objects go into each. Its id is the project's export mode name
// (exportModeName), so "single" serves ExportMode::Single and "grouped" serves ExportMode::Grouped. Each lives
// in its own file in core/src/exporters/ with an ExporterRegistrar at namespace scope, built into an OBJECT
// library like the importers, so adding one never means editing existing code.
//
// The Blender side writes every group with its one FBX profile (blender/plynth/exporters/fbx.py). A UCX_
// collision mesh always goes into the file of the object it belongs to. Every group shares the building
// origin, because the export applies every transform (ExportJob.h, step prepare).
//
// Which group a built object goes to (the worker's write_fbx step, in this order):
//   1. the group whose members list its plynth_kind and plynth_id exactly
//   2. the group whose members list its plynth_kind with an empty id (every object of that kind)
//   3. the first group in order with no members whose kinds and ids filters take it
//   4. the group with rest set (objects nobody else takes: hand added ones, kinds no rule knows)
// An object none of these takes is left out and named in a finding of the "groups" check. Objects of kind
// "kit" (Kit.h) never go into a group. A group that gets no object is not written (ExportWorkerOutput).
// Core never lists one (kind, id) pair in two groups.

// One built object a group takes: its plynth_kind and plynth_id custom properties (ExportGroups.h lists the
// kinds). An empty id takes every object of that kind that no exact member takes.
struct ExportMember {
    std::string kind;
    Id id;

    bool operator==(const ExportMember&) const = default;
    auto operator<=>(const ExportMember&) const = default;
};

// One FBX file of an export.
struct ExportGroup {
    std::string id;                 // lower case, for example "building"
    std::string file;               // the file name inside export/, for example "Flat.fbx"
    std::vector<std::string> kinds; // plynth_kind values of the objects it takes; empty takes every kind
    std::vector<Id> ids;            // plynth_id values it takes; empty takes every id
    std::string name;               // shown to the user and used for the file name, for example "Walls"
    std::vector<ExportMember> members; // when not empty, the group takes these and kinds and ids are not used
    bool rest = false;              // takes every object no other group takes; at most one group has it

    bool operator==(const ExportGroup&) const = default;
};

class Exporter {
public:
    virtual ~Exporter() = default;

    // Stable lower case id, the export mode name, for example "single".
    virtual std::string_view id() const = 0;
    // Short name for the app, for example "Single FBX".
    virtual std::string_view name() const = 0;
    // One plain sentence on what it writes.
    virtual std::string_view description() const = 0;
    // The files of an export of this project. Every built object must fall into exactly one group.
    // InvalidValue with a plain sentence when the project cannot be exported this way. runExport passes the
    // project with the plan of the last successful build (build/built-plan.json) when there is one, so the
    // groups match the objects in building.blend.
    virtual Status groups(const Project& project, std::vector<ExportGroup>& out) const = 0;
    // True when the user reviews and edits the groups before an export (rename, merge, move members), so
    // runExport applies the project's saved edits (ExportGroups.h) to what groups gives. False by default.
    virtual bool reviewable() const { return false; }
};

// Adds an exporter. False, and it is dropped, when its id is taken or not a well formed lower case name.
bool registerExporter(std::unique_ptr<Exporter> exporter);

// Registers an exporter while the program starts.
struct ExporterRegistrar {
    explicit ExporterRegistrar(std::unique_ptr<Exporter> exporter);
};

// The exporter with this id. Null when there is none.
const Exporter* findExporter(std::string_view id);

// Every exporter, sorted by id.
std::vector<const Exporter*> allExporters();

// The file name stem of a project's export: the project name with every run of characters other than letters,
// digits, "_" and "-" turned into one "_", trimmed of "_" at both ends, at most 64 characters; "Building" when
// nothing is left. "My Flat 2" gives "My_Flat_2".
std::string exportFileStem(std::string_view projectName);

// The single exporter (id "single", name "Single FBX"): one group "building", file exportFileStem(name) plus
// ".fbx", taking every object (DESIGN.md section 7.16 "Single").
inline constexpr std::string_view kSingleExporterId = "single";
inline constexpr std::string_view kSingleExportGroup = "building";

// The grouped exporter (id "grouped", name "Grouped FBX", reviewable): the groups of automaticExportGroups
// (ExportGroups.h), one file each, named by nameExportGroupFiles (DESIGN.md section 7.16 "Grouped").
inline constexpr std::string_view kGroupedExporterId = "grouped";

}
