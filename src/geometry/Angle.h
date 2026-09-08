#pragma once

#include <cmath>
#include <numbers>
#include <algorithm>
#include "geometry/Epsilon.h"

namespace cad::geo {

inline constexpr double kPi = std::numbers::pi;

// ─── Degree / Radian conversion ───────────────────────────────────────────────

/// Convert degrees to radians.
constexpr double degToRad(double deg) { return deg * kPi / 180.0; }

/// Convert radians to degrees.
constexpr double radToDeg(double rad) { return rad * 180.0 / kPi; }

// ─── Angle normalization ──────────────────────────────────────────────────────

/// Normalize an angle in degrees to the range (-180, 180].
/// Correctly maps +180 → +180 (not -180).
///
/// This is the 带符号折角 display convention (v3 定稿): 折叠 0° / 垂直 ±90° /
/// 开平 ±180°, 符号 = 折向 (alpha > 180° → alpha − 360°). Formerly duplicated
/// as signedFoldDeg in ToolRotate/ConnectGesture/SegmentConnectionCard.
inline double normalizeDeg180(double deg)
{
    if (!std::isfinite(deg)) return 0.0;
    double a = std::fmod(deg, 360.0);
    if (a >  180.0) a -= 360.0;
    if (a <= -180.0) a += 360.0;
    return a;
}

/// Normalize an angle in degrees to the storage domain [0, 360) (存储域 α —
/// followerAngle / serialization). Formerly duplicated as
/// alphaFromSignedFold in ToolRotate/ConnectGesture/SegmentConnectionCard.
inline double normalizeDeg360(double deg)
{
    if (!std::isfinite(deg)) return 0.0;
    double a = std::fmod(deg, 360.0);
    if (a < 0.0) a += 360.0;
    return a;
}

/// Normalize an angle in radians to the range (-π, π].
inline double normalizeRad(double rad)
{
    if (!std::isfinite(rad)) return 0.0;
    double a = std::fmod(rad, 2.0 * kPi);
    if (a >  kPi) a -= 2.0 * kPi;
    if (a <= -kPi) a += 2.0 * kPi;
    return a;
}

// ─── 角度显示角色（2026-09 旋转/角度统一 — 显示与输入的单一映射入口）──────────
//
// 同一个角度数在不同物理量下的显示域不同，角色决定域：
//   · WorldDirection — 世界方向角（线段朝向 / 基准方向）→ [0, 360)
//   · FoldPose       — 折角（连接线跟随角，开平基准）→ (−180, 180]
//   · DeltaAmount    — 增量（本次转了多少）→ 带符号不折叠，≈0 视为无增量
//
// 显示 / 输入口一律经 toDisplayDeg 取域；禁止各自调 normalizeDeg180/360
// （2026-09 统一前有 4 处各自选域，同一姿态在条带与属性卡显示两个数字）。
enum class AngleDisplayRole { WorldDirection, FoldPose, DeltaAmount };

/// 按角色把角度（度）换算到显示域。弦长反算属数学必需，仍走折角域，
/// 不经此函数（见 degToChordMm / chordMmToDeg）。
inline double toDisplayDeg(double deg, AngleDisplayRole role)
{
    switch (role) {
    case AngleDisplayRole::WorldDirection: return normalizeDeg360(deg);
    case AngleDisplayRole::FoldPose:       return normalizeDeg180(deg);
    case AngleDisplayRole::DeltaAmount:    return std::isfinite(deg) ? deg : 0.0;
    }
    return deg;
}

// ─── Arc length ↔ angle conversion (闭合基准, 2026-08 定稿) ────────────────────

/// Convert an arc length (mm) on a circle of the given radius (mm) to the
/// subtended angle in degrees. The closed-base mapping: 弧长 0 = 0° 折叠,
/// πr = 180° 开平. Returns 0 for a degenerate (≤ cad::geo::kGeomEps) radius.
inline double arcMmToDeg(double arcMm, double radiusMm)
{
    return (radiusMm > cad::geo::kGeomEps) ? (arcMm / radiusMm) * 180.0 / kPi : 0.0;
}

/// Convert an angle in degrees to the arc length (mm) on a circle of the
/// given radius (mm). Inverse of arcMmToDeg.
inline double degToArcMm(double deg, double radiusMm)
{
    return deg * kPi / 180.0 * radiusMm;
}

// ─── Chord length ↔ angle conversion (直线弦长/开度模式) ──────────────────────

/// Convert a chord length (mm) on a circle of the given radius (mm) to the
/// subtended angle in degrees.
/// chord = 2 * r * sin(theta / 2)  =>  theta = 2 * asin(chord / (2 * r))
/// Returns 0 for degenerate radius (<= cad::geo::kGeomEps).
inline double chordMmToDeg(double chordMm, double radiusMm)
{
    if (radiusMm <= cad::geo::kGeomEps) return 0.0;
    const double ratio = std::clamp(std::abs(chordMm) / (2.0 * radiusMm), 0.0, 1.0);
    const double deg = 2.0 * std::asin(ratio) * 180.0 / kPi;
    return (chordMm < 0.0) ? -deg : deg;
}

/// Convert an angle in degrees to the chord length (mm) on a circle of the
/// given radius (mm). Inverse of chordMmToDeg.
inline double degToChordMm(double deg, double radiusMm)
{
    if (radiusMm <= cad::geo::kGeomEps) return 0.0;
    const double rad = std::abs(deg) * kPi / 180.0;
    const double chord = 2.0 * radiusMm * std::sin(rad * 0.5);
    return (deg < 0.0) ? -chord : chord;
}

} // namespace cad::geo
