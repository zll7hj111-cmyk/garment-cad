#pragma once

#include <QWidget>
#include <QUuid>
#include <vector>
#include <utility>

class QLineEdit;
class QComboBox;
class QCheckBox;

namespace cad::param { struct ParamPoint; }

namespace cad::tools {

/// Shared edit form for an auxiliary (Interpolated) point's parameters:
/// 名称 / 计算方向 / 百分比 / 常量 / 显示名称.
/// Used by the "辅助点" tab of LinePropertyDialog and by QuickAuxDialog
/// (smart-pen click on a segment body) so both entries stay in sync.
///
/// The form is a dumb view: loadFrom() fills the fields from a ParamPoint,
/// applyTo() writes them back (formula-vs-number dispatch, cm→mm conversion).
/// The owner decides when to apply (debounce / dialog accept).
class AuxPointForm : public QWidget
{
    Q_OBJECT

public:
    explicit AuxPointForm(QWidget* parent = nullptr);

    /// Refresh the direction combo's item labels so they show the CURRENT
    /// start/end endpoint serials+names (helps the user tell which physical
    /// endpoint is the "start" vs the "end"). Either pointer may be null.
    void setEndpointLabels(const cad::param::ParamPoint* startPt,
                           const cad::param::ParamPoint* endPt);

    /// Populate the measurement-reference combo with points available on the
    /// host segment (endpoints + aux/intersection points). Call before loadFrom.
    /// @param points  All points on the host segment (id + display label).
    void setRefPointList(const std::vector<std::pair<QUuid, QString>>& points);

    /// Fill all fields from the point (signals blocked while populating).
    void loadFrom(const cad::param::ParamPoint& pt);

    /// Write the current field values into the point. Numeric input clears
    /// the matching formula; non-numeric non-empty input becomes the formula.
    /// Constant is entered in cm and stored in mm.
    void applyTo(cad::param::ParamPoint& pt) const;

    /// Raw percent field access (QuickAuxDialog swaps t ↔ 1−t when the
    /// direction is flipped and the user hasn't typed a custom value yet).
    [[nodiscard]] QString percentText() const;
    void setPercentText(const QString& text);

signals:
    /// A field was committed (editingFinished / combo change / checkbox).
    void edited();
    /// Text changed — the owner should (re)start its debounce timer.
    void dirty();
    /// The direction combo changed (false = from start, true = from end).
    void directionChanged(bool fromEnd);

private:
    QLineEdit* m_editName         = nullptr;
    QComboBox* m_cmbDir           = nullptr;   ///< 0 = 从起点, 1 = 从终点.
    QComboBox* m_cmbRefPoint      = nullptr;   ///< Measurement reference point.
    QLineEdit* m_editPercent      = nullptr;
    QLineEdit* m_editConstant     = nullptr;   ///< cm input.
    QLineEdit* m_editOffsetAngle  = nullptr;   ///< degrees, construction-angle semantics.
    QLineEdit* m_editOffsetDist   = nullptr;   ///< cm input.
    QCheckBox* m_chkShowName      = nullptr;
};

} // namespace cad::tools
