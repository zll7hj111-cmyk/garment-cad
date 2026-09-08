#include "BlockItemPick.h"
#include "CanvasScene.h"
#include "CanvasStyle.h"
#include "parametric/PerfProbe.h"

#include <QPainterPathStroker>
#include <algorithm>
#include <cmath>

namespace BlockItemPick {

double computeHoverThreshold(const CanvasScene* scene)
{
    double threshold = 8.0;  // default
    if (scene) {
        threshold = scene->style()->hoverRadiusPx();
        const qreal m11 = scene->currentZoom();
        if (std::abs(m11) > 1e-9)
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
    // PICK radius is unified at 2.5 for ALL point kinds — deliberately larger
    // than the 0.8 visual radius so grabbing stays finger-friendly.
    for (const auto& pc : points) {
        const double rPx = pc.isPlaced ? 6.0 : 2.5;
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
        // Distance from point to line segment.
        const double ax = lc.p1.x(), ay = lc.p1.y();
        const double bx = lc.p2.x(), by = lc.p2.y();
        const double px = localPos.x(), py = localPos.y();

        const double abx = bx - ax, aby = by - ay;
        const double apx = px - ax, apy = py - ay;
        const double lenSq = abx * abx + aby * aby;

        double t = 0.0;
        if (lenSq > 1e-12)
            t = std::clamp((apx * abx + apy * aby) / lenSq, 0.0, 1.0);

        const double cx = ax + t * abx - px;
        const double cy = ay + t * aby - py;
        const double dist = std::sqrt(cx * cx + cy * cy);

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
