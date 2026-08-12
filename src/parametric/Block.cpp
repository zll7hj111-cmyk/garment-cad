#include "Block.h"

#include <algorithm>
#include <cmath>

#include "parametric/ExpressionEvaluator.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"

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
                    const QHash<QString, QList<Condition>>& conditioned,
                    EvalContext* ctx)
{
    // Snapshot resolved positions so we can detect actual geometry changes
    // and bump geometryEpoch (canvas lightweight-sync relies on it).
    std::vector<geo::Vec2> prevPos;
    prevPos.reserve(points.size());
    for (const auto& pt : points)
        prevPos.push_back(pt.resolvedPos);

    // Reset resolved state
    for (auto& pt : points) {
        pt.resolved = false;
    }

    resolveUnresolved(params, conditioned, ctx);

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

    // Bump the geometry epoch when any point actually moved (sub-nanometre
    // FP noise is ignored). The canvas uses this to skip full cache rebuilds
    // when only the block's transform changed (drag).
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i].resolved &&
            points[i].resolvedPos.distanceSquaredTo(prevPos[i]) > 1e-6) {
            ++geometryEpoch;
            break;
        }
    }

    // Rebuild the frame-level curve cache from the FINAL resolved positions
    // (one C2 solve per curve segment per frame). Consumers that run later in
    // the same frame — Resolver attachment settling (exitDirectionAtPoint),
    // BlockItem::rebuildCache, SnapEngine projection, tangent handles — all
    // share this cache instead of re-solving each on their own. Pure
    // transform changes (origin/rotation) leave the local spans untouched, so
    // the cache stays valid across drags.
    rebuildCurveCache();
}

