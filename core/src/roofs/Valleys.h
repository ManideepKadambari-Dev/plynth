#pragma once

#include "roofs/RoofParts.h"

#include <vector>

// Where the pitched roofs of one floor meet. Written in Valleys.cpp; floorRoofs (Roofs.cpp) calls joinSections
// once all sections of a floor are built, before their faces are tidied. Roof types may use the other two.
namespace plynth::roofparts {

// One built roof section as the join sees it.
struct JoinSection {
    bool pitched = false;        // the type built uses pitch; only pitched sections join
    RoofInput input;
    std::vector<RoofFace> faces; // as the type built them, not yet tidied
};

// The covering faces of a roof as pieces: each face seen from above with the plane it lies in.
std::vector<Piece> coveringPieces(const std::vector<RoofFace>& faces);

// The faces of planar pieces: each piece's covering, its underside kRoofThickness lower, and an edge face wherever
// a piece's outline is the edge of the roof, that is where no piece of all lies beyond it or one lies beyond it
// lower down (a step). Parts along the outline of above get no edge.
std::vector<RoofFace> pieceFaces(const std::vector<Piece>& own, const std::vector<Piece>& all,
                                 const std::vector<WorldPolygon>& above);

// Joins the pitched sections of one floor whose eaves overlap. A roof carries on over its neighbour's eaves
// wherever its planes that reach them stay above the neighbour's roof (a wing runs into the main roof), then at
// every point only the highest covering is kept, ties going to the section first in index order. The coverings
// so meet in valley lines with no gap and no overlap, and together cover the eaves of all of them. Undersides and
// edges are made again from the coverings; gable and glazing faces are kept as built. Sections that meet no
// other are left as they are.
void joinSections(std::vector<JoinSection>& sections, const std::vector<WorldPolygon>& above);

}
