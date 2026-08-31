#include "Block.h"

#include <algorithm>
#include <cmath>

#include "parametric/ExpressionEvaluator.h"
#include "parametric/ConditionEngine.h"
#include "parametric/IntersectDebug.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"

#include <cstring>

namespace cad::param {

namespace {

/// Hash one double into a running fingerprint (boost-style combine on the
/// bit pattern). Used by Block::spansForSegment's memo key.
inline quint64 fpMix(quint64 h, double v)
{
    quint64 bits = 0;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(v));
    h ^= bits + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

/// Fingerprint of everything that determines a curve's Bézier spans: anchor
/// positions, stored tangents, auto flags and the segment tension. Any
/// change to the curve shape changes the key — so a memo hit is ALWAYS the
/// same spans a fresh Hobby solve would produce, mid-fixpoint included.
quint64 curveAnchorFingerprint(const std::vector<geo::Vec2>& pts,
                               const std::vector<geo::Vec2>& tIn,
                               const std::vector<geo::Vec2>& tOut,
                               const std::vector<bool>& autoTan,
                               double tension)
{
    quint64 h = 0xcbf29ce484222325ULL;
    h = fpMix(h, static_cast<double>(pts.size()));
    h = fpMix(h, tension);
    for (size_t i = 0; i < pts.size(); ++i) {
        h = fpMix(h, pts[i].x);   h = fpMix(h, pts[i].y);
        h = fpMix(h, tIn[i].x);   h = fpMix(h, tIn[i].y);
        h = fpMix(h, tOut[i].x);  h = fpMix(h, tOut[i].y);
        h = fpMix(h, (i < autoTan.size() && autoTan[i]) ? 1.0 : 0.0);
    }
    return h;
}

} // namespace

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
            points[i].resolvedPos.distanceSquaredTo(prevPos[i]) > 1e-6) {
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
    const double angleRad = baseAngle + ang * M_PI / 180.0;
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

    double angleRad = baseAngle + ang * M_PI / 180.0;
    pt.resolvedPos = ref->resolvedPos + geo::Vec2{
        dist * std::cos(angleRad),
        dist * std::sin(angleRad)
    };
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
    if (segLen < 1e-9 && !ep->resolved) {
        geo::Vec2 seed;
        if (polarEndpointCycleSeed(*ep, *this, *seg, *sp, params,
                                   conditioned, ctx, seed)) {
            segDir = seed - spEff;
            segLen = segDir.length();
        }
    }
    if (segLen < 1e-9) return false;  // Degenerate segment.

    // Ray direction: aim-point mode (指向点) overrides the
    // numeric/formula angle — the ray points straight at
    // interAimPointId. An aim point outside this block defers to
    // the Resolver's cross-block pass (Step 6).
    double theta;
    if (!pt.interAimPointId.isNull()) {
        const ParamPoint* aim = findPoint(pt.interAimPointId);
        if (!aim || !aim->resolved) return false;
        geo::Vec2 toAim = aim->resolvedPos - origin->resolvedPos;
        if (toAim.lengthSquared() < 1e-12) return false;  // Coincident with origin.
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
            theta = angleDeg * M_PI / 180.0 - transform.rotation;
        } else {
            theta = baseAngle + angleDeg * M_PI / 180.0;
        }
    }

    // Ray direction.
    geo::Vec2 d{std::cos(theta), std::sin(theta)};

    // --- Curve target: rayCurveIntersect ---
    if (seg->isCurve()) {
        // Unified span entry (single Hobby solve path). Mid-fixpoint
        // tolerance matches the old ad-hoc build: unresolved pass points are
        // skipped, a mid-cycle endpoint contributes its cached position.
        // (Curves do not support endpoint extension, so resolvedPos == the
        // effective position here — spEff/epEff only differ for lines.)
        const auto spans = spansForSegment(*seg, /*skipUnresolvedPassPoints=*/true,
                                           /*tolerateStaleEndpoints=*/true);
        if (spans.empty()) return false;

        auto hits = geo::rayCurveIntersect(origin->resolvedPos, d, spans, pt.interBidirectional);
        if (hits.empty()) return false;

        pt.resolvedPos = hits[0].point;
        pt.resolved = true;
        return true;
    }

    // --- Straight-line target: cross-product method ---
    // Ray-segment intersection via cross products.
    // Ray: R(s) = origin + s*d,  s >= 0 (or any s if bidirectional)
    // Segment: L(t) = sp + t*segDir,  t in [0,1]
    double denom = d.cross(segDir);
    if (std::abs(denom) < 1e-9) {
        // Parallel — no intersection; keep last position if available.
        return false;
    }

    geo::Vec2 w = spEff - origin->resolvedPos;
    double s = w.cross(segDir) / denom;  // Ray parameter.
    double t = w.cross(d) / denom;       // Segment parameter.

    // Validity check.
    constexpr double eps = 1e-6;
    bool validT = (t >= -eps && t <= 1.0 + eps);
    bool validS = pt.interBidirectional ? true : (s >= -eps);
    if (!validT || !validS) {
        if (idbg::enabled())
            idbg::log(QStringLiteral("[inter-local] MISS pt=%1 s=%2 t=%3")
                          .arg(pt.serial).arg(s).arg(t));
        return false;  // No valid intersection.
    }

    pt.resolvedPos = origin->resolvedPos + d * s;
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
    if (len < 1e-9) {
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

        double totalArc = geo::totalArcLength(spans);
        if (totalArc < 1e-9) {
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
        ConditionEngine::evaluateLengthMm(pt.interpOffsetDistFormula, params, conditioned, offDist, ctx);

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
            touchGeometry();
    }
}

