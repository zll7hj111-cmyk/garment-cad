#pragma once

#include <vector>
#include <QUuid>

#include "BlockGeometryCache.h"

class QPainter;
class QGraphicsItem;
class CanvasStyle;
class CanvasAnimator;

struct BlockPaintContext {
    QPainter* painter = nullptr;
    const CanvasStyle* style = nullptr;
    CanvasAnimator* animator = nullptr;
    const QGraphicsItem* animatorTargetItem = nullptr;
    const std::vector<LineCache>& lines;
    const std::vector<PointCache>& points;
    LayerMode layerMode = LayerMode::Normal;
    QUuid hoveredEntity;
    QUuid hoveredPointId;
    QUuid selectedPointId;
    QUuid leaderEntity;
    bool forceName = false;
    bool forceLen = false;
    bool dirArrows = true;
};

namespace BlockItemPainter {

void paint(const BlockPaintContext& ctx);

} // namespace BlockItemPainter
