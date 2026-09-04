#include "document/commands/BlockLifecycleCommands.h"

#include <algorithm>
#include <QSet>

#include "parametric/ParamDocument.h"
#include "parametric/Serial.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── AddBlockCommand ───

AddBlockCommand::AddBlockCommand(cad::param::ParamDocument* doc,
                                 cad::param::Block block,
                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_block(std::move(block))
{
    setText(QStringLiteral("添加线段"));
}

void AddBlockCommand::redo()
{
    m_doc->addBlock(m_block);
}

void AddBlockCommand::undo()
{
    m_doc->removeBlock(m_block.id);
}

// ─── RemoveBlockCommand ───

RemoveBlockCommand::RemoveBlockCommand(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("删除线段"));

    // Save the block for undo.
    if (const auto* b = doc->findBlock(blockId))
        m_block = *b;

    // Cascade set: the block + every bridge pinned to it (the model layer
    // releases those bridges as independent segments when the host goes
    // away — their pre-deletion state is snapshotted for undo) + every SHADOW
    // affected by the deletion (拆开影子基准级联, DETACH_SHADOW_DESIGN.md ⑥⑦):
    //   · 本体被删 → 影子块级联删除 (undo 须还原影子块);
    //   · 跟随线被删 → 影子失去 Att2 级联删除 (undo 须还原影子块);
    //   · 挂载宿主 (L3) 被删 → 影子弹回拆开态 (影子保留, 但其 Att1 被删、
    //     Att2 回 angleOnly —— 两连接都须入快照供 undo verbatim 还原)。
    // 弹回影子不进 cascadeBlocks (块保留), 但其连接必须进快照扫描集 touchSet。
    QSet<QUuid> cascade{blockId};
    QSet<QUuid> shadowTouch;  // 受影响影子 (连接快照扫描集)
    for (const QUuid& bridgeId : doc->attachmentsView().bridgesPinnedTo(blockId)) {
        if (cascade.contains(bridgeId)) continue;
        cascade.insert(bridgeId);
        if (const auto* bridge = doc->findBlock(bridgeId))
            m_bridges.push_back(*bridge);
    }
    for (const auto& b : doc->blocks()) {
        if (!b.isShadow) continue;
        bool masterDied = (b.shadowMasterBlockId == blockId);
        bool att2FromVictim = false, att1ToVictim = false;
        for (const auto& a : doc->attachments()) {
            if (a.isPin) continue;
            if (a.toBlockId == b.id && a.fromBlockId == blockId)
                att2FromVictim = true;
            if (a.fromBlockId == b.id && a.toBlockId == blockId)
                att1ToVictim = true;
        }
        if (masterDied || att2FromVictim) {
            cascade.insert(b.id);       // 影子块随删 (undo 还原)
            shadowTouch.insert(b.id);
            m_shadows.push_back(b);
        } else if (att1ToVictim) {
            shadowTouch.insert(b.id);   // 弹回拆开态 (Att1 删 / Att2 翻旗, 快照还原)
        }
    }

    // Snapshot every attachment touching any block in the cascade set, plus
    // the surviving (弹回) shadow's connections — undo restores Att2's angle
    // flags verbatim via the same snapshot (overwrite-or-insert below).
    QSet<QUuid> seen;
    const QSet<QUuid> touchSet = cascade | shadowTouch;
    for (const auto& att : doc->attachments()) {
        if (!touchSet.contains(att.fromBlockId) && !touchSet.contains(att.toBlockId))
            continue;
        if (seen.contains(att.id)) continue;
        seen.insert(att.id);
        m_attachments.push_back(att);
    }

    // Linked variables sourced from the cascade set are auto-deleted with the
    // block, and their exact-match consumers (length-linked copies) get baked
    // to plain numbers (长度固化为数值). Snapshot both for undo.
    for (const QUuid& srcId : cascade) {
        for (const auto& lv : doc->linkedVars())
            if (lv.sourceBlockId == srcId)
                m_linked.push_back(lv);
        for (const QUuid& cid : doc->linkedConsumerBlocks(srcId)) {
            if (cascade.contains(cid)) continue;   // removed & restored anyway
            const bool taken = std::any_of(
                m_bakedConsumers.begin(), m_bakedConsumers.end(),
                [&cid](const cad::param::Block& b) { return b.id == cid; });
            if (taken) continue;
            if (const auto* cb = doc->findBlock(cid))
                m_bakedConsumers.push_back(*cb);
        }
        // Measure variables referencing the cascade set (as an endpoint OR as
        // their owner bridge line) are auto-deleted by removeBlock(); snapshot
        // for undo restore.
        for (const auto& mv : doc->measureVars())
            if (mv.blockA == srcId || mv.blockB == srcId || mv.ownerBlockId == srcId)
                m_measures.push_back(mv);
    }
}

void RemoveBlockCommand::redo()
{
    m_doc->removeBlock(m_block.id);
}

