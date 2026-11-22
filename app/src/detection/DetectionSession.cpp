#include "DetectionSession.h"

#include "detection/DetectionRunner.h"
#include "tools/ToolController.h"

#include <plynth/core/Document.h>
#include <plynth/core/commands/DetectionCommands.h>
#include <plynth/core/detection/Detector.h>

#include <QStringList>
#include <QTimer>

#include <cmath>
#include <set>

namespace plynth::app {

namespace {

constexpr int kLiveDelayMs = 150;

// Turns a core sentence into Qt text.
QString fromCore(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// "1 wall" or "5 walls".
QString counted(std::size_t count, const QString& one, const QString& many)
{
    return QStringLiteral("%1 %2").arg(count).arg(count == 1 ? one : many);
}

}

// Makes the runner and the timer that waits for the sliders to rest.
DetectionSession::DetectionSession(ToolController& host, QObject* parent)
    : QObject(parent), host_(host), runner_(new DetectionRunner(this)), timer_(new QTimer(this)),
      detectorId_(plynth::kDefaultDetectorId)
{
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &DetectionSession::launch);
    connect(runner_, &DetectionRunner::finished, this, &DetectionSession::onFinished);
}

// Stops the runner first, so no result arrives while the session is being taken apart.
DetectionSession::~DetectionSession()
{
    runner_->cancel();
}

// Starts detection when the tool is switched on and throws everything away when it is switched off.
void DetectionSession::setActive(bool active)
{
    if (active_ == active)
        return;
    active_ = active;
    if (active_) {
        restart(0);
        return;
    }
    timer_->stop();
    runner_->cancel();
    clearResult();
    state_ = State::Idle;
    statusText_.clear();
    emit changed();
}

// Keeps new settings and runs detection again shortly after the last change.
void DetectionSession::setParams(const plynth::DetectionParams& params)
{
    if (params == params_)
        return;
    params_ = params;
    if (active_)
        restart(kLiveDelayMs);
    emit changed();
}

// Puts every setting back to its default.
void DetectionSession::resetParams()
{
    setParams(plynth::DetectionParams{});
}

// Chooses the detector.
void DetectionSession::setDetectorId(const std::string& id)
{
    if (id == detectorId_)
        return;
    detectorId_ = id;
    if (active_)
        restart(kLiveDelayMs);
    emit changed();
}

// Stops what runs now and starts again after the delay.
void DetectionSession::restart(int delayMs)
{
    runner_->cancel();
    state_ = State::Running;
    statusText_ = tr("Detecting...");
    timer_->start(delayMs);
    emit changed();
}

// Asks core for the image and scale of the floor and hands the work to the runner.
void DetectionSession::launch()
{
    const plynth::Floor* floor = host_.floor();
    if (!active_ || floor == nullptr) {
        clearResult();
        state_ = State::Idle;
        statusText_.clear();
        emit changed();
        return;
    }
    plynth::Document& document = host_.document();
    const plynth::DetectionSource source = plynth::detectionSource(
        document.plan(), document.hasFolder() ? document.folder() : std::filesystem::path(), floor->id);
    if (!source.status.ok()) {
        clearResult();
        state_ = State::Failed;
        statusText_ = fromCore(source.status.message);
        emit changed();
        return;
    }
    runner_->start(source.imageFile, source.pixelsPerMetre, params_, detectorId_);
}

// Keeps the result of the latest detection, or shows why it failed.
void DetectionSession::onFinished(std::shared_ptr<const plynth::DetectionResult> result)
{
    if (!active_)
        return;
    if (!result->status.ok()) {
        clearResult();
        state_ = State::Failed;
        statusText_ = fromCore(result->status.message);
        emit changed();
        return;
    }
    raw_ = std::move(result);
    state_ = State::Done;
    remark();
}

// Marks the proposals against the walls and rooms the floor has now, and writes the status line.
void DetectionSession::remark()
{
    if (raw_ == nullptr)
        return;
    marked_ = *raw_;
    if (const plynth::Floor* floor = host_.floor())
        plynth::markExisting(*floor, marked_);
    const plynth::ProposalCounts counts = plynth::countProposals(marked_);

    QStringList found;
    found << counted(counts.walls, tr("wall"), tr("walls"));
    if (marked_.params.rooms)
        found << counted(counts.rooms, tr("room"), tr("rooms"));
    if (marked_.params.openings)
        found << counted(counts.openings, tr("opening"), tr("openings"));
    QString text = tr("%1 in %2 ms").arg(found.join(QStringLiteral(", "))).arg(std::lround(marked_.elapsedMs));
    if (counts.existingWalls > 0)
        text += QLatin1Char('\n') + tr("%1 already drawn").arg(counted(counts.existingWalls, tr("wall"), tr("walls")));
    if (counts.overlappingWalls > 0)
        text += QLatin1Char('\n') + tr("%1 overlap drawn walls and are left out")
                                        .arg(counted(counts.overlappingWalls, tr("wall"), tr("walls")));
    statusText_ = text;
    emit changed();
}

// Forgets the proposals.
void DetectionSession::clearResult()
{
    raw_.reset();
    marked_ = plynth::DetectionResult{};
}

// There is something to add when a wall is new, or a room is new and rooms are on.
bool DetectionSession::canAccept() const
{
    if (!active_ || state_ != State::Done || raw_ == nullptr)
        return false;
    const plynth::ProposalCounts counts = plynth::countProposals(marked_);
    return counts.newWalls > 0 || (marked_.params.rooms && counts.newRooms > 0) ||
           (marked_.params.openings && counts.newOpenings > 0);
}

// Runs the accept command and says what it added.
bool DetectionSession::accept()
{
    const plynth::Floor* floor = host_.floor();
    if (!canAccept() || floor == nullptr)
        return false;
    auto command = std::make_unique<plynth::ApplyDetectionCommand>(floor->id, marked_, marked_.params.rooms);
    const plynth::ApplyDetectionCommand* raw = command.get();
    if (!host_.run(std::move(command)).ok())
        return false;
    host_.showNotice(tr("Added %1, %2 and %3")
                         .arg(counted(raw->wallIds().size(), tr("wall"), tr("walls")),
                              counted(raw->openingIds().size(), tr("opening"), tr("openings")),
                              counted(raw->roomIds().size(), tr("room"), tr("rooms"))));
    return true;
}

// Builds what the canvas draws from the new proposals only.
ProposalOverlay DetectionSession::overlay() const
{
    ProposalOverlay out;
    if (!active_ || state_ == State::Failed || raw_ == nullptr)
        return out;
    const double ppm = marked_.pixelsPerMetre;
    const plynth::Floor* floor = host_.floor();
    const plynth::ImageOffset offset = floor != nullptr ? floor->imageOffset : plynth::ImageOffset{};
    const auto at = [ppm, offset](const plynth::Vec2& p) {
        return QPointF((p.x + offset.x) * ppm, (p.y + offset.y) * ppm);
    };

    std::set<std::size_t> used;
    for (const plynth::ProposedWall& wall : marked_.walls) {
        if (wall.state != plynth::ProposalState::New)
            continue;
        out.walls.append({at(marked_.points[wall.from]), at(marked_.points[wall.to]), wall.thickness * ppm});
        used.insert(wall.from);
        used.insert(wall.to);
    }
    for (std::size_t index : used)
        out.points.append(at(marked_.points[index]));
    for (const plynth::ProposedRoom& room : marked_.rooms) {
        if (room.state != plynth::ProposalState::New)
            continue;
        QVector<QPolygonF> rings;
        for (const std::vector<plynth::Vec2>& ring : room.rings) {
            QPolygonF polygon;
            for (const plynth::Vec2& corner : ring)
                polygon << at(corner);
            rings.append(polygon);
        }
        out.rooms.append(rings);
    }
    for (const plynth::ProposedOpening& opening : marked_.openings) {
        const plynth::ProposedWall& wall = marked_.walls[opening.wall];
        if (opening.state != plynth::ProposalState::New)
            continue;
        const QPointF from = at(marked_.points[wall.from]);
        const QPointF to = at(marked_.points[wall.to]);
        const QPointF delta = to - from;
        const double length = std::hypot(delta.x(), delta.y());
        if (length <= 0.0)
            continue;
        const QPointF dir = delta / length;
        ProposalOpening mark;
        mark.centre = from + dir * (opening.offset * ppm);
        mark.direction = dir;
        mark.thickness = wall.thickness * ppm;
        mark.width = opening.width * ppm;
        mark.kind = opening.type == plynth::OpeningType::Door ? 1 : opening.type == plynth::OpeningType::Window ? 2 : 0;
        mark.hingeAtEnd = opening.hinge == plynth::HingeEnd::End;
        mark.swingRight = opening.swing == plynth::WallSide::Right;
        out.openings.append(mark);
    }
    return out;
}

// Starts again on the image of the floor that is shown now.
void DetectionSession::floorChanged()
{
    if (!active_)
        return;
    clearResult();
    restart(0);
}

// A new image or scale runs detection again. Any other edit only re-checks what the floor already has.
void DetectionSession::documentChanged(unsigned changes)
{
    if (!active_)
        return;
    if (changes & (plynth::Change::FloorImage | plynth::Change::Scale | plynth::Change::Floors | plynth::Change::Folder)) {
        clearResult();
        restart(0);
        return;
    }
    if (changes & (plynth::Change::Points | plynth::Change::Walls | plynth::Change::Rooms))
        remark();
}

}
