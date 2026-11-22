#include "plynth/core/catalog/MaterialImporter.h"

#include <algorithm>
#include <array>
#include <map>
#include <mutex>

namespace plynth {

namespace {

// The registry: importers by id, guarded because registration and lookup may meet on different threads.
struct Registry {
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<MaterialImporter>, std::less<>> importers;
};

// The one registry. Made on first use so registrars in any file can use it while the program starts.
Registry& registry()
{
    static Registry instance;
    return instance;
}

// Says whether an id is a lower case name: a letter, then letters, digits or "_".
bool isWellFormedId(std::string_view id)
{
    if (id.empty() || !(id[0] >= 'a' && id[0] <= 'z')) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

}

// The picture endings a texture set may hold.
std::span<const std::string_view> textureImageSuffixes()
{
    static constexpr std::array<std::string_view, 8> suffixes = {".png", ".jpg", ".jpeg", ".tga", ".tif", ".tiff", ".bmp", ".exr"};
    return suffixes;
}

// Adds an importer unless its id is taken or badly formed.
bool registerMaterialImporter(std::unique_ptr<MaterialImporter> importer)
{
    if (!importer) return false;
    const std::string id(importer->id());
    if (!isWellFormedId(id)) return false;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    return r.importers.emplace(id, std::move(importer)).second;
}

MaterialImporterRegistrar::MaterialImporterRegistrar(std::unique_ptr<MaterialImporter> importer)
{
    registerMaterialImporter(std::move(importer));
}

// Looks an importer up by id.
const MaterialImporter* findMaterialImporter(std::string_view id)
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    const auto it = r.importers.find(id);
    return it == r.importers.end() ? nullptr : it->second.get();
}

// Lists the importers in id order.
std::vector<const MaterialImporter*> allMaterialImporters()
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    std::vector<const MaterialImporter*> out;
    for (const auto& entry : r.importers) out.push_back(entry.second.get());
    return out;
}

// The importer with the best score for a source.
const MaterialImporter* pickMaterialImporter(const ImportSource& source)
{
    const MaterialImporter* best = nullptr;
    int bestScore = 0;
    for (const MaterialImporter* importer : allMaterialImporters()) {
        const int score = importer->score(source);
        if (score > bestScore) {
            best = importer;
            bestScore = score;
        }
    }
    return best;
}

// Every file ending some importer reads.
std::vector<std::string> importableMaterialSuffixes()
{
    std::vector<std::string> out;
    for (const MaterialImporter* importer : allMaterialImporters()) {
        for (const std::string& suffix : importer->fileSuffixes()) out.push_back(suffix);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}
