#pragma once

#include <plynth/core/SetupCheck.h>

#include <QString>
#include <QWidget>

#include <map>
#include <optional>
#include <stop_token>
#include <thread>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace plynth {
class Integration;
}

namespace plynth::app {

class LibraryHost;

// What the import buttons of the tab do. The window or the store glue fills it in, so the panel holds no
// queue code.
class IntegrationActions {
public:
    virtual ~IntegrationActions() = default;
    // Import the item at a pasted address as an asset or as a material.
    virtual void importAddress(const QString& integrationId, const QString& address, bool material) = 0;
    // Ask for files and import them as assets or as materials.
    virtual void importFiles(const QString& integrationId, bool material) = 0;
};

// The Integrations tab: one card per source of assets and materials, with its state, its saved options and
// its import buttons. It reads core's integration registry and saves options with core calls.
class IntegrationsPanel : public QWidget {
    Q_OBJECT
public:
    IntegrationsPanel(LibraryHost& host, QWidget* parent = nullptr);
    ~IntegrationsPanel() override;

    // Where the import buttons send their requests. Null turns the import buttons off.
    void setActions(IntegrationActions* actions);
    // Reads the saved options again and redraws the values.
    void refresh();
    // Runs the setup check on a worker thread and shows the states when it ends.
    void checkAgain();
    // Fills the BlenderKit address field and puts the focus there.
    void setAddress(const QString& text);
    void focusAddress();
    // True once a setup check was started and has ended. For checks only.
    bool checkSettled() const { return checkedOnce_ && !checking_; }

protected:
    void showEvent(QShowEvent* event) override;

private:
    struct Card {
        const plynth::Integration* integration = nullptr;
        QLabel* state = nullptr;
        QLabel* detail = nullptr;
        QPushButton* check = nullptr;
        QLineEdit* address = nullptr;
        QLabel* addressNote = nullptr;
        QLabel* optionNote = nullptr;
        QPushButton* importAsset = nullptr;
        QPushButton* importMaterial = nullptr;
        std::map<QString, QComboBox*> options; // by option key
    };

    void buildCard(const plynth::Integration& integration);
    QComboBox* addOption(Card& card, QVBoxLayout* box, const QString& key, const QString& label, const QString& tip);
    void saveOption(Card& card, const QString& key, const QString& value);
    void showReport(const plynth::SetupReport& report);
    void updateStates();
    void updateAddress(Card& card);
    void updateImportButtons();
    void fillLicenses(QComboBox* combo);

    LibraryHost& host_;
    IntegrationActions* actions_ = nullptr;
    QVBoxLayout* cards_ = nullptr;
    std::vector<Card> list_;
    std::optional<plynth::SetupReport> report_;
    bool checking_ = false;
    bool checkedOnce_ = false;
    bool refreshing_ = false;
    int generation_ = 0;
    std::stop_source stop_;
    std::jthread worker_;
};

}
