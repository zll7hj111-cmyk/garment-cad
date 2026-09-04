#include "ui/LinePropertySession.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "ui/SegmentAuxTab.h"
#include "document/commands/BlockCommands.h"

namespace cad::ui {

void LinePropertySession::takeSnapshot(cad::param::ParamDocument* doc,
                                       const QUuid& blockId,
                                       const QUuid& segmentId)
{
    if (!doc) return;
    const auto* block = doc->findBlock(blockId);
    if (!block) return;
    const auto* seg = block->findSegment(segmentId);
    if (!seg) return;

    m_snapshot.followerAtt.reset();
    for (const auto& att : doc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId != blockId) continue;
        m_snapshot.followerAtt = att;
        break;
    }

    m_snapshot.segName       = seg->name;
    m_snapshot.segAnnotation = seg->annotation;
    m_snapshot.showName      = seg->showName;
    m_snapshot.showLength    = seg->showLength;
    m_snapshot.visible       = seg->visible;
    m_snapshot.role          = static_cast<int>(seg->role);
    m_snapshot.lengthFormula = seg->lengthFormula;
    m_snapshot.color         = seg->color;
    m_snapshot.tension       = seg->tension;
    if (const auto* ep = block->findPoint(seg->endPointId)) {
        m_snapshot.distance = ep->distance;
        m_snapshot.distanceFormula = ep->distanceFormula;
        m_snapshot.angle = ep->angle;
        m_snapshot.angleFormula = ep->angleFormula;
        m_snapshot.constraint = ep->constraint;
        m_snapshot.endPoint.name = ep->name;
        m_snapshot.endPoint.annotation = ep->annotation;
        m_snapshot.endPoint.showName = ep->showName;
    }
    if (const auto* sp = block->findPoint(seg->startPointId)) {
        m_snapshot.startPoint.name = sp->name;
        m_snapshot.startPoint.annotation = sp->annotation;
        m_snapshot.startPoint.showName = sp->showName;
    }
    m_snapshot.lineStyle     = static_cast<int>(seg->lineStyle);
    m_snapshot.weight        = seg->weight;
    m_snapshot.lengthAuto    = block->lengthAuto;

    m_snapshot.endTargetBlockId = block->endTargetBlockId;
    m_snapshot.endTargetPointId = block->endTargetPointId;
    m_snapshot.endTargetOffset  = block->endTargetOffset;
    m_snapshot.endTargetOffsetFormula = block->endTargetOffsetFormula;
}

bool LinePropertySession::commit(cad::param::ParamDocument* doc,
                                 const QUuid& blockId,
                                 const QUuid& segmentId,
                                 bool isCreation)
{
    if (!doc || isCreation) return false;

    cad::cmd::SetLinePropertiesCommand::Props oldProps;
    oldProps.name = m_snapshot.segName;
    oldProps.annotation = m_snapshot.segAnnotation;
    oldProps.role = static_cast<cad::param::SegmentRole>(m_snapshot.role);
    oldProps.showName = m_snapshot.showName;
    oldProps.showLength = m_snapshot.showLength;
    oldProps.visible = m_snapshot.visible;
    oldProps.color = m_snapshot.color;
    oldProps.lineStyle = static_cast<cad::param::LineStyle>(m_snapshot.lineStyle);
    oldProps.weight = m_snapshot.weight;
    oldProps.lengthAuto = m_snapshot.lengthAuto;
    oldProps.lengthFormula = m_snapshot.lengthFormula;
    oldProps.distance = m_snapshot.distance;
    oldProps.distanceFormula = m_snapshot.distanceFormula;
    oldProps.startName = m_snapshot.startPoint.name;
    oldProps.startAnno = m_snapshot.startPoint.annotation;
    oldProps.startShowName = m_snapshot.startPoint.showName;
    oldProps.endName = m_snapshot.endPoint.name;
    oldProps.endAnno = m_snapshot.endPoint.annotation;
    oldProps.endShowName = m_snapshot.endPoint.showName;

    cad::cmd::SetLinePropertiesCommand::Props newProps;
    if (const auto* b = doc->findBlock(blockId)) {
        if (const auto* s = b->findSegment(segmentId)) {
            newProps.name = s->name;
            newProps.annotation = s->annotation;
            newProps.role = s->role;
            newProps.showName = s->showName;
            newProps.showLength = s->showLength;
            newProps.visible = s->visible;
            newProps.color = s->color;
            newProps.lineStyle = s->lineStyle;
            newProps.weight = s->weight;
            newProps.lengthAuto = b->lengthAuto;
            newProps.lengthFormula = s->lengthFormula;
            if (const auto* ep = b->findPoint(s->endPointId)) {
                newProps.distance = ep->distance;
                newProps.distanceFormula = ep->distanceFormula;
                newProps.endName = ep->name;
                newProps.endShowName = ep->showName;
                newProps.endAnno = ep->annotation;
            }
            if (const auto* sp = b->findPoint(s->startPointId)) {
                newProps.startName = sp->name;
                newProps.startShowName = sp->showName;
                newProps.startAnno = sp->annotation;
            }
        }
    }

    if (oldProps != newProps && doc->undoStack()) {
        doc->undoStack()->push(new cad::cmd::SetLinePropertiesCommand(
            doc, blockId, segmentId, oldProps, newProps));
        return true;
    }
    return false;
}

