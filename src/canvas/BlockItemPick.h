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

/// 普通点拾取半径 (px)：大于 0.8 的可视半径，保证好抓（PICK radius 统一口径）。
inline constexpr double kPointPickRadiusPx = 2.5;

/// 放置点 (isPlaced) 拾取半径 (px)：放置点本体更小，放宽到 6px。
inline constexpr double kPlacedPointPickRadiusPx = 6.0;

/// boundingRect 命中余量 (局部坐标)：必须覆盖拾取带 ——
/// hoverRadiusPx(8) ÷ ZOOM_MIN(0.2) = 40 + 余量；余量偏小会让 BSP 索引丢弃
/// 「只在带内、不在图元 bbox 内」的命中（低缩放不可选）。
inline constexpr double kPickMarginLocal = 42.0;

} // namespace BlockItemPick
