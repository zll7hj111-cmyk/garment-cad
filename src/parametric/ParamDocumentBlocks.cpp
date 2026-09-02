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

// 自由点 / Block / Component 生命周期 + 删除影响报告 (2026-08 拆分)

void ParamDocument::addFreePoint(ParamPoint pt)
{
    m_freePoints.push_back(std::move(pt));
    emit documentChanged();
}

void ParamDocument::removeFreePoint(const QUuid& id)
{
    auto it = std::find_if(m_freePoints.begin(), m_freePoints.end(),
        [&id](const ParamPoint& p) { return p.id == id; });
    if (it != m_freePoints.end()) {
        m_freePoints.erase(it);
        emit documentChanged();
    }
}

ParamPoint* ParamDocument::findFreePoint(const QUuid& id)
{
    auto it = std::find_if(m_freePoints.begin(), m_freePoints.end(),
        [&id](const ParamPoint& p) { return p.id == id; });
    return (it != m_freePoints.end()) ? &(*it) : nullptr;
}

// --- Blocks ---

bool ParamDocument::blockPointerInRange(const Block* p) const noexcept
{
#ifndef NDEBUG
    // Debug builds: a pointer handed out before a structural mutation points
    // into a buffer the vector no longer owns. Comparing against the CURRENT
    // storage catches the common case (a fresh, differently-placed allocation);
    // it cannot catch a realloc that happens to land at the same address.
    if (p == nullptr) return false;
    const Block* first = m_blocks.data();
    return p >= first && p < first + m_blocks.size();
#else
    Q_UNUSED(p);
    return true;   // release: no bookkeeping, no behaviour change
#endif
}

QUuid ParamDocument::addBlock(Block block)
{
    QUuid id = block.id;
    // A block without a layer assignment lands on the first WORKING layer —
    // never on the auxiliary calculation layer (no implicit aux drafts).
    if (block.layer.isNull())
        block.layer = m_layerRegistry->firstWorkingLayerId();
    // Assign readable serials to any points/segments that lack one.
    for (auto& pt : block.points)
        if (pt.serial.isEmpty()) pt.serial = newPointSerial();
    for (auto& seg : block.segments)
        if (seg.serial.isEmpty()) seg.serial = newLineSerial();
    block.resolve(m_parameters, m_conditioned);
    m_blockIndex.insert(id, static_cast<int>(m_blocks.size()));
    m_blocks.push_back(std::move(block));
    bumpStructureEpoch();  // P1-3: m_blocks may have reallocated
    m_followersDirty = true;  // the new block may be a future attachment endpoint
    m_referenceIndexDirty = true;  // its points may be referenced / ref fields set
    emit blockAdded(id);
    emit documentChanged();
    emit structureChanged();
    return id;
}

