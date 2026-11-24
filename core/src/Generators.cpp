#include <plynth/core/Generators.h>

#include <plynth/core/Addons.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetInfo.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxNameLength = 80;
constexpr std::size_t kMaxDescriptionLength = 400;
constexpr std::size_t kMaxLabelLength = 60;
constexpr std::size_t kMaxHelpLength = 200;

// Makes a refusal of a file.
GeneratorFile refused(std::string message, std::string where = {})
{
    GeneratorFile file;
    file.status = fail(ErrorCode::BadFile, std::move(message), std::move(where));
    return file;
}

// Removes spaces, tabs and line ends from both ends of text.
std::string_view trimmed(std::string_view text)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    return text;
}

// Lower cases ASCII text.
std::string lowered(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Finds the docstring: the text after its opening quotes up to its closing quotes.
bool findDocstring(std::string_view source, std::string_view& body)
{
    std::size_t pos = 0;
    if (source.substr(0, 3) == "\xEF\xBB\xBF") pos = 3;
    while (pos < source.size()) {
        std::size_t end = source.find('\n', pos);
        if (end == std::string_view::npos) end = source.size();
        const std::string_view line = trimmed(source.substr(pos, end - pos));
        if (line.empty() || line.front() == '#') {
            pos = end + 1;
            continue;
        }
        break;
    }
    if (pos >= source.size()) return false;
    const std::string_view rest = source.substr(pos);
    const std::string_view quote = rest.substr(0, 3);
    if (quote != "\"\"\"" && quote != "'''") return false;
    const std::string_view inside = rest.substr(3);
    const std::size_t close = inside.find(quote);
    if (close == std::string_view::npos) return false;
    body = inside.substr(0, close);
    return true;
}

// Says whether a JSON value is a finite number.
bool isFinite(const Json& json)
{
    return json.is_number() && std::isfinite(json.get<double>());
}

// Says whether a JSON value is a number with no fraction.
bool isWhole(const Json& json)
{
    if (!isFinite(json)) return false;
    const double value = json.get<double>();
    return value == std::floor(value);
}

// Refuses keys that are not in the list.
bool unknownKey(const Json& object, const std::set<std::string>& allowed, const std::string& pointer, GeneratorFile& out)
{
    for (const auto& [key, value] : object.items()) {
        if (allowed.count(key) == 0) {
            out = refused("The field \"" + key + "\" is not known.", pointer + "/" + key);
            return true;
        }
    }
    return false;
}

// Reads a string field with a length limit. False and out set when it is wrong.
bool readText(const Json& object, const char* key, const std::string& pointer, std::size_t maxLength, bool needed,
              std::string& value, GeneratorFile& out)
{
    const std::string where = pointer + "/" + key;
    const auto it = object.find(key);
    if (it == object.end()) {
        if (needed) {
            out = refused(std::string("The field \"") + key + "\" is missing.", where);
            return false;
        }
        return true;
    }
    if (!it->is_string()) {
        out = refused(std::string("The field \"") + key + "\" must be text.", where);
        return false;
    }
    value = it->get<std::string>();
    if (needed && value.empty()) {
        out = refused(std::string("The field \"") + key + "\" is empty.", where);
        return false;
    }
    if (value.size() > maxLength) {
        out = refused(std::string("The field \"") + key + "\" is longer than " + std::to_string(maxLength) +
                          " characters.",
                      where);
        return false;
    }
    return true;
}

// Reads an optional number field. False and out set when it is wrong.
bool readNumber(const Json& object, const char* key, const std::string& pointer, std::optional<double>& value,
                GeneratorFile& out)
{
    const auto it = object.find(key);
    if (it == object.end()) return true;
    if (!isFinite(*it)) {
        out = refused(std::string("The field \"") + key + "\" must be a number.", pointer + "/" + key);
        return false;
    }
    value = it->get<double>();
    return true;
}

// Reads the choices of a choice parameter. False and out set when they are wrong.
bool readChoices(const Json& json, const std::string& pointer, GeneratorParam& param, GeneratorFile& out)
{
    if (!json.contains("choices")) {
        if (param.type == GeneratorParamType::Choice) {
            out = refused("A choice needs its choices.", pointer + "/choices");
            return false;
        }
        return true;
    }
    if (param.type != GeneratorParamType::Choice) {
        out = refused("Only a choice has choices.", pointer + "/choices");
        return false;
    }
    const Json& list = json["choices"];
    if (!list.is_array() || list.empty()) {
        out = refused("The choices must be a list with at least one entry.", pointer + "/choices");
        return false;
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::string choicePointer = pointer + "/choices/" + std::to_string(i);
        if (!list[i].is_object()) {
            out = refused("A choice must be an object.", choicePointer);
            return false;
        }
        if (unknownKey(list[i], {"id", "label"}, choicePointer, out)) return false;
        GeneratorChoice choice;
        if (!readText(list[i], "id", choicePointer, 64, true, choice.id, out)) return false;
        if (!isGeneratorId(choice.id)) {
            out = refused("The choice id \"" + choice.id + "\" must use lower case letters, digits and \"_\".",
                          choicePointer + "/id");
            return false;
        }
        if (!readText(list[i], "label", choicePointer, kMaxLabelLength, true, choice.label, out)) return false;
        for (const GeneratorChoice& other : param.choices) {
            if (other.id == choice.id) {
                out = refused("The choice \"" + choice.id + "\" is used twice.", choicePointer + "/id");
                return false;
            }
        }
        param.choices.push_back(std::move(choice));
    }
    return true;
}

// Reads the default of a parameter. False and out set when it is wrong.
bool readDefault(const Json& json, const std::string& pointer, GeneratorParam& param, GeneratorFile& out)
{
    const std::string where = pointer + "/default";
    if (!json.contains("default")) {
        out = refused("The field \"default\" is missing.", where);
        return false;
    }
    const Json& value = json["default"];
    switch (param.type) {
    case GeneratorParamType::Number:
        if (value.is_string() && value.get<std::string>() == kCeilingHeightDefault) {
            if (param.unit != GeneratorUnit::Metres) {
                out = refused("\"ceiling_height\" is only for a number in metres.", where);
                return false;
            }
            param.defaultCeiling = true;
            param.defaultValue = 0.0;
            return true;
        }
        [[fallthrough]];
    case GeneratorParamType::Integer: {
        const bool good = param.type == GeneratorParamType::Integer ? isWhole(value) : isFinite(value);
        if (!good) {
            out = refused(param.type == GeneratorParamType::Integer ? "The default must be a whole number."
                                                                    : "The default must be a number.",
                          where);
            return false;
        }
        const double number = value.get<double>();
        if ((param.min && number < *param.min) || (param.max && number > *param.max)) {
            out = refused("The default is outside the minimum and maximum.", where);
            return false;
        }
        param.defaultValue = number;
        return true;
    }
    case GeneratorParamType::Boolean:
        if (!value.is_boolean()) {
            out = refused("The default must be true or false.", where);
            return false;
        }
        param.defaultValue = value.get<bool>();
        return true;
    case GeneratorParamType::Choice: {
        const std::string text = value.is_string() ? value.get<std::string>() : std::string();
        const bool known = std::any_of(param.choices.begin(), param.choices.end(),
                                       [&](const GeneratorChoice& c) { return c.id == text; });
        if (!known) {
            out = refused("The default is not one of the choices.", where);
            return false;
        }
        param.defaultValue = text;
        return true;
    }
    case GeneratorParamType::Material:
        if (value.is_null()) {
            param.defaultValue = std::string();
        } else if (value.is_string() && checkCatalogId(value.get<std::string>()).ok()) {
            param.defaultValue = value.get<std::string>();
        } else {
            out = refused("The default must be a materials catalog id or null.", where);
            return false;
        }
        return true;
    }
    return true;
}

// Reads one parameter. False and out set when it is wrong.
bool readParam(const Json& json, std::size_t index, GeneratorParam& param, GeneratorFile& out)
{
    const std::string pointer = "/params/" + std::to_string(index);
    if (!json.is_object()) {
        out = refused("A parameter must be an object.", pointer);
        return false;
    }
    if (unknownKey(json, {"id", "label", "help", "type", "unit", "default", "min", "max", "step", "choices"}, pointer,
                   out)) {
        return false;
    }
    if (!readText(json, "id", pointer, 64, true, param.id, out)) return false;
    if (!isGeneratorId(param.id)) {
        out = refused("The parameter id \"" + param.id + "\" must use lower case letters, digits and \"_\" and start with a letter.",
                      pointer + "/id");
        return false;
    }
    if (!readText(json, "label", pointer, kMaxLabelLength, true, param.label, out)) return false;
    if (!readText(json, "help", pointer, kMaxHelpLength, false, param.help, out)) return false;

    std::string typeName;
    if (!readText(json, "type", pointer, 32, true, typeName, out)) return false;
    const auto type = generatorParamTypeFromName(typeName);
    if (!type) {
        out = refused("The parameter type \"" + typeName + "\" is not known.", pointer + "/type");
        return false;
    }
    param.type = *type;
    const bool numeric = param.type == GeneratorParamType::Number || param.type == GeneratorParamType::Integer;

    if (json.contains("unit")) {
        if (param.type != GeneratorParamType::Number) {
            out = refused("Only a number has a unit.", pointer + "/unit");
            return false;
        }
        std::string unit;
        if (!readText(json, "unit", pointer, 8, true, unit, out)) return false;
        if (unit == "none") param.unit = GeneratorUnit::None;
        else if (unit == "m") param.unit = GeneratorUnit::Metres;
        else if (unit == "deg") param.unit = GeneratorUnit::Degrees;
        else {
            out = refused("The unit \"" + unit + "\" must be none, m or deg.", pointer + "/unit");
            return false;
        }
    }
    for (const char* key : {"min", "max", "step"}) {
        if (json.contains(key) && !numeric) {
            out = refused(std::string("Only a number or integer has \"") + key + "\".", pointer + "/" + key);
            return false;
        }
    }
    if (!readNumber(json, "min", pointer, param.min, out)) return false;
    if (!readNumber(json, "max", pointer, param.max, out)) return false;
    if (param.min && param.max && *param.min > *param.max) {
        out = refused("The minimum is above the maximum.", pointer + "/min");
        return false;
    }
    std::optional<double> step;
    if (!readNumber(json, "step", pointer, step, out)) return false;
    if (step) {
        if (!(*step > 0.0)) {
            out = refused("The step must be above zero.", pointer + "/step");
            return false;
        }
        param.step = *step;
    }
    return readChoices(json, pointer, param, out) && readDefault(json, pointer, param, out);
}

// Reads one side of the size. False and out set when it is wrong.
bool readLength(const Json& json, const char* key, const std::vector<GeneratorParam>& params, GeneratorLength& length,
                GeneratorFile& out)
{
    const std::string where = std::string("/size/") + key;
    if (!json.contains(key)) {
        out = refused(std::string("The size needs \"") + key + "\".", where);
        return false;
    }
    const Json& value = json[key];
    if (value.is_number()) {
        if (!isFinite(value) || !(value.get<double>() > 0.0)) {
            out = refused("A fixed size must be above zero.", where);
            return false;
        }
        length.value = value.get<double>();
        return true;
    }
    if (!value.is_object()) {
        out = refused("A size side must be a number or an object with \"param\".", where);
        return false;
    }
    if (unknownKey(value, {"param", "times", "plus"}, where, out)) return false;
    if (!value.contains("param") || !value["param"].is_string()) {
        out = refused("A size side needs \"param\" as text.", where + "/param");
        return false;
    }
    length.param = value["param"].get<std::string>();
    const auto it = std::find_if(params.begin(), params.end(), [&](const GeneratorParam& p) { return p.id == length.param; });
    if (it == params.end()) {
        out = refused("The size names the unknown parameter \"" + length.param + "\".", where + "/param");
        return false;
    }
    if (it->type != GeneratorParamType::Number && it->type != GeneratorParamType::Integer) {
        out = refused("The size names \"" + length.param + "\", which is not a number or integer.", where + "/param");
        return false;
    }
    std::optional<double> times;
    std::optional<double> plus;
    if (!readNumber(value, "times", where, times, out) || !readNumber(value, "plus", where, plus, out)) return false;
    length.times = times.value_or(1.0);
    length.plus = plus.value_or(0.0);
    return true;
}

// The reason a value does not fit one parameter, or empty when it does.
std::string valueProblem(const GeneratorParam& param, const GeneratorValue& value)
{
    const std::string name = "\"" + param.id + "\"";
    switch (param.type) {
    case GeneratorParamType::Number:
    case GeneratorParamType::Integer: {
        const double* number = std::get_if<double>(&value);
        if (!number || !std::isfinite(*number)) return "The value of " + name + " must be a number.";
        if (param.type == GeneratorParamType::Integer && *number != std::floor(*number)) {
            return "The value of " + name + " must be a whole number.";
        }
        if (param.min && *number < *param.min) return "The value of " + name + " is below " + generatorValueText(*param.min) + ".";
        if (param.max && *number > *param.max) return "The value of " + name + " is above " + generatorValueText(*param.max) + ".";
        return {};
    }
    case GeneratorParamType::Boolean:
        return std::holds_alternative<bool>(value) ? std::string() : "The value of " + name + " must be true or false.";
    case GeneratorParamType::Choice: {
        const std::string* text = std::get_if<std::string>(&value);
        const bool known = text && std::any_of(param.choices.begin(), param.choices.end(),
                                               [&](const GeneratorChoice& c) { return c.id == *text; });
        return known ? std::string() : "The value of " + name + " is not one of its choices.";
    }
    case GeneratorParamType::Material: {
        const std::string* text = std::get_if<std::string>(&value);
        if (!text) return "The value of " + name + " must be a material id or empty.";
        if (!text->empty() && !checkCatalogId(*text).ok()) return "The value of " + name + " is not a well formed material id.";
        return {};
    }
    }
    return {};
}

// The default of one parameter.
GeneratorValue defaultOf(const GeneratorParam& param, double ceilingHeight)
{
    if (!param.defaultCeiling) return param.defaultValue;
    double height = ceilingHeight;
    if (param.min) height = std::max(height, *param.min);
    if (param.max) height = std::min(height, *param.max);
    return height;
}

// The length of one side of the size for these values.
double lengthOf(const GeneratorLength& length, const std::vector<GeneratorParamValue>& params)
{
    if (length.param.empty()) return length.value;
    const GeneratorValue* value = findGeneratorValue(params, length.param);
    const double* number = value ? std::get_if<double>(value) : nullptr;
    return (number ? *number : 0.0) * length.times + length.plus;
}

// The names of add-ons for a sentence: "Archimesh", "A and B", "A, B and C".
std::string addonList(const std::vector<std::string>& names)
{
    std::string text;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) text += (i + 1 == names.size()) ? " and " : ", ";
        text += addonDisplayName(names[i]);
    }
    return text;
}

