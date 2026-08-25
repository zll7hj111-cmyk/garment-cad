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

// Attachment 查找与全部附着操作（连接/滑轨/锁/降级/桥线） (2026-08 拆分)

Attachment* ParamDocument::findAttachment(const QUuid& id)
{
    for (auto& a : m_attachments)
        if (a.id == id)
            return &a;
    return nullptr;
}

const Attachment* ParamDocument::findAttachment(const QUuid& id) const
{
    for (const auto& a : m_attachments)
        if (a.id == id)
            return &a;
    return nullptr;
}

bool ParamDocument::addAttachment(Attachment att)
{
    // 组件级连接 (组件整体作为 follower, 借用暴露端点连接 + 端点线段方向):
    // handled by the component branch — the forest invariant is checked at the
    // COMPONENT dimension (one external line per component), the exposed
    // endpoint is validated against the member set, and the line-level
    // follower slots of the members are untouched.
    if (!att.fromComponentId.isNull())
        return addComponentAttachment(std::move(att));

    // Enforce the forest invariant (see Attachment.h glossary): reject
    // attachments referencing missing blocks, a second leader for the same
    // follower, or links that would close a cycle.
    const Block* fromBlock = blockById(att.fromBlockId);
    const Block* toBlock = blockById(att.toBlockId);
    if (!fromBlock || !toBlock)
        return false;
    // Cross-layer boundary rule (单向跨层附着, one-way only):
    //   aux follower → working leader  PERMITTED — the Resolver settles these
    //     followers in Phase 3 (跨层沉降) after the working layers are final;
    //     the aux layer's geometry thus tracks working-layer movement.
    //   working follower → aux leader  REJECTED — working geometry must never
    //     be driven by the frozen calculation draft.
    const bool fromAux = isAuxBlock(*fromBlock);
    const bool toAux   = isAuxBlock(*toBlock);
    if (!fromAux && toAux)
        return false;
    const bool crossLayer = fromAux && !toAux;

    // 拖动保护 (用户拍板 2026-08 复旧, 2026-10 曾被改为可选): **新建连接
    // 默认焊接** (isLocked=true) — "拖任一端整对移动" 是默认行为, 拖跟随线
    // 不再快速拆散 (拆散走 D 键快拆 / 面板「拖动保护」取消勾选 / 滑轨态)。
    // 属性面板「拖动保护」仍是可选焊接开关 (✗ = 解焊仍完整连接)。多线整体
    // 移动交给组件 (componentClosure)。旧档案 isLocked=false 的连接读出来
    // 保持解焊 (不迁移)。注意 undo/redo 的快照还原走
    // addAttachmentRaw/addAttachmentsRaw (verbatim), 不经过这里。
    // A bridge is a pure downstream leaf: its pinned endpoints cannot anchor
    // followers, and bridge-to-bridge pins are forbidden. However, an AUXILIARY
    // point on a bridge is a legitimate leader target — the Resolver settles
    // bridge followers after the bridge (Step 4/5), so they land correctly.
    if (toBlock->isBridge) {
        if (att.isPin)
            return false;
        const ParamPoint* tp = toBlock->findPoint(att.toPointId);
        if (!tp || !tp->isAuxiliary)
            return false;
    }
    if (checkAttachment(m_attachments, att) != AttachmentIssue::Ok)
        return false;
    // NOTE: 组对连接零限制 (模型层组零限制, 2026-08-04 设计定稿) ——
    // 无主连接预算, 组内外连接自由建立/断开, 与自由线段完全一致.

    // Value-cycle pre-check (值循环预检): a cross-layer edge that hangs a
    // measurement's SOURCE block beneath its CONSUMER would oscillate
    // (consumer pose → measured value → consumer pose). Reject it here.
    if (crossLayer && wouldCreateMeasureValueCycle(att))
        return false;

    // 新建连接默认勾选「拖动保护」(isLocked=true): 整对移动是默认语义。
    // 面板取消勾选 = 解焊 (连接保持完整, 拖跟随线可拆散); 仅角度态
    // (angleOnly) 或滑轨态的置位互斥由 setAttachmentXxx 保持。
    att.isLocked = true;

    m_attachments.push_back(std::move(att));
    if (crossLayer)
        ++m_crossLayerCount;
    m_followersDirty = true;  // new leader→follower edge
    resolveAll();
    emit structureChanged();
    return true;
}

