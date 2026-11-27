#include "FileIo.h"

#include "plynth/core/EngineLink.h"
#include "plynth/core/ProjectFiles.h"
#include "plynth/core/UnrealLink.h"
#include "plynth/core/UnrealRemote.h"

#include <fstream>
#include <memory>
#include <string>

namespace plynth {

namespace fs = std::filesystem;

namespace {

// How long the editor may take to import one building before the run counts as failed.
constexpr std::chrono::seconds kUnrealDeliveryTimeout{30 * 60};
// How many of the editor's last output lines a failure message quotes.
constexpr std::size_t kQuotedOutputLines = 3;

// The last few output lines of the editor, for a message when the script gave no usable answer.
std::string lastLines(const std::vector<std::string>& output)
{
    std::string out;
    const std::size_t from = output.size() > kQuotedOutputLines ? output.size() - kQuotedOutputLines : 0;
    for (std::size_t i = from; i < output.size(); ++i) {
        if (!out.empty()) out += " ";
        out += output[i];
    }
    return out.empty() ? std::string("It printed nothing.") : "Its last output: " + out;
}

// The Unreal Engine link: finds editors through remote execution and runs the Plynth script inside one.
class UnrealLink final : public EngineLink {
public:
    std::string_view id() const override { return kUnrealLinkId; }
    std::string_view name() const override { return "Unreal Engine"; }
    std::string_view description() const override
    {
        return "Puts the building into the project open in Unreal Engine and updates it after every build.";
    }
    ExportProfile profile() const override { return ExportProfile::Unreal; }

    // Pings for running Unreal editors and lists those that answered.
    EngineEditorSearch findEditors(std::chrono::milliseconds wait, std::stop_token stop) const override
    {
        EngineEditorSearch search;
        const UnrealEndpointRead endpoint = unrealEndpoint();
        if (!endpoint.status.ok()) {
            search.status = endpoint.status;
            return search;
        }
        const UnrealDiscovery found = findUnrealEditors(endpoint.endpoint, wait, stop);
        search.status = found.status;
        for (const UnrealEditorNode& node : found.editors) {
            EngineEditor editor;
            editor.id = node.nodeId;
            editor.target.projectName = node.projectName;
            editor.target.projectRoot = node.projectRoot;
            editor.target.engineVersion = node.engineVersion;
            editor.user = node.user;
            editor.machine = node.machine;
            search.editors.push_back(std::move(editor));
        }
        return search;
    }

    // Writes the Unreal job, runs the Plynth script in the editor and reads what it did.
    Status deliver(const EngineDelivery& delivery, EngineLinkReport& report,
                   const std::function<void(const EngineLinkProgress&)>& onProgress, std::stop_token stop) const override
    {
        const auto progress = [&](std::string message) {
            if (!onProgress) return;
            EngineLinkProgress p;
            p.stage = EngineLinkStage::Delivering;
            p.message = std::move(message);
            onProgress(p);
        };
        const fs::path scripts = unrealScriptsFolder();
        if (scripts.empty()) {
            return fail(ErrorCode::NotFound, "The Plynth scripts for Unreal Engine were not found. Install Plynth again.");
        }
        const UnrealEndpointRead endpoint = unrealEndpoint();
        if (!endpoint.status.ok()) return endpoint.status;

        std::error_code ec;
        fs::create_directories(delivery.workFolder, ec);
        const fs::path jobFile = delivery.workFolder / pathFromUtf8(kUnrealJobFileName);
        const fs::path resultFile = delivery.workFolder / pathFromUtf8(kUnrealResultFileName);
        fs::remove(resultFile, ec);

        UnrealJobChecks checks;
        checks.enabled = delivery.selfCheck;
        if (delivery.selfCheck) {
            checks.expectedBounds = expectedWallBounds(delivery.project);
            checks.screenshot = delivery.screenshot;
        }
        const Json job = makeUnrealJob(delivery, checks, resultFile);
        report.contentPath = job["content_path"].get<std::string>();
        if (job["meshes"].empty()) {
            return fail(ErrorCode::InvalidValue, "The export wrote no building file to send to Unreal Engine.");
        }
        if (const Status written = fileio::writeAtomic(jobFile, job.dump(2) + "\n"); !written.ok()) return written;

        UnrealEditorNode node;
        node.nodeId = delivery.editor.id;
        node.projectName = delivery.editor.target.projectName;
        node.projectRoot = delivery.editor.target.projectRoot;
        node.engineVersion = delivery.editor.target.engineVersion;
        node.user = delivery.editor.user;
        node.machine = delivery.editor.machine;
        const std::string command = "\"" + pathToUtf8(scripts / pathFromUtf8(kUnrealLinkScript)) + "\" \"" + pathToUtf8(jobFile) + "\"";
        progress("Importing the building into " + (node.projectName.empty() ? std::string("Unreal Engine") : node.projectName) + ".");
        const UnrealCommandResult ran = runUnrealCommand(endpoint.endpoint, node, command, UnrealExecMode::ExecuteFile,
                                                         kUnrealDeliveryTimeout, stop);

        report.logFile = delivery.workFolder / L"editor.log";
        std::string log;
        for (const std::string& line : ran.output) log += line + "\n";
        if (!ran.result.empty()) log += "Result: " + ran.result + "\n";
        fileio::writeAtomic(report.logFile, log);
        if (!ran.status.ok()) return ran.status;

        std::ifstream in(resultFile, std::ios::binary);
        if (!in) {
            return fail(ErrorCode::EngineFailed, "Unreal Engine ran the Plynth script, but it wrote no result. " + lastLines(ran.output));
        }
        const Json answer = Json::parse(in, nullptr, false);
        if (answer.is_discarded()) {
            return fail(ErrorCode::EngineFailed, "The result of the Plynth script in Unreal Engine is not JSON. " + lastLines(ran.output));
        }
        const UnrealResult result = readUnrealResult(answer, delivery.jobId);
        if (!result.status.ok()) {
            return fail(ErrorCode::EngineFailed, result.status.message + " " + lastLines(ran.output));
        }
        if (!result.contentPath.empty()) report.contentPath = result.contentPath;
        report.assets = result.assets;
        report.actors = result.actors;
        report.checks.clear();
        for (const UnrealCheck& c : result.checks) report.checks.push_back({c.id, c.status, c.message});
        report.screenshot = result.screenshot;
        if (!result.ok) {
            return fail(ErrorCode::EngineFailed, result.errorMessage.empty() ? "The import in Unreal Engine failed." : result.errorMessage);
        }
        return {};
    }
};

const EngineLinkRegistrar registrar(std::make_unique<UnrealLink>());

}

}