void Block::resolveUnresolved(const QHash<QString, double>& params,
                              const QHash<QString, QList<Condition>>& conditioned,
                              EvalContext* ctx)
{
    // Iterative resolve: keep resolving until no more progress or all done.
    // This handles arbitrary dependency ordering via simple fixpoint iteration.
    bool progress = true;
    while (progress) {
        progress = false;
        for (auto& pt : points) {
            if (pt.resolved) continue;

            switch (pt.constraint) {
            // NOTE: exhaustive by design — extending PointConstraint requires
            // touching every layer listed in the registry next to the enum
            // (ParamPoint.h). If the compiler stops complaining, that means a
            // NEW case is still missing here.
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
                    auto r = ConditionEngine::evaluate(pt.distanceFormula, params, conditioned, ctx);
                    if (r.ok) dist = geo::Units::cmToMm(r.value);
                }

                double ang = pt.angle;
                if (!pt.angleFormula.isEmpty()) {
                    auto r = ConditionEngine::evaluate(pt.angleFormula, params, conditioned, ctx);
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

            case PointConstraint::Intersection: {
                // Ray origin (refPointA) must be resolved.
                const ParamPoint* origin = findPoint(pt.refPointA);
                if (!origin || !origin->resolved) break;

                // Target segment (hostSegmentId) and its endpoints. The END
                // point may be mid-cycle in the fixpoint (e.g. a break endpoint
                // whose position depends on THIS intersection): its cached
                // position is used now and later iterations converge once the
                // endpoint resolves. The START point must be resolved — it is
                // the anchor of the segment geometry.
                const Segment* seg = findSegment(pt.hostSegmentId);
                if (!seg) break;
                const ParamPoint* sp = findPoint(seg->startPointId);
                const ParamPoint* ep = findPoint(seg->endPointId);
                if (!sp || !ep || !sp->resolved) break;

                // Base angle = target segment's start→end direction.
                geo::Vec2 segDir = ep->resolvedPos - sp->resolvedPos;
                double segLen = segDir.length();
                if (segLen < 1e-9) break;  // Degenerate segment.

                // Ray direction: aim-point mode (指向点) overrides the
                // numeric/formula angle — the ray points straight at
                // interAimPointId. An aim point outside this block defers to
                // the Resolver's cross-block pass (Step 6).
                double theta;
                if (!pt.interAimPointId.isNull()) {
                    const ParamPoint* aim = findPoint(pt.interAimPointId);
                    if (!aim || !aim->resolved) break;
                    geo::Vec2 toAim = aim->resolvedPos - origin->resolvedPos;
                    if (toAim.lengthSquared() < 1e-12) break;  // Coincident with origin.
                    theta = std::atan2(toAim.y, toAim.x);
                } else {
                    double baseAngle = std::atan2(segDir.y, segDir.x);
                    // Evaluate ray angle (formula overrides numeric).
                    double angleDeg = pt.interAngle;
                    if (!pt.interAngleFormula.isEmpty()) {
                        auto r = ConditionEngine::evaluate(pt.interAngleFormula, params, conditioned, ctx);
                        if (r.ok) angleDeg = r.value;
                    }
                    theta = baseAngle + angleDeg * M_PI / 180.0;
                }

                // Ray direction.
                geo::Vec2 d{std::cos(theta), std::sin(theta)};

                // --- Curve target: rayCurveIntersect ---
                if (seg->isCurve()) {
                    std::vector<geo::Vec2> pts;
                    std::vector<geo::Vec2> tIn, tOut;
                    std::vector<bool> autoTan;
                    pts.push_back(sp->resolvedPos);
                    tIn.push_back(sp->tangentIn); tOut.push_back(sp->tangentOut); autoTan.push_back(sp->autoTangent);
                    for (const auto& ppId : seg->passPointIds) {
                        const ParamPoint* pp = findPoint(ppId);
                        if (!pp || !pp->resolved) continue;
                        pts.push_back(pp->resolvedPos);
                        tIn.push_back(pp->tangentIn); tOut.push_back(pp->tangentOut); autoTan.push_back(pp->autoTangent);
                    }
                    pts.push_back(ep->resolvedPos);
                    tIn.push_back(ep->tangentIn); tOut.push_back(ep->tangentOut); autoTan.push_back(ep->autoTangent);

                    auto spans = geo::buildBezierSpans(pts, tIn, tOut, autoTan, seg->tension,
                                                        geo::AutoCurveMode::Hobby);
                    if (spans.empty()) break;

                    auto hits = geo::rayCurveIntersect(origin->resolvedPos, d, spans, pt.interBidirectional);
                    if (hits.empty()) break;

                    pt.resolvedPos = hits[0].point;
                    pt.resolved = true;
                    progress = true;
                    break;
                }

                // --- Straight-line target: cross-product method ---
                // Ray-segment intersection via cross products.
                // Ray: R(s) = origin + s*d,  s >= 0 (or any s if bidirectional)
                // Segment: L(t) = sp + t*segDir,  t in [0,1]
                double denom = d.cross(segDir);
                if (std::abs(denom) < 1e-9) {
                    // Parallel — no intersection; keep last position if available.
                    break;
                }

                geo::Vec2 w = sp->resolvedPos - origin->resolvedPos;
                double s = w.cross(segDir) / denom;  // Ray parameter.
                double t = w.cross(d) / denom;       // Segment parameter.

                // Validity check.
                constexpr double eps = 1e-6;
                bool validT = (t >= -eps && t <= 1.0 + eps);
                bool validS = pt.interBidirectional ? true : (s >= -eps);
                if (!validT || !validS) break;  // No valid intersection.

                pt.resolvedPos = origin->resolvedPos + d * s;
                pt.resolved = true;
                progress = true;
                break;
            }

            case PointConstraint::Interpolated: {
                if (resolveInterpolatedPoint(pt, params, conditioned, ctx))
                    progress = true;
                break;
            }

            case PointConstraint::CurveAnchor: {
                // Curve pass-point (曲线点): positioned on the CHORD of its host
                // segment (start→end straight line) by a fraction + perpendicular
                // offset. Resolving against the chord (not the curve) avoids a
                // circular dependency — the anchor itself shapes the curve.
                const Segment* hostSeg = findSegment(pt.hostSegmentId);
                if (!hostSeg) break;
                const ParamPoint* sp = findPoint(hostSeg->startPointId);
                const ParamPoint* ep = findPoint(hostSeg->endPointId);
                if (!sp || !ep || !sp->resolved || !ep->resolved) break;

                geo::Vec2 chord = ep->resolvedPos - sp->resolvedPos;
                const double len = chord.length();
                if (len < 1e-9) {
                    // Degenerate chord — sit on the start point.
                    pt.resolvedPos = sp->resolvedPos;
                    pt.resolved = true;
                    progress = true;
                    break;
                }
                const geo::Vec2 unitDir = chord / len;
                const geo::Vec2 normal{-unitDir.y, unitDir.x};  // left of start→end

                const double percent = pt.interpPercent;
                const double offset  = pt.interpOffsetDist;

                // The anchor's offset is used as-is (no taper): the curve keeps its
                // full shape even when the anchor is near / past an endpoint. The
                // smoothness near endpoints is guaranteed by the curve math
                // (catmullRomTangent keeps a non-collapsing tangent), NOT by
                // flattening the anchor onto the chord.

                pt.resolvedPos = sp->resolvedPos
                               + unitDir * (len * percent)
                               + normal * offset;
                pt.resolved = true;
                progress = true;
                break;
            }
            }
        }
    }
}

