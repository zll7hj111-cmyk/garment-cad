#include "document/commands/BlockTransformCommands.h"

#include <algorithm>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "geometry/Angle.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── ShadowRotateCommand ───

ShadowRotateCommand::ShadowRotateCommand(cad::param::ParamDocument* doc,
                                         const QUuid& shadowId,
                                         const cad::param::Transform2D& shadowOld,
                                         const cad::param::Transform2D& shadowNew,
                                         const QUuid& followerId,
                                         const cad::param::Transform2D& followerOld,
                                         const cad::param::Transform2D& followerNew,
                                         QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_shadowId(shadowId)
    , m_followerId(followerId)
    , m_shadowOld(shadowOld)
    , m_shadowNew(shadowNew)
    , m_followerOld(followerOld)
    , m_followerNew(followerNew)
{
    setText(QStringLiteral("改写影子角度"));
}

void ShadowRotateCommand::redo()
{
    if (auto* sh = m_doc->findBlock(m_shadowId))
        sh->transform = m_shadowNew;
    if (auto* b = m_doc->findBlock(m_followerId))
        b->transform = m_followerNew;
    m_doc->resolveAll();
}

void ShadowRotateCommand::undo()
{
    if (auto* sh = m_doc->findBlock(m_shadowId))
        sh->transform = m_shadowOld;
    if (auto* b = m_doc->findBlock(m_followerId))
        b->transform = m_followerOld;
    m_doc->resolveAll();
}

// ─── MoveBlockCommand ───

MoveBlockCommand::MoveBlockCommand(cad::param::ParamDocument* doc,
                                   const QList<QUuid>& blockIds,
                                   const cad::geo::Vec2& delta,
                                   QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockIds(blockIds)
    , m_delta(delta)
{
    setText(QStringLiteral("移动"));
}

void MoveBlockCommand::redo()
{
    for (const auto& id : m_blockIds) {
        if (auto* b = m_doc->findBlock(id))
            b->transform.origin = b->transform.origin + m_delta;
    }
    m_doc->resolveAll();
}

void MoveBlockCommand::undo()
{
    for (const auto& id : m_blockIds) {
        if (auto* b = m_doc->findBlock(id))
            b->transform.origin = b->transform.origin - m_delta;
    }
    m_doc->resolveAll();
}

bool MoveBlockCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;
    const auto* cmd = dynamic_cast<const MoveBlockCommand*>(other);
    if (!cmd) return false;  // id collision safety net (P0-2)
    if (cmd->m_blockIds != m_blockIds)
        return false;
    m_delta = m_delta + cmd->m_delta;
    return true;
}

// ─── RotateBlockCommand ───

RotateBlockCommand::RotateBlockCommand(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       const cad::param::Transform2D& oldTf,
                                       const cad::param::Transform2D& newTf,
                                       const QUuid& oldEndTargetBlock,
                                       const QUuid& oldEndTargetPoint,
                                       const QUuid& newEndTargetBlock,
                                       const QUuid& newEndTargetPoint,
                                       const QUuid& releasedAttId,
                                       const cad::param::Attachment& releasedAttBackup,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_oldTf(oldTf)
    , m_newTf(newTf)
    , m_oldEndTargetBlock(oldEndTargetBlock)
    , m_oldEndTargetPoint(oldEndTargetPoint)
    , m_newEndTargetBlock(newEndTargetBlock)
    , m_newEndTargetPoint(newEndTargetPoint)
    , m_releasedAttId(releasedAttId)
    , m_releasedAttBackup(releasedAttBackup)
{
    setText(QStringLiteral("旋转"));
}

void RotateBlockCommand::redo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        b->transform = m_newTf;
        b->endTargetBlockId = m_newEndTargetBlock;
        b->endTargetPointId = m_newEndTargetPoint;
    }
    // 旋转 = 放弃跟随: the pivot was moved off the attachment point, so the
    // rotation detaches the follower link (undo restores it).
    if (!m_releasedAttId.isNull())
        m_doc->removeAttachment(m_releasedAttId);
    m_doc->resolveAll();
}

void RotateBlockCommand::undo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        b->transform = m_oldTf;
        b->endTargetBlockId = m_oldEndTargetBlock;
        b->endTargetPointId = m_oldEndTargetPoint;
    }
    if (!m_releasedAttId.isNull())
        cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_releasedAttBackup);  // verbatim (keep snapshot isLocked)
    m_doc->resolveAll();
}

bool RotateBlockCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id())
        return false;
    const auto* cmd = dynamic_cast<const RotateBlockCommand*>(other);
    if (!cmd) return false;  // id collision safety net (P0-2)
    if (cmd->m_blockId != m_blockId)
        return false;
    // A rotation that ALSO released a follower / changed the endpoint-aim
    // must stay a separate command: the merged snapshot only carries ONE
    // attachment backup + ONE aim pair, so absorbing the second state would
    // silently drop the first release / aim change from undo and redo.
    if (cmd->m_releasedAttId != m_releasedAttId
        || cmd->m_newEndTargetBlock != m_newEndTargetBlock
        || cmd->m_newEndTargetPoint != m_newEndTargetPoint)
        return false;
    m_newTf = cmd->m_newTf;  // keep the oldest m_oldTf
    return true;
}

// ─── RotateBlocksCommand ───

RotateBlocksCommand::RotateBlocksCommand(cad::param::ParamDocument* doc,
                                         std::vector<BlockTransformSnapshot> snapshots,
                                         std::vector<cad::param::Attachment> releasedAttachments,
                                         QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_snapshots(std::move(snapshots))
    , m_releasedAttachments(std::move(releasedAttachments))
{
    setText(QStringLiteral("旋转 %1 条线段").arg(m_snapshots.size()));
}

void RotateBlocksCommand::redo()
{
    for (const auto& s : m_snapshots) {
        if (auto* b = m_doc->findBlock(s.blockId)) {
            b->transform = s.newTf;
            b->endTargetBlockId = s.newEndTargetBlock;
            b->endTargetPointId = s.newEndTargetPoint;
            b->touchGeometry();
        }
    }
    for (const auto& a : m_releasedAttachments) {
        m_doc->removeAttachment(a.id);
    }
    m_doc->resolveAll();
}

void RotateBlocksCommand::undo()
{
    for (const auto& s : m_snapshots) {
        if (auto* b = m_doc->findBlock(s.blockId)) {
            b->transform = s.oldTf;
            b->endTargetBlockId = s.oldEndTargetBlock;
            b->endTargetPointId = s.oldEndTargetPoint;
            b->touchGeometry();
        }
    }
    for (const auto& a : m_releasedAttachments) {
        cad::param::RawModelAccess::addAttachmentRaw(*m_doc, a);
    }
    m_doc->resolveAll();
}

} // namespace cad::cmd
