#include "Block.h"

#include <algorithm>
#include <cmath>

#include "geometry/CurveMath.h"
#include "geometry/RayCast.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "parametric/ConditionEngine.h"
#include "parametric/ExpressionEvaluator.h"
#include "parametric/IntersectDebug.h"
#include "geometry/Epsilon.h"

namespace cad::param {
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

    // 端点延长线：本帧延长量先求值（数值 mm / 公式 cm 域）—— 求解 pass 内的
    // effectiveLocalPos（交叉点宿主段、辅助点宿主等）即读到本帧值。
    evaluateExtendValues(params, conditioned, ctx);

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
            points[i].resolvedPos.distanceSquaredTo(prevPos[i]) > cad::geo::kGeomEpsLoose) {
            touchGeometry();
            break;
        }
    }

    // Rebuild the frame-level curve cache ONLY when the local geometry
    // actually changed (epoch delta from a point move or an explicit curve-
    // edit bump) or the cache is cold / the curve-ness changed. A pure rigid
    // drag (transform-only) leaves the local spans byte-identical — re-solving
    // the C2 tangents, re-flattening (0.1mm) and re-integrating the arc length
    // on EVERY frame is pure waste, and any per-frame micro-residual of the
    // settle would otherwise show up as a wobble of the drawn polyline
    // (曲线跟随拖动抖动, 用户报告 2026-09). Consumers that run later in the
    // frame — exitDirectionAtPoint, SnapEngine projection, tangent handles,
    // BlockItem::rebuildCache — all read the spans in LOCAL coordinates, so
    // frozen spans + current transform stay exact under rigid motion.
    bool hasCurveSegments = false;
    for (const auto& seg : segments)
        if (seg.isCurve()) { hasCurveSegments = true; break; }
    const bool staleCache = hasCurveSegments
        ? (m_curveSpans.empty() || m_geometryEpoch != m_curveCacheEpoch)
        : !m_curveSpans.empty();  // curve→straight: drop stale entries
    if (staleCache)
        rebuildCurveCache();

    // 端点延长线：本体解算完成后重建有效位置缓存（resolvedPos 保持本体；
    // 实际位置经 worldPos()/effectiveLocalPos() 读取）。比例类定义读 resolvedPos
    // 不变（EXTEND_LINE_DESIGN.md）。
    applyEffectivePositions();
}

bool Block::polarEndpointCycleSeed(const ParamPoint& ep, const Block& block,
                                   const Segment& seg, const ParamPoint& sp,
                                   const QHash<QString, double>& params,
                                   const QHash<QString, QList<Condition>>& conditioned,
                                   EvalContext* ctx, geo::Vec2& outLocal)
{
    if (ep.constraint != PointConstraint::Polar || ep.refPointId.isNull())
        return false;
    // An ordinary start-anchored polar endpoint does not need the seed.
    if (ep.refPointId == seg.startPointId || ep.refPointId == seg.endPointId)
        return false;
    // The ref must be an aux point ON this exact segment (the cycle).
    const ParamPoint* ref = block.findPoint(ep.refPointId);
    if (!ref) return false;
    const bool auxOnThisSeg =
        ((ref->constraint == PointConstraint::Intersection
          || ref->constraint == PointConstraint::Interpolated)
         && ref->hostSegmentId == seg.id);
    if (!auxOnThisSeg) return false;

    // Evaluate the polar formula anchored at the segment START instead (a
    // principled bootstrap — the true anchor resolves in a later pass).
    double dist = ep.distance;
    ConditionEngine::evaluateLengthMm(ep.distanceFormula, params, conditioned, dist, ctx);
    double ang = ep.angle;
    if (!ep.angleFormula.isEmpty()) {
        auto r = ConditionEngine::evaluate(ep.angleFormula, params, conditioned, ctx);
        if (r.ok) ang = r.value;
    }
    double baseAngle = 0.0;
    if (!ep.refSegmentId.isNull()) {
        const Segment* rseg = block.findSegment(ep.refSegmentId);
        if (rseg) {
            const ParamPoint* rsp = block.findPoint(rseg->startPointId);
            const ParamPoint* rep = block.findPoint(rseg->endPointId);
            if (rsp && rep && rsp->resolved && rep->resolved)
                baseAngle = std::atan2(rep->resolvedPos.y - rsp->resolvedPos.y,
                                       rep->resolvedPos.x - rsp->resolvedPos.x);
        }
    }
    const double angleRad = baseAngle + cad::geo::degToRad(ang);
    outLocal = sp.resolvedPos + geo::Vec2{dist * std::cos(angleRad),
                                          dist * std::sin(angleRad)};
    return true;
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
                if (resolvePolarPoint(pt, params, conditioned, ctx))
                    progress = true;
                break;
            }

            case PointConstraint::OrthoOffset: {
                if (resolveOrthoOffsetPoint(pt, params, conditioned, ctx))
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
                if (resolveIntersectionPoint(pt, params, conditioned, ctx))
                    progress = true;
                break;
            }

            case PointConstraint::Interpolated: {
                if (resolveInterpolatedPoint(pt, params, conditioned, ctx))
                    progress = true;
                break;
            }

            case PointConstraint::CurveAnchor: {
                if (resolveCurveAnchorPoint(pt))
                    progress = true;
                break;
            }
            }
        }
    }
}

