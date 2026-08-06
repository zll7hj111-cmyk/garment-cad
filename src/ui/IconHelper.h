#pragma once

#include <QIcon>
#include <QColor>
#include <QString>

namespace cad::ui {

/// Loads Phosphor SVG icons from embedded resources with optional color tinting.
/// Icons use fill="currentColor" which gets replaced by the requested color.
class IconHelper
{
public:
    /// Returns a tinted QIcon for the given resource path (e.g. ":/icons/pen.svg").
    /// If color is invalid (default), the icon keeps its original black fill.
    static QIcon icon(const QString& resourcePath, const QColor& color = QColor());

    /// Convenience: icon by short name (e.g. "pen" -> ":/icons/pen.svg").
    static QIcon iconByName(const QString& name, const QColor& color = QColor());

    /// Two-state icon: `normal` tint at rest, `active` tint on hover/press.
    /// Useful for buttons whose background changes on hover.
    static QIcon icon2State(const QString& name, const QColor& normal, const QColor& active);

    /// Application window icon (t-shirt, brand blue).
    static QIcon appIcon();
};

} // namespace cad::ui
