#include "ui/SegmentConnectionCard.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "parametric/FollowerAngle.h"
#include "canvas/CanvasScene.h"
#include "ui/PointRefEdit.h"
#include "ui/LayerFeedback.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockCommands.h"

namespace cad::ui {

// ── 连接生命周期：建立 / 重定向 / 拆开↔重连 (位置维度) / ──
// 2026-xx: 「连接线段」复选框与「清除」按钮已删 —— 连接 = 输入 P#,
// 断开/恢复 = 连接点「拆开/重连」(位置维度) + 基准点「拆开/重连」(角度维度,
// SegmentRefCard), 两维独立。滑轨已上移 LinePropertyDialog。
// 2026-08: 「连接线段」框改用 PointRefEdit 统一解析 (同名弹窗消歧/红闪反馈),
// 旧匿名 resolveSegmentInput (静默取首个命中) 已删。

void SegmentConnectionCard::onConnPointResolved(const QUuid& blockId,
                                                const QUuid& pointId)
{
    // 「连接点」统一入口: 已有连接 → 重定向; 自由线 → 建立连接。
    if (findFollowerAttachment())
        onTargetResolved(blockId, pointId);
    else
        onConnectToResolved(blockId, pointId);
}

void SegmentConnectionCard::onTargetResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_doc) return;

    // Locate the mutable follower attachment.
    cad::param::Attachment* att = nullptr;
    for (const auto& a : m_doc->attachments()) {
        if (!a.isPin && a.fromBlockId == m_blockId) { att = m_doc->findAttachment(a.id); break; }
    }
    if (!att) return;

    // 影子基准拓扑 (拆开影子基准, DETACH_SHADOW_DESIGN.md §7.4): 面板重定向
    // 目标线带影子基准 —— 挂回本体 = ⑤ 删影子 + 活引用恢复 (门面影子感知
    // 路由); 挂到其他线 = ③ 影子挂载链 (Att1 反算保向 + Att2 重新焊接)。
    // 校验由门面内部把关 (拒绝 = refresh 还原显示, 与既有拒绝路径一致)。
    if (const auto* curTo = m_doc->findBlock(att->toBlockId);
        curTo && curTo->isShadow) {
        if (blockId == curTo->shadowMasterBlockId)
            m_doc->reattachShadowToMaster(att->id);  // ⑤ 挂回本体 (不依赖 angleOnly 旗标)
        else
            m_doc->mountShadowTo(curTo->id, blockId, pointId, QUuid());  // ③ 影子挂载
        refresh();
        emit changed();
        return;
    }

    // Validate: would the re-targeted attachment create a cycle?
    cad::param::Attachment candidate = *att;
    candidate.toBlockId = blockId;
    candidate.toPointId = pointId;
    std::vector<cad::param::Attachment> others;
    for (const auto& a : m_doc->attachments())
        if (a.id != att->id) others.push_back(a);
    if (cad::param::checkAttachment(others, candidate)
            != cad::param::AttachmentIssue::Ok) {
        refreshCard();  // Revert the widget display.
        return;
    }

    const auto* leader = m_doc->findBlock(blockId);
    auto* block = m_doc->findBlock(m_blockId);
    if (!leader || !block) { refreshCard(); return; }

    // 重连保持角度基准 (用户拍板 2026-09): 自动态下把旧所连线段固化为两点
    // 基准 (点1 = 旧目标点, 点2 = 旧线段另一端) —— 方向基准不随新宿主漂移
    // (此前只固化点1, 点2 留空, 两点连线方向退化为单点出口方向)。已自定义
    // 的基准原样保留。必须在改写 toBlockId 之前调用 (旧宿主信息仍在 att 上)。
    cad::param::preserveAngleRefOnReattach(m_doc, *att);

    // 仅角度拖动到新端点：旧角度基准保留为独立角度基准，位置挂到新端点，
    // 从而自动进入“双基准”。
    if (att->angleOnly) {
        att->angleOnly = false;
        att->slideMode = cad::param::SlideMode::None;
        att->isLocked = true;
    }

    // Re-target.
    att->toBlockId = blockId;
    att->toPointId = pointId;
    att->toSegmentId = leader->exitSegmentAtPoint(pointId);

    // Back-solve the follower angle so the CURRENT world direction is
    // preserved (no visual jump on re-attach). 基准方向 = 有效角度基准
    // (与 Resolver 同构): 重连保持基准后 = 旧基准两点连线方向, 自动态 =
    // 新宿主出口方向 —— 此前恒用新宿主方向反算, 固化基准后 Resolver 按
    // 旧基准驱动 → 重连瞬间跳线。
    if (auto* seg = block->findSegment(m_segmentId)) {
        const double refWorld = cad::param::effectiveAngleRefWorld(m_doc, *att);
        const double localDir = block->directionAtPoint(seg->startPointId);
        att->followerAngle = cad::param::backSolveFollowerAngle(
            block->transform.rotation, localDir, refWorld);
    }
    att->followerAngleFormula.clear();
    att->rotationMode = cad::param::RotationMode::Angle;
    att->arcLength = 0.0;
    att->arcLengthFormula.clear();

    // 滑轨模式重定向后: 锁轴坐标必须在**新**基准线局部系下重快照 (否则
    // 锁定的垂直/沿线偏移会沿用旧基准的坐标, 重定向瞬间跟随线会跳).
    if (att->slideMode != cad::param::SlideMode::None)
        m_doc->refreshSlideOffsets(att->id);

    refresh();
    emit changed();
}

