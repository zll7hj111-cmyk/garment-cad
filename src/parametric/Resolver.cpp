#include "Resolver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <QHash>

#include "Block.h"
#include "Attachment.h"
#include "ConditionEngine.h"
#include "parametric/PerfProbe.h"
#include "parametric/IntersectDebug.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"

namespace cad::param {

namespace {

/// Append a diagnostic unless the same (kind, attachment) pair was already
/// reported (the resolve loop visits each attachment multiple times).
void report(std::vector<ResolveDiagnostic>* diagnostics,
            ResolveDiagnostic::Kind kind, const QUuid& attachmentId)
{
    if (!diagnostics) return;
    for (const auto& d : *diagnostics)
        if (d.kind == kind && d.attachmentId == attachmentId) return;
    diagnostics->push_back({kind, attachmentId});
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

    // Find the origin point in another block.
    geo::Vec2 originWorld;
    bool found = false;
    for (const auto& ob : blocks) {
        const ParamPoint* op = ob.findPoint(pt.refPointA);
        if (op && op->resolved) {
            originWorld = ob.worldPos(pt.refPointA);
            found = true;
            break;
        }
    }
    if (!found) {
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
        bool aimFound = false;
        for (const auto& ob : blocks) {
            const ParamPoint* ap = ob.findPoint(pt.interAimPointId);
            if (ap && ap->resolved) {
                aimWorld = ob.worldPos(pt.interAimPointId);
                aimFound = true;
                break;
            }
        }
        if (!aimFound) return false;
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

void Resolver::resolveAll(std::vector<Block>& blocks,
                          const std::vector<Attachment>& attachments,
                          const QHash<QString, double>& params,
                          const QHash<QString, QList<Condition>>& conditioned,
                          std::vector<ResolveDiagnostic>* diagnostics,
                          Scope scope,
                          const QUuid& auxLayerId,
                          const QSet<QUuid>* affectedOnly,
                          ExpressionCache* exprCache)
{
    if (diagnostics) diagnostics->clear();
    GCAD_PERF_SCOPE("r.total");

    // Layer-scope filter: true when a block belongs to the group this pass
    // is allowed to MOVE. Out-of-scope blocks keep their cached transforms
    // and act as static geometry (targets) only.
    // affectedOnly (drag-follow mode) narrows the moving set further: only
    // blocks in the dirty subgraph may move, everything else is static.
    const auto inScope = [&](const Block& b) {
        if (affectedOnly && !affectedOnly->contains(b.id))
            return false;
        switch (scope) {
        case Scope::All:         return true;
        case Scope::AuxOnly:     return b.layer == auxLayerId;
        case Scope::WorkingOnly: return b.layer != auxLayerId;
        }
        return true;
    };

    // Per-pass memo: params/conditioned are constant throughout this call, so
    // identical formula texts (shared by many points/attachments) execute once.
    EvalContext ctx;
    // Compile cache: the caller's (document-owned) instance when provided,
    // else cacheFor() falls back to the thread-local default.
    ctx.cache = exprCache;

    // Step 1: Resolve each block's internal points independently.
    {
        GCAD_PERF_SCOPE("r.blocks");
        for (auto& block : blocks) {
            if (!inScope(block)) continue;
            block.resolve(params, conditioned, &ctx);
        }
    }

    // Step 2: Build O(1) index for attachment resolution (ALL blocks —
    // out-of-scope blocks may still be looked up as targets).
    QHash<QUuid, int> blockIndex;
    blockIndex.reserve(static_cast<int>(blocks.size()));
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i)
        blockIndex.insert(blocks[i].id, i);

    // Settle the (non-pin) attachment forest in TOPOLOGICAL order. The forest
    // invariant (see AttachmentGraph.h) guarantees every block is the follower
    // of AT MOST one regular attachment and the leader links are acyclic, so
    // settling leaders before followers resolves the whole forest in a SINGLE
    // pass — no iterative relaxation. This is O(blocks + attachments) instead of
    // the old O(iterations x attachments), which degraded to O(N^2) for deep
    // chains whose attachments happened to be ordered against the dependency
    // direction.
    //
    // Attachments are scoped by their FROM block (the one that moves). The
    // one-way cross-layer rule permits ONLY aux follower → working leader;
    // in a WorkingOnly pass such an edge's follower is out of scope (frozen),
    // and in an AuxOnly pass its working leader is out of scope, so the aux
    // follower becomes a root that settles against the static leader. A
    // follower whose leader is OUT of scope (frozen) is always a root: it
    // settles against the static leader immediately. Returns true on
    // non-convergence (a cycle that the invariant should make impossible —
    // reported for safety).
    auto settleAttachments = [&](bool preserveEndTargetRotation = false) -> bool {
        GCAD_PERF_SCOPE("r.settle");
        const int n = static_cast<int>(blocks.size());

        // Each follower is driven by its position leader (toBlock) and, when a
        // separate angle reference is configured, also by the angle-ref block.
        // This is a small DAG: a block is settled only after all in-scope
        // dependencies (position leader + optional angle reference) are final.
        std::vector<int> incoming(n, -1);
        std::vector<std::vector<int>> dependents(n);
        std::vector<int> remaining(n, 0);
        std::vector<char> inScopeArr(n, 0);
        for (int b = 0; b < n; ++b)
            inScopeArr[b] = inScope(blocks[b]) ? 1 : 0;

        int toSettle = 0;
        for (int ai = 0; ai < static_cast<int>(attachments.size()); ++ai) {
            const auto& att = attachments[ai];
            // Bridge pins are pure position constraints resolved in Step 4 —
            // they never participate in the leader forest settlement.
            if (att.isPin) continue;
            auto fromIt = blockIndex.find(att.fromBlockId);
            if (fromIt == blockIndex.end()) {              // dangling from-block
                report(diagnostics, ResolveDiagnostic::Kind::DanglingBlock, att.id);
                continue;
            }
            const int fi = fromIt.value();
            if (!inScopeArr[fi]) continue;                // frozen group
            incoming[fi] = ai;
            ++toSettle;

            auto addDep = [&](const QUuid& depBlockId) {
                if (depBlockId.isNull()) return;
                auto depIt = blockIndex.find(depBlockId);
                if (depIt == blockIndex.end()) return;     // missing = static/absent
                const int di = depIt.value();
                if (di == fi || !inScopeArr[di]) return;   // self or frozen
                dependents[di].push_back(fi);
                ++remaining[fi];
            };
            addDep(att.toBlockId);
            addDep(att.angleRefBlockId);
        }

        // Seed with every in-scope block whose dependencies are already final.
        std::vector<int> queue;
        queue.reserve(n);
        for (int b = 0; b < n; ++b) {
            if (inScopeArr[b] && remaining[b] == 0)
                queue.push_back(b);
        }

        int settled = 0;
        for (size_t head = 0; head < queue.size(); ++head) {
            const int b = queue[head];
            if (incoming[b] >= 0) {
                const auto& att = attachments[incoming[b]];
                auto toIt = blockIndex.find(att.toBlockId);
                const Block* angleRef = nullptr;
                if (!att.angleRefBlockId.isNull()) {
                    auto refIt = blockIndex.find(att.angleRefBlockId);
                    if (refIt != blockIndex.end())
                        angleRef = &blocks[refIt.value()];
                }
                if (toIt == blockIndex.end()) {
                    report(diagnostics, ResolveDiagnostic::Kind::DanglingBlock, att.id);
                } else {
                    applyAttachment(blocks[b], att, blocks[toIt.value()], angleRef,
                                    params, conditioned, diagnostics, &ctx,
                                    preserveEndTargetRotation);
                    ++settled;
                }
            }
            for (const int d : dependents[b]) {
                if (--remaining[d] == 0)
                    queue.push_back(d);
            }
        }

        // Unsettled owners mean a cycle/unreachable block (forest invariant
        // violation) — mirror the old iterative budget's non-convergence signal.
        return settled < toSettle;
    };

    // Step 3: settle the attachment forest. Bridges are not yet final, so
    // followers led BY a bridge get a second settlement in Step 5.
    if (settleAttachments())
        report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());

    // Step 4: bridges resolve LAST — pure downstream leaves whose two endpoints
    // are pinned to already-settled host points (桥接线, see Block::isBridge).
    // Length/direction are passive: instead of driving a rigid transform, both
    // pinned points are placed directly on their hosts (origin = first pin's
    // host, rotation = 0), so the segment between them stretches to fit.
    // bridgesMoved records whether ANY bridge actually changed position — Step 5
    // only needs to re-settle when a bridge moved (otherwise the forest settled
    // in Step 3 is still valid and the extra settle pass is pure waste).
    bool bridgesMoved = false;
    for (auto& bridge : blocks) {
        if (!bridge.isBridge) continue;
        if (!inScope(bridge)) continue;  // frozen group

        struct Pin { QUuid fromPointId; geo::Vec2 hostWorld; };
        std::vector<Pin> pins;
        for (const auto& att : attachments) {
            if (!att.isPin || att.fromBlockId != bridge.id) continue;
            auto toIt = blockIndex.find(att.toBlockId);
            if (toIt == blockIndex.end()) {
                report(diagnostics, ResolveDiagnostic::Kind::DanglingBlock, att.id);
                continue;
            }
            const Block& host = blocks[toIt.value()];
            const ParamPoint* hp = host.findPoint(att.toPointId);
            if (!hp || !hp->resolved) {
                report(diagnostics, ResolveDiagnostic::Kind::DanglingPoint, att.id);
                continue;
            }
            pins.push_back({att.fromPointId, host.worldPos(att.toPointId)});
        }
        // A healthy bridge always has both pins (ParamDocument releases broken
        // ones as independent segments); mid-construction states are skipped
        // silently.
        if (pins.size() < 2) continue;

        bridge.transform.rotation = 0.0;
        bridge.transform.origin   = pins[0].hostWorld;
        for (const auto& pin : pins) {
            ParamPoint* pt = bridge.findPoint(pin.fromPointId);
            if (!pt) continue;
            const geo::Vec2 newPos = pin.hostWorld - bridge.transform.origin;
            if (pt->resolvedPos.distanceSquaredTo(newPos) > 1e-6) {
                bridge.touchGeometry();
                bridgesMoved = true;
            }
            pt->resolvedPos = newPos;
            pt->resolved    = true;
        }

        // The pinned endpoints just "stretched" onto their hosts; re-resolve
        // the bridge's auxiliary (Interpolated) points so they track the
        // stretched segment rather than the pre-stretch local construction.
        bridge.resolveInterpolatedPoints(params, conditioned, &ctx);
    }

    // Step 5: attachments led BY a bridge are only final now (the bridge's
    // transform and aux points were settled in Step 4). Re-settle the forest
    // so bridge followers (and anything downstream of them) land correctly.
    // Skipped entirely when no bridge moved — the Step 3 settlement is still
    // valid then, and an extra settle pass is the single biggest per-frame cost.

    // Step 6/6b/6c: cross-block intersection points and the interpolated points
    // that depend on them, in a SHARED bounded fixpoint. An intersection's ray
    // origin (refPointA) or aim point (interAimPointId) can live in ANOTHER
    // block; the aim may itself be an interpolated aux point whose reference
    // is a cross-block intersection — one Step-6 pass is NOT enough (the aim
    // only resolves during 6b, so the intersection would stay unresolved).
    // Loop both until no progress, bounded like the old Step 6b.
    //
    // Extracted as a lambda so Step 7's endpoint-aim rotations (which rotate
    // the TARGET segment of intersections on the same block) can re-run it —
    // a stale local intersection drifts off the origin→borrow ray (用户回归:
    // P612 在肩褶高 15/20 时不共线).
    // @param budgetExhausted set when every round still made progress, i.e. the
    //        geometry was still moving when the budget ran out (no fixed point).
    auto runIntersectionFixpoint = [&](bool* budgetExhausted = nullptr) -> bool {
        bool geoProgressed = false;
        bool converged = false;
        for (int pass = 0; pass < kMaxSettleRounds; ++pass) {
            bool progressed = false;

    // --- Step 6: cross-block intersections ---
    for (auto& block : blocks) {
        if (!inScope(block)) continue;  // frozen group
        for (auto& pt : block.points) {
            if (pt.constraint != PointConstraint::Intersection) continue;
            if (resolveCrossBlockIntersection(blocks, block, pt, params,
                                              conditioned, ctx, pass, scope))
                progressed = true;
        }
    }

    // --- Step 6b: interpolated points referencing (possibly fresh)
    // --- cross-block intersections as their measurement origin. Bounded
    // fixpoint handles chains (aux -> aux -> intersection).
    //
    // NOTE: re-evaluate ALL interpolated points, not only the unresolved ones.
    // A point whose ref is a cross-block intersection resolved in Step 6 keeps
    // resolved=true with a STALE position (Step 1 evaluated it against the
    // OLD intersection), and skipping it would freeze the chain — the borrow
    // point of P612 never followed parameter changes, so the intersection
    // drifted off the origin→borrow ray (用户回归 2026-08: 肩褶高 15/20 时
    // 交点不共线).
    for (auto& block : blocks) {
        if (!inScope(block)) continue;  // frozen group
        std::vector<geo::Vec2> prevPos;
        for (const auto& p : block.points)
            if (p.constraint == PointConstraint::Interpolated)
                prevPos.push_back(p.resolvedPos);
        block.resolveInterpolatedPoints(params, conditioned, &ctx);
        // Progress = ANY interpolated point moved (stale resolved=true values
        // must re-enter the fixpoint so a following Step 6 re-aims against the
        // fresh borrow point).
        size_t k = 0;
        for (const auto& p : block.points) {
            if (p.constraint != PointConstraint::Interpolated) continue;
            if (k < prevPos.size() && p.resolved
                && p.resolvedPos.distanceSquaredTo(prevPos[k]) > 1e-9)
                progressed = true;
            ++k;
        }
    }

    // --- Step 6c: other still-unresolved points (no reset). A Polar endpoint
    // whose ref is an interpolated point / intersection resolved above (e.g. a
    // break endpoint Polar-referencing an aux point that references a
    // cross-block intersection) can only converge AFTER 6b — Step 1 resets
    // every point each outer iteration, so this in-pass retry is the only
    // place it gets a chance.
    for (auto& block : blocks) {
        if (!inScope(block)) continue;  // frozen group
        const int unresolvedBefore = block.unresolvedCount();
        if (unresolvedBefore == 0) continue;
        block.resolveUnresolved(params, conditioned, &ctx);
        if (block.unresolvedCount() < unresolvedBefore) progressed = true;
    }

        if (progressed) geoProgressed = true;
        if (!progressed) { converged = true; break; }
    }
        if (budgetExhausted) *budgetExhausted = !converged;
        return geoProgressed;
    };

    {
    GCAD_PERF_SCOPE("r.intersect");
    bool intersectExhausted = false;
    const bool geoProgressed = runIntersectionFixpoint(&intersectExhausted);
    // Step 6d: the cross-block fixpoint may have resolved points that are
    // attachment targets (e.g. a break endpoint Polar-referencing an aux point
    // that references a cross-block intersection). Re-settle the forest so
    // followers track the fresh geometry before Step 7 runs.
    if (intersectExhausted)
        report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
    if (geoProgressed && settleAttachments())
        report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
    }

    // Step 7: endpoint aim constraints (终点指向). A block with endTarget rotates
    // so its segment's end point aims at the target point on another block.
    // Applied LAST so it overrides any attachment-driven rotation.
    //
    // Rotating an aimed block moves any of its points that are not at its local
    // origin. Followers already settled onto those points (Steps 3/5) would be
    // left behind, so after applying the aim rotations the forest is re-settled
    // with the aim-driven rotations preserved — followers re-snap to the moved
    // leader points without fighting the aim. Bounded iteration handles chains
    // where an aimed block is itself a follower (its origin may shift when its
    // own attachment re-snaps, requiring a fresh aim).
    {
    GCAD_PERF_SCOPE("r.aim");
    bool aimConverged = false;
    for (int aimPass = 0; aimPass < kMaxSettleRounds; ++aimPass) {
        bool rotated = false;
        for (auto& block : blocks) {
            if (!inScope(block)) continue;  // frozen group
            if (block.endTargetBlockId.isNull() || block.endTargetPointId.isNull())
                continue;
            if (block.segments.empty()) continue;

            // Locate the target point's world position.
            auto targetIt = blockIndex.find(block.endTargetBlockId);
            if (targetIt == blockIndex.end()) continue;
            const Block& targetBlock = blocks[targetIt.value()];
            const ParamPoint* tp = targetBlock.findPoint(block.endTargetPointId);
            if (!tp || !tp->resolved) continue;
            geo::Vec2 targetWorld = targetBlock.worldPos(block.endTargetPointId);

            // This block's segment start/end (local, resolved).
            const Segment& seg = block.segments.front();
            const ParamPoint* sp = block.findPoint(seg.startPointId);
            const ParamPoint* ep = block.findPoint(seg.endPointId);
            if (!sp || !ep || !sp->resolved || !ep->resolved) continue;

            geo::Vec2 startWorld = block.transform.toWorld(sp->resolvedPos);
            geo::Vec2 aim = targetWorld - startWorld;
            if (aim.lengthSquared() < 1e-12) continue;  // Target coincides with start.
            double aimAngle = std::atan2(aim.y, aim.x);

            // Evaluate angular offset (formula overrides numeric).
            double offsetDeg = block.endTargetOffset;
            if (!block.endTargetOffsetFormula.isEmpty()) {
                auto r = ConditionEngine::evaluate(block.endTargetOffsetFormula, params, conditioned, &ctx);
                if (r.ok) offsetDeg = r.value;
            }
            double offsetRad = offsetDeg * M_PI / 180.0;

            // Local segment direction (start→end).
            geo::Vec2 localDir = ep->resolvedPos - sp->resolvedPos;
            double localAngle = std::atan2(localDir.y, localDir.x);

            // Drive rotation so worldSegDir == aimAngle + offset.
            const double newRotation = aimAngle + offsetRad - localAngle;
            if (std::abs(newRotation - block.transform.rotation) > 1e-9)
                rotated = true;
            block.transform.rotation = newRotation;
        }

        // Re-settle followers of the just-rotated aimed blocks, preserving the
        // aim-driven rotations (preserveEndTargetRotation). Only needed when
        // this pass actually rotated something — if nothing rotated the forest
        // is undisturbed and the settle (the dominant per-frame cost) is
        // skipped. unsettled stays false on skip, so the termination condition
        // below still fires. (A fundamentally non-convergent forest was already
        // reported in Step 3; re-settling it here cannot fix it.)
        bool unsettled = false;
        if (rotated)
            unsettled = settleAttachments(/*preserveEndTargetRotation=*/true);
        if (!rotated && !unsettled) { aimConverged = true; break; }
    }
    // Budget exhausted = aims were still rotating on the last allowed round
    // (typically two blocks aiming at each other). Previously silent; now the
    // caller can surface it (diagnostics badge) instead of shipping a pose that
    // is one rotation short of the fixed point.
    if (!aimConverged)
        report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
    }

    // Step 7b: the aim rotations may have rotated the TARGET segments of
    // cross-block intersections (their local coordinates were solved against
    // the pre-rotation pose in Step 6). Re-run the intersection fixpoint so
    // the points stay on the origin→borrow ray (用户回归: P612 在肩褶高
    // 15/20 时不共线).
    {
        GCAD_PERF_SCOPE("r.intersect.7b");
        bool reExhausted = false;
        const bool reProgressed = runIntersectionFixpoint(&reExhausted);
        if (reExhausted)
            report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
        if (reProgressed && settleAttachments())
            report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
    }

    // Step 8: dart-line constraints (省道线, 用户拍板 2026-08). The block's
    // start point pins to A on another block and its end point E is derived
    // from reference point B:
    //     E = B_world + d · dir(θ_B + β),  θ_B = ref rotation + local exit dir
    // The block is then placed with origin = A_world and rotation = A→E, and
    // its end point's Polar distance is written back as |A−E| — the line's
    // length and direction are computed every pass (线是算出来的). Bounded
    // iteration handles chains where a start/reference block is itself
    // dart-driven (B may be another dart line's computed endpoint). Applied
    // LAST so it overrides any transform the block might have inherited.
    {
        GCAD_PERF_SCOPE("r.dart");
        bool dartMoved = false;
        bool dartConverged = false;
        for (int dartPass = 0; dartPass < kMaxSettleRounds; ++dartPass) {
            bool passMoved = false;
            for (auto& block : blocks) {
                if (!inScope(block)) continue;  // frozen group
                if (!block.isDart()) continue;
                if (block.segments.empty()) continue;

                const auto startIt = blockIndex.find(block.dartStartBlockId);
                const auto refIt   = blockIndex.find(block.dartRefBlockId);
                if (startIt == blockIndex.end() || refIt == blockIndex.end())
                    continue;  // reference vanished (delete-time cleanup clears fields)

                const Block& startBlock = blocks[startIt.value()];
                const Block& refBlock   = blocks[refIt.value()];
                const ParamPoint* aPt = startBlock.findPoint(block.dartStartPointId);
                const ParamPoint* bPt = refBlock.findPoint(block.dartRefPointId);
                if (!aPt || !bPt || !aPt->resolved || !bPt->resolved) continue;

                const geo::Vec2 aWorld = startBlock.worldPos(block.dartStartPointId);
                const geo::Vec2 bWorld = refBlock.worldPos(block.dartRefPointId);

                const double thetaB = refBlock.transform.rotation
                    + refBlock.exitDirectionAtPoint(block.dartRefPointId,
                                                    block.dartRefSegmentId);

                // Offset distance d (formula overrides the numeric value;
                // formula domain is cm, auto-converted to mm).
                double dMm = block.dartOffsetMm;
                ConditionEngine::evaluateLengthMm(block.dartOffsetFormula, params, conditioned, dMm, &ctx);
                // Angle β relative to the reference segment (formula override).
                double betaDeg = block.dartAngleDeg;
                if (!block.dartAngleFormula.isEmpty()) {
                    auto r = ConditionEngine::evaluate(block.dartAngleFormula,
                                                       params, conditioned, &ctx);
                    if (r.ok) betaDeg = r.value;
                }
                const double betaRad = betaDeg * M_PI / 180.0;
                const geo::Vec2 eWorld = bWorld
                    + geo::Vec2(std::cos(thetaB + betaRad),
                                std::sin(thetaB + betaRad)) * dMm;

                const Segment& seg = block.segments.front();
                ParamPoint* sp = block.findPoint(seg.startPointId);
                ParamPoint* ep = block.findPoint(seg.endPointId);
                if (!sp || !ep) continue;

                const geo::Vec2 delta = eWorld - aWorld;
                const double newRotation = std::atan2(delta.y, delta.x);
                const double newLength = delta.length();

                const bool rotChanged =
                    std::abs(newRotation - block.transform.rotation) > 1e-9;
                const bool orgChanged =
                    block.transform.origin.distanceSquaredTo(aWorld) > 1e-12;
                const bool lenChanged = std::abs(ep->distance - newLength) > 1e-9;

                block.transform.origin   = aWorld;
                block.transform.rotation = newRotation;
                // The start point is Free at local (0,0); its resolved cache
                // stays put under a rigid-body transform move.
                sp->resolved = true;
                ep->distance = newLength;
                const geo::Vec2 newEndLocal(newLength, 0.0);
                if (ep->resolvedPos.distanceSquaredTo(newEndLocal) > 1e-9) {
                    block.touchGeometry();
                    ep->resolvedPos = newEndLocal;
                    ep->resolved = true;
                }

                if (rotChanged || orgChanged || lenChanged) {
                    passMoved = true;
                    dartMoved = true;
                }
            }
            if (!passMoved) { dartConverged = true; break; }
        }
        // Budget exhausted = dart endpoints were still moving on the last round
        // (a dart chain that never reaches its fixed point).
        if (!dartConverged)
            report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());

        // Dart-driven endpoints may carry followers (a line snapped to the
        // computed end point E): re-settle the forest with aim-driven
        // rotations preserved so followers track the freshly placed points.
        // Dart blocks themselves own no incoming attachment (start A is a
        // plain reference, not an Attachment), so the settle never fights
        // their computed transforms.
        if (dartMoved && settleAttachments(/*preserveEndTargetRotation=*/true))
            report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
    }
}

bool Resolver::applyAttachment(Block& from, const Attachment& att,
                               const Block& to,
                                const Block* angleRef,
                               const QHash<QString, double>& params,
                               const QHash<QString, QList<Condition>>& conditioned,
                               std::vector<ResolveDiagnostic>* diagnostics,
                               EvalContext* ctx,
                               bool preserveEndTargetRotation)
{
    // The leader's snapped point must exist and be resolved.
    const ParamPoint* toPt = to.findPoint(att.toPointId);
    if (!toPt || !toPt->resolved) {
        report(diagnostics, ResolveDiagnostic::Kind::DanglingPoint, att.id);
        return false;
    }

    // Get the target point's world position on the "to" block.
    geo::Vec2 targetWorldPos = to.worldPos(att.toPointId);

    // Reference direction for the POSITION leader (used by slide rails).
    const double leaderRefWorld = to.transform.rotation
                    + to.exitDirectionAtPoint(att.toPointId, att.toSegmentId);
    double refWorld = leaderRefWorld;
    // 位置锚点与角度基准分离 (用户需求 2026): when angleRefBlockId is set, the
    // followerAngle is measured against the separate reference SEGMENT instead of
    // the position leader's exit direction.
    if (!att.angleRefBlockId.isNull() && angleRef) {
        const Segment* refSeg = angleRef->findSegment(att.angleRefSegmentId);
        if (refSeg) {
            if (!att.angleRefPointId.isNull()) {
                // 使用用户选择的角度基准点的出口方向（与位置连接同构）。
                const ParamPoint* rp = angleRef->findPoint(att.angleRefPointId);
                if (rp && rp->resolved) {
                    refWorld = angleRef->transform.rotation
                             + angleRef->exitDirectionAtPoint(
                                   att.angleRefPointId, att.angleRefSegmentId);
                }
            } else {
                const ParamPoint* rsp = angleRef->findPoint(refSeg->startPointId);
                const ParamPoint* rep = angleRef->findPoint(refSeg->endPointId);
                if (rsp && rep && rsp->resolved && rep->resolved) {
                    // 旧档/未选点：保持历史行为，用 start->end 世界方向。
                    refWorld = angleRef->transform.rotation
                             + angleRef->directionAtPoint(refSeg->startPointId);
                }
            }
        }
    }

    // 基准影子偏转角 (用户拍板 2026-08-27, Attachment::baselineOffsetDeg):
    // 有效基准方向 = 真基准方向 + 影子累计偏转。平时为 0, 行为不变;
    // 批量/整组旋转的会话把"基准在旋转集外"的连接逐帧写成 base+δ,
    // 让被驱朝向跟着组走而真基准不动 (ROTATE_REDESIGN_DESIGN.md §2.6)。
    // 只影响驱动旋转的 refWorld —— 滑轨轨道用的 leaderRefWorld 是位置宿主
    // 的方向, 与影子无关; 本字段位于公式求值链之外, 公式/常量原样存活。
    refWorld += att.baselineOffsetDeg * M_PI / 180.0;

    // The follower's attached point must exist and be resolved (checked before
    // any direction lookup so dangling points short-circuit cleanly).
    const ParamPoint* fromPt = from.findPoint(att.fromPointId);
    if (!fromPt || !fromPt->resolved) {
        report(diagnostics, ResolveDiagnostic::Kind::DanglingPoint, att.id);
        return false;
    }

    // Local direction of the follower's attached segment (the one anchored at
    // fromPointId). The follower's own orientation is its start->end direction.
    // Its world segment direction equals from.transform.rotation + localDir, so
    // to achieve the desired world direction (refWorld + π − angle, 闭合基准
    // 2026-08: 0° = 折叠重叠、180° = 直行延续) we set:
    //     rotation = refWorld + π − angle − localDir
    double localDir = from.directionAtPoint(att.fromPointId);

    // Evaluate follower angle: formula overrides numeric value.
    double angleRad;
    if (att.rotationMode == RotationMode::ArcLength) {
        // Arc-length mode: convert arc length to angle via radius = segment length.
        // The arc starts at the CLOSED position (弧长 0 = 角度 0° = 两线折叠
        // 重叠), sweeping so that πr = 180° = straight continuation (闭合基准,
        // 用户拍板 2026-08 定稿, 与角度模式同基准): 弧长 0 = 0°, 弧长 πr = 180°.
        double arcMm = att.arcLength;
        ConditionEngine::evaluateLengthMm(att.arcLengthFormula, params, conditioned, arcMm, ctx);
        const double radius = from.segmentLengthAtPoint(att.fromPointId);
        angleRad = cad::geo::degToRad(cad::geo::arcMmToDeg(arcMm, radius));
    } else {
        // Angle mode (default).
        double angleDeg = att.followerAngle;
        if (!att.followerAngleFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(att.followerAngleFormula, params, conditioned, ctx);
            if (r.ok) angleDeg = r.value;  // result is in degrees, no conversion
        }
        angleRad = angleDeg * M_PI / 180.0;
    }

    // Closed-base convention (闭合基准, 用户拍板 2026-08 定稿): followerAngle
    // 0° = the follower folds back onto the leader (两线重叠), 90° = vertical,
    // 180° = straight continuation along the leader's exit direction. The
    // world direction is therefore refWorld + π − angleRad (mirror about the
    // perpendicular), NOT refWorld + angleRad. Both rotation modes share it.
    double newRotation = refWorld + M_PI - angleRad - localDir;

    // A block whose rotation is driven by an endpoint-aim constraint (endTarget,
    // applied in Step 7) must not have its rotation overwritten by the attachment
    // during the post-aim re-settle: the aim rotation is authoritative. Only the
    // position constraint is enforced (origin re-snapped about the kept rotation).
    if (preserveEndTargetRotation && !from.endTargetBlockId.isNull())
        newRotation = from.transform.rotation;

    // 位置吸附保持、角度独立 (用户新需求 2026): the point is still pinned to
    // the leader, but the follower keeps its OWN rotation. This is the inverse
    // of angleOnly: position follows, angle does not.
    if (att.angleIndependent)
        newRotation = from.transform.rotation;

    // 拆开保留角度 (angleOnly, 用户拍板 2026-08): the follower keeps following
    // the leader's ANGLE — rotation is still driven by leader direction +
    // followerAngle — but the position constraint is released: the from-point
    // no longer has to land on the leader's point, so the line translates
    // freely while its orientation keeps the relative angle.
    // 2026-xx 两维独立 (用户拍板): angleOnly 与 angleIndependent 不再互斥 ——
    // 双拆开 (angleOnly + angleIndependent) = 位置自由 + 角度自管 = 自由线
    // (rotation 已被上面 angleIndependent 分支保持为自身值, 这里写入同值
    // 并提前 return, 跳过位置钉点)。
    if (att.angleOnly) {
        const bool moved = std::abs(newRotation - from.transform.rotation) > 1e-9;
        from.transform.rotation = newRotation;
        return moved;
    }

    // ── 滑轨模式 (slideMode, 抽屉式滑动, 用户拍板 2026-08) ──
    // 连接姿态保持 (rotation 照旧由基准线方向 + followerAngle 驱动), 位置
    // 只保留一个自由度: 在基准线局部系 (x = 沿基准线延长方向, y = 垂直,
    // 基准线旋转时滑轨跟着转) 下 —— AlongLeader 沿 x 滑动 (y 锁
    // slidePerpMm), PerpLeader 沿 y 拉出 (x 锁 slideAlongMm)。
    //
    // 位置**只从存储坐标 (slideAlongMm/slidePerpMm) 解算**, 不做现场投影:
    // 基准线刚体移动 (平移/旋转) 时滑轨局部坐标不变 → 跟随线随滑轨刚性
    // 携带; 拖动跟随线时由拖拽工具每帧调用
    // ParamDocument::updateSlideOffsetsFromCurrent() 回写**自由轴**坐标,
    // 锁轴坐标保持激活时快照不变。
    if (att.slideMode != SlideMode::None && !att.angleIndependent) {
        // Leader-local rail frame at the anchor point.
        const double railAngle = leaderRefWorld;  // leader's world exit direction
        const geo::Vec2 alongDir(std::cos(railAngle), std::sin(railAngle));
        const geo::Vec2 perpDir(-alongDir.y, alongDir.x);

        // 数值或公式 (cm 域, 2026-12): 公式优先于存储值; 公式无效时回退存储值
        // (与弧长/跟随角公式同约定)。面板输入的 .00 只是数值回显, 变量/表达式
        // 同样可用。
        double alongMm = att.slideAlongMm;
        double perpMm  = att.slidePerpMm;
        ConditionEngine::evaluateLengthMm(att.slideAlongFormula, params, conditioned, alongMm, ctx);
        ConditionEngine::evaluateLengthMm(att.slidePerpFormula, params, conditioned, perpMm, ctx);

        // from-point world position on the rail, pinned from the stored pair.
        const geo::Vec2 localOffset = fromPt->resolvedPos;
        const geo::Vec2 fromPointWorld =
            targetWorldPos + alongDir * alongMm + perpDir * perpMm;

        // origin = from-point world minus the (new-rotation) rotated local offset.
        const double c = std::cos(newRotation);
        const double sn = std::sin(newRotation);
        const geo::Vec2 rotatedOffset{
            localOffset.x * c - localOffset.y * sn,
            localOffset.x * sn + localOffset.y * c
        };
        const geo::Vec2 newOrigin = fromPointWorld - rotatedOffset;

        const bool moved =
            std::abs(newRotation - from.transform.rotation) > 1e-9 ||
            std::abs(newOrigin.x - from.transform.origin.x) > 1e-6 ||
            std::abs(newOrigin.y - from.transform.origin.y) > 1e-6;
        from.transform.rotation = newRotation;
        from.transform.origin = newOrigin;
        return moved;
    }

    // Now position the from-block so that its from-point lands on targetWorldPos.
    // from-point in local coords:
    geo::Vec2 localOffset = fromPt->resolvedPos;

    // Rotate localOffset by the new rotation
    double c = std::cos(newRotation);
    double s = std::sin(newRotation);
    geo::Vec2 rotatedOffset{
        localOffset.x * c - localOffset.y * s,
        localOffset.x * s + localOffset.y * c
    };

    // origin = targetWorldPos - rotatedOffset
    const geo::Vec2 newOrigin = targetWorldPos - rotatedOffset;

    // Only report "moved" when the transform actually changed, so the outer
    // loop can detect convergence of a healthy forest.
    const bool moved =
        std::abs(newRotation - from.transform.rotation) > 1e-9 ||
        std::abs(newOrigin.x - from.transform.origin.x) > 1e-6 ||
        std::abs(newOrigin.y - from.transform.origin.y) > 1e-6;

    from.transform.rotation = newRotation;
    from.transform.origin = newOrigin;
    return moved;
}

// ────────────────────────────────────────────────────────────────────────────
// 滑轨投影快照 (用户拍板 2026-08): 跟随线当前 from-point 的世界位置投影到
// 基准线局部系 (x = 基准线在吸附点的延长方向, y = 垂直)。激活/重定向滑轨
// 模式时锁定轴坐标从此处快照。
// ────────────────────────────────────────────────────────────────────────────
std::pair<double, double> computeSlideOffsets(const Block& from,
                                              const Attachment& att,
                                              const Block& to)
{
    const ParamPoint* toPt = to.findPoint(att.toPointId);
    const ParamPoint* fromPt = from.findPoint(att.fromPointId);
    if (!toPt || !toPt->resolved || !fromPt || !fromPt->resolved)
        return {0.0, 0.0};

    // Leader-local frame at the anchor (same reference as applyAttachment).
    const double railAngle = to.transform.rotation
                           + to.exitDirectionAtPoint(att.toPointId, att.toSegmentId);
    const geo::Vec2 alongDir(std::cos(railAngle), std::sin(railAngle));
    const geo::Vec2 perpDir(-alongDir.y, alongDir.x);

    const geo::Vec2 fromPtWorldCur = from.worldPos(att.fromPointId);
    const geo::Vec2 rel = fromPtWorldCur - to.worldPos(att.toPointId);
    const double s = rel.x * alongDir.x + rel.y * alongDir.y;
    const double t = rel.x * perpDir.x + rel.y * perpDir.y;
    return {s, t};
}

} // namespace cad::param