/// 组件级连接校验 + 加入 (see addAttachment).
bool ParamDocument::addComponentAttachment(Attachment att)
{
    Component* comp = findComponent(att.fromComponentId);
    const Block* toBlock = blockById(att.toBlockId);
    if (!comp || !toBlock || comp->memberBlockIds.empty())
        return false;
    // 暴露端点必须是组件内某成员的端点 (借用端点).
    const QUuid exposedBlockId = memberOwningPoint(*comp, att.fromPointId);
    if (exposedBlockId.isNull())
        return false;
    // 森林不变式 (组件维度): 一个组件至多一条外部跟随线.
    for (const auto& a : m_attachments)
        if (a.fromComponentId == att.fromComponentId)
            return false;
    // 无环: 外部线及其 leader 链不能是组件内成员 (组件跟随 X 且 X 跟随
    // 组件成员 → 环).
    QSet<QUuid> seen;
    QUuid cur = att.toBlockId;
    while (!cur.isNull() && !seen.contains(cur)) {
        if (comp->isMember(cur))
            return false;
        seen.insert(cur);
        QUuid leader;
        for (const auto& a : m_attachments)
            if (a.fromBlockId == cur) { leader = a.toBlockId; break; }
        cur = leader;
    }
    // 跨层规则 (单向): 暴露端点所在成员 aux → working leader 允许;
    // working 成员 → aux leader 拒绝. 组件 v1 成员同层, 只校验暴露端点宿主.
    const Block* exposedBlk = blockById(exposedBlockId);
    if (exposedBlk && isAuxBlock(*exposedBlk) != isAuxBlock(*toBlock)) {
        if (!isAuxBlock(*exposedBlk))
            return false;  // working member → aux leader rejected
    }

    // 组件级连接同样默认勾选「拖动保护」(整组随外部线移动, 拖组件不拆).
    att.isLocked = true;

    m_attachments.push_back(std::move(att));
    m_followersDirty = true;  // new leader→component edge
    // 自动暴露: 首个组件级连接时记录借用端点为暴露端点.
    if (Component* c = findComponent(att.fromComponentId))
        recordExposedEndpoint(*c, att);
    resolveAll();
    emit structureChanged();
    return true;
}

void ParamDocument::recordExposedEndpoint(Component& comp, const Attachment& att)
{
    if (!comp.exposedPointId.isNull()) return;  // 首个连接时记录, 之后保持
    comp.exposedPointId = att.fromPointId;
    const Block* eb = blockById(memberOwningPoint(comp, att.fromPointId));
    comp.exposedSegmentId = eb ? eb->exitSegmentAtPoint(att.fromPointId) : QUuid();
}

void ParamDocument::removeAttachment(const QUuid& id)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it != m_attachments.end()) {
        // 断开组件级连接: 若暴露端点即该连接的借用端点 → 清空 (自动暴露还原).
        if (!it->fromComponentId.isNull()) {
            if (Component* c = findComponent(it->fromComponentId)) {
                if (c->exposedPointId == it->fromPointId) {
                    c->exposedPointId = {};
                    c->exposedSegmentId = {};
                    emit componentsChanged();
                }
            }
        }
        m_attachments.erase(it);
        recountCrossLayerAttachments();
        m_followersDirty = true;  // edge removed
        // Detaching either pin of a bridge releases it as an independent segment.
        releaseOrphanedBridges();
        resolveAll();
        emit structureChanged();
    }
}

void ParamDocument::setAttachmentLocked(const QUuid& id, bool locked)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end() || it->isLocked == locked)
        return;
    it->isLocked = locked;
    resolveAll();
}

