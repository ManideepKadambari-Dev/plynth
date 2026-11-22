#pragma once

#include "Plan.h"
#include "Project.h"
#include "Status.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace plynth {

// Exporters are registry entries (DESIGN.md sections 4 and 7.16). An exporter decides which FBX files an
// export writes and which built objects go into each. Its id is the project's export mode name
// (exportModeName), so "single" serves ExportMode::Single and the "grouped" exporter of milestone 11 serves
// ExportMode::Grouped. Each lives in its own file in core/src/exporters/ with an ExporterRegistrar at
// namespace scope, built into an OBJECT library like the importers, so adding one never means editing
// existing code.
//
// The Blender side writes every group with its one FBX profile (blender/plynth/exporters/fbx.py). A UCX_
// collision mesh always goes into the file of the object it belongs to.

// One FBX file of an export.
struct ExportGroup {
    std::string id;                 // lower case, for example "building"
    std::string file;               // the file name inside export/, for example "Flat.fbx"
    std::vector<std::string> kinds; // plynth_kind values of the objects it takes; empty takes every kind
    std::vector<Id> ids;            // plynth_id values it takes; empty takes every id

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
    // InvalidValue with a plain sentence when the project cannot be exported this way.
    virtual Status groups(const Project& project, std::vector<ExportGroup>& out) const = 0;
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

}
