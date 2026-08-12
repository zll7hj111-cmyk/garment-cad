#include "Resolver.h"

#include <algorithm>
#include <cmath>

#include <QHash>

#include "Block.h"
#include "Attachment.h"
#include "ConditionEngine.h"
#include "parametric/PerfProbe.h"
#include "geometry/Units.h"

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

void Resolver::resolveAll(std::vector<Block>& blocks,
                          const std::vector<Attachment>& attachments,
                          const QHash<QString, double>& params,
                          const QHash<QString, QList<Condition>>& conditioned,
                          std::vector<ResolveDiagnostic>* diagnostics,
                          Scope scope,
                          const QUuid& auxLayerId,
                          const QSet<QUuid>* affectedOnly)
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

        // incoming[b] = index of the unique non-pin attachment whose from-block
        // is b (-1 if none); leader[b] = block index b follows (-1 if none/absent).
        std::vector<int> incoming(n, -1);
        std::vector<int> leader(n, -1);
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
            if (!inScope(blocks[fi])) continue;            // frozen group
            incoming[fi] = ai;
            auto toIt = blockIndex.find(att.toBlockId);
            leader[fi] = (toIt != blockIndex.end()) ? toIt.value() : -1;
        }

        // Child adjacency as flat linked lists (firstChild/nextSibling) to avoid
        // per-block heap allocations on this per-frame path.
        std::vector<int> firstChild(n, -1);
        std::vector<int> nextSibling(n, -1);
        int toSettle = 0;  // # in-scope blocks that own an incoming attachment
        for (int b = 0; b < n; ++b) {
            if (incoming[b] < 0) continue;
            ++toSettle;
            const int l = leader[b];
            if (l >= 0 && inScope(blocks[l])) {  // b is a child of an in-scope leader
                nextSibling[b] = firstChild[l];
                firstChild[l] = b;
            }
        }

        // Seed the queue with roots: in-scope blocks with no in-scope leader
        // (unconstrained blocks, and followers of a static/absent leader). A root
        // with no incoming attachment still must be visited to release its children.
        std::vector<int> queue;
        queue.reserve(n);
        for (int b = 0; b < n; ++b) {
            if (!inScope(blocks[b])) continue;
            const int l = leader[b];
            if (!(l >= 0 && inScope(blocks[l])))
                queue.push_back(b);
        }

        // BFS: settle a block (its leader is already final), then release followers.
        int settled = 0;
        for (size_t head = 0; head < queue.size(); ++head) {
            const int b = queue[head];
            if (incoming[b] >= 0) {
                const auto& att = attachments[incoming[b]];
                auto toIt = blockIndex.find(att.toBlockId);
                if (toIt == blockIndex.end()) {
                    report(diagnostics, ResolveDiagnostic::Kind::DanglingBlock, att.id);
                } else {
                    applyAttachment(blocks[b], att, blocks[toIt.value()],
                                    params, conditioned, diagnostics, &ctx,
                                    preserveEndTargetRotation);
                }
                ++settled;
            }
            for (int c = firstChild[b]; c != -1; c = nextSibling[c])
                queue.push_back(c);
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
                ++bridge.geometryEpoch;  // canvas must rebuild the cache
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
    if (bridgesMoved && settleAttachments())
        report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());

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
    auto runIntersectionFixpoint = [&]() -> bool {
        bool geoProgressed = false;
        for (int pass = 0; pass < 4; ++pass) {
            bool progressed = false;

    // --- Step 6: cross-block intersections ---
    for (auto& block : blocks) {
        if (!inScope(block)) continue;  // frozen group
        for (auto& pt : block.points) {
            if (pt.constraint != PointConstraint::Intersection) continue;
            // Skip if BOTH the origin and (when set) the aim point live in the
            // same block (already resolved in Step 1). An origin or aim point
            // outside this block is resolved here in world space.
            if (block.findPoint(pt.refPointA)
                && (pt.interAimPointId.isNull() || block.findPoint(pt.interAimPointId)))
                continue;

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
            if (!found) continue;

            // Target segment endpoints (world). The END point may be
            // mid-cycle in the outer fixpoint (e.g. a break endpoint whose
            // position depends on this intersection): its cached position is
            // used now and later iterations converge once the endpoint
            // resolves. The START point must be resolved — it anchors the
            // segment geometry.
            const Segment* seg = block.findSegment(pt.hostSegmentId);
            if (!seg) continue;
            const ParamPoint* sp = block.findPoint(seg->startPointId);
            const ParamPoint* ep = block.findPoint(seg->endPointId);
            if (!sp || !ep || !sp->resolved) continue;

            geo::Vec2 w1 = block.transform.toWorld(sp->resolvedPos);
            geo::Vec2 w2 = block.transform.toWorld(ep->resolvedPos);
            geo::Vec2 segDir = w2 - w1;
            double segLen = segDir.length();
            if (segLen < 1e-9) continue;

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
                if (!aimFound) continue;
                geo::Vec2 toAim = aimWorld - originWorld;
                if (toAim.lengthSquared() < 1e-12) continue;  // Coincident with origin.
                theta = std::atan2(toAim.y, toAim.x);
            } else {
                double baseAngle = std::atan2(segDir.y, segDir.x);

                // Evaluate angle (formula).
                double angleDeg = pt.interAngle;
                if (!pt.interAngleFormula.isEmpty()) {
                    auto r = ConditionEngine::evaluate(pt.interAngleFormula, params, conditioned, &ctx);
                    if (r.ok) angleDeg = r.value;
                }
                theta = baseAngle + angleDeg * M_PI / 180.0;
            }
            geo::Vec2 d{std::cos(theta), std::sin(theta)};

            double denom = d.cross(segDir);
            if (std::abs(denom) < 1e-9) continue;  // Parallel.

            geo::Vec2 w = w1 - originWorld;
            double s = w.cross(segDir) / denom;
            double t = w.cross(d) / denom;

            constexpr double eps = 1e-6;
            bool validT = (t >= -eps && t <= 1.0 + eps);
            bool validS = pt.interBidirectional ? true : (s >= -eps);
            if (!validT || !validS) continue;

            geo::Vec2 hitWorld = originWorld + d * s;
            const geo::Vec2 newLocal = block.transform.toLocal(hitWorld);
            if (!pt.resolved || pt.resolvedPos.distanceSquaredTo(newLocal) > 1e-6)
                ++block.geometryEpoch;  // canvas must rebuild the cache
            if (!pt.resolved) progressed = true;
            pt.resolvedPos = newLocal;
            pt.resolved = true;
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
        if (!progressed) break;
    }
        return geoProgressed;
    };

    {
    GCAD_PERF_SCOPE("r.intersect");
    const bool geoProgressed = runIntersectionFixpoint();
    // Step 6d: the cross-block fixpoint may have resolved points that are
    // attachment targets (e.g. a break endpoint Polar-referencing an aux point
    // that references a cross-block intersection). Re-settle the forest so
    // followers track the fresh geometry before Step 7 runs.
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
    for (int aimPass = 0; aimPass < 4; ++aimPass) {
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
        if (!rotated && !unsettled) break;
    }
    }

    // Step 7b: the aim rotations may have rotated the TARGET segments of
    // cross-block intersections (their local coordinates were solved against
    // the pre-rotation pose in Step 6). Re-run the intersection fixpoint so
    // the points stay on the origin→borrow ray (用户回归: P612 在肩褶高
    // 15/20 时不共线).
    {
        GCAD_PERF_SCOPE("r.intersect.7b");
        if (runIntersectionFixpoint() && settleAttachments())
            report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
    }
}

bool Resolver::applyAttachment(Block& from, const Attachment& att,
                               const Block& to,
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

    // Reference direction: the leader's world "exit" direction at the snapped
    // point — the direction that continues the leader straight past that point
    // (see Block::exitDirectionAtPoint). Using the exit direction (rather than
    // the raw start->end direction) makes followerAngle == 0 mean "continue
    // straight along the leader" regardless of which leader endpoint is snapped.
    // toSegmentId (when set) pins the reference to the explicitly chosen leader
    // segment — points shared by several segments stay unambiguous.
    double refWorld = to.transform.rotation
                    + to.exitDirectionAtPoint(att.toPointId, att.toSegmentId);

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
        if (!att.arcLengthFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(att.arcLengthFormula, params, conditioned, ctx);
            if (r.ok) arcMm = geo::Units::cmToMm(r.value);
        }
        const double radius = from.segmentLengthAtPoint(att.fromPointId);
        angleRad = (radius > 1e-9) ? (arcMm / radius) : 0.0;
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

} // namespace cad::param
