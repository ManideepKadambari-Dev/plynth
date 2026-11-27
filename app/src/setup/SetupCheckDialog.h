#pragma once

#include <plynth/core/BlenderDownload.h>
#include <plynth/core/SetupCheck.h>

#include <QDialog>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <optional>
#include <stop_token>
#include <thread>

class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace plynth::app {

class LibraryHost;
class UpdateController;

// The setup check screen (DESIGN.md section 7.19b). It shows what core's setup check found, one row per
// item, and lets the user fix each one: pick a Blender, choose or create the folders. Every change is saved
// at once and the check runs again on a worker thread.
class SetupCheckDialog : public QDialog {
    Q_OBJECT
public:
    explicit SetupCheckDialog(LibraryHost& host, QWidget* parent = nullptr, UpdateController* updates = nullptr);
    ~SetupCheckDialog() override;

    // Runs the check again on the worker thread.
    void recheck();
    // True when no check is running and a result is shown.
    // For checks only: scrolls the list until the row with this title is in view.
    void scrollToItem(const QString& title);
    bool checkFinished() const { return !checking_ && report_.has_value(); }
    // True when the shown result has no required item that blocks.
    bool ready() const { return report_.has_value() && report_->ready; }

    // For checks only: the dialogs opened from now on pretend no Blender was found and show "Get Blender for me"
    // in this made up state with a made up offer. Nothing is sent, downloaded or saved by it.
    static void useCannedDownload(const QString& name);
    // The made up states useCannedDownload takes.
    static QStringList cannedDownloadNames();

private:
    // The steps of "Get Blender for me".
    enum class GetStep { Idle, Looking, Question, Downloading, Failed, Stopped };

    void showReport(const plynth::SetupReport& report);
    void rebuildRows();
    void addRow(const plynth::SetupItem& item);
    void addDownloadPanel(QVBoxLayout* box, QWidget* row);
    void setGetStep(GetStep step);
    void lookForOffer();
    void offerFound(const plynth::BlenderOfferCheck& check);
    void startDownload();
    void stopDownload();
    void showDownloadProgress(const plynth::BlenderDownloadProgress& progress);
    void refreshProgress();
    void downloadFinished(const plynth::BlenderDownload& result);
    void showGetFailure(const plynth::Status& status, bool whileLooking);
    bool downloadBusy() const { return getStep_ == GetStep::Looking || getStep_ == GetStep::Downloading; }
    void addUpdateRow();
    void updateSummary();
    void applySettings(const std::function<void(plynth::MachineSettings&)>& change);
    void chooseFolder(bool library);
    void createFolder(bool library);
    void browseBlender();
    void finish();
    void openLink(const QString& link);

    LibraryHost& host_;
    UpdateController* updates_ = nullptr;
    std::optional<plynth::SetupReport> report_;
    bool checking_ = false;
    int generation_ = 0;
    std::stop_source stop_;
    std::jthread worker_;

    QString canned_;
    GetStep getStep_ = GetStep::Idle;
    std::optional<plynth::BlenderOffer> offer_;
    plynth::BlenderDownloadProgress progress_;
    QString getProblem_;
    QString getDetail_;
    QString getDone_;
    std::stop_source downloadStop_;
    std::jthread downloadWorker_;
    QPointer<QProgressBar> bar_;
    QPointer<QLabel> progressLabel_;

    QLabel* summary_ = nullptr;
    QLabel* note_ = nullptr;
    QLabel* problem_ = nullptr;
    QWidget* rowsHost_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QVBoxLayout* rows_ = nullptr;
    QPushButton* again_ = nullptr;
    QPushButton* done_ = nullptr;
};

}
