#include "IntegrationsPanel.h"

#include "catalog/LibraryHost.h"
#include "theme/Theme.h"

#include <plynth/core/catalog/BlenderKit.h>
#include <plynth/core/catalog/Integrations.h>

#include <QComboBox>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QUrl>
#include <QVBoxLayout>

namespace plynth::app {

namespace {

QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString fromView(std::string_view text)
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
    return label;
}

// A note line in the amber warning color, hidden until it has text.
QLabel* warningLabel(QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(parent);
    label->setFont(t.uiFont(t.sizeSmall()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setVisible(false);
    return label;
}

// The words on the import buttons, by what the integration takes.
struct ButtonWords {
    QString asset;
    QString material;
    QString assetTip;
    QString materialTip;
};

ButtonWords wordsFor(const plynth::Integration& integration)
{
    if (integration.takesAddresses())
        return {QObject::tr("Import as asset"), QObject::tr("Import as material"),
                QObject::tr("Download the item at this address and add it to the asset catalog"),
                QObject::tr("Download the item at this address and add it to the materials catalog")};
    if (integration.id() == "fab")
        return {QObject::tr("Import model zips..."), QObject::tr("Import material zips..."),
                QObject::tr("Pick one or more Fab model zips and add them to the asset catalog"),
                QObject::tr("Pick one or more Fab material zips and add them to the materials catalog")};
    if (integration.id() == "local")
        return {QObject::tr("Import asset..."), QObject::tr("Import material..."),
                QObject::tr("Pick model files and add them to the asset catalog"),
                QObject::tr("Pick texture files or a folder and add them to the materials catalog")};
    return {QObject::tr("Import assets..."), QObject::tr("Import materials..."),
            QObject::tr("Pick files and add them to the asset catalog"),
            QObject::tr("Pick files and add them to the materials catalog")};
}

}

// Builds the cards from the integration registry.
IntegrationsPanel::IntegrationsPanel(LibraryHost& host, QWidget* parent) : QWidget(parent), host_(host)
{
    const Theme& t = Theme::instance();
    setProperty("tourId", QStringLiteral("integrations-panel"));
    setToolTip(tr("Where assets and materials come from"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->verticalScrollBar()->setStyleSheet(t.scrollBarStyleSheet());
    auto* body = new QWidget(scroll);
    cards_ = new QVBoxLayout(body);
    cards_->setContentsMargins(t.unit() * 3, t.unit() * 3, t.unit() * 3, t.unit() * 3);
    cards_->setSpacing(t.unit() * 4);
    scroll->setWidget(body);
    outer->addWidget(scroll);

    const auto integrations = plynth::allIntegrations();
    list_.reserve(integrations.size());
    for (const plynth::Integration* integration : integrations)
        buildCard(*integration);
    cards_->addStretch(1);

    connect(&host_, &LibraryHost::libraryChanged, this, &IntegrationsPanel::refresh);
    refresh();
}

// Stops a running check before the panel goes away.
IntegrationsPanel::~IntegrationsPanel()
{
    stop_.request_stop();
    worker_ = std::jthread();
}

// Sets where the import buttons send their requests.
void IntegrationsPanel::setActions(IntegrationActions* actions)
{
    actions_ = actions;
    updateImportButtons();
}

// Builds one card: title, state, options, import buttons and the site link.
void IntegrationsPanel::buildCard(const plynth::Integration& integration)
{
    const Theme& t = Theme::instance();
    const QString id = fromView(integration.id());
    list_.emplace_back();
    Card& card = list_.back();
    card.integration = &integration;

    auto* frame = new QFrame(cards_->parentWidget());
    frame->setObjectName(QStringLiteral("integrationCard"));
    frame->setStyleSheet(QStringLiteral("QFrame#integrationCard { background: %1; border: 1px solid %2; border-radius: %3px; }")
                             .arg(t.raised().name(), t.line().name())
                             .arg(t.radius()));
    frame->setProperty("tourId", QStringLiteral("integration-%1").arg(id));
    frame->setToolTip(tr("%1: %2").arg(fromView(integration.name()), fromView(integration.description())));
    auto* box = new QVBoxLayout(frame);
    box->setContentsMargins(t.unit() * 3, t.unit() * 3, t.unit() * 3, t.unit() * 3);
    box->setSpacing(t.unit() * 2);

    auto* top = new QHBoxLayout();
    top->setSpacing(t.unit() * 2);
    auto* title = new QLabel(fromView(integration.name()), frame);
    title->setFont(t.uiFont(t.sizeLarge(), QFont::DemiBold));
    title->setToolTip(tr("The name of this source"));
    top->addWidget(title, 1);
    card.state = new QLabel(frame);
    card.state->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    card.state->setToolTip(tr("Whether this source is ready to use"));
    card.state->setProperty("tourId", QStringLiteral("integration-%1-state").arg(id));
    top->addWidget(card.state);
    box->addLayout(top);

    auto* description = new QLabel(fromView(integration.description()), frame);
    description->setFont(t.uiFont(t.sizeNormal()));
    description->setWordWrap(true);
    description->setTextFormat(Qt::PlainText);
    description->setToolTip(tr("What this source does and what it needs"));
    box->addWidget(description);

    if (!integration.setupItem().empty()) {
        card.detail = caption(QString(), frame);
        card.detail->setTextFormat(Qt::PlainText);
        card.detail->setToolTip(tr("What the setup check found and what to do next"));
        box->addWidget(card.detail);
        card.check = new QPushButton(tr("Check again"), frame);
        card.check->setToolTip(tr("Run the setup check again, including Blender and its add-ons"));
        card.check->setProperty("tourId", QStringLiteral("integration-%1-check").arg(id));
        connect(card.check, &QPushButton::clicked, this, &IntegrationsPanel::checkAgain);
        auto* row = new QHBoxLayout();
        row->addWidget(card.check);
        row->addStretch(1);
        box->addLayout(row);
    }

    if (!integration.note().empty()) {
        QLabel* note = warningLabel(frame);
        note->setText(fromView(integration.note()));
        note->setToolTip(tr("What to know before using this source"));
        note->setProperty("tourId", QStringLiteral("integration-%1-note").arg(id));
        note->setVisible(true);
        box->addWidget(note);
    }

    const ButtonWords words = wordsFor(integration);

    if (integration.takesAddresses()) {
        card.address = new QLineEdit(frame);
        card.address->setFont(t.uiFont(t.sizeNormal()));
        card.address->setPlaceholderText(id == QStringLiteral("blenderkit") ? tr("Paste a BlenderKit address")
                                                                              : tr("Paste an address"));
        card.address->setClearButtonEnabled(true);
        card.address->setToolTip(tr("The address of one item, copied from its page in your browser"));
        card.address->setProperty("tourId", QStringLiteral("integration-%1-address").arg(id));
        box->addWidget(card.address);
        card.addressNote = warningLabel(frame);
        box->addWidget(card.addressNote);
        connect(card.address, &QLineEdit::textChanged, this, [this, id]() {
            for (Card& c : list_)
                if (fromView(c.integration->id()) == id)
                    updateAddress(c);
        });
    }

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 2);
    if (!integration.assetImporters().empty()) {
        card.importAsset = new QPushButton(words.asset, frame);
        card.importAsset->setToolTip(words.assetTip);
        card.importAsset->setProperty("tourId", QStringLiteral("integration-%1-import-asset").arg(id));
        card.importAsset->setDefault(integration.takesAddresses());
        buttons->addWidget(card.importAsset);
    }
    if (!integration.materialImporters().empty()) {
        card.importMaterial = new QPushButton(words.material, frame);
        card.importMaterial->setToolTip(words.materialTip);
        card.importMaterial->setProperty("tourId", QStringLiteral("integration-%1-import-material").arg(id));
        buttons->addWidget(card.importMaterial);
    }
    buttons->addStretch(1);
    box->addLayout(buttons);
    const auto send = [this, id](bool material) {
        if (actions_ == nullptr)
            return;
        for (Card& c : list_) {
            if (fromView(c.integration->id()) != id)
                continue;
            if (c.address != nullptr)
                actions_->importAddress(id, c.address->text().trimmed(), material);
            else
                actions_->importFiles(id, material);
        }
    };
    if (card.importAsset != nullptr)
        connect(card.importAsset, &QPushButton::clicked, this, [send]() { send(false); });
    if (card.importMaterial != nullptr)
        connect(card.importMaterial, &QPushButton::clicked, this, [send]() { send(true); });

    if (id == QStringLiteral("blenderkit")) {
        QComboBox* resolution = addOption(card, box, QStringLiteral("resolution"), tr("Texture resolution"),
                                          tr("The size of the textures to download"));
        for (const char* r : {"0.5k", "1k", "2k", "4k", "8k"})
            resolution->addItem(QString::fromLatin1(r), QString::fromLatin1(r));
        resolution->addItem(tr("Original"), QStringLiteral("original"));
    } else if (id == QStringLiteral("fab")) {
        QComboBox* format = addOption(card, box, QStringLiteral("format"), tr("Preferred format"),
                                      tr("The model format to take first when a zip holds several"));
        for (const char* f : {"fbx", "gltf", "obj", "blend"})
            format->addItem(QString::fromLatin1(f).toUpper(), QString::fromLatin1(f));
        QComboBox* quality = addOption(card, box, QStringLiteral("quality"), tr("Quality"),
                                       tr("Which version to take when a zip holds several qualities"));
        quality->addItem(tr("Low"), QStringLiteral("low"));
        quality->addItem(tr("Medium"), QStringLiteral("medium"));
        quality->addItem(tr("High"), QStringLiteral("high"));
        quality->addItem(tr("Raw"), QStringLiteral("raw"));
        QComboBox* size = addOption(card, box, QStringLiteral("max_texture_size"), tr("Largest texture"),
                                    tr("The largest texture size to take. Bigger images are left in the zip"));
        for (int s : {512, 1024, 2048, 4096, 8192, 16384})
            size->addItem(tr("%1 px").arg(s), QString::number(s));
        size->addItem(tr("No limit"), QStringLiteral("none"));
        QComboBox* normal = addOption(card, box, QStringLiteral("normal"), tr("Normal maps"),
                                      tr("Which way the normal maps point. Automatic reads the file name"));
        normal->addItem(tr("Automatic"), QStringLiteral("auto"));
        normal->addItem(tr("OpenGL"), QStringLiteral("opengl"));
        normal->addItem(tr("DirectX"), QStringLiteral("directx"));
        QComboBox* license = addOption(card, box, QStringLiteral("license"), tr("License"),
                                       tr("The license given to items imported from Fab zips"));
        fillLicenses(license);
    }
    if (!card.options.empty()) {
        card.optionNote = warningLabel(frame);
        box->addWidget(card.optionNote);
    }

    if (!integration.site().empty()) {
        auto* link = new QLabel(frame);
        link->setFont(t.uiFont(t.sizeSmall()));
        link->setTextFormat(Qt::RichText);
        link->setText(QStringLiteral("<a href=\"%1\">%2</a>")
                          .arg(fromView(integration.site()),
                               tr("Open the %1 website").arg(fromView(integration.name()).toHtmlEscaped())));
        QPalette palette = link->palette();
        palette.setColor(QPalette::Link, t.signalBlue());
        link->setPalette(palette);
        link->setToolTip(tr("Open the official site in your browser"));
        link->setProperty("tourId", QStringLiteral("integration-%1-site").arg(id));
        connect(link, &QLabel::linkActivated, this, [](const QString& url) { QDesktopServices::openUrl(QUrl(url)); });
        box->addWidget(link);
    }

    cards_->addWidget(frame);
}

// Adds a captioned drop down for one saved option. The caller fills in the choices.
QComboBox* IntegrationsPanel::addOption(Card& card, QVBoxLayout* box, const QString& key, const QString& label, const QString& tip)
{
    const Theme& t = Theme::instance();
    const QString id = fromView(card.integration->id());
    QWidget* parent = box->parentWidget();
    auto* row = new QHBoxLayout();
    row->setSpacing(t.unit() * 2);
    auto* name = new QLabel(label, parent);
    name->setFont(t.uiFont(t.sizeNormal()));
    name->setToolTip(tip);
    row->addWidget(name, 1);
    auto* combo = new QComboBox(parent);
    combo->setFont(t.uiFont(t.sizeNormal()));
    combo->setStyle(t.controlStyle());
    combo->setToolTip(tip);
    combo->setProperty("tourId", QStringLiteral("integration-%1-%2").arg(id, key));
    row->addWidget(combo);
    box->addLayout(row);
    card.options[key] = combo;
    connect(combo, &QComboBox::activated, this, [this, id, key, combo](int index) {
        if (refreshing_)
            return;
        for (Card& c : list_)
            if (fromView(c.integration->id()) == id)
                saveOption(c, key, combo->itemData(index).toString());
    });
    return combo;
}

// Fills a license drop down from the catalog's license list.
void IntegrationsPanel::fillLicenses(QComboBox* combo)
{
    combo->clear();
    if (host_.catalog() == nullptr)
        return;
    for (const plynth::LicenseEntry& entry : host_.catalog()->licenses())
        combo->addItem(fromCore(entry.name), fromCore(entry.id));
}

// Saves one changed option at once and shows the refusal in amber when core says no.
void IntegrationsPanel::saveOption(Card& card, const QString& key, const QString& value)
{
    plynth::Json changes = plynth::Json::object();
    if (key == QStringLiteral("format")) {
        std::vector<std::string> formats{toCore(value)};
        for (const char* f : {"fbx", "gltf", "obj", "blend"})
            if (value != QString::fromLatin1(f))
                formats.emplace_back(f);
        changes["formats"] = formats;
    } else if (key == QStringLiteral("max_texture_size")) {
        changes[toCore(key)] = value == QStringLiteral("none") ? plynth::Json(nullptr) : plynth::Json(value.toInt());
    } else {
        changes[toCore(key)] = toCore(value);
    }
    const plynth::Status status = plynth::setIntegrationOptions(host_.paths(), card.integration->id(), changes);
    if (card.optionNote != nullptr) {
        card.optionNote->setText(status.ok() ? QString() : fromCore(status.message));
        card.optionNote->setVisible(!status.ok());
    }
    if (!status.ok())
        refresh();
}

// Shows the saved values in the drop downs and turns the controls on or off with the library.
void IntegrationsPanel::refresh()
{
    refreshing_ = true;
    const bool library = host_.hasLibrary();
    const plynth::IntegrationOptionsRead read = library ? plynth::readIntegrationOptions(host_.paths())
                                                         : plynth::IntegrationOptionsRead{};
    const bool readable = library && read.status.ok();
    for (Card& card : list_) {
        const plynth::Json options = plynth::integrationOptions(*card.integration, read.saved);
        for (auto& [key, combo] : card.options) {
            combo->setEnabled(readable);
            if (key == QStringLiteral("license") && combo->count() == 0)
                fillLicenses(combo);
            QString value;
            if (key == QStringLiteral("format")) {
                const auto it = options.find("formats");
                if (it != options.end() && it->is_array() && !it->empty() && it->front().is_string())
                    value = fromCore(it->front().get<std::string>());
            } else if (key == QStringLiteral("max_texture_size")) {
                const auto it = options.find("max_texture_size");
                if (it != options.end())
                    value = it->is_number_integer() ? QString::number(it->get<int>()) : QStringLiteral("none");
            } else {
                const auto it = options.find(toCore(key));
                if (it != options.end() && it->is_string())
                    value = fromCore(it->get<std::string>());
            }
            const int index = combo->findData(value);
            if (index >= 0)
                combo->setCurrentIndex(index);
        }
        if (card.optionNote != nullptr) {
            QString note;
            if (!library)
                note = tr("Open a library to change these options.");
            else if (!read.status.ok())
                note = fromCore(read.status.message);
            card.optionNote->setText(note);
            card.optionNote->setVisible(!note.isEmpty());
        }
    }
    refreshing_ = false;
    updateStates();
    updateImportButtons();
}

// Turns each import button on when its card has what it needs.
void IntegrationsPanel::updateImportButtons()
{
    const bool ready = actions_ != nullptr && host_.hasLibrary();
    const QString why = actions_ == nullptr ? tr("Importing arrives with the import queue") : tr("Open a library first");
    for (Card& card : list_) {
        bool addressOk = true;
        if (card.address != nullptr)
            addressOk = !card.address->text().trimmed().isEmpty() && !card.addressNote->isVisible();
        const bool on = ready && addressOk;
        const ButtonWords words = wordsFor(*card.integration);
        const auto set = [&](QPushButton* button, const QString& tip) {
            if (button == nullptr)
                return;
            button->setEnabled(on);
            button->setToolTip(ready ? tip : why);
        };
        set(card.importAsset, words.assetTip);
        set(card.importMaterial, words.materialTip);
    }
}

// Checks the address field as it changes. BlenderKit addresses are read by core.
void IntegrationsPanel::updateAddress(Card& card)
{
    const QString text = card.address->text().trimmed();
    QString problem;
    if (!text.isEmpty() && card.integration->id() == "blenderkit") {
        const plynth::BlenderKitAddress parsed = plynth::parseBlenderKitAddress(toCore(text));
        if (!parsed.status.ok())
            problem = fromCore(parsed.status.message);
    }
    card.addressNote->setText(problem);
    card.addressNote->setVisible(!problem.isEmpty());
    updateImportButtons();
}

// Sets the state line of every card from the last report.
void IntegrationsPanel::updateStates()
{
    const Theme& t = Theme::instance();
    for (Card& card : list_) {
        QString text;
        QColor color = t.muted();
        QString detail;
        if (card.integration->setupItem().empty()) {
            text = tr("Ready");
            color = t.signalBlue();
        } else if (checking_ && !report_) {
            text = tr("Checking...");
        } else {
            const plynth::IntegrationStatus status =
                plynth::integrationStatus(*card.integration, report_ ? &*report_ : nullptr);
            switch (status.state) {
            case plynth::IntegrationState::Ready:
                text = tr("Ready");
                color = t.signalBlue();
                break;
            case plynth::IntegrationState::NeedsSetup:
                text = tr("Needs setup");
                color = t.signalAmber();
                break;
            case plynth::IntegrationState::NotChecked:
                text = tr("Not checked");
                break;
            }
            detail = fromCore(status.detail);
            if (!status.action.empty())
                detail += QStringLiteral(" ") + fromCore(status.action);
        }
        card.state->setText(text);
        card.state->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
        if (card.detail != nullptr) {
            card.detail->setText(detail);
            card.detail->setVisible(!detail.isEmpty());
        }
        if (card.check != nullptr) {
            card.check->setEnabled(!checking_);
            card.check->setText(checking_ ? tr("Checking...") : tr("Check again"));
        }
    }
}

// Runs the setup check on a worker thread. An older run is stopped and its answer ignored.
void IntegrationsPanel::checkAgain()
{
    stop_.request_stop();
    stop_ = std::stop_source();
    const int generation = ++generation_;
    checking_ = true;
    checkedOnce_ = true;
    updateStates();
    const plynth::MachineSettings settings = host_.settings();
    plynth::SetupCheckOptions options;
    options.settingsExist = host_.settingsExisted();
    const std::stop_token token = stop_.get_token();
    worker_ = std::jthread([this, generation, settings, options, token]() {
        plynth::SetupReport report = plynth::runSetupCheck(settings, options, token);
        QMetaObject::invokeMethod(
            this,
            [this, generation, report]() {
                if (generation == generation_)
                    showReport(report);
            },
            Qt::QueuedConnection);
    });
}

// Keeps a finished report and shows it.
void IntegrationsPanel::showReport(const plynth::SetupReport& report)
{
    report_ = report;
    checking_ = false;
    updateStates();
}

// Fills the address field of the first card that takes addresses.
void IntegrationsPanel::setAddress(const QString& text)
{
    for (Card& card : list_)
        if (card.address != nullptr) {
            card.address->setText(text);
            card.address->setFocus();
            return;
        }
}

// Puts the focus in the address field.
void IntegrationsPanel::focusAddress()
{
    for (Card& card : list_)
        if (card.address != nullptr) {
            card.address->setFocus();
            return;
        }
}

// Runs the first check when the tab is first shown.
void IntegrationsPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (checkedOnce_)
        return;
    for (const Card& card : list_)
        if (card.check != nullptr) {
            checkAgain();
            return;
        }
}

}