void ParamDocument::setAttachmentAngleOnly(const QUuid& id, bool angleOnly)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end() || it->angleOnly == angleOnly)
        return;
    it->angleOnly = angleOnly;
    // 位置自由 ↔ 焊接互斥: 拆开自动解锁; 恢复完整连接重新焊接 (默认保护).
    it->isLocked = !angleOnly;
    // 拆开 (位置全自由) 与滑轨 (一轴自由) 互斥: 拆开时清除滑轨模式.
    it->slideMode = SlideMode::None;
    // 仅角度 (位置自由+角度跟随) 与角度独立 (位置钉住+角度自由) 相反。
    if (angleOnly)
        it->angleIndependent = false;
    resolveAll();
}

void ParamDocument::setAttachmentAngleIndependent(const QUuid& id, bool angleIndependent)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end() || it->angleIndependent == angleIndependent)
        return;
    it->angleIndependent = angleIndependent;
    if (angleIndependent) {
        // 角度独立 = 位置仍然全连接; 与“仅角度”和“滑轨”互斥。
        it->angleOnly = false;
        it->slideMode = SlideMode::None;
    } else {
        // 退出角度独立: 反算当前世界方向对应的 followerAngle, 恢复角度跟随
        // 时不跳线。清掉旧公式/弧长模式, 以反算值为准。
        const Block* from = blockById(it->fromBlockId);
        const Block* to = blockById(it->toBlockId);
        if (from && to) {
            // 若设置了独立角度基准, 退出角度独立后仍应回到那条基准。
            const Block* refBlock = to;
            double refWorld = to->transform.rotation
                + to->exitDirectionAtPoint(it->toPointId, it->toSegmentId);
            if (!it->angleRefBlockId.isNull()) {
                if (const Block* rb = blockById(it->angleRefBlockId))
                    refBlock = rb;
            }
            if (!it->angleRefBlockId.isNull() && !it->angleRefSegmentId.isNull()) {
                if (const Segment* seg = refBlock->findSegment(it->angleRefSegmentId)) {
                    if (!it->angleRefPointId.isNull()) {
                        if (const ParamPoint* rp = refBlock->findPoint(it->angleRefPointId);
                            rp && rp->resolved) {
                            refWorld = refBlock->transform.rotation
                                     + refBlock->exitDirectionAtPoint(
                                           it->angleRefPointId, it->angleRefSegmentId);
                        }
                    } else if (const ParamPoint* sp = refBlock->findPoint(seg->startPointId);
                               sp && sp->resolved) {
                        refWorld = refBlock->transform.rotation
                                 + refBlock->directionAtPoint(seg->startPointId);
                    }
                }
            }
            const double localDir = from->directionAtPoint(it->fromPointId);
            it->followerAngle = backSolveFollowerAngle(
                from->transform.rotation, localDir, refWorld);
            it->followerAngleFormula.clear();
            it->rotationMode = RotationMode::Angle;
            it->arcLength = 0.0;
            it->arcLengthFormula.clear();
        }
        it->angleOnly = false;
        it->slideMode = SlideMode::None;
    }
    resolveAll();
}