bool Block::resolveInterpolatedPoint(ParamPoint& pt,
                                     const QHash<QString, double>& params,
                                     const QHash<QString, QList<Condition>>& conditioned,
                                     EvalContext* ctx)
{
    // Auxiliary point interpolated on a host segment.
    const Segment* hostSeg = findSegment(pt.hostSegmentId);
    if (!hostSeg) return false;

    // --- Curve branch: arc-length parameterized interpolation ---
    if (hostSeg->isCurve()) {
        // Collect resolved positions: start + passPoints + end
        // The END point may be mid-cycle in the fixpoint (e.g. a break endpoint
        // whose position depends on THIS interpolated point): its cached position
        // is used now and later iterations converge once the endpoint resolves.
        const ParamPoint* sp = findPoint(hostSeg->startPointId);
        const ParamPoint* ep = findPoint(hostSeg->endPointId);
        if (!sp || !ep || !sp->resolved) return false;
        
        std::vector<geo::Vec2> pts;
        std::vector<geo::Vec2> tIn, tOut;
        std::vector<bool> autoTan;
        pts.push_back(sp->resolvedPos);
        tIn.push_back(sp->tangentIn);
        tOut.push_back(sp->tangentOut);
        autoTan.push_back(sp->autoTangent);

        for (const auto& ppId : hostSeg->passPointIds) {
            const ParamPoint* pp = findPoint(ppId);
            if (!pp || !pp->resolved) return false;
            pts.push_back(pp->resolvedPos);
            tIn.push_back(pp->tangentIn);
            tOut.push_back(pp->tangentOut);
            autoTan.push_back(pp->autoTangent);
        }
        pts.push_back(ep->resolvedPos);
        tIn.push_back(ep->tangentIn);
        tOut.push_back(ep->tangentOut);
        autoTan.push_back(ep->autoTangent);

        auto spans = geo::buildBezierSpans(pts, tIn, tOut, autoTan, hostSeg->tension,
                                            geo::AutoCurveMode::Hobby);
        if (spans.empty()) return false;

        double totalArc = geo::totalArcLength(spans);
        if (totalArc < 1e-9) {
            pt.resolvedPos = sp->resolvedPos;
            pt.resolved = true;
            return true;
        }

        // Evaluate percent (arc-length fraction)
        double percent = pt.interpPercent;
        if (!pt.interpPercentFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(pt.interpPercentFormula, params, conditioned, ctx);
            if (r.ok) percent = r.value;
        }

        // Evaluate constant (cm → mm)
        double constant = pt.interpConstant;
        if (!pt.interpConstantFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(pt.interpConstantFormula, params, conditioned, ctx);
            if (r.ok) constant = geo::Units::cmToMm(r.value);
        }

        // Target arc-length position
        double targetS = totalArc * percent + constant;
        if (pt.interpFromEnd) targetS = totalArc - targetS;
        targetS = std::clamp(targetS, 0.0, totalArc);

        // Arc-length → parameter T → point + tangent
        double T = geo::arcLengthToParam(spans, targetS);
        geo::Vec2 basePos = geo::evalCurve(spans, T);
        geo::Vec2 tangent = geo::evalCurveTangent(spans, T);
        double baseAngle = std::atan2(tangent.y, tangent.x);
        if (pt.interpFromEnd) baseAngle += M_PI;  // Flip direction

        // Offset (perpendicular to tangent)
        double offAngle = pt.interpOffsetAngle;
        if (!pt.interpOffsetAngleFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(pt.interpOffsetAngleFormula, params, conditioned, ctx);
            if (r.ok) offAngle = r.value;
        }
        double offDist = pt.interpOffsetDist;
        if (!pt.interpOffsetDistFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(pt.interpOffsetDistFormula, params, conditioned, ctx);
            if (r.ok) offDist = geo::Units::cmToMm(r.value);
        }

        if (std::abs(offDist) > 1e-9) {
            double angleRad = baseAngle + offAngle * M_PI / 180.0;
            pt.resolvedPos = basePos + geo::Vec2{
                offDist * std::cos(angleRad),
                offDist * std::sin(angleRad)
            };
        } else {
            pt.resolvedPos = basePos;
        }
        pt.resolved = true;
        return true;
    }

    // --- Straight-line branch (existing logic) ---
    // The END point may be mid-cycle in the fixpoint (e.g. a break endpoint
    // whose position depends on THIS interpolated point): its cached position
    // is used now and later iterations converge once the endpoint resolves.
    const ParamPoint* sp = findPoint(hostSeg->startPointId);
    const ParamPoint* ep = findPoint(hostSeg->endPointId);
    if (!sp || !ep || !sp->resolved) return false;

    // Reference direction: by default the point is measured from the segment's
    // START toward its END. When interpFromEnd is set, measure from the END
    // toward the START instead — the flipped base direction applies to the
    // percent, the constant and the offset angle alike.
    const bool fromEnd = pt.interpFromEnd;
    geo::Vec2 dir = fromEnd ? (sp->resolvedPos - ep->resolvedPos)
                            : (ep->resolvedPos - sp->resolvedPos);
    double distAB = dir.length();
    if (distAB < 1e-9) {
        // Degenerate segment — place at the reference origin.
        pt.resolvedPos = fromEnd ? ep->resolvedPos : sp->resolvedPos;
        pt.resolved = true;
        return true;
    }
    geo::Vec2 unitDir = dir / distAB;
    double baseAngle = std::atan2(unitDir.y, unitDir.x);

    // Measurement origin: default is the segment endpoint (per interpFromEnd).
    // When interpRefPointId is set, measure from that point's resolved position
    // instead (it must be on the same host segment and already resolved).
    geo::Vec2 origin;
    if (!pt.interpRefPointId.isNull()) {
        const ParamPoint* refPt = findPoint(pt.interpRefPointId);
        if (!refPt || !refPt->resolved) return false;  // Dependency not ready.
        origin = refPt->resolvedPos;
    } else {
        origin = fromEnd ? ep->resolvedPos : sp->resolvedPos;
    }

    // Evaluate percent
    double percent = pt.interpPercent;
    if (!pt.interpPercentFormula.isEmpty()) {
        auto r = ConditionEngine::evaluate(pt.interpPercentFormula, params, conditioned, ctx);
        if (r.ok) percent = r.value;
    }

    // Evaluate constant (cm domain → mm)
    double constant = pt.interpConstant;
    if (!pt.interpConstantFormula.isEmpty()) {
        auto r = ConditionEngine::evaluate(pt.interpConstantFormula, params, conditioned, ctx);
        if (r.ok) constant = geo::Units::cmToMm(r.value);
    }

    double along = distAB * percent + constant;
    geo::Vec2 basePos = origin + unitDir * along;

    // Evaluate offset angle (degrees)
    double offAngle = pt.interpOffsetAngle;
    if (!pt.interpOffsetAngleFormula.isEmpty()) {
        auto r = ConditionEngine::evaluate(pt.interpOffsetAngleFormula, params, conditioned, ctx);
        if (r.ok) offAngle = r.value;
    }

    // Evaluate offset distance (cm domain → mm)
    double offDist = pt.interpOffsetDist;
    if (!pt.interpOffsetDistFormula.isEmpty()) {
        auto r = ConditionEngine::evaluate(pt.interpOffsetDistFormula, params, conditioned, ctx);
        if (r.ok) offDist = geo::Units::cmToMm(r.value);
    }

    if (std::abs(offDist) > 1e-9) {
        double angleRad = baseAngle + offAngle * M_PI / 180.0;
        pt.resolvedPos = basePos + geo::Vec2{
            offDist * std::cos(angleRad),
            offDist * std::sin(angleRad)
        };
    } else {
        pt.resolvedPos = basePos;
    }
    pt.resolved = true;
    return true;
}

