#include "AttachmentAngleCommands.h"

#include "parametric/ParamDocument.h"
#include "parametric/FollowerAngle.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── SetAttachmentAngleIndependentCommand ───

SetAttachmentAngleIndependentCommand::SetAttachmentAngleIndependentCommand(
    cad::param::ParamDocument* doc, const QUuid& attId, bool angleIndependent,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newIndependent(angleIndependent)
    , m_oldIndependent(false)
{
    setText(QStringLiteral("角度独立"));

    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldIndependent = a.angleIndependent;
            m_oldAngleOnly = a.angleOnly;
            m_oldSlideMode = a.slideMode;
            m_oldLocked = a.isLocked;
            m_oldFollowerAngle = a.followerAngle;
            m_oldFollowerFormula = a.followerAngleFormula;
            m_oldRotationMode = a.rotationMode;
            m_oldArcLength = a.arcLength;
            m_oldArcFormula = a.arcLengthFormula;
            break;
        }
    }
}

void SetAttachmentAngleIndependentCommand::redo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;

    a->angleIndependent = m_newIndependent;
    if (m_newIndependent) {
        a->slideMode = cad::param::SlideMode::None;
    } else {
        const auto* from = m_doc->findBlock(a->fromBlockId);
        const auto* to = m_doc->findBlock(a->toBlockId);
        if (from && to) {
            const double refWorld = cad::param::effectiveAngleRefWorld(m_doc, *a);
            const double localDir = from->directionAtPoint(a->fromPointId);
            a->followerAngle = cad::param::backSolveFollowerAngle(
                from->transform.rotation, localDir, refWorld);
            a->followerAngleFormula.clear();
            a->rotationMode = cad::param::RotationMode::Angle;
            a->arcLength = 0.0;
            a->arcLengthFormula.clear();
        }
        a->slideMode = cad::param::SlideMode::None;
    }
    m_doc->resolveAll();
}

void SetAttachmentAngleIndependentCommand::undo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;
    a->angleIndependent = m_oldIndependent;
    a->angleOnly = m_oldAngleOnly;
    a->slideMode = m_oldSlideMode;
    a->isLocked = m_oldLocked;
    a->followerAngle = m_oldFollowerAngle;
    a->followerAngleFormula = m_oldFollowerFormula;
    a->rotationMode = m_oldRotationMode;
    a->arcLength = m_oldArcLength;
    a->arcLengthFormula = m_oldArcFormula;
    m_doc->resolveAll();
}

// ─── SetAttachmentAngleRefCommand ───

SetAttachmentAngleRefCommand::SetAttachmentAngleRefCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    const QUuid& newRefBlockId, const QUuid& newRefSegmentId,
    const QUuid& newRefPointId,
    const QUuid& newRef2BlockId, const QUuid& newRef2PointId,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newRefBlockId(newRefBlockId)
    , m_newRefSegmentId(newRefSegmentId)
    , m_newRefPointId(newRefPointId)
    , m_newRef2BlockId(newRef2BlockId)
    , m_newRef2PointId(newRef2PointId)
{
    setText(QStringLiteral("修改角度基准"));

    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldRefBlockId = a.angleRefBlockId;
            m_oldRefSegmentId = a.angleRefSegmentId;
            m_oldRefPointId = a.angleRefPointId;
            m_oldRef2BlockId = a.angleRef2BlockId;
            m_oldRef2PointId = a.angleRef2PointId;
            m_oldAngleIndependent = a.angleIndependent;
            m_oldAngleOnly = a.angleOnly;
            m_oldSlideMode = a.slideMode;
            m_oldLocked = a.isLocked;
            m_oldFollowerAngle = a.followerAngle;
            m_oldFollowerFormula = a.followerAngleFormula;
            m_oldRotationMode = a.rotationMode;
            m_oldArcLength = a.arcLength;
            m_oldArcFormula = a.arcLengthFormula;
            break;
        }
    }
}

void SetAttachmentAngleRefCommand::redo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;

    a->angleRefBlockId = m_newRefBlockId;
    a->angleRefSegmentId = m_newRefSegmentId;
    a->angleRefPointId = m_newRefPointId;
    a->angleRef2BlockId = m_newRef2BlockId;
    a->angleRef2PointId = m_newRef2PointId;
    a->angleIndependent = false;

    const auto* from = m_doc->findBlock(a->fromBlockId);
    const auto* to = m_doc->findBlock(a->toBlockId);
    if (from && to) {
        const double refWorld = cad::param::effectiveAngleRefWorld(m_doc, *a);
        const double localDir = from->directionAtPoint(a->fromPointId);
        a->followerAngle = cad::param::backSolveFollowerAngle(
            from->transform.rotation, localDir, refWorld);
    }
    a->followerAngleFormula.clear();
    a->rotationMode = cad::param::RotationMode::Angle;
    a->arcLength = 0.0;
    a->arcLengthFormula.clear();
    m_doc->resolveAll();
}

void SetAttachmentAngleRefCommand::undo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;
    a->angleRefBlockId = m_oldRefBlockId;
    a->angleRefSegmentId = m_oldRefSegmentId;
    a->angleRefPointId = m_oldRefPointId;
    a->angleRef2BlockId = m_oldRef2BlockId;
    a->angleRef2PointId = m_oldRef2PointId;
    a->angleIndependent = m_oldAngleIndependent;
    a->angleOnly = m_oldAngleOnly;
    a->slideMode = m_oldSlideMode;
    a->isLocked = m_oldLocked;
    a->followerAngle = m_oldFollowerAngle;
    a->followerAngleFormula = m_oldFollowerFormula;
    a->rotationMode = m_oldRotationMode;
    a->arcLength = m_oldArcLength;
    a->arcLengthFormula = m_oldArcFormula;
    m_doc->resolveAll();
}