void ParamDocument::setAttachmentAngleRef(const QUuid& id,
                                          const QUuid& refBlockId,
                                          const QUuid& refSegmentId,
                                          const QUuid& refPointId)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end())
        return;
    if (it->angleRefBlockId == refBlockId
        && it->angleRefSegmentId == refSegmentId
        && it->angleRefPointId == refPointId)
        return;

    it->angleRefBlockId = refBlockId;
    it->angleRefSegmentId = refSegmentId;
    it->angleRefPointId = refPointId;
    // 有独立角度基准时取消“角度独立”，角度由指定线段约束。
    it->angleIndependent = false;

    // 反算当前世界方向到新基准, 避免切换角度基准时跳线。
    const Block* from = blockById(it->fromBlockId);
    const Block* to = blockById(it->toBlockId);
    if (from && to) {
        double refWorld = to->transform.rotation
            + to->exitDirectionAtPoint(it->toPointId, it->toSegmentId);
        const Block* refBlock = to;
        if (!refBlockId.isNull()) {
            if (const Block* rb = blockById(refBlockId))
                refBlock = rb;
        }
        if (!refBlockId.isNull() && !refSegmentId.isNull()) {
            const Segment* seg = refBlock->findSegment(refSegmentId);
            if (seg) {
                if (!refPointId.isNull()) {
                    // 使用用户选择的角度基准点的出口方向（与位置连接同构）。
                    if (const ParamPoint* rp = refBlock->findPoint(refPointId);
                        rp && rp->resolved) {
                        refWorld = refBlock->transform.rotation
                                 + refBlock->exitDirectionAtPoint(
                                       refPointId, refSegmentId);
                    }
                } else if (const ParamPoint* sp = refBlock->findPoint(seg->startPointId);
                           sp && sp->resolved) {
                    // 旧档/未选点：保持历史行为，用 start→end 方向。
                    refWorld = refBlock->transform.rotation
                             + refBlock->directionAtPoint(seg->startPointId);
                }
            }
        }
        const double localDir = from->directionAtPoint(it->fromPointId);
        it->followerAngle = backSolveFollowerAngle(
            from->transform.rotation, localDir, refWorld);
        it->followerAngleFormula.clear();
        it->rotationMode = RotationMode::Angle;
        it->arcLength = 0.0;
        it->arcLengthFormula.clear();
    }
    resolveAll();
}


void ParamDocument::setAttachmentSlideMode(const QUuid& id, SlideMode mode)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end() || it->slideMode == mode)
        return;
    if (mode != SlideMode::None) {
        // Snapshot the locked-axis coordinate from the CURRENT settled
        // geometry BEFORE switching (the follower's from-point projected onto
        // the leader-local rail frame). For a plain full connection this is
        // (0, 0) — the from-point sits exactly on the anchor.
        const Block* from = blockById(it->fromBlockId);
        const Block* to = blockById(it->toBlockId);
        if (from && to) {
            const auto [s, t] = computeSlideOffsets(*from, *it, *to);
            it->slideAlongMm = s;
            it->slidePerpMm = t;
        }
        // 滑轨与拆开/拖动保护互斥: 位置只留一轴自由度 — 必须解锁 (可滑动).
        it->angleOnly = false;
        it->angleIndependent = false;
        it->isLocked = false;
        it->slideMode = mode;
    } else {
        // 切回普通全连接: 位置吸附 + 角度跟随恢复; 拖动保护恢复焊接
        // (新建连接默认勾选「拖动保护」, 2026-08 复旧; 需要解焊可面板取消。
        // undo 路径经 SetAttachmentSlideModeCommand 快照恢复原 isLocked)。
        // 锁轴快照保留 (切回滑轨时按当时几何重快照, 不依赖旧值)。
        it->slideMode = SlideMode::None;
        it->angleOnly = false;
        it->isLocked = true;
    }
    resolveAll();
}

void ParamDocument::refreshSlideOffsets(const QUuid& id)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end() || it->slideMode == SlideMode::None)
        return;
    const Block* from = blockById(it->fromBlockId);
    const Block* to = blockById(it->toBlockId);
    if (!from || !to) return;
    const auto [s, t] = computeSlideOffsets(*from, *it, *to);
    it->slideAlongMm = s;
    it->slidePerpMm = t;
    resolveAll();
}

void ParamDocument::updateSlideOffsetsFromCurrent(const QUuid& id)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end()
        || it->slideMode == SlideMode::None)
        return;
    const Block* from = blockById(it->fromBlockId);
    const Block* to = blockById(it->toBlockId);
    if (!from || !to) return;
    const auto [s, t] = computeSlideOffsets(*from, *it, *to);
    // 只回写自由轴; 锁轴坐标保持激活时快照 (拖动只改变自由轴).
    if (it->slideMode == SlideMode::AlongLeader)
        it->slideAlongMm = s;
    else
        it->slidePerpMm = t;
}

