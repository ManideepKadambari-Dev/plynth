#pragma once

#include "../Generators.h"
#include "../Plan.h"
#include "../Status.h"
#include "AssetCatalog.h"
#include "MaterialCatalog.h"

#include <string_view>
#include <vector>

namespace plynth {

// Catalog checks the app's pickers and the CLI share (issues 44 and 45), so "plynth item params",
// "plynth place --param" and "plynth opening set --frame" refuse what the app's pickers block.

// Checks the material values in params against the materials catalog. params holds the values the user gave
// (the --param values, or the one box the app changed); it may also be a full set. Values of other kinds and
// unknown ids are left to checkGeneratorParams, so a default the user did not touch is never refused here and a
// default material the library lacks still only gives the build's missing_material warning. Each material value
// must be text that is empty or a well formed id (the checkGeneratorParams sentence), and when not empty it must
// be usable (materialForUse). The first refusal names the parameter: "The material of colour: There is no
// material oak_plank in the library.". Without a catalog a value that is not empty is refused with NoLibrary
// "The material of colour: There is no library with materials.", as the app's picker does.
Status checkGeneratorMaterials(const GeneratorInfo& info, const std::vector<GeneratorParamValue>& params,
                               const MaterialCatalog* materials);

// Checks a catalog asset as the frame of an opening of a type: NotFound when the catalog has no such asset;
// InvalidValue, in this order of checks, for a plain opening (checked first, before the catalog), when it is not
// in the door category for a door or the window category for a window, when it is Broken ("<name> cannot be
// fitted: <problem>"), or when it has not been measured yet ("<name> is not measured yet, so it cannot be fitted.
// Retry its render first."). An asset that NeedsRefresh can be fitted.
Status checkFrameAsset(const AssetCatalog& assets, std::string_view assetId, OpeningType type);

}
