#include "tools/RotateInputTracker.h"

#include <QGraphicsEllipseItem>
#include <QPen>

#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"

namespace cad::tools {

void RotateInputTracker::ensureHoverSnapRing(CanvasScene* scene)
{
    if (m_hoverSnapRing || !scene) return;
    const double zoom = scene->currentZoom() > 1e-9 ? scene->currentZoom() : 1.0;
    m_hoverSnapRing = scene->addEllipse(0, 0, 12, 12);
    m_hoverSnapRing->setBrush(Qt::NoBrush);
    QPen pen(QColor(250, 204, 21), 2.0 / zoom);
    m_hoverSnapRing->setPen(pen);
    m_hoverSnapRing->setZValue(1000);
    m_hoverSnapRing->setVisible(false);
    m_managed.own(m_hoverSnapRing, &m_hoverSnapRing);
}

void RotateInputTracker::hideHoverSnap()
{
    m_hoverSnapped = false;
    if (m_hoverSnapRing) {
        m_hoverSnapRing->setVisible(false);
    }
}

void RotateInputTracker::updateHoverSnap(CanvasScene* scene,
                                        cad::param::ParamDocument* doc,
                                        const cad::geo::Vec2& worldPos)
{
    if (!scene || !doc) {
        hideHoverSnap();
        return;
    }
    const double zoom = scene->currentZoom() > 1e-9 ? scene->currentZoom() : 1.0;
    const double tol = 12.0 / zoom;
    double bestDist = tol;
    cad::geo::Vec2 bestPt;
    bool found = false;

    for (const auto& blk : doc->blocks()) {
        for (const auto& seg : blk.segments) {
            for (const QUuid& pid : {seg.startPointId, seg.endPointId}) {
                const auto* p = blk.findPoint(pid);
                if (p && p->resolved) {
                    const cad::geo::Vec2 wpt = blk.worldPos(pid);
                    const double d = wpt.distanceTo(worldPos);
                    if (d < bestDist) {
                        bestDist = d;
                        bestPt = wpt;
                        found = true;
                    }
                }
            }
        }
    }

    if (found) {
        m_hoverSnapped = true;
        m_hoverSnapPoint = bestPt;
        ensureHoverSnapRing(scene);
        const double r = 6.0 / zoom;
        m_hoverSnapRing->setRect(bestPt.x - r, bestPt.y - r, 2 * r, 2 * r);
        QPen pen(QColor(250, 204, 21), 2.0 / zoom);
        m_hoverSnapRing->setPen(pen);
        m_hoverSnapRing->setVisible(true);
    } else {
        hideHoverSnap();
    }
}

void RotateInputTracker::teardown()
{
    hideHoverSnap();
    m_managed.clear();
    m_hoverSnapRing = nullptr;
}

} // namespace cad::tools
