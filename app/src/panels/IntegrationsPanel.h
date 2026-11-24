#pragma once

#include <plynth/core/SetupCheck.h>

#include <QString>
#include <QWidget>

#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

class QComboBox;
class QLabel;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace plynth {
class Integration;
struct OptionField;
}

namespace plynth::app {

class LibraryHost;

// True for a source of the user's own files: it takes files and lists no store kinds.
bool isOwnFilesIntegration(const plynth::Integration& integration);
// True for the source that brings the Fab zip importer, which has its own check dialog.
bool isFabIntegration(const plynth::Integration& integration);

// What the import buttons of the window do. The window or the store glue fills it in, so the panel holds no
// queue code.
class IntegrationActions {
public:
    virtual ~IntegrationActions() = default;
    // Import the item at a pasted address as an asset or as a material.
    virtual void importAddress(const QString& integrationId, const QString& address, bool material) = 0;
    // Ask for files and import them as assets or as materials.
    virtual void importFiles(const QString& integrationId, bool material) = 0;
};

// The Sources window: one card per source of assets and materials, with its state, its saved options and
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
    // One saved option on a card: a drop down for choices and licenses, a number field for whole numbers.
    struct Field {
        QString key;
        bool license = false;
        QComboBox* combo = nullptr;
        QSpinBox* number = nullptr;
        int minimum = 0;
        int maximum = 0;
    };

    struct Card {
        const plynth::Integration* integration = nullptr;
        QLabel* state = nullptr;
        QLabel* detail = nullptr;
        QPushButton* check = nullptr;
        QLineEdit* address = nullptr;
        QLabel* addressNote = nullptr;
        bool addressOk = false; // the field holds text that core accepts
        QLabel* optionNote = nullptr;
        QPushButton* importAsset = nullptr;
        QPushButton* importMaterial = nullptr;
        std::vector<Field> fields; // in the order of optionFields
    };

    void buildCard(const plynth::Integration& integration);
    void addField(Card& card, QVBoxLayout* box, const plynth::OptionField& field);
    Card* cardFor(const QString& id);
    void saveOption(Card& card, const QString& key, const QString& valueJson);
    void showValue(Field& field, const QString& valueJson);
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