bool Block::resolvePolarPoint(ParamPoint& pt,
                              const QHash<QString, double>& params,
                              const QHash<QString, QList<Condition>>& conditioned,
                              EvalContext* ctx)
{
    const ParamPoint* ref = findPoint(pt.refPointId);
    if (!ref || !ref->resolved) return false;

    // Evaluate formulas if present, otherwise use numeric values.
    // Formula domain is cm (user-facing unit); convert result to mm.
    double dist = pt.distance;
    ConditionEngine::evaluateLengthMm(pt.distanceFormula, params, conditioned, dist, ctx);

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

    double angleRad = baseAngle + cad::geo::degToRad(ang);
    pt.resolvedPos = ref->resolvedPos + geo::Vec2{
        dist * std::cos(angleRad),
        dist * std::sin(angleRad)
    };
    pt.resolved = true;
    return true;
}

bool Block::resolveOrthoOffsetPoint(ParamPoint& pt,
                                    const QHash<QString, double>& params,
                                    const QHash<QString, QList<Condition>>& conditioned,
                                    EvalContext* ctx)
{
    const ParamPoint* ref = findPoint(pt.refPointId);
    if (!ref || !ref->resolved) return false;

    double dist = pt.distance;
    ConditionEngine::evaluateLengthMm(pt.distanceFormula, params, conditioned, dist, ctx);

    double offsetDist = pt.orthoOffsetDist;
    ConditionEngine::evaluateLengthMm(pt.orthoOffsetDistFormula, params, conditioned, offsetDist, ctx);

    double ang = pt.angle;
    if (!pt.angleFormula.isEmpty()) {
        auto r = ConditionEngine::evaluate(pt.angleFormula, params, conditioned, ctx);
        if (r.ok) ang = r.value;
    }

    double baseAngle = 0.0;
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

    double axisRad = baseAngle + cad::geo::degToRad(ang);
    geo::Vec2 axisDir{std::cos(axisRad), std::sin(axisRad)};
    geo::Vec2 perpDir{axisDir.y, -axisDir.x}; // positive offset = left (+90° in screen space, Y-down)

    pt.resolvedPos = ref->resolvedPos + axisDir * dist + perpDir * offsetDist;
    pt.resolved = true;
    return true;
}