// Sorts values by id.
void sortById(std::vector<GeneratorParamValue>& values)
{
    std::sort(values.begin(), values.end(), [](const GeneratorParamValue& a, const GeneratorParamValue& b) { return a.id < b.id; });
}

}

// The name of a parameter type.
std::string_view generatorParamTypeName(GeneratorParamType type)
{
    switch (type) {
    case GeneratorParamType::Number: return "number";
    case GeneratorParamType::Integer: return "integer";
    case GeneratorParamType::Boolean: return "boolean";
    case GeneratorParamType::Choice: return "choice";
    case GeneratorParamType::Material: return "material";
    }
    return "number";
}

// The type with this name.
std::optional<GeneratorParamType> generatorParamTypeFromName(std::string_view name)
{
    for (const GeneratorParamType type : {GeneratorParamType::Number, GeneratorParamType::Integer, GeneratorParamType::Boolean,
                                          GeneratorParamType::Choice, GeneratorParamType::Material}) {
        if (generatorParamTypeName(type) == name) return type;
    }
    return std::nullopt;
}

// The name of a unit as written in the file.
std::string_view generatorUnitName(GeneratorUnit unit)
{
    switch (unit) {
    case GeneratorUnit::None: return "none";
    case GeneratorUnit::Metres: return "m";
    case GeneratorUnit::Degrees: return "deg";
    }
    return "none";
}

