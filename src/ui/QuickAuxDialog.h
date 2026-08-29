#pragma once

#include "ElaDialog.h"

#include "parametric/ParamPoint.h"

class QLabel;

namespace cad::tools {

class AuxPointForm;

/// Modal dialog for the smart pen's "click on a segment body" gesture:
/// quick-create an auxiliary (Interpolated) point on the host segment.
///
/// Shows the reserved read-only serial (the point's identity — dual-track
/// naming: serial is immutable, 名称 is an optional display label, default
/// empty) above the shared AuxPointForm. The percent field is pre-filled
/// with the click's projection parameter t, so confirming with defaults
/// drops the point exactly under the X marker. Flipping the direction
/// combo swaps the prefill to 1−t (unless the user already typed a custom
/// value).
///
/// The dialog never touches the document: the caller reads point() after
/// exec() == Accepted and pushes an AddAuxPointCommand.
class QuickAuxDialog : public ElaDialog
{
    Q_OBJECT

public:
    /// @param pt       Prepared point (Interpolated defaults, serial reserved,
    ///                 interpPercent pre-filled with the projection t).
    /// @param startPt  Host segment's start point (direction combo label).
    /// @param endPt    Host segment's end point (direction combo label).
    QuickAuxDialog(const cad::param::ParamPoint& pt,
                   const cad::param::ParamPoint* startPt,
                   const cad::param::ParamPoint* endPt,
                   QWidget* parent = nullptr);

    /// The configured point (valid after exec() == Accepted).
    [[nodiscard]] cad::param::ParamPoint point() const;

private:
    cad::param::ParamPoint m_pt;
    AuxPointForm* m_form = nullptr;
    double m_prefillT = 0.5;  ///< Projection t — basis for the 1−t swap.
};

} // namespace cad::tools
