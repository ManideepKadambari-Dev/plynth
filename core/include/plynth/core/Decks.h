#pragma once

#include "BuildParts.h"
#include "Plan.h"
#include "Status.h"

#include <optional>
#include <string_view>
#include <vector>

namespace plynth {

// Decks and outside steps (issues 23 and 24, plan format 8).
//
// Decks: the part maker "decks" (order 20, kind "deck", group "surfaces"), core/src/parts/makers/DeckParts.cpp. Each
// deck of a floor is one part <floor>_<deck>_Deck: a prism of its outline from top - thickness to top above the
// floor's level, top face in the deck slot (surface Deck), sides in the same slot; with skirt, a second prism
// kSkirtInset inside the outline from the floor's level up to the deck's bottom, sides in the skirt slot. Its
// collision is the outline from the floor's level (or the deck bottom without a skirt) to top, cut into convex
// pieces. Where the outline overlaps the floor's wall bands it is cut back to the outer wall faces. Problems:
//   deck_outline_invalid (error: the outline crosses itself or has no area; not built),
//   deck_inside_building (warning: more than half of it lies inside the floor's footprint).
// A deck whose outline lies wholly on the wall bands also gets deck_outline_invalid and is not built.
//
// Outside steps: a Stair with rise set (Plan.h). stairLayout (Stairs.h) then uses that rise instead of the
// floor rise, needs no floor above, cuts no hole and arrives on nothing; the step count is worked out the same
// way (risers at most kMaxRiser, treads kStairGoing). A stair with rise set is checked against the decks and door
// sills it reaches: stair_rise_mismatch (warning) when its top is more than 0.02 m from the top of the deck or the
// sill of a door its arrival point lies in or within 0.3 m of, and stair_off_ground (warning) when it stands
// outside the footprint of a floor that is not the ground level floor. Its other problems are only
// stair_type_unknown and stair_overlaps_wall on its own floor. In the build job its to_floor is null. cheeks adds
// the part maker "stair_cheeks" (order 25, kind "stair", group "surfaces"), core/src/parts/makers/StairCheekParts.cpp:
// one solid wall kCheekThickness thick along each side of the flight, from the floor's level to kCheekRise above
// the nose line, in the stair's slot, object <floor>_<stair>_Cheeks with one hull per wall. Only a straight stair
// gets them; another type gets stair_cheeks_not_straight (warning). railing false leaves its railings out.

inline constexpr double kSkirtInset = 0.05;     // metres
inline constexpr double kCheekThickness = 0.15; // metres
inline constexpr double kCheekRise = 0.05;      // metres above the step noses

// Checks the values a deck command would set, with the rules of checkPlan. InvalidValue with a plain sentence.
Status checkDeck(const Deck& deck);

// The top of the deck a plan point lies on, above the floor's level, for railings, steps and items placed on a
// deck. Empty when the point is on no deck of the floor.
std::optional<double> deckTopAt(const Floor& floor, double x, double y);

}