void LinePropertySession::rollback(cad::param::ParamDocument* doc,
                                   const QUuid& blockId,
                                   const QUuid& segmentId,
                                   bool isCreation,
                                   SegmentAuxTab* auxTab)
{
    if (!doc) return;

    if (isCreation && doc->findBlock(blockId)) {
        doc->removeBlock(blockId);
        return;
    }

    auto* block = doc->findBlock(blockId);
    if (block) {
        auto* seg = block->findSegment(segmentId);
        if (seg) {
            seg->name = m_snapshot.segName;
            seg->annotation = m_snapshot.segAnnotation;
            seg->showName = m_snapshot.showName;
            seg->showLength = m_snapshot.showLength;
            seg->visible = m_snapshot.visible;
            seg->role = static_cast<cad::param::SegmentRole>(m_snapshot.role);
            seg->lengthFormula = m_snapshot.lengthFormula;
            seg->lineStyle = static_cast<cad::param::LineStyle>(m_snapshot.lineStyle);
            seg->weight = m_snapshot.weight;
            seg->color = m_snapshot.color;
            seg->tension = m_snapshot.tension;
            block->lengthAuto = m_snapshot.lengthAuto;

            if (auto* ep = block->findPoint(seg->endPointId)) {
                ep->distance = m_snapshot.distance;
                ep->distanceFormula = m_snapshot.distanceFormula;
                ep->angle = m_snapshot.angle;
                ep->angleFormula = m_snapshot.angleFormula;
                ep->constraint = m_snapshot.constraint;
                ep->refPointId = m_snapshot.refPointId;
                ep->name = m_snapshot.endPoint.name;
                ep->showName = m_snapshot.endPoint.showName;
                ep->annotation = m_snapshot.endPoint.annotation;
            }
            if (auto* sp = block->findPoint(seg->startPointId)) {
                sp->name = m_snapshot.startPoint.name;
                sp->showName = m_snapshot.startPoint.showName;
                sp->annotation = m_snapshot.startPoint.annotation;
            }
        }

        block->endTargetBlockId = m_snapshot.endTargetBlockId;
        block->endTargetPointId = m_snapshot.endTargetPointId;
        block->endTargetOffset  = m_snapshot.endTargetOffset;
        block->endTargetOffsetFormula = m_snapshot.endTargetOffsetFormula;
    }

    doc->restoreFollowerAttachment(blockId, m_snapshot.followerAtt);

    if (auxTab)
        auxTab->restoreSnapshots();
}

} // namespace cad::ui