void ParamDocument::removeBlock(const QUuid& id)
{
    auto it = m_blockIndex.find(id);
    if (it == m_blockIndex.end()) return;

    const int idx = it.value();
    m_blocks.erase(m_blocks.begin() + idx);  // invalidates every Block* past idx
    bumpStructureEpoch();                    // P1-3
    m_blockIndex.erase(it);

    // Rebuild index for elements after the removed one.
    for (int i = idx; i < static_cast<int>(m_blocks.size()); ++i)
        m_blockIndex[m_blocks[i].id] = i;

    // Also remove attachments referencing this block
    m_attachments.erase(
        std::remove_if(m_attachments.begin(), m_attachments.end(),
            [&id](const Attachment& a) {
                return a.fromBlockId == id || a.toBlockId == id;
            }),
        m_attachments.end());
    recountCrossLayerAttachments();
    m_followersDirty = true;  // attachments referencing the removed block vanished
    m_referenceIndexDirty = true;  // its points may have been referenced

    // Auto-delete linked variables whose source is this block. Exact-match
    // consumers (length-linked copies, 复制的线段) first bake the frozen
    // measurement back to a plain number — the reference object is gone
    // (引用对象被删, 长度恢复为数值).
    for (const auto& lv : m_measureStore->linkedVars()) {
        if (lv.sourceBlockId != id || lv.refName.isEmpty()) continue;
        for (auto& b : m_blocks) {
            for (auto& s : b.segments)
                if (s.lengthFormula == lv.refName)
                    s.lengthFormula.clear();
            for (auto& p : b.points) {
                if (p.distanceFormula != lv.refName) continue;
                p.distance = lv.value;   // frozen measurement (mm)
                p.distanceFormula.clear();
            }
        }
    }
    m_measureStore->purgeBlockReferences(id);

    // Dart lines (省道线) that referenced the removed block (as start pin A
    // or offset point B) lose their constraint and degrade to plain lines —
    // their current geometry stays frozen in place (降级普通线).
    for (auto& b : m_blocks) {
        if (b.dartStartBlockId == id || b.dartRefBlockId == id) {
            b.dartStartBlockId = {};
            b.dartStartPointId = {};
            b.dartRefBlockId   = {};
            b.dartRefPointId   = {};
            b.dartRefSegmentId = {};
            b.dartOffsetFormula.clear();
            b.dartAngleFormula.clear();
        }
    }

    // ── 影子善后 (拆开影子基准, DETACH_SHADOW_DESIGN.md §6 状态机 ⑤⑥⑦) ──
    // ⑥ 本体 (master) 被删 → 其影子块级联删除 (系统级, 不计入影响报告);
    //    Att2 一并移除 → 跟随线失去角度基准转独立线 (方向冻结)。
    //    跟随线被删 → 影子失去 Att2 (上方引用清理已删) → 影子+Att1 清理
    //    (无消费者的系统对象不留孤儿)。
    // ⑦ 挂载宿主 (L3) 被删 → Att1 已随引用清理消失 → 影子弹回拆开态
    //    (保持当前解算姿态 = 冻结当前方向, Att2 回 angleOnly)。
    {
        std::vector<QUuid> shadowsToRemove;
        std::vector<QUuid> shadowsToRelease;
        for (const auto& b : m_blocks) {
            if (!b.isShadow) continue;
            bool hasAtt2 = false, hasAtt1 = false;
            for (const auto& a : m_attachments) {
                if (a.isPin) continue;
                if (a.toBlockId == b.id && a.fromComponentId.isNull())
                    hasAtt2 = true;
                if (a.fromBlockId == b.id) hasAtt1 = true;
            }
            if (b.shadowMasterBlockId == id || !hasAtt2)
                shadowsToRemove.push_back(b.id);
            else if (!hasAtt1)
                shadowsToRelease.push_back(b.id);
        }
        for (const QUuid& sid : shadowsToRelease) {
            for (auto& a : m_attachments) {
                if (!a.isPin && a.toBlockId == sid) {
                    a.angleOnly = true;
                    a.isLocked = false;
                    a.slideMode = SlideMode::None;
                }
            }
        }
        for (const QUuid& sid : shadowsToRemove) {
            m_attachments.erase(
                std::remove_if(m_attachments.begin(), m_attachments.end(),
                    [&sid](const Attachment& a) {
                        return !a.isPin
                            && (a.toBlockId == sid || a.fromBlockId == sid);
                    }),
                m_attachments.end());
        }
        if (!shadowsToRelease.empty()) {
            recountCrossLayerAttachments();
            m_followersDirty = true;
        }
        for (const QUuid& sid : shadowsToRemove)
            removeBlock(sid);  // 级联信号/索引/重解复用完整路径 (影子无嵌套)
    }

    emit blockRemoved(id);
    // Bridges pinned to the removed block just lost a pin — they are released
    // as independent segments (父线段删除后桥接线独立, see Block::isBridge).
    releaseOrphanedBridges();
    // Intersection points that lost their ray origin or target segment are
    // frozen at their last position (角度基准消失后交点冻结).
    degradeOrphanedIntersections();
    // Component cleanup: drop the removed block from any component (dissolve
    // groups that fall below two members).
    if (pruneComponentsForBlock(id))
        emit componentsChanged();
    // Component-level attachments whose exposed endpoint died with the block
    // become dangling (组件失去外部连接) — remove them.
    for (auto it = m_attachments.begin(); it != m_attachments.end(); ) {
        if (it->fromComponentId.isNull()) { ++it; continue; }
        const Component* c = findComponent(it->fromComponentId);
        if (c && !memberOwningPoint(*c, it->fromPointId).isNull()) { ++it; continue; }
        it = m_attachments.erase(it);
        m_followersDirty = true;
    }
    // The attachment graph changed: re-resolve so remaining blocks settle and
    // stale diagnostics (e.g. a dangling point on the removed block) refresh.
    resolveAll();
    emit structureChanged();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Components (组件: rigid work groups)
// ═══════════════════════════════════════════════════════════════════════════════

Component* ParamDocument::findComponent(const QUuid& id)
{
    for (auto& c : m_components)
        if (c.id == id) return &c;
    return nullptr;
}

const Component* ParamDocument::findComponent(const QUuid& id) const
{
    for (const auto& c : m_components)
        if (c.id == id) return &c;
    return nullptr;
}

Component* ParamDocument::componentOfBlock(const QUuid& blockId)
{
    const auto it = m_blockToComponent.constFind(blockId);
    if (it == m_blockToComponent.constEnd()) return nullptr;
    return findComponent(it.value());
}

const Component* ParamDocument::componentOfBlock(const QUuid& blockId) const
{
    const auto it = m_blockToComponent.constFind(blockId);
    if (it == m_blockToComponent.constEnd()) return nullptr;
    return findComponent(it.value());
}

QUuid ParamDocument::addComponent(Component comp)
{
    // Validate: at least two members, all present.
    if (comp.memberBlockIds.size() < 2)
        return comp.id;
    for (const QUuid& mid : comp.memberBlockIds)
        if (!blockById(mid))
            return comp.id;  // dangling member — nothing added

    m_components.push_back(std::move(comp));
    const QUuid id = m_components.back().id;
    for (const QUuid& mid : m_components.back().memberBlockIds)
        m_blockToComponent.insert(mid, id);
    emit componentsChanged();
    emit documentChanged();
    emit structureChanged();
    return id;
}

void ParamDocument::restoreComponentRaw(Component comp)
{
    m_components.push_back(std::move(comp));
    const QUuid id = m_components.back().id;
    for (const QUuid& mid : m_components.back().memberBlockIds)
        m_blockToComponent.insert(mid, id);
}

void ParamDocument::removeComponentRecord(const QUuid& id)
{
    const auto it = std::find_if(m_components.begin(), m_components.end(),
        [&id](const Component& c) { return c.id == id; });
    if (it == m_components.end()) return;
    for (const QUuid& mid : it->memberBlockIds)
        m_blockToComponent.remove(mid);
    m_components.erase(it);
    emit componentsChanged();
    emit structureChanged();
}

void ParamDocument::updateComponent(const Component& comp)
{
    Component* c = findComponent(comp.id);
    if (!c) return;
    *c = comp;
    emit componentsChanged();
}

QSet<QUuid> ParamDocument::componentClosure(const QSet<QUuid>& seed) const
{
    QSet<QUuid> out = seed;
    if (m_components.empty()) return out;
    for (const auto& c : m_components) {
        bool hit = false;
        for (const QUuid& mid : c.memberBlockIds)
            if (seed.contains(mid)) { hit = true; break; }
        if (!hit) continue;
        for (const QUuid& mid : c.memberBlockIds)
            out.insert(mid);
    }
    return out;
}

QUuid ParamDocument::memberOwningPoint(const Component& comp, const QUuid& pointId) const
{
    for (const QUuid& mid : comp.memberBlockIds) {
        const Block* m = blockById(mid);
        if (m && m->findPoint(pointId))
            return mid;
    }
    return QUuid();
}

BBox ParamDocument::boundingBoxOf(const QUuid& componentId) const
{
    BBox box;
    const Component* c = findComponent(componentId);
    if (!c) return box;
    for (const QUuid& mid : c->memberBlockIds) {
        const Block* b = blockById(mid);
        if (!b) continue;
        for (const auto& pt : b->points) {
            if (!pt.resolved) continue;
            box.expand(b->transform.toWorld(pt.resolvedPos));
        }
        // Curve control hulls (conservative): the spans cache carries the bbox.
        for (const auto& seg : b->segments) {
            const CurveSpanEntry* e = b->curveSpanEntry(seg.id);
            if (!e) continue;
            const geo::Vec2 corners[4] = {
                b->transform.toWorld(e->bboxMin),
                b->transform.toWorld(geo::Vec2(e->bboxMax.x, e->bboxMin.y)),
                b->transform.toWorld(geo::Vec2(e->bboxMin.x, e->bboxMax.y)),
                b->transform.toWorld(e->bboxMax),
            };
            for (const geo::Vec2& p : corners) box.expand(p);
        }
    }
    return box;
}

BBox ParamDocument::boundingBoxOfBlocks(const QList<QUuid>& blockIds) const
{
    // 选集 AABB (旋转选集高亮, 2026-08-27): 与 boundingBoxOf 相同的逐块几何
    // (resolved 点 + 曲线控制 hull), 并集到任意块集上。
    BBox box;
    for (const QUuid& mid : blockIds) {
        const Block* b = blockById(mid);
        if (!b) continue;
        for (const auto& pt : b->points) {
            if (!pt.resolved) continue;
            box.expand(b->transform.toWorld(pt.resolvedPos));
        }
        for (const auto& seg : b->segments) {
            const CurveSpanEntry* e = b->curveSpanEntry(seg.id);
            if (!e) continue;
            const geo::Vec2 corners[4] = {
                b->transform.toWorld(e->bboxMin),
                b->transform.toWorld(geo::Vec2(e->bboxMax.x, e->bboxMin.y)),
                b->transform.toWorld(geo::Vec2(e->bboxMin.x, e->bboxMax.y)),
                b->transform.toWorld(e->bboxMax),
            };
            for (const geo::Vec2& p : corners) box.expand(p);
        }
    }
    return box;
}

// ── Component-level attachment settlement (组件级连接: 整组整体变换) ──
// The external line drives the component's OVERALL pose as ONE rigid transform
// (借用暴露端点连接 + 借用端点线段形成夹角): rotate every member about the exposed
// endpoint so its direction reaches refWorld + π − α, then translate so the
// exposed endpoint lands on the leader point. Member-internal relations stay
// ACTIVE — a rigid transform preserves their relative geometry, so line-level
// attachments inside the component remain satisfied without any override.
bool ParamDocument::applyComponentTransform(Component& comp, const Attachment& att, const Block& toBlk)
{
    const QUuid exposedBlockId = memberOwningPoint(comp, att.fromPointId);
    Block* exposed = blockById(exposedBlockId);
    if (!exposed) return false;
    const ParamPoint* p = exposed->findPoint(att.fromPointId);
    if (!p || !p->resolved) return false;
    if (comp.memberBlockIds.empty()) return false;

    const geo::Vec2 pWorld = exposed->transform.toWorld(p->resolvedPos);
    const double localDir = exposed->directionAtPoint(att.fromPointId);
    const double refWorld = toBlk.transform.rotation
        + toBlk.exitDirectionAtPoint(att.toPointId, att.toSegmentId);
    double angleDeg = att.followerAngle;
    if (!att.followerAngleFormula.isEmpty()) {
        auto r = ConditionEngine::evaluate(att.followerAngleFormula, m_parameters, m_conditioned);
        if (r.ok) angleDeg = r.value;  // 公式结果即角度 (度), 无单位换算
    }
    const double angleRad = geo::degToRad(angleDeg);
    const double targetRot = refWorld + M_PI - angleRad - localDir;
    const double delta = targetRot - exposed->transform.rotation;
    const geo::Vec2 toWorld = toBlk.worldPos(att.toPointId);
    const geo::Vec2 translate = toWorld - pWorld;

    // 2026-09 性能: 姿态无变化 (epsilon, 与 applyAttachment 同阈值) 则整体跳过
    // — 旧实现无条件写回所有成员 transform + 触发森林重解, 每次 resolve 白跑.
    if (std::abs(delta) < 1e-9 && translate.lengthSquared() < 1e-12)
        return false;

    for (const QUuid& mid : comp.memberBlockIds) {
        Block* m = blockById(mid);
        if (!m) continue;
        m->transform.origin = pWorld + (m->transform.origin - pWorld).rotated(delta);
        m->transform.rotation += delta;
        m->transform.origin += translate;
    }
    return true;
}

bool ParamDocument::settleComponents(const std::vector<Attachment>& atts,
                                     const QSet<QUuid>* affectedOnly)
{
    if (m_components.empty()) return false;
    bool anyMoved = false;
    for (const auto& att : atts) {
        if (att.fromComponentId.isNull()) continue;
        Component* comp = findComponent(att.fromComponentId);
        if (!comp || comp->memberBlockIds.empty()) continue;
        const Block* toBlk = blockById(att.toBlockId);
        if (!toBlk) continue;
        // 收窄求解 (拖帧 affectedOnly): leader 与所有成员都不在 affected =
        // 全部冻结 → 组件姿态不可能变, 整体跳过 (每组件一次集合查询).
        if (affectedOnly) {
            bool touches = affectedOnly->contains(att.toBlockId);
            if (!touches) {
                for (const QUuid& mid : comp->memberBlockIds) {
                    if (affectedOnly->contains(mid)) { touches = true; break; }
                }
            }
            if (!touches) continue;
        }
        if (applyComponentTransform(*comp, att, *toBlk))
            anyMoved = true;
    }
    return anyMoved;
}

bool ParamDocument::pruneComponentsForBlock(const QUuid& blockId)
{
    bool changed = false;
    for (auto it = m_components.begin(); it != m_components.end(); ) {
        Component& c = *it;
        const int idx = c.memberIndex(blockId);
        if (idx < 0) { ++it; continue; }
        c.memberBlockIds.erase(c.memberBlockIds.begin() + idx);
        m_blockToComponent.remove(blockId);
        changed = true;
        // 暴露端点宿主被删 → 暴露端点失效 (首个外部连接会重新记录).
        if (c.exposedPointId == blockId || memberOwningPoint(c, c.exposedPointId).isNull()) {
            c.exposedPointId = {};
            c.exposedSegmentId = {};
        }
        if (c.memberBlockIds.size() < 2) {
            for (const QUuid& mid : c.memberBlockIds)
                m_blockToComponent.remove(mid);
            it = m_components.erase(it);
            continue;
        }
        ++it;
    }
    return changed;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Delete-impact report (删除影响报告)
// ═══════════════════════════════════════════════════════════════════════════════
// Mirrors every cascade branch of removeBlock() — keep the two in sync when
// the cleanup logic grows. Prediction only: no mutation.
//
// 影子块 (Block::isShadow, 拆开影子基准) 十项计数全不计入 (设计稿 §7.5 拍板:
// 影子不可见/系统级): 影子非桥线/省道、无交点/测量/关联变量, 结构上天然不
// 触发任何计数器; 本体或跟随线被删时影子的级联清理 (影子块 + Att1/Att2) 属
// 系统级善后, 同样不占位 —— 删除挂载宿主 L3 时其 Att1 因直接引用 L3 照常
// 计入 attachmentsRemoved (下方第 1 条既有规则覆盖)。
// ═══════════════════════════════════════════════════════════════════════════════

ParamDocument::DeleteImpact ParamDocument::deleteImpactReport(const QUuid& id) const
{
    DeleteImpact r;
    const Block* victim = blockById(id);
    if (!victim) return r;

    // 1. Attachments referencing the block vanish with it.
    for (const auto& a : m_attachments)
        if (a.fromBlockId == id || a.toBlockId == id)
            ++r.attachmentsRemoved;

    // 2. Bridges that lose at least one pin AND would drop below two pins are
    //    released as independent segments (releaseOrphanedBridges semantics).
    for (const auto& b : m_blocks) {
        if (!b.isBridge || b.id == id) continue;
        int pins = 0, pinsToVictim = 0;
        for (const auto& a : m_attachments) {
            if (!a.isPin || a.fromBlockId != b.id) continue;
            ++pins;
            if (a.toBlockId == id) ++pinsToVictim;
        }
        if (pinsToVictim > 0 && pins - pinsToVictim < 2)
            ++r.bridgesReleased;
    }

    // 3. Intersection points whose ray origin lives in the victim block are
    //    frozen at their last position (degradeOrphanedIntersections: the
    //    origin point can be cross-block; the target segment is always inside
    //    the point's own block, so only the origin can be lost here).
    for (const auto& b : m_blocks) {
        if (b.id == id) continue;
        for (const auto& pt : b.points) {
            if (pt.constraint != PointConstraint::Intersection) continue;
            if (victim->findPoint(pt.refPointA))
                ++r.intersectionsFrozen;
            // Aim-point reference (指向点) lost → the ray falls back to its
            // stored angle (reference cleared, point stays valid).
            if (!pt.interAimPointId.isNull() && victim->findPoint(pt.interAimPointId))
                ++r.intersectionsAimCleared;
        }
    }

    // 4+5. Linked variables sourced from the victim: consumers freeze their
    //    measurement back to a plain number; the variables themselves die.
    for (const auto& lv : m_measureStore->linkedVars()) {
        if (lv.sourceBlockId != id) continue;
        ++r.linkedVarsRemoved;
        if (lv.refName.isEmpty()) continue;
        for (const auto& b : m_blocks) {
            if (b.id == id) continue;  // the victim's own refs die with it
            for (const auto& s : b.segments)
                if (s.lengthFormula == lv.refName ||
                    s.extendStartFormula == lv.refName ||
                    s.extendEndFormula == lv.refName) ++r.linkedFrozen;
            for (const auto& p : b.points)
                if (p.distanceFormula == lv.refName) ++r.linkedFrozen;
        }
    }

    // 6. Measure variables referencing the victim (as endpoint or owner).
    for (const auto& mv : m_measureStore->measureVars())
        if (mv.blockA == id || mv.blockB == id || mv.ownerBlockId == id)
            ++r.measureVarsRemoved;

    // 7. Angle measures referencing the victim (either segment's host).
    for (const auto& am : m_measureStore->angleMeasures())
        if (am.blockA == id || am.blockB == id)
            ++r.angleVarsRemoved;

    // 8. Formulas referencing any measurement name removed above lose their
    //    operand and will report an evaluation error on the next resolve.
    QSet<QString> removedNames;
    for (const auto& lv : m_measureStore->linkedVars())
        if (lv.sourceBlockId == id && !lv.refName.isEmpty())
            removedNames.insert(lv.refName);
    for (const auto& mv : m_measureStore->measureVars())
        if ((mv.blockA == id || mv.blockB == id || mv.ownerBlockId == id)
            && !mv.refName.isEmpty())
            removedNames.insert(mv.refName);
    for (const auto& am : m_measureStore->angleMeasures())
        if ((am.blockA == id || am.blockB == id) && !am.refName.isEmpty())
            removedNames.insert(am.refName);
    if (!removedNames.isEmpty()) {
        for (const auto& f : m_variableStore->formulas()) {
            const QStringList names = ExpressionEvaluator::referencedNames(f.expression);
            for (const QString& n : names) {
                if (removedNames.contains(n)) { ++r.formulasBroken; break; }
            }
        }
    }

    // 9. Dart lines (省道线) that referenced the victim (start pin A or offset
    //    point B) degrade to plain lines, keeping their current geometry.
    for (const auto& b : m_blocks) {
        if (b.id == id) continue;
        if (b.dartStartBlockId == id || b.dartRefBlockId == id)
            ++r.dartLinesDegraded;
    }
    return r;
}

Block* ParamDocument::findBlock(const QUuid& id)
{
    return blockById(id);
}

const Block* ParamDocument::findBlock(const QUuid& id) const
{
    return blockById(id);
}

Block* ParamDocument::blockById(const QUuid& id)
{
    auto it = m_blockIndex.find(id);
    if (it == m_blockIndex.end()) return nullptr;
    return &m_blocks[it.value()];
}

const Block* ParamDocument::blockById(const QUuid& id) const
{
    auto it = m_blockIndex.find(id);
    if (it == m_blockIndex.end()) return nullptr;
    return &m_blocks[it.value()];
}

} // namespace cad::param