bool Block::collectCurveAnchors(const Segment& seg,
                                std::vector<geo::Vec2>& pts,
                                std::vector<geo::Vec2>& tIn,
                                std::vector<geo::Vec2>& tOut,
                                std::vector<bool>& autoTan,
                                bool skipUnresolvedPass,
                                bool tolerateStaleEndpoints) const
{
    pts.clear(); tIn.clear(); tOut.clear(); autoTan.clear();

    const ParamPoint* sp = findPoint(seg.startPointId);
    const ParamPoint* ep = findPoint(seg.endPointId);
    if (!sp || !ep) return false;
    if (!tolerateStaleEndpoints && (!sp->resolved || !ep->resolved))
        return false;
    // Tolerant mode: an endpoint mid-cycle in the fixpoint keeps its CACHED
    // resolvedPos — the fixpoint converges once the endpoint resolves
    // (endpoint↔query dependency cycle, e.g. break endpoints).
    if (!sp->resolved || !ep->resolved) {
        // Never fabricate geometry from a never-resolved (zero) endpoint.
        if (sp->resolvedPos.lengthSquared() < 1e-12 &&
            ep->resolvedPos.lengthSquared() < 1e-12)
            return false;
    }

    pts.push_back(sp->resolvedPos);
    tIn.push_back(sp->tangentIn);  tOut.push_back(sp->tangentOut);  autoTan.push_back(sp->autoTangent);
    for (const auto& ppId : seg.passPointIds) {
        const ParamPoint* pp = findPoint(ppId);
        if (!pp || !pp->resolved) {
            if (skipUnresolvedPass) continue;  // mid-fixpoint tolerance
            return false;
        }
        pts.push_back(pp->resolvedPos);
        tIn.push_back(pp->tangentIn); tOut.push_back(pp->tangentOut); autoTan.push_back(pp->autoTangent);
    }
    pts.push_back(ep->resolvedPos);
    tIn.push_back(ep->tangentIn);  tOut.push_back(ep->tangentOut);  autoTan.push_back(ep->autoTangent);
    return true;
}

