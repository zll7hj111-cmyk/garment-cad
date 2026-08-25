#include "ParamDocument.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <QDebug>

#include "parametric/Resolver.h"
#include "parametric/Serial.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ExpressionEvaluator.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "parametric/FollowerAngle.h"
#include "parametric/PerfProbe.h"
#include "parametric/IntersectDebug.h"
#include "parametric/LayerRegistry.h"
#include "parametric/VariableStore.h"
#include "parametric/MeasurementStore.h"

namespace cad::param {

// 求解装配：环检测 / 引用追踪 / 受影响集 / resolveAll* 管线 (2026-08 拆分)

bool ParamDocument::wouldCreateMeasureValueCycle(const Attachment& candidate) const
{
    // Conservative static check, invoked ONLY for cross-layer candidates
    // (same-layer topologies keep their long-standing behaviour untouched).
    //
    // 已知缺口 (formula side): this intercepts EDGE creation only. Editing an
    // existing topology's length/angle formula to reference a measurement
    // whose source already hangs beneath the consumer creates the same value
    // cycle — that guard belongs to the formula-commit entry (VariableCommands
    // layer) and is intentionally not handled here.

    // 1) Subtree beneath the candidate FOLLOWER over the EXISTING edges
    //    (pins included — a bridge rides on its hosts positionally).
    ensureFollowersIndex();
    QSet<QUuid> subTree{candidate.fromBlockId};
    QList<QUuid> queue{candidate.fromBlockId};
    while (!queue.isEmpty()) {
        const QUuid cur = queue.takeFirst();
        const auto fit = m_followersOf.constFind(cur);
        if (fit == m_followersOf.constEnd()) continue;
        for (const QUuid& f : fit.value())
            if (!subTree.contains(f)) {
                subTree.insert(f);
                queue.push_back(f);
            }
    }

    // 2) Ancestor set of the candidate LEADER. Walks EVERY attachment edge
    //    — pins included: a bridge's pose is driven by its pin hosts, so a
    //    bridge leader must pull its hosts into the ancestor set (otherwise
    //    the guard misses cycles whose path runs through a bridge).
    //    Multi-source BFS: a block can own several leader edges (a bridge
    //    has two pins), so a single-chain walk is insufficient.
    QSet<QUuid> ancestors{candidate.toBlockId};
    {
        QList<QUuid> queue{candidate.toBlockId};
        while (!queue.isEmpty()) {
            const QUuid cur = queue.takeFirst();
            for (const auto& a : m_attachments) {
                if (a.fromBlockId != cur) continue;
                if (!ancestors.contains(a.toBlockId)) {
                    ancestors.insert(a.toBlockId);
                    queue.push_back(a.toBlockId);
                }
            }
        }
    }

    // 3) Consumer test: does @p blockId's pose depend on the measurement
    //    @p refName? ownerBlock (measure line) plus every formula that can
    //    drive the block's geometry.
    const auto formulaRefs = [](const QString& formula, const QString& refName) {
        if (formula.isEmpty()) return false;
        const QStringList names = ExpressionEvaluator::referencedNames(formula);
        for (const QString& n : names)
            if (n.compare(refName, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };
    const auto consumedBy = [&](const QString& refName, const QUuid& ownerBlockId,
                                const QUuid& blockId) {
        if (!ownerBlockId.isNull() && blockId == ownerBlockId)
            return true;
        const Block* b = blockById(blockId);
        if (!b) return false;
        for (const auto& s : b->segments)
            if (formulaRefs(s.lengthFormula, refName) ||
                formulaRefs(s.extendStartFormula, refName) ||
                formulaRefs(s.extendEndFormula, refName)) return true;
        for (const auto& p : b->points)
            if (formulaRefs(p.distanceFormula, refName) ||
                formulaRefs(p.angleFormula, refName))
                return true;
        if (formulaRefs(b->endTargetOffsetFormula, refName)) return true;
        for (const auto& a : m_attachments)
            if (a.fromBlockId == blockId &&
                (formulaRefs(a.followerAngleFormula, refName) ||
                 formulaRefs(a.arcLengthFormula, refName)))
                return true;
        return false;
    };

    // 4) A cycle forms iff a measurement's SOURCE sits in the new follower
    //    subtree while one of its CONSUMERS is on the leader's ancestor
    //    chain (the only relation the new edge creates).
    const auto checkVar = [&](const QString& refName, const QUuid& ownerBlockId,
                              std::initializer_list<QUuid> sources) {
        if (refName.isEmpty()) return false;
        for (const QUuid& src : sources) {
            if (src.isNull() || !subTree.contains(src)) continue;
            for (const QUuid& anc : ancestors)
                if (consumedBy(refName, ownerBlockId, anc))
                    return true;
        }
        return false;
    };
    for (const auto& mv : m_measureStore->measureVars())
        if (checkVar(mv.refName, mv.ownerBlockId, {mv.blockA, mv.blockB}))
            return true;
    for (const auto& lv : m_measureStore->linkedVars())
        if (checkVar(lv.refName, QUuid(), {lv.sourceBlockId}))
            return true;
    for (const auto& am : m_measureStore->angleMeasures())
        if (checkVar(am.refName, QUuid(), {am.blockA, am.blockB}))
            return true;
    return false;
}

//---------------------------------------------------------------------------------------------------------------------
bool ParamDocument::blockReferences(const Block& b, const QUuid& targetBlockId) const
{
    // NOTE: a NEW point constraint whose position depends on another block's
    // point/segment must be registered HERE, or drag-follow (阶段2 dirty
    // propagation) will silently MISS its cross-block dependency and the
    // follower will not move. See the registry in ParamPoint.h.
    // Endpoint-aim target (终点指向).
    if (b.endTargetBlockId == targetBlockId)
        return true;
    // Dart-line references (省道线): the start pin A and the offset point B
    // must both re-solve (the dart block re-computes its transform) whenever
    // their host blocks move.
    if (b.dartStartBlockId == targetBlockId || b.dartRefBlockId == targetBlockId)
        return true;
    // Curve-anchor follow target (曲线点跟随).
    for (const auto& pt : b.points)
        if (pt.followBlockId == targetBlockId)
            return true;
    // Point-id references (Polar / Midpoint / OnSegment / Intersection ray
    // origin + aim point / Interpolated measurement origin) that land in the
    // target block.
    const Block* target = blockById(targetBlockId);
    if (!target) return false;
    for (const auto& pt : b.points) {
        const QUuid refs[] = {pt.refPointId, pt.refPointA, pt.refPointB,
                              pt.interpRefPointId, pt.interAimPointId};
        for (const QUuid& ref : refs)
            if (!ref.isNull() && target->findPoint(ref))
                return true;
    }
    return false;
}

//---------------------------------------------------------------------------------------------------------------------
QSet<QUuid> ParamDocument::collectAffected(const QList<QUuid>& seeds) const
{
    ensureFollowersIndex();
    // Rigid components: seeding any member must move the WHOLE group (and the
    // followers hanging off any of its members) — expand the seed to the full
    // component closure before the BFS.
    QSet<QUuid> seedSet;
    for (const QUuid& s : seeds)
        if (!s.isNull()) seedSet.insert(s);
    const QSet<QUuid> expandedSeeds = componentClosure(seedSet);

    QSet<QUuid> affected;
    QList<QUuid> queue;
    for (const QUuid& s : expandedSeeds)
        if (!affected.contains(s)) {
            affected.insert(s);
            queue.push_back(s);
        }

    while (!queue.isEmpty()) {
        const QUuid cur = queue.takeFirst();

        // 1) Attachment subtree: everything that follows the current block
        //    (regular attachments AND bridge pins — bridges depend on hosts).
        const auto fit = m_followersOf.constFind(cur);
        if (fit != m_followersOf.constEnd()) {
            for (const QUuid& f : fit.value())
                if (!affected.contains(f)) {
                    affected.insert(f);
                    queue.push_back(f);
                }
        }

        // 2) Cross-block referencers: blocks holding an aim / point reference
        //    into the current block must re-solve when it moves — via the lazy
        //    reference index (2026-09: 旧实现每出队节点全表扫描 O(N²)/帧).
        //    References are at most one hop; deeper chains re-enter via queue.
        ensureReferencesIndex();
        const auto rit = m_referenceIndex.constFind(cur);
        if (rit != m_referenceIndex.constEnd()) {
            for (auto f = rit.value().cbegin(); f != rit.value().cend(); ++f) {
                const QUuid fid = f.key();
                if (!affected.contains(fid)) {
                    affected.insert(fid);
                    queue.push_back(fid);
                }
            }
        }
    }
    return affected;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Resolve
// ═══════════════════════════════════════════════════════════════════════════════

void ParamDocument::resolveAll()
{
    m_referenceIndexDirty = true;  // 所有引用字段变更都经全量 resolve 落地
    resolveAllInternal(true);
}

//---------------------------------------------------------------------------------------------------------------------
void ParamDocument::resolveForDrag(const QList<QUuid>& affectedBlockIds,
                                   const QList<QUuid>& ignoredAttachments)
{
    // Cross-layer attachments present: dragging a working-layer block must
    // move its aux followers within the SAME frame — escalate to dual-group
    // invalidation so Phase 1/2/3 all run and the cross-layer settle happens
    // inside this drag frame. Documents WITHOUT cross-layer attachments keep
    // the exact pre-existing narrowed behaviour (zero overhead).
    if (m_crossLayerCount > 0)
        m_layerRegistry->invalidateAllLayers();

    // Dirty-subgraph mode: seeds are non-empty → narrow the pass to the
    // affected subgraph. Empty seeds = plain full resolve (minus panels).
    // NOTE: collectAffected() BFS rides the full edge table — cross-layer
    // edges included — so a working seed automatically pulls its aux
    // followers into the affected set (Phase 3 moves only that subset).
    const QSet<QUuid> affected = affectedBlockIds.isEmpty()
        ? QSet<QUuid>()
        : collectAffected(affectedBlockIds);
    const QSet<QUuid>* affectedPtr = affectedBlockIds.isEmpty() ? nullptr : &affected;

    // Keep the pass-local ignored list alive for the duration of the call.
    QList<QUuid> ignored = ignoredAttachments;
    resolveAllInternal(false, affectedPtr,
                       ignored.isEmpty() ? nullptr : &ignored);
}

//---------------------------------------------------------------------------------------------------------------------
void ParamDocument::resolveAllInternal(bool emitDocChanged,
                                       const QSet<QUuid>* affectedOnly,
                                       const QList<QUuid>* ignoredAttachments)
{
    GCAD_PERF_SCOPE("resolve");
    // Conservative fallback: callers that did not narrow the scope via
    // invalidateLayer()/invalidateAllLayers() re-resolve everything.
    if (!m_layerRegistry->dirtyAnnotated())
        m_layerRegistry->invalidateAllLayers();
    m_layerRegistry->clearDirtyAnnotation();

    // Invariant: the aux layer id comes from the registry (element 0).
    const QUuid kAuxLayer = m_layerRegistry->auxLayerId();
    if (idbg::enabled())
        idbg::log(QStringLiteral("[resolve] full=%1 affected=%2 auxRefsWorking=%3 workRefsAux=%4 crossLayer=%5")
                      .arg(emitDocChanged ? 1 : 0).arg(affectedOnly ? affectedOnly->size() : -1)
                      .arg(m_auxIntersectToWorking ? 1 : 0)
                      .arg(m_workingIntersectToAux ? 1 : 0)
                      .arg(m_crossLayerCount));

    // Attachments excluded from this pass (drag-time cross-selection links
    // pending removal). They stay in the document; the pass simply skips them.
    std::vector<Attachment> filteredAttachments;
    const std::vector<Attachment>* passAttachments = &m_attachments;
    if (ignoredAttachments && !ignoredAttachments->isEmpty()) {
        filteredAttachments.reserve(m_attachments.size());
        for (const auto& a : m_attachments)
            if (!ignoredAttachments->contains(a.id))
                filteredAttachments.push_back(a);
        passAttachments = &filteredAttachments;
    }

    // Dirty-subgraph narrowing: starts as the caller-provided subset; upgraded
    // to null (full resolve) the moment a measurement changes, because a
    // measured value can feed formulas in blocks OUTSIDE the subset.
    const QSet<QUuid>* effAffected = affectedOnly;

    // ── Phase 1: auxiliary calculation layer (only when dirty) ──
    // Aux geometry is a pure function of the variables; during working-layer
    // manipulation it stays frozen and its cached transforms remain valid.
    bool auxRan = false, workingRan = false;
    if (m_layerRegistry->auxDirty()) {
        GCAD_PERF_SCOPE("resolve.aux");
        std::vector<ResolveDiagnostic> auxDiag;  // discarded (phase 2 owns m_diagnostics)
        measureLinkedVars();   // feed aux formulas before resolving (old semantics)
        measureMeasureVars();
        measureAngleMeasureVars();
        Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                             &auxDiag, Resolver::Scope::AuxOnly, kAuxLayer,
                             effAffected);
        for (int i = 0; i < 4 && (measureLinkedVars() || measureMeasureVars()
                                  || measureAngleMeasureVars()); ++i) {
            if (effAffected) effAffected = nullptr;  // measurement changed → full
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters,
                                 m_conditioned, &auxDiag,
                                 Resolver::Scope::AuxOnly, kAuxLayer,
                                 effAffected);
        }
        m_layerRegistry->setAuxDirty(false);
        auxRan = true;
        // Published measurement values sourced from the aux layer may have
        // changed → working layers must re-measure and re-resolve.
        m_layerRegistry->setWorkingDirty(true);
    }

    // ── Phase 2: working layers ──
    // Extracted into a closure so the Phase 3 cross-layer fixpoint can
    // re-run it when settling aux followers perturbs published measurements.
    auto runWorkingPhase = [&]() {
        if (!m_layerRegistry->workingDirty()) return;
        GCAD_PERF_SCOPE("resolve.work");
        // Measurements sourced entirely from the (now clean) aux layer keep
        // their cached values — only working-geometry measurements re-run.
        // (Cross-layer-linked aux blocks are exempt from the cache — see
        // collectMobileAuxBlocks: their geometry tracks the working layers.)
        measureLinkedVars(/*skipAuxSource=*/true);
        measureMeasureVars(/*skipAuxSource=*/true);
        measureAngleMeasureVars(/*skipAuxSource=*/true);
        Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                             &m_diagnostics, Resolver::Scope::WorkingOnly, kAuxLayer,
                             effAffected);
        // Linked measurements are taken BEFORE the pass; if the pass moved any
        // measured geometry (e.g. the source segment of a length-linked copy was
        // just edited), propagate to consumers until stable (bounded: linked
        // chains are shallow — copies reference originals directly).
        for (int i = 0; i < 4 &&
             (measureLinkedVars(/*skipAuxSource=*/true) ||
              measureMeasureVars(/*skipAuxSource=*/true) ||
              measureAngleMeasureVars(/*skipAuxSource=*/true)); ++i) {
            if (effAffected) effAffected = nullptr;  // measurement changed → full
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters,
                                 m_conditioned, &m_diagnostics,
                                 Resolver::Scope::WorkingOnly, kAuxLayer,
                                 effAffected);
        }
        m_layerRegistry->setWorkingDirty(false);
        workingRan = true;
    };
    runWorkingPhase();

