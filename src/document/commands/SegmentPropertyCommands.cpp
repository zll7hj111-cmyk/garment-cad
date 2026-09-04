#include "document/commands/SegmentPropertyCommands.h"

#include <algorithm>

#include "parametric/ParamDocument.h"
#include "parametric/AngleMeasureVariable.h"
#include "parametric/Serial.h"
#include "geometry/Angle.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::cmd {

// ─── SetSegmentPropertyCommand ───

namespace {
/// Apply a property snapshot to a segment; reports whether anything changed.
/// Display attributes (name/visible/style/...) are mirrored in the canvas
/// item cache (BlockItem::m_lines), which is only rebuilt when
/// block->geometryEpoch changes — a property-only edit moves no points, so
/// the resolve pass alone would never invalidate the cache (same rationale
/// as ParamDocument::setOwnerMeasureName).
bool applySegmentProps(cad::param::Segment* s,
                       const SetSegmentPropertyCommand::Props& p)
{
    bool changed = false;
    auto upd = [&changed](auto& dst, const auto& src) {
        if (dst != src) { dst = src; changed = true; }
    };
    upd(s->name, p.name);
    upd(s->annotation, p.annotation);
    upd(s->role, p.role);
    upd(s->lineStyle, p.lineStyle);
    upd(s->color, p.color);
    upd(s->weight, p.weight);
    upd(s->visible, p.visible);
    upd(s->showName, p.showName);
    upd(s->showLength, p.showLength);
    upd(s->lengthFormula, p.lengthFormula);
    return changed;
}
} // namespace

SetSegmentPropertyCommand::SetSegmentPropertyCommand(
    cad::param::ParamDocument* doc,
    const QUuid& blockId, const QUuid& segmentId,
    const Props& newProps,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_newProps(newProps)
{
    setText(QStringLiteral("修改线段属性"));

    // Capture old state
    if (auto* b = doc->findBlock(blockId)) {
        if (auto* s = b->findSegment(segmentId)) {
            m_oldProps.name = s->name;
            m_oldProps.annotation = s->annotation;
            m_oldProps.role = s->role;
            m_oldProps.lineStyle = s->lineStyle;
            m_oldProps.color = s->color;
            m_oldProps.weight = s->weight;
            m_oldProps.visible = s->visible;
            m_oldProps.showName = s->showName;
            m_oldProps.showLength = s->showLength;
            m_oldProps.lengthFormula = s->lengthFormula;
        }
    }
}

void SetSegmentPropertyCommand::redo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        if (auto* s = b->findSegment(m_segmentId)) {
            // Bump geometryEpoch so the canvas rebuilds its cached display
            // state on the next resolve — visibility/name/style edits move no
            // geometry, so Block::resolve would not invalidate the cache.
            if (applySegmentProps(s, m_newProps))
                b->touchGeometry();
        }
    }
    m_doc->resolveAll();
}

void SetSegmentPropertyCommand::undo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        if (auto* s = b->findSegment(m_segmentId)) {
            if (applySegmentProps(s, m_oldProps))
                b->touchGeometry();
        }
    }
    m_doc->resolveAll();
}

// ─── SetSegmentExtendCommand (端点延长量, EXTEND_LINE_DESIGN.md) ───

bool SetSegmentExtendCommand::apply(cad::param::Segment* s, const Values& v)
{
    if (!s) return false;
    bool changed = false;
    auto upd = [&changed](auto& dst, const auto& src) {
        if (dst != src) { dst = src; changed = true; }
    };
    upd(s->extendStartMm, v.startMm);
    upd(s->extendStartFormula, v.startFormula);
    upd(s->extendEndMm, v.endMm);
    upd(s->extendEndFormula, v.endFormula);
    return changed;
}

