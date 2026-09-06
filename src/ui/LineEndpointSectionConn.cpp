#include "ui/LineEndpointSection.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/FollowerAngle.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/EndpointCommands.h"

namespace cad::ui {

// ── 连接行 handler 拆分 (文件体量红线, 2026-09-06) ──
// 起点/终点「连接到·拆开」四个 handler + 跟随线查找助手。
// undo 语义: 离散语义操作各自入栈 (与会话回放范式一致), 无 undo 栈
// (测试直改路径) 逐处回退门面直写。

const cad::param::Attachment* findFollowerAttachment(const cad::param::ParamDocument* doc,
                                                    const QUuid& blockId)
{
    if (!doc) return nullptr;
    for (const auto& att : doc->attachments()) {
        if (!att.isPin && att.fromBlockId == blockId)
            return &att;
    }
    return nullptr;
}

void LineEndpointSection::onStartConnectResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;
    if (!m_topIsStart) { refreshEndpointConnRows(); return; }

    const auto* att = findFollowerAttachment(m_paramDoc, m_blockId);
    if (att) {
        auto* mut = m_paramDoc->findAttachment(att->id);
        if (!mut) return;
        const auto* leader = m_paramDoc->findBlock(blockId);
        if (!leader || !leader->findPoint(pointId)) { refreshEndpointConnRows(); return; }
        // 入栈基线 (会话回放范式): 动作前连接态 + 跟随线 transform,
        // 供下方非影子重定向路径 verbatim 重放。
        const cad::param::Attachment oldAtt = *mut;
        const cad::geo::Vec2 oldOrigin = block->transform.origin;
        const double oldRotation = block->transform.rotation;
        bool shadowRouted = false;
        if (const auto* curTo = m_paramDoc->findBlock(mut->toBlockId); curTo && curTo->isShadow) {
            if (auto* stack = m_paramDoc->undoStack()) {
                if (blockId == curTo->shadowMasterBlockId) {
                    // ⑤ 挂回本体: 纯构建器预检 (拒绝 = 静默刷回, 不推空命令),
                    // forceMaster 跳过重连缓存改道 —— 用户显式选了本体落点。
                    cad::param::Attachment restored;
                    if (m_paramDoc->buildShadowReconnect(mut->id, restored, pointId))
                        stack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
                            m_paramDoc, mut->id, /*angleOnly=*/false, pointId,
                            QUuid(), /*forceMaster=*/true));
                } else {
                    // ③ 影子挂载: 预检同门面 buildShadowMount。
                    cad::param::Attachment att1;
                    if (m_paramDoc->buildShadowMount(curTo->id, blockId, pointId, QUuid(), att1))
                        stack->push(new cad::cmd::ShadowMountCommand(
                            m_paramDoc, curTo->id, blockId, pointId, QUuid()));
                }
            } else if (blockId == curTo->shadowMasterBlockId) {
                m_paramDoc->reattachShadowToMaster(mut->id, pointId);
            } else {
                m_paramDoc->mountShadowTo(curTo->id, blockId, pointId);
            }
            shadowRouted = true;
        }
        if (!shadowRouted) {
            cad::param::preserveAngleRefOnReattach(m_paramDoc, *mut);
            mut->angleOnly = false;
            mut->isLocked = true;
            mut->slideMode = cad::param::SlideMode::None;
            mut->toBlockId = blockId;
            mut->toPointId = pointId;
            mut->toSegmentId = leader->exitSegmentAtPoint(pointId);
            if (auto* s = block->findSegment(m_segmentId)) {
                const double refWorld = cad::param::effectiveAngleRefWorld(m_paramDoc, *mut);
                const double localDir = block->directionAtPoint(s->startPointId);
                if (mut->followerAngleFormula.isEmpty()) {
                    mut->followerAngle = cad::param::backSolveFollowerAngle(
                        block->transform.rotation, localDir, refWorld);
                    mut->rotationMode = cad::param::RotationMode::Angle;
                    mut->arcLength = 0.0;
                    mut->arcLengthFormula.clear();
                }
            }
            m_paramDoc->resolveAll();
            if (auto* stack = m_paramDoc->undoStack()) {
                if (auto* newAtt = m_paramDoc->findAttachment(oldAtt.id)) {
                    // 重定向入栈: 恢复动作前连接态, 经命令 verbatim 重放新态
                    // (与连接手势收尾同款, 单步 undo)。
                    const cad::param::Attachment newSnap = *newAtt;
                    m_paramDoc->restoreFollowerAttachment(m_blockId, oldAtt);
                    stack->push(new cad::cmd::ReconnectAttachmentCommand(
                        m_paramDoc, oldAtt.id, newSnap, oldAtt,
                        oldOrigin, oldRotation));
                }
            }
        }
    } else {
        const auto* leader = m_paramDoc->findBlock(blockId);
        if (!leader || !leader->findPoint(pointId)) { refreshEndpointConnRows(); return; }
        cad::param::Attachment attNew;
        attNew.fromBlockId = m_blockId;
        attNew.fromPointId = seg->startPointId;
        attNew.toBlockId = blockId;
        attNew.toPointId = pointId;
        attNew.toSegmentId = leader->exitSegmentAtPoint(pointId);
        const double refWorld = leader->transform.rotation + leader->exitDirectionAtPoint(pointId, attNew.toSegmentId);
        const double localDir = block->directionAtPoint(seg->startPointId);
        attNew.followerAngle = cad::param::backSolveFollowerAngle(block->transform.rotation, localDir, refWorld);
        if (m_paramDoc->addAttachment(attNew)) {
            // 建立连接入栈: 门面已做校验 + 默认焊接, 摘除刚插入的终态后
            // 经 AddAttachmentCommand verbatim 重放 (单步 undo)。
            if (auto* stack = m_paramDoc->undoStack()) {
                if (const auto* ins = m_paramDoc->findAttachment(attNew.id)) {
                    const cad::param::Attachment inserted = *ins;
                    m_paramDoc->removeAttachment(attNew.id);
                    stack->push(new cad::cmd::AddAttachmentCommand(m_paramDoc, inserted));
                }
            }
        }
    }
    refreshEndpointConnRows();
    emit connectionChanged();
}

