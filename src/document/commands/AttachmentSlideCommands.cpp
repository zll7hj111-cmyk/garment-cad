#include "AttachmentSlideCommands.h"

#include "parametric/ParamDocument.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── SetAttachmentSlideModeCommand ───

SetAttachmentSlideModeCommand::SetAttachmentSlideModeCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    cad::param::SlideMode mode, QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newMode(mode)
{
    setText(QStringLiteral("\xe6\xbb\x91\xe8\xbd\xa8\xe6\xa8\xa1\xe5\xbc\x8f"));

    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldMode = a.slideMode;
            m_oldAlongMm = a.slideAlongMm;
            m_oldPerpMm = a.slidePerpMm;
            m_oldAngleOnly = a.angleOnly;
            m_oldLocked = a.isLocked;
            break;
        }
    }
}

void SetAttachmentSlideModeCommand::redo()
{
    m_doc->setAttachmentSlideMode(m_attId, m_newMode);
}

void SetAttachmentSlideModeCommand::undo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->slideMode = m_oldMode;
        a->slideAlongMm = m_oldAlongMm;
        a->slidePerpMm = m_oldPerpMm;
        a->angleOnly = m_oldAngleOnly;
        a->isLocked = m_oldLocked;
    }
    m_doc->resolveAll();
}

// ─── SetAttachmentLockedCommand ───

SetAttachmentLockedCommand::SetAttachmentLockedCommand(
    cad::param::ParamDocument* doc, const QUuid& attId, bool locked,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newLocked(locked)
    , m_oldLocked(false)
{
    setText(QStringLiteral("\xe6\x8b\x96\xe5\x8a\xa8\xe4\xbf\x9d\xe6\x8a\xa4"));

    for (const auto& a : doc->attachments())
        if (a.id == attId) { m_oldLocked = a.isLocked; break; }
}

void SetAttachmentLockedCommand::redo()
{
    m_doc->setAttachmentLocked(m_attId, m_newLocked);
}

void SetAttachmentLockedCommand::undo()
{
    m_doc->setAttachmentLocked(m_attId, m_oldLocked);
}

// ─── SetSlideOffsetsCommand ───

SetSlideOffsetsCommand::SetSlideOffsetsCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    double oldAlong, double oldPerp, double newAlong, double newPerp,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_oldAlong(oldAlong)
    , m_oldPerp(oldPerp)
    , m_newAlong(newAlong)
    , m_newPerp(newPerp)
{
    setText(QStringLiteral("\xe6\xbb\x91\xe5\x8a\xa8\xe5\xbe\xae\xe8\xb0\x83"));
}

void SetSlideOffsetsCommand::redo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->slideAlongMm = m_newAlong;
        a->slidePerpMm = m_newPerp;
    }
}

void SetSlideOffsetsCommand::undo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->slideAlongMm = m_oldAlong;
        a->slidePerpMm = m_oldPerp;
    }
    m_doc->resolveAll();
}

// ─── SetAttachmentSlideOffsetsCommand ───

SetAttachmentSlideOffsetsCommand::SetAttachmentSlideOffsetsCommand(
    cad::param::ParamDocument* doc, const QUuid& attId,
    cad::param::SlideMode newMode,
    double newAlongMm, const QString& newAlongFormula,
    double newPerpMm, const QString& newPerpFormula,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newMode(newMode)
    , m_newAlongMm(newAlongMm)
    , m_newAlongFormula(newAlongFormula)
    , m_newPerpMm(newPerpMm)
    , m_newPerpFormula(newPerpFormula)
{
    setText(QStringLiteral("滑轨偏移"));
    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            m_oldMode = a.slideMode;
            m_oldAlongMm = a.slideAlongMm;
            m_oldAlongFormula = a.slideAlongFormula;
            m_oldPerpMm = a.slidePerpMm;
            m_oldPerpFormula = a.slidePerpFormula;
            break;
        }
    }
}

void SetAttachmentSlideOffsetsCommand::redo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->slideMode = m_newMode;
        a->slideAlongMm = m_newAlongMm;
        a->slideAlongFormula = m_newAlongFormula;
        a->slidePerpMm = m_newPerpMm;
        a->slidePerpFormula = m_newPerpFormula;
    }
    m_doc->resolveAll();
}

void SetAttachmentSlideOffsetsCommand::undo()
{
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->slideMode = m_oldMode;
        a->slideAlongMm = m_oldAlongMm;
        a->slideAlongFormula = m_oldAlongFormula;
        a->slidePerpMm = m_oldPerpMm;
        a->slidePerpFormula = m_oldPerpFormula;
    }
    m_doc->resolveAll();
}

} // namespace cad::cmd
