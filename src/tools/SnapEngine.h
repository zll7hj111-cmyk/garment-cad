#pragma once

#include <QUuid>
#include <QString>
#include <optional>

#include "geometry/Vec2.h"

namespace cad::param { class ParamDocument; }

namespace cad::tools {

/// Result of a successful snap operation.
struct SnapResult {
    cad::geo::Vec2 worldPos;  ///< World-coordinate position of the snapped point.
    QUuid blockId;            ///< Block containing the point.
    QUuid pointId;            ///< The ParamPoint that was snapped to.
    QString pointName;        ///< Display name (may be empty).
};

/// Provides point-snapping capability for drawing tools.
/// Searches all resolved points in the ParamDocument within a screen-space radius.
class SnapEngine
{
public:
    /// Snap radius in scene pixels (default 12px).
    double snapRadius = 12.0;

    /// Find the nearest snappable point within snapRadius of the given world position.
    /// @param worldPos   Cursor position in user/world coordinates (+Y up).
    /// @param paramDoc   The parametric document to search.
    /// @param zoom       Current view zoom factor (for screen-space radius conversion).
    /// @return           SnapResult if a point was found, std::nullopt otherwise.
    [[nodiscard]] std::optional<SnapResult> findSnap(
        const cad::geo::Vec2& worldPos,
        const cad::param::ParamDocument* paramDoc,
        double zoom) const;
};

} // namespace cad::tools