bool Block::resolveIntersectionPoint(ParamPoint& pt,
                                     const QHash<QString, double>& params,
                                     const QHash<QString, QList<Condition>>& conditioned,
                                     EvalContext* ctx)
{
    // Ray origin (refPointA) must be resolved.
    const ParamPoint* origin = findPoint(pt.refPointA);
    if (!origin || !origin->resolved) return false;

    // Target segment (hostSegmentId) and its endpoints. The END
    // point may be mid-cycle in the fixpoint (e.g. a break endpoint
    // whose position depends on THIS intersection): its cached
    // position is used now and later iterations converge once the
    // endpoint resolves. The START point must be resolved — it is
    // the anchor of the segment geometry.
    const Segment* seg = findSegment(pt.hostSegmentId);
    if (!seg) return false;
    const ParamPoint* sp = findPoint(seg->startPointId);
    const ParamPoint* ep = findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved) return false;

    // Degenerate-segment bootstrap: on a cold start an endpoint
    // anchored to an on-segment aux has NO cached pose (zero
    // position), so the segment is zero-length and this
    // intersection (which the endpoint depends on) can never fire.
    // The cached position (if any — warm/live case) is the designed
    // bootstrap and is left untouched; only a truly degenerate
    // cache gets the seeded polar formula (anchored at the segment
    // START) as a one-pass bootstrap — later fixpoint passes
    // re-anchor it to the true ref.
    // 宿主段几何按"有效位置"（含端点延长尾巴，D7b：交叉点跟实际线走）。
    const geo::Vec2 spEff = effectiveLocalPos(seg->startPointId);
    const geo::Vec2 epEff = effectiveLocalPos(seg->endPointId);
    geo::Vec2 segDir = epEff - spEff;
    double segLen = segDir.length();
    if (segLen < cad::geo::kGeomEps && !ep->resolved) {
        geo::Vec2 seed;
        if (polarEndpointCycleSeed(*ep, *this, *seg, *sp, params,
                                   conditioned, ctx, seed)) {
            segDir = seed - spEff;
            segLen = segDir.length();
        }
    }
    if (segLen < cad::geo::kGeomEps) return false;  // Degenerate segment.

    // Ray direction: aim-point mode (指向点) overrides the
    // numeric/formula angle — the ray points straight at
    // interAimPointId. An aim point outside this block defers to
    // the Resolver's cross-block pass (Step 6).
    double theta;
    if (!pt.interAimPointId.isNull()) {
        const ParamPoint* aim = findPoint(pt.interAimPointId);
        if (!aim || !aim->resolved) return false;
        geo::Vec2 toAim = aim->resolvedPos - origin->resolvedPos;
        if (toAim.lengthSquared() < cad::geo::kGeomEpsTight) return false;  // Coincident with origin.
        theta = std::atan2(toAim.y, toAim.x);
    } else {
        double baseAngle = std::atan2(segDir.y, segDir.x);
        // Evaluate ray angle (formula overrides numeric).
        double angleDeg = pt.interAngle;
        if (!pt.interAngleFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(pt.interAngleFormula, params, conditioned, ctx);
            if (r.ok) angleDeg = r.value;
        }
        if (pt.interUseWorldAngle) {
            theta = cad::geo::degToRad(angleDeg) - transform.rotation;
        } else {
            theta = baseAngle + cad::geo::degToRad(angleDeg);
        }
    }

    // Ray direction.
    geo::Vec2 d{std::cos(theta), std::sin(theta)};

    // Host segment as curve spans: empty for a line, spansForSegment output for
    // a Bézier. Both branches now go through ONE geometry entry point
    // (geo::raySegmentOrCurveIntersect — 审计 PAR-P0-3 / U6).
    std::vector<geo::BezierSpan> spans;
    if (seg->isCurve()) {
        // Unified span entry (single Hobby solve path). Mid-fixpoint
        // tolerance matches the old ad-hoc build: unresolved pass points are
        // skipped, a mid-cycle endpoint contributes its cached position.
        // (Curves do not support endpoint extension, so resolvedPos == the
        // effective position here — spEff/epEff only differ for lines.)
        spans = spansForSegment(*seg, /*skipUnresolvedPassPoints=*/true,
                                /*tolerateStaleEndpoints=*/true);
        if (spans.empty()) return false;
    }

    // Straight-line branch: R(s) = origin + s*d (s >= 0 unless bidirectional),
    // L(t) = spEff + t*segDir, t in [0,1]; segDir already carries the
    // degenerate-segment bootstrap seed when one was used.
    const auto hit = geo::raySegmentOrCurveIntersect(
        origin->resolvedPos, d, spEff, spEff + segDir, spans, pt.interBidirectional);
    if (!hit.hit) {
        if (idbg::enabled())
            idbg::log(QStringLiteral("[inter-local] MISS pt=%1 s=%2 t=%3")
                          .arg(pt.serial).arg(hit.s).arg(hit.t));
        return false;  // No valid intersection.
    }

    pt.resolvedPos = hit.point;
    pt.resolved = true;
    if (idbg::enabled())
        idbg::log(QStringLiteral("[inter-local] HIT pt=%1 local=(%2,%3)")
                      .arg(pt.serial).arg(pt.resolvedPos.x).arg(pt.resolvedPos.y));
    return true;
}

