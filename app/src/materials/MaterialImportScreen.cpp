#include "MaterialImportScreen.h"

#include "catalog/LibraryHost.h"
#include "materials/MaterialImportQueue.h"
#include "panels/ImportQueueView.h"
#include "theme/Theme.h"

#include <plynth/core/catalog/MaterialImporter.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace plynth::app {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;

// Turns a core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Turns Qt text into the UTF-8 text core uses.
std::string toCore(const QString& text)
{
    return text.toUtf8().toStdString();
}

// Turns a Qt path into a standard path without losing non English letters.
std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path(text.toStdWString());
}

// A small muted caption above a field.
QLabel* caption(const QString& text, QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(text, parent);
    label->setFont(t.uiFont(t.sizeSmall()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    return label;
}

// The name a channel has on screen.
QString channelTitle(plynth::MapChannel channel)
{
    switch (channel) {
    case plynth::MapChannel::BaseColor: return QObject::tr("Base color");
    case plynth::MapChannel::Normal: return QObject::tr("Normal");
    case plynth::MapChannel::Roughness: return QObject::tr("Roughness");
    case plynth::MapChannel::Metallic: return QObject::tr("Metallic");
    case plynth::MapChannel::Opacity: return QObject::tr("Opacity");
    case plynth::MapChannel::Occlusion: return QObject::tr("Occlusion");
    }
    return QString();
}

// A tile size spin box in metres.
QDoubleSpinBox* tileSpin(const QString& tip, const QString& tourId, QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(3);
    spin->setRange(plynth::kMinTileSize, plynth::kMaxTileSize);
    spin->setSingleStep(0.05);
    spin->setValue(plynth::kDefaultTileSize);
    spin->setSuffix(QObject::tr(" m"));
    spin->setFont(t.monoFont(t.sizeNormal()));
    spin->setKeyboardTracking(false);
    spin->setStyle(t.controlStyle());
    spin->setToolTip(tip);
    spin->setProperty("tourId", tourId);
    return spin;
}

}

// Builds the screen: sources and queue on the left, the fields on the right.
MaterialImportScreen::MaterialImportScreen(LibraryHost& host, MaterialImportQueue& queue, QWidget* parent)
    : QWidget(parent), host_(host), queue_(queue)
{
    const Theme& t = Theme::instance();
    setProperty("tourId", QStringLiteral("materials-import-screen"));
    setAcceptDrops(true);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(t.unit() * 8, t.unit() * 6, t.unit() * 8, t.unit() * 5);
    column->setSpacing(t.unit() * 4);

    auto* head = new QHBoxLayout();
    head->setSpacing(t.unit() * 4);
    auto* title = new QLabel(tr("Import materials"), this);
    title->setFont(t.uiFont(t.sizeLarge() + 2, QFont::DemiBold));
    head->addWidget(title);
    head->addStretch(1);
    auto* back = new QPushButton(tr("Back"), this);
    back->setToolTip(tr("Close the import screen and go back to the plan"));
    back->setProperty("tourId", QStringLiteral("materials-import-back"));
    connect(back, &QPushButton::clicked, this, &MaterialImportScreen::backRequested);
    head->addWidget(back);
    column->addLayout(head);

    auto* body = new QHBoxLayout();
    body->setSpacing(t.unit() * 8);
    body->addWidget(buildSources(), 1);
    body->addWidget(buildForm(), 1);
    column->addLayout(body, 1);
    updateState();
}

// Paints the ground color, like the home screen.
void MaterialImportScreen::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::instance().ground());
}