void Block::resolveInterpolatedPoints(const QHash<QString, double>& params,
                                      const QHash<QString, QList<Condition>>& conditioned,
                                      EvalContext* ctx)
{
    for (auto& pt : points) {
        if (pt.constraint != PointConstraint::Interpolated) continue;
        const geo::Vec2 oldPos = pt.resolvedPos;
        resolveInterpolatedPoint(pt, params, conditioned, ctx);
        if (pt.resolvedPos.distanceSquaredTo(oldPos) > 1e-6)
            ++geometryEpoch;
    }
}

bool Block::collectCurveAnchors(const Segment& seg,
                                std::vector<geo::Vec2>& pts,
                                std::vector<geo::Vec2>& tIn,
                                std::vector<geo::Vec2>& tOut,
                                std::vector<bool>& autoTan) const
{
    pts.clear(); tIn.clear(); tOut.clear(); autoTan.clear();

    const ParamPoint* sp = findPoint(seg.startPointId);
    const ParamPoint* ep = findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return false;

    pts.push_back(sp->resolvedPos);
    tIn.push_back(sp->tangentIn);  tOut.push_back(sp->tangentOut);  autoTan.push_back(sp->autoTangent);
    for (const auto& ppId : seg.passPointIds) {
        const ParamPoint* pp = findPoint(ppId);
        if (!pp || !pp->resolved) return false;
        pts.push_back(pp->resolvedPos);
        tIn.push_back(pp->tangentIn); tOut.push_back(pp->tangentOut); autoTan.push_back(pp->autoTangent);
    }
    pts.push_back(ep->resolvedPos);
    tIn.push_back(ep->tangentIn);  tOut.push_back(ep->tangentOut);  autoTan.push_back(ep->autoTangent);
    return true;
}

