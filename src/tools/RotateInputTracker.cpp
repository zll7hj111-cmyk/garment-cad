#include "tools/RotateInputTracker.h"

#include "canvas/CanvasScene.h"
#include "canvas/overlay/TransientOverlay.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "geometry/Epsilon.h"
#include "tools/InteractionTolerances.h"  // kPointSnapRadiusPx

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
    const double zoom = scene->safeZoom();
    const double tol = kPointSnapRadiusPx / zoom;
    double bestDist = tol;
    cad::geo::Vec2 bestPt;
    bool found = false;

    // 旋转中心吸附候选 = 所有「可见且可选」的已解析点, 而不是只扫段端点。
    // 圆段的圆心既不是 startPointId 也不是 endPointId (端点接缝在弧上),
    // 只扫端点会永远吸不到圆心 (用户报告 m01094 ⑤: 「选择旋转中心也捕捉不到
    // 圆心」)。端点本身仍在候选里, 既有「吸附到端点」的断言不受影响。
    for (const auto& blk : doc->blocks()) {
        for (const auto& p : blk.points) {
            if (!p.resolved || !p.selectable || !p.visible) continue;
            const cad::geo::Vec2 wpt = blk.worldPos(p.id);
            const double d = wpt.distanceTo(worldPos);
            if (d < bestDist) {
                bestDist = d;
                bestPt = wpt;
                found = true;
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
