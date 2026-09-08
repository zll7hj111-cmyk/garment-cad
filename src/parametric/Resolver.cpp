#include "Resolver.h"

#include <algorithm>
#include <cmath>

#include <QHash>

#include "Block.h"
#include "Attachment.h"
#include "ConditionEngine.h"
#include "parametric/PerfProbe.h"
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
    auto settleAttachments = [&](bool preserveEndTargetRotation = false) -> bool {
        GCAD_PERF_SCOPE("r.settle");
        const int n = static_cast<int>(blocks.size());

        std::vector<int> incoming(n, -1);
        std::vector<std::vector<int>> dependents(n);
        std::vector<int> remaining(n, 0);
        std::vector<char> inScopeArr(n, 0);
        for (int b = 0; b < n; ++b)
            inScopeArr[b] = inScope(blocks[b]) ? 1 : 0;

        int toSettle = 0;
        for (int ai = 0; ai < static_cast<int>(attachments.size()); ++ai) {
            const auto& att = attachments[ai];
            if (att.isPin) continue;
            if (!att.fromComponentId.isNull()) continue;
            auto fromIt = blockIndex.find(att.fromBlockId);
            if (fromIt == blockIndex.end()) {
                report(diagnostics, ResolveDiagnostic::Kind::DanglingBlock, att.id);
                continue;
            }
            const int fi = fromIt.value();
            if (!inScopeArr[fi]) continue;
            incoming[fi] = ai;
            ++toSettle;

            auto addDep = [&](const QUuid& depBlockId) {
                if (depBlockId.isNull()) return;
                auto depIt = blockIndex.find(depBlockId);
                if (depIt == blockIndex.end()) return;
                const int di = depIt.value();
                if (di == fi || !inScopeArr[di]) return;
                dependents[di].push_back(fi);
                ++remaining[fi];
            };
            addDep(att.toBlockId);
            addDep(att.angleRefBlockId);
            addDep(att.angleRef2BlockId);
        }

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
                const Block* angleRef2 = nullptr;
                if (!att.angleRef2BlockId.isNull()) {
                    auto ref2It = blockIndex.find(att.angleRef2BlockId);
                    if (ref2It != blockIndex.end())
                        angleRef2 = &blocks[ref2It.value()];
                }
                if (toIt == blockIndex.end()) {
                    report(diagnostics, ResolveDiagnostic::Kind::DanglingBlock, att.id);
                } else {
                    applyAttachment(blocks[b], att, blocks[toIt.value()], angleRef,
                                    angleRef2,
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

        return settled < toSettle;
    };

    // Step 3: settle the attachment forest.
    if (settleAttachments())
        report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());

    // Step 4: bridges resolve LAST.
    struct Pin { QUuid fromPointId; geo::Vec2 hostWorld; };
    QHash<QUuid, std::vector<Pin>> pinsByBridge;
    for (const auto& att : attachments) {
        if (!att.isPin) continue;
        const auto reportIfBridge = [&]() {
            auto fromIt = blockIndex.find(att.fromBlockId);
            return fromIt != blockIndex.end()
                && blocks[fromIt.value()].isBridge
                && inScope(blocks[fromIt.value()]);
        };
        auto toIt = blockIndex.find(att.toBlockId);
        if (toIt == blockIndex.end()) {
            if (reportIfBridge())
                report(diagnostics, ResolveDiagnostic::Kind::DanglingBlock, att.id);
            continue;
        }
        const Block& host = blocks[toIt.value()];
        const ParamPoint* hp = host.findPoint(att.toPointId);
        if (!hp || !hp->resolved) {
            if (reportIfBridge())
                report(diagnostics, ResolveDiagnostic::Kind::DanglingPoint, att.id);
            continue;
        }
        pinsByBridge[att.fromBlockId].push_back(
            {att.fromPointId, host.worldPos(att.toPointId)});
    }

    bool bridgesMoved = false;
    for (auto& bridge : blocks) {
        if (!bridge.isBridge) continue;
        if (!inScope(bridge)) continue;

        const auto pinsIt = pinsByBridge.constFind(bridge.id);
        if (pinsIt == pinsByBridge.constEnd()) continue;
        const std::vector<Pin>& pins = pinsIt.value();
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

        bridge.resolveInterpolatedPoints(params, conditioned, &ctx);
    }

    // Step 5: attachments led BY a bridge.
    if (bridgesMoved && settleAttachments())
        report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());

    // Step 6/6b/6c: cross-block intersection points fixpoint.
    auto runIntersectionFixpoint = [&](bool* budgetExhausted = nullptr) -> bool {
        bool geoProgressed = false;
        bool converged = false;
        for (int pass = 0; pass < kMaxSettleRounds; ++pass) {
            bool progressed = false;

            // --- Step 6: cross-block intersections ---
            for (auto& block : blocks) {
                if (!inScope(block)) continue;
                for (auto& pt : block.points) {
                    if (pt.constraint != PointConstraint::Intersection) continue;
                    if (resolveCrossBlockIntersection(blocks, block, pt, params,
                                                      conditioned, ctx, pass, scope))
                        progressed = true;
                }
            }

            // --- Step 6b: interpolated points ---
            for (auto& block : blocks) {
                if (!inScope(block)) continue;
                std::vector<geo::Vec2> prevPos;
                for (const auto& p : block.points)
                    if (p.constraint == PointConstraint::Interpolated)
                        prevPos.push_back(p.resolvedPos);
                block.resolveInterpolatedPoints(params, conditioned, &ctx);
                size_t k = 0;
                for (const auto& p : block.points) {
                    if (p.constraint != PointConstraint::Interpolated) continue;
                    if (k < prevPos.size() && p.resolved
                        && p.resolvedPos.distanceSquaredTo(prevPos[k]) > 1e-9)
                        progressed = true;
                    ++k;
                }
            }

            // --- Step 6c: other still-unresolved points ---
            for (auto& block : blocks) {
                if (!inScope(block)) continue;
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
        if (intersectExhausted)
            report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
        if (geoProgressed && settleAttachments())
            report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
    }

    // Step 7: endpoint aim constraints (终点指向).
    {
        GCAD_PERF_SCOPE("r.aim");
        bool aimConverged = false;
        for (int aimPass = 0; aimPass < kMaxSettleRounds; ++aimPass) {
            bool rotated = false;
            for (auto& block : blocks) {
                if (!inScope(block)) continue;
                if (block.endTargetBlockId.isNull() || block.endTargetPointId.isNull())
                    continue;
                if (block.segments.empty()) continue;

                auto targetIt = blockIndex.find(block.endTargetBlockId);
                if (targetIt == blockIndex.end()) continue;
                const Block& targetBlock = blocks[targetIt.value()];
                const ParamPoint* tp = targetBlock.findPoint(block.endTargetPointId);
                if (!tp || !tp->resolved) continue;
                geo::Vec2 targetWorld = targetBlock.worldPos(block.endTargetPointId);

                const Segment& seg = block.segments.front();
                const ParamPoint* sp = block.findPoint(seg.startPointId);
                const ParamPoint* ep = block.findPoint(seg.endPointId);
                if (!sp || !ep || !sp->resolved || !ep->resolved) continue;

                geo::Vec2 startWorld = block.transform.toWorld(sp->resolvedPos);
                geo::Vec2 aim = targetWorld - startWorld;
                if (aim.lengthSquared() < 1e-12) continue;
                double aimAngle = std::atan2(aim.y, aim.x);

                double offsetDeg = block.endTargetOffset;
                if (!block.endTargetOffsetFormula.isEmpty()) {
                    auto r = ConditionEngine::evaluate(block.endTargetOffsetFormula, params, conditioned, &ctx);
                    if (r.ok) offsetDeg = r.value;
                }
                double offsetRad = offsetDeg * M_PI / 180.0;

                geo::Vec2 localDir = ep->resolvedPos - sp->resolvedPos;
                double localAngle = std::atan2(localDir.y, localDir.x);

                const double newRotation = aimAngle + offsetRad - localAngle;
                if (std::abs(newRotation - block.transform.rotation) > 1e-9)
                    rotated = true;
                block.transform.rotation = newRotation;
            }

            bool unsettled = false;
            if (rotated)
                unsettled = settleAttachments(/*preserveEndTargetRotation=*/true);
            if (!rotated && !unsettled) { aimConverged = true; break; }
        }
        if (!aimConverged)
            report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
    }

    // Step 7b: re-run the intersection fixpoint after aim rotations.
    {
        GCAD_PERF_SCOPE("r.intersect.7b");
        bool reExhausted = false;
        const bool reProgressed = runIntersectionFixpoint(&reExhausted);
        if (reExhausted)
            report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
        if (reProgressed && settleAttachments())
            report(diagnostics, ResolveDiagnostic::Kind::NotConverged, QUuid());
    }
}

} // namespace cad::param
