#include "FabImportDialog.h"

#include "theme/ElideLabel.h"
#include "theme/Theme.h"

#include <plynth/core/catalog/FabZip.h>
#include <plynth/core/catalog/MaterialInfo.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace plynth::app {

namespace {

using Json = nlohmann::ordered_json;

QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string toCore(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

// A small muted caption.
QLabel* caption(const QString& text, QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(text, parent);
    label->setFont(t.uiFont(t.sizeSmall()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

// A section title.
QLabel* heading(const QString& text, QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(text, parent);
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
                                 bool keepZips, QWidget* parent)
    : QDialog(parent), library_(library), material_(material)
{
    const Theme& t = Theme::instance();
    setWindowTitle(material ? tr("Import Fab material zips") : tr("Import Fab model zips"));
    setModal(true);
    setProperty("tourId", QStringLiteral("fab-import-dialog"));
    setToolTip(tr("Check what Plynth takes from each Fab zip before it imports them"));
    resize(t.unit() * 140, t.unit() * 100);
    setMinimumSize(t.unit() * 80, t.unit() * 60);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(t.dialogMargin(), t.dialogMargin(), t.dialogMargin(), t.dialogMargin());
    layout->setSpacing(t.unit() * 2);

    auto* nav = new QHBoxLayout();
    nav->setSpacing(t.unit() * 2);
    where_ = new QLabel(this);
    where_->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    where_->setToolTip(tr("Which zip is shown"));
    nav->addWidget(where_, 1);
    previous_ = new QPushButton(tr("Previous"), this);
    previous_->setToolTip(tr("Show the previous zip"));
    previous_->setProperty("tourId", QStringLiteral("fab-previous"));
    next_ = new QPushButton(tr("Next"), this);
    next_->setToolTip(tr("Show the next zip"));
    next_->setProperty("tourId", QStringLiteral("fab-next"));
    nav->addWidget(previous_);
    nav->addWidget(next_);
    layout->addLayout(nav);

    stack_ = new QStackedWidget(this);
    layout->addWidget(stack_, 1);

    keep_ = new QCheckBox(tr("Keep the zip files after importing"), this);
    keep_->setStyle(t.controlStyle());
    keep_->setChecked(keepZips);
    keep_->setToolTip(tr("On: the zips stay where they are. Off: they go to the Recycle Bin after a good import."));
    keep_->setProperty("tourId", QStringLiteral("fab-keep-zip"));
    layout->addWidget(keep_);

    auto* buttons = new QDialogButtonBox(this);
    import_ = buttons->addButton(tr("Import"), QDialogButtonBox::AcceptRole);
    QPushButton* cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    import_->setDefault(true);
    import_->setToolTip(tr("Add the zips that are ready to the import queue"));
    import_->setProperty("tourId", QStringLiteral("fab-import"));
    cancel->setToolTip(tr("Close without importing anything"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(previous_, &QPushButton::clicked, this, [this]() { showPage(current_ - 1); });
    connect(next_, &QPushButton::clicked, this, [this]() { showPage(current_ + 1); });

    pages_.resize(static_cast<size_t>(zips.size()));
    channels_.resize(pages_.size());
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
    auto* name = new ElideLabel(QFileInfo(page.zip).fileName(), widget);
    name->setFont(t.monoFont(t.sizeNormal()));
    name->setToolTip(page.zip);
    top->addWidget(name, 1);
    page.leaveOut = new QCheckBox(tr("Leave this zip out"), widget);
    page.leaveOut->setStyle(t.controlStyle());
    page.leaveOut->setToolTip(tr("Do not import this zip"));
    page.leaveOut->setProperty("tourId", QStringLiteral("fab-leave-out"));
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
        request.importer = std::string(plynth::kFabImporterId);
        request.importerOptions = page.options;
        page.preview = plynth::previewMaterialImport(library_, request);
    } else {
        plynth::ImportRequest request;
        request.source.file = file;
        request.importer = std::string(plynth::kFabImporterId);
        request.importerOptions = page.options;
        page.preview = plynth::previewAssetImport(library_, request);
    }
    page.ready = page.preview.status.ok();

    if (!page.ready) {
        auto* why = new QLabel(fromCore(page.preview.status.message), page.body);
        why->setTextFormat(Qt::PlainText);
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
                combo->setToolTip(tr("The zip holds several models. Choose the one to import."));
                combo->setProperty("tourId", QStringLiteral("fab-model"));
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
        combo->setProperty("tourId", QStringLiteral("fab-model"));
        connect(combo, &QComboBox::activated, this, [this, index, combo](int at) {
            chooseModel(index, combo->itemData(at).toString());
        });
        page.layout->addWidget(combo);
    }

    const Json textures = details.value("textures", Json::array());
    page.layout->addWidget(heading(tr("Textures"), page.body));
    if (details.contains("texture_resolution") && details["texture_resolution"].is_number_integer()) {
        page.layout->addWidget(caption(tr("Taken at %1 pixels across.").arg(details["texture_resolution"].get<int>()),
                                       page.body));
    }
    if (textures.empty())
        page.layout->addWidget(caption(tr("No textures were found. The materials keep the colors of the model."), page.body));
    for (const Json& texture : textures) {
        const QString entry = fromCore(texture.is_string() ? texture.get<std::string>() : std::string());
        auto* wrap = new QWidget(page.body);
        auto* row = new QHBoxLayout(wrap);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(t.unit() * 2);
        auto* name = new ElideLabel(entry, wrap);
        name->setFont(t.monoFont(t.sizeSmall()));
        name->setToolTip(entry);
        row->addWidget(name, 1);
        auto* combo = new QComboBox(wrap);
        combo->setStyle(t.controlStyle());
        combo->setFont(t.uiFont(t.sizeSmall()));
        combo->addItem(tr("Automatic"), QString());
        for (const plynth::MapChannel channel : plynth::allMapChannels()) {
            const QString name2 = fromCore(std::string(plynth::mapChannelName(channel)));
            combo->addItem(name2, name2);
        }
        QString chosen;
        for (const auto& pair : channels_[static_cast<size_t>(index)])
            if (pair.first == entry)
                chosen = pair.second;
        combo->setCurrentIndex(std::max(0, combo->findData(chosen)));
        combo->setToolTip(tr("Which channel this texture is for. Automatic reads it from the file name."));
        combo->setProperty("tourId", QStringLiteral("fab-channel"));
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

// Takes the channel the user set for a texture and asks core again.
void FabImportDialog::chooseChannel(int index, const QString& entry, const QString& channel)
{
    auto& list = channels_[static_cast<size_t>(index)];
    std::erase_if(list, [&](const auto& pair) { return pair.first == entry; });
    if (!channel.isEmpty())
        list.emplace_back(entry, channel);
    Page& page = pages_[static_cast<size_t>(index)];
    page.options.erase("maps");
    if (!list.empty()) {
        Json all = Json::object();
        for (const auto& pair : list)
            all[toCore(pair.second)] = toCore(pair.first);
        page.options["maps"] = Json{{"*", all}};
    }
    fill(index);
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
