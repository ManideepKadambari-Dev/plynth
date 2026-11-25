#include "IntegrationsPanel.h"

#include "catalog/LibraryHost.h"
#include "theme/PlainText.h"
#include "theme/Theme.h"
#include "theme/WheelGuard.h"

#include <plynth/core/catalog/FabZip.h>
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
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>

namespace plynth::app {

namespace {

// Marks a drop down entry that only shows a saved value the list does not have.
constexpr int kExtraRole = Qt::UserRole + 1;

// Turns core text into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Turns a piece of core text into Qt text.
QString fromView(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// Turns Qt text into core text.
std::string toCore(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

// The text a drop down keeps for a value, so any value can be found again.
QString valueKey(const plynth::Json& value)
{
    return fromCore(value.dump());
}

// The words shown for a saved value that is not one of the choices.
QString shownValue(const plynth::Json& value)
{
    if (value.is_string())
        return fromCore(value.get<std::string>());
    if (value.is_null())
        return QObject::tr("None");
    if (value.is_array()) {
        QStringList parts;
        for (const plynth::Json& item : value)
            parts << (item.is_string() ? fromCore(item.get<std::string>()) : fromCore(item.dump()));
        return parts.join(QStringLiteral(", "));
    }
    return fromCore(value.dump());
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

// A note line in the amber warning color, hidden until it has text.
QLabel* warningLabel(QWidget* parent)
{
    const Theme& t = Theme::instance();
    auto* label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setFont(t.uiFont(t.sizeSmall()));
    label->setStyleSheet(QStringLiteral("color: %1;").arg(t.signalAmber().name()));
    label->setWordWrap(true);
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

// Picks the button words for an integration: addresses, Fab zips, own files or files of another source.
ButtonWords wordsFor(const plynth::Integration& integration)
{
    if (integration.takesAddresses())
        return {QObject::tr("Import as asset"), QObject::tr("Import as material"),
                QObject::tr("Download the item at this address and add it to the asset catalog"),
                QObject::tr("Download the item at this address and add it to the materials catalog")};
    if (isFabIntegration(integration))
        return {QObject::tr("Import model zips..."), QObject::tr("Import material zips..."),
                QObject::tr("Pick one or more Fab model zips and add them to the asset catalog"),
                QObject::tr("Pick one or more Fab material zips and add them to the materials catalog")};
    if (isOwnFilesIntegration(integration))
        return {QObject::tr("Import asset..."), QObject::tr("Import material..."),
                QObject::tr("Pick model files and add them to the asset catalog"),
                QObject::tr("Pick texture files or a folder and add them to the materials catalog")};
    return {QObject::tr("Import assets..."), QObject::tr("Import materials..."),
            QObject::tr("Pick files and add them to the asset catalog"),
            QObject::tr("Pick files and add them to the materials catalog")};
}

}

// True for a source of the user's own files: it takes files and lists no store kinds.
bool isOwnFilesIntegration(const plynth::Integration& integration)
{
    return !integration.takesAddresses() && integration.storeSourceKinds().empty();
}

// True for the source that brings the Fab zip importer, which has its own check dialog.
bool isFabIntegration(const plynth::Integration& integration)
{
    const std::string fab(plynth::kFabImporterId);
    const auto has = [&fab](const std::vector<std::string>& ids) { return std::find(ids.begin(), ids.end(), fab) != ids.end(); };
    return has(integration.assetImporters()) || has(integration.materialImporters());
}

// Builds the cards from the integration registry.
IntegrationsPanel::IntegrationsPanel(LibraryHost& host, QWidget* parent) : QWidget(parent), host_(host)
{
    const Theme& t = Theme::instance();
    setToolTip(tr("Where assets and materials come from"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // The cards sit on the window's own Panel background, so a wide window shows one surface, not a darker strip.
    scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
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

// The card of the integration with this id, or null.
IntegrationsPanel::Card* IntegrationsPanel::cardFor(const QString& id)
{
    for (Card& card : list_)
        if (fromView(card.integration->id()) == id)
            return &card;
    return nullptr;
}

// Builds one card: title, state, options, import buttons and the site link.
void IntegrationsPanel::buildCard(const plynth::Integration& integration)
{
    const Theme& t = Theme::instance();
    const QString id = fromView(integration.id());
    const QString name = fromView(integration.name());
    list_.emplace_back();
    Card& card = list_.back();
    card.integration = &integration;

    auto* frame = new QFrame(cards_->parentWidget());
    frame->setObjectName(QStringLiteral("integrationCard"));
    frame->setStyleSheet(QStringLiteral("QFrame#integrationCard { background: %1; border: 1px solid %2; border-radius: %3px; }")
                             .arg(t.raised().name(), t.line().name())
                             .arg(t.radius()));
    frame->setToolTip(plainToolTip(tr("%1: %2").arg(name, fromView(integration.description()))));
    auto* box = new QVBoxLayout(frame);
    box->setContentsMargins(t.unit() * 3, t.unit() * 3, t.unit() * 3, t.unit() * 3);
    box->setSpacing(t.unit() * 2);

    auto* top = new QHBoxLayout();
    top->setSpacing(t.unit() * 2);
    auto* title = new QLabel(frame);
    title->setTextFormat(Qt::PlainText);
    title->setText(name);
    title->setFont(t.uiFont(t.sizeLarge(), QFont::DemiBold));
    title->setToolTip(tr("The name of this source"));
    top->addWidget(title, 1);
    card.state = new QLabel(frame);
    card.state->setTextFormat(Qt::PlainText);
    card.state->setFont(t.uiFont(t.sizeNormal(), QFont::DemiBold));
    card.state->setToolTip(tr("Whether this source is ready to use"));
    top->addWidget(card.state);
    box->addLayout(top);

    auto* description = new QLabel(frame);
    description->setTextFormat(Qt::PlainText);
    description->setText(fromView(integration.description()));
    description->setFont(t.uiFont(t.sizeNormal()));
    description->setWordWrap(true);
    description->setToolTip(tr("What this source does and what it needs"));
    box->addWidget(description);

    if (!integration.setupItem().empty()) {
        card.detail = caption(QString(), frame);
        card.detail->setToolTip(tr("What the setup check found and what to do next"));
        box->addWidget(card.detail);
        card.check = new QPushButton(tr("Check again"), frame);
        card.check->setToolTip(tr("Run the setup check again, including Blender and its add-ons"));
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
        note->setVisible(true);
        box->addWidget(note);
    }

    const ButtonWords words = wordsFor(integration);

    if (integration.takesAddresses()) {
        card.address = new QLineEdit(frame);
        card.address->setFont(t.monoFont(t.sizeNormal()));
        card.address->setPlaceholderText(tr("Paste a %1 address").arg(name));
        card.address->setClearButtonEnabled(true);
        card.address->setToolTip(tr("The address of one item, copied from its page in your browser"));
        box->addWidget(card.address);
        card.addressNote = warningLabel(frame);
        card.addressNote->setToolTip(tr("Why this address cannot be used"));
        box->addWidget(card.addressNote);
        connect(card.address, &QLineEdit::textChanged, this, [this, id]() {
            if (Card* c = cardFor(id))
                updateAddress(*c);
        });
    }

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(t.unit() * 2);
    if (!integration.assetImporters().empty()) {
        card.importAsset = new QPushButton(words.asset, frame);
        card.importAsset->setToolTip(words.assetTip);
        card.importAsset->setDefault(integration.takesAddresses());
        buttons->addWidget(card.importAsset);
    }
    if (!integration.materialImporters().empty()) {
        card.importMaterial = new QPushButton(words.material, frame);
        card.importMaterial->setToolTip(words.materialTip);
        buttons->addWidget(card.importMaterial);
    }
    buttons->addStretch(1);
    box->addLayout(buttons);
    const auto send = [this, id](bool material) {
        Card* c = cardFor(id);
        if (actions_ == nullptr || c == nullptr)
            return;
        if (c->address != nullptr)
            actions_->importAddress(id, c->address->text().trimmed(), material);
        else
            actions_->importFiles(id, material);
    };
    if (card.importAsset != nullptr)
        connect(card.importAsset, &QPushButton::clicked, this, [send]() { send(false); });
    if (card.importMaterial != nullptr)
        connect(card.importMaterial, &QPushButton::clicked, this, [send]() { send(true); });

    for (const plynth::OptionField& field : integration.optionFields())
        addField(card, box, field);
    if (!card.fields.empty()) {
        card.optionNote = warningLabel(frame);
        card.optionNote->setToolTip(tr("Why the options cannot be changed now"));
        box->addWidget(card.optionNote);
    }

    if (!integration.site().empty()) {
        auto* link = new QLabel(frame);
        link->setFont(t.uiFont(t.sizeSmall()));
        link->setTextFormat(Qt::RichText);
        link->setText(QStringLiteral("<a href=\"%1\">%2</a>")
                          .arg(fromView(integration.site()).toHtmlEscaped(),
                               tr("Open the %1 website").arg(name).toHtmlEscaped()));
        QPalette palette = link->palette();
        palette.setColor(QPalette::Link, t.signalBlue());
        link->setPalette(palette);
        link->setToolTip(tr("Open the official site in your browser"));
        const QString site = fromView(integration.site());
        connect(link, &QLabel::linkActivated, this, [site]() { QDesktopServices::openUrl(QUrl(site)); });
        box->addWidget(link);
    }

    cards_->addWidget(frame);
}

// Adds a captioned control for one saved option, drawn from what core says the option allows.
void IntegrationsPanel::addField(Card& card, QVBoxLayout* box, const plynth::OptionField& option)
{
    const Theme& t = Theme::instance();
    const QString id = fromView(card.integration->id());
    const QString key = fromCore(option.key);
    const QString tip = plainToolTip(fromCore(option.help));
    QWidget* parent = box->parentWidget();
    auto* row = new QHBoxLayout();
    row->setSpacing(t.unit() * 2);
    auto* name = new QLabel(parent);
    name->setTextFormat(Qt::PlainText);
    name->setText(fromCore(option.label));
    name->setFont(t.uiFont(t.sizeNormal()));
    name->setToolTip(tip);
    row->addWidget(name, 1);

    Field field;
    field.key = key;
    field.license = option.kind == plynth::OptionKind::License;
    field.minimum = option.minimum;
    field.maximum = option.maximum;
    if (option.kind == plynth::OptionKind::WholeNumber) {
        auto* number = new QSpinBox(parent);
        number->setFont(t.uiFont(t.sizeNormal()));
        number->setStyle(t.controlStyle());
        number->setRange(option.minimum, option.maximum);
        // Saves when the user is done typing, not at every digit.
        number->setKeyboardTracking(false);
        number->setToolTip(tip);
        WheelGuard::guard(number);
        connect(number, &QSpinBox::valueChanged, this, [this, id, key](int value) {
            if (refreshing_)
                return;
            if (Card* c = cardFor(id))
                saveOption(*c, key, QString::number(value));
        });
        row->addWidget(number);
        field.number = number;
    } else {
        auto* combo = new QComboBox(parent);
        combo->setFont(t.uiFont(t.sizeNormal()));
        combo->setStyle(t.controlStyle());
        combo->setToolTip(tip);
        WheelGuard::guard(combo);
        if (field.license)
            fillLicenses(combo);
        else
            for (const plynth::OptionChoice& choice : option.choices)
                combo->addItem(fromCore(choice.label), valueKey(choice.value));
        connect(combo, &QComboBox::activated, this, [this, id, key, combo](int index) {
            if (refreshing_ || combo->itemData(index, kExtraRole).toBool())
                return;
            if (Card* c = cardFor(id))
                saveOption(*c, key, combo->itemData(index).toString());
        });
        row->addWidget(combo);
        field.combo = combo;
    }
    box->addLayout(row);
    card.fields.push_back(field);
}

// Fills a license drop down from the catalog's license list.
void IntegrationsPanel::fillLicenses(QComboBox* combo)
{
    combo->clear();
    if (host_.catalog() == nullptr)
        return;
    for (const plynth::LicenseEntry& entry : host_.catalog()->licenses())
        combo->addItem(fromCore(entry.name), valueKey(plynth::Json(entry.id)));
}

// Saves one changed option at once and shows the refusal in amber when core says no.
void IntegrationsPanel::saveOption(Card& card, const QString& key, const QString& valueJson)
{
    const plynth::Json value = plynth::Json::parse(toCore(valueJson), nullptr, false);
    if (value.is_discarded())
        return;
    plynth::Json changes = plynth::Json::object();
    changes[toCore(key)] = value;
    const plynth::Status status = plynth::setIntegrationOptions(host_.paths(), card.integration->id(), changes);
    if (status.ok())
        return;
    refresh();
    if (card.optionNote != nullptr) {
        card.optionNote->setText(fromCore(status.message));
        card.optionNote->setVisible(true);
    }
}

// Shows a saved value in its control. A value the list lacks gets an entry of its own, and nothing is saved.
void IntegrationsPanel::showValue(Field& field, const QString& valueJson)
{
    const plynth::Json value = plynth::Json::parse(toCore(valueJson), nullptr, false);
    if (field.number != nullptr) {
        if (!value.is_number_integer())
            return;
        const long long whole = value.get<long long>();
        const int shown = static_cast<int>(std::clamp<long long>(whole, INT_MIN, INT_MAX));
        field.number->setRange(std::min(field.minimum, shown), std::max(field.maximum, shown));
        field.number->setValue(shown);
        return;
    }
    QComboBox* combo = field.combo;
    for (int i = combo->count() - 1; i >= 0; --i)
        if (combo->itemData(i, kExtraRole).toBool())
            combo->removeItem(i);
    if (value.is_discarded()) {
        combo->setCurrentIndex(-1);
        return;
    }
    const QString wanted = valueKey(value);
    int index = combo->findData(wanted);
    if (index < 0) {
        combo->addItem(shownValue(value), wanted);
        index = combo->count() - 1;
        combo->setItemData(index, true, kExtraRole);
        combo->setItemData(index, tr("The saved value. It is not one of the usual choices."), Qt::ToolTipRole);
    }
    combo->setCurrentIndex(index);
}

// Shows the saved values in the controls and turns them on or off with the library.
void IntegrationsPanel::refresh()
{
    refreshing_ = true;
    const bool library = host_.hasLibrary();
    const plynth::IntegrationOptionsRead read = library ? plynth::readIntegrationOptions(host_.paths())
                                                         : plynth::IntegrationOptionsRead{};
    const bool readable = library && read.status.ok();
    const plynth::Json saved = readable ? read.saved : plynth::Json::object();
    for (Card& card : list_) {
        const plynth::Json options = plynth::integrationOptions(*card.integration, saved);
        for (Field& field : card.fields) {
            QWidget* control = field.combo != nullptr ? static_cast<QWidget*>(field.combo) : field.number;
            control->setEnabled(readable);
            if (field.license)
                fillLicenses(field.combo);
            const auto it = options.find(toCore(field.key));
            showValue(field, it != options.end() ? fromCore(it->dump()) : QString());
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
        const bool addressOk = card.address == nullptr || card.addressOk;
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

// Checks the address field as it changes. The integration itself says what it can use.
void IntegrationsPanel::updateAddress(Card& card)
{
    const QString text = card.address->text().trimmed();
    QString problem;
    if (!text.isEmpty()) {
        const plynth::Status status = card.integration->checkAddress(toCore(text));
        if (!status.ok())
            problem = fromCore(status.message);
    }
    card.addressOk = !text.isEmpty() && problem.isEmpty();
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

// Runs the first check when the window is first shown.
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
