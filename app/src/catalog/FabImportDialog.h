#pragma once

#include <plynth/core/Library.h>
#include <plynth/core/catalog/ImportPreview.h>

#include <QDialog>
#include <QString>
#include <QStringList>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

class QCheckBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace plynth::app {

// Shows what a Fab zip would import, one page per zip, before anything runs. Every page asks core for a dry
// run (previewAssetImport or previewMaterialImport) and draws the answer: the picked model and the other
// models to choose instead, the textures taken with a material and a channel for each, and the entries left
// out. A refusal shows its sentence and keeps Import off until the user has picked a model. Pick the model of
// a failed queue row uses it too, with that row's importer.
class FabImportDialog : public QDialog {
    Q_OBJECT
public:
    // What to import for one page the user kept.
    struct Pick {
        QString zip;
        nlohmann::ordered_json options; // the per import options: model and maps
    };

    // importer is the importer the dry run and the import name, kFabImporterId from the Fab card, or empty
    // to let core pick as it does for a file whose source was guessed.
    FabImportDialog(const plynth::LibraryPaths& library, const QStringList& zips, bool material, bool keepZips,
                    const std::string& importer, QWidget* parent = nullptr);

    // The pages that are ready and not left out.
    std::vector<Pick> picks() const;
    // The state of the Keep the zip check.
    bool keepZips() const;
    // True when the dialog is for materials.
    bool material() const { return material_; }
    // The importer the picks are for, empty for a guess.
    const std::string& importer() const { return importer_; }

private:
    // The material and channel the user set for one texture. material "*" is every material, an empty
    // channel is Automatic.
    struct TextureChoice {
        QString entry;
        QString material = QStringLiteral("*");
        QString channel;
    };

    struct Page {
        QString zip;
        nlohmann::ordered_json options = nlohmann::ordered_json::object();
        plynth::ImportPreview preview;
        QWidget* body = nullptr;
        QVBoxLayout* layout = nullptr;
        QCheckBox* leaveOut = nullptr;
        bool ready = false;
        std::vector<TextureChoice> textures;
    };

    void buildPage(int index);
    void fill(int index);
    void showPage(int index);
    void updateButtons();
    void chooseModel(int index, const QString& entry);
    void chooseChannel(int index, const QString& entry, const QString& channel);
    void chooseMaterial(int index, const QString& entry, const QString& material);
    TextureChoice& choiceFor(int index, const QString& entry);
    void writeMaps(int index);
    QStringList typedMaterials() const;

    plynth::LibraryPaths library_;
    bool material_ = false;
    std::string importer_;
    std::vector<Page> pages_;
    QStackedWidget* stack_ = nullptr;
    QLabel* where_ = nullptr;
    QPushButton* previous_ = nullptr;
    QPushButton* next_ = nullptr;
    QPushButton* import_ = nullptr;
    QCheckBox* keep_ = nullptr;
    int current_ = 0;
};

}
