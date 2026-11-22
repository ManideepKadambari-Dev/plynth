#include "plynth/core/catalog/Integrations.h"

#include "../FileIo.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/catalog/AssetInfo.h"
#include "plynth/core/catalog/Importer.h"
#include "plynth/core/catalog/MaterialImporter.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// The registry: integrations by id, guarded because registration and lookup may meet on different threads.
struct Registry {
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<Integration>, std::less<>> integrations;
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

// Escapes a key for use inside a JSON pointer.
std::string pointerPart(std::string_view key)
{
    std::string out;
    for (const char c : key) {
        if (c == '~') out += "~0";
        else if (c == '/') out += "~1";
        else out += c;
    }
    return out;
}

// Reads the file as JSON, or gives a failed status.
Status parseFile(const fs::path& file, Json& out)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) return fail(ErrorCode::CannotRead, "integrations.json could not be read.");
    std::ostringstream text;
    text << in.rdbuf();
    out = Json::parse(text.str(), nullptr, false);
    if (out.is_discarded()) return fail(ErrorCode::BadFile, "integrations.json is not valid JSON.");
    return {};
}

// Writes the file with the given integrations object.
Status writeSaved(const LibraryPaths& library, const Json& saved)
{
    Json file = Json::object();
    file["format"] = std::string(kIntegrationsFormat);
    file["version"] = kIntegrationsVersion;
    file["integrations"] = saved;
    std::error_code ec;
    fs::create_directories(library.root, ec);
    return fileio::writeAtomic(integrationsFile(library), file.dump(2) + "\n");
}

// Checks one saved entry key by key so the failure can point at the option.
Status checkSavedEntry(const Integration& integration, const Json& entry, const std::string& id)
{
    const std::string base = "/integrations/" + pointerPart(id);
    if (!entry.is_object()) {
        return fail(ErrorCode::BadFile, "integrations.json: the entry for " + id + " is not an object.", base);
    }
    for (const auto& item : entry.items()) {
        Json one = Json::object();
        one[item.key()] = item.value();
        const Status s = integration.checkOptions(one, OptionUse::Saved);
        if (!s.ok()) return fail(ErrorCode::BadFile, "integrations.json: " + s.message, base + "/" + pointerPart(item.key()));
    }
    return {};
}

}

// Adds an integration unless its id is taken or badly formed.
bool registerIntegration(std::unique_ptr<Integration> integration)
{
    if (!integration) return false;
    const std::string id(integration->id());
    if (!isWellFormedId(id)) return false;
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    return r.integrations.emplace(id, std::move(integration)).second;
}

IntegrationRegistrar::IntegrationRegistrar(std::unique_ptr<Integration> integration)
{
    registerIntegration(std::move(integration));
}

// Looks an integration up by id.
const Integration* findIntegration(std::string_view id)
{
    Registry& r = registry();
    std::lock_guard lock(r.mutex);
    const auto it = r.integrations.find(id);
    return it == r.integrations.end() ? nullptr : it->second.get();
}

// Lists the integrations by order, then id.
std::vector<const Integration*> allIntegrations()
{
    std::vector<const Integration*> out;
    {
        Registry& r = registry();
        std::lock_guard lock(r.mutex);
        for (const auto& entry : r.integrations) out.push_back(entry.second.get());
    }
    std::stable_sort(out.begin(), out.end(), [](const Integration* a, const Integration* b) {
        if (a->order() != b->order()) return a->order() < b->order();
        return a->id() < b->id();
    });
    return out;
}

// Finds the integration that brings an importer.
const Integration* integrationForImporter(std::string_view importerId, bool material)
{
    for (const Integration* integration : allIntegrations()) {
        const std::vector<std::string> ids = material ? integration->materialImporters() : integration->assetImporters();
        if (std::find(ids.begin(), ids.end(), importerId) != ids.end()) return integration;
    }
    return nullptr;
}

// Gives no importer for a button yet.
std::string integrationImporter(const Integration&, const ImportSource&, bool)
{
    return {};
}

// Says whether a source kind is a store item.
bool isStoreSource(std::string_view sourceKind)
{
    return sourceKind == kSourceBlenderKit || sourceKind == kSourceFabZip;
}

// The place of integrations.json.
fs::path integrationsFile(const LibraryPaths& library)
{
    return library.root / std::string(kIntegrationsFileName);
}

// Reads integrations.json and checks it.
IntegrationOptionsRead readIntegrationOptions(const LibraryPaths& library)
{
    IntegrationOptionsRead result;
    const fs::path file = integrationsFile(library);
    std::error_code ec;
    if (!fs::exists(file, ec)) return result;
    result.exists = true;
    Json json;
    if (Status s = parseFile(file, json); !s.ok()) {
        result.status = s;
        return result;
    }
    if (!json.is_object() || json.value("format", std::string()) != kIntegrationsFormat) {
        result.status = fail(ErrorCode::BadFile, "integrations.json is not a Plynth integrations file.", "/format");
        return result;
    }
    const auto version = json.find("version");
    if (version == json.end() || !version->is_number_integer()) {
        result.status = fail(ErrorCode::BadFile, "integrations.json has no version number.", "/version");
        return result;
    }
    if (version->get<int>() > kIntegrationsVersion) {
        result.status = fail(ErrorCode::UnsupportedVersion, "integrations.json was written by a newer Plynth.", "/version");
        return result;
    }
    const auto entries = json.find("integrations");
    if (entries == json.end() || !entries->is_object()) {
        result.status = fail(ErrorCode::BadFile, "integrations.json has no integrations object.", "/integrations");
        return result;
    }
    for (const auto& item : entries->items()) {
        if (const Integration* integration = findIntegration(item.key())) {
            if (Status s = checkSavedEntry(*integration, item.value(), item.key()); !s.ok()) {
                result.status = s;
                return result;
            }
        }
    }
    result.saved = *entries;
    return result;
}

