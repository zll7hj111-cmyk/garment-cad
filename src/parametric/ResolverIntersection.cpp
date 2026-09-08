#include "Resolver.h"

#include <cmath>
#include <algorithm>

#include "Block.h"
#include "ConditionEngine.h"
#include "parametric/IntersectDebug.h"
#include "geometry/Units.h"

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

    // Target segment endpoints (world). The END point may be
    // mid-cycle in the outer fixpoint (e.g. a break endpoint whose
    // position depends on this intersection): its cached position is
    // used now and later iterations converge once the endpoint
    // resolves. The START point must be resolved — it anchors the
    // segment geometry.
    const Segment* seg = block.findSegment(pt.hostSegmentId);
    if (!seg) return false;
    const ParamPoint* sp = block.findPoint(seg->startPointId);
    const ParamPoint* ep = block.findPoint(seg->endPointId);
    if (!sp || !ep || !sp->resolved) return false;

    // 宿主段几何按"有效位置"（含端点延长尾巴，D7b：交叉点跟实际线走）。
    geo::Vec2 w1 = block.transform.toWorld(block.effectiveLocalPos(seg->startPointId));
    geo::Vec2 w2 = block.transform.toWorld(block.effectiveLocalPos(seg->endPointId));
    geo::Vec2 segDir = w2 - w1;
    double segLen = segDir.length();
    // Degenerate-segment bootstrap: only when the endpoint has NO
    // cached pose either (cold start — zero position). The cached
    // pose of a warm/live doc is the designed bootstrap and is left
    // untouched. The seed evaluates the polar formula anchored at the
    // segment START so the intersection can fire; the fixpoint
    // re-anchors the endpoint once the aux resolves.
    if (segLen < 1e-9 && !ep->resolved) {
        geo::Vec2 seedLocal;
        if (Block::polarEndpointCycleSeed(*ep, block, *seg, *sp,
                                          params, conditioned, &ctx,
                                          seedLocal)) {
            w2 = block.transform.toWorld(seedLocal);
            segDir = w2 - w1;
            segLen = segDir.length();
        }
    }
    if (segLen < 1e-9) return false;

    // Ray direction: aim-point mode (指向点) overrides the angle —
    // the ray points straight at interAimPointId (world space).
    double theta;
    if (!pt.interAimPointId.isNull()) {
        geo::Vec2 aimWorld;
        if (!findResolvedPointWorld(blocks, block, pt.interAimPointId, aimWorld))
            return false;
        geo::Vec2 toAim = aimWorld - originWorld;
        if (toAim.lengthSquared() < 1e-12) return false;  // Coincident with origin.
        theta = std::atan2(toAim.y, toAim.x);
    } else {
        double baseAngle = std::atan2(segDir.y, segDir.x);

        // Evaluate angle (formula).
        double angleDeg = pt.interAngle;
        if (!pt.interAngleFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(pt.interAngleFormula, params, conditioned, &ctx);
            if (r.ok) angleDeg = r.value;
        }
        if (pt.interUseWorldAngle) {
            theta = angleDeg * M_PI / 180.0;
        } else {
            theta = baseAngle + angleDeg * M_PI / 180.0;
        }
    }
    geo::Vec2 d{std::cos(theta), std::sin(theta)};

    double denom = d.cross(segDir);
    if (std::abs(denom) < 1e-9) return false;  // Parallel.

    geo::Vec2 w = w1 - originWorld;
    double s = w.cross(segDir) / denom;
    double t = w.cross(d) / denom;

    constexpr double eps = 1e-6;
    bool validT = (t >= -eps && t <= 1.0 + eps);
    bool validS = pt.interBidirectional ? true : (s >= -eps);
    if (!validT || !validS) {
        if (idbg::enabled())
            idbg::log(QStringLiteral("[inter] MISS pt=%1 s=%2 t=%3 (bidir=%4) prior=(%5,%6)")
                          .arg(pt.serial).arg(s).arg(t)
                          .arg(pt.interBidirectional ? 1 : 0)
                          .arg(pt.resolvedPos.x).arg(pt.resolvedPos.y));
        return false;
    }

    geo::Vec2 hitWorld = originWorld + d * s;
    const geo::Vec2 newLocal = block.transform.toLocal(hitWorld);
    if (idbg::enabled())
        idbg::log(QStringLiteral("[inter] HIT pt=%1 hit=(%2,%3) local=(%4,%5) moved=%6")
                      .arg(pt.serial).arg(hitWorld.x).arg(hitWorld.y)
                      .arg(newLocal.x).arg(newLocal.y)
                      .arg((pt.resolvedPos - newLocal).length()));
    if (!pt.resolved || pt.resolvedPos.distanceSquaredTo(newLocal) > 1e-6)
        block.touchGeometry();
    const bool madeProgress = !pt.resolved;
    pt.resolvedPos = newLocal;
    pt.resolved = true;
    return madeProgress;
}

} // namespace cad::param
