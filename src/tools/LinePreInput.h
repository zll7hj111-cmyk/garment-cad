#pragma once

#include <QString>

namespace cad::tools {

/// One-shot pre-input for the NEXT line the smart pen creates (预输入).
/// Typed in the status-bar pre-input strip while the smart pen is active;
/// the values are consumed by the next committed line and then cleared.
/// Length is in cm and angle in degrees — both accept numbers or formulas,
/// matching the SegmentEditBar semantics.
/// 独立小头 (阶段 3): ToolSmartPen 与 SmartPenStrokeInput 共用, 避免互相 include。
struct LinePreInput
{
    QString name;      ///< Segment name for the next line.
    QString lengthCm;  ///< Length in cm: number or formula.
    QString angleDeg;  ///< Angle in degrees: number or formula.

    /// Angle display convention (与 HUD/闭合基准一致): with a snapped start
    /// it is the follower fold angle (0° = 折叠重叠, 180° = 直行延续);
    /// with a free start it is the absolute world angle (0~360° CCW).
    [[nodiscard]] bool hasLength() const { return !lengthCm.trimmed().isEmpty(); }
    [[nodiscard]] bool hasAngle() const { return !angleDeg.trimmed().isEmpty(); }
};

} // namespace cad::tools