void SegmentConnectionCard::onConnectToResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    const cad::param::Block* leader = m_doc->findBlock(blockId);
    if (!leader || !leader->findPoint(pointId)) { refreshCard(); return; }

    // Build the new attachment (same back-solve as the old checkbox path).
    cad::param::Attachment att;
    att.fromBlockId = m_blockId;
    att.fromPointId = seg->startPointId;
    att.toBlockId   = blockId;
    att.toPointId   = pointId;
    att.toSegmentId = leader->exitSegmentAtPoint(pointId);

    const double refWorld = leader->transform.rotation
        + leader->exitDirectionAtPoint(pointId, att.toSegmentId);
    const double localDir = block->directionAtPoint(seg->startPointId);
    att.followerAngle = cad::param::backSolveFollowerAngle(
        block->transform.rotation, localDir, refWorld);

    // May be rejected (cycle / conflicting follower).
    const bool added = m_doc->addAttachment(att);
    if (added && m_scene) {
        // Toast only for genuinely NEW cross-layer connections.
        if (const QString toast = crossLayerToast(m_doc, *block, *leader);
            !toast.isEmpty())
            m_scene->showToast(toast);
    }
    // 引用预填自动落库已移至 SegmentRefCard::refresh (2026-12 面板重设计)。

    refresh();
    emit changed();
}

// 「连接线段」框 (PointRefEdit) 解析成功: L#/名称 → 线段起点, 或 P# → 该点。
// 已有连接 = 重定向; 自由线 = 预填到「连接点」框 (回车才建立, 零意外突变)。
void SegmentConnectionCard::onLeaderSegResolved(const QUuid& blockId,
                                                const QUuid& pointId)
{
    if (!m_doc) return;
    if (const auto* att = findFollowerAttachment()) {
        onTargetResolved(blockId, pointId);
    } else {
        m_refConnPoint->setPoint(blockId, pointId);
        refreshCard();
    }
}

// ── 终点连接行 actions (2026-xx 每端完整连接) ────────────────────────────────
// 引擎载体 = Block::endTarget 终点指向 (Resolver Step 7)。建立/重定向统一走
// ConnectEndCommand (长度模式「自动」时发布测量 M_xxx 驱动长度); 拆开/重连/
// 偏移编辑走 SetEndTargetCommand。双端连接 = 桥接线 (角度/基准线互斥)。

void SegmentConnectionCard::onEndConnPointResolved(const QUuid& blockId,
                                                   const QUuid& pointId)
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;

    // 自连 / 目标缺失 / 跨层守卫 → 拒绝并回显。
    const auto* targetBlk = m_doc->findBlock(blockId);
    const bool bad = blockId == m_blockId || !targetBlk
        || !targetBlk->findPoint(pointId);
    if (!bad && m_doc->isAuxBlock(*targetBlk) != m_doc->isAuxBlock(*block)) {
        refreshEndRow(block);
        return;  // 跨辅助层/工作层指向拒绝 (与旧 SegmentRefCard 同规)
    }
    if (bad) { refreshEndRow(block); return; }

    // 长度模式「自动」= 桥接落点 (发布测量驱动长度); 「指定」= 仅指向。
    const bool bridgeLand = block->lengthAuto;
    if (auto* stack = m_doc->undoStack()) {
        stack->push(new cad::cmd::ConnectEndCommand(
            m_doc, m_blockId, m_segmentId, blockId, pointId,
            block->endTargetPointId.isNull() ? 0.0 : block->endTargetOffset,
            bridgeLand));
    } else {
        block->endTargetBlockId = blockId;
        block->endTargetPointId = pointId;
        block->endTargetOffset = 0.0;
        block->endTargetOffsetFormula.clear();
        m_doc->resolveAll();
    }
    refresh();
    emit changed();
}

