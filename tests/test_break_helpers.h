#include <QtTest>
#include <QUuid>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Component.h"
#include "document/commands/BreakCommands.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "tools/ToolBreak.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

/// Test convenience: stable id of the display layer at @p row.
QUuid layerIdAt(const cad::param::ParamDocument& doc, int row)
{
    const auto& ls = doc.layers();
    return (row >= 0 && row < static_cast<int>(ls.size()))
        ? ls[static_cast<size_t>(row)].id : QUuid();
}

/// Create a horizontal line block with an optional length formula.
struct LineSetup {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

LineSetup makeLine(ParamDocument& doc, double lenMm,
                   const QString& lengthFormula = {})
{
    Block block;
    block.transform.origin = Vec2::zero();
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
    seg.lengthFormula = lengthFormula;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId, segId};
}

/// Add an auxiliary (Interpolated) point on a segment.
QUuid addAuxPoint(ParamDocument& doc, const QUuid& blockId,
                  const QUuid& segId, double percent,
                  double constantMm = 0.0, bool fromEnd = false,
                  double offsetDist = 0.0)
{
    auto* block = doc.findBlock(blockId);
    auto* seg = block ? block->findSegment(segId) : nullptr;
    if (!block || !seg) return {};

    ParamPoint pt;
    pt.constraint = PointConstraint::Interpolated;
    pt.hostSegmentId = segId;
    pt.isAuxiliary = true;
    pt.interpPercent = percent;
    pt.interpConstant = constantMm;
    pt.interpFromEnd = fromEnd;
    pt.interpOffsetDist = offsetDist;

    QUuid ptId = pt.id;
    block->addPoint(std::move(pt));
    seg->auxPointIds.push_back(ptId);
    doc.resolveAll();
    return ptId;
}

/// Flatten a curve entry's spans (local) into a world-space polyline.
std::vector<Vec2> worldPolyline(const Block& b,
                                const CurveSpanEntry& e)
{
    std::vector<Vec2> out;
    for (const auto& s : e.spans) {
        cad::geo::BezierSpan ws;
        ws.p0 = b.transform.toWorld(s.p0);
        ws.ctrl1 = b.transform.toWorld(s.ctrl1);
        ws.ctrl2 = b.transform.toWorld(s.ctrl2);
        ws.p3 = b.transform.toWorld(s.p3);
        auto seg = cad::geo::flattenBezierSpans({ws}, 0.05);
        if (out.empty()) out.insert(out.end(), seg.begin(), seg.end());
        else out.insert(out.end(), seg.begin() + 1, seg.end());
    }
    return out;
}

/// Max deviation: for every point of A, the min distance to polyline B's
/// segments (one-directional Hausdorff).
double maxDeviation(const std::vector<Vec2>& a, const std::vector<Vec2>& b)
{
    double worst = 0.0;
    auto distToSeg = [](const Vec2& p, const Vec2& s0, const Vec2& s1) {
        const Vec2 d = s1 - s0;
        double l2 = d.lengthSquared();
        if (l2 < 1e-12) return p.distanceTo(s0);
        double t = std::clamp((p - s0).dot(d) / l2, 0.0, 1.0);
        return p.distanceTo(s0 + d * t);
    };
    for (const auto& p : a) {
        double best = 1e18;
        for (size_t i = 0; i + 1 < b.size(); ++i)
            best = std::min(best, distToSeg(p, b[i], b[i + 1]));
        worst = std::max(worst, best);
    }
    return worst;
}

/// Build a curve block: Free start (local 0,0) → Polar end, Bezier segment
/// with two CurveAnchor pass points.
struct CurveSetup {
    QUuid blockId;
    QUuid segId;
    QUuid pp1Id;
    QUuid pp2Id;
};

CurveSetup makeCurve(ParamDocument& doc)
{
    Block block;
    block.transform.origin = Vec2(30.0, -40.0);
    block.transform.rotation = 25.0 * M_PI / 180.0;

    ParamPoint sp;
    sp.constraint = PointConstraint::Free;
    sp.freePos = Vec2::zero();
    QUuid spId = sp.id;

    ParamPoint ep;
    ep.constraint = PointConstraint::Polar;
    ep.refPointId = spId;
    ep.distance = 120.0;
    ep.angle = 0.0;
    QUuid epId = ep.id;

    block.addPoint(std::move(sp));
    block.addPoint(std::move(ep));

    Segment seg;
    seg.type = SegmentType::Bezier;
    seg.startPointId = spId;
    seg.endPointId = epId;
    seg.tension = 0.0;
    QUuid segId = seg.id;

    ParamPoint pp1;
    pp1.constraint = PointConstraint::CurveAnchor;
    pp1.hostSegmentId = segId;
    pp1.interpPercent = 0.33;
    pp1.interpOffsetDist = 18.0;
    pp1.autoTangent = true;
    QUuid pp1Id = pp1.id;

    ParamPoint pp2;
    pp2.constraint = PointConstraint::CurveAnchor;
    pp2.hostSegmentId = segId;
    pp2.interpPercent = 0.66;
    pp2.interpOffsetDist = -14.0;
    pp2.autoTangent = true;
    QUuid pp2Id = pp2.id;

    block.addPoint(std::move(pp1));
    block.addPoint(std::move(pp2));
    seg.passPointIds = {pp1Id, pp2Id};
    QUuid blockId = block.id;
    block.addSegment(std::move(seg));
    doc.addBlock(std::move(block));
    doc.resolveAll();
    return {blockId, segId, pp1Id, pp2Id};
}

} // namespace
