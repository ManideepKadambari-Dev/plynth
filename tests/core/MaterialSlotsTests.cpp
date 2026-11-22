#include <plynth/core/MaterialSlots.h>

#include <catch2/catch_test_macros.hpp>

using namespace plynth;

TEST_CASE("slotPascalCase turns ids into PascalCase", "[slots]") {
    REQUIRE(slotPascalCase("guest_room") == "GuestRoom");
    REQUIRE(slotPascalCase("oak_planks") == "OakPlanks");
    REQUIRE(slotPascalCase("living") == "Living");
    REQUIRE(slotPascalCase("grey-tile one") == "GreyTileOne");
    REQUIRE(slotPascalCase("paint_a") == "PaintA");
    REQUIRE(slotPascalCase("keepCase") == "KeepCase");
    REQUIRE(slotPascalCase("a.b!c") == "Abc");
}

TEST_CASE("slotPascalCase uses the fallback for empty or digit led results", "[slots]") {
    REQUIRE(slotPascalCase("2nd") == "Room2nd");
    REQUIRE(slotPascalCase("") == "Room");
    REQUIRE(slotPascalCase("___") == "Room");
    REQUIRE(slotPascalCase("!!") == "Room");
    REQUIRE(slotPascalCase("2nd", "Mat") == "Mat2nd");
    REQUIRE(slotPascalCase("", "Mat") == "Mat");
    REQUIRE(slotPascalCase("2nd", "") == "2nd");
}

TEST_CASE("roomSlotLabel is the room type or Room", "[slots]") {
    Room room;
    REQUIRE(roomSlotLabel(room) == "Room");
    room.type = "guest_room";
    REQUIRE(roomSlotLabel(room) == "GuestRoom");
    room.type = "";
    REQUIRE(roomSlotLabel(room) == "Room");
    room.type = "2nd";
    REQUIRE(roomSlotLabel(room) == "Room2nd");
}

TEST_CASE("slotNameWithMaterial adds the material or Neutral", "[slots]") {
    REQUIRE(slotNameWithMaterial("Bedroom_Floor", "grey_tile") == "Bedroom_Floor_GreyTile");
    REQUIRE(slotNameWithMaterial("Bedroom_Floor", "oak_planks") == "Bedroom_Floor_OakPlanks");
    REQUIRE(slotNameWithMaterial("Bedroom_Floor", "") == "Bedroom_Floor_Neutral");
    REQUIRE(slotNameWithMaterial("Bedroom_Floor", "!!") == "Bedroom_Floor_Neutral");
}

TEST_CASE("slot constants hold only safe characters", "[slots]") {
    for (const std::string_view name : {kExteriorWallSlot, kWallTopSlot, kThresholdSlot, kDoorSlot, kFrameSlot,
                                        kGlassSlot, kHandleSlot, kNoRoomTypeLabel, kNeutralSlotSuffix}) {
        for (const char c : name) {
            REQUIRE(((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'));
        }
    }
}