void RemoveBlockCommand::undo()
{
    // Bridges pinned to the removed block were RELEASED by redo() (converted
    // to independent segments, still present in the document). Remove the
    // released versions first, then restore the pristine snapshots.
    for (const auto& bridge : m_bridges)
        m_doc->removeBlock(bridge.id);
    m_doc->addBlock(m_block);
    for (const auto& bridge : m_bridges)
        m_doc->addBlock(bridge);
    // 影子级联块 (⑥): 与本体一同 verbatim 还原 (addBlock 自带 resolve)。
    for (const auto& shadow : m_shadows)
        if (!m_doc->findBlock(shadow.id))
            m_doc->addBlock(shadow);
    // Verbatim restore: keep each attachment's snapshot isLocked (拖动保护
    // 默认开启只针对新建连接; 快照还原必须保留用户手动解锁的状态). The
    // 弹回影子's Att2 was NOT removed by redo (only flag-flipped) — it is
    // still present, so overwrite it in place instead of inserting a dup.
    for (const auto& att : m_attachments) {
        if (auto* a = m_doc->findAttachment(att.id))
            *a = att;  // in-place verbatim (授权通道: findAttachment 可变重载)
        else
            cad::param::RawModelAccess::addAttachmentRaw(*m_doc, att);
    }
    // Re-publish the auto-deleted linked variables, then restore the pristine
    // formulas of their consumers (baked to numbers by redo).
    for (const auto& lv : m_linked)
        m_doc->addLinked(lv);
    for (const auto& mv : m_measures)
        m_doc->addMeasure(mv);
    for (const auto& snap : m_bakedConsumers) {
        if (auto* b = m_doc->findBlock(snap.id))
            *b = snap;
    }
    // 影子级联还原也需要重解 (Att1/Att2 verbatim 回填后链条重新落位)。
    m_doc->resolveAll();
}

// ─── DuplicateBlocksCommand ───

DuplicateBlocksCommand::DuplicateBlocksCommand(cad::param::ParamDocument* doc,
                                               cad::param::DuplicateResult result,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_result(std::move(result))
{
    setText(QStringLiteral("复制 %1 条线段").arg(m_result.blocks.size()));
}

void DuplicateBlocksCommand::redo()
{
    // Linked variables first: their refName must be in the parameter map
    // before the cloned blocks resolve their length formulas.
    for (const auto& lv : m_result.newLinked)
        m_doc->addLinked(lv);
    for (const auto& b : m_result.blocks)
        m_doc->addBlock(b);
    // Verbatim: cloned connections keep the ORIGINAL's isLocked (复制语义).
    cad::param::RawModelAccess::addAttachmentsRaw(*m_doc, m_result.attachments);
    for (const auto& c : m_result.components)
        m_doc->addComponent(c);
    m_doc->resolveAll();
}

void DuplicateBlocksCommand::undo()
{
    for (const auto& c : m_result.components)
        m_doc->removeComponentRecord(c.id);
    // removeBlock also drops any attachment touching the clone; cloned
    // bridges may get mutated (released) when their first pin goes away,
    // but they are removed right after, so the mutation is irrelevant.
    for (const auto& att : m_result.attachments)
        m_doc->removeAttachment(att.id);
    for (const auto& b : m_result.blocks)
        m_doc->removeBlock(b.id);
    for (const auto& lv : m_result.newLinked)
        m_doc->removeLinked(lv.id);
}

// ─── RotateCopyCommand (旋转复制) ───

RotateCopyCommand::RotateCopyCommand(cad::param::ParamDocument* doc,
                                     cad::param::DuplicateResult result,
                                     const QUuid& originalBlockId,
                                     const QUuid& pivotPointId,
                                     const QUuid& clonePivotPointId,
                                     const QUuid& leaderSegmentId,
                                     double followerAngle,
                                     const QString& followerAngleFormula,
                                     QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_result(std::move(result))
    , m_originalBlockId(originalBlockId)
    , m_pivotPointId(pivotPointId)
    , m_clonePivotPointId(clonePivotPointId)
    , m_leaderSegmentId(leaderSegmentId)
{
    setText(QStringLiteral("旋转复制"));
    // The clone→original attachment is a normal follower whose follower angle
    // is measured RELATIVE to the original's current direction (so a
    // later rotation of the original keeps the copy's relative angle).
    m_att.fromBlockId = m_result.blocks.empty() ? QUuid() : m_result.blocks.front().id;
    m_att.fromPointId = m_clonePivotPointId;
    m_att.toBlockId = m_originalBlockId;
    m_att.toPointId = m_pivotPointId;
    m_att.toSegmentId = m_leaderSegmentId;
    m_att.followerAngle = followerAngle;
    m_att.followerAngleFormula = followerAngleFormula;
    m_att.rotationMode = cad::param::RotationMode::Angle;
}

void RotateCopyCommand::redo()
{
    if (m_result.blocks.empty()) return;
    // Linked variables first (their refName must resolve before the clone).
    for (const auto& lv : m_result.newLinked)
        m_doc->addLinked(lv);
    m_doc->addBlock(m_result.blocks.front());
    m_doc->addAttachment(m_att);
    m_doc->resolveAll();
}

void RotateCopyCommand::undo()
{
    if (m_result.blocks.empty()) return;
    m_doc->removeAttachment(m_att.id);
    m_doc->removeBlock(m_result.blocks.front().id);
    for (const auto& lv : m_result.newLinked)
        m_doc->removeLinked(lv.id);
}

} // namespace cad::cmd
