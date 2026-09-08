#include "Block.h"

#include <cmath>

#include "geometry/CurveMath.h"
#include "geometry/Vec2.h"

namespace cad::param {
geo::Vec2 Block::worldPos(const QUuid& pointId) const
{
    return transform.toWorld(effectiveLocalPos(pointId));
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

    // Check if pointId is an auxiliary (interpolated) point → use host segment direction.
    const ParamPoint* pt = findPoint(pointId);
    if (pt && pt->constraint == PointConstraint::Interpolated && !pt->hostSegmentId.isNull()) {
        const Segment* hostSeg = findSegment(pt->hostSegmentId);
        if (hostSeg) {
            const ParamPoint* sp = findPoint(hostSeg->startPointId);
            const ParamPoint* ep = findPoint(hostSeg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;
                return std::atan2(dir.y, dir.x);
            }
        }
    }

    return 0.0;
}

double Block::exitDirectionAtPoint(const QUuid& pointId) const
{
    for (const auto& seg : segments) {
        const bool isStart = (seg.startPointId == pointId);
        const bool isEnd   = (seg.endPointId == pointId);
        if (!isStart && !isEnd) continue;

        const ParamPoint* sp = findPoint(seg.startPointId);
        const ParamPoint* ep = findPoint(seg.endPointId);
        if (sp && ep && sp->resolved && ep->resolved) {
            // --- Curve: use endpoint tangent ---
            if (seg.isCurve()) {
                // Frame-level cache: spans are built once per resolve pass;
                // re-solving here would repeat the C2 tridiagonal solve for
                // every attachment that references this block per frame.
                if (const CurveSpanEntry* entry = curveSpanEntry(seg.id);
                    entry && !entry->spans.empty()) {
                    if (isEnd) {
                        // Exit at END: tangent at t=1 of last span
                        geo::Vec2 tan = geo::evalBezierDerivative(entry->spans.back(), 1.0);
                        if (tan.lengthSquared() > 1e-12)
                            return std::atan2(tan.y, tan.x);
                    } else {
                        // Exit at START: negate tangent at t=0 of first span
                        geo::Vec2 tan = geo::evalBezierDerivative(entry->spans.front(), 0.0);
                        if (tan.lengthSquared() > 1e-12)
                            return std::atan2(-tan.y, -tan.x);
                    }
                }
                // Fallback to chord direction (also when the cache is cold).
            }

            // --- Straight line (or curve fallback) ---
            geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;  // start -> end
            // At the START point, "continue straight" extends backward (end->start).
            if (isStart) dir = -dir;
            return std::atan2(dir.y, dir.x);
        }
        break;  // segment found but unresolved
    }

    // Auxiliary (interpolated) point: use host segment's start→end direction.
    const ParamPoint* pt = findPoint(pointId);
    if (pt && pt->constraint == PointConstraint::Interpolated && !pt->hostSegmentId.isNull()) {
        const Segment* hostSeg = findSegment(pt->hostSegmentId);
        if (hostSeg) {
            const ParamPoint* sp = findPoint(hostSeg->startPointId);
            const ParamPoint* ep = findPoint(hostSeg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;
                return std::atan2(dir.y, dir.x);
            }
        }
    }

    // Curve anchor (曲线点): use the host curve's tangent at the anchor, so a
    // line started from the anchor (Alt+click) continues the curve's direction.
    if (pt && pt->constraint == PointConstraint::CurveAnchor && !pt->hostSegmentId.isNull()) {
        const Segment* hostSeg = findSegment(pt->hostSegmentId);
        if (hostSeg) {
            const ParamPoint* sp = findPoint(hostSeg->startPointId);
            const ParamPoint* ep = findPoint(hostSeg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                if (const CurveSpanEntry* entry = curveSpanEntry(hostSeg->id);
                    entry && !entry->spans.empty()) {
                    auto proj = geo::projectPointOnCurve(pt->resolvedPos, entry->spans,
                                                         &entry->cumArcLengthMm);
                    if (proj.valid && proj.tangent.lengthSquared() > 1e-12)
                        return std::atan2(proj.tangent.y, proj.tangent.x);
                }
            }
        }
    }

    return 0.0;
}

double Block::exitDirectionAtPoint(const QUuid& pointId,
                                   const QUuid& preferredSegmentId) const
{
    // 快速通道仅当偏好段存在且可用；一切失败路径回落到遗留扫描。
    if (preferredSegmentId.isNull())
        return exitDirectionAtPoint(pointId);
    const Segment* seg = findSegment(preferredSegmentId);
    if (!seg)
        return exitDirectionAtPoint(pointId);

    const bool isStart = (seg->startPointId == pointId);
    const bool isEnd   = (seg->endPointId == pointId);

    if (!isStart && !isEnd) {
        // Interpolated aux point hosted on the preferred segment:
        // use the host's start→end direction (matches legacy overload).
        const ParamPoint* pt = findPoint(pointId);
        if (pt && pt->constraint == PointConstraint::Interpolated
            && pt->hostSegmentId == preferredSegmentId) {
            const ParamPoint* sp = findPoint(seg->startPointId);
            const ParamPoint* ep = findPoint(seg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;
                return std::atan2(dir.y, dir.x);
            }
        }
        return exitDirectionAtPoint(pointId);
    }

    const ParamPoint* sp = findPoint(seg->startPointId);
    const ParamPoint* ep = findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved)
        return exitDirectionAtPoint(pointId);

    // Curve: use the endpoint tangent (not the chord) so a
    // follower with followerAngle==0 continues the curve smoothly.
    if (seg->isCurve()) {
        if (const CurveSpanEntry* entry = curveSpanEntry(seg->id);
            entry && !entry->spans.empty()) {
            if (isEnd) {
                geo::Vec2 tan = geo::evalBezierDerivative(entry->spans.back(), 1.0);
                if (tan.lengthSquared() > 1e-12)
                    return std::atan2(tan.y, tan.x);
            } else {
                geo::Vec2 tan = geo::evalBezierDerivative(entry->spans.front(), 0.0);
                if (tan.lengthSquared() > 1e-12)
                    return std::atan2(-tan.y, -tan.x);
            }
        }
        // Fallback to chord direction below.
    }
    geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;  // start -> end
    // At the START point, "continue straight" extends backward.
    if (isStart) dir = -dir;
    return std::atan2(dir.y, dir.x);
}

QUuid Block::exitSegmentAtPoint(const QUuid& pointId) const
{
    for (const auto& seg : segments)
        if (seg.startPointId == pointId || seg.endPointId == pointId)
            return seg.id;

    const ParamPoint* pt = findPoint(pointId);
    if (pt && (pt->constraint == PointConstraint::Interpolated ||
               pt->constraint == PointConstraint::CurveAnchor))
        return pt->hostSegmentId;

    return {};
}

double Block::segmentLengthAtPoint(const QUuid& pointId) const
{
    for (const auto& seg : segments) {
        if (seg.startPointId == pointId || seg.endPointId == pointId)
            return segmentEffectiveLength(seg.id);
    }

    const ParamPoint* pt = findPoint(pointId);
    if (pt && !pt->hostSegmentId.isNull())
        return segmentEffectiveLength(pt->hostSegmentId);

    return 0.0;
}


} // namespace cad::param
