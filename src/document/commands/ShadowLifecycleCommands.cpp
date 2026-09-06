#include "ShadowLifecycleCommands.h"

#include "parametric/ParamDocument.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── ShadowMountCommand ───

ShadowMountCommand::ShadowMountCommand(cad::param::ParamDocument* doc,
                                       const QUuid& shadowId,
                                       const QUuid& toBlockId,
                                       const QUuid& toPointId,
                                       const QUuid& toSegmentId,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_shadowId(shadowId)
{
    setText(QStringLiteral("影子挂载"));

    cad::param::Attachment att1;
    if (!doc->buildShadowMount(shadowId, toBlockId, toPointId, toSegmentId, att1))
        return;
    att1.isLocked = true;  // 新建挂载默认焊接 (拖拽整体跟随不撕裂)
    m_valid = true;
    m_att1 = att1;
    if (const auto* s = doc->findBlock(shadowId)) {
        m_oldLastHostBlockId = s->shadowLastHostBlockId;
        m_oldLastHostPointId = s->shadowLastHostPointId;
        m_oldLastHostSegmentId = s->shadowLastHostSegmentId;
    }
    for (const auto& a : doc->attachments()) {
        if (!a.isPin && a.fromComponentId.isNull() && a.toBlockId == shadowId) {
            m_att2Id = a.id;
            m_oldAtt2 = a;
            break;
        }
    }
}

void ShadowMountCommand::redo()
{
    if (!m_valid) return;
    if (!m_doc->findAttachment(m_att1.id))
        cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_att1);
    if (auto* a = m_doc->findAttachment(m_att2Id)) {
        a->angleOnly = false;
        a->isLocked = true;
        a->slideMode = cad::param::SlideMode::None;
    }
    if (auto* s = m_doc->findBlock(m_shadowId)) {
        s->shadowLastHostBlockId = m_att1.toBlockId;
        s->shadowLastHostPointId = m_att1.toPointId;
        s->shadowLastHostSegmentId = m_att1.toSegmentId;
    }
    m_doc->resolveAll();
    emit m_doc->structureChanged();
}

void ShadowMountCommand::undo()
{
    if (!m_valid) return;
    if (m_doc->findAttachment(m_att1.id))
        m_doc->removeAttachment(m_att1.id);
    if (auto* a = m_doc->findAttachment(m_att2Id))
        *a = m_oldAtt2;
    if (auto* s = m_doc->findBlock(m_shadowId)) {
        s->shadowLastHostBlockId = m_oldLastHostBlockId;
        s->shadowLastHostPointId = m_oldLastHostPointId;
        s->shadowLastHostSegmentId = m_oldLastHostSegmentId;
    }
    m_doc->resolveAll();
    emit m_doc->structureChanged();
}

// ─── RemoveShadowCommand ───

RemoveShadowCommand::RemoveShadowCommand(cad::param::ParamDocument* doc,
                                         const QUuid& shadowId,
                                         QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_shadowId(shadowId)
{
    setText(QStringLiteral("\xe6\xb8\x85\xe9\x99\xa4\xe5\xbd\xb1\xe5\xad\x90"));

    const auto* shadowBlk = doc->findBlock(shadowId);
    if (!shadowBlk || !shadowBlk->isShadow) return;
    m_valid = true;
    m_shadow = *shadowBlk;
    for (const auto& a : doc->attachments()) {
        if (a.isPin) continue;
        if (a.toBlockId == shadowId || a.fromBlockId == shadowId)
            m_atts.push_back(a);
    }
}

void RemoveShadowCommand::redo()
{
    if (!m_valid) return;
    m_doc->removeBlock(m_shadowId);
    m_doc->resolveAll();
    emit m_doc->structureChanged();
}

void RemoveShadowCommand::undo()
{
    if (!m_valid) return;
    if (!m_doc->findBlock(m_shadowId))
        cad::param::RawModelAccess::addBlockRaw(*m_doc, m_shadow);
    for (const auto& att : m_atts)
        if (!m_doc->findAttachment(att.id))
            cad::param::RawModelAccess::addAttachmentRaw(*m_doc, att);
    m_doc->resolveAll();
    emit m_doc->structureChanged();
}

} // namespace cad::cmd