std::vector<geo::BezierSpan> Block::spansForSegment(
    const Segment& seg, bool skipUnresolvedPassPoints,
    bool tolerateStaleEndpoints) const
{
    if (!seg.isCurve()) return {};

    std::vector<geo::Vec2> pts, tIn, tOut;
    std::vector<bool> autoTan;
    if (!collectCurveAnchors(seg, pts, tIn, tOut, autoTan,
                             skipUnresolvedPassPoints, tolerateStaleEndpoints))
        return {};
    if (pts.size() < 2) return {};

    // Memo by anchor fingerprint: within one settle sweep the anchors do not
    // move (aux/intersection points never shape the curve), so N consumers
    // on the same curve share ONE Hobby solve. The fingerprint — not the
    // geometry epoch — is the validity key, because mid-fixpoint positions
    // change BEFORE the epoch bump (Block::resolve bumps it after the
    // fixpoint), so an epoch check would serve stale spans exactly when the
    // resolve-time callers run.
    const quint64 fp = curveAnchorFingerprint(pts, tIn, tOut, autoTan, seg.tension);
    const auto it = m_spanMemo.constFind(seg.id);
    if (it != m_spanMemo.constEnd() && it->fingerprint == fp)
        return it->spans;

    auto spans = geo::buildBezierSpans(pts, tIn, tOut, autoTan, seg.tension,
                                       geo::AutoCurveMode::Hobby);
    m_spanMemo.insert(seg.id, CurveSpanMemo{fp, spans});
    return spans;
}

void Block::rebuildCurveCache()
{
    ++curveCacheBuilds;               // telemetry: rigid drags must NOT grow this
    m_curveCacheEpoch = m_geometryEpoch;
    m_curveSpans.clear();
    m_curveSpans.reserve(segments.size());
    m_curveSpanIndex.clear();
    m_curveSpanIndex.reserve(segments.size());
    m_spanMemo.clear();  // repopulated below with the canonical spans

    for (const auto& seg : segments) {
        if (!seg.isCurve()) continue;

        std::vector<geo::Vec2> pts, tIn, tOut;
        std::vector<bool> autoTan;
        if (!collectCurveAnchors(seg, pts, tIn, tOut, autoTan)) continue;

        std::vector<geo::BezierSpan> spans =
            geo::buildBezierSpans(pts, tIn, tOut, autoTan, seg.tension,
                                  geo::AutoCurveMode::Hobby);
        if (spans.empty()) continue;

        // Prime the spansForSegment memo with the canonical spans so
        // post-resolve consumers (tools, commands) reuse this solve.
        m_spanMemo.insert(seg.id, CurveSpanMemo{
            curveAnchorFingerprint(pts, tIn, tOut, autoTan, seg.tension), spans});

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
        entry.arcLengthMm  = geo::totalArcLength(entry.spans);
        // Label anchor: the ARC-LENGTH midpoint of the whole curve. The old
        // evalCurve(spans, 0.5) was the parametric middle of span 0 — on
        // multi-anchor curves (3+ spans) the name/length labels drifted
        // toward the start of the curve instead of sitting at its middle.
        const double tMid = geo::arcLengthToParam(entry.spans, entry.arcLengthMm * 0.5);
        entry.labelLocal   = geo::evalCurve(entry.spans, tMid);
        entry.labelLocalDir = geo::evalCurveTangent(entry.spans, tMid);
        m_curveSpanIndex.insert(seg.id, static_cast<int>(m_curveSpans.size()));
        m_curveSpans.push_back(std::move(entry));
    }
}

const CurveSpanEntry* Block::curveSpanEntry(const QUuid& segmentId) const
{
    // Indexed lookup: this accessor runs per curve per snap query / per canvas
    // cache rebuild — a linear scan is O(k^2) per block with k curves.
    const auto it = m_curveSpanIndex.constFind(segmentId);
    if (it == m_curveSpanIndex.constEnd())
        return nullptr;
    const int idx = it.value();
    if (idx < 0 || idx >= static_cast<int>(m_curveSpans.size()))
        return nullptr;
    const auto& e = m_curveSpans[static_cast<size_t>(idx)];
    // idempotency guard: the index and spans are rebuilt together, so a stale
    // entry can only appear via direct vector mutation — verify before use.
    return e.segmentId == segmentId ? &e : nullptr;
}

