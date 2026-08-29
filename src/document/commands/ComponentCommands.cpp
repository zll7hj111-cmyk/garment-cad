#include "ComponentCommands.h"

#include <QSet>

#include <algorithm>

#include "parametric/ParamDocument.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Angle.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── MakeComponentCommand ───

MakeComponentCommand::MakeComponentCommand(cad::param::ParamDocument* doc,
                                           const QList<QUuid>& memberBlockIds,
                                           const QString& name,
                                           QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_memberBlockIds(memberBlockIds)
    , m_name(name)
    , m_componentId(QUuid::createUuid())
{
    setText(QStringLiteral("创建组件"));
    // Creation-time overall orientation (回到默认角度 target): the first
    // member's current rotation.
    if (!m_memberBlockIds.isEmpty()) {
        if (const auto* a = doc->blocksView().byId(m_memberBlockIds.first()))
            m_defaultAngleDeg = cad::geo::radToDeg(a->transform.rotation);
    }
}

void MakeComponentCommand::redo()
{
    cad::param::Component c;
    c.id = m_componentId;
    c.name = m_name;
    c.memberBlockIds.assign(m_memberBlockIds.begin(), m_memberBlockIds.end());
    c.defaultAngleDeg = m_defaultAngleDeg;
    m_doc->addComponent(c);
}

void MakeComponentCommand::undo()
{
    m_doc->removeComponentRecord(m_componentId);
}

// ─── DissolveComponentCommand ───

DissolveComponentCommand::DissolveComponentCommand(cad::param::ParamDocument* doc,
                                                   const QUuid& componentId,
                                                   QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("解散组件"));
    if (const auto* c = doc->componentsView().byId(componentId))
        m_component = *c;
}

void DissolveComponentCommand::redo()
{
    m_doc->removeComponentRecord(m_component.id);
}

void DissolveComponentCommand::undo()
{
    m_doc->addComponent(m_component);
}

// ─── DeleteComponentCommand ───

DeleteComponentCommand::DeleteComponentCommand(cad::param::ParamDocument* doc,
                                               const QUuid& componentId,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("删除组件"));
    const cad::param::Component* c = doc->componentsView().byId(componentId);
    if (!c) return;
    m_component = *c;

    // Cascade set = all members + bridges pinned to any member.
    QSet<QUuid> cascade;
    for (const QUuid& mid : c->memberBlockIds) {
        cascade.insert(mid);
        if (const auto* b = doc->blocksView().byId(mid))
            m_blocks.push_back(*b);
    }
    for (const QUuid& mid : c->memberBlockIds) {
        for (const QUuid& bridgeId : doc->attachmentsView().bridgesPinnedTo(mid)) {
            if (cascade.contains(bridgeId)) continue;
            cascade.insert(bridgeId);
            if (const auto* b = doc->blocksView().byId(bridgeId))
                m_bridges.push_back(*b);
        }
    }

    // Attachments touching any cascade block (deduped across the whole set),
    // plus the component's own component-level attachment.
    QSet<QUuid> seen;
    for (const auto& att : doc->attachments()) {
        const bool touchesMember = cascade.contains(att.fromBlockId)
            || cascade.contains(att.toBlockId);
        const bool isOwn = att.fromComponentId == m_component.id;
        if (!touchesMember && !isOwn) continue;
        if (seen.contains(att.id)) continue;
        seen.insert(att.id);
        m_attachments.push_back(att);
    }

    // Linked variables sourced from any cascade block, their baked consumers,
    // and measure variables referencing the cascade set.
    for (const QUuid& srcId : cascade) {
        for (const auto& lv : doc->linkedVars())
            if (lv.sourceBlockId == srcId)
                m_linked.push_back(lv);
        for (const QUuid& cid : doc->linkedConsumerBlocks(srcId)) {
            if (cascade.contains(cid)) continue;
            const bool taken = std::any_of(
                m_bakedConsumers.begin(), m_bakedConsumers.end(),
                [&cid](const cad::param::Block& b) { return b.id == cid; });
            if (taken) continue;
            if (const auto* cb = doc->blocksView().byId(cid))
                m_bakedConsumers.push_back(*cb);
        }
        for (const auto& mv : doc->measureVars())
            if (mv.blockA == srcId || mv.blockB == srcId || mv.ownerBlockId == srcId)
                m_measures.push_back(mv);
    }
}

void DeleteComponentCommand::redo()
{
    // Dissolve first (members become independent), then delete every member.
    m_doc->removeComponentRecord(m_component.id);
    for (const auto& b : m_blocks)
        m_doc->removeBlock(b.id);
}