// The left column: the source list with its buttons, a note, and the queue.
QWidget* MaterialImportScreen::buildSources()
{
    const Theme& t = Theme::instance();
    auto* page = new QWidget(this);
    auto* box = new QVBoxLayout(page);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(t.unit() * 2);
    box->addWidget(caption(tr("What to import"), page));

    sources_ = new QListWidget(page);
    sources_->setFont(t.monoFont(t.sizeSmall()));
    sources_->setStyle(t.controlStyle());
    sources_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    sources_->setTextElideMode(Qt::ElideMiddle);
    sources_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sources_->setStyleSheet(t.scrollBarStyleSheet());
    sources_->setMinimumHeight(t.unit() * 40);
    sources_->setToolTip(tr("Folders and zip files of textures, single images, or a .blend file. Drop them here or use the buttons"));
    sources_->setProperty("tourId", QStringLiteral("materials-import-sources"));
    box->addWidget(sources_, 1);
    sourcesHint_ = new QLabel(tr("Drop folders, zip files, images or .blend files here, or use Add folder / Add files."),
                              sources_->viewport());
    sourcesHint_->setWordWrap(true);
    sourcesHint_->setAlignment(Qt::AlignCenter);
    sourcesHint_->setFont(t.uiFont(t.sizeNormal()));
    sourcesHint_->setStyleSheet(QStringLiteral("color: %1;").arg(t.muted().name()));
    sourcesHint_->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* hintLayout = new QVBoxLayout(sources_->viewport());
    hintLayout->setContentsMargins(t.unit() * 8, t.unit() * 4, t.unit() * 8, t.unit() * 4);
    hintLayout->addWidget(sourcesHint_);
    connect(sources_, &QListWidget::itemSelectionChanged, this, &MaterialImportScreen::updateState);

    note_ = new QLabel(page);
    note_->setWordWrap(true);
    note_->setFont(t.uiFont(t.sizeSmall()));
    note_->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    note_->setToolTip(tr("Why something was left out of the list"));
    note_->setProperty("tourId", QStringLiteral("materials-import-note"));
    note_->hide();
    box->addWidget(note_);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 2);
    auto* folder = new QPushButton(tr("Add folder..."), page);
    folder->setToolTip(tr("Pick a folder that holds the texture images of one material"));
    folder->setProperty("tourId", QStringLiteral("materials-import-add-folder"));
    connect(folder, &QPushButton::clicked, this, &MaterialImportScreen::chooseFolder);
    auto* files = new QPushButton(tr("Add files..."), page);
    files->setToolTip(tr("Pick zip files, image files or a .blend file"));
    files->setProperty("tourId", QStringLiteral("materials-import-add-files"));
    connect(files, &QPushButton::clicked, this, &MaterialImportScreen::chooseFiles);
    remove_ = new QPushButton(tr("Remove"), page);
    remove_->setToolTip(tr("Take the selected lines off the list"));
    remove_->setProperty("tourId", QStringLiteral("materials-import-remove"));
    connect(remove_, &QPushButton::clicked, this, [this]() {
        qDeleteAll(sources_->selectedItems());
        updateState();
    });
    clear_ = new QPushButton(tr("Clear"), page);
    clear_->setToolTip(tr("Empty the list"));
    clear_->setProperty("tourId", QStringLiteral("materials-import-clear"));
    connect(clear_, &QPushButton::clicked, this, [this]() {
        sources_->clear();
        note_->hide();
        updateState();
    });
    buttons->addWidget(folder);
    buttons->addWidget(files);
    buttons->addStretch(1);
    buttons->addWidget(remove_);
    buttons->addWidget(clear_);
    box->addLayout(buttons);

    queueView_ = new ImportQueueView(queue_, page);
    box->addWidget(queueView_);

    import_ = new QPushButton(tr("Import"), page);
    import_->setDefault(true);
    import_->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    {
        const QColor dim = QColor::fromRgbF(t.signalBlue().redF() * 0.35 + t.ground().redF() * 0.65,
                                            t.signalBlue().greenF() * 0.35 + t.ground().greenF() * 0.65,
                                            t.signalBlue().blueF() * 0.35 + t.ground().blueF() * 0.65);
        import_->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %1; "
                                              "border-radius: %3px; padding: %4px %5px; }"
                                              "QPushButton:hover { border-color: %2; }"
                                              "QPushButton:disabled { background: %6; color: %7; border-color: %6; }")
                                   .arg(t.signalBlue().name(), t.onBlue().name())
                                   .arg(t.radius())
                                   .arg(t.unit())
                                   .arg(t.unit() * 5)
                                   .arg(dim.name(), t.muted().name()));
    }
    import_->setToolTip(tr("Add every line of the list to the import queue"));
    import_->setProperty("tourId", QStringLiteral("materials-import-start"));
    connect(import_, &QPushButton::clicked, this, &MaterialImportScreen::importAll);
    box->addWidget(import_, 0, Qt::AlignRight);
    return page;
}

