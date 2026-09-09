#include "tools/CircleFactory.h"

#include <QUndoStack>
#include <QString>

#include "document/commands/DocumentCommands.h"
#include "parametric/Block.h"
#include "parametric/ParamDocument.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"

namespace cad::tools {

CircleFactory::CircleFactory(cad::param::ParamDocument* doc, QUndoStack* undoStack)
    : m_paramDoc(doc)
    , m_undoStack(undoStack)
{
}

QUuid CircleFactory::createCircle(const Vec2& center, double radiusMm, double startAngleDeg)
{
    if (!m_paramDoc || !(radiusMm > 0.0)) return QUuid();

    cad::param::Block block;
    block.layer = m_paramDoc->layersView().activeLayer();
    block.lengthAuto = false;
    block.transform.origin = center;
    block.transform.rotation = 0.0;

    // Center: Free at the block origin (local (0,0)).
    cad::param::ParamPoint ptCenter;
    ptCenter.constraint = cad::param::PointConstraint::Free;
    ptCenter.freePos = cad::geo::Vec2::zero();
    const QUuid centerId = ptCenter.id;

    // Start / end: Polar around the center. The end angle is stored UNWRAPPED
    // (a0 + 360) — the resolver must never normalize it into [0,360) (D12/D18).
    cad::param::ParamPoint ptStart;
    ptStart.constraint = cad::param::PointConstraint::Polar;
    ptStart.refPointId = centerId;
    ptStart.distance = radiusMm;
    ptStart.angle = startAngleDeg;
    const QUuid startId = ptStart.id;

    cad::param::ParamPoint ptEnd;
    ptEnd.constraint = cad::param::PointConstraint::Polar;
    ptEnd.refPointId = centerId;
    ptEnd.distance = radiusMm;               // mirrored by syncCircleFitRadius()
    ptEnd.angle = startAngleDeg + 360.0;     // sweep authority
    // Same position as the start point: hide + unselect the duplicate handle.
    ptEnd.visible = false;
    ptEnd.selectable = false;
    const QUuid endId = ptEnd.id;

    // Segment: Bezier + fitKind=Circle, three interior anchors at the quadrant
    // positions (25% / 50% / 75% of the 360° sweep).
    cad::param::Segment seg;
    seg.type = cad::param::SegmentType::Bezier;
    seg.fitKind = cad::param::FitKind::Circle;
    seg.role = cad::param::SegmentRole::Auxiliary;   // D4: construction circle
    seg.startPointId = startId;
    seg.endPointId = endId;

    block.addPoint(std::move(ptCenter));
    block.addPoint(std::move(ptStart));
    block.addPoint(std::move(ptEnd));

    const double kAnchorPercents[] = {0.25, 0.50, 0.75};
    for (double percent : kAnchorPercents) {
        cad::param::ParamPoint anchor;
        anchor.constraint = cad::param::PointConstraint::CurveAnchor;
        anchor.hostSegmentId = seg.id;
        anchor.interpPercent = percent;
        anchor.interpOffsetDist = 0.0;   // unused by the circle-fit branch
        seg.passPointIds.push_back(anchor.id);
        block.addPoint(std::move(anchor));
    }
    block.addSegment(std::move(seg));

    const QUuid blockId = block.id;
    if (m_undoStack) {
        auto* cmd = new cad::cmd::DrawLineCommand(m_paramDoc, std::move(block));
        cmd->setText(QString::fromUtf8("画圆"));
        m_undoStack->push(cmd);
    } else {
        m_paramDoc->addBlock(std::move(block));
    }
    return blockId;
}

} // namespace cad::tools
