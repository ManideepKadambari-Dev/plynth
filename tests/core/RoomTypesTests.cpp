#include <plynth/core/RoomTypes.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace plynth;

TEST_CASE("built in room types", "[core][roomtypes]") {
    const auto types = builtInRoomTypes();
    REQUIRE(types.size() == 9);
    const char* ids[] = {"living", "bedroom", "kitchen", "bathroom", "hall", "dining", "office", "storage", "other"};
    for (std::size_t i = 0; i < 9; ++i) REQUIRE(types[i].id == ids[i]);
    REQUIRE(types[0].name == "Living room");
    REQUIRE(types[5].name == "Dining room");
}

TEST_CASE("findRoomType", "[core][roomtypes]") {
    const RoomType* t = findRoomType("kitchen");
    REQUIRE(t);
    REQUIRE(t->name == "Kitchen");
    REQUIRE(findRoomType("garage") == nullptr);
    REQUIRE(findRoomType("") == nullptr);
}

TEST_CASE("roomTypeName", "[core][roomtypes]") {
    REQUIRE(roomTypeName(std::string("bedroom")) == "Bedroom");
    REQUIRE(roomTypeName(std::string("garage")) == "garage");
    REQUIRE(roomTypeName(std::nullopt) == "Room");
    REQUIRE(roomTypeName(std::string("")) == "Room");
}

TEST_CASE("checkRoomType", "[core][roomtypes]") {
    REQUIRE(checkRoomType("living").ok());
    const Status s = checkRoomType("garage");
    REQUIRE(s.code == ErrorCode::InvalidValue);
    REQUIRE(s.message.find("garage") != std::string::npos);
    for (const RoomType& t : builtInRoomTypes()) {
        REQUIRE(s.message.find(std::string(t.id)) != std::string::npos);
    }
}
