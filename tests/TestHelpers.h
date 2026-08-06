#pragma once

/// @file TestHelpers.h
/// Shared test utilities for parametric CAD unit tests.
/// Header-only: no CMake source-list changes needed.

#include <QUuid>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Attachment.h"
#include "geometry/Vec2.h"

namespace cad::test {

using cad::param::ParamDocument;
using cad::param::Block;
using cad::param::ParamPoint;
using cad::param::Segment;
using cad::param::Attachment;
using cad::param::PointConstraint;
using cad::geo::Vec2;

/// Handles of a simple 2-point / 1-segment line block.
struct LineSetup {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

/// Create a horizontal line block: Free start at local (0,0), Polar end.
/// @param doc              Target document (block is added immediately).
/// @param lenMm            Segment length in mm.
/// @param origin           Block transform origin (default: zero).
/// @param pointDistFormula Optional distance formula on the END POINT (cm domain).
/// @param segLenFormula    Optional length formula on the SEGMENT (cm domain).
inline LineSetup makeLine(ParamDocument& doc, double lenMm,
                          Vec2 origin = Vec2::zero(),
                          const QString& pointDistFormula = {},
                          const QString& segLenFormula = {})
{
    Block block;
    block.transform.origin = origin;
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = lenMm;
    p2.angle = 0.0;
    p2.distanceFormula = pointDistFormula;
    QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    seg.lengthFormula = segLenFormula;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId, segId};
}

/// Create a bridge block pinned between two host points.
inline LineSetup makeBridge(ParamDocument& doc,
                            const QUuid& hostA, const QUuid& hostAPoint,
                            const QUuid& hostB, const QUuid& hostBPoint)
{
    const Vec2 startWorld = doc.findBlock(hostA)->worldPos(hostAPoint);
    const Vec2 endWorld   = doc.findBlock(hostB)->worldPos(hostBPoint);

    Block block;
    block.isBridge = true;
    block.transform.origin = startWorld;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;

    const Vec2 delta = endWorld - startWorld;
    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = delta.length();
    p2.angle = std::atan2(delta.y, delta.x) * 180.0 / M_PI;
    QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    doc.addBlock(std::move(block));

    Attachment pinStart;
    pinStart.isPin = true;
    pinStart.fromBlockId = blockId;
    pinStart.fromPointId = startId;
    pinStart.toBlockId = hostA;
    pinStart.toPointId = hostAPoint;

    Attachment pinEnd;
    pinEnd.isPin = true;
    pinEnd.fromBlockId = blockId;
    pinEnd.fromPointId = endId;
    pinEnd.toBlockId = hostB;
    pinEnd.toPointId = hostBPoint;

    doc.addAttachment(std::move(pinStart));
    doc.addAttachment(std::move(pinEnd));
    return {blockId, startId, endId, segId};
}

/// Resolved length (mm) of the first segment of a block in the document.
/// Returns -1.0 if the block/segment/points are missing or unresolved.
inline double segmentLength(const ParamDocument& doc, const QUuid& blockId)
{
    const Block* b = doc.findBlock(blockId);
    if (!b || b->segments.empty()) return -1.0;
    const Segment& seg = b->segments.front();
    const ParamPoint* sp = b->findPoint(seg.startPointId);
    const ParamPoint* ep = b->findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return -1.0;
    return sp->resolvedPos.distanceTo(ep->resolvedPos);
}

} // namespace cad::test
