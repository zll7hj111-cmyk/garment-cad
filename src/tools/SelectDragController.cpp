#include "SelectDragController.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/DomainViews.h"
#include "tools/ConnectGesture.h"   // kConnectSnapRadius (重挂吸附半径)
#include "document/commands/BlockCommands.h"
#include "document/commands/AttachmentCommands.h"

#include <QUndoStack>
#include <cmath>

namespace cad::tools {

void SelectDragController::begin(const cad::geo::Vec2& pos,
                                 const QSet<QUuid>& selection)
{
    m_startPos = pos;
    m_detachedAttachments.clear();

    // 拖动集合严格等于当前选中对象集合（用户拍板 2026-09: 取消全部默认跟随，不通过 lockedClosure 扩充未选对象）
    const QSet<QUuid> dragSet = selection;
    m_blockIds = dragSet.values();
    m_origins.clear();
    for (const QUuid& id : m_blockIds) {
        if (const auto* b = m_paramDoc->blocksView().byId(id))
            m_origins.insert(id, b->transform.origin);
    }

    // 连接关系脆弱化（用户拍板 2026-09）:
    // 当未选中对象和选中对象有连接时，拖动不跟随，而是直接断开。
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromComponentId.isNull()) continue;
        const cad::param::Component* c = m_paramDoc->componentsView().byId(att.fromComponentId);
        if (!c) continue;
        bool memberIn = false;
        for (const QUuid& mid : c->memberBlockIds)
            if (dragSet.contains(mid)) { memberIn = true; break; }
        if (memberIn) {
            m_detachedAttachments.append(att.id);
            continue;
        }
    }
    for (const auto& att : m_paramDoc->attachments()) {
        const bool fromIn = dragSet.contains(att.fromBlockId);
        const bool toIn   = dragSet.contains(att.toBlockId);
        // 只要一端在选集中、另一端不在选集中（无论是母线被拖走还是子线被拖走），直接脆弱化断开
        if ((fromIn && !toIn) || (!fromIn && toIn)) {
            if (!m_detachedAttachments.contains(att.id)) {
                m_detachedAttachments.append(att.id);
            }
            if (auto* childBlk = m_paramDoc->findBlock(att.fromBlockId)) {
                childBlk->preservedBenchmarkAngle = att.followerAngle;
            }
        }
    }

    // 滑轨模式 (抽屉式滑动, 用户拍板 2026-08): slide attachments are kept
    // ACTIVE (never detached — dragging a slide follower stays on the rail).
    // Snapshot their pre-drag rail coordinates so the end macro can undo
    // the slide back to the pre-drag rail position.
    m_oldSlideOffsets.clear();
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.slideMode == cad::param::SlideMode::None) continue;
        if (!dragSet.contains(att.fromBlockId)) continue;
        m_oldSlideOffsets.insert(att.id,
            {att.slideAlongMm, att.slidePerpMm});
    }

    m_stateFn(SelectState::Dragging);
}

void SelectDragController::update(const cad::geo::Vec2& pos)
{
    const cad::geo::Vec2 delta = pos - m_startPos;
    for (const QUuid& id : m_blockIds) {
        if (auto* b = m_paramDoc->findBlock(id))
            b->transform.origin = m_origins.value(id) + delta;
    }
    // Live resolve every frame: followers cascade in real time (same pattern
    // as updateConnect). resolveForDrag() emits resolved() → the scene syncs
    // block positions; panels stay silent until the gesture commits (end
    // pushes an undo command whose resolveAll() refreshes).
    // Seeds = the dragged set; cross-selection attachments pending removal are
    // ignored so a dragged follower is not pulled back to its leader.
    for (const QUuid& id : m_blockIds) {
        if (auto* b = m_paramDoc->findBlock(id))
            m_paramDoc->invalidateLayer(b->layer);
    }
    // 滑轨模式 (抽屉式滑动): 拖动跟随线时每帧把当前 (拖拽) 位置回写到
    // 自由轴坐标 (锁轴保持激活时快照), 随后 resolveForDrag 按新坐标落位,
    // 锁轴分量被弹回 —— 跟随线只沿滑轨动。
    for (const QUuid& attId : m_oldSlideOffsets.keys())
        m_paramDoc->updateSlideOffsetsFromCurrent(attId);
    m_paramDoc->resolveForDrag(m_blockIds, m_detachedAttachments);
}