void Block::rebuildCurveCache()
{
    m_curveSpans.clear();
    m_curveSpans.reserve(segments.size());

    for (const auto& seg : segments) {
        if (!seg.isCurve()) continue;

        std::vector<geo::Vec2> pts, tIn, tOut;
        std::vector<bool> autoTan;
        if (!collectCurveAnchors(seg, pts, tIn, tOut, autoTan)) continue;

        std::vector<geo::BezierSpan> spans =
            geo::buildBezierSpans(pts, tIn, tOut, autoTan, seg.tension,
                                  geo::AutoCurveMode::Hobby);
        if (spans.empty()) continue;

        // Control-polygon bbox (local): the curve lies inside it (convex hull
        // property of Béziers), safe for coarse distance culling.
        geo::Vec2 lo = pts[0], hi = pts[0];
        const auto grow = [&lo, &hi](const geo::Vec2& p) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
        };
        for (const auto& s : spans) {
            grow(s.ctrl1); grow(s.ctrl2); grow(s.p3);
        }

        // Render cache (local coords): flatten, label midpoint/tangent and
        // exact arc length are computed ONCE per resolve here — the canvas
        // cache rebuild (BlockItem) only applies rotation + Y-flip to these,
        // never re-flattens / re-integrates the curve per frame.
        CurveSpanEntry entry;
        entry.segmentId = seg.id;
        entry.spans = std::move(spans);
        entry.anchors = std::move(pts);
        entry.bboxMin = lo;
        entry.bboxMax = hi;
        entry.flatLocal    = geo::flattenBezierSpans(entry.spans, 0.1);
        entry.labelLocal   = geo::evalCurve(entry.spans, 0.5);
        entry.labelLocalDir = geo::evalCurveTangent(entry.spans, 0.5);
        entry.arcLengthMm  = geo::totalArcLength(entry.spans);
        m_curveSpans.push_back(std::move(entry));
    }
}

