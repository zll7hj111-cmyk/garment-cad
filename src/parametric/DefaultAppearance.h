#pragma once

#include <QColor>

/// 默认外观的模型层单一来源（2026-12 审计 P1-9）。
///
/// 新建线段的默认线宽与线色此前在四处各自写死：
///   - src/parametric/Segment.h（模型字段默认值）
///   - src/canvas/CanvasStyle.h / .cpp（EntityPaintParams + roleDefaults 线宽）
///   - src/document/DocumentSerializer.cpp（旧档缺省字段回退）
/// 任何一处改动而其余未跟随，同一条线在不同路径上就会呈现不同粗细 / 颜色。
/// 现在全部推导自本文件；本头文件只依赖 <QColor>，canvas 层可直接 include，
/// 不必把 Segment.h 拖进画布 TU。
namespace cad::param {

/// 默认线宽（cosmetic px）。
inline constexpr double kDefaultSegmentWeight = 1.2;

/// 默认线色 #1E1E1E 的 RGB 数值（近黑墨色）。
inline constexpr int kDefaultSegmentColorRgb = 0x1E1E1E;

/// 默认线色的小写十六进制（JSON 缺省字段回退用）。
inline constexpr char kDefaultSegmentColorHex[] = "#1e1e1e";  // color-allow: 数据层默认线色（模型层单一来源）

/// 默认线色。QColor 不是字面量类型，只能走函数。
[[nodiscard]] inline QColor defaultSegmentColor()
{
    // color-allow: 数据层默认线色（模型层单一来源）
    return QColor::fromRgb(0x1E, 0x1E, 0x1E);
}

} // namespace cad::param
