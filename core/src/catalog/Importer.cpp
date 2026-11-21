#include "plynth/core/catalog/Importer.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace plynth {

namespace {

// The registry: importers by id, guarded because registration and lookup may meet on different threads.
struct Registry {
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<Importer>, std::less<>> importers;
};

// The one registry. Made on first use so registrars in any file can use it while the program starts.
Registry& registry()
{
    static Registry instance;
    return instance;
}

// Says whether an id is a lower case name: a letter, then letters, digits or "_".
bool isWellFormedImporterId(std::string_view id)
{
    if (id.empty() || !(id[0] >= 'a' && id[0] <= 'z')) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

}

// Adds an importer unless its id is taken or badly formed.
bool registerImporter(std::unique_ptr<Importer> importer)
{
    if (!importer) return false;
    const std::string id(importer->id());
    if (!isWellFormedImporterId(id)) return false;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    return r.importers.emplace(id, std::move(importer)).second;
}

ImporterRegistrar::ImporterRegistrar(std::unique_ptr<Importer> importer)
{
    registerImporter(std::move(importer));
}

// Looks an importer up by id.
const Importer* findImporter(std::string_view id)
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    const auto it = r.importers.find(id);
    return it == r.importers.end() ? nullptr : it->second.get();
}

// Lists the importers in id order.
std::vector<const Importer*> allImporters()
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    std::vector<const Importer*> out;
    for (const auto& entry : r.importers) out.push_back(entry.second.get());
    return out;
}

// The importer with the best score for a source.
const Importer* pickImporter(const ImportSource& source)
{
    const Importer* best = nullptr;
    int bestScore = 0;
    for (const Importer* importer : allImporters()) {
        const int score = importer->score(source);
        if (score > bestScore) {
            best = importer;
            bestScore = score;
        }
    }
    return best;
}

// Every file ending an importer reads.
std::vector<std::string> importableSuffixes()
{
    std::vector<std::string> out;
    for (const Importer* importer : allImporters()) {
        for (const std::string& suffix : importer->fileSuffixes()) out.push_back(suffix);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}