// ─── ReattachAttachmentCommand ───

ReattachAttachmentCommand::ReattachAttachmentCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    const QUuid& newToBlockId, const QUuid& newToPointId,
    const QUuid& newToSegmentId, QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newToBlockId(newToBlockId)
    , m_newToPointId(newToPointId)
    , m_newToSegmentId(newToSegmentId)
{
    setText(QStringLiteral("重新挂接"));
    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldAtt = a;
            m_hasOldAtt = true;
            if (const auto* b = doc->findBlock(a.fromBlockId)) {
                m_oldOrigin = b->transform.origin;
                m_oldRotation = b->transform.rotation;
            }
            break;
        }
    }
}

void ReattachAttachmentCommand::redo()
{
    if (!m_hasOldAtt) return;
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;

    cad::param::Attachment newAtt = *a;
    cad::param::preserveAngleRefOnReattach(m_doc, newAtt);
    newAtt.toBlockId   = m_newToBlockId;
    newAtt.toPointId   = m_newToPointId;
    newAtt.toSegmentId = m_newToSegmentId;
    newAtt.angleOnly = false;
    newAtt.slideMode = cad::param::SlideMode::None;
    if (m_oldAtt.angleOnly)
        newAtt.isLocked = true;

    const auto* from = m_doc->findBlock(newAtt.fromBlockId);
    if (from && m_doc->findBlock(newAtt.toBlockId)) {
        const double refWorld = cad::param::effectiveAngleRefWorld(m_doc, newAtt);
        const double localDir = from->directionAtPoint(newAtt.fromPointId);
        newAtt.followerAngle = cad::param::backSolveFollowerAngle(
            from->transform.rotation, localDir, refWorld);
    }
    newAtt.followerAngleFormula.clear();
    newAtt.rotationMode = cad::param::RotationMode::Angle;
    newAtt.arcLength = 0.0;
    newAtt.arcLengthFormula.clear();

    m_doc->removeAttachment(m_attId);
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, newAtt);
    m_doc->resolveAll();
}

void ReattachAttachmentCommand::undo()
{
    if (!m_hasOldAtt) return;
    if (auto* a = m_doc->findAttachment(m_attId))
        m_doc->removeAttachment(m_attId);
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_oldAtt);
    if (auto* b = m_doc->findBlock(m_oldAtt.fromBlockId)) {
        b->transform.origin = m_oldOrigin;
        b->transform.rotation = m_oldRotation;
    }
    m_doc->resolveAll();
}

// ─── SetAlignPointCommand ───

SetAlignPointCommand::SetAlignPointCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    const QUuid& newFromPointId, QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newFromPointId(newFromPointId)
{
    setText(QStringLiteral("设置对齐点"));
    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldFromPointId = a.fromPointId;
            m_oldFollowerAngle = a.followerAngle;
            m_oldFollowerFormula = a.followerAngleFormula;
            m_oldRotationMode = a.rotationMode;
            m_oldArcLength = a.arcLength;
            m_oldArcFormula = a.arcLengthFormula;
            break;
        }
    }
}

void SetAlignPointCommand::redo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a || a->fromPointId == m_newFromPointId) return;
    a->fromPointId = m_newFromPointId;
    m_doc->resolveAll();
}

void SetAlignPointCommand::undo()
{
    auto* a = m_doc->findAttachment(m_attId);
    if (!a) return;
    a->fromPointId = m_oldFromPointId;
    a->followerAngle = m_oldFollowerAngle;
    a->followerAngleFormula = m_oldFollowerFormula;
    a->rotationMode = m_oldRotationMode;
    a->arcLength = m_oldArcLength;
    a->arcLengthFormula = m_oldArcFormula;
    m_doc->resolveAll();
}

// ─── ReconnectAttachmentCommand ───

ReconnectAttachmentCommand::ReconnectAttachmentCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    const cad::param::Attachment& newAtt,
    const cad::param::Attachment& oldAtt,
    const cad::geo::Vec2& oldOrigin, double oldRotation, QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newAtt(newAtt)
    , m_oldAtt(oldAtt)
    , m_oldOrigin(oldOrigin)
    , m_oldRotation(oldRotation)
{
    setText(QStringLiteral("\xe9\x87\x8d\xe6\x96\xb0\xe6\x8c\x82\xe6\x8e\xa5"));  // 重新挂接
}

void ReconnectAttachmentCommand::redo()
{
    m_doc->removeAttachment(m_attId);
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_newAtt);
    m_doc->resolveAll();
}

void ReconnectAttachmentCommand::undo()
{
    m_doc->removeAttachment(m_attId);
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_oldAtt);
    if (auto* b = m_doc->findBlock(m_oldAtt.fromBlockId)) {
        b->transform.origin = m_oldOrigin;
        b->transform.rotation = m_oldRotation;
    }
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
    const auto* cmd = dynamic_cast<const SetFollowerAngleCommand*>(other);
    if (!cmd) return false;
    if (cmd->m_attId != m_attId) return false;
    if (!m_newFormula.isEmpty() || !cmd->m_newFormula.isEmpty()) return false;
    if (!m_newArcFormula.isEmpty() || !cmd->m_newArcFormula.isEmpty()) return false;
    m_newAngle = cmd->m_newAngle;
    m_newMode = cmd->m_newMode;
    m_newArcLength = cmd->m_newArcLength;
    return true;
}

} // namespace cad::cmd
