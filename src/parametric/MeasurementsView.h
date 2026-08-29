#pragma once

/// Narrow domain view over the ParamDocument measurement domain (B2,
/// 门面按域分组 — see BlockView.h for the pattern established in B1).
///
/// Covers the three measurement registries: linked variables (geometric
/// measurements 关联), two-point distance measures (测量) and two-segment
/// relative angle measures (角度测量).
///
/// Contract (mirrors the facade — nothing new, nothing bypassed):
///   * READ-ONLY. add/remove/update and the measureLinkedVars /
///     measureMeasureVars / measureAngleMeasureVars re-measure passes stay
///     on the facade (they sync into the parameter map and resolve).
///   * The mutable find* overloads (findLinked / findMeasure /
///     findLinkedBySource) stay facade-only — they are the authorized
///     in-place edit channels and must stay greppable as edits.
///   * Stateless — holds a `const ParamDocument*`, always reflects the live
///     document.
///
/// Usage:  for (const auto& m : doc.measurementsView().measureVars()) { ... }
///         if (const auto* m = doc.measurementsView().measureByOwner(blockId)) { ... }

#include <QUuid>
#include <vector>

#include "parametric/AngleMeasureVariable.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "parametric/ParamDocument.h"

namespace cad::param {

class MeasurementsView
{
public:
    explicit MeasurementsView(const ParamDocument& doc) noexcept
        : m_doc(&doc) {}

    /// All linked variables (geometric measurements, 关联) in registry order.
    [[nodiscard]] const std::vector<LinkedVariable>& linkedVars() const
    { return m_doc->linkedVars(); }

    /// All two-point distance measures (测量) in registry order.
    [[nodiscard]] const std::vector<MeasureVariable>& measureVars() const
    { return m_doc->measureVars(); }

    /// All two-segment relative angle measures (角度测量) in registry order.
    [[nodiscard]] const std::vector<AngleMeasureVariable>& angleMeasures() const
    { return m_doc->angleMeasures(); }

    /// The measure variable OWNED by @p ownerBlockId (the measurement line
    /// created together with it), nullptr when the block owns none. The
    /// canvas right-click menu uses this to detect measure lines
    /// (烘焙到操作层入口).
    [[nodiscard]] const MeasureVariable* measureByOwner(const QUuid& ownerBlockId) const
    { return m_doc->findMeasureByOwner(ownerBlockId); }

private:
    const ParamDocument* m_doc;
};

/// Facade accessor — defined here so ParamDocument.h only carries the
/// forward declaration (keeps the facade header free of the view body).
inline MeasurementsView ParamDocument::measurementsView() const noexcept
{
    return MeasurementsView(*this);
}

} // namespace cad::param