bool Block::resolveCurveAnchorPoint(ParamPoint& pt)
{
    // Curve pass-point (曲线点): positioned on the CHORD of its host
    // segment (start→end straight line) by a fraction + perpendicular
    // offset. Resolving against the chord (not the curve) avoids a
    // circular dependency — the anchor itself shapes the curve.
    const Segment* hostSeg = findSegment(pt.hostSegmentId);
    if (!hostSeg) return false;
    const ParamPoint* sp = findPoint(hostSeg->startPointId);
    const ParamPoint* ep = findPoint(hostSeg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return false;

    geo::Vec2 chord = ep->resolvedPos - sp->resolvedPos;
    const double len = chord.length();
    if (len < cad::geo::kGeomEps) {
        // Degenerate chord — sit on the start point.
        pt.resolvedPos = sp->resolvedPos;
        pt.resolved = true;
        return true;
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
    return true;
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
        // Unified span entry (single Hobby solve path, memoized per sweep —
        // the old ad-hoc build re-ran the full Hobby solve for EVERY aux
        // point on EVERY fixpoint pass). Strict on pass points (CurveAnchors
        // resolve inside Block::resolve, before this step), tolerant on the
        // END point: it may be mid-cycle (a break endpoint whose position
        // depends on THIS interpolated point) — its cached position is used
        // now and later iterations converge once the endpoint resolves.
        const auto spans = spansForSegment(*hostSeg, /*skipUnresolvedPassPoints=*/false,
                                           /*tolerateStaleEndpoints=*/true);
        if (spans.empty()) return false;

        // Per-span cumulative arc length built ONCE here (5-point GL per span),
        // shared by the total and the arc-length→parameter lookup — the old
        // code ran totalArcLength() and arcLengthToParam() separately, each
        // re-integrating every span.
        const std::vector<double> cumLen = geo::buildCumulativeArcLength(spans);
        const double totalArc = cumLen.back();
        if (totalArc < cad::geo::kGeomEps) {
            // Degenerate curve — sit on the start anchor (spans[0].p0).
            pt.resolvedPos = spans[0].p0;
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
        ConditionEngine::evaluateLengthMm(pt.interpConstantFormula, params, conditioned, constant, ctx);

        // Target arc-length position
        double targetS = totalArc * percent + constant;
        if (pt.interpFromEnd) targetS = totalArc - targetS;
        targetS = std::clamp(targetS, 0.0, totalArc);

        // Arc-length → parameter T → point + tangent
        double T = geo::arcLengthToParam(spans, targetS, &cumLen);
        geo::Vec2 basePos = geo::evalCurve(spans, T);
        geo::Vec2 tangent = geo::evalCurveTangent(spans, T);
        double baseAngle = std::atan2(tangent.y, tangent.x);
        if (pt.interpFromEnd) baseAngle += cad::geo::kPi;  // Flip direction

        // Offset (perpendicular to tangent)
        double offAngle = pt.interpOffsetAngle;
        if (!pt.interpOffsetAngleFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(pt.interpOffsetAngleFormula, params, conditioned, ctx);
            if (r.ok) offAngle = r.value;
        }
        double offDist = pt.interpOffsetDist;
        ConditionEngine::evaluateLengthMm(pt.interpOffsetDistFormula, params, conditioned, offDist, ctx);

        if (std::abs(offDist) > cad::geo::kGeomEps) {
            double angleRad = baseAngle + cad::geo::degToRad(offAngle);
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
    if (distAB < cad::geo::kGeomEps) {
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
    ConditionEngine::evaluateLengthMm(pt.interpConstantFormula, params, conditioned, constant, ctx);

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
    ConditionEngine::evaluateLengthMm(pt.interpOffsetDistFormula, params, conditioned, offDist, ctx);

    if (std::abs(offDist) > cad::geo::kGeomEps) {
        double angleRad = baseAngle + cad::geo::degToRad(offAngle);
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
        if (pt.resolvedPos.distanceSquaredTo(oldPos) > cad::geo::kGeomEpsLoose)
            touchGeometry();
    }
}


} // namespace cad::param
