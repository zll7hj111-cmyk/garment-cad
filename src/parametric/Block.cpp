#include "Block.h"

#include <cmath>

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


} // namespace cad::param