// Puts what is saved over the defaults.
Json integrationOptions(const Integration& integration, const Json& saved)
{
    Json out = integration.defaultOptions();
    const auto entry = saved.find(std::string(integration.id()));
    if (entry != saved.end() && entry->is_object()) {
        for (const auto& item : entry->items()) {
            if (out.contains(item.key())) out[item.key()] = item.value();
        }
    }
    return out;
}

// Saves changes to one integration's options.
Status setIntegrationOptions(const LibraryPaths& library, std::string_view id, const Json& changes)
{
    const Integration* integration = findIntegration(id);
    if (!integration) return fail(ErrorCode::NotFound, "There is no integration called " + std::string(id) + ".");
    if (!changes.is_object()) return fail(ErrorCode::InvalidValue, "The options must be an object.");
    if (Status s = integration->checkOptions(changes, OptionUse::Saved); !s.ok()) return s;
    IntegrationOptionsRead current = readIntegrationOptions(library);
    if (!current.status.ok()) return current.status;
    Json& entry = current.saved[std::string(id)];
    if (!entry.is_object()) entry = Json::object();
    for (const auto& item : changes.items()) entry[item.key()] = item.value();
    return writeSaved(library, current.saved);
}

// Removes an integration's saved entry.
Status resetIntegrationOptions(const LibraryPaths& library, std::string_view id)
{
    if (!findIntegration(id)) return fail(ErrorCode::NotFound, "There is no integration called " + std::string(id) + ".");
    IntegrationOptionsRead current = readIntegrationOptions(library);
    if (!current.status.ok()) return current.status;
    if (!current.exists || !current.saved.contains(std::string(id))) return {};
    current.saved.erase(std::string(id));
    return writeSaved(library, current.saved);
}

// Works out the options of one import: defaults, then saved, then the request.
ImporterOptions optionsForImporter(const LibraryPaths& library, std::string_view importerId, bool material,
                                   const Json& request)
{
    ImporterOptions result;
    const bool noRequest = request.is_null() || (request.is_object() && request.empty());
    if (!noRequest && !request.is_object()) {
        result.status = fail(ErrorCode::InvalidValue, "The importer options must be an object.");
        return result;
    }
    const Integration* integration = integrationForImporter(importerId, material);
    if (!integration) {
        if (!noRequest) result.status = fail(ErrorCode::InvalidValue, std::string(importerId) + " takes no options.");
        return result;
    }
    if (!noRequest) {
        if (Status s = integration->checkOptions(request, OptionUse::Import); !s.ok()) {
            result.status = s;
            return result;
        }
    }
    const IntegrationOptionsRead read = readIntegrationOptions(library);
    if (!read.status.ok()) {
        result.status = read.status;
        return result;
    }
    result.options = integrationOptions(*integration, read.saved);
    if (!noRequest) {
        for (const auto& item : request.items()) result.options[item.key()] = item.value();
    }
    return result;
}

// The name of a state in the CLI answer.
std::string_view integrationStateName(IntegrationState state)
{
    switch (state) {
    case IntegrationState::Ready: return "ready";
    case IntegrationState::NeedsSetup: return "needs_setup";
    case IntegrationState::NotChecked: return "not_checked";
    }
    return "not_checked";
}

// Works out the state from a setup report.
IntegrationStatus integrationStatus(const Integration& integration, const SetupReport* report)
{
    IntegrationStatus out;
    if (integration.setupItem().empty()) {
        out.state = IntegrationState::Ready;
        out.detail = "Ready.";
        return out;
    }
    const SetupItem* item = report ? report->find(integration.setupItem()) : nullptr;
    if (!item || item->state == CheckState::NotChecked) {
        out.state = IntegrationState::NotChecked;
        if (item) {
            out.detail = item->detail;
            out.link = item->link;
        }
        return out;
    }
    out.detail = item->detail;
    out.action = item->action;
    out.link = item->link;
    out.state = item->state == CheckState::Found ? IntegrationState::Ready : IntegrationState::NeedsSetup;
    return out;
}

// One entry as the CLI lists it.
Json integrationToJson(const Integration& integration, const IntegrationStatus& status, const Json& options)
{
    std::set<std::string> suffixes;
    const std::vector<std::string> assetIds = integration.assetImporters();
    for (const std::string& id : assetIds) {
        if (const Importer* importer = findImporter(id)) {
            for (const std::string& s : importer->fileSuffixes()) suffixes.insert(s);
        }
    }
    const std::vector<std::string> materialIds = integration.materialImporters();
    for (const std::string& id : materialIds) {
        if (const MaterialImporter* importer = findMaterialImporter(id)) {
            for (const std::string& s : importer->fileSuffixes()) suffixes.insert(s);
        }
    }
    Json out = Json::object();
    out["id"] = std::string(integration.id());
    out["name"] = std::string(integration.name());
    out["description"] = std::string(integration.description());
    out["order"] = integration.order();
    out["takes_addresses"] = integration.takesAddresses();
    out["suffixes"] = Json::array();
    for (const std::string& s : suffixes) out["suffixes"].push_back(s);
    out["asset_importers"] = assetIds;
    out["material_importers"] = materialIds;
    out["note"] = std::string(integration.note());
    out["site"] = std::string(integration.site());
    out["state"] = std::string(integrationStateName(status.state));
    out["detail"] = status.detail;
    out["action"] = status.action;
    out["link"] = status.link;
    out["options"] = options;
    out["defaults"] = integration.defaultOptions();
    return out;
}

}
