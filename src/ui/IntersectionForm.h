#pragma once

#include <QWidget>

class ElaLineEdit;
class ElaCheckBox;
class ElaText;
class ElaPushButton;

namespace cad::param { struct ParamPoint; }

namespace cad::tools {

/// Edit form for an Intersection point's parameters:
/// 名称 / 射线起点 / 射线角度(可切换绝对角度) / 显示名称.
/// Used by the "辅助点" tab of LinePropertyDialog (intersection points are
/// listed together with auxiliary points).
///
/// Angle reference: by default interAngle is relative to the
/// target segment's start→end direction. When "绝对角度" is checked, it is
/// stored as an absolute world angle (persisted) and stays world-anchored.
/// The checkbox is persisted via interUseWorldAngle.
///
/// Dumb view: loadFrom() fills fields, applyTo() writes back.
class IntersectionForm : public QWidget
{
    Q_OBJECT

public:
    explicit IntersectionForm(QWidget* parent = nullptr);

    /// Set the target segment's current WORLD direction (degrees). Used to
    /// convert between relative and world angle representations.
    void setSegmentWorldDir(double deg) { m_segWorldDir = deg; }

    /// Fill all fields from the point (signals blocked while populating).
    void loadFrom(const cad::param::ParamPoint& pt);

    /// Write the current field values into the point. Numeric input clears
    /// the matching formula; non-numeric non-empty input becomes the formula.
    /// When 绝对角度 mode is on, the numeric angle is back-calculated to the
    /// segment-relative value before storing.
    void applyTo(cad::param::ParamPoint& pt) const;

    /// Set the read-only origin label (ray origin point name/serial).
    void setOriginLabel(const QString& text);

    /// Set the read-only aim-point (指向点) label; empty text hides the link
    /// ("—") and disables the clear button.
    void setAimLabel(const QString& text);

signals:
    /// A field was committed (editingFinished / checkbox).
    void edited();
    /// Text changed — the owner should (re)start its debounce timer.
    void dirty();
    /// The user cleared the aim-point link — the owner should clear
    /// interAimPointId and re-apply (fallback to the stored angle).
    void aimCleared();

private:
    void onWorldAngleToggled(bool checked);

    ElaLineEdit* m_editName      = nullptr;
    ElaText*   m_lblOrigin     = nullptr;   ///< Read-only: ray origin point.
    ElaLineEdit* m_editAngle     = nullptr;   ///< Angle (deg) or formula.
    ElaCheckBox* m_chkWorldAngle = nullptr;   ///< Interpret angle field as world angle.
    ElaCheckBox* m_chkShowName   = nullptr;
    ElaText*   m_lblAim        = nullptr;   ///< Read-only: aim point (指向点).
    ElaPushButton* m_btnClearAim = nullptr;   ///< Drop the aim link (back to angle mode).

    double m_segWorldDir = 0.0;             ///< Target segment world direction (deg).
};

} // namespace cad::tools
