#include "plynth/core/Exporter.h"

#include <map>
#include <mutex>

namespace plynth {

namespace {

// The registry: exporters by id, guarded because registration and lookup may meet on different threads.
struct Registry {
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<Exporter>, std::less<>> exporters;
};

// The one registry. Made on first use so registrars in any file can use it while the program starts.
Registry& registry()
{
    static Registry instance;
    return instance;
}

// Says whether an id is a lower case name: a letter, then letters, digits or "_".
bool isWellFormedExporterId(std::string_view id)
{
    if (id.empty() || !(id[0] >= 'a' && id[0] <= 'z')) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// Says whether a byte may stay in a file name stem.
bool isStemCharacter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

}

// Adds an exporter unless its id is taken or badly formed.
bool registerExporter(std::unique_ptr<Exporter> exporter)
{
    if (!exporter) return false;
    const std::string id(exporter->id());
    if (!isWellFormedExporterId(id)) return false;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    return r.exporters.emplace(id, std::move(exporter)).second;
}

ExporterRegistrar::ExporterRegistrar(std::unique_ptr<Exporter> exporter)
{
    registerExporter(std::move(exporter));
}

// Looks an exporter up by id.
const Exporter* findExporter(std::string_view id)
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    const auto it = r.exporters.find(id);
    return it == r.exporters.end() ? nullptr : it->second.get();
}

// Lists the exporters in id order.
std::vector<const Exporter*> allExporters()
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    std::vector<const Exporter*> out;
    for (const auto& entry : r.exporters) out.push_back(entry.second.get());
    return out;
}

// Turns a project name into a safe file name stem.
std::string exportFileStem(std::string_view projectName)
{
    std::string stem;
    for (const char c : projectName) {
        if (isStemCharacter(c)) {
            stem.push_back(c);
        } else if (stem.empty() || stem.back() != '_') {
            stem.push_back('_');
        }
    }
    const auto trim = [&stem] {
        while (!stem.empty() && stem.back() == '_') stem.pop_back();
        std::size_t start = 0;
        while (start < stem.size() && stem[start] == '_') ++start;
        stem.erase(0, start);
    };
    trim();
    if (stem.size() > 64) stem.resize(64);
    trim();
    return stem.empty() ? "Building" : stem;
}

}