// The parameter with this id.
const GeneratorParam* GeneratorInfo::findParam(std::string_view paramId) const
{
    for (const GeneratorParam& p : params) {
        if (p.id == paramId) return &p;
    }
    return nullptr;
}

// Reads the declaration from a file's text.
GeneratorFile parseGeneratorSource(std::string_view source, std::string_view id)
{
    if (!isGeneratorId(id)) {
        return refused("The file name \"" + std::string(id) + "\" must use lower case letters, digits and \"_\" and start with a letter.");
    }
    std::string_view body;
    if (!findDocstring(source, body)) {
        return refused("The file has no docstring at its start. It must begin with \"\"\"plynth-generator.");
    }
    const std::size_t lineEnd = body.find('\n');
    const std::string_view firstLine = trimmed(body.substr(0, lineEnd));
    if (firstLine != kGeneratorMarker) {
        return refused("The docstring must start with the line \"plynth-generator\".");
    }
    const std::string_view jsonText = lineEnd == std::string_view::npos ? std::string_view() : body.substr(lineEnd + 1);
    const Json json = Json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) return refused("The declaration after the marker is not valid JSON.");

    GeneratorFile out;
    if (unknownKey(json, {"format", "version", "name", "description", "addons", "params", "size"}, {}, out)) return out;
    for (const char* key : {"format", "version", "name", "params", "size"}) {
        if (!json.contains(key)) return refused(std::string("The field \"") + key + "\" is missing.", std::string("/") + key);
    }
    if (!json["format"].is_string() || json["format"].get<std::string>() != kGeneratorFormatName) {
        return refused("The format must be \"plynth-generator\".", "/format");
    }
    if (!json["version"].is_number_integer() || json["version"].get<int>() != kGeneratorFormatVersion) {
        return refused("The version is not supported. This Plynth reads version 1.", "/version");
    }

    GeneratorInfo info;
    info.id = std::string(id);
    if (!readText(json, "name", {}, kMaxNameLength, true, info.name, out)) return out;
    if (!readText(json, "description", {}, kMaxDescriptionLength, false, info.description, out)) return out;

    if (json.contains("addons")) {
        const Json& list = json["addons"];
        if (!list.is_array()) return refused("The add-ons must be a list.", "/addons");
        for (std::size_t i = 0; i < list.size(); ++i) {
            const std::string where = "/addons/" + std::to_string(i);
            if (!list[i].is_string() || !isGeneratorId(list[i].get<std::string>())) {
                return refused("An add-on name must use lower case letters, digits and \"_\".", where);
            }
            const std::string name = list[i].get<std::string>();
            if (std::find(info.addons.begin(), info.addons.end(), name) != info.addons.end()) {
                return refused("The add-on \"" + name + "\" is listed twice.", where);
            }
            info.addons.push_back(name);
        }
        std::sort(info.addons.begin(), info.addons.end());
    }

    const Json& params = json["params"];
    if (!params.is_array() || params.empty()) return refused("There must be at least one parameter.", "/params");
    if (params.size() > kMaxGeneratorParams) {
        return refused("There are more than " + std::to_string(kMaxGeneratorParams) + " parameters.", "/params");
    }
    for (std::size_t i = 0; i < params.size(); ++i) {
        GeneratorParam param;
        if (!readParam(params[i], i, param, out)) return out;
        if (info.findParam(param.id)) {
            return refused("The parameter \"" + param.id + "\" is declared twice.", "/params/" + std::to_string(i) + "/id");
        }
        info.params.push_back(std::move(param));
    }

    const Json& size = json["size"];
    if (!size.is_object()) return refused("The size must be an object.", "/size");
    if (unknownKey(size, {"width", "depth", "height"}, "/size", out)) return out;
    if (!readLength(size, "width", info.params, info.size.width, out)) return out;
    if (!readLength(size, "depth", info.params, info.size.depth, out)) return out;
    if (!readLength(size, "height", info.params, info.size.height, out)) return out;

    GeneratorFile ok;
    ok.info = std::move(info);
    return ok;
}

