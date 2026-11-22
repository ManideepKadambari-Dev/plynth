#include "PreviewView.h"

#include "GraphicsProbe.h"
#include "theme/Theme.h"

#include <plynth/core/BuildGeometry.h>
#include <plynth/core/BuildJob.h>
#include <plynth/core/BuildPreview.h>
#include <plynth/core/Roofs.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace plynth::app {

namespace {

constexpr float kBoundsPad = 0.15f;

// The folder docs/ of the install or the source tree, found by walking up from the program. Empty if none.
QString helpFolder()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("docs/USER_GUIDE.md"))))
            return dir.filePath(QStringLiteral("docs"));
        if (!dir.cdUp())
            break;
    }
    return QString();
}

// Draws a small icon for a button in one color: a square seen from above, or a cube seen from the front right.
QIcon viewIcon(bool top, const QColor& color, int side)
{
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.5));
    p.setBrush(Qt::NoBrush);
    const qreal m = side * 0.25;
    const QRectF box(m, m, side - 2 * m, side - 2 * m);
    if (top) {
        p.drawRect(box);
        p.drawRect(box.adjusted(side * 0.14, side * 0.14, -side * 0.14, -side * 0.14));
    } else {
        const QPointF c(side / 2.0, side / 2.0);
        const qreal r = side * 0.28;
        QPolygonF hex;
        for (int i = 0; i < 6; ++i)
            hex << QPointF(c.x() + r * std::cos(std::numbers::pi / 6 + i * std::numbers::pi / 3), c.y() + r * std::sin(std::numbers::pi / 6 + i * std::numbers::pi / 3));
        p.drawPolygon(hex);
        p.drawLine(c, hex[1]);
        p.drawLine(c, hex[3]);
        p.drawLine(c, hex[5]);
    }
    return QIcon(pixmap);
}

}

// Builds the scene and the two view buttons, or the message when the graphics cannot run it.
PreviewView::PreviewView(QWidget* parent) : QWidget(parent)
{
    const Theme& t = Theme::instance();
    setProperty("tourId", QStringLiteral("preview-3d"));
    setToolTip(tr("The building in 3D. Drag to orbit, middle or right drag to pan, wheel to zoom"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);

    const plynth::GraphicsProbe& graphics = appGraphics();
    if (!graphics.ok) {
        showUnavailable(QString::fromStdString(graphics.problem));
        return;
    }

    scene_ = new ModelView(true, true, this);
    scene_->useBuiltMaterials(true);
    if (scene_->status() == QQuickWidget::Error) {
        const QString problem = scene_->errorText();
        delete scene_;
        scene_ = nullptr;
        showUnavailable(problem);
        return;
    }
    column->addWidget(scene_);
    scene_->clearModel(tr("Build the plan to see it in 3D. (Ctrl+B)"));

    const int side = t.previewButton();
    const QString style = QStringLiteral("QToolButton { background: %1; border: 1px solid %2; border-radius: %3px; }"
                                         "QToolButton:hover { border-color: %4; }"
                                         "QToolButton:checked { border-color: %5; }")
                              .arg(t.raised().name(), t.line().name())
                              .arg(t.radius())
                              .arg(t.muted().name(), t.signalBlue().name());
    auto makeButton = [&](bool top, const QString& tip, const QString& tourId) {
        auto* button = new QToolButton(this);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setAutoRaise(false);
        button->setFixedSize(side, side);
        button->setIconSize(QSize(side - 2, side - 2));
        button->setIcon(viewIcon(top, t.text(), side - 2));
        button->setToolTip(tip);
        button->setProperty("tourId", tourId);
        button->setStyleSheet(style);
        return button;
    };
    topButton_ = makeButton(true, tr("Look straight down at the building"), QStringLiteral("preview-top"));
    angleButton_ = makeButton(false, tr("Look at the building from the front right, as in the thumbnails"),
                              QStringLiteral("preview-three-quarter"));
    angleButton_->setChecked(true);
    connect(topButton_, &QToolButton::clicked, this, [this]() { setPreset(ModelView::Preset::Top); });
    connect(angleButton_, &QToolButton::clicked, this, [this]() { setPreset(ModelView::Preset::ThreeQuarter); });
    topButton_->raise();
    angleButton_->raise();
    placeButtons();
}

// Replaces the scene with the reason the 3D view cannot run and a link to the help page.
void PreviewView::showUnavailable(const QString& problem)
{
    const Theme& t = Theme::instance();
    unavailable_ = new QLabel(this);
    unavailable_->setAlignment(Qt::AlignCenter);
    unavailable_->setWordWrap(true);
    unavailable_->setFont(t.uiFont(t.sizeNormal()));
    unavailable_->setTextFormat(Qt::RichText);
    unavailable_->setStyleSheet(QStringLiteral("background: %1; color: %2; padding: %3px;")
                                    .arg(t.ground().name(), t.signalAmber().name())
                                    .arg(t.unit() * 8));
    unavailable_->setText(tr("The 3D view cannot start. %1<br><a style=\"color: %2;\" href=\"help\">If the 3D view "
                             "stays empty</a>")
                              .arg(problem.toHtmlEscaped(), t.signalBlue().name()));
    connect(unavailable_, &QLabel::linkActivated, this, []() {
        const QString folder = helpFolder();
        if (!folder.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder + QStringLiteral("/USER_GUIDE.md")));
    });
    layout()->addWidget(unavailable_);
}

