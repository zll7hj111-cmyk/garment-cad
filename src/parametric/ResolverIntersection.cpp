#include "Resolver.h"

#include <cmath>
#include <algorithm>

#include "Block.h"
#include "ConditionEngine.h"
#include "parametric/IntersectDebug.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/Epsilon.h"
#include "geometry/RayCast.h"

namespace cad::param {

namespace {

/// Find a resolved point by UUID and return its world position.
/// Checks localBlock first as a fast path. Since UUIDs are globally unique,
/// locating the point in any block immediately halts the search (if unresolved,
/// it cannot exist in any subsequent block).
bool findResolvedPointWorld(const std::vector<Block>& blocks, const Block& localBlock,
                            const QUuid& pointId, geo::Vec2& outPos)
{
    if (pointId.isNull()) return false;
    if (const ParamPoint* lp = localBlock.findPoint(pointId)) {
        if (lp->resolved) {
            outPos = localBlock.worldPos(pointId);
            return true;
        }
        return false;
    }
    for (const auto& ob : blocks) {
        if (&ob == &localBlock) continue;
        if (const ParamPoint* op = ob.findPoint(pointId)) {
            if (op->resolved) {
                outPos = ob.worldPos(pointId);
                return true;
            }
            return false;
        }
    }
    return false;
}

} // namespace

bool Resolver::resolveCrossBlockIntersection(
    std::vector<Block>& blocks, Block& block, ParamPoint& pt,
    const QHash<QString, double>& params,
    const QHash<QString, QList<Condition>>& conditioned,
    EvalContext& ctx, int pass, Scope scope)
{
    // Skip if BOTH the origin and (when set) the aim point live in the
    // same block (already resolved in Step 1). An origin or aim point
    // outside this block is resolved here in world space.
    if (block.findPoint(pt.refPointA)
        && (pt.interAimPointId.isNull() || block.findPoint(pt.interAimPointId)))
        return false;

    // Find the origin point.
    geo::Vec2 originWorld;
    if (!findResolvedPointWorld(blocks, block, pt.refPointA, originWorld)) {
        if (idbg::enabled())
            idbg::log(QStringLiteral("[inter] origin NOT resolved pt=%1 pass=%2 scope=%3")
                          .arg(pt.serial).arg(pass).arg(int(scope)));
        return false;
    }
    if (idbg::enabled())
        idbg::log(QStringLiteral("[inter] eval pt=%1 pass=%2 scope=%3 origin=(%4,%5)")
                      .arg(pt.serial).arg(pass).arg(int(scope))
                      .arg(originWorld.x).arg(originWorld.y));

    // Target segment endpoints. The END point may be mid-cycle in the outer
    // fixpoint (e.g. a break endpoint whose position depends on this
    // intersection): its cached position is used now and later iterations
    // converge once the endpoint resolves. The START point must be resolved —
    // it anchors the segment geometry.
    const Segment* seg = block.findSegment(pt.hostSegmentId);
    if (!seg) return false;
    const ParamPoint* sp = block.findPoint(seg->startPointId);
    const ParamPoint* ep = block.findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved) return false;

    // Work in the block's LOCAL space. The transform is rigid (no scale), so
    // the ray/segment parameters and the validity tests are identical to the
    // world-space formulation, the hit comes back directly as the local
    // position we store, and this pass shares ONE geometry entry point with
    // the same-block pass (geo::raySegmentOrCurveIntersect — 审计 PAR-P0-3/U6,
    // the old world-space copy had no curve branch and hit the CHORD).
    const geo::Vec2 originLocal = block.transform.toLocal(originWorld);
    // 宿主段几何按"有效位置"（含端点延长尾巴，D7b：交叉点跟实际线走）。
    geo::Vec2 spEff = block.effectiveLocalPos(seg->startPointId);
    geo::Vec2 epEff = block.effectiveLocalPos(seg->endPointId);
    geo::Vec2 segDir = epEff - spEff;
    double segLen = segDir.length();
    // Degenerate-segment bootstrap: only when the endpoint has NO
    // cached pose either (cold start — zero position). The cached
    // pose of a warm/live doc is the designed bootstrap and is left
    // untouched. The seed evaluates the polar formula anchored at the
    // segment START so the intersection can fire; the fixpoint
    // re-anchors the endpoint once the aux resolves.
    if (segLen < cad::geo::kGeomEps && !ep->resolved) {
        geo::Vec2 seedLocal;
        if (Block::polarEndpointCycleSeed(*ep, block, *seg, *sp,
                                          params, conditioned, &ctx,
                                          seedLocal)) {
            epEff = seedLocal;
            segDir = epEff - spEff;
            segLen = segDir.length();
        }
    }
    if (segLen < cad::geo::kGeomEps && seg->fitKind != FitKind::Circle) return false;
    // A fitted circle host has a degenerate chord by construction (full circle:
    // start == end) — it is consumed through its spans below. The relative
    // angle anchors on the start tangent instead (CIRCLE_TOOL_DESIGN.md §16).