bool SelectDragController::tryReattachOnDragEnd(const cad::geo::Vec2& pos)
{
    if (!m_paramDoc || !m_undoStack) return false;
    // 只在单一跟随线拖拽释放时做“位置重挂 + 角度基准保留”，避免多选拖动语义复杂化。
    if (m_detachedAttachments.size() != 1 || m_blockIds.size() != 1)
        return false;

    const cad::param::Attachment* att = m_paramDoc->attachmentsView().byId(m_detachedAttachments.first());
    if (!att || att->isPin || !att->fromComponentId.isNull())
        return false;

    // SnapEngine::findSnap 的 zoom 参数用于把半径换算成用户单位 (radiusPx/zoom);
    // 拖动释放点与画布当前缩放绑定, 这里沿用会话上下文记录的 zoom （ToolSelect
    // 注入）。
    const auto snap = m_snapEngine.findSnap(
        pos, m_paramDoc, m_zoom, kConnectSnapRadius, {}, &att->fromBlockId);
    if (!snap || snap->blockId == att->fromBlockId)
        return false;

    const auto* leader = m_paramDoc->blocksView().byId(snap->blockId);
    const QUuid segId = leader ? leader->exitSegmentAtPoint(snap->pointId) : QUuid();
    if (segId.isNull()) return false;

    const auto* curTo = m_paramDoc->blocksView().byId(att->toBlockId);
    if (!curTo || !curTo->isShadow) {
        return false;
    }

    // 影子基准拓扑 (拆开影子基准, DETACH_SHADOW_DESIGN.md §7.4): 被拖拆的
    // 连接以影子为基准 (挂载态跟随线拖离影子) —— 挂回本体 = ⑤ 删影子+活引用;
    // 挂到其他线 = ③ 影子挂载链 (Att1 反算保向 + Att2 重新焊接)。
    const bool toMaster = (snap->blockId == curTo->shadowMasterBlockId);
    m_undoStack->beginMacro(QStringLiteral("重新挂接"));
    if (toMaster) {
        m_undoStack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
            m_paramDoc, att->id, /*angleOnly=*/false, snap->pointId, segId));
    } else {
        m_undoStack->push(new cad::cmd::ShadowMountCommand(
            m_paramDoc, curTo->id, snap->blockId, snap->pointId, segId));
    }
    m_undoStack->endMacro();
    return true;
}

bool SelectDragController::end(const cad::geo::Vec2& pos, double zoom)
{
    const cad::geo::Vec2 delta = pos - m_startPos;

    // Restore originals, then re-apply through the undo stack.
    for (const QUuid& id : m_blockIds) {
        if (auto* b = m_paramDoc->findBlock(id))
            b->transform.origin = m_origins.value(id);
    }

    // A pure click (press + release without moving) is NOT a drag: keep the
    // selection — 按住移动才是拖动, 几乎没动的拖动按单击回退 (2026-09;
    // 双击编辑保持完好).
    if (delta.lengthSquared() <= 1e-10) {
        return true;
    }

    if (m_undoStack && !tryReattachOnDragEnd(pos)) {
        m_undoStack->beginMacro(QStringLiteral(
            "\xe7\xa7\xbb\xe5\x8a\xa8 %1 \xe4\xb8\xaa\xe5\xaf\xb9\xe8\xb1\xa1").arg(m_blockIds.size()));
        for (const QUuid& attId : m_detachedAttachments) {
            m_undoStack->push(new cad::cmd::RemoveAttachmentCommand(
                m_paramDoc, attId));
        }
        // 滑轨模式 (抽屉式滑动): 拖动沿滑轨走了 —— 自由轴坐标 old→new 与
        // 移动一起入栈 (undo 整体回到拖前滑轨位置).
        for (auto it = m_oldSlideOffsets.cbegin();
             it != m_oldSlideOffsets.cend(); ++it) {
            const cad::param::Attachment* att = m_paramDoc->attachmentsView().byId(it.key());
            if (!att) continue;
            const auto [oldAlong, oldPerp] = it.value();
            if (std::abs(att->slideAlongMm - oldAlong) <= 1e-9
                && std::abs(att->slidePerpMm - oldPerp) <= 1e-9) continue;
            m_undoStack->push(new cad::cmd::SetSlideOffsetsCommand(
                m_paramDoc, it.key(), oldAlong, oldPerp,
                att->slideAlongMm, att->slidePerpMm));
        }
        m_undoStack->push(new cad::cmd::MoveBlockCommand(m_paramDoc, m_blockIds, delta));
        m_undoStack->endMacro();
    }
    return false;
}

void SelectDragController::cancelDrag()
{
    for (const QUuid& id : m_blockIds) {
        if (auto* b = m_paramDoc->findBlock(id))
            b->transform.origin = m_origins.value(id);
    }
    m_paramDoc->resolveAll();
}

} // namespace cad::tools