// Puts the two buttons in the top right corner.
void PreviewView::placeButtons()
{
    if (topButton_ == nullptr)
        return;
    const Theme& t = Theme::instance();
    const int gap = t.unit() * 2;
    const int side = t.previewButton();
    angleButton_->move(width() - gap - side, gap);
    topButton_->move(width() - gap - 2 * side - t.unit(), gap);
    topButton_->raise();
    angleButton_->raise();
}

// Keeps the buttons in the corner.
void PreviewView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeButtons();
}

// Computes the box of the whole building from the walls, with a margin.
void PreviewView::buildingBounds(const plynth::Plan& plan, QVector3D* low, QVector3D* high, int lastFloor,
                                 bool withRoofs)
{
    const std::vector<double> levels = plynth::floorElevations(plan);
    bool any = false;
    float minX = 0, minZ = 0, maxX = 0, maxZ = 0, top = 0;
    const size_t count = lastFloor < 0 ? plan.floors.size() : std::min(plan.floors.size(), static_cast<size_t>(lastFloor) + 1);
    for (size_t f = 0; f < count; ++f) {
        const plynth::Floor& floor = plan.floors[f];
        const double level = f < levels.size() ? levels[f] : 0.0;
        for (const plynth::Wall& wall : floor.walls) {
            const plynth::Point* a = plynth::findPoint(floor, wall.from);
            const plynth::Point* b = plynth::findPoint(floor, wall.to);
            if (a == nullptr || b == nullptr)
                continue;
            const double reach = wall.thickness / 2 + std::abs(wall.bulge);
            for (const plynth::Point* p : {a, b}) {
                const float x0 = static_cast<float>(p->x - reach);
                const float x1 = static_cast<float>(p->x + reach);
                const float z0 = static_cast<float>(p->y - reach);
                const float z1 = static_cast<float>(p->y + reach);
                minX = any ? std::min(minX, x0) : x0;
                maxX = any ? std::max(maxX, x1) : x1;
                minZ = any ? std::min(minZ, z0) : z0;
                maxZ = any ? std::max(maxZ, z1) : z1;
                any = true;
            }
            top = std::max(top, static_cast<float>(level + wall.height.value_or(floor.ceilingHeight)));
        }
        for (const plynth::Item& item : floor.items) {
            const float reach = static_cast<float>(std::hypot(item.size.width, item.size.depth) / 2);
            const float x0 = static_cast<float>(item.x) - reach;
            const float x1 = static_cast<float>(item.x) + reach;
            const float z0 = static_cast<float>(item.y) - reach;
            const float z1 = static_cast<float>(item.y) + reach;
            minX = any ? std::min(minX, x0) : x0;
            maxX = any ? std::max(maxX, x1) : x1;
            minZ = any ? std::min(minZ, z0) : z0;
            maxZ = any ? std::max(maxZ, z1) : z1;
            any = true;
            top = std::max(top, static_cast<float>(level + item.z + item.size.height));
        }
        if (!withRoofs)
            continue;
        for (const plynth::RoofBuild& roof : plynth::floorRoofs(plan, f, levels).roofs) {
            for (const plynth::RoofFace& face : roof.faces) {
                for (const plynth::Vec3& v : face.outline) {
                    const float x = static_cast<float>(v.x);
                    const float z = static_cast<float>(-v.y);
                    minX = any ? std::min(minX, x) : x;
                    maxX = any ? std::max(maxX, x) : x;
                    minZ = any ? std::min(minZ, z) : z;
                    maxZ = any ? std::max(maxZ, z) : z;
                    any = true;
                    top = std::max(top, static_cast<float>(v.z));
                }
            }
        }
    }
    if (!any) {
        *low = QVector3D(-1, 0, -1);
        *high = QVector3D(1, 1, 1);
        return;
    }
    *low = QVector3D(minX - kBoundsPad, 0.0f, minZ - kBoundsPad);
    *high = QVector3D(maxX + kBoundsPad, top, maxZ + kBoundsPad);
}

