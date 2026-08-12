#include "AttachmentCommands.h"

#include <algorithm>

#include "parametric/ParamDocument.h"

namespace cad::cmd {

// ─── AddAttachmentCommand ───

AddAttachmentCommand::AddAttachmentCommand(cad::param::ParamDocument* doc,
                                           cad::param::Attachment att,
                                           QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_att(std::move(att))
{
    setText(QStringLiteral("添加连接"));
}

void AddAttachmentCommand::redo()
{
    m_doc->addAttachment(m_att);
}

void AddAttachmentCommand::undo()
{
    m_doc->removeAttachment(m_att.id);
}

// ─── RemoveAttachmentCommand ───

RemoveAttachmentCommand::RemoveAttachmentCommand(cad::param::ParamDocument* doc,
                                                 const QUuid& attId,
                                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("断开连接"));

    for (const auto& a : doc->attachments()) {
        if (a.id == attId) { m_att = a; break; }
    }

    // Removing a bridge pin releases the bridge (the model layer converts it
    // to an independent segment) — snapshot its pristine state and every
    // attachment touching it so undo can restore the full bridge.
    if (m_att.isPin) {
        if (const auto* b = doc->findBlock(m_att.fromBlockId);
            b && b->isBridge) {
            m_bridge = *b;
            m_hasBridge = true;
            for (const auto& a : doc->attachments()) {
                if (a.fromBlockId == b->id || a.toBlockId == b->id)
                    m_bridgeAtts.push_back(a);
            }
        }
    }
}

void RemoveAttachmentCommand::redo()
{
    m_doc->removeAttachment(m_att.id);
}

void RemoveAttachmentCommand::undo()
{
    if (m_hasBridge) {
        // The bridge was released by redo() — replace the converted version
        // with the pristine snapshot, then restore all of its attachments
        // (both pins + any follower led by its auxiliary points).
        m_doc->removeBlock(m_bridge.id);
        m_doc->addBlock(m_bridge);
        for (const auto& a : m_bridgeAtts)
            m_doc->addAttachmentRaw(a);  // verbatim (keep snapshot isLocked)
        m_doc->resolveAll();
        return;
    }
    m_doc->addAttachmentRaw(m_att);  // verbatim (keep snapshot isLocked)
    m_doc->resolveAll();
}

// ─── SetFollowerAngleCommand ───

SetFollowerAngleCommand::SetFollowerAngleCommand(cad::param::ParamDocument* doc,
                                             const QUuid& attId, double newAngle,
                                             const QString& newFormula,
                                             cad::param::RotationMode newMode,
                                             double newArcLength,
                                             const QString& newArcFormula,
                                             QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newAngle(newAngle)
    , m_oldAngle(0.0)
    , m_newFormula(newFormula)
    , m_newMode(newMode)
    , m_oldMode(cad::param::RotationMode::Angle)
    , m_newArcLength(newArcLength)
    , m_oldArcLength(0.0)
    , m_newArcFormula(newArcFormula)
{
    setText(QStringLiteral("\xe4\xbf\xae\xe6\x94\xb9\xe8\xb7\x9f\xe9\x9a\x8f\xe8\xa7\x92\xe5\xba\xa6"));  // 修改跟随角度

    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldAngle = a.followerAngle;
            m_oldFormula = a.followerAngleFormula;
            m_oldMode = a.rotationMode;
            m_oldArcLength = a.arcLength;
            m_oldArcFormula = a.arcLengthFormula;
            break;
        }
    }
}

void SetFollowerAngleCommand::redo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->followerAngle = m_newAngle;
        a->followerAngleFormula = m_newFormula;
        a->rotationMode = m_newMode;
        a->arcLength = m_newArcLength;
        a->arcLengthFormula = m_newArcFormula;
    }
    m_doc->resolveAll();
}

void SetFollowerAngleCommand::undo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->followerAngle = m_oldAngle;
        a->followerAngleFormula = m_oldFormula;
        a->rotationMode = m_oldMode;
        a->arcLength = m_oldArcLength;
        a->arcLengthFormula = m_oldArcFormula;
    }
    m_doc->resolveAll();
}

bool SetFollowerAngleCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) return false;
    const auto* cmd = static_cast<const SetFollowerAngleCommand*>(other);
    if (cmd->m_attId != m_attId) return false;
    // Only plain numeric drags merge; formula changes are discrete edits.
    if (!m_newFormula.isEmpty() || !cmd->m_newFormula.isEmpty()) return false;
    if (!m_newArcFormula.isEmpty() || !cmd->m_newArcFormula.isEmpty()) return false;
    m_newAngle = cmd->m_newAngle;
    m_newMode = cmd->m_newMode;
    m_newArcLength = cmd->m_newArcLength;
    return true;
}

} // namespace cad::cmd
