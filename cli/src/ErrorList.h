#pragma once

#include "Answer.h"

namespace plynth::cli {

// Every error code an answer can carry: each core code, then the codes only the CLI gives. Each entry has code,
// description and from ("core" or "cli").
Json errorCodesJson();

// Every exit code with its meaning: code and description.
Json exitCodesJson();

}