QSet<QUuid> ParamDocument::lockedClosure(const QSet<QUuid>& seed) const
{
    QSet<QUuid> result = seed;
    bool expanded = true;
    while (expanded) {
        expanded = false;
        for (const auto& att : m_attachments) {
            // angleOnly (拆开保留角度) / 滑轨 (slideMode) attachments are
            // position-constrained picks (free / one-axis-free): they must
            // never weld the pair back together for drags.
            if (!att.isLocked || att.angleOnly
                || att.slideMode != SlideMode::None) continue;
            const bool fromIn = result.contains(att.fromBlockId);
            const bool toIn   = result.contains(att.toBlockId);
            if (fromIn != toIn) {
                result.insert(fromIn ? att.toBlockId : att.fromBlockId);
                expanded = true;
            }
        }
    }
    return result;
}

void ParamDocument::removeAttachments(const QList<QUuid>& ids)
{
    if (ids.isEmpty()) return;
    const QSet<QUuid> idSet(ids.begin(), ids.end());
    m_attachments.erase(std::remove_if(m_attachments.begin(), m_attachments.end(),
        [&idSet](const Attachment& a) { return idSet.contains(a.id); }),
        m_attachments.end());
    recountCrossLayerAttachments();
    m_followersDirty = true;
    // Same cascade as removeAttachment(): a bridge that lost a pin is released.
    releaseOrphanedBridges();
    resolveAll();
    emit structureChanged();
}

void ParamDocument::addAttachmentsRaw(const std::vector<Attachment>& atts)
{
    if (atts.empty()) return;
    m_attachments.insert(m_attachments.end(), atts.begin(), atts.end());
    recountCrossLayerAttachments();
    m_followersDirty = true;
    resolveAll();
    emit structureChanged();
}

int ParamDocument::removeAttachmentsOfBlock(const QUuid& blockId)
{
    const auto before = m_attachments.size();
    m_attachments.erase(
        std::remove_if(m_attachments.begin(), m_attachments.end(),
            [&blockId](const Attachment& a) {
                return a.fromBlockId == blockId || a.toBlockId == blockId;
            }),
        m_attachments.end());
    const int removed = static_cast<int>(before - m_attachments.size());
    if (removed > 0) {
        recountCrossLayerAttachments();
        m_followersDirty = true;  // edges removed
        // Kicking a block out may have unpinned bridges attached to it (or the
        // kicked block itself may be a bridge that just lost both pins) —
        // release them as independent segments.
        releaseOrphanedBridges();
        resolveAll();
        emit structureChanged();
    }
    return removed;
}

void ParamDocument::restoreFollowerAttachment(
    const QUuid& fromBlockId, const std::optional<Attachment>& followerAtt)
{
    std::erase_if(m_attachments, [&fromBlockId](const Attachment& a) {
        return !a.isPin && a.fromBlockId == fromBlockId;
    });
    if (followerAtt)
        addAttachmentRaw(*followerAtt);  // verbatim (keeps the snapshot's isLocked)
    resolveAll();
}

std::vector<QUuid> ParamDocument::bridgesPinnedTo(const QUuid& hostBlockId) const
{
    std::vector<QUuid> result;
    for (const auto& a : m_attachments) {
        if (!a.isPin || a.toBlockId != hostBlockId) continue;
        if (std::find(result.begin(), result.end(), a.fromBlockId) == result.end())
            result.push_back(a.fromBlockId);
    }
    return result;
}

std::vector<QUuid> ParamDocument::releaseOrphanedBridges()
{
    std::vector<QUuid> released;
    for (auto& b : m_blocks) {
        if (!b.isBridge) continue;
        int pins = 0;
        for (const auto& a : m_attachments)
            if (a.isPin && a.fromBlockId == b.id) ++pins;
        if (pins >= 2) continue;

        // Lost at least one pin: become an independent segment instead of
        // being deleted (父线段被删后保留为独立线段).
        releaseBridge(b);
        released.push_back(b.id);
    }
    return released;
}