void DeleteComponentCommand::undo()
{
    for (const auto& bridge : m_bridges)
        m_doc->removeBlock(bridge.id);
    for (const auto& b : m_blocks)
        m_doc->addBlock(b);
    for (const auto& bridge : m_bridges)
        m_doc->addBlock(bridge);
    cad::param::RawModelAccess::addAttachmentsRaw(*m_doc, m_attachments);
    for (const auto& lv : m_linked)
        m_doc->addLinked(lv);
    for (const auto& mv : m_measures)
        m_doc->addMeasure(mv);
    for (const auto& snap : m_bakedConsumers) {
        if (auto* b = m_doc->findBlock(snap.id))
            *b = snap;
    }
    m_doc->addComponent(m_component);
    m_doc->resolveAll();
}

// ─── SetComponentPropertyCommand ───

SetComponentPropertyCommand::SetComponentPropertyCommand(cad::param::ParamDocument* doc,
                                                         const QUuid& componentId,
                                                         const QString& name,
                                                         bool showBoundingBox,
                                                         double defaultAngleDeg,
                                                         const QString& defaultAngleFormula,
                                                         QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("组件属性"));
    if (const auto* c = doc->componentsView().byId(componentId))
        m_old = *c;
    m_new = m_old;
    m_new.name = name;
    m_new.showBoundingBox = showBoundingBox;
    m_new.defaultAngleDeg = defaultAngleDeg;
    m_new.defaultAngleFormula = defaultAngleFormula;
}

void SetComponentPropertyCommand::redo()
{
    m_doc->updateComponent(m_new);
}

void SetComponentPropertyCommand::undo()
{
    m_doc->updateComponent(m_old);
}

// ─── ResetComponentAngleCommand ───

ResetComponentAngleCommand::ResetComponentAngleCommand(cad::param::ParamDocument* doc,
                                                       const QUuid& componentId,
                                                       QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
{
    setText(QStringLiteral("回到初始状态"));
    const cad::param::Component* c = doc->componentsView().byId(componentId);
    if (!c || c->memberBlockIds.empty()) return;
    m_oldComponent = *c;
    m_newComponent = *c;

    // 1) 取消跟随: snapshot the component-level attachment (if any).
    for (const auto& a : doc->attachments()) {
        if (a.fromComponentId == componentId) {
            m_att = a;
            m_hadAttachment = true;
            break;
        }
    }

    // Overall orientation = exposed endpoint's host member (or first member).
    QUuid dirBlockId = c->memberBlockIds.front();
    if (!c->exposedPointId.isNull())
        dirBlockId = doc->componentsView().memberOwningPoint(*c, c->exposedPointId);
    const cad::param::Block* dirBlk = doc->blocksView().byId(dirBlockId);
    if (!dirBlk) return;
    const cad::param::BBox box = doc->componentsView().boundingBoxOf(componentId);
    if (!box.valid) return;

    // 2) 转回原始角: 公式 (若有) 求值一次并烘焙为数值, 然后清除公式.
    double targetDeg = c->defaultAngleDeg;
    if (!c->defaultAngleFormula.isEmpty()) {
        auto r = cad::param::ConditionEngine::evaluate(
            c->defaultAngleFormula, m_doc->parameters(), m_doc->conditions());
        if (r.ok) targetDeg = r.value;
    }
    m_newComponent.defaultAngleDeg = targetDeg;
    m_newComponent.defaultAngleFormula.clear();

    const double targetRad = cad::geo::degToRad(targetDeg);
    const double delta = targetRad - dirBlk->transform.rotation;
    const cad::geo::Vec2 pivot = box.center();
    for (const QUuid& mid : c->memberBlockIds) {
        const cad::param::Block* b = doc->blocksView().byId(mid);
        if (!b) continue;
        m_oldTransforms.insert(mid, b->transform);
        cad::param::Transform2D nf = b->transform;
        nf.origin = pivot + (nf.origin - pivot).rotated(delta);
        nf.rotation = nf.rotation + delta;
        m_newTransforms.insert(mid, nf);
    }
}

void ResetComponentAngleCommand::redo()
{
    // 1) 取消跟随.
    if (m_hadAttachment)
        m_doc->removeAttachment(m_att.id);
    // 2) 清除公式 + 烘焙数值.
    m_doc->updateComponent(m_newComponent);
    // 3) 转回原始角.
    for (auto it = m_newTransforms.cbegin(); it != m_newTransforms.cend(); ++it) {
        if (auto* b = m_doc->findBlock(it.key()))
            b->transform = it.value();
    }
    m_doc->resolveAll();
}

void ResetComponentAngleCommand::undo()
{
    // 还原旋转.
    for (auto it = m_oldTransforms.cbegin(); it != m_oldTransforms.cend(); ++it) {
        if (auto* b = m_doc->findBlock(it.key()))
            b->transform = it.value();
    }
    // 还原公式/数值.
    m_doc->updateComponent(m_oldComponent);
    // 还原对接.
    if (m_hadAttachment)
        cad::param::RawModelAccess::addAttachmentRaw(*m_doc, m_att);
    m_doc->resolveAll();
}


} // namespace cad::cmd
