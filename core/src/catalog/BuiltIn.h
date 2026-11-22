#pragma once

#include "Database.h"

namespace plynth::catalog {

// Fills a new assets.db with the built-in room types, categories, links and licenses.
void seedBuiltIns(Database& db);

// Fills materials.db with the built-in material groups when its groups table is empty, and the licenses when
// its license table is empty.
void seedMaterialBuiltIns(Database& db);

}