// Reads a file and parses it.
GeneratorFile readGeneratorFile(const fs::path& file)
{
    GeneratorFile out;
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(file, ec);
    if (ec) {
        out.status = fail(ErrorCode::CannotRead, "The generator file " + pathToUtf8(file) + " could not be read.");
        return out;
    }
    if (size > kMaxGeneratorFileBytes) return refused("The generator file is larger than 1 MB.");
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        out.status = fail(ErrorCode::CannotRead, "The generator file " + pathToUtf8(file) + " could not be opened.");
        return out;
    }
    std::ostringstream text;
    text << in.rdbuf();
    const std::u8string stem = file.stem().u8string();
    out = parseGeneratorSource(text.str(), std::string(stem.begin(), stem.end()));
    if (out.status.ok()) out.info.file = fs::absolute(file, ec);
    return out;
}

// Says whether text is a well formed id.
bool isGeneratorId(std::string_view text)
{
    if (text.empty() || text.size() > 64) return false;
    if (text.front() < 'a' || text.front() > 'z') return false;
    return std::all_of(text.begin(), text.end(),
                       [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

// The generator with this id.
const GeneratorInfo* GeneratorLibrary::find(std::string_view generatorId) const
{
    for (const GeneratorInfo& g : generators) {
        if (g.id == generatorId) return &g;
    }
    return nullptr;
}

// Reads every generator file of a folder.
GeneratorLibrary loadGeneratorFolder(const fs::path& folder)
{
    GeneratorLibrary library;
    library.folder = folder;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return library;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path& path = it->path();
        if (lowered(path.extension().string()) != kGeneratorFileSuffix) continue;
        if (path.filename().string().rfind('_', 0) == 0) continue;
        files.push_back(path);
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });
    for (const fs::path& file : files) {
        GeneratorFile read = readGeneratorFile(file);
        if (read.status.ok()) library.generators.push_back(std::move(read.info));
        else library.refused.emplace_back(file, std::move(read.status));
    }
    std::sort(library.generators.begin(), library.generators.end(),
              [](const GeneratorInfo& a, const GeneratorInfo& b) { return a.id < b.id; });
    return library;
}

// The default values, sorted by id.
std::vector<GeneratorParamValue> defaultGeneratorParams(const GeneratorInfo& info, double ceilingHeight)
{
    std::vector<GeneratorParamValue> values;
    for (const GeneratorParam& p : info.params) values.push_back({p.id, defaultOf(p, ceilingHeight)});
    sortById(values);
    return values;
}

// Checks a full set of values.
Status checkGeneratorParams(const GeneratorInfo& info, const std::vector<GeneratorParamValue>& params)
{
    for (std::size_t i = 0; i < params.size(); ++i) {
        const GeneratorParamValue& v = params[i];
        if (i > 0 && params[i - 1].id == v.id) {
            return fail(ErrorCode::InvalidValue, "The parameter \"" + v.id + "\" is used twice.");
        }
        if (i > 0 && params[i - 1].id > v.id) {
            return fail(ErrorCode::InvalidValue, "The parameters must be sorted by id, but \"" + v.id +
                                                     "\" comes after \"" + params[i - 1].id + "\".");
        }
        const GeneratorParam* param = info.findParam(v.id);
        if (!param) {
            return fail(ErrorCode::InvalidValue, "The parameter \"" + v.id + "\" is not one of " + info.name + "'s parameters.");
        }
        const std::string problem = valueProblem(*param, v.value);
        if (!problem.empty()) return fail(ErrorCode::InvalidValue, problem);
    }
    for (const GeneratorParam& p : info.params) {
        if (!findGeneratorValue(params, p.id)) {
            return fail(ErrorCode::InvalidValue, "The parameter \"" + p.id + "\" has no value.");
        }
    }
    return {};
}

// Fits stored values to the current declaration.
GeneratorParamsFit fitGeneratorParams(const GeneratorInfo& info, const std::vector<GeneratorParamValue>& stored,
                                      double ceilingHeight)
{
    GeneratorParamsFit fit;
    for (const GeneratorParamValue& v : stored) {
        if (!info.findParam(v.id)) {
            fit.changed = true;
            fit.notes.push_back("The parameter \"" + v.id + "\" is no longer used and was dropped.");
        }
    }
    for (const GeneratorParam& p : info.params) {
        const GeneratorValue* value = findGeneratorValue(stored, p.id);
        if (value && valueProblem(p, *value).empty()) {
            fit.params.push_back({p.id, *value});
            continue;
        }
        fit.changed = true;
        fit.params.push_back({p.id, defaultOf(p, ceilingHeight)});
        const std::string text = generatorValueText(fit.params.back().value);
        fit.notes.push_back(value ? "The value of \"" + p.id + "\" no longer fits and was reset to " + text + "."
                                  : "The parameter \"" + p.id + "\" is new and was set to " + text + ".");
    }
    sortById(fit.params);
    return fit;
}

// Reads a value typed by a person.
GeneratorValueRead parseGeneratorValue(const GeneratorParam& param, std::string_view text)
{
    GeneratorValueRead read;
    const std::string_view value = trimmed(text);
    const std::string name = "\"" + param.id + "\"";
    const std::string typed = "\"" + std::string(value) + "\"";
    switch (param.type) {
    case GeneratorParamType::Number:
    case GeneratorParamType::Integer: {
        double number = 0.0;
        const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), number);
        if (value.empty() || ec != std::errc() || end != value.data() + value.size() || !std::isfinite(number)) {
            read.status = fail(ErrorCode::InvalidValue, "The value of " + name + " must be a number, not " + typed + ".");
        } else if (param.type == GeneratorParamType::Integer && number != std::floor(number)) {
            read.status = fail(ErrorCode::InvalidValue, "The value of " + name + " must be a whole number, not " + typed + ".");
        } else {
            read.value = number;
        }
        break;
    }
    case GeneratorParamType::Boolean: {
        const std::string word = lowered(std::string(value));
        if (word == "true" || word == "yes" || word == "1") read.value = true;
        else if (word == "false" || word == "no" || word == "0") read.value = false;
        else read.status = fail(ErrorCode::InvalidValue, "The value of " + name + " must be true or false, not " + typed + ".");
        break;
    }
    case GeneratorParamType::Choice: {
        const bool known = std::any_of(param.choices.begin(), param.choices.end(),
                                       [&](const GeneratorChoice& c) { return c.id == value; });
        if (known) read.value = std::string(value);
        else read.status = fail(ErrorCode::InvalidValue, "The value of " + name + " is not one of its choices, not " + typed + ".");
        break;
    }
    case GeneratorParamType::Material:
        if (lowered(std::string(value)) == "none") {
            read.value = std::string();
        } else if (checkCatalogId(value).ok()) {
            read.value = std::string(value);
        } else {
            read.status = fail(ErrorCode::InvalidValue, "The value of " + name + " must be a material id or none, not " + typed + ".");
        }
        break;
    }
    return read;
}

