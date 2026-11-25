#include "FabImportDialog.h"

#include "theme/ElideLabel.h"
#include "theme/PlainText.h"
#include "theme/Theme.h"
#include "theme/WheelGuard.h"

#include <plynth/core/catalog/FabZip.h>
#include <plynth/core/catalog/MaterialInfo.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace plynth::app {

namespace {

using Json = nlohmann::ordered_json;

// Turns core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Turns Qt text into core text.
std::string toCore(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

// A small muted caption.
QLabel* caption(const QString& text, QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setText(text);
    label->setFont(t.uiFont(t.sizeSmall()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    label->setWordWrap(true);
    return label;
}

// A section title.
QLabel* heading(const QString& text, QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setText(text);
    label->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    return label;
}

// A text field of the details, or an empty string.
QString textOf(const Json& json, const char* key)
{
    const auto found = json.find(key);
    return found != json.end() && found->is_string() ? fromCore(found->get<std::string>()) : QString();
}

// The words next to a model entry: its quality and format.
QString modelLabel(const Json& model)
{
    QStringList words;
    const QString quality = textOf(model, "quality");
    if (!quality.isEmpty())
        words << quality;
    const QString format = textOf(model, "format");
    if (!format.isEmpty())
        words << format.toUpper();
    const QString entry = textOf(model, "entry");
    return words.isEmpty() ? entry : QStringLiteral("%1  (%2)").arg(entry, words.join(QStringLiteral(", ")));
}

}

// Builds the pages, the navigation and the buttons.
FabImportDialog::FabImportDialog(const plynth::LibraryPaths& library, const QStringList& zips, bool material,
                                 bool keepZips, const std::string& importer, QWidget* parent)
    : QDialog(parent), library_(library), material_(material), importer_(importer)
{
    const Theme& t = Theme::instance();
    if (importer_ == plynth::kFabImporterId)
        setWindowTitle(material ? tr("Import Fab material zips") : tr("Import Fab model zips"));
    else
        setWindowTitle(tr("Pick the model"));
    setModal(true);
    resize(t.unit() * 140, t.unit() * 100);
    setMinimumSize(t.unit() * 80, t.unit() * 60);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(t.dialogMargin(), t.dialogMargin(), t.dialogMargin(), t.dialogMargin());
    layout->setSpacing(t.unit() * 2);

    auto* nav = new QHBoxLayout();
    nav->setSpacing(t.unit() * 2);
    where_ = new QLabel(this);
    where_->setTextFormat(Qt::PlainText);
    where_->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    where_->setToolTip(tr("Which zip is shown"));
    nav->addWidget(where_, 1);
    previous_ = new QPushButton(tr("Previous"), this);
    previous_->setToolTip(tr("Show the previous zip"));
    next_ = new QPushButton(tr("Next"), this);
    next_->setToolTip(tr("Show the next zip"));
    nav->addWidget(previous_);
    nav->addWidget(next_);
    layout->addLayout(nav);

    stack_ = new QStackedWidget(this);
    layout->addWidget(stack_, 1);

    keep_ = new QCheckBox(tr("Keep the zip files after importing"), this);
    keep_->setStyle(t.controlStyle());
    keep_->setChecked(keepZips);
    keep_->setToolTip(tr("On: the zips stay where they are. Off: they go to the Recycle Bin after a good import."));
    const bool anyZip = std::any_of(zips.begin(), zips.end(),
                                    [](const QString& zip) { return zip.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive); });
    keep_->setVisible(anyZip);
    layout->addWidget(keep_);

    auto* buttons = new QDialogButtonBox(this);
    import_ = buttons->addButton(tr("Import"), QDialogButtonBox::AcceptRole);
    QPushButton* cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    import_->setDefault(true);
    import_->setToolTip(tr("Add the zips that are ready to the import queue"));
    cancel->setToolTip(tr("Close without importing anything"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(previous_, &QPushButton::clicked, this, [this]() { showPage(current_ - 1); });
    connect(next_, &QPushButton::clicked, this, [this]() { showPage(current_ + 1); });

    pages_.resize(static_cast<size_t>(zips.size()));
    for (int i = 0; i < zips.size(); ++i) {
        pages_[static_cast<size_t>(i)].zip = zips[i];
        buildPage(i);
        fill(i);
    }
    showPage(0);
}

// Makes the empty page of one zip: its name, the leave out check and a scrolling body.
void FabImportDialog::buildPage(int index)
{
    const Theme& t = Theme::instance();
    Page& page = pages_[static_cast<size_t>(index)];
    auto* widget = new QWidget(stack_);
    auto* box = new QVBoxLayout(widget);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(t.unit() * 2);

    auto* top = new QHBoxLayout();
    top->setSpacing(t.unit() * 2);
    auto* name = new ElideLabel(widget);
    name->setTextFormat(Qt::PlainText);
    name->setText(QFileInfo(page.zip).fileName());
    name->setFont(t.monoFont(t.sizeNormal()));
    name->setToolTip(plainToolTip(page.zip));
    top->addWidget(name, 1);
    page.leaveOut = new QCheckBox(tr("Leave this zip out"), widget);
    page.leaveOut->setStyle(t.controlStyle());
    page.leaveOut->setToolTip(tr("Do not import this zip"));
    connect(page.leaveOut, &QCheckBox::toggled, this, [this]() { updateButtons(); });
    top->addWidget(page.leaveOut);
    box->addLayout(top);

    auto* scroll = new QScrollArea(widget);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    page.body = new QWidget(scroll);
    page.layout = new QVBoxLayout(page.body);
    page.layout->setContentsMargins(0, 0, t.unit() * 2, 0);
    page.layout->setSpacing(t.unit() * 2);
    scroll->setWidget(page.body);
    box->addWidget(scroll, 1);
    stack_->addWidget(widget);
}

// Asks core what the zip would import and draws the answer.
void FabImportDialog::fill(int index)
{
    const Theme& t = Theme::instance();
    Page& page = pages_[static_cast<size_t>(index)];
    while (QLayoutItem* item = page.layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    const std::filesystem::path file(page.zip.toStdWString());
    if (material_) {
        plynth::MaterialImportRequest request;
        request.source.file = file;
        request.importer = importer_;
        request.importerOptions = page.options;
        page.preview = plynth::previewMaterialImport(library_, request);
    } else {
        plynth::ImportRequest request;
        request.source.file = file;
        request.importer = importer_;
        request.importerOptions = page.options;
        page.preview = plynth::previewAssetImport(library_, request);
    }
    page.ready = page.preview.status.ok();

    if (!page.ready) {
        auto* why = new QLabel(page.body);
        why->setTextFormat(Qt::PlainText);
        why->setText(fromCore(page.preview.status.message));
        why->setWordWrap(true);
        why->setFont(t.uiFont(t.sizeNormal()));
        why->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
        why->setToolTip(tr("Why this zip cannot be imported yet"));
        page.layout->addWidget(why);
        if (!material_) {
            const Json models = page.preview.plan.value("details", Json::object()).value("models", Json::array());
            if (models.size() > 1) {
                page.layout->addWidget(heading(tr("Model"), page.body));
                auto* combo = new QComboBox(page.body);
                combo->setStyle(t.controlStyle());
                combo->setFont(t.monoFont(t.sizeSmall()));
                combo->addItem(tr("Pick a model..."), QString());
                for (const Json& model : models)
                    combo->addItem(modelLabel(model), textOf(model, "entry"));
                combo->setToolTip(tr("The file holds several models. Choose the one to import."));
                WheelGuard::guard(combo);
                connect(combo, &QComboBox::activated, this, [this, index, combo](int at) {
                    const QString entry = combo->itemData(at).toString();
                    if (!entry.isEmpty())
                        chooseModel(index, entry);
                });
                page.layout->addWidget(combo);
            }
        }
        page.layout->addStretch(1);
        updateButtons();
        return;
    }

    const Json details = page.preview.plan.value("details", Json::object());

    if (!material_) {
        page.layout->addWidget(heading(tr("Model"), page.body));
        const QString picked = textOf(details, "picked");
        auto* combo = new QComboBox(page.body);
        combo->setStyle(t.controlStyle());
        combo->setFont(t.monoFont(t.sizeSmall()));
        int current = 0;
        const Json models = details.value("models", Json::array());
        for (const Json& model : models) {
            combo->addItem(modelLabel(model), textOf(model, "entry"));
            if (textOf(model, "entry") == picked)
                current = combo->count() - 1;
        }
        combo->setCurrentIndex(current);
        combo->setToolTip(tr("The model Plynth takes. Choose another version of the model to use it instead."));
        WheelGuard::guard(combo);
        connect(combo, &QComboBox::activated, this, [this, index, combo](int at) {
            chooseModel(index, combo->itemData(at).toString());
        });
        page.layout->addWidget(combo);
    }

    const Json textures = details.value("textures", Json::array());
    page.layout->addWidget(heading(tr("Textures"), page.body));
    if (details.contains("texture_resolution") && details["texture_resolution"].is_number_integer()) {
        page.layout->addWidget(caption(tr("Taken at up to %1 pixels across.").arg(details["texture_resolution"].get<int>()),
                                       page.body));
    }
    if (textures.empty())
        page.layout->addWidget(caption(tr("No textures were found. The materials keep the colors of the model."), page.body));
    else if (!material_)
        page.layout->addWidget(caption(tr("A texture can go to one material by its name. The names are in the Materials "
                                          "list of the imported asset, so keep the zip to import it again with them."),
                                       page.body));
    const QStringList typed = typedMaterials();
    for (const Json& texture : textures) {
        const QString entry = fromCore(texture.is_string() ? texture.get<std::string>() : std::string());
        TextureChoice shown{entry};
        for (const TextureChoice& choice : page.textures)
            if (choice.entry == entry)
                shown = choice;
        auto* wrap = new QWidget(page.body);
        auto* row = new QHBoxLayout(wrap);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(t.unit() * 2);
        auto* name = new ElideLabel(wrap);
        name->setTextFormat(Qt::PlainText);
        name->setText(entry);
        name->setFont(t.monoFont(t.sizeSmall()));
        name->setToolTip(plainToolTip(entry));
        row->addWidget(name, 1);
        if (!material_) {
            auto* which = new QComboBox(wrap);
            which->setStyle(t.controlStyle());
            which->setFont(t.uiFont(t.sizeSmall()));
            which->setEditable(true);
            which->setInsertPolicy(QComboBox::NoInsert);
            which->addItem(tr("Every material"), QStringLiteral("*"));
            for (const QString& typedName : typed)
                which->addItem(typedName, typedName);
            if (shown.material == QStringLiteral("*"))
                which->setCurrentIndex(0);
            else
                which->setCurrentText(shown.material);
            which->setToolTip(tr("The material this texture is for. Type a material name of the model, or keep Every material."));
            WheelGuard::guard(which);
            connect(which, &QComboBox::activated, this, [this, index, entry, which](int at) {
                chooseMaterial(index, entry, which->itemData(at).toString());
            });
            connect(which->lineEdit(), &QLineEdit::editingFinished, this, [this, index, entry, which]() {
                chooseMaterial(index, entry, which->currentText());
            });
            row->addWidget(which);
        }
        auto* combo = new QComboBox(wrap);
        combo->setStyle(t.controlStyle());
        combo->setFont(t.uiFont(t.sizeSmall()));
        combo->addItem(tr("Automatic"), QString());
        for (const plynth::MapChannel channel : plynth::allMapChannels()) {
            const QString channelName = fromCore(std::string(plynth::mapChannelName(channel)));
            combo->addItem(channelName, channelName);
        }
        combo->setCurrentIndex(std::max(0, combo->findData(shown.channel)));
        combo->setToolTip(tr("Which channel this texture is for. Automatic reads it from the file name. "
                             "Each material takes one texture for each channel."));
        WheelGuard::guard(combo);
        connect(combo, &QComboBox::activated, this, [this, index, entry, combo](int at) {
            chooseChannel(index, entry, combo->itemData(at).toString());
        });
        row->addWidget(combo);
        page.layout->addWidget(wrap);
    }

    const Json skipped = details.value("skipped", Json::array());
    if (!skipped.empty()) {
        page.layout->addWidget(heading(tr("Left out"), page.body));
        for (const Json& item : skipped) {
            const QString line = QStringLiteral("%1 - %2").arg(textOf(item, "entry"), textOf(item, "reason"));
            auto* label = caption(line, page.body);
            label->setFont(t.monoFont(t.sizeSmall()));
            label->setToolTip(tr("An entry of the zip Plynth does not use, and why"));
            page.layout->addWidget(label);
        }
    }
    page.layout->addStretch(1);
    updateButtons();
}

// Shows one page and the navigation for it.
void FabImportDialog::showPage(int index)
{
    if (pages_.empty())
        return;
    current_ = std::clamp(index, 0, static_cast<int>(pages_.size()) - 1);
    stack_->setCurrentIndex(current_);
    updateButtons();
}

// Sets the position text, the navigation buttons and whether Import can be pressed.
void FabImportDialog::updateButtons()
{
    const int count = static_cast<int>(pages_.size());
    where_->setText(count > 1 ? tr("Zip %1 of %2").arg(current_ + 1).arg(count) : tr("One zip"));
    previous_->setVisible(count > 1);
    next_->setVisible(count > 1);
    previous_->setEnabled(current_ > 0);
    next_->setEnabled(current_ + 1 < count);
    int included = 0;
    bool blocked = false;
    for (const Page& page : pages_) {
        if (page.leaveOut != nullptr && page.leaveOut->isChecked())
            continue;
        ++included;
        if (!page.ready)
            blocked = true;
    }
    import_->setEnabled(included > 0 && !blocked);
}

// Takes the model the user chose and asks core again.
void FabImportDialog::chooseModel(int index, const QString& entry)
{
    Page& page = pages_[static_cast<size_t>(index)];
    page.options["model"] = toCore(entry);
    fill(index);
}

// The choice of one texture on a page, Automatic for every material when it is new.
FabImportDialog::TextureChoice& FabImportDialog::choiceFor(int index, const QString& entry)
{
    auto& list = pages_[static_cast<size_t>(index)].textures;
    for (TextureChoice& choice : list)
        if (choice.entry == entry)
            return choice;
    list.push_back(TextureChoice{entry});
    return list.back();
}

// Takes the channel the user set for a texture. An older texture on the same material and channel goes back
// to Automatic, so each material gets one texture for each channel. Then asks core again.
void FabImportDialog::chooseChannel(int index, const QString& entry, const QString& channel)
{
    TextureChoice& chosen = choiceFor(index, entry);
    if (chosen.channel == channel)
        return;
    chosen.channel = channel;
    if (!channel.isEmpty())
        for (TextureChoice& other : pages_[static_cast<size_t>(index)].textures)
            if (other.entry != entry && other.material == chosen.material && other.channel == channel)
                other.channel.clear();
    writeMaps(index);
    fill(index);
}

// Takes the material the user typed or picked for a texture, with the same one texture rule as a channel.
void FabImportDialog::chooseMaterial(int index, const QString& entry, const QString& material)
{
    QString name = material.trimmed();
    if (name.isEmpty() || name == tr("Every material"))
        name = QStringLiteral("*");
    TextureChoice& chosen = choiceFor(index, entry);
    if (chosen.material == name)
        return;
    chosen.material = name;
    // Nothing changes in the import until the texture also has a channel.
    if (chosen.channel.isEmpty())
        return;
    for (TextureChoice& other : pages_[static_cast<size_t>(index)].textures)
        if (other.entry != entry && other.material == name && other.channel == chosen.channel)
            other.channel.clear();
    writeMaps(index);
    fill(index);
}

// Puts the texture choices of a page into its maps option: {"<material or *>": {"<channel>": "<entry>"}}.
void FabImportDialog::writeMaps(int index)
{
    Page& page = pages_[static_cast<size_t>(index)];
    page.options.erase("maps");
    Json maps = Json::object();
    for (const TextureChoice& choice : page.textures)
        if (!choice.channel.isEmpty())
            maps[toCore(choice.material)][toCore(choice.channel)] = toCore(choice.entry);
    if (!maps.empty())
        page.options["maps"] = maps;
}

// The material names typed so far on every page, for the material fields to offer.
QStringList FabImportDialog::typedMaterials() const
{
    QStringList names;
    for (const Page& page : pages_)
        for (const TextureChoice& choice : page.textures)
            if (choice.material != QStringLiteral("*") && !names.contains(choice.material))
                names << choice.material;
    names.sort(Qt::CaseInsensitive);
    return names;
}

// The pages that are ready and not left out.
std::vector<FabImportDialog::Pick> FabImportDialog::picks() const
{
    std::vector<Pick> out;
    for (const Page& page : pages_)
        if (page.ready && !page.leaveOut->isChecked())
            out.push_back({page.zip, page.options});
    return out;
}

// The state of the Keep the zip check.
bool FabImportDialog::keepZips() const
{
    return keep_->isChecked();
}

}