geo::Vec2 Block::worldPos(const QUuid& pointId) const
{
    return transform.toWorld(effectiveLocalPos(pointId));
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
        if (seg.startPointId != pointId && seg.endPointId != pointId) continue;
        return segmentEffectiveLength(seg.id);
    }
    return 0.0;
}

geo::Vec2 Block::effectiveLocalPos(const QUuid& pointId) const
{
    // 按当前位置即时计算（求解 pass 内也不依赖缓存）：有效位置 = 本体 +
    // 该端点所属段的延长量 × 本体出方向。无延长/非端点 = 本体位。
    const ParamPoint* pt = findPoint(pointId);
    if (!pt) return geo::Vec2::zero();
    // 无延长（绝大多数文档的常态）：本体位即有效位，跳过逐段扫描。
    // 与旧行为等价：m_extendEval 为空时下面的循环也必然找不到匹配段。
    if (m_extendEval.isEmpty())
        return pt->resolvedPos;
    for (const auto& seg : segments) {
        auto it = m_extendEval.constFind(seg.id);
        if (it == m_extendEval.constEnd()) continue;
        const bool isStart = (seg.startPointId == pointId);
        const bool isEnd   = (seg.endPointId == pointId);
        if (!isStart && !isEnd) continue;
        const double ext = isStart ? it->startMm : it->endMm;
        if (ext <= 0.0) continue;
        const ParamPoint* sp = findPoint(seg.startPointId);
        const ParamPoint* ep = findPoint(seg.endPointId);
        if (!sp || !ep || !sp->resolved) return pt->resolvedPos;
        geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;
        const double len = dir.length();
        if (len < 1e-9) return pt->resolvedPos;  // 退化段: 无出方向
        dir = dir / len;
        // 出方向与 exitDirectionAtPoint 一致：终点 = start→end；起点 = end→start。
        if (isEnd)
            return ep->resolvedPos + dir * ext;
        return sp->resolvedPos - dir * ext;
    }
    return pt->resolvedPos;
}

double Block::segmentBaseLength(const QUuid& segmentId) const
{
    const Segment* seg = findSegment(segmentId);
    if (!seg) return 0.0;
    const ParamPoint* sp = findPoint(seg->startPointId);
    const ParamPoint* ep = findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved) return 0.0;
    if (seg->isCurve()) {
        if (const CurveSpanEntry* entry = curveSpanEntry(seg->id);
            entry && !entry->spans.empty())
            return geo::totalArcLength(entry->spans);
    }
    return ep->resolved ? sp->resolvedPos.distanceTo(ep->resolvedPos) : 0.0;
}

double Block::segmentEffectiveLength(const QUuid& segmentId) const
{
    const Segment* seg = findSegment(segmentId);
    if (!seg) return 0.0;
    if (seg->isCurve())
        return segmentBaseLength(segmentId);  // 曲线不支持延长（D3）
    const ParamPoint* sp = findPoint(seg->startPointId);
    const ParamPoint* ep = findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return 0.0;
    return effectiveLocalPos(sp->id).distanceTo(effectiveLocalPos(ep->id));
}

double Block::segmentExtendStart(const QUuid& segmentId) const
{
    auto it = m_extendEval.constFind(segmentId);
    return it == m_extendEval.constEnd() ? 0.0 : it->startMm;
}

double Block::segmentExtendEnd(const QUuid& segmentId) const
{
    auto it = m_extendEval.constFind(segmentId);
    return it == m_extendEval.constEnd() ? 0.0 : it->endMm;
}