// A value as text.
std::string generatorValueText(const GeneratorValue& value)
{
    if (const double* number = std::get_if<double>(&value)) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.6f", *number);
        std::string text = buffer;
        if (text.find('.') != std::string::npos) {
            while (text.back() == '0') text.pop_back();
            if (text.back() == '.') text.pop_back();
        }
        if (text == "-0") text = "0";
        return text;
    }
    if (const bool* flag = std::get_if<bool>(&value)) return *flag ? "true" : "false";
    const std::string& text = std::get<std::string>(value);
    return text.empty() ? "none" : text;
}

// Sets one value in a full set.
Status setGeneratorParam(const GeneratorInfo& info, std::vector<GeneratorParamValue>& params, std::string_view paramId,
                         const GeneratorValue& value)
{
    const GeneratorParam* param = info.findParam(paramId);
    if (!param) {
        return fail(ErrorCode::InvalidValue,
                    "The parameter \"" + std::string(paramId) + "\" is not one of " + info.name + "'s parameters.");
    }
    const std::string problem = valueProblem(*param, value);
    if (!problem.empty()) return fail(ErrorCode::InvalidValue, problem);
    for (GeneratorParamValue& existing : params) {
        if (existing.id == paramId) {
            existing.value = value;
            return {};
        }
    }
    params.push_back({std::string(paramId), value});
    sortById(params);
    return {};
}

