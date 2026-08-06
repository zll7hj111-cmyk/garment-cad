#pragma once

#include <QColor>
#include <Qt>

namespace cad::param { enum class SegmentRole; }

/// Visual interaction state of a canvas entity (segment or point).
/// Unified language: hover = tint toward the accent color (no width change),
/// selected = full accent color, locked = accent + bold weight (confirmed
/// selection, ready for drag/connect operations).
enum class EntityState { Normal, Hover, Selected, Locked };

/// Fully-resolved paint parameters for one entity at one frame.
/// The animator interpolates between two instances of this struct.
struct EntityPaintParams {
    QColor lineColor;
    double lineWidth   = 1.2;
    QColor pointFill;
    double pointRadius = 2.5;
    QColor labelColor;
    QColor lengthLabelColor;  ///< Distinct color for length annotations.
    double labelAlpha  = 1.0;
};

/// Centralized design-token table for all canvas visuals.
/// Pure value type — no QObject, no signals. Theme switching = replace instance.
class CanvasStyle
{
public:
    // ── Role defaults (queried by SegmentRole) ──
    struct RoleDefaults {
        QColor       color;
        double       weight;
        Qt::PenStyle penStyle;
    };
    [[nodiscard]] RoleDefaults roleDefaults(cad::param::SegmentRole role) const;

    // ── State-dependent parameters (core query interface) ──
    /// Line color: base is the data-driven color; state transforms it.
    [[nodiscard]] QColor lineColor(EntityState s, const QColor& base) const;
    /// Line width: base is the data-driven weight; state adds delta.
    [[nodiscard]] double lineWidth(EntityState s, double base) const;
    /// Point fill color.
    [[nodiscard]] QColor pointColor(EntityState s, bool auxiliary) const;
    /// Point radius in pixels (cosmetic).
    [[nodiscard]] double pointRadius(EntityState s, bool auxiliary) const;
    /// Label color (isLength distinguishes name vs length label).
    [[nodiscard]] QColor labelColor(EntityState s, bool isLength) const;

    // ── Hit testing ──
    [[nodiscard]] double hoverRadiusPx() const { return m_hoverRadiusPx; }

    // ── Animation ──
    [[nodiscard]] int transitionMs() const { return m_transitionMs; }

    // ── Attachment markers ──
    /// Anchor-ring color for connection points (attachment nodes). The ring is
    /// drawn around the shared point of a leader/follower pair so connections
    /// are visible at a glance. A ring width of 0 disables the marker (print).
    QColor attachmentNodeColor = QColor(0, 150, 136);
    /// Ring color for LOCKED connections (锁定连接/焊接): amber tells the
    /// user this connection cannot be torn apart by dragging.
    QColor lockedAttachmentColor = QColor(230, 160, 40);
    double attachmentRingWidth = 1.0;  ///< Cosmetic stroke width of the ring.
    double attachmentRingGap   = 1.5;  ///< Gap between point radius and ring.

    // ── Tool visuals ──
    QColor previewLineColor   = QColor(0, 120, 215);
    QColor snapIndicatorColor = QColor(220, 50, 50);
    QColor snapPointColor     = QColor(0, 180, 80);
    /// X marker on a segment body (smart pen: click to quick-create an
    /// auxiliary point). Matches the auxiliary point green.
    QColor auxMarkerColor     = QColor(67, 160, 71);
    QColor hudBackground      = QColor(255, 255, 255, 215);
    QColor hudText            = QColor(30, 30, 30);
    QColor crosshairColor     = QColor(200, 200, 200);

    // ── Canvas ──
    QColor canvasBackground   = QColor(250, 250, 250);

    // ── Theme factories ──
    static CanvasStyle lightTheme();
    static CanvasStyle darkTheme();
    static CanvasStyle printTheme();

private:
    // State tokens — hover swaps to a solid light accent (direct recolor),
    // selected uses the full accent. Same family, two clear steps.
    QColor  m_selectColor      = QColor(220, 40, 40);
    double  m_selectWidthDelta = 0.6;
    double  m_lockedWidthDelta = 1.8;  ///< Extra width for confirmed (locked) selection.
    double  m_lockedPointDelta = 0.8;  ///< Extra point radius when locked.
    QColor  m_hoverTint        = QColor(235, 110, 110);
    double  m_hoverTintRatio   = 1.0;  ///< 1 = full replacement; print sets 0.

    // Point tokens — visual radius is a small marker (0.8), unified across
    // ALL point kinds (normal / auxiliary / curve anchor); the PICK radius in
    // BlockItem::shape() stays 2.5 so grabbing stays finger-friendly.
    // Auxiliary points render as solid green discs (绿色实心小圆).
    QColor  m_pointColor       = QColor(30, 30, 30);
    QColor  m_auxPointColor    = QColor(67, 160, 71);
    double  m_pointRadius      = 0.8;   // 原 1.6 → 缩小一半，全点对齐
    double  m_auxPointRadius   = 0.8;   // 原 2.2 → 对齐普通点

    // Label tokens
    QColor  m_nameLabelColor   = QColor(100, 100, 100);
    QColor  m_lengthLabelColor = QColor(0, 110, 60);
    QColor  m_pointLabelColor  = QColor(80, 80, 80);

    // Interaction tokens
    double  m_hoverRadiusPx    = 8.0;
    int     m_transitionMs     = 150;
};