const CurveSpanEntry* Block::curveSpanEntry(const QUuid& segmentId) const
{
    for (const auto& e : m_curveSpans)
        if (e.segmentId == segmentId) return &e;
    return nullptr;
}

geo::Vec2 Block::worldPos(const QUuid& pointId) const
{
    const ParamPoint* pt = findPoint(pointId);
    if (!pt) return geo::Vec2::zero();
    return transform.toWorld(pt->resolvedPos);
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
                    auto proj = geo::projectPointOnCurve(pt->resolvedPos, entry->spans);
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
    if (!preferredSegmentId.isNull()) {
        if (const Segment* seg = findSegment(preferredSegmentId)) {
            const bool isStart = (seg->startPointId == pointId);
            const bool isEnd   = (seg->endPointId == pointId);
            if (isStart || isEnd) {
                const ParamPoint* sp = findPoint(seg->startPointId);
                const ParamPoint* ep = findPoint(seg->endPointId);
                if (sp && ep && sp->resolved && ep->resolved) {
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
            } else {
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
            }
        }
    }
    // Segment missing / not incident / unresolved — legacy scan fallback.
    return exitDirectionAtPoint(pointId);
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
        if (seg.startPointId != pointId && seg.endPointId != pointId) continue;
        const ParamPoint* sp = findPoint(seg.startPointId);
        const ParamPoint* ep = findPoint(seg.endPointId);
        if (sp && ep && sp->resolved && ep->resolved) {
            // Curve: return arc length
            if (seg.isCurve()) {
                if (const CurveSpanEntry* entry = curveSpanEntry(seg.id);
                    entry && !entry->spans.empty())
                    return geo::totalArcLength(entry->spans);
            }
            // Line (or curve fallback): chord length
            return sp->resolvedPos.distanceTo(ep->resolvedPos);
        }
        break;
    }
    return 0.0;
}

bool Block::freezeSegmentGeometry()
{
    if (segments.empty()) return false;
    const Segment& seg = segments.front();
    ParamPoint* pStart = findPoint(seg.startPointId);
    ParamPoint* pEnd   = findPoint(seg.endPointId);
    if (!pStart || !pEnd || !pStart->resolved || !pEnd->resolved) return false;

    const geo::Vec2 w1 = transform.toWorld(pStart->resolvedPos);
    const geo::Vec2 w2 = transform.toWorld(pEnd->resolvedPos);
    const geo::Vec2 d  = w2 - w1;
    const double len   = d.length();
    const double dir   = std::atan2(d.y, d.x);

    transform.origin   = w1;
    transform.rotation = dir;
    pStart->constraint = PointConstraint::Free;
    pStart->freePos    = geo::Vec2::zero();
    pEnd->constraint  = PointConstraint::Polar;
    pEnd->refPointId  = pStart->id;
    pEnd->refSegmentId = QUuid();
    pEnd->distance    = len;              // own length attribute (mm)
    pEnd->distanceFormula.clear();
    pEnd->angle       = 0.0;
    pEnd->angleFormula.clear();

    // Sync the resolved cache with the frozen local construction (stale
    // stretched positions would corrupt directionAtPoint for callers that
    // back-solve angles before the next resolve pass).
    pStart->resolvedPos = geo::Vec2::zero();
    pStart->resolved    = true;
    pEnd->resolvedPos   = geo::Vec2(len, 0.0);
    pEnd->resolved      = true;
    return true;
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