bool Block::segmentSnapWithinBase(const QUuid& segmentId, double t) const
{
    const Segment* seg = findSegment(segmentId);
    if (!seg) return true;
    const ParamPoint* sp = findPoint(seg->startPointId);
    const ParamPoint* ep = findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return true;
    const double baseLen = segmentBaseLength(segmentId);
    const double effLen  = segmentEffectiveLength(segmentId);
    if (effLen < 1e-9) return true;
    // t 沿有效段（SnapEngine 用有效端点投影）。距本体起点的距离 =
    // t·effLen − 起点延长量；本体范围 = [0, baseLen]。
    const double along = t * effLen - segmentExtendStart(segmentId);
    constexpr double kEps = 1e-6;
    return along >= -kEps && along <= baseLen + kEps;
}

void Block::evaluateExtendValues(const QHash<QString, double>& params,
                                 const QHash<QString, QList<Condition>>& conditioned,
                                 EvalContext* ctx)
{
    // 求值各段延长量（数值 mm / 公式 cm 域；防御性 clamp ≥0 — 只往外，D2）。
    QHash<QUuid, ExtendEval> extendEval;
    for (const auto& seg : segments) {
        ExtendEval e;
        e.startMm = seg.extendStartMm;
        ConditionEngine::evaluateLengthMm(seg.extendStartFormula, params, conditioned, e.startMm, ctx);
        e.endMm = seg.extendEndMm;
        ConditionEngine::evaluateLengthMm(seg.extendEndFormula, params, conditioned, e.endMm, ctx);
        if (e.startMm < 0.0) e.startMm = 0.0;
        if (e.endMm   < 0.0) e.endMm   = 0.0;
        if (e.startMm > 0.0 || e.endMm > 0.0)
            extendEval.insert(seg.id, e);
    }
    m_extendEval = std::move(extendEval);
}

void Block::applyEffectivePositions()
{
    // 无延长（包括公式求值为 0）：清空缓存；上帧有尾巴（刚被清零）→ 可视几何
    // 变化，显式 +epoch 触发重绘。
    if (m_extendEval.isEmpty()) {
        if (!m_effectiveLocal.isEmpty())
            touchGeometry();
        m_effectiveLocal.clear();
        return;
    }

    // 本体位置入缓存，再叠加端点延长（出方向 = 本体方向，与 exitDirectionAtPoint
    // 的"延长方向"语义一致：终点 = start→end，起点 = end→start）。
    QHash<QUuid, geo::Vec2> eff;
    eff.reserve(points.size());
    for (const auto& pt : points)
        eff.insert(pt.id, pt.resolvedPos);

    for (const auto& seg : segments) {
        auto it = m_extendEval.constFind(seg.id);
        if (it == m_extendEval.constEnd()) continue;
        const ParamPoint* sp = findPoint(seg.startPointId);
        const ParamPoint* ep = findPoint(seg.endPointId);
        if (!sp || !ep || !sp->resolved || !ep->resolved) continue;
        geo::Vec2 dir = ep->resolvedPos - sp->resolvedPos;  // start→end (本体)
        const double len = dir.length();
        if (len < 1e-9) continue;  // 退化线段：无出方向，跳过
        const geo::Vec2 u = dir / len;
        if (it->startMm > 0.0)
            eff[sp->id] = sp->resolvedPos - u * it->startMm;  // 起点往起点外
        if (it->endMm > 0.0)
            eff[ep->id] = ep->resolvedPos + u * it->endMm;    // 终点往终点外
    }

    // 可视几何变化检测：本体不动但尾巴变了 → 显式 +epoch（画布重绘铁律）。
    // 与上帧有效缓存比较，稳定后每帧零开销。
    if (!m_effectiveLocal.isEmpty()) {
        bool moved = m_effectiveLocal.size() != eff.size();
        if (!moved) {
            for (auto cit = eff.constBegin(); cit != eff.constEnd(); ++cit) {
                auto prev = m_effectiveLocal.constFind(cit.key());
                if (prev == m_effectiveLocal.constEnd() ||
                    prev->distanceSquaredTo(cit.value()) > 1e-6) {
                    moved = true;
                    break;
                }
            }
        }
        if (moved)
            touchGeometry();
    }
    m_effectiveLocal = std::move(eff);
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
