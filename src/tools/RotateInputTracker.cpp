#include "tools/RotateInputTracker.h"

#include "canvas/CanvasScene.h"
#include "canvas/overlay/TransientOverlay.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"

namespace cad::tools {

void RotateInputTracker::hideHoverSnap()
{
    m_hoverSnapped = false;
    if (m_scene && m_scene->overlay()) {
        m_scene->overlay()->clear(cad::canvas::OverlayTier::Hover);
    }
}

void RotateInputTracker::updateHoverSnap(CanvasScene* scene,
                                        cad::param::ParamDocument* doc,
                                        const cad::geo::Vec2& worldPos)
{
    m_scene = scene;
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
        if (m_scene && m_scene->overlay()) {
            m_scene->overlay()->showSnapAim(bestPt);
        }
    } else {
        hideHoverSnap();
    }
}

void RotateInputTracker::teardown()
{
    hideHoverSnap();
    m_scene = nullptr;
}

} // namespace cad::tools
