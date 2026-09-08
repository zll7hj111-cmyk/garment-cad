#pragma once

#include "canvas/CanvasStyle.h"  // kHoverRadiusPx（画布悬停半径，唯一数值来源）
#include "geometry/Vec2.h"

// 交互容差中央常量（2026-12 审计 P1-3 / §5.2 N2 收口）
//
// 铁律：
//   · 常量名必须带单位后缀 —— Px（屏幕像素，使用处自行 ÷ zoom）、Mm（世界毫米）、
//     Deg（度）。禁止在调用点写裸数字或自造局部 constexpr。
//   · 画布侧的悬停/拾取半径属于 canvas 层（CanvasStyle::hoverRadiusPx /
//     BlockItemPick.h），本层只读取不复制（canvas 不得依赖 tools）。
//   · 角度换算一律走 geometry/Angle.h（kPi / degToRad / normalizeDeg*）。

namespace cad::tools {

/// 线身吸附半径 (px) —— 与画布悬停半径同源：高亮什么就吸附什么。
/// 数值唯一来源在 canvas 层 CanvasStyle.h 的 kHoverRadiusPx，此处只做别名。
inline constexpr double kBodySnapRadiusPx = kHoverRadiusPx;

/// 点/端点吸附半径 (px)：端点目标比线身小，抓取半径刻意放宽（SnapEngine 的
/// snapRadius 默认值来源）。
inline constexpr double kPointSnapRadiusPx = 12.0;

/// 连接手势目标吸附 / 光环 / 目标环半径 (px)。WYSIWYG 铁律：
/// 光环 = 目标环 = 吸附范围（DECISIONS.md:21 用户拍板 2026-09）。
inline constexpr double kConnectSnapRadiusPx = 7.5;

/// 连接手势源端点抓取半径 (px)。抓取必须好抓（比落点宽松），落点必须精确
/// （DECISIONS.md:21 用户拍板 2026-09）。
inline constexpr double kConnectGrabRadiusPx = 10.0;

/// 连接重叠消歧：源端口标记半径 (px)。
inline constexpr double kSourcePortRadiusPx = 5.0;

/// 旋转对齐吸附搜索半径 (px)。
inline constexpr double kAimSearchRadiusPx = 15.0;

/// 旋转对齐吸附角容差 (deg)。
inline constexpr double kAimAlignTolDeg = 2.0;

/// 选择工具拖动阈值 (px)：press 未选中块（DECISIONS.md:27 用户拍板 2026-09）。
inline constexpr double kDragThresholdPx = 5.0;

/// 选择工具拖动阈值 (px)：press 已选中块（选集已建立时误拖代价更高）。
inline constexpr double kDragThresholdSelectedPx = 10.0;

/// 同点堆叠判定容差 (mm)：智能笔落点确认 / 连接手势重叠消歧 / 重叠候选去重。
/// 两个捕捉候选的世界坐标距离小于此值时视为「落在同一点」（唯一共享来源）。
inline constexpr double kOverlapEpsMm = 0.5;

/// 水平/垂直测量判定轴重合的零容差 (mm)：跨度低于 0.1mm 显示精度时会读成
/// 0.00 cm，此时拒绝发布无意义的测量。
inline constexpr double kAxisZeroEpsMm = 0.05;

/// 单一「点击 vs 拖动」判定（2026-12 审计 U9 / TOOL-P0-6、TOOL-P0-7 收口）：
/// 位移超过拖动阈值（屏幕像素 ÷ zoom 换算世界单位）才算拖动。
/// @param selectionEstablished 拖动/复制对象来自已建立的选集 —— 阈值取
///        kDragThresholdSelectedPx，与 SelectHoverFeedback 的 press 阈值同源；
///        否则取 kDragThresholdPx。zoom 无效时按 1.0 处理。
/// 原三处各写一份：ToolSelect.cpp 4.0/zoom、CopyDragController.cpp 5.0/zoom、
/// SelectDragController.cpp 绝对 1e-10 mm²（与 zoom 无关，量纲错）。
[[nodiscard]] inline bool isDrag(const cad::geo::Vec2& delta, double zoom,
                                 bool selectionEstablished)
{
    const double z = cad::canvas::safeZoomOr(zoom);
    const double thresholdPx =
        selectionEstablished ? kDragThresholdSelectedPx : kDragThresholdPx;
    return delta.length() > thresholdPx / z;
}

}  // namespace cad::tools
