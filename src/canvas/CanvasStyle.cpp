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
    // Dark theme: light-on-dark family (pure white outlines, crisp light-gray
    // internals, warm light-gray dashes) so lines read clearly on the black canvas.
    if (dark) {
        switch (role) {
        case SegmentRole::Outline:
            return { QColor(255, 255, 255), 1.2, Qt::SolidLine };  // pure white #FFFFFF
        case SegmentRole::Internal:
            return { QColor(213, 208, 197), 1.0, Qt::SolidLine };  // borderStrong #D5D0C5
        case SegmentRole::Auxiliary:
            return { QColor(163, 158, 147), 0.8, Qt::DashLine };   // text2 #A39E93
        }
        return { QColor(255, 255, 255), 1.2, Qt::SolidLine };
    }
    switch (role) {
    case SegmentRole::Outline:
        return { QColor(20, 20, 19), 1.2, Qt::SolidLine };        // text1 #141413 near black ink
    case SegmentRole::Internal:
        return { QColor(92, 88, 80), 1.0, Qt::SolidLine };        // text2 #5C5850 warm slate ink
    case SegmentRole::Auxiliary:
        return { QColor(140, 135, 125), 0.8, Qt::DashLine };     // text3 #8C877D warm muted gray
    }
    return { QColor(20, 20, 19), 1.2, Qt::SolidLine };
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
    if (luma >= 0.55) return dataColor;

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
    s.canvasBackground    = QColor(20, 20, 19);     // #141413 carbon slate night paper
    s.crosshairColor      = QColor(56, 53, 49);     // border #383531
    s.gridDotColor        = QColor(77, 73, 67, 160);
    s.gridMajorColor      = QColor(56, 53, 49, 140);
    s.previewLineColor    = QColor(217, 119, 87);   // dark accent #D97757
    s.snapIndicatorColor  = QColor(82, 171, 127);   // success #52AB7F
    s.snapPointColor      = QColor(82, 171, 127);   // success #52AB7F
    s.auxMarkerColor      = QColor(82, 171, 127);
    s.hudBackground       = QColor(31, 30, 29, 240); // #1F1E1D
    s.hudText             = QColor(236, 233, 226);  // #ECE9E2

    s.m_selectColor       = QColor(217, 119, 87);   // accent #D97757
    s.m_hoverTint         = QColor(217, 119, 87);
    s.m_surfaceColor      = QColor(31, 30, 29);     // #1F1E1D
    s.m_accentWash        = QColor(56, 39, 33);     // #382721 pre-blended wash
    s.m_borderSoft        = QColor(56, 53, 49);     // #383531
    s.attachmentNodeColor = QColor(59, 160, 176);   // #3BA0B0
    s.lockedAttachmentColor = QColor(226, 160, 74); // #E2A04A
    s.m_pointColor        = QColor(255, 255, 255);  // #FFFFFF pure white
    s.m_auxPointColor     = QColor(82, 171, 127);
    s.m_nameLabelColor    = QColor(163, 158, 147);  // #A39E93
    s.m_lengthLabelColor  = QColor(82, 171, 127);
    s.m_pointLabelColor   = QColor(163, 158, 147);
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
