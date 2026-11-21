#pragma once

#include "plynth/core/Library.h"

namespace plynth {

// Makes the folders of a library that are missing. Leaves .staging alone. CannotWrite.
Status makeLibraryFolders(const LibraryPaths& paths);

// Creates assets.db and materials.db when they are missing, with their tables and the built-in lists, and
// checks the version of those that exist. DatabaseError or UnsupportedVersion.
Status ensureLibraryDatabases(const LibraryPaths& paths);

}
