#pragma once

#include <vector>
#include <QPainterPath>
#include <QPointF>
#include <QUuid>

#include "BlockGeometryCache.h"

class CanvasScene;

namespace BlockItemPick {

/// Pick tolerance in scene units: screen px ÷ view zoom.
[[nodiscard]] double computeHoverThreshold(const CanvasScene* scene);

/// Precise pick region: strokes around segments/points with a screen-space tolerance.
[[nodiscard]] QPainterPath buildShape(const std::vector<LineCache>& lines,
                                      const std::vector<PointCache>& points,
                                      double tol, double pxToLocal);

/// Hit-test: returns nearest LINE entity ID within threshold, or null QUuid.
[[nodiscard]] QUuid hitTestLines(const std::vector<LineCache>& lines,
                                 const QPointF& localPos, double threshold,
                                 double* bestDistOut = nullptr);

/// Hit-test: nearest POINT within radius (screen-derived scene units).
[[nodiscard]] QUuid hitTestPoints(const std::vector<PointCache>& points,
                                  const QPointF& localPos, double radius);

} // namespace BlockItemPick
