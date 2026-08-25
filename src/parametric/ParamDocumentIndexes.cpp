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

// 序列号分配 / 图层删除 / 跟随者与引用索引 / 跨层扫描 (2026-08 拆分)

QString ParamDocument::newPointSerial()
{
    return Serial::make(Serial::randomPrefix(), QLatin1Char('P'), m_nextPointSeq++);
}

QString ParamDocument::newLineSerial()
{
    return Serial::make(Serial::randomPrefix(), QLatin1Char('L'), m_nextLineSeq++);
}

// --- Canvas layers (facade: block re-layering + registry removal) ---

void ParamDocument::removeLayer(const QUuid& layerId)
{
    const int n = m_layerRegistry->layerCount();
    if (n <= 2)
        return;  // Need at least aux + one working layer.
    const int row = m_layerRegistry->indexOf(layerId);
    if (row < 0 || m_layerRegistry->isAuxLayer(layerId))
        return;  // Unknown or the auxiliary calculation layer: cannot be removed.

    // Blocks in the removed layer fall to the layer below, but never into the
    // auxiliary layer. Blocks in other layers keep their stable ids untouched
    // (removal no longer shifts any references).
    QUuid targetId = row > 0 ? m_layerRegistry->layers()[static_cast<size_t>(row - 1)].id
                             : QUuid();
    if (targetId.isNull() || m_layerRegistry->isAuxLayer(targetId))
        targetId = m_layerRegistry->firstWorkingLayerId();
    for (auto& b : m_blocks) {
        if (b.layer == layerId)
            b.layer = targetId;
    }
    m_layerRegistry->removeLayerRaw(layerId);
}

// --- Resolve ---

// ═══════════════════════════════════════════════════════════════════════════════
// Dirty-subgraph machinery (阶段2: 依赖边表 + 脏传播)
// ═══════════════════════════════════════════════════════════════════════════════

void ParamDocument::ensureFollowersIndex() const
{
    if (!m_followersDirty) return;
    m_followersOf.clear();
    for (const auto& att : m_attachments) {
        if (!att.fromComponentId.isNull()) {
            // 组件级连接: leader 移动 → 组件整体 (所有成员) 需重新结算.
            if (const Component* c = findComponent(att.fromComponentId))
                for (const QUuid& mid : c->memberBlockIds)
                    m_followersOf[att.toBlockId].push_back(mid);
            continue;
        }
        m_followersOf[att.toBlockId].push_back(att.fromBlockId);
    }
    m_followersDirty = false;
}

// ── 跨块引用索引 (2026-09 性能专项) ─────────────────────────────────────────
// target 块 → 引用它的块集合, 语义与 blockReferences() 完全一致 (但不含自引用
// — 种子块已在 affected 中)。旧实现 collectAffected 的 BFS 每次出队都全表扫描
// 找 blockReferences → O(N²)/拖拽帧; 索引 O(1) 查询。失效时机: 任何全量
// resolveAll() (所有引用字段变更都经它落地) + 增删块/清库。
void ParamDocument::ensureReferencesIndex() const
{
    if (!m_referenceIndexDirty) return;
    m_referenceIndex.clear();

    // pointId → owning block (一次 O(N·P) 建表, 之后每引用 O(1) 定归属).
    QHash<QUuid, QUuid> pointOwner;
    {
        qsizetype total = 0;
        for (const auto& b : m_blocks) total += b.points.size();
        pointOwner.reserve(static_cast<int>(total));
    }
    for (const auto& b : m_blocks)
        for (const auto& pt : b.points)
            pointOwner.insert(pt.id, b.id);

    auto addRef = [&](const QUuid& target, const QUuid& from) {
        if (target.isNull() || from.isNull() || target == from) return;
        m_referenceIndex[target].insert(from, 1);
    };

    for (const auto& b : m_blocks) {
        addRef(b.endTargetBlockId, b.id);      // 终点指向
        addRef(b.dartStartBlockId, b.id);      // 省道: 起点 pin A
        addRef(b.dartRefBlockId, b.id);        // 省道: 偏移点 B
        for (const auto& pt : b.points) {
            addRef(pt.followBlockId, b.id);    // 曲线锚点跟随
            const QUuid refs[] = {pt.refPointId, pt.refPointA, pt.refPointB,
                                  pt.interpRefPointId, pt.interAimPointId};
            for (const QUuid& ref : refs) {
                if (ref.isNull()) continue;
                const auto it = pointOwner.constFind(ref);
                if (it != pointOwner.constEnd())
                    addRef(it.value(), b.id);  // polar/midpoint/on-seg/交点/插值
            }
        }
    }
    m_referenceIndexDirty = false;
}

