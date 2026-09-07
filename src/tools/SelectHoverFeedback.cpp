#include "SelectHoverFeedback.h"

#include "parametric/Block.h"
#include "parametric/DomainViews.h"

#include <algorithm>

namespace cad::tools {

namespace {

constexpr double kConnectGrabRadius = 10.0;  ///< 端点悬停抓取半径 (px).

}

void SelectHoverFeedback::beginPending(const cad::geo::Vec2& pos,
                                       const QUuid& blockId, bool wasSelected)
{
    m_pending = true;
    m_blockId = blockId;
    m_wasSelected = wasSelected;
    m_pos = pos;
}

void SelectHoverFeedback::cancelPending()
{
    m_pending = false;
    m_blockId = QUuid();
    m_wasSelected = false;
}

double SelectHoverFeedback::thresholdUserUnits(double zoom) const
{
    return (m_wasSelected ? kDragThresholdSelectedPx : kDragThresholdPx)
           / (zoom > 1e-9 ? zoom : 1.0);
}

Qt::CursorShape SelectHoverFeedback::cursorShapeFor(
    cad::param::ParamDocument* doc, const QUuid& blockHit,
    const cad::geo::Vec2& pos, double zoom, bool ctrlHeld) const
{
    if (ctrlHeld && !blockHit.isNull()) return Qt::DragCopyCursor;
    const double worldR = kConnectGrabRadius / (zoom > 1e-9 ? zoom : 1.0);
    if (findEndpointNear(doc, pos, worldR))
        return Qt::CrossCursor;
    if (!blockHit.isNull())
        return Qt::OpenHandCursor;
    return Qt::ArrowCursor;
}

bool SelectHoverFeedback::blockHasEndpointNear(cad::param::ParamDocument* doc,
                                               const QUuid& blockId,
                                               const cad::geo::Vec2& pos,
                                               double worldRadius) const
{
    const auto* blk = doc ? doc->blocksView().byId(blockId) : nullptr;
    if (!blk) return false;
    const double rSq = worldRadius * worldRadius;
    for (const auto& pt : blk->points) {
        if (!pt.selectable || !pt.resolved) continue;
        if (blk->worldPos(pt.id).distanceSquaredTo(pos) < rSq)
            return true;
    }
    return false;
}

bool SelectHoverFeedback::findEndpointNear(cad::param::ParamDocument* doc,
                                           const cad::geo::Vec2& pos,
                                           double worldRadius,
                                           cad::geo::Vec2* outPos,
                                           QUuid* outBlockId,
                                           QUuid* outPointId) const
{
    if (!doc) return false;
    const double rSq = worldRadius * worldRadius;
    const QUuid activeLayer = doc->layersView().activeLayer();
    for (const auto& blk : doc->blocks()) {
        if (blk.isBridge) continue;
        if (blk.layer != activeLayer) continue;
        for (const auto& seg : blk.segments) {
            for (const QUuid& pid : {seg.startPointId, seg.endPointId}) {
                const auto* pt = blk.findPoint(pid);
                if (!pt || !pt->selectable || !pt->resolved) continue;
                const cad::geo::Vec2 wpt = blk.worldPos(pid);
                if (wpt.distanceSquaredTo(pos) < rSq) {
                    if (outPos) *outPos = wpt;
                    if (outBlockId) *outBlockId = blk.id;
                    if (outPointId) *outPointId = pid;
                    return true;
                }
            }
        }
    }
    return false;
}

void SelectHoverFeedback::resetCursor()
{
    m_cursor = Qt::ArrowCursor;
}

} // namespace cad::tools
