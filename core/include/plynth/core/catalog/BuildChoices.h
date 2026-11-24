#pragma once

#include "../Choices.h"
#include "AssetCatalog.h"
#include "MaterialCatalog.h"

namespace plynth {

// The choices a library offers a build (Choices.h): every asset that is Ready, with its categories, tags, size
// and the supports its info.json holds (none until it was measured for them), and every material that is Ready,
// with its groups. Each list sorted by id. A missing catalog gives no entries of its kind.
BuildChoices buildChoicesFrom(const AssetCatalog* assets, const MaterialCatalog* materials);

}