    // Ray direction (local). Aim-point mode (指向点) overrides the angle — the
    // ray points straight at interAimPointId. interUseWorldAngle means the
    // angle is measured against WORLD axes, so subtract the block rotation to
    // express it locally.
    double theta;
    if (!pt.interAimPointId.isNull()) {
        geo::Vec2 aimWorld;
        if (!findResolvedPointWorld(blocks, block, pt.interAimPointId, aimWorld))
            return false;
        const geo::Vec2 toAim = block.transform.toLocal(aimWorld) - originLocal;
        if (toAim.lengthSquared() < cad::geo::kGeomEpsTight) return false;  // Coincident with origin.
        theta = std::atan2(toAim.y, toAim.x);
    } else {
        double baseAngle = std::atan2(segDir.y, segDir.x);
        if (seg->fitKind == FitKind::Circle) {
            // Degenerate chord: anchor "relative to the host" on the CCW
            // tangent at the segment start (defined for a full circle).
            if (const ParamPoint* center = block.findPoint(sp->refPointId);
                center && center->resolved) {
                baseAngle = cad::geo::degToRad(
                    cad::geo::circleCcwTangentDeg(spEff, center->resolvedPos));
            }
        }

        // Evaluate angle (formula).
        double angleDeg = pt.interAngle;
        if (!pt.interAngleFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(pt.interAngleFormula, params, conditioned, &ctx);
            if (r.ok) angleDeg = r.value;
        }
        if (pt.interUseWorldAngle) {
            theta = cad::geo::degToRad(angleDeg) - block.transform.rotation;
        } else {
            theta = baseAngle + cad::geo::degToRad(angleDeg);
        }
    }
    geo::Vec2 d{std::cos(theta), std::sin(theta)};

    // Host segment as curve spans (local): empty for a line, spansForSegment
    // output for a Bézier — the branch this pass used to be missing.
    std::vector<geo::BezierSpan> spans;
    if (seg->isCurve()) {
        spans = block.spansForSegment(*seg, /*skipUnresolvedPassPoints=*/true,
                                      /*tolerateStaleEndpoints=*/true);
        if (spans.empty()) return false;
    }

    const auto hit = geo::raySegmentOrCurveIntersect(
        originLocal, d, spEff, spEff + segDir, spans, pt.interBidirectional);
    if (!hit.hit) {
        if (idbg::enabled())
            idbg::log(QStringLiteral("[inter] MISS pt=%1 s=%2 t=%3 (bidir=%4) prior=(%5,%6)")
                          .arg(pt.serial).arg(hit.s).arg(hit.t)
                          .arg(pt.interBidirectional ? 1 : 0)
                          .arg(pt.resolvedPos.x).arg(pt.resolvedPos.y));
        return false;
    }

    const geo::Vec2 newLocal = hit.point;
    if (idbg::enabled())
        idbg::log(QStringLiteral("[inter] HIT pt=%1 local=(%2,%3) moved=%4")
                      .arg(pt.serial).arg(newLocal.x).arg(newLocal.y)
                      .arg((pt.resolvedPos - newLocal).length()));
    if (!pt.resolved || pt.resolvedPos.distanceSquaredTo(newLocal) > cad::geo::kGeomEpsLoose)
        block.touchGeometry();
    const bool madeProgress = !pt.resolved;
    pt.resolvedPos = newLocal;
    pt.resolved = true;
    return madeProgress;
}

} // namespace cad::param
