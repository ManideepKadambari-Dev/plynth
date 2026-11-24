#include "SchemaSupport.h"
#include "TestSupport.h"
#include "../../core/src/IdSource.h"

#include <plynth/core/ExportGroups.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <regex>
#include <set>
#include <string>
#include <vector>

using namespace plynth;

TEST_CASE("a new group does not take an id that a stored edit names", "[core][export][ids]")
{
    const int edit = GENERATE(0, 1, 2, 3);
    INFO("edit " << edit);
    ExportGroup walls;
    walls.id = "structure";
    walls.name = "Walls";
    const ExportMember sofa{std::string(kKindItem), "i1"};
    ExportGroup furniture;
    furniture.id = "furniture";
    furniture.name = "Furniture";
    furniture.members = {sofa};
    const std::vector<ExportGroup> automatic{walls, furniture};

    ExportGroupEdits edits;
    if (edit == 0) edits.moves.push_back({sofa, "group_1"});
    if (edit == 1) edits.renames.push_back({"group_1", "Old"});
    if (edit == 2) edits.merges.push_back({"group_1", "structure"});
    if (edit == 3) edits.merges.push_back({"structure", "group_1"});
    std::string id;
    test::requireOk(addExportGroup(edits, applyExportGroupEdits(automatic, edits, "Flat").groups, "Extra", id));
    CHECK(id == "group_2");
    const ExportGroupsApplied after = applyExportGroupEdits(automatic, edits, "Flat");
    CHECK(after.notes.size() == 1);
    for (const ExportGroup& group : after.groups) {
        if (group.id != id) continue;
        CHECK(group.members.empty());
        CHECK(group.name == "Extra");
    }
}

namespace {

// Says whether an added group id has the new shape.
bool newGroupShape(const std::string& id)
{
    static const std::regex shape("^group_[0-9][0-9a-hjkmnp-tv-z]{7}$");
    return std::regex_match(id, shape) && id.substr(7).find_first_not_of("0123456789") != std::string::npos;
}

// An export options file with one added group of this id.
Json optionsWithGroup(const std::string& id)
{
    return Json::parse(R"({"format":"plynth-export-options","version":3,"groups":{"added":[{"id":")" + id +
                       R"(","name":"Hero"}],"renames":[],"merges":[],"moves":[]},"kit":{"enabled":false}})");
}

}

TEST_CASE("a new group id is drawn again while any stored edit names it", "[core][export][ids]")
{
    ExportGroup walls;
    walls.id = "structure";
    walls.name = "Walls";
    const ExportMember sofa{std::string(kKindItem), "i1"};
    walls.members = {sofa};
    const std::vector<ExportGroup> automatic{walls};

    ExportGroupEdits edits;
    edits.added.push_back({"group_2aaaaaaa", "Kept"});
    edits.renames.push_back({"group_3bbbbbbb", "Old"});
    edits.merges.push_back({"group_4ccccccc", "structure"});
    edits.merges.push_back({"structure", "group_5ddddddd"});
    edits.moves.push_back({sofa, "group_6eeeeeee"});
    detail::ScopedIdSource source(detail::scriptedIdParts(
        {"2aaaaaaa", "3bbbbbbb", "4ccccccc", "5ddddddd", "6eeeeeee", "7fffffff"}));
    std::string id;
    test::requireOk(addExportGroup(edits, applyExportGroupEdits(automatic, edits, "Flat").groups, "Extra", id));
    CHECK(id == "group_7fffffff");
    CHECK(newGroupShape(id));

    ExportOptions options;
    options.groups = edits;
    CHECK(test::schemaProblem("export-options.schema.json", nlohmann::json::parse(exportOptionsToJson(options).dump())) ==
          "");
}

TEST_CASE("the product source gives added groups ids of the new shape", "[core][export][ids]")
{
    detail::ScopedIdSource source{detail::IdPartMaker(&detail::systemIdPart)};
    ExportGroupEdits edits;
    std::set<std::string> seen;
    for (int n = 0; n < 200; ++n) {
        std::string id;
        test::requireOk(addExportGroup(edits, applyExportGroupEdits({}, edits, "Flat").groups, "Group " + std::to_string(n), id));
        CHECK(newGroupShape(id));
        CHECK(seen.insert(id).second);
    }
}

TEST_CASE("added group ids of both shapes are read and others refused", "[core][export][ids]")
{
    for (const char* good : {"group_1", "group_12", "group_7k3q9xm2", "group_0abcdefg"}) {
        INFO(good);
        const ExportOptionsRead read = exportOptionsFromJson(optionsWithGroup(good));
        test::requireOk(read.status);
        CHECK(read.options.groups.added.at(0).id == good);
        CHECK(test::schemaProblem("export-options.schema.json", nlohmann::json::parse(optionsWithGroup(good).dump())) == "");
    }
    for (const char* bad : {"group_0", "group_", "group_7k3q9xm", "group_7k3q9xm22", "group_7K3Q9XM2", "group_7k3q9xmi",
                            "group_abcdefgh", "group_7k3q9-m2", "Group_7k3q9xm2"}) {
        INFO(bad);
        CHECK(exportOptionsFromJson(optionsWithGroup(bad)).status.code == ErrorCode::BadFile);
        CHECK_FALSE(test::schemaProblem("export-options.schema.json", nlohmann::json::parse(optionsWithGroup(bad).dump())).empty());
    }
}