SetSegmentExtendCommand::SetSegmentExtendCommand(cad::param::ParamDocument* doc,
                                                 const QUuid& blockId,
                                                 const QUuid& segmentId,
                                                 const Values& newValues,
                                                 QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_newValues(newValues)
{
    setText(QStringLiteral("修改延长量"));
    if (auto* b = doc->findBlock(blockId)) {
        if (auto* s = b->findSegment(segmentId)) {
            m_oldValues.startMm = s->extendStartMm;
            m_oldValues.startFormula = s->extendStartFormula;
            m_oldValues.endMm = s->extendEndMm;
            m_oldValues.endFormula = s->extendEndFormula;
        }
    }
}

void SetSegmentExtendCommand::redo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        auto* s = b->findSegment(m_segmentId);
        // 本体不动但可视尾巴变 → 显式 +epoch（画布重绘铁律）。
        if (apply(s, m_newValues))
            b->touchGeometry();
    }
    m_doc->resolveAll();
}

void SetSegmentExtendCommand::undo()
{
    if (auto* b = m_doc->findBlock(m_blockId)) {
        auto* s = b->findSegment(m_segmentId);
        if (apply(s, m_oldValues))
            b->touchGeometry();
    }
    m_doc->resolveAll();
}

// ─── SegmentEditBarCommand ───

namespace {

/// Apply one edit-strip state snapshot to the model (name/length/angle).
/// Missing block/segment/attachment → no-op (deleted concurrently).
void applyEditStripState(cad::param::ParamDocument& doc,
                         const QUuid& blockId, const QUuid& segmentId,
                         const SegmentEditBarCommand::State& s)
{
    auto* b = doc.findBlock(blockId);
    auto* seg = b ? b->findSegment(segmentId) : nullptr;
    if (!b || !seg) return;
    if (seg->name != s.segName) {
        seg->name = s.segName;
        b->touchGeometry();
    }
    seg->lengthFormula = s.lengthFormula;
    // The owned measure variable's display name follows the segment name.
    doc.setOwnerMeasureName(blockId, s.segName);
    if (auto* ep = b->findPoint(seg->endPointId)) {
        ep->distance = s.endDistance;
        ep->distanceFormula = s.endDistanceFormula;
        ep->angle = s.endAngle;
        ep->angleFormula = s.endAngleFormula;
        ep->constraint = static_cast<cad::param::PointConstraint>(s.endConstraint);
        ep->refPointId = s.endRefPointId;
    }
    if (!s.attId.isNull()) {
        if (auto* a = doc.findAttachment(s.attId)) {
            a->followerAngle = s.followerAngle;
            a->followerAngleFormula = s.followerAngleFormula;
            a->arcLength = s.arcLength;
            a->arcLengthFormula = s.arcLengthFormula;
            a->rotationMode = static_cast<cad::param::RotationMode>(s.rotationMode);
        }
    }
}

} // namespace