    // ── Phase 2.5: cross-layer intersection re-solve (跨层交点重解) ──
    // An aux-layer intersection whose ray origin / borrow point lives on a
    // WORKING layer was solved in Phase 1 against the STALE working position
    // (the working layers only move in Phase 2). Without a re-solve the
    // intersection drifts off the origin→borrow ray (用户回归: P612 在
    // 肩褶高 15/20 时不共线). Detect the dependency (cheap: aux intersections
    // only) and re-run the aux pass with the fresh working values; changed
    // aux measurements re-trigger the working pass, bounded like Phase 3.
    if (auxRan && workingRan) {
        // Structural property of the doc — recompute only on FULL resolves
        // (every structural mutation funnels through resolveAll()); narrowed
        // drag frames reuse the cached value instead of re-scanning
        // O(work×aux) blocks × points per frame (2026-10 性能).
        if (!affectedOnly) {
            m_auxIntersectToWorking = scanAuxIntersectionCrossRefs();
            m_workingIntersectToAux = scanWorkingIntersectionAuxRefs();
        }
        const bool auxRefsWorking = m_auxIntersectToWorking;
        if (auxRefsWorking) {
            for (int round = 0; round < 4; ++round) {
                std::vector<ResolveDiagnostic> auxDiag2;  // discarded
                Resolver::resolveAll(m_blocks, *passAttachments, m_parameters,
                                     m_conditioned, &auxDiag2,
                                     Resolver::Scope::AuxOnly, kAuxLayer,
                                     nullptr);
                if (!(measureLinkedVars() || measureMeasureVars()
                      || measureAngleMeasureVars()))
                    break;
                m_layerRegistry->setWorkingDirty(true);
                runWorkingPhase();
            }
        }
    }

