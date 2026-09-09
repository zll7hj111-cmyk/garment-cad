#include "Block.h"

#include <cmath>

#include "geometry/Angle.h"

namespace cad::param {
// --- Transform2D ---

geo::Vec2 Transform2D::toWorld(const geo::Vec2& local) const
{
    // Rotate then translate
    double c = std::cos(rotation);
    double s = std::sin(rotation);
    return {
        origin.x + local.x * c - local.y * s,
        origin.y + local.x * s + local.y * c
    };
}

geo::Vec2 Transform2D::toLocal(const geo::Vec2& world) const
{
    // Translate then inverse-rotate
    double dx = world.x - origin.x;
    double dy = world.y - origin.y;
    double c = std::cos(-rotation);
    double s = std::sin(-rotation);
    return {
        dx * c - dy * s,
        dx * s + dy * c
    };
}


ParamPoint* Block::findPoint(const QUuid& pointId)
{
    // Defensive, symmetric with findSegment(): the index can lag only if
    // someone mutated `points` without addPoint()/rebuildPointIndex().
    if (m_pointIndex.size() != static_cast<int>(points.size()))
        rebuildPointIndex();
    auto it = m_pointIndex.find(pointId);
    if (it == m_pointIndex.end()) return nullptr;
    return &points[it.value()];
}

const ParamPoint* Block::findPoint(const QUuid& pointId) const
{
    if (m_pointIndex.size() != static_cast<int>(points.size()))
        rebuildPointIndex();
    auto it = m_pointIndex.find(pointId);
    if (it == m_pointIndex.end()) return nullptr;
    return &points[it.value()];
}

Segment* Block::findSegment(const QUuid& segmentId)
{
    // Defensive: the index can lag only if someone mutated `segments` without
    // going through addSegment()/rebuildSegmentIndex() — resync by size.
    if (m_segmentIndex.size() != static_cast<int>(segments.size()))
        rebuildSegmentIndex();
    auto it = m_segmentIndex.find(segmentId);
    if (it == m_segmentIndex.end()) return nullptr;
    return &segments[it.value()];
}

const Segment* Block::findSegment(const QUuid& segmentId) const
{
    if (m_segmentIndex.size() != static_cast<int>(segments.size()))
        rebuildSegmentIndex();
    auto it = m_segmentIndex.find(segmentId);
    if (it == m_segmentIndex.end()) return nullptr;
    return &segments[it.value()];
}


QUuid Block::addPoint(ParamPoint pt)
{
    QUuid id = pt.id;
    m_pointIndex.insert(id, static_cast<int>(points.size()));
    points.push_back(std::move(pt));
    return id;
}

QUuid Block::addSegment(Segment seg)
{
    QUuid id = seg.id;
    m_segmentIndex.insert(id, static_cast<int>(segments.size()));
    segments.push_back(std::move(seg));
    return id;
}

void Block::rebuildPointIndex() const
{
    m_pointIndex.clear();
    m_pointIndex.reserve(static_cast<int>(points.size()));
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
        m_pointIndex.insert(points[i].id, i);
}

void Block::rebuildSegmentIndex() const
{
    m_segmentIndex.clear();
    m_segmentIndex.reserve(static_cast<int>(segments.size()));
    for (int i = 0; i < static_cast<int>(segments.size()); ++i)
        m_segmentIndex.insert(segments[i].id, i);
}

// --- FitKind::Circle 几何访问器 (CIRCLE_TOOL_DESIGN.md §4.1/D2) ---

const ParamPoint* Block::circleCenterPoint(const Segment& seg) const
{
    const auto* sp = findPoint(seg.startPointId);
    return sp ? findPoint(sp->refPointId) : nullptr;
}

double Block::circleRadiusMm(const Segment& seg) const
{
    const auto* sp = findPoint(seg.startPointId);
    const auto* c  = circleCenterPoint(seg);
    if (sp && c && sp->resolved && c->resolved)
        return (sp->resolvedPos - c->resolvedPos).length();
    return sp ? sp->distance : 0.0;
}

double Block::circleStartAngleDeg(const Segment& seg) const
{
    const auto* sp = findPoint(seg.startPointId);
    const auto* c  = circleCenterPoint(seg);
    if (sp && c && sp->resolved && c->resolved) {
        const auto v = sp->resolvedPos - c->resolvedPos;
        return geo::radToDeg(std::atan2(v.y, v.x));
    }
    return sp ? sp->angle : 0.0;
}

double Block::circleSweepDeg(const Segment& seg) const
{
    const auto* sp = findPoint(seg.startPointId);
    const auto* ep = findPoint(seg.endPointId);
    const auto* c  = circleCenterPoint(seg);
    if (sp && ep && c && sp->resolved && ep->resolved && c->resolved) {
        const auto v0 = sp->resolvedPos - c->resolvedPos;
        const auto v1 = ep->resolvedPos - c->resolvedPos;
        double d = geo::radToDeg(std::atan2(v1.y, v1.x) - std::atan2(v0.y, v0.x));
        while (d <= 1e-9) d += 360.0;
        while (d > 360.0) d -= 360.0;
        return d;
    }
    double d = (sp && ep) ? (ep->angle - sp->angle) : 360.0;
    if (d <= 1e-9) d += 360.0;
    return d;
}

} // namespace cad::param