// 终点行「连接线段」框 (PointRefEdit) 解析成功 → 统一走终点连接点入口。
void SegmentConnectionCard::onEndLeaderSegResolved(const QUuid& blockId,
                                                   const QUuid& pointId)
{
    if (!m_doc) return;
    // 统一走连接点入口 (已有指向 = 重定向; 自由 = 建立)。
    onEndConnPointResolved(blockId, pointId);
}

void SegmentConnectionCard::onEndOffsetEdited()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block || block->endTargetPointId.isNull()) { refreshEndRow(block); return; }

    const QString raw = m_editEndOffset->text().trimmed();
    bool isNum = false;
    const double val = raw.toDouble(&isNum);
    if (!isNum && !raw.isEmpty()) { refreshEndRow(block); return; }  // 仅数值
    const double offset = isNum ? val : 0.0;
    if (std::abs(offset - block->endTargetOffset) < 1e-9) { refreshEndRow(block); return; }

    if (auto* stack = m_doc->undoStack()) {
        stack->push(new cad::cmd::SetEndTargetCommand(
            m_doc, m_blockId, block->endTargetBlockId, block->endTargetPointId,
            offset));
    } else {
        block->endTargetOffset = offset;
        block->endTargetOffsetFormula.clear();
        m_doc->resolveAll();
    }
    refresh();
    emit changed();
}

void SegmentConnectionCard::onEndDetachClicked()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;

    const bool hasEnd = !block->endTargetPointId.isNull();
    if (hasEnd) {
        // 拆开: 记忆目标 → 清指向 (测量保留, 长度公式不动)。
        m_endMemBlock = block->endTargetBlockId;
        m_endMemPoint = block->endTargetPointId;
        m_endMemOffset = block->endTargetOffset;
        if (auto* stack = m_doc->undoStack()) {
            stack->push(new cad::cmd::SetEndTargetCommand(
                m_doc, m_blockId, QUuid(), QUuid(), 0.0));
        } else {
            block->endTargetBlockId = QUuid();
            block->endTargetPointId = QUuid();
            block->endTargetOffset = 0.0;
            m_doc->resolveAll();
        }
    } else if (!m_endMemBlock.isNull() && m_doc->findBlock(m_endMemBlock)) {
        // 重连: 恢复到记忆目标。
        if (auto* stack = m_doc->undoStack()) {
            stack->push(new cad::cmd::SetEndTargetCommand(
                m_doc, m_blockId, m_endMemBlock, m_endMemPoint, m_endMemOffset));
        } else {
            block->endTargetBlockId = m_endMemBlock;
            block->endTargetPointId = m_endMemPoint;
            block->endTargetOffset = m_endMemOffset;
            m_doc->resolveAll();
        }
    }
    refresh();
    emit changed();
}

// 拆开/重连 双面按钮 (位置维度, 2026-xx 用户拍板两维独立):
//   · 拆开 → SetAttachmentAngleOnlyCommand(true): 位置自由、角度仍跟随基准线
//     (与 D 键快拆/拖拆同语义, 一次 undo)。不碰角度维度 (angleIndependent)。
//   · 重连 → SetAttachmentAngleOnlyCommand(false): 位置重新吸附回原宿主点并
//     重新焊接 (isLocked=true), 角度基准保留。
// 无连接禁用 (refreshUnifiedState 已置灰)。
void SegmentConnectionCard::onDetachClicked()
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment();
    if (!att) { refresh(); return; }
    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
            m_doc, att->id, /*angleOnly=*/!att->angleOnly));
    else
        m_doc->setAttachmentAngleOnly(att->id, !att->angleOnly);
    refresh();
    emit changed();
}

} // namespace cad::ui
