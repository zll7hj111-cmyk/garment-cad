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
/// Kept in sync with cad::ui::ThemeTokens by hand (same accent / semantic /
/// piece families — "Pattern Workbench": paper canvas, fabric-block piece
/// hues, single accent, semantic = meaning only).
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

    // ── Dark-mode color adaptation ──
    /// Map a data-driven segment color to the color that is actually painted.
    /// Light theme: identity. Dark theme: dark data colors (the default
    /// near-black ink) are lifted to the role's light-on-dark family so lines
    /// stay legible on the night-paper canvas; user-chosen bright colors
    /// (e.g. red, teal) pass through untouched.
    [[nodiscard]] QColor displayColor(cad::param::SegmentRole role,
                                      const QColor& dataColor) const;

    // ── Hit testing ──
    [[nodiscard]] double hoverRadiusPx() const { return m_hoverRadiusPx; }

    // ── Palette accessors (badges, overlays drawn outside the QSS layer) ──
    [[nodiscard]] QColor selectColorForBadge() const { return m_selectColor; }
    [[nodiscard]] QColor accentWash() const { return m_accentWash; }
    [[nodiscard]] QColor borderSoft() const { return m_borderSoft; }
    [[nodiscard]] QColor surfaceColor() const { return m_surfaceColor; }
    [[nodiscard]] QColor textSecondary() const { return m_nameLabelColor; }

    // ── Animation ──
    [[nodiscard]] int transitionMs() const { return m_transitionMs; }

    // ── Attachment markers ──
    /// Anchor-ring color for connection points (attachment nodes). The ring is
    /// drawn around the shared point of a leader/follower pair so connections
    /// are visible at a glance. A ring width of 0 disables the marker (print).
    /// Teal — same family as ThemeTokens::teal.
    QColor attachmentNodeColor = QColor(15, 118, 110);   // teal (light)
    /// Ring color for PROTECTED connections (拖动保护/焊接): amber tells the
    /// user this connection cannot be torn apart by dragging.
    QColor lockedAttachmentColor = QColor(180, 83, 9);   // warning (light)
    double attachmentRingWidth = 1.0;  ///< Cosmetic stroke width of the ring.
    double attachmentRingGap   = 1.5;  ///< Gap between point radius and ring.

    // ── Tool visuals ──
    QColor previewLineColor   = QColor(47, 111, 237);    // accent
    QColor snapIndicatorColor = QColor(220, 38, 38);     // danger
    QColor snapPointColor     = QColor(21, 128, 61);     // success (light)
    /// X marker on a segment body (smart pen: click to quick-create an
    /// auxiliary point). Matches the auxiliary point green.
    QColor auxMarkerColor     = QColor(21, 128, 61);
    QColor hudBackground      = QColor(255, 255, 255, 240);
    QColor hudText            = QColor(13, 17, 23);
    QColor crosshairColor     = QColor(203, 210, 220);

    // ── Canvas (Endfield blueprint ground, light theme) ──
    QColor canvasBackground   = QColor(236, 239, 242);   // #ECEFF2 Endfield light concrete

    // ── Theme factories ──
    static CanvasStyle lightTheme();
    static CanvasStyle darkTheme();
    static CanvasStyle printTheme();

    /// Dark-mode flag: role defaults (segment line colors) switch to the
    /// light-on-dark family when set by darkTheme().
    bool dark = false;

private:
    // State tokens — one accent family drives every interaction state:
    // hover = 55% blend toward the accent, selected = solid accent,
    // locked = accent + weight. Same hue, three clear steps.
    QColor  m_selectColor      = QColor(47, 111, 237);  // accent #2F6FED
    double  m_selectWidthDelta = 0.6;
    double  m_lockedWidthDelta = 1.8;  ///< Extra width for confirmed (locked) selection.
    double  m_lockedPointDelta = 0.8;  ///< Extra point radius when locked.
    QColor  m_hoverTint        = QColor(47, 111, 237);  // accent
    double  m_hoverTintRatio   = 0.55; ///< Blend strength; print sets 0.

    // Palette mirror (light defaults; darkTheme overrides): fills, washes and
    // soft borders for canvas overlays that QSS cannot reach (group
    // bounding boxes, HUD chips). Kept in sync with ThemeTokens by hand.
    QColor m_surfaceColor = QColor(255, 255, 255);   // surface
    QColor m_accentWash   = QColor(234, 242, 254);   // accentTint
    QColor m_borderSoft   = QColor(213, 219, 219);   // badge idle border

    // Point tokens — visual radius is a small marker (0.8), unified across
    // ALL point kinds (normal / auxiliary / curve anchor); the PICK radius in
    // BlockItem::shape() stays 2.5 so grabbing stays finger-friendly.
    // Auxiliary points render as solid green discs (绿色实心小圆).
    QColor  m_pointColor       = QColor(29, 33, 41);     // text1
    QColor  m_auxPointColor    = QColor(21, 128, 61);    // success (light)
    double  m_pointRadius      = 0.8;   // 原 1.6 → 缩小一半，全点对齐
    double  m_auxPointRadius   = 0.8;   // 原 2.2 → 对齐普通点

    // Label tokens
    QColor  m_nameLabelColor   = QColor(77, 87, 102);    // text2 (light)
    QColor  m_lengthLabelColor = QColor(21, 128, 61);    // deep success green
    QColor  m_pointLabelColor  = QColor(77, 87, 102);

    // Interaction tokens
    double  m_hoverRadiusPx    = 8.0;
    int     m_transitionMs     = 150;
};