void ParamDocument::releaseBridge(Block& b)
{
    b.isBridge = false;

    // Freeze the current (stretched) world geometry into a self-contained
    // local construction (shared with the duplicate path).
    if (!b.freezeSegmentGeometry()) return;

    // A surviving pin becomes a normal follower attachment. Its construction
    // angle is back-solved from the Resolver formula
    //     rotation = refWorld + angle·π/180 − localDir
    // so the frozen world direction is preserved (角度约束不变, no jump).
    for (auto& a : m_attachments) {
        if (!a.isPin || a.fromBlockId != b.id) continue;
        const Block* leader = blockById(a.toBlockId);
        if (!leader) continue;            // host gone: the pin follows shortly
        a.isPin = false;
        a.toSegmentId = leader->exitSegmentAtPoint(a.toPointId);
        const double refWorld = leader->transform.rotation
            + leader->exitDirectionAtPoint(a.toPointId, a.toSegmentId);
        const double localDir = b.directionAtPoint(a.fromPointId);
        a.followerAngle = backSolveFollowerAngle(
            b.transform.rotation, localDir, refWorld);
        a.followerAngleFormula.clear();
        a.rotationMode = RotationMode::Angle;
        a.arcLength = 0.0;
        a.arcLengthFormula.clear();
    }
}

void ParamDocument::degradeOrphanedIntersections()
{
    for (auto& b : m_blocks) {
        for (auto& pt : b.points) {
            if (pt.constraint != PointConstraint::Intersection) continue;

            bool degraded = false;

            // Check if the target segment still exists in this block.
            const Segment* seg = b.findSegment(pt.hostSegmentId);
            if (!seg) {
                // Segment gone: freeze as Free point at last position.
                degraded = true;
                if (pt.resolved) {
                    pt.constraint = PointConstraint::Free;
                    pt.freePos = pt.resolvedPos;
                } else {
                    pt.constraint = PointConstraint::Free;
                    pt.freePos = geo::Vec2::zero();
                }
            } else {
                // Check if the ray origin point still exists (any block).
                bool originFound = false;
                for (const auto& ob : m_blocks) {
                    if (ob.findPoint(pt.refPointA)) { originFound = true; break; }
                }
                if (!originFound) {
                    // Origin gone: freeze as OnSegment (ratio from last position).
                    degraded = true;
                    const ParamPoint* sp = b.findPoint(seg->startPointId);
                    const ParamPoint* ep = b.findPoint(seg->endPointId);
                    double t = 0.5;
                    if (sp && ep && sp->resolved && ep->resolved && pt.resolved) {
                        geo::Vec2 d = ep->resolvedPos - sp->resolvedPos;
                        double len2 = d.lengthSquared();
                        if (len2 > 1e-12) {
                            t = (pt.resolvedPos - sp->resolvedPos).dot(d) / len2;
                            t = std::clamp(t, 0.0, 1.0);
                        }
                    }
                    pt.constraint = PointConstraint::OnSegment;
                    pt.refPointA = seg->startPointId;
                    pt.refPointB = seg->endPointId;
                    pt.ratio = t;
                }
                // Aim point (指向点) gone: fall back to the stored angle mode —
                // the point stays valid, only the point-aim link is dropped.
                if (!pt.interAimPointId.isNull()) {
                    bool aimFound = false;
                    for (const auto& ob : m_blocks) {
                        if (ob.findPoint(pt.interAimPointId)) { aimFound = true; break; }
                    }
                    if (!aimFound) pt.interAimPointId = QUuid();
                }
            }

            if (degraded) {
                // Clear intersection-specific fields.
                pt.interAngle = 90.0;
                pt.interUseWorldAngle = false;
                pt.interAngleFormula.clear();
                pt.interBidirectional = false;
                pt.hostSegmentId = QUuid();
            }
        }
    }
}

} // namespace cad::param