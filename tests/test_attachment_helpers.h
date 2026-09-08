#pragma once

#include <QtTest>
#include <QUuid>
#include <QUndoStack>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/ExpressionEvaluator.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/VariableCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/LayerCommands.h"
#include "geometry/Vec2.h"
#include "geometry/CurveMath.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "parametric/FollowerAngle.h"
#include "document/DocumentSerializer.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

/// Test convenience: stable id of the display layer at @p row.
inline QUuid layerIdAt(const cad::param::ParamDocument& doc, int row)
{
    const auto& ls = doc.layers();
    return (row >= 0 && row < static_cast<int>(ls.size()))
        ? ls[static_cast<size_t>(row)].id : QUuid();
}

/// Create a minimal horizontal line block and add it to the document.
struct LineSetup {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

inline LineSetup makeLine(ParamDocument& doc, double lenMm, const Vec2& origin = Vec2::zero())
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
    return {blockId, startId, endId, segId};
}

} // namespace