// Loads building.fbx of the project, or shows the empty state.
void PreviewView::setProject(const std::filesystem::path& folder, const plynth::Plan& plan)
{
    fbx_.clear();
    if (!folder.empty())
        fbx_ = folder / std::string(plynth::kBuildFolderName) / std::string(plynth::kBuildFbxFileName);
    loadFile(plan);
}

// Loads the file again, because a build wrote a new one.
void PreviewView::reload(const plynth::Plan& plan)
{
    loadFile(plan);
}

// Reads the preview parts of the build that holds this fbx.
QList<ModelView::Part> PreviewView::readParts(const std::filesystem::path& fbx, const std::vector<std::string>& floorIds)
{
    QList<ModelView::Part> list;
    const plynth::PreviewPartsRead read = plynth::readPreviewParts(fbx.parent_path());
    if (!read.status.ok())
        return list;
    for (const plynth::PreviewPart& part : read.parts) {
        const auto it = std::find(floorIds.begin(), floorIds.end(), part.floor);
        if (it == floorIds.end())
            continue;
        ModelView::Part out;
        out.file = QUrl::fromLocalFile(QString::fromStdWString(part.file.wstring()));
        out.floorIndex = static_cast<int>(it - floorIds.begin());
        out.roof = part.kind == plynth::PreviewPartKind::Roof;
        list.append(out);
    }
    return list;
}

// Gives the scene the parts (or building.fbx without parts) and the bounds of the plan, or the empty state
// when the file is missing.
void PreviewView::loadFile(const plynth::Plan& plan)
{
    if (scene_ == nullptr)
        return;
    std::error_code ignored;
    if (fbx_.empty() || !std::filesystem::exists(fbx_, ignored)) {
        scene_->clearModel(tr("Build the plan to see it in 3D. (Ctrl+B)"));
        return;
    }
    floorIds_.clear();
    for (const plynth::Floor& floor : plan.floors)
        floorIds_.push_back(floor.id);
    buildingBounds(plan, &low_, &high_);
    cutHigh_.clear();
    for (size_t f = 0; f < plan.floors.size(); ++f) {
        QVector3D lo;
        QVector3D hi;
        buildingBounds(plan, &lo, &hi, static_cast<int>(f), false);
        cutHigh_.push_back(hi);
    }
    QList<ModelView::Part> parts = readParts(fbx_, floorIds_);
    if (parts.isEmpty())
        parts.append(ModelView::Part{QUrl::fromLocalFile(QString::fromStdWString(fbx_.wstring())), 0, false});
    scene_->setParts(parts, low_, high_);
    applyFilter();
}

// Passes the roof and cut choices to the scene with the bounds that frame what stays.
void PreviewView::applyFilter()
{
    if (scene_ == nullptr || scene_->partCount() == 0)
        return;
    int cut = -1;
    if (!cutFloorId_.isEmpty()) {
        const auto it = std::find(floorIds_.begin(), floorIds_.end(), cutFloorId_.toStdString());
        if (it != floorIds_.end())
            cut = static_cast<int>(it - floorIds_.begin());
    }
    QVector3D high = high_;
    if (cut >= 0 && static_cast<size_t>(cut) < cutHigh_.size())
        high = QVector3D(high_.x(), cutHigh_[static_cast<size_t>(cut)].y(), high_.z());
    scene_->setPartFilter(showRoofs_, cut, low_, high);
}

// Remembers the roof and cut choices and applies them.
void PreviewView::setRoofOptions(bool showRoofs, const QString& cutFloorId)
{
    showRoofs_ = showRoofs;
    cutFloorId_ = cutFloorId;
    applyFilter();
}

// Frames the building.
void PreviewView::fit()
{
    if (scene_ != nullptr)
        scene_->fit();
}

// Switches the camera angle and the checked button.
void PreviewView::setPreset(ModelView::Preset preset)
{
    if (scene_ == nullptr)
        return;
    topButton_->setChecked(preset == ModelView::Preset::Top);
    angleButton_->setChecked(preset == ModelView::Preset::ThreeQuarter);
    scene_->setPreset(preset);
    emit presetChanged();
}

// The angle the camera was last set to.
ModelView::Preset PreviewView::preset() const
{
    return scene_ != nullptr ? scene_->preset() : ModelView::Preset::ThreeQuarter;
}

// True when a model is loaded or loading.
bool PreviewView::hasModel() const
{
    return scene_ != nullptr && !scene_->modelFile().isEmpty();
}

// True when there is nothing to wait for or the model has been drawn.
bool PreviewView::pictureReady() const
{
    return scene_ == nullptr || scene_->pictureReady();
}

}
