#pragma once

// 几何/数值容差单一来源（2026-12 审计 P1-4 收口）。
// 约定：容差按量级成梯，均为绝对值；部分调用点比较的是平方量
// （distanceSquaredTo / lengthSquared / 叉积行列式），此时常量即平方阈值。
// 非几何语义的阈值（缩放守卫、显示去抖、有限差分步长、以「度」为单位的
// 角度容差）不在此表内，保留在各自调用点。

namespace cad::geo {

/// 松：参数域 t/s 有效性、投影包含判定、点去重（1e-6）。
inline constexpr double kGeomEpsLoose = 1e-6;

/// 默认：长度/坐标相等判定（1e-9）。
inline constexpr double kGeomEps = 1e-9;

/// 更紧：弦长/半径等长度守卫（1e-10）。
inline constexpr double kGeomEpsUltra = 1e-10;

/// 最紧：平方长度/叉积行列式判定（1e-12）。
inline constexpr double kGeomEpsTight = 1e-12;

/// 平方量纲：长度² 容差与相对缩放的细尺度容差（1e-8）。
inline constexpr double kGeomEpsSq = 1e-8;

}  // namespace cad::geo
