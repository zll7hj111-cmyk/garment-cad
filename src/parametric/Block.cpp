#include "Block.h"

#include <algorithm>
#include <cmath>

#include "parametric/ExpressionEvaluator.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"

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

// --- Block ---

void Block::resolve(const QHash<QString, double>& params,
                    const QHash<QString, QList<Condition>>& conditioned)
{
    // Reset resolved state
    for (auto& pt : points) {
        pt.resolved = false;
    }

    // Iterative resolve: keep resolving until no more progress or all done.
    // This handles arbitrary dependency ordering via simple fixpoint iteration.
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto& pt : points) {
            if (pt.resolved) continue;

            switch (pt.constraint) {
            case PointConstraint::Free:
                pt.resolvedPos = pt.freePos;
                pt.resolved = true;
                progress = true;
                break;

            case PointConstraint::Polar: {
                const ParamPoint* ref = findPoint(pt.refPointId);
                if (!ref || !ref->resolved) break;

                // Evaluate formulas if present, otherwise use numeric values.
                // Formula domain is cm (user-facing unit); convert result to mm.
                double dist = pt.distance;
                if (!pt.distanceFormula.isEmpty()) {
                    auto r = ConditionEngine::evaluate(pt.distanceFormula, params, conditioned);
                    if (r.ok) dist = geo::Units::cmToMm(r.value);
                }

                double ang = pt.angle;
                if (!pt.angleFormula.isEmpty()) {
                    auto r = ConditionEngine::evaluate(pt.angleFormula, params, conditioned);
                    if (r.ok) ang = r.value;
                }

                double baseAngle = 0.0;
                // If a reference segment is specified, use its direction as baseline
                if (!pt.refSegmentId.isNull()) {
                    const Segment* seg = findSegment(pt.refSegmentId);
                    if (seg) {
                        const ParamPoint* sp = findPoint(seg->startPointId);
                        const ParamPoint* ep = findPoint(seg->endPointId);
                        if (sp && ep && sp->resolved && ep->resolved) {
                            geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;
                            baseAngle = std::atan2(dir.y, dir.x);
                        }
                    }
                }

                double angleRad = baseAngle + ang * M_PI / 180.0;
                pt.resolvedPos = ref->resolvedPos + geo::Vec2{
                    dist * std::cos(angleRad),
                    dist * std::sin(angleRad)
                };
                pt.resolved = true;
                progress = true;
                break;
            }

            case PointConstraint::Midpoint: {
                const ParamPoint* a = findPoint(pt.refPointA);
                const ParamPoint* b = findPoint(pt.refPointB);
                if (!a || !b || !a->resolved || !b->resolved) break;

                pt.resolvedPos = a->resolvedPos.lerp(b->resolvedPos, 0.5);
                pt.resolved = true;
                progress = true;
                break;
            }

            case PointConstraint::OnSegment: {
                const ParamPoint* a = findPoint(pt.refPointA);
                const ParamPoint* b = findPoint(pt.refPointB);
                if (!a || !b || !a->resolved || !b->resolved) break;

                pt.resolvedPos = a->resolvedPos.lerp(b->resolvedPos, pt.ratio);
                pt.resolved = true;
                progress = true;
                break;
            }

            case PointConstraint::Intersection:
                // Reserved for future implementation
                break;
            }
        }
    }

    // Handle closure constraint: if isClosed and there are segments,
    // force the last segment's endpoint to equal the first segment's startpoint.
    if (isClosed && !segments.empty()) {
        const ParamPoint* firstPt = findPoint(segments.front().startPointId);
        ParamPoint* lastPt = findPoint(segments.back().endPointId);
        if (firstPt && lastPt && firstPt->resolved) {
            lastPt->resolvedPos = firstPt->resolvedPos;
            lastPt->resolved = true;
        }
    }
}

geo::Vec2 Block::worldPos(const QUuid& pointId) const
{
    const ParamPoint* pt = findPoint(pointId);
    if (!pt) return geo::Vec2::zero();
    return transform.toWorld(pt->resolvedPos);
}

ParamPoint* Block::findPoint(const QUuid& pointId)
{
    auto it = m_pointIndex.find(pointId);
    if (it == m_pointIndex.end()) return nullptr;
    return &points[it.value()];
}

const ParamPoint* Block::findPoint(const QUuid& pointId) const
{
    auto it = m_pointIndex.find(pointId);
    if (it == m_pointIndex.end()) return nullptr;
    return &points[it.value()];
}

Segment* Block::findSegment(const QUuid& segmentId)
{
    auto it = std::find_if(segments.begin(), segments.end(),
        [&segmentId](const Segment& s) { return s.id == segmentId; });
    return (it != segments.end()) ? &(*it) : nullptr;
}

double Block::directionAtPoint(const QUuid& pointId) const
{
    for (const auto& seg : segments) {
        if (seg.startPointId == pointId || seg.endPointId == pointId) {
            const ParamPoint* sp = findPoint(seg.startPointId);
            const ParamPoint* ep = findPoint(seg.endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;
                return std::atan2(dir.y, dir.x);
            }
            break;  // segment found but unresolved
        }
    }
    return 0.0;
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
    segments.push_back(std::move(seg));
    return id;
}

void Block::rebuildPointIndex()
{
    m_pointIndex.clear();
    m_pointIndex.reserve(static_cast<int>(points.size()));
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
        m_pointIndex.insert(points[i].id, i);
}

} // namespace cad::param