// The right column: every field of the material, in a scroll area.
QWidget* MaterialImportScreen::buildForm()
{
    const Theme& t = Theme::instance();
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->verticalScrollBar()->setStyleSheet(t.scrollBarStyleSheet());
    scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
    scroll->setProperty("tourId", QStringLiteral("materials-import-form"));
    auto* body = new QWidget();
    auto* box = new QVBoxLayout(body);
    box->setContentsMargins(0, 0, t.unit() * 3, 0);
    box->setSpacing(t.unit() * 2);

    box->addWidget(caption(tr("Name (one source only)"), body));
    name_ = new QLineEdit(body);
    name_->setFont(t.uiFont(t.sizeNormal()));
    name_->setStyle(t.controlStyle());
    name_->setPlaceholderText(tr("From material.json or the folder or file name"));
    name_->setToolTip(tr("The name shown in the catalog. Leave empty to take it from the source"));
    name_->setProperty("tourId", QStringLiteral("materials-import-name"));
    box->addWidget(name_);

    box->addWidget(caption(tr("Groups"), body));
    groups_ = new QListWidget(body);
    groups_->setFont(t.uiFont(t.sizeNormal()));
    groups_->setStyle(t.controlStyle());
    groups_->setFixedHeight(t.unit() * 34);
    groups_->setStyleSheet(t.scrollBarStyleSheet());
    groups_->setToolTip(tr("Tick the groups the material goes in. With none ticked, the app asks after the import"));
    groups_->setProperty("tourId", QStringLiteral("materials-import-groups"));
    box->addWidget(groups_);

    box->addWidget(caption(tr("Tags, separated by commas"), body));
    tags_ = new QLineEdit(body);
    tags_->setFont(t.uiFont(t.sizeNormal()));
    tags_->setStyle(t.controlStyle());
    tags_->setToolTip(tr("Words that help find the material. Lower case letters, digits, _ and -"));
    tags_->setProperty("tourId", QStringLiteral("materials-import-tags"));
    box->addWidget(tags_);

    box->addWidget(caption(tr("License"), body));
    license_ = new QComboBox(body);
    license_->setFont(t.uiFont(t.sizeNormal()));
    license_->setStyle(t.controlStyle());
    license_->setToolTip(tr("The license the material came with. The first entry takes it from the source"));
    license_->setProperty("tourId", QStringLiteral("materials-import-license"));
    box->addWidget(license_);

    setTile_ = new QCheckBox(tr("Set the tile size"), body);
    setTile_->setFont(t.uiFont(t.sizeNormal()));
    setTile_->setToolTip(tr("How far one repeat of the texture reaches on a wall or floor. Off takes it from material.json, else 1 m"));
    setTile_->setProperty("tourId", QStringLiteral("materials-import-set-tile"));
    box->addWidget(setTile_);
    auto* tile = new QHBoxLayout();
    tile->setSpacing(t.unit() * 2);
    tileWidth_ = tileSpin(tr("Width of one tile in metres"), QStringLiteral("materials-import-tile-width"), body);
    tileHeight_ = tileSpin(tr("Height of one tile in metres"), QStringLiteral("materials-import-tile-height"), body);
    auto* by = new QLabel(QStringLiteral("x"), body);
    by->setFont(t.monoFont(t.sizeNormal()));
    tile->addWidget(tileWidth_, 1);
    tile->addWidget(by);
    tile->addWidget(tileHeight_, 1);
    box->addLayout(tile);
    const auto syncTile = [this]() {
        tileWidth_->setEnabled(setTile_->isChecked());
        tileHeight_->setEnabled(setTile_->isChecked());
    };
    connect(setTile_, &QCheckBox::toggled, this, syncTile);
    syncTile();

    box->addWidget(caption(tr("Normal map type"), body));
    normal_ = new QComboBox(body);
    normal_->setFont(t.uiFont(t.sizeNormal()));
    normal_->setStyle(t.controlStyle());
    normal_->addItem(tr("Automatic (from the file name)"), QStringLiteral("auto"));
    normal_->addItem(tr("OpenGL"), QStringLiteral("opengl"));
    normal_->addItem(tr("DirectX (flips green)"), QStringLiteral("directx"));
    normal_->setToolTip(tr("Pick DirectX when bumps look inverted. A map with dx in its name is found by itself"));
    normal_->setProperty("tourId", QStringLiteral("materials-import-normal"));
    box->addWidget(normal_);

    box->addWidget(caption(tr("Material name inside a .blend file"), body));
    blendMaterial_ = new QLineEdit(body);
    blendMaterial_->setFont(t.uiFont(t.sizeNormal()));
    blendMaterial_->setStyle(t.controlStyle());
    blendMaterial_->setPlaceholderText(tr("Needed when the file holds several materials"));
    blendMaterial_->setToolTip(tr("Which material of a .blend file to take. Ignored for textures"));
    blendMaterial_->setProperty("tourId", QStringLiteral("materials-import-blend-material"));
    box->addWidget(blendMaterial_);

    box->addWidget(caption(tr("Bake size for .blend materials"), body));
    bake_ = new QComboBox(body);
    bake_->setFont(t.monoFont(t.sizeNormal()));
    bake_->setStyle(t.controlStyle());
    for (int size = plynth::kMinBakeSize; size <= plynth::kMaxBakeSize; size *= 2)
        bake_->addItem(QStringLiteral("%1 x %1 px").arg(size), size);
    bake_->setCurrentIndex(std::max(0, bake_->findData(plynth::kDefaultBakeSize)));
    bake_->setToolTip(tr("Size of the images made from a .blend material that is not plain image textures"));
    bake_->setProperty("tourId", QStringLiteral("materials-import-bake"));
    box->addWidget(bake_);

    box->addWidget(caption(tr("Image files per channel (one source only)"), body));
    int row = 0;
    for (const plynth::MapChannel channel : plynth::allMapChannels()) {
        auto* line = new QHBoxLayout();
        line->setSpacing(t.unit() * 2);
        auto* label = new QLabel(channelTitle(channel), body);
        label->setFont(t.uiFont(t.sizeNormal()));
        label->setFixedWidth(t.unit() * 24);
        auto* edit = new QLineEdit(body);
        edit->setFont(t.monoFont(t.sizeSmall()));
        edit->setStyle(t.controlStyle());
        edit->setPlaceholderText(tr("Leave empty to match by file name"));
        edit->setToolTip(tr("The image to use for %1. Leave empty to match by file name").arg(channelTitle(channel).toLower()));
        edit->setProperty("tourId", QStringLiteral("materials-import-channel-%1").arg(fromCore(std::string(plynth::mapChannelName(channel)))));
        auto* browse = new QPushButton(tr("Browse..."), body);
        browse->setToolTip(tr("Pick the image file for %1").arg(channelTitle(channel).toLower()));
        browse->setProperty("tourId", QStringLiteral("materials-import-channel-browse"));
        connect(browse, &QPushButton::clicked, this, [this, edit]() {
            const QString file = QFileDialog::getOpenFileName(this, tr("Pick an image"), lastFolder_,
                                                              tr("Images (*.png *.jpg *.jpeg *.tga *.tif *.tiff *.bmp *.exr)"));
            if (!file.isEmpty()) {
                lastFolder_ = QFileInfo(file).absolutePath();
                edit->setText(QDir::toNativeSeparators(file));
            }
        });
        line->addWidget(label);
        line->addWidget(edit, 1);
        line->addWidget(browse);
        box->addLayout(line);
        channelFiles_[static_cast<size_t>(row)] = edit;
        channelBrowse_[static_cast<size_t>(row)] = browse;
        ++row;
    }
    box->addStretch(1);
    scroll->setWidget(body);
    refreshLists();
    return scroll;
}