void LineEndpointSection::onStartDetachClicked()
{
    if (!m_paramDoc) return;
    const auto* att = findFollowerAttachment(m_paramDoc, m_blockId);
    if (!att || !m_topIsStart) { refreshEndpointConnRows(); return; }
    const auto* leaderBlk = m_paramDoc->findBlock(att->toBlockId);
    const auto* leaderPt = leaderBlk ? leaderBlk->findPoint(att->toPointId) : nullptr;
    const bool isAuxMount = (leaderPt && leaderPt->isAuxiliary);
    if (isAuxMount) {
        if (auto* stack = m_paramDoc->undoStack())
            stack->push(new cad::cmd::RemoveAttachmentCommand(m_paramDoc, att->id));
        else
            m_paramDoc->removeAttachment(att->id);
    } else {
        if (auto* stack = m_paramDoc->undoStack())
            stack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(m_paramDoc, att->id, !att->angleOnly));
        else
            m_paramDoc->setAttachmentAngleOnly(att->id, !att->angleOnly);
    }
    refreshEndpointConnRows();
    emit connectionChanged();
}

void LineEndpointSection::onEndConnectResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block || m_topIsStart) { refreshEndpointConnRows(); return; }
    const auto* targetBlock = m_paramDoc->findBlock(blockId);
    if (!targetBlock || !targetBlock->findPoint(pointId)) { refreshEndpointConnRows(); return; }
    if (blockId == m_blockId) { refreshEndpointConnRows(); return; }

    // 终点指向入栈 (offset/公式原样保留 —— 与旧直写行为一致, 仅补 undo)。
    if (auto* stack = m_paramDoc->undoStack()) {
        stack->push(new cad::cmd::SetEndTargetCommand(
            m_paramDoc, m_blockId, blockId, pointId,
            block->endTargetOffset, block->endTargetOffsetFormula));
    } else {
        block->endTargetBlockId = blockId;
        block->endTargetPointId = pointId;
        m_paramDoc->resolveAll();
    }
    refreshEndpointConnRows();
    emit connectionChanged();
}

void LineEndpointSection::onEndDetachClicked()
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    // 拆开入栈: 只清指向, offset/公式不动 (与旧直写行为一致, 重连隐式还原)。
    if (auto* stack = m_paramDoc->undoStack()) {
        stack->push(new cad::cmd::SetEndTargetCommand(
            m_paramDoc, m_blockId, QUuid(), QUuid(),
            block->endTargetOffset, block->endTargetOffsetFormula));
    } else {
        block->endTargetBlockId = QUuid();
        block->endTargetPointId = QUuid();
        m_paramDoc->resolveAll();
    }
    refreshEndpointConnRows();
    emit connectionChanged();
}

} // namespace cad::ui