// The value with this id.
const GeneratorValue* findGeneratorValue(const std::vector<GeneratorParamValue>& params, std::string_view paramId)
{
    for (const GeneratorParamValue& v : params) {
        if (v.id == paramId) return &v.value;
    }
    return nullptr;
}

// The size a generator makes with these values.
GeneratorSize generatorSize(const GeneratorInfo& info, const std::vector<GeneratorParamValue>& params)
{
    GeneratorSize out;
    out.status = checkGeneratorParams(info, params);
    if (!out.status.ok()) return out;
    out.size.width = lengthOf(info.size.width, params);
    out.size.depth = lengthOf(info.size.depth, params);
    out.size.height = lengthOf(info.size.height, params);
    if (!(out.size.width > 0.0) || !(out.size.depth > 0.0) || !(out.size.height > 0.0)) {
        out.status = fail(ErrorCode::InvalidValue, "These values make a size that is not above zero.");
        out.size = {};
    }
    return out;
}

// The use and size an item needs.
GeneratorPlacement makeGeneratorUse(const GeneratorInfo& info, const std::vector<GeneratorParamValue>& params)
{
    GeneratorPlacement placement;
    const GeneratorSize size = generatorSize(info, params);
    placement.status = size.status;
    if (!placement.status.ok()) return placement;
    placement.use.name = info.id;
    placement.use.params = params;
    placement.size = size.size;
    return placement;
}