// Fills the group and license lists from the open library, keeping what was ticked.
void MaterialImportScreen::refreshLists()
{
    QStringList ticked;
    for (int i = 0; i < groups_->count(); ++i)
        if (groups_->item(i)->checkState() == Qt::Checked)
            ticked << groups_->item(i)->data(kIdRole).toString();
    const QVariant keepLicense = license_->currentData();
    groups_->clear();
    license_->clear();
    license_->addItem(tr("From the source"), QString());
    plynth::MaterialCatalog* catalog = host_.materialCatalog();
    if (catalog == nullptr)
        return;
    for (const plynth::MaterialGroupEntry& entry : catalog->groups()) {
        auto* item = new QListWidgetItem(fromCore(entry.name), groups_);
        item->setData(kIdRole, fromCore(entry.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(ticked.contains(fromCore(entry.id)) ? Qt::Checked : Qt::Unchecked);
    }
    for (const plynth::LicenseEntry& entry : catalog->licenses())
        license_->addItem(fromCore(entry.name), fromCore(entry.id));
    license_->setCurrentIndex(std::max(0, license_->findData(keepLicense)));
}

// Turns Import on or off with the library.
void MaterialImportScreen::setLibraryOpen(bool open)
{
    libraryOpen_ = open;
    updateState();
}

// Enables the buttons that need a library or a list, and the fields that only fit one source.
void MaterialImportScreen::updateState()
{
    const int count = sources_->count();
    sourcesHint_->setVisible(count == 0);
    import_->setEnabled(libraryOpen_ && count > 0);
    import_->setToolTip(!libraryOpen_ ? tr("Set a library folder first")
                        : count == 0  ? tr("Add something to import first")
                                      : tr("Add every line of the list to the import queue"));
    remove_->setEnabled(!sources_->selectedItems().isEmpty());
    clear_->setEnabled(count > 0);
    const bool single = count <= 1;
    name_->setEnabled(single);
    for (size_t i = 0; i < channelFiles_.size(); ++i) {
        channelFiles_[i]->setEnabled(single);
        channelBrowse_[i]->setEnabled(single);
    }
    blendMaterial_->setEnabled(single);
}

// Adds sources no importer refuses.
void MaterialImportScreen::addSources(const QStringList& paths)
{
    QStringList refused;
    for (const QString& path : paths) {
        const QString clean = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
        plynth::ImportSource source;
        source.file = toPath(clean);
        if (plynth::pickMaterialImporter(source) == nullptr) {
            refused << QFileInfo(path).fileName();
            continue;
        }
        if (!sources_->findItems(clean, Qt::MatchExactly).isEmpty())
            continue;
        auto* item = new QListWidgetItem(clean, sources_);
        item->setToolTip(tr("%1\nTaken by: %2").arg(clean, fromCore(std::string(plynth::pickMaterialImporter(source)->name()))));
    }
    note_->setVisible(!refused.isEmpty());
    if (!refused.isEmpty())
        note_->setText(tr("Not a texture folder, zip, image or .blend file: %1").arg(refused.join(QStringLiteral(", "))));
    updateState();
}

// Asks for a folder of textures.
void MaterialImportScreen::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Pick a texture folder"), lastFolder_);
    if (folder.isEmpty())
        return;
    lastFolder_ = QFileInfo(folder).absolutePath();
    addSources({folder});
}

// Asks for zips, images or a .blend file.
void MaterialImportScreen::chooseFiles()
{
    QStringList patterns;
    for (const std::string& suffix : plynth::importableMaterialSuffixes())
        patterns << QStringLiteral("*") + fromCore(suffix);
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Pick materials"), lastFolder_,
                                                            tr("Materials (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (files.isEmpty())
        return;
    lastFolder_ = QFileInfo(files.first()).absolutePath();
    addSources(files);
}

// Builds the request for one source from the fields.
plynth::MaterialImportRequest MaterialImportScreen::makeRequest(const QString& path) const
{
    plynth::MaterialImportRequest request;
    request.source.file = toPath(path);
    const bool single = sources_->count() <= 1;
    if (single && !name_->text().trimmed().isEmpty())
        request.name = toCore(name_->text().trimmed());
    for (int i = 0; i < groups_->count(); ++i)
        if (groups_->item(i)->checkState() == Qt::Checked)
            request.groups.push_back(toCore(groups_->item(i)->data(kIdRole).toString()));
    std::vector<std::string> tags;
    for (const QString& part : tags_->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString tag = part.trimmed().toLower();
        if (!tag.isEmpty())
            tags.push_back(toCore(tag));
    }
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    request.tags = tags;
    request.license = toCore(license_->currentData().toString());
    if (setTile_->isChecked())
        request.tileSize = plynth::TileSize{tileWidth_->value(), tileHeight_->value()};
    request.normal = plynth::normalConventionFromName(toCore(normal_->currentData().toString()))
                         .value_or(plynth::NormalConvention::Auto);
    request.bakeSize = bake_->currentData().toInt();
    if (single) {
        request.blendMaterial = toCore(blendMaterial_->text().trimmed());
        size_t i = 0;
        for (const plynth::MapChannel channel : plynth::allMapChannels()) {
            const QString file = channelFiles_[i++]->text().trimmed();
            if (!file.isEmpty())
                request.maps.emplace_back(channel, toPath(file));
        }
    }
    return request;
}

// Queues every source, then empties the list and the fields that belong to one source.
void MaterialImportScreen::importAll()
{
    if (!libraryOpen_)
        return;
    QStringList paths;
    for (int i = 0; i < sources_->count(); ++i)
        paths << sources_->item(i)->text();
    for (const QString& path : paths)
        queue_.enqueueImport(makeRequest(path));
    sources_->clear();
    note_->hide();
    name_->clear();
    blendMaterial_->clear();
    for (QLineEdit* edit : channelFiles_)
        edit->clear();
    updateState();
}

// Accepts files and folders dragged from Explorer.
void MaterialImportScreen::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

// Adds the dropped files and folders.
void MaterialImportScreen::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    if (!paths.isEmpty())
        addSources(paths);
}

}
