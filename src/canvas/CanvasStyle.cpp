#include "CanvasStyle.h"

#include "parametric/Segment.h"  // cad::param::SegmentRole

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Role defaults
// ---------------------------------------------------------------------------

CanvasStyle::RoleDefaults CanvasStyle::roleDefaults(cad::param::SegmentRole role) const
{
    using cad::param::SegmentRole;
    // Dark theme: light-on-dark family (near-white outlines, light-gray
    // internals, mid-gray dashes) so lines read clearly on the black canvas.
    if (dark) {
        switch (role) {
        case SegmentRole::Outline:
            return { QColor(244, 246, 248), 1.2, Qt::SolidLine };
        case SegmentRole::Internal:
            return { QColor(198, 205, 213), 1.0, Qt::SolidLine };
        case SegmentRole::Auxiliary:
            return { QColor(96, 104, 114), 0.8, Qt::DashLine };
        }
        return { QColor(244, 246, 248), 1.2, Qt::SolidLine };
    }
    switch (role) {
    case SegmentRole::Outline:
        return { QColor(30, 30, 30), 1.2, Qt::SolidLine };
    case SegmentRole::Internal:
        return { QColor(60, 60, 60), 1.0, Qt::SolidLine };
    case SegmentRole::Auxiliary:
        return { QColor(150, 150, 150), 0.8, Qt::DashLine };
    }
    return { QColor(30, 30, 30), 1.2, Qt::SolidLine };
}

QColor CanvasStyle::displayColor(cad::param::SegmentRole role,
                                 const QColor& dataColor) const
{
    // Light theme / print: the data color is the paint color.
    if (!dark) return dataColor;

    // Dark theme: lift dark ink to the role's light-on-dark family. The
    // default segment color is near-black (30,30,30); on the night-paper
    // canvas it would vanish. User-chosen bright colors stay as chosen.
    // Luma threshold: mid-gray (~#808080) and darker get lifted.
    const double luma = 0.2126 * dataColor.redF()
                      + 0.7152 * dataColor.greenF()
                      + 0.0722 * dataColor.blueF();
    if (luma >= 0.5) return dataColor;

    const RoleDefaults rd = roleDefaults(role);
    return rd.color;
}

// ---------------------------------------------------------------------------
// State-dependent line parameters
// ---------------------------------------------------------------------------

QColor CanvasStyle::lineColor(EntityState s, const QColor& base) const
{
    switch (s) {
    case EntityState::Normal:
        return base;
    case EntityState::Hover: {
        // Blend base with hover tint (Figma-style: user color remains visible)
        const double r = m_hoverTintRatio;
        return QColor(
            std::lround(base.red()   * (1.0 - r) + m_hoverTint.red()   * r),
            std::lround(base.green() * (1.0 - r) + m_hoverTint.green() * r),
            std::lround(base.blue()  * (1.0 - r) + m_hoverTint.blue()  * r),
            base.alpha());
    }
    case EntityState::Selected:
        return m_selectColor;
    case EntityState::Locked:
        return m_selectColor;  // Same accent family — weight distinguishes.
    }
    return base;
}

double CanvasStyle::lineWidth(EntityState s, double base) const
{
    switch (s) {
    case EntityState::Normal:
        return base;
    case EntityState::Hover:
        return base;  // Hover changes color only — no width jump.
    case EntityState::Selected:
        return base + m_selectWidthDelta;
    case EntityState::Locked:
        return base + m_lockedWidthDelta;  // Bold = confirmed, action-ready.
    }
    return base;
}

// ---------------------------------------------------------------------------
// State-dependent point parameters
// ---------------------------------------------------------------------------

QColor CanvasStyle::pointColor(EntityState s, bool auxiliary) const
{
    const QColor baseCol = auxiliary ? m_auxPointColor : m_pointColor;
    switch (s) {
    case EntityState::Normal:
        return baseCol;
    case EntityState::Hover:
        return m_hoverTint;
    case EntityState::Selected:
        return m_selectColor;
    case EntityState::Locked:
        return m_selectColor;
    }
    return baseCol;
}

double CanvasStyle::pointRadius(EntityState s, bool auxiliary) const
{
    const double base = auxiliary ? m_auxPointRadius : m_pointRadius;
    switch (s) {
    case EntityState::Normal:
        return base;
    case EntityState::Hover:
        return base;  // Color only — consistent with line hover.
    case EntityState::Selected:
        return base;
    case EntityState::Locked:
        return base + m_lockedPointDelta;  // Slightly larger = grabbable.
    }
    return base;
}

// ---------------------------------------------------------------------------
// State-dependent label parameters
// ---------------------------------------------------------------------------

QColor CanvasStyle::labelColor(EntityState s, bool isLength) const
{
    const QColor baseCol = isLength ? m_lengthLabelColor : m_nameLabelColor;
    switch (s) {
    case EntityState::Normal:
        return baseCol;
    case EntityState::Hover:
        return m_hoverTint;
    case EntityState::Selected:
        return m_selectColor;
    case EntityState::Locked:
        return m_selectColor;
    }
    return baseCol;
}

// ---------------------------------------------------------------------------
// Theme factories
// ---------------------------------------------------------------------------

CanvasStyle CanvasStyle::lightTheme()
{
    // Default member initializers already define the light theme.
    return CanvasStyle{};
}

CanvasStyle CanvasStyle::darkTheme()
{
    CanvasStyle s;
    s.dark = true;                       // white-ish role lines
    s.canvasBackground    = QColor(20, 24, 30);     // #14181E night paper
    s.crosshairColor      = QColor(38, 43, 49);
    s.previewLineColor    = QColor(76, 141, 255);     // dark accent
    s.snapIndicatorColor  = QColor(240, 101, 90);
    s.snapPointColor      = QColor(52, 199, 123);
    s.auxMarkerColor      = QColor(52, 199, 123);
    s.hudBackground       = QColor(35, 40, 46, 230);
    s.hudText             = QColor(232, 234, 237);

    s.m_selectColor       = QColor(76, 141, 255);     // accent family
    s.m_hoverTint         = QColor(76, 141, 255);
    s.m_surfaceColor      = QColor(29, 33, 38);       // dark surface
    s.m_accentWash        = QColor(30, 43, 66);       // dark accentTint
    s.m_borderSoft        = QColor(51, 58, 66);       // dark border
    s.attachmentNodeColor = QColor(43, 179, 163);
    s.lockedAttachmentColor = QColor(240, 169, 75);
    s.m_pointColor        = QColor(236, 238, 241);
    s.m_auxPointColor     = QColor(52, 199, 123);
    s.m_nameLabelColor    = QColor(174, 181, 191);
    s.m_lengthLabelColor  = QColor(52, 199, 123);
    s.m_pointLabelColor   = QColor(154, 163, 173);
    return s;
}

CanvasStyle CanvasStyle::printTheme()
{
    CanvasStyle s;
    // Print: no hover/selection emphasis, auxiliary lines very light
    s.m_selectWidthDelta  = 0.0;
    s.m_lockedWidthDelta  = 0.0;
    s.m_lockedPointDelta  = 0.0;
    s.m_hoverTintRatio    = 0.0;
    s.m_transitionMs      = 0;  // No animation in print mode
    s.attachmentRingWidth = 0.0;  // No connection markers on printed patterns
    s.crosshairColor      = QColor(230, 230, 230);
    return s;
}