SegmentEditBarCommand::SegmentEditBarCommand(cad::param::ParamDocument* doc,
                                             const QUuid& blockId,
                                             const QUuid& segmentId,
                                             State newState,
                                             QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_newState(std::move(newState))
{
    setText(QStringLiteral("编辑线段属性"));
    // Snapshot the pre-edit state from the model.
    if (const auto* b = doc->findBlock(blockId)) {
        if (const auto* seg = b->findSegment(segmentId)) {
            m_oldState.segName = seg->name;
            m_oldState.lengthFormula = seg->lengthFormula;
            if (const auto* ep = b->findPoint(seg->endPointId)) {
                m_oldState.endDistance = ep->distance;
                m_oldState.endDistanceFormula = ep->distanceFormula;
                m_oldState.endAngle = ep->angle;
                m_oldState.endAngleFormula = ep->angleFormula;
                m_oldState.endConstraint = static_cast<int>(ep->constraint);
                m_oldState.endRefPointId = ep->refPointId;
            }
            // Follower attachment snapshot: the attachment anchored at THIS
            // segment's start/end point (a block may own one attachment while
            // having several lines — the first block-wide match would snapshot
            // the WRONG line's attachment; same rule as SegmentEditBar).
            for (const auto& att : doc->attachments()) {
                if (att.fromBlockId != blockId || att.isPin) continue;
                if (att.fromPointId != seg->startPointId
                    && att.fromPointId != seg->endPointId)
                    continue;
                m_oldState.attId = att.id;
                m_oldState.followerAngle = att.followerAngle;
                m_oldState.followerAngleFormula = att.followerAngleFormula;
                m_oldState.arcLength = att.arcLength;
                m_oldState.arcLengthFormula = att.arcLengthFormula;
                m_oldState.rotationMode = static_cast<int>(att.rotationMode);
                break;
            }
        }
    }
}

void SegmentEditBarCommand::redo()
{
    applyEditStripState(*m_doc, m_blockId, m_segmentId, m_newState);
    m_doc->resolveAll();
}

void SegmentEditBarCommand::undo()
{
    applyEditStripState(*m_doc, m_blockId, m_segmentId, m_oldState);
    m_doc->resolveAll();
}

// ─── SetLinePropertiesCommand (P0-3: LinePropertyDialog 会话收口) ───

bool SetLinePropertiesCommand::Props::operator==(const Props& o) const
{
    return name == o.name && annotation == o.annotation && role == o.role
        && showName == o.showName && showLength == o.showLength
        && visible == o.visible && color == o.color
        && lineStyle == o.lineStyle && weight == o.weight
        && lengthFormula == o.lengthFormula
        && distance == o.distance && distanceFormula == o.distanceFormula
        && startName == o.startName && startAnno == o.startAnno
        && startShowName == o.startShowName
        && endName == o.endName && endAnno == o.endAnno
        && endShowName == o.endShowName
        && lengthAuto == o.lengthAuto;
}

bool SetLinePropertiesCommand::apply(cad::param::ParamDocument* doc,
                                     cad::param::Block* b,
                                     cad::param::Segment* s,
                                     const Props& p)
{
    if (!doc || !b || !s) return false;
    bool changed = false;
    auto upd = [&changed](auto& dst, const auto& src) {
        if (dst != src) { dst = src; changed = true; }
    };
    upd(s->name, p.name);
    upd(s->annotation, p.annotation);
    upd(s->role, p.role);
    upd(s->showName, p.showName);
    upd(s->showLength, p.showLength);
    upd(s->visible, p.visible);
    upd(s->color, p.color);
    upd(s->lineStyle, p.lineStyle);
    upd(s->weight, p.weight);
    upd(s->lengthFormula, p.lengthFormula);
    upd(b->lengthAuto, p.lengthAuto);   // 块级长度模式 (2026-09 审核收口)
    if (auto* ep = b->findPoint(s->endPointId)) {
        upd(ep->distance, p.distance);
        upd(ep->distanceFormula, p.distanceFormula);
        upd(ep->name, p.endName);
        upd(ep->showName, p.endShowName);
        upd(ep->annotation, p.endAnno);
    }
    if (auto* sp = b->findPoint(s->startPointId)) {
        upd(sp->name, p.startName);
        upd(sp->showName, p.startShowName);
        upd(sp->annotation, p.startAnno);
    }
    // The owned measure variable's display name follows the segment name
    // (same coupling as LinePropertyDialog::applyToModel / SegmentEditBar).
    if (doc)
        doc->setOwnerMeasureName(b->id, p.name);
    return changed;
}

SetLinePropertiesCommand::SetLinePropertiesCommand(
    cad::param::ParamDocument* doc,
    const QUuid& blockId, const QUuid& segmentId,
    Props oldProps, Props newProps,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_doc(doc)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_oldProps(std::move(oldProps))
    , m_newProps(std::move(newProps))
{
    setText(QStringLiteral("修改线条属性"));
}

void SetLinePropertiesCommand::redo()
{
    auto* b = m_doc->findBlock(m_blockId);
    auto* s = b ? b->findSegment(m_segmentId) : nullptr;
    if (apply(m_doc, b, s, m_newProps) && b)
        b->touchGeometry();
    m_doc->resolveAll();
}

void SetLinePropertiesCommand::undo()
{
    auto* b = m_doc->findBlock(m_blockId);
    auto* s = b ? b->findSegment(m_segmentId) : nullptr;
    if (apply(m_doc, b, s, m_oldProps) && b)
        b->touchGeometry();
    m_doc->resolveAll();
}

} // namespace cad::cmd
