#include "AttachmentLifecycleCommands.h"

#include <algorithm>

#include "parametric/ParamDocument.h"
#include "parametric/FollowerAngle.h"
#include "parametric/ParamDocumentRaw.h"

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
    // 快照完整性 (用户拍板 2026-09): verbatim 插入, 不经过 addAttachment 的
    // 强制 isLocked=true — undo/redo 必须原样还原用户状态 (与
    // RemoveAttachmentCommand::undo 的 addAttachmentRaw 对称)。
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_att);
    m_doc->resolveAll();
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
            cad::param::RawModelAccess::addAttachmentRaw(*m_doc, a);
        m_doc->resolveAll();
        return;
    }
    cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_att);
    m_doc->resolveAll();
}

// ─── SetAttachmentAngleOnlyCommand (影子基准语义, DETACH_SHADOW_DESIGN.md) ───

SetAttachmentAngleOnlyCommand::SetAttachmentAngleOnlyCommand(
    cad::param::ParamDocument* doc, const QUuid& attId, bool angleOnly,
    const QUuid& explicitToPoint, const QUuid& explicitToSegment,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_attId(attId)
    , m_newAngleOnly(angleOnly)
    , m_oldAngleOnly(false)
    , m_oldLocked(false)
    , m_oldSlideMode(cad::param::SlideMode::None)
    , m_explicitToPoint(explicitToPoint)
    , m_explicitToSegment(explicitToSegment)
{
    setText(QStringLiteral("\xe6\x8b\x86\xe5\xbc\x80\xe4\xbf\x9d\xe7\x95\x99\xe8\xa7\x92\xe5\xba\xa6"));  // 拆开保留角度

    const cad::param::Attachment* att = nullptr;
    bool toIsShadow = false;
    for (const auto& a : doc->attachments()) {
        if (a.id == attId) {
            att = &a;
            m_oldAngleOnly = a.angleOnly;
            m_oldLocked = a.isLocked;
            m_oldSlideMode = a.slideMode;
            m_oldAtt = a;
            if (const auto* toBlk = doc->findBlock(a.toBlockId))
                toIsShadow = toBlk->isShadow;
            break;
        }
    }
    if (!att) return;

    if (angleOnly && toIsShadow) {
        m_mode = Mode::ReDetach;
        for (const auto& a : doc->attachments()) {
            if (!a.isPin && a.fromBlockId == att->toBlockId) {
                m_oldAtt1 = a;
                m_hasAtt1 = true;
                break;
            }
        }
        return;
    }
    if (angleOnly) {
        cad::param::Block shadow;
        cad::param::Attachment newAtt;
        if (doc->buildShadowDetach(attId, shadow, newAtt)) {
            m_mode = Mode::FreshDetach;
            m_shadow = std::move(shadow);
            m_newAtt = std::move(newAtt);
            m_hasShadow = true;
        }
        return;
    }
    if (toIsShadow) {
        cad::param::Attachment restored;
        if (doc->buildShadowReconnect(attId, restored,
                                      m_explicitToPoint, m_explicitToSegment)) {
            m_mode = Mode::ReconnectMaster;
            m_newAtt = std::move(restored);
            if (const auto* shadowBlk = doc->findBlock(m_oldAtt.toBlockId)) {
                m_shadow = *shadowBlk;
                m_hasShadow = true;
            }
            for (const auto& a : doc->attachments()) {
                if (!a.isPin && a.fromBlockId == m_oldAtt.toBlockId) {
                    m_oldAtt1 = a;
                    m_hasAtt1 = true;
                    break;
                }
            }
        }
        return;
    }
}

void SetAttachmentAngleOnlyCommand::redo()
{
    switch (m_mode) {
    case Mode::FreshDetach: {
        if (!m_doc->findBlock(m_shadow.id))
            cad::param::RawModelAccess::addBlockRaw(*m_doc, m_shadow);
        if (auto* a = m_doc->findAttachment(m_attId))
            *a = m_newAtt;
        m_doc->resolveAll();
        emit m_doc->structureChanged();
        return;
    }
    case Mode::ReDetach: {
        if (m_hasAtt1 && m_doc->findAttachment(m_oldAtt1.id))
            m_doc->removeAttachment(m_oldAtt1.id);
        if (auto* a = m_doc->findAttachment(m_attId)) {
            a->angleOnly = true;
            a->isLocked = false;
            a->slideMode = cad::param::SlideMode::None;
        }
        m_doc->resolveAll();
        return;
    }
    case Mode::ReconnectMaster: {
        if (auto* a = m_doc->findAttachment(m_attId))
            *a = m_newAtt;
        m_doc->removeBlock(m_shadow.id);
        m_doc->resolveAll();
        emit m_doc->structureChanged();
        return;
    }
    case Mode::Legacy:
    default:
        break;
    }
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->angleOnly = m_newAngleOnly;
        if (m_newAngleOnly) {
            a->isLocked = false;
            a->slideMode = cad::param::SlideMode::None;
        } else {
            a->isLocked = true;
        }
    }
    m_doc->resolveAll();
}

void SetAttachmentAngleOnlyCommand::undo()
{
    switch (m_mode) {
    case Mode::FreshDetach: {
        if (auto* a = m_doc->findAttachment(m_attId))
            *a = m_oldAtt;
        m_doc->removeBlock(m_shadow.id);
        m_doc->resolveAll();
        emit m_doc->structureChanged();
        return;
    }
    case Mode::ReDetach: {
        if (auto* a = m_doc->findAttachment(m_attId))
            *a = m_oldAtt;
        if (m_hasAtt1)
            cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_oldAtt1);
        m_doc->resolveAll();
        emit m_doc->structureChanged();
        return;
    }
    case Mode::ReconnectMaster: {
        if (!m_doc->findBlock(m_shadow.id))
            cad::param::RawModelAccess::addBlockRaw(*m_doc, m_shadow);
        if (m_hasAtt1)
            cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_oldAtt1);
        if (auto* a = m_doc->findAttachment(m_attId))
            *a = m_oldAtt;
        m_doc->resolveAll();
        emit m_doc->structureChanged();
        return;
    }
    case Mode::Legacy:
    default:
        break;
    }
    if (auto* a = m_doc->findAttachment(m_attId)) {
        a->angleOnly = m_oldAngleOnly;
        a->isLocked = m_oldLocked;
        a->slideMode = m_oldSlideMode;
    }
    m_doc->resolveAll();
}

// ─── ShadowMountCommand ───

ShadowMountCommand::ShadowMountCommand(cad::param::ParamDocument* doc,
                                       const QUuid& shadowId,
                                       const QUuid& toBlockId,
                                       const QUuid& toPointId,
                                       const QUuid& toSegmentId,
                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("\xe5\xbd\xb1\xe5\xad\x90\xe6\x8c\x82\xe8\xbd\xbd"));

    cad::param::Attachment att1;
    if (!doc->buildShadowMount(shadowId, toBlockId, toPointId, toSegmentId, att1))
        return;
    m_valid = true;
    m_att1 = att1;
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
