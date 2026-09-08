#include "BlockItemPick.h"
#include "CanvasScene.h"
#include "CanvasStyle.h"
#include "parametric/PerfProbe.h"

#include <QPainterPathStroker>
#include <algorithm>
#include <cmath>
#include "geometry/Epsilon.h"
#include "geometry/Vec2.h"

namespace BlockItemPick {

double computeHoverThreshold(const CanvasScene* scene)
{
    double threshold = CanvasStyle::fallback().hoverRadiusPx();
    if (scene) {
        threshold = scene->style()->hoverRadiusPx();
        const qreal m11 = scene->currentZoom();
        if (std::abs(m11) > cad::geo::kGeomEps)
            threshold /= std::abs(m11);
    }
    return threshold;
}

QPainterPath buildShape(const std::vector<LineCache>& lines,
                        const std::vector<PointCache>& points,
                        double tol, double pxToLocal)
{
    QPainterPath path;
    QPainterPathStroker stroker;
    stroker.setWidth(tol * 2.0);
    stroker.setCapStyle(Qt::RoundCap);
    GCAD_PERF_SCOPE("shape.rebuild");
    for (const auto& lc : lines) {
        QPainterPath seg;
        seg.moveTo(lc.p1);
        seg.lineTo(lc.p2);
        path.addPath(stroker.createStroke(seg));
    }
    // Curves contribute through their OWN child items (CurveItem::shape) —
    // the framework hit-tests them independently, so the block shape only
    // covers lines and points.
    // Points contribute only their visual disc (they are tiny; the segment
    // band already covers their surroundings for block-level picking).
    // PICK radius 见 BlockItemPick.h 常量：普通点 2.5px / 放置点 6px ——
    // 刻意大于 0.8 的可视半径，保证好抓。
    for (const auto& pc : points) {
        const double rPx = pc.isPlaced ? kPlacedPointPickRadiusPx
                                       : kPointPickRadiusPx;
        const double r = rPx * pxToLocal;
        path.addEllipse(pc.pos, r, r);
    }

    return path;
}

QUuid hitTestLines(const std::vector<LineCache>& lines,
                   const QPointF& localPos, double threshold,
                   double* bestDistOut)
{
    // Points are deliberately NOT hover targets: they are tiny snap anchors,
    // and every point interaction (double-click edit, pen-tool connection via
    // SnapEngine) goes through segments or the document directly. Hovering
    // only ever highlights segments — no contention at endpoints.
    // Curves are hit-tested by their OWN child items (CurveItem::shape).
    double bestDist = threshold;
    QUuid bestId;

    // Test line segments.
    for (const auto& lc : lines) {
        // Distance from point to line segment (U5: shared geometry helper).
        const cad::geo::Vec2 a{lc.p1.x(), lc.p1.y()};
        const cad::geo::Vec2 b{lc.p2.x(), lc.p2.y()};
        const cad::geo::Vec2 p{localPos.x(), localPos.y()};
        const double dist = cad::geo::Vec2::distanceToSegment(p, a, b);

        if (dist < bestDist) {
            bestDist = dist;
            bestId = lc.id;
        }
    }

    if (bestDistOut)
        *bestDistOut = bestDist;
    return bestId;
}

QUuid hitTestPoints(const std::vector<PointCache>& points,
                    const QPointF& localPos, double radius)
{
    QUuid best;
    double bestDistSq = radius * radius;
    for (const auto& pc : points) {
        const double dx = pc.pos.x() - localPos.x();
        const double dy = pc.pos.y() - localPos.y();
        const double d = dx * dx + dy * dy;
        if (d < bestDistSq) {
            bestDistSq = d;
            best = pc.id;
        }
    }
    return best;
}

} // namespace BlockItemPick
