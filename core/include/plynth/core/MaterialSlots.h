#pragma once

#include "Plan.h"

#include <optional>
#include <string>
#include <string_view>

namespace plynth {

// Material slot names (DESIGN.md section 7.16: "named material slots, for example Hall_Floor, Bedroom_Wall, so
// the user assigns their own Unreal materials"). From milestone 10 every built surface's Blender and FBX
// material is named after its slot, not after its catalog material; the slot table of the build job says
// which catalog material (or neutral look) each slot shows. Core names the slots; the Blender side never does.
//
// Rules:
//   a wall face looking into room r (and an end face looking into r)   <Label(r)>_Wall
//   a wall face looking into no room                                   Exterior_Wall
//   wall tops and bottoms                                              Wall_Top
//   a room's floor and ceiling                                         <Label(r)>_Floor, <Label(r)>_Ceiling
//   reveals                    the slot of the wall face whose material they follow (Materials.h)
//   thresholds                 the floor slot of the room whose floor they follow, or Threshold when neither
//                              side has a room
//   door leaves, frames, glass and handles                             Door, Frame, Glass, Handle
//   slab edges and ledge tops                                          Exterior_Wall (look Wall_Outside)
//   stair well sides                                                   Stair_Well (look Wall_Inside)
//   slab and roof undersides                                           Soffit
//   stairs, every part                                                 Stair
//   a roof's covering          Flat_Roof when the type built is flat, otherwise Roof
//   roof edges                                                         Fascia
//   gables and parapets                                                Exterior_Wall (look Wall_Outside)
//   sawtooth glazing                                                   Glass
//   a generator's material parameter                                   <Generator>_<Param> (look Generated)
//   a generator without a material parameter                           Generated
// <Generator> and <Param> are the ids in PascalCase, each then cut to 24 characters: Column_Material. Items of
// one generator share their slots across the whole building. When <Generator> is Door, Frame, Glass, Handle,
// Threshold, Stair, Roof, Soffit, Fascia, Wall, Exterior, Flat or Generated, or <Param> is Wall, Floor or
// Ceiling, the name is <Generator>Generator_<Param>, for example DoorGenerator_Frame, so it meets no surface's
// slot. When one name would show more than one material the rule at the end holds for it too:
// Column_Material_WhitePaint, Column_Material_Neutral.
// Two different (generator, parameter) pairs never share a name and none shares one with a surface, also when
// these rules alone would give the same (a room type living_generator and the generator living with the
// parameter wall, the generators door and door_generator, ids alike in their first 24 characters). Surface
// slots are named first, as if there were no generators, and never change for one. Then the pairs, sorted by
// generator id and then parameter id, each take their name unless it, or one of the names the material rule
// makes of it, equals a name already taken (a surface slot's, with or without its material, Generated, or
// those of an earlier pair), case ignored. Such a pair takes the first free name with 2, 3 and so on at the
// end of the generator's part: LivingGenerator2_Wall, DoorGenerator2_Material.
// A part that a slab or roof section does not have (a well without a stair, a gable on a flat roof) is treated
// like a part an opening does not have: it names no material and makes no other name show two materials.
// Label(r) is the room type in PascalCase ("living" Living, "guest_room" GuestRoom), or Room when the room
// has no type. Rooms with the same type share their slots across the whole building.
// When one name would show more than one material (a bedroom with oak floors and one with tiles), every use
// of that name gets the material added: Bedroom_Floor_OakPlanks, Bedroom_Floor_GreyTile, and _Neutral for a
// surface with no material. Names hold only letters, digits and "_".

inline constexpr std::string_view kExteriorWallSlot = "Exterior_Wall";
inline constexpr std::string_view kWallTopSlot = "Wall_Top";
inline constexpr std::string_view kThresholdSlot = "Threshold";
inline constexpr std::string_view kDoorSlot = "Door";
inline constexpr std::string_view kFrameSlot = "Frame";
inline constexpr std::string_view kGlassSlot = "Glass";
inline constexpr std::string_view kHandleSlot = "Handle";
inline constexpr std::string_view kStairSlot = "Stair";
inline constexpr std::string_view kRoofSlot = "Roof";
inline constexpr std::string_view kFlatRoofSlot = "Flat_Roof";
inline constexpr std::string_view kSoffitSlot = "Soffit";
inline constexpr std::string_view kFasciaSlot = "Fascia";
inline constexpr std::string_view kStairWellSlot = "Stair_Well";
inline constexpr std::string_view kGeneratedSlot = "Generated";
inline constexpr std::string_view kGeneratorSlotLabel = "Generator"; // added to a reserved generator label
inline constexpr std::string_view kNoRoomTypeLabel = "Room";
inline constexpr std::string_view kNeutralSlotSuffix = "Neutral";

// Turns an id into PascalCase: split on "_", "-" and spaces, each part's first letter upper case, the rest
// kept, other characters dropped. "guest_room" gives "GuestRoom", "oak_planks" gives "OakPlanks". A result that
// is empty or starts with a digit gets fallback in front.
std::string slotPascalCase(std::string_view id, std::string_view fallback = kNoRoomTypeLabel);

// The label of a room's slots: its type in PascalCase, or kNoRoomTypeLabel.
std::string roomSlotLabel(const Room& room);

// The name of a slot whose plain name shows more than one material: base, "_" and the material in PascalCase,
// or kNeutralSlotSuffix when material is empty.
std::string slotNameWithMaterial(std::string_view base, std::string_view material);

}