    // ── Phase 3: cross-layer settle (跨层沉降) ──
    // Only when cross-layer attachments EXIST (counter-maintained; documents
    // without them pay exactly one integer test). Aux followers are settled
    // onto the now-final working leaders via an AuxOnly pass (out-of-scope
    // working leaders act as static roots). Settling can move measurement
    // SOURCES (aux geometry) → re-measure WITHOUT the aux cache; a changed
    // value feeds working-layer formulas → re-run Phase 2 + Phase 3, bounded
    // by the same ≤4-round fixpoint budget. Non-convergence reports
    // ResolveDiagnostic::NotConverged.
    bool xLayerMoved = false;
    if (m_crossLayerCount > 0 && workingRan) {
        bool settled = false;
        for (int round = 0; round < 4; ++round) {
            xLayerMoved = true;
            GCAD_PERF_SCOPE("resolve.xlayer");
            std::vector<ResolveDiagnostic> xDiag;  // discarded (phase 2 owns m_diagnostics)
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                                 &xDiag, Resolver::Scope::AuxOnly, kAuxLayer,
                                 effAffected);
            // No skipAuxSource here: cross-layer-linked aux geometry may have
            // moved, and its published values must be re-measured fully.
            if (!(measureLinkedVars() || measureMeasureVars()
                  || measureAngleMeasureVars())) {
                settled = true;
                break;
            }
            // Measurement changed → working layers must re-solve, then the
            // aux followers re-settle (next loop round).
            if (effAffected) effAffected = nullptr;  // measurement changed → full
            m_layerRegistry->setWorkingDirty(true);
            runWorkingPhase();
        }
        if (!settled) {
            // Budget exhausted: the last round re-solved the working layers,
            // but the aux followers never got their FINAL settle against the
            // fresh leader poses — geometry would otherwise lag one round
            // behind. One pure AuxOnly settle (no re-measurement, no
            // working re-solve), then report the non-convergence.
            GCAD_PERF_SCOPE("resolve.xlayer.final");
            std::vector<ResolveDiagnostic> finalDiag;  // discarded (NotConverged reported below)
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                                 &finalDiag, Resolver::Scope::AuxOnly, kAuxLayer,
                                 effAffected);
            m_diagnostics.push_back({ResolveDiagnostic::Kind::NotConverged, QUuid()});
        }
    }

    // ── Phase 4: working-side cross-layer intersection re-solve (工作侧跨层交点重解) ──
    // A WORKING-layer intersection whose ray origin / aim point lives on the aux
    // layer was solved in Phase 2 against the aux pose of that moment; Phase 3's
    // aux settle may move the origin afterwards (aux followers track working
    // geometry), leaving the intersection drifted off the origin→borrow ray
    // (用户回归 2026-08: 辅助层射线起点 + 工作层交点, 变量修改后偏离 1.3mm).
    // Re-run the working pass against the fresh aux pose; if the re-solve moved
    // measured geometry, the aux must re-settle too — bounded like Phase 2.5/3.
    if (m_workingIntersectToAux && workingRan) {
        for (int round = 0; round < 4; ++round) {
            m_layerRegistry->setWorkingDirty(true);
            runWorkingPhase();
            if (!(measureLinkedVars() || measureMeasureVars()
                  || measureAngleMeasureVars()))
                break;
            // Working geometry moved measured values → aux re-settle so the next
            // round re-solves the working intersections against the fresh pose.
            m_layerRegistry->setAuxDirty(true);
            std::vector<ResolveDiagnostic> auxDiag4;  // discarded
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters,
                                 m_conditioned, &auxDiag4,
                                 Resolver::Scope::AuxOnly, kAuxLayer,
                                 nullptr);
            m_layerRegistry->setAuxDirty(false);
        }
    }

    // Component-level attachments (组件级连接): drive each component's
    // OVERALL pose as one rigid transform AFTER the line-level settle so the
    // external leader's final position is used. If any component ACTUALLY
    // moved, re-settle the line forest (external followers of members follow
    // the new member poses; internal relations are preserved by the rigid
    // transform). 2026-09 性能: settleComponents 只在姿态真的变了才返回 true
    // (旧实现只要存在组件级连接就无条件跑满 4 轮), 且收窄求解下跳过与本次
    // 移动无关的组件 —— 拖帧/无变化文档零森林重解.
    // 曲线跟随组件拖动抖动 (用户报告 2026-09): 锚点 follow 后处理改写宿主
    // 曲线切线 → 吸附到该端点的内部线 (B dir = A exit + 180) 在重解时被重
    // 定向 → 组件对齐打破 (bEnd 离开 xEnd), 且其后没有组件再沉降. 修复:
    // 把 [组件沉降 → 锚点后处理 → 同范围重解] 包成有界外层收敛循环, 直到
    // 两者都不再移动 (不动点 5 未知/5 约束, 见 test_component.cpp
    // dragComponentLeaderCurveFollowStable). 常见拖帧 (无锚点跟随) 第一轮
    // 即 break, 仅多一次 settleComponents 扫描 + 后处理扫描.
    constexpr int kMaxFollowSettleRounds = 4;
    for (int round = 0; round < kMaxFollowSettleRounds; ++round) {
        for (int inner = 0; inner < 4; ++inner) {
            const bool compMoved = settleComponents(*passAttachments, effAffected);
            if (!compMoved) break;
            // 组件动了 → 重解线级森林 (外部 followers 跟随成员新姿态)
            std::vector<ResolveDiagnostic> reDiag;  // discarded (phase 2 owns m_diagnostics)
            // 组件沉降移动成员 → 基于成员点的测量值 (M_ 变量) 可能变化 →
            // 必须先重测, 否则引用这些测量的块 (如曲线 Polar 端) 用旧值
            // 欠收敛一帧, 停顿帧才修正 = 拖动时曲线锚点微抖 (用户报告
            // 2026-09 "曲线跟随拖动的时候晃动"). 测量变化 → 升级全量重解.
            if (measureLinkedVars() || measureMeasureVars()
                || measureAngleMeasureVars()) {
                if (effAffected) effAffected = nullptr;
            }
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                                 &reDiag, Resolver::Scope::All, QUuid(), effAffected);
        }

        // --- Curve-anchor follow post-pass ---
        // CurveAnchor points with a follow connection track their target point:
        // recompute chord-relative params so the anchor stays at target + offset.
        // Only blocks in layer groups that actually re-resolved are touched.
        // NOTE: this pass runs AFTER every Resolver pass AND the component
        // settle of THIS round, so the anchor is re-targeted against the
        // component's current pose — it no longer ends the frame at target +
        // (the settle's per-frame displacement), which previously alternated
        // with the correct position on pause frames and read as drag wobble
        // (曲线跟随组件拖动抖动, 用户报告 2026-09).
        // NOTE: an anchor moved here leaves attached followers on the OLD anchor
        // position for the rest of this round — the final drag frame would show
        // the follower one frame behind (曲线点连接不跟随, 用户报告 2026-08).
        // followMoved tracks that case so the re-settle below closes the gap.
        bool followMoved = false;
        for (auto& blk : m_blocks) {
            const bool blkAux = isAuxLayer(blk.layer);
            if (!(blkAux ? (auxRan || xLayerMoved) : workingRan)) continue;
            for (auto& pt : blk.points) {
                if (pt.constraint != PointConstraint::CurveAnchor)
                    continue;
                if (pt.followPointId.isNull())
                    continue;
                // Find the target point (may be in another block or the same one).
                const Block* targetBlk = (pt.followBlockId == blk.id)
                    ? &blk : findBlock(pt.followBlockId);
                if (!targetBlk) continue;
                const ParamPoint* target = targetBlk->findPoint(pt.followPointId);
                if (!target || !target->resolved) continue;

                // Desired world position = target world pos + follow offset.
                const geo::Vec2 targetWorld = targetBlk->transform.toWorld(target->resolvedPos);
                const geo::Vec2 desiredWorld = targetWorld + pt.followOffset;
                const geo::Vec2 desiredLocal = blk.transform.toLocal(desiredWorld);

                // Convert to chord-relative (percent, offset).
                const Segment* hostSeg = blk.findSegment(pt.hostSegmentId);
                if (!hostSeg) continue;
                const ParamPoint* sp = blk.findPoint(hostSeg->startPointId);
                const ParamPoint* ep = blk.findPoint(hostSeg->endPointId);
                if (!sp || !ep || !sp->resolved || !ep->resolved) continue;
                const geo::Vec2 chord = ep->resolvedPos - sp->resolvedPos;
                const double len = chord.length();
                if (len < 1e-9) continue;
                const geo::Vec2 unitDir = chord / len;
                const geo::Vec2 normal{-unitDir.y, unitDir.x};
                const geo::Vec2 rel = desiredLocal - sp->resolvedPos;
                pt.interpPercent = rel.dot(unitDir) / len;
                pt.interpOffsetDist = rel.dot(normal);

                // Re-resolve this point's position from the new params.
                const geo::Vec2 newPos = sp->resolvedPos
                                       + unitDir * (len * pt.interpPercent)
                                       + normal * pt.interpOffsetDist;
                // The anchor moved — bump the epoch so the canvas rebuilds the
                // curve cache this frame (Block::resolve's own epoch bump already
                // ran BEFORE this post-pass, so without this the curve would keep
                // passing through the OLD anchor until the next resolve).
                if (pt.resolvedPos.distanceSquaredTo(newPos) > 1e-6) {
                    ++blk.geometryEpoch;
                    followMoved = true;
                }
                pt.resolvedPos = newPos;
                pt.resolved = true;
            }
        }

        // ── Curve-anchor follow re-settle (曲线点连接同帧跟随, 2026-08) ──
        // The post-pass moved anchor(s) AFTER the attachment settle of this
        // round. Re-run the same scoped resolve(s) (affected-narrowed) so
        // followers attached to the moved anchors land on the fresh positions
        // in THIS frame — and the curve span cache is rebuilt against the moved
        // anchor (exit-direction lookups stay consistent). The re-settle may
        // re-derive a follower's rotation from the anchor-moved tangent and
        // break the component alignment again — hence the outer loop re-runs
        // [component settle → post-pass] until neither moves (fixed point
        // exists: 5 unknowns / 5 equations). Bounded: the re-settle is a plain
        // Resolver pass and never re-enters this post-pass (it lives below),
        // so no recursion; the outer loop caps oscillation at
        // kMaxFollowSettleRounds.
        if (followMoved) {
            if (workingRan) {
                GCAD_PERF_SCOPE("resolve.followResettle");
                std::vector<ResolveDiagnostic> reDiag;  // discarded (phase 2 owns m_diagnostics)
                Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                                     &reDiag, Resolver::Scope::WorkingOnly, kAuxLayer,
                                     effAffected);
            }
            if (auxRan || xLayerMoved) {
                GCAD_PERF_SCOPE("resolve.followResettleAux");
                std::vector<ResolveDiagnostic> reDiag;  // discarded
                Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                                     &reDiag, Resolver::Scope::AuxOnly, kAuxLayer,
                                     effAffected);
            }
        }

        // 本轮组件与锚点都没动 → 已到不动点, 结束外层收敛。
        // compMoved 由内层循环消费 (它负责组件动了之后的 forest 重解); 这里
        // 只看 post-pass —— 锚点没动就没有需要再对账的几何, 继续一轮纯属浪费.
        if (!followMoved) break;
    }

    emit resolved();
    if (emitDocChanged)
        emit documentChanged();
    cad::perf::Probe::get().frameTick();  // perf probe: one logical frame done
}

} // namespace cad::param