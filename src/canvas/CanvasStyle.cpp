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
    s.canvasBackground    = QColor(30, 30, 30);
    s.crosshairColor      = QColor(70, 70, 70);
    s.previewLineColor    = QColor(80, 170, 255);
    s.snapIndicatorColor  = QColor(255, 90, 90);
    s.snapPointColor      = QColor(60, 220, 120);
    s.auxMarkerColor      = QColor(102, 187, 106);
    s.hudBackground       = QColor(50, 50, 50, 230);
    s.hudText             = QColor(220, 220, 220);

    s.m_selectColor       = QColor(255, 80, 80);
    s.m_hoverTint         = QColor(255, 140, 140);
    s.attachmentNodeColor = QColor(0, 200, 180);
    s.m_pointColor        = QColor(210, 210, 210);
    s.m_auxPointColor     = QColor(102, 187, 106);
    s.m_nameLabelColor    = QColor(170, 170, 170);
    s.m_lengthLabelColor  = QColor(80, 200, 130);
    s.m_pointLabelColor   = QColor(160, 160, 160);
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
