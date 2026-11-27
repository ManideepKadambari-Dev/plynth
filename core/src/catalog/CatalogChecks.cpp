#include "plynth/core/catalog/CatalogChecks.h"

#include "plynth/core/OpeningAssets.h"
#include "plynth/core/catalog/AssetInfo.h"

#include <algorithm>
#include <optional>
#include <string>
#include <variant>

namespace plynth {

namespace {

// Checks one material value on its own with the generator's own rules: text, empty or a well formed id.
Status materialValueForm(const GeneratorInfo& info, const GeneratorParam& param, const GeneratorValue& value)
{
    GeneratorInfo single;
    single.id = info.id;
    single.name = info.name;
    single.params = {param};
    return checkGeneratorParams(single, {GeneratorParamValue{param.id, value}});
}

}

// Checks the material values of a generator against the materials catalog.
Status checkGeneratorMaterials(const GeneratorInfo& info, const std::vector<GeneratorParamValue>& params,
                               const MaterialCatalog* materials)
{
    for (const GeneratorParamValue& entry : params) {
        const GeneratorParam* param = info.findParam(entry.id);
        if (param == nullptr || param->type != GeneratorParamType::Material) continue;
        if (const Status form = materialValueForm(info, *param, entry.value); !form.ok()) return form;
        const std::string& id = std::get<std::string>(entry.value);
        if (id.empty()) continue;
        if (materials == nullptr) {
            return fail(ErrorCode::NoLibrary, "The material of " + param->id + ": There is no library with materials.");
        }
        const MaterialForUse use = materialForUse(*materials, id);
        if (!use.status.ok()) return fail(use.status.code, "The material of " + param->id + ": " + use.status.message);
    }
    return {};
}

// Checks a catalog asset as the door or window an opening shows.
Status checkFrameAsset(const AssetCatalog& assets, std::string_view assetId, OpeningType type)
{
    const std::string id(assetId);
    if (type == OpeningType::Plain) {
        return fail(ErrorCode::InvalidValue, "A plain opening takes no door or window asset. Use --frame auto or generated.");
    }
    const std::optional<AssetRecord> record = assets.asset(assetId);
    if (!record) return fail(ErrorCode::NotFound, "There is no asset " + id + " in the library.");
    const std::string category(type == OpeningType::Door ? kDoorCategory : kWindowCategory);
    if (std::ranges::find(record->categories, category) == record->categories.end()) {
        return fail(ErrorCode::InvalidValue, "The asset " + id + " is not in the " + category +
                                                 " category, so it cannot fill a " + category + ".");
    }
    if (record->state == AssetState::Broken) {
        const std::string why = record->problem.empty() ? std::string("It is broken.") : record->problem;
        return fail(ErrorCode::InvalidValue, record->name + " cannot be fitted: " + why);
    }
    if (!isMeasured(record->size)) {
        return fail(ErrorCode::InvalidValue,
                    record->name + " is not measured yet, so it cannot be fitted. Retry its render first.");
    }
    return {};
}

}