// The ids of the material parameters.
std::vector<std::string> generatorMaterialParams(const GeneratorInfo& info)
{
    std::vector<std::string> ids;
    for (const GeneratorParam& p : info.params) {
        if (p.type == GeneratorParamType::Material) ids.push_back(p.id);
    }
    return ids;
}

// The name used in answers.
std::string_view generatorAvailabilityName(GeneratorAvailability availability)
{
    switch (availability) {
    case GeneratorAvailability::Available: return "available";
    case GeneratorAvailability::NeedsAddon: return "needs_addon";
    case GeneratorAvailability::Unknown: return "unknown";
    }
    return "available";
}

// Works out whether a generator can run.
GeneratorState generatorState(const GeneratorInfo& info, const std::optional<AddonCache>& addons)
{
    GeneratorState state;
    if (info.addons.empty()) return state;
    if (!addons) {
        const bool one = info.addons.size() == 1;
        state.availability = GeneratorAvailability::Unknown;
        state.message = "Needs the " + addonList(info.addons) + (one ? " add-on" : " add-ons") +
                        ". Check the add-ons to see if " + (one ? "it is" : "they are") + " enabled.";
        return state;
    }
    for (const std::string& name : info.addons) {
        if (!addonEnabled(*addons, name)) state.missingAddons.push_back(name);
    }
    if (state.missingAddons.empty()) return state;
    std::sort(state.missingAddons.begin(), state.missingAddons.end());
    state.availability = GeneratorAvailability::NeedsAddon;
    const bool one = state.missingAddons.size() == 1;
    state.message = "Needs the " + addonList(state.missingAddons) + (one ? " add-on, which is" : " add-ons, which are") +
                    " not enabled in Blender.";
    return state;
}

