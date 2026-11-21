#pragma once

#include "Database.h"

namespace plynth::catalog {

// Fills a new assets.db with the built-in room types, categories, links and licenses.
void seedBuiltIns(Database& db);

}
