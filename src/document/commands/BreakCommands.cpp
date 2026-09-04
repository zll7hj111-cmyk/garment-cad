#include "document/commands/BreakCommands.h"

#include <algorithm>

#include "document/commands/BreakAnalysis.h"
#include "document/commands/BreakExecution.h"
#include "parametric/ParamDocument.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

BreakSegmentCommand::BreakSegmentCommand(cad::param::ParamDocument* doc,
                                         const QUuid& blockId,
                                         const QUuid& segmentId,
                                         const QUuid& auxPointId,
                                         QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_auxPointId(auxPointId)
{
    setText(QStringLiteral("打断线段"));

    // Validate preconditions at construction time.
    const auto* block = doc->findBlock(blockId);
    const auto* seg = block ? block->findSegment(segmentId) : nullptr;
    const auto* pt = block ? block->findPoint(auxPointId) : nullptr;
    if (block && seg && pt)
        m_valid = canBreak(*block, *seg, *pt);
}

void BreakSegmentCommand::redo()
{
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    auto* seg = block->findSegment(m_segmentId);
    auto* auxPt = block->findPoint(m_auxPointId);
    if (!seg || !auxPt) return;
    if (!canBreak(*block, *seg, *auxPt)) return;

    // Fresh state for every redo pass (undo removes the auto-published var).
    m_publishedLinkedId = QUuid();
    m_publishedRefName.clear();

    // --- Snapshot for undo ---
    m_origBlockSnapshot = *block;

    // Save attachments that will be removed (those targeting the aux point
    // or the original end point on this block).
    m_removedAttachments.clear();
    const QUuid origEndId = seg->endPointId;
    for (const auto& att : m_doc->attachments()) {
        if (att.toBlockId == m_blockId
            && (att.toPointId == m_auxPointId || att.toPointId == origEndId)) {
            m_removedAttachments.push_back(att);
        }
    }

    // --- 六阶段流水线（每阶段一个命名函数，中间状态经 BreakState 传递） ---
    // 1) 几何解析：段长/方向；曲线切线冻结与 pass 点分配
    // 2) 位置求值：按 Formula/Freeze/RefChain 模式生成前后段公式与距离
    // 3) 辅助点分派：按打断位置决定去前段还是后段
    // 4) 前段改造：断点转 Polar 端点、段属性、长度发布、曲线 pass 点/切线
    // 5) 后段构建：断点→新起点、Polar 终点、曲线中点插入保持形状
    // 6) 收尾：移除旧附件、入块、建连接（addBlock/addAttachment 触发求解）
    BreakState st;
    if (!gatherBreakGeometry(*m_doc, m_blockId, m_segmentId, m_auxPointId, st))
        return;
    evaluateBreakPosition(*m_doc, m_blockId, m_segmentId, m_auxPointId, st);
    redistributeAuxPoints(*m_doc, m_blockId, m_segmentId, m_auxPointId, st);
    modifyFrontBlock(*m_doc, m_blockId, m_segmentId, m_auxPointId, st, origEndId,
                     m_publishedLinkedId, m_publishedRefName);
    cad::param::Block backBlock = buildBackBlock(
        *m_doc, m_blockId, m_segmentId, m_auxPointId, st, origEndId,
        m_publishedRefName);
    m_newBlockId = backBlock.id;
    finalizeBreak(*m_doc, st, m_blockId, m_segmentId, m_auxPointId,
                  std::move(backBlock), m_removedAttachments);
}

void BreakSegmentCommand::undo()
{
    // Remove the back block (also removes its attachments — including the
    // re-pointed copies that now target the back block's end point).
    m_doc->removeBlock(m_newBlockId);

    // Restore the original block from snapshot.
    if (auto* block = m_doc->findBlock(m_blockId))
        *block = m_origBlockSnapshot;

    // Restore attachments verbatim: drop the live (compensated / re-pointed)
    // copies first, then re-add the pre-break originals (keeps isLocked).
    QList<QUuid> liveIds;
    for (const auto& att : m_doc->attachments()) {
        const bool snapped = std::any_of(
            m_removedAttachments.begin(), m_removedAttachments.end(),
            [&att](const cad::param::Attachment& o) { return o.id == att.id; });
        if (snapped) liveIds.push_back(att.id);
    }
    m_doc->removeAttachments(liveIds);
    cad::param::RawModelAccess::addAttachmentsRaw(*m_doc, m_removedAttachments);

    // Remove the auto-published front-length variable (a variable the user
    // published themselves stays untouched).
    if (!m_publishedLinkedId.isNull())
        m_doc->removeLinked(m_publishedLinkedId);

    m_doc->resolveAll();
}

} // namespace cad::cmd