// A lookup for the item checks.
GeneratorItemLookup generatorItemLookupFor(const GeneratorLibrary& library, const std::optional<AddonCache>& addons)
{
    return [&library, addons](const GeneratorUse& use) -> std::optional<GeneratorItemState> {
        const GeneratorInfo* info = library.find(use.name);
        if (!info) return std::nullopt;
        const GeneratorState state = generatorState(*info, addons);
        GeneratorItemState item;
        item.available = state.availability != GeneratorAvailability::NeedsAddon;
        if (!item.available) item.unavailableReason = state.message;
        item.valuesFit = checkGeneratorParams(*info, use.params).ok();
        const GeneratorSize size = generatorSize(*info, use.params);
        if (size.status.ok()) item.size = size.size;
        return item;
    };
}

// Copies the sample generators into a library.
SampleGeneratorsCopy addSampleGenerators(const fs::path& generatorsFolder, const fs::path& from)
{
    SampleGeneratorsCopy copy;
    std::error_code ec;
    if (!fs::is_directory(from, ec)) {
        copy.status = fail(ErrorCode::NotFound, "The sample generators folder " + pathToUtf8(from) + " was not found.");
        return copy;
    }
    fs::create_directories(generatorsFolder, ec);
    if (ec) {
        copy.status = fail(ErrorCode::CannotWrite, "The folder " + pathToUtf8(generatorsFolder) + " could not be created.");
        return copy;
    }
    std::vector<fs::path> files;
    for (fs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && lowered(it->path().extension().string()) == kGeneratorFileSuffix) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { return a.filename() < b.filename(); });
    for (const fs::path& file : files) {
        const fs::path target = generatorsFolder / file.filename();
        const std::string name = pathToUtf8(file.filename());
        if (fs::exists(target, ec)) {
            copy.skipped.push_back(name);
            continue;
        }
        if (!fs::copy_file(file, target, fs::copy_options::none, ec) || ec) {
            copy.status = fail(ErrorCode::CannotWrite,
                               "The file " + name + " could not be copied to " + pathToUtf8(generatorsFolder) + ".");
            return copy;
        }
        copy.copied.push_back(name);
    }
    return copy;
}

}
