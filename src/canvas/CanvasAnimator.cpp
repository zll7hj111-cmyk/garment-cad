#include "CanvasAnimator.h"

#include <QGraphicsItem>
#include <cmath>

namespace {

/// OutCubic easing: fast start, gentle finish (CAD convention).
double easeOutCubic(double t)
{
    const double t1 = 1.0 - t;
    return 1.0 - t1 * t1 * t1;
}

/// Linear interpolation for doubles.
double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

/// Linear RGB interpolation for QColor.
QColor lerpColor(const QColor& a, const QColor& b, double t)
{
    return QColor(
        std::lround(lerp(a.red(),   b.red(),   t)),
        std::lround(lerp(a.green(), b.green(), t)),
        std::lround(lerp(a.blue(),  b.blue(),  t)),
        std::lround(lerp(a.alpha(), b.alpha(), t)));
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CanvasAnimator::CanvasAnimator(const CanvasStyle* style, QObject* parent)
    : QObject(parent)
    , m_style(style)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(16);  // ~60 fps
    connect(m_timer, &QTimer::timeout, this, &CanvasAnimator::tick);
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void CanvasAnimator::setState(QGraphicsItem* owner, const QUuid& entityId, EntityState target)
{
    auto& map = m_entries[owner];
    auto it = map.find(entityId);

    if (it != map.end()) {
        // Already has an entry — if target is same as current "to", ignore.
        if (it->to == target)
            return;
        // Start new transition from wherever we currently are.
        // We treat the current interpolated visual as the new "from" by
        // snapping from = to of the previous transition at current progress.
        // Simpler: set from = current effective state (to if progress >= 1,
        // otherwise keep from and adjust progress). For correctness we just
        // restart from the current "to" (the state we're heading toward).
        it->from = it->to;
        it->to   = target;
        it->progress = 0.0;
    } else {
        // First time: assume entity was at Normal.
        Entry e;
        e.from     = EntityState::Normal;
        e.to       = target;
        e.progress = (target == EntityState::Normal) ? 1.0 : 0.0;
        map.insert(entityId, e);
    }

    if (m_style->transitionMs() <= 0) {
        // No animation (e.g. print theme) — snap immediately.
        map[entityId].progress = 1.0;
        emit invalidationRequested(owner);
        return;
    }

    // Hover state snaps IMMEDIATELY (no transition): hover toggles at mouse
    // frequency — an animated hover keeps the 16 ms timer running and forces
    // a full-viewport repaint on every tick, which is the dominant jank
    // source on slow (software-GL / VM) machines. Selection/lock states are
    // low-frequency and keep their transition.
    if (target == EntityState::Hover) {
        map[entityId].progress = 1.0;
        emit invalidationRequested(owner);
        return;
    }

    ensureTimerRunning();
}

EntityPaintParams CanvasAnimator::lineParams(const QGraphicsItem* owner, const QUuid& entityId,
                                             const QColor& baseColor, double baseWidth) const
{
    EntityPaintParams pp;

    const auto ownerIt = m_entries.constFind(owner);
    if (ownerIt == m_entries.constEnd()) {
        // No animation state — resolve as Normal.
        pp.lineColor  = m_style->lineColor(EntityState::Normal, baseColor);
        pp.lineWidth  = m_style->lineWidth(EntityState::Normal, baseWidth);
        pp.labelColor = m_style->labelColor(EntityState::Normal, false);
        pp.lengthLabelColor = m_style->labelColor(EntityState::Normal, true);
        return pp;
    }

    const auto entryIt = ownerIt->constFind(entityId);
    if (entryIt == ownerIt->constEnd()) {
        pp.lineColor  = m_style->lineColor(EntityState::Normal, baseColor);
        pp.lineWidth  = m_style->lineWidth(EntityState::Normal, baseWidth);
        pp.labelColor = m_style->labelColor(EntityState::Normal, false);
        pp.lengthLabelColor = m_style->labelColor(EntityState::Normal, true);
        return pp;
    }

    const Entry& e = entryIt.value();
    const double t = easeOutCubic(e.progress);

    const QColor fromColor = m_style->lineColor(e.from, baseColor);
    const QColor toColor   = m_style->lineColor(e.to, baseColor);
    pp.lineColor = lerpColor(fromColor, toColor, t);

    const double fromW = m_style->lineWidth(e.from, baseWidth);
    const double toW   = m_style->lineWidth(e.to, baseWidth);
    pp.lineWidth = lerp(fromW, toW, t);

    const QColor fromLabel = m_style->labelColor(e.from, false);
    const QColor toLabel   = m_style->labelColor(e.to, false);
    pp.labelColor = lerpColor(fromLabel, toLabel, t);

    const QColor fromLenLabel = m_style->labelColor(e.from, true);
    const QColor toLenLabel   = m_style->labelColor(e.to, true);
    pp.lengthLabelColor = lerpColor(fromLenLabel, toLenLabel, t);

    return pp;
}

EntityPaintParams CanvasAnimator::pointParams(const QGraphicsItem* owner, const QUuid& entityId,
                                              bool auxiliary) const
{
    EntityPaintParams pp;

    const auto ownerIt = m_entries.constFind(owner);
    if (ownerIt == m_entries.constEnd()) {
        pp.pointFill   = m_style->pointColor(EntityState::Normal, auxiliary);
        pp.pointRadius = m_style->pointRadius(EntityState::Normal, auxiliary);
        pp.labelColor  = m_style->labelColor(EntityState::Normal, false);
        return pp;
    }

    const auto entryIt = ownerIt->constFind(entityId);
    if (entryIt == ownerIt->constEnd()) {
        pp.pointFill   = m_style->pointColor(EntityState::Normal, auxiliary);
        pp.pointRadius = m_style->pointRadius(EntityState::Normal, auxiliary);
        pp.labelColor  = m_style->labelColor(EntityState::Normal, false);
        return pp;
    }

    const Entry& e = entryIt.value();
    const double t = easeOutCubic(e.progress);

    pp.pointFill = lerpColor(m_style->pointColor(e.from, auxiliary),
                             m_style->pointColor(e.to, auxiliary), t);
    pp.pointRadius = lerp(m_style->pointRadius(e.from, auxiliary),
                          m_style->pointRadius(e.to, auxiliary), t);
    pp.labelColor = lerpColor(m_style->labelColor(e.from, false),
                              m_style->labelColor(e.to, false), t);

    return pp;
}

bool CanvasAnimator::isAnimating(const QGraphicsItem* owner, const QUuid& entityId) const
{
    const auto ownerIt = m_entries.constFind(owner);
    if (ownerIt == m_entries.constEnd())
        return false;
    const auto entryIt = ownerIt->constFind(entityId);
    if (entryIt == ownerIt->constEnd())
        return false;
    return entryIt->progress < 1.0;
}

void CanvasAnimator::removeOwner(QGraphicsItem* owner)
{
    m_entries.remove(owner);
    stopTimerIfIdle();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void CanvasAnimator::tick()
{
    const double dt = static_cast<double>(m_timer->interval()) /
                      static_cast<double>(m_style->transitionMs());

    bool anyActive = false;
    // Collect owners that need repaint.
    QList<const QGraphicsItem*> dirtyOwners;

    for (auto ownerIt = m_entries.begin(); ownerIt != m_entries.end(); ++ownerIt) {
        bool ownerDirty = false;
        auto& map = ownerIt.value();
        for (auto it = map.begin(); it != map.end(); ++it) {
            Entry& e = it.value();
            if (e.progress < 1.0) {
                e.progress = std::min(1.0, e.progress + dt);
                ownerDirty = true;
                if (e.progress < 1.0)
                    anyActive = true;
            }
        }
        if (ownerDirty)
            dirtyOwners.append(ownerIt.key());
    }

    for (const QGraphicsItem* item : dirtyOwners)
        emit invalidationRequested(item);

    if (!anyActive)
        m_timer->stop();
}

void CanvasAnimator::ensureTimerRunning()
{
    if (!m_timer->isActive())
        m_timer->start();
}

void CanvasAnimator::stopTimerIfIdle()
{
    for (const auto& map : std::as_const(m_entries)) {
        for (const auto& e : map) {
            if (e.progress < 1.0)
                return;  // Still animating
        }
    }
    m_timer->stop();
}