//---------------------------------------------------------------------------------------------------------------------
void ParamDocument::recountCrossLayerAttachments()
{
    int count = 0;
    for (const auto& a : m_attachments) {
        const Block* fb = blockById(a.fromBlockId);
        const Block* tb = blockById(a.toBlockId);
        if (fb && tb && isAuxBlock(*fb) && !isAuxBlock(*tb))
            ++count;
    }
    m_crossLayerCount = count;
}

//---------------------------------------------------------------------------------------------------------------------
bool ParamDocument::scanAuxIntersectionCrossRefs() const
{
    // Any aux-layer Intersection point whose ref (refPointA / interAimPointId)
    // lives on a working layer? True = Phase 2.5 (跨层交点重解) must run on
    // every pass where both phases ran. Structural property — callers cache
    // the result; only full resolves re-scan (2026-10 性能).
    for (const auto& blk : m_blocks) {
        if (isAuxLayer(blk.layer)) continue;
        for (const auto& ob : m_blocks) {
            if (!isAuxLayer(ob.layer)) continue;
            for (const auto& pt : ob.points) {
                if (pt.constraint != PointConstraint::Intersection) continue;
                for (const QUuid& ref : {pt.refPointA, pt.interAimPointId}) {
                    if (ref.isNull()) continue;
                    if (blk.findPoint(ref)) return true;
                }
            }
        }
    }
    return false;
}

//---------------------------------------------------------------------------------------------------------------------
bool ParamDocument::scanWorkingIntersectionAuxRefs() const
{
    // Any WORKING-layer Intersection point whose ref (refPointA / interAimPointId)
    // lives on the aux layer? True = Phase 4 (工作侧跨层交点重解) must run after
    // the Phase 3 settle — the aux origin may have moved AFTER the working pass
    // solved the intersection, leaving it off the origin→borrow ray. Structural
    // property — cached like m_auxIntersectToWorking (2026-08 跨图层交点).
    for (const auto& blk : m_blocks) {
        if (isAuxLayer(blk.layer)) continue;
        for (const auto& pt : blk.points) {
            if (pt.constraint != PointConstraint::Intersection) continue;
            for (const QUuid& ref : {pt.refPointA, pt.interAimPointId}) {
                if (ref.isNull()) continue;
                for (const auto& ob : m_blocks) {
                    if (!isAuxLayer(ob.layer)) continue;
                    if (ob.findPoint(ref)) return true;
                }
            }
        }
    }
    return false;
}

//---------------------------------------------------------------------------------------------------------------------
QSet<QUuid> ParamDocument::collectMobileAuxBlocks() const
{
    QSet<QUuid> mobile;
    if (m_crossLayerCount == 0)
        return mobile;  // fast path: the boundary is sealed — nothing moves
    ensureFollowersIndex();
    QList<QUuid> queue;
    // Seeds: the aux followers of every cross-layer edge (pins included — a
    // pinned bridge moves with its host positionally).
    for (const auto& a : m_attachments) {
        const Block* fb = blockById(a.fromBlockId);
        const Block* tb = blockById(a.toBlockId);
        if (!fb || !tb) continue;
        if (isAuxBlock(*fb) && !isAuxBlock(*tb) && !mobile.contains(a.fromBlockId)) {
            mobile.insert(a.fromBlockId);
            queue.push_back(a.fromBlockId);
        }
    }
    // Everything beneath a cross-layer follower moves with it (all aux —
    // working→aux edges are rejected, so the subtree never leaves the layer).
    while (!queue.isEmpty()) {
        const QUuid cur = queue.takeFirst();
        const auto fit = m_followersOf.constFind(cur);
        if (fit == m_followersOf.constEnd()) continue;
        for (const QUuid& f : fit.value())
            if (!mobile.contains(f)) {
                mobile.insert(f);
                queue.push_back(f);
            }
    }
    return mobile;
}

} // namespace cad::param