#include "ComponentCommands.h"

#include <QSet>

#include <algorithm>

#include "parametric/ParamDocument.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Angle.h"

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
        if (const auto* a = doc->findBlock(m_memberBlockIds.first()))
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
    if (const auto* c = doc->findComponent(componentId))
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
    const cad::param::Component* c = doc->findComponent(componentId);
    if (!c) return;
    m_component = *c;

    // Cascade set = all members + bridges pinned to any member.
    QSet<QUuid> cascade;
    for (const QUuid& mid : c->memberBlockIds) {
        cascade.insert(mid);
        if (const auto* b = doc->findBlock(mid))
            m_blocks.push_back(*b);
    }
    for (const QUuid& mid : c->memberBlockIds) {
        for (const QUuid& bridgeId : doc->bridgesPinnedTo(mid)) {
            if (cascade.contains(bridgeId)) continue;
            cascade.insert(bridgeId);
            if (const auto* b = doc->findBlock(bridgeId))
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
            if (const auto* cb = doc->findBlock(cid))
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
    m_doc->addAttachmentsRaw(m_attachments);
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
    if (const auto* c = doc->findComponent(componentId))
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
    const cad::param::Component* c = doc->findComponent(componentId);
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
        dirBlockId = doc->memberOwningPoint(*c, c->exposedPointId);
    const cad::param::Block* dirBlk = doc->findBlock(dirBlockId);
    if (!dirBlk) return;
    const cad::param::BBox box = doc->boundingBoxOf(componentId);
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
        const cad::param::Block* b = doc->findBlock(mid);
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
        m_doc->addAttachmentRaw(m_att);
    m_doc->resolveAll();
}


// ─── RotateComponentCommand ───

RotateComponentCommand::RotateComponentCommand(cad::param::ParamDocument* doc,
                                               const QUuid& componentId,
                                               const QHash<QUuid, cad::param::Transform2D>& oldTf,
                                               const QHash<QUuid, cad::param::Transform2D>& newTf,
                                               const std::vector<cad::param::Attachment>& releasedAtts,
                                               const std::vector<AimRelease>& releasedTargets,
                                               const std::vector<DartRelease>& releasedDarts,
                                               QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_componentId(componentId)
    , m_oldTf(oldTf)
    , m_newTf(newTf)
    , m_releasedAtts(releasedAtts)
    , m_releasedTargets(releasedTargets)
    , m_releasedDarts(releasedDarts)
{
    setText(QStringLiteral("整组旋转"));
}

void RotateComponentCommand::redo()
{
    if (!m_doc) return;
    // 1) 全体成员写入新刚体位姿.
    for (auto it = m_newTf.cbegin(); it != m_newTf.cend(); ++it) {
        if (auto* b = m_doc->findBlock(it.key()))
            b->transform = it.value();
    }
    // 2) 释放外部约束: attachment 批处理 (组件级不清 exposedPointId).
    QList<QUuid> ids;
    ids.reserve(static_cast<int>(m_releasedAtts.size()));
    for (const auto& a : m_releasedAtts) ids << a.id;
    if (!ids.isEmpty())
        m_doc->removeAttachments(ids);
    // 3) 清外部 endTarget (成员指向组外点).
    for (const auto& r : m_releasedTargets) {
        if (auto* b = m_doc->findBlock(r.blockId)) {
            b->endTargetBlockId = QUuid();
            b->endTargetPointId = QUuid();
            b->endTargetOffset = 0.0;
            b->endTargetOffsetFormula.clear();
        }
    }
    // 4) 省道引用组外 → 降级普通线 (清 start/ref; 偏移/角度字段保留).
    for (const auto& r : m_releasedDarts) {
        if (auto* b = m_doc->findBlock(r.blockId)) {
            b->dartStartBlockId = QUuid();
            b->dartStartPointId = QUuid();
            b->dartRefBlockId = QUuid();
            b->dartRefPointId = QUuid();
            b->dartRefSegmentId = QUuid();
        }
    }
    m_doc->resolveAll();
}

void RotateComponentCommand::undo()
{
    if (!m_doc) return;
    // 还原全体成员位姿.
    for (auto it = m_oldTf.cbegin(); it != m_oldTf.cend(); ++it) {
        if (auto* b = m_doc->findBlock(it.key()))
            b->transform = it.value();
    }
    // 还原外部约束 (原样快照, 快照完整性).
    if (!m_releasedAtts.empty())
        m_doc->addAttachmentsRaw(m_releasedAtts);
    for (const auto& r : m_releasedTargets) {
        if (auto* b = m_doc->findBlock(r.blockId)) {
            b->endTargetBlockId = r.endTargetBlockId;
            b->endTargetPointId = r.endTargetPointId;
            b->endTargetOffset = r.endTargetOffset;
            b->endTargetOffsetFormula = r.endTargetOffsetFormula;
        }
    }
    for (const auto& r : m_releasedDarts) {
        if (auto* b = m_doc->findBlock(r.blockId)) {
            b->dartStartBlockId = r.dartStartBlockId;
            b->dartStartPointId = r.dartStartPointId;
            b->dartRefBlockId = r.dartRefBlockId;
            b->dartRefPointId = r.dartRefPointId;
            b->dartRefSegmentId = r.dartRefSegmentId;
            b->dartOffsetMm = r.dartOffsetMm;
            b->dartOffsetFormula = r.dartOffsetFormula;
            b->dartAngleDeg = r.dartAngleDeg;
            b->dartAngleFormula = r.dartAngleFormula;
        }
    }
    m_doc->resolveAll();
}

} // namespace cad::cmd
