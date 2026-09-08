#pragma once

#include <QColor>
#include <Qt>

#include "parametric/DefaultAppearance.h"
#include "geometry/Epsilon.h"

namespace cad::param { enum class SegmentRole; }

/// 画布悬停/拾取半径 (px)：悬停高亮、选择命中带、线身吸附共用的唯一像素半径。
/// canvas 层单一来源 —— tools 层经 InteractionTolerances.h 的 kBodySnapRadiusPx
/// 别名引用（canvas 不得依赖 tools，故数值只能在此定义）。
inline constexpr double kHoverRadiusPx = 8.0;

namespace cad::canvas {
/// 缩放守卫唯一判定 (2026-12 审计 U11): 非正/退化缩放 (<= kGeomEps, 含 0、
/// 负、未初始化) 一律按 1.0 处理 —— 像素↔世界单位换算的唯一除零守卫。
/// CanvasScene::safeZoom() 是场景版; 工具/UI 拿到裸 zoom 参数时用本函数。
[[nodiscard]] inline double safeZoomOr(double zoom)
{
    return zoom > cad::geo::kGeomEps ? zoom : 1.0;
}
} // namespace cad::canvas

/// Visual interaction state of a canvas entity (segment or point).
/// Unified language: hover = tint toward the accent color (no width change),
/// selected = full accent color, locked = accent + bold weight (confirmed
/// selection, ready for drag/connect operations).
enum class EntityState { Normal, Hover, Selected, Locked };

/// Fully-resolved paint parameters for one entity at one frame.
/// The animator interpolates between two instances of this struct.
struct EntityPaintParams {
    QColor lineColor;
    double lineWidth   = cad::param::kDefaultSegmentWeight;
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
/// hues, single accent, semantic = meaning only) — enforced by
/// tests/test_theme_sync.cpp (2026-12 审计 P0-3 / G3).
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
    // ── Attachment markers ──
    /// Anchor-ring color for connection points (attachment nodes). The ring is
    /// drawn around the shared point of a leader/follower pair so connections
    /// are visible at a glance. A ring width of 0 disables the marker (print).
    /// Teal — same family as ThemeTokens::teal.
    QColor attachmentNodeColor = QColor(42, 123, 136);   // teal #2A7B88
    /// Ring color for PROTECTED connections (拖动保护/焊接): amber tells the
    /// user this connection cannot be torn apart by dragging.
    QColor lockedAttachmentColor = QColor(212, 139, 56); // warning #D48B38
    double attachmentRingWidth = 1.0;  ///< Cosmetic stroke width of the ring.
    double attachmentRingGap   = 1.5;  ///< Gap between point radius and ring.

    // ── Tool visuals ──
    QColor previewLineColor   = QColor(204, 120, 92);    // accent #CC785C
    QColor snapIndicatorColor = QColor(62, 137, 102);    // success #3E8966
    QColor snapPointColor     = QColor(62, 137, 102);    // success #3E8966
    /// X marker on a segment body (smart pen: click to quick-create an
    /// auxiliary point). Matches the auxiliary point green.
    QColor auxMarkerColor     = QColor(62, 137, 102);    // success #3E8966
    QColor hudBackground      = QColor(250, 249, 245, 245);  // 温润象牙纸面 (#FAF9F5)
    QColor hudText            = QColor(20, 20, 19);          // 炭黑文字 (#141413)
    QColor crosshairColor     = QColor(213, 208, 197);       // borderStrong #D5D0C5
    QColor gridDotColor       = QColor(213, 208, 197, 180);   // Dot grid
    QColor gridMajorColor     = QColor(229, 226, 218, 100);   // 100mm/50mm hairline rule (40% alpha)

    // ── Canvas (Warm ivory drafting ground, light theme) ──
    QColor canvasBackground   = QColor(250, 249, 245);   // #FAF9F5

    // ── Entity / interaction colors (audit P0-1: the ONE canvas palette) ──
    // Every canvas painter that used to spell a QColor literal inline reads it
    // from here instead; the hardcoded-color guard scans only files outside
    // the token tables, so a new literal in a painter is a build-time failure.
    QColor grayedLineColor       = QColor(0x9E, 0x9E, 0x9E);   ///< 灰显图层线色
    double grayedOpacity         = 0.4;                        ///< 灰显不透明度
    QColor curveAnchorColor      = QColor(0xE9, 0x1E, 0x63);   ///< ETCAD 粉: 曲线锚点
    QColor placedPointColor      = QColor(255, 140, 0);        ///< 放置点 (琥珀菱形)
    QColor snapNodeColor         = QColor(38, 166, 154);       ///< 吸附/连接节点 (青绿)
    QColor componentBoxColor     = QColor(47, 111, 237, 210);  ///< 部件包围盒虚线
    QColor marqueeColor          = QColor(0, 120, 215);        ///< 框选矩形
    QColor measureColor          = QColor(255, 152, 0);        ///< 测量预览/高亮 (琥珀)
    QColor confirmHighlightColor = QColor(0xF3, 0x9C, 0x12);   ///< 连接确认高亮 (橙)
    QColor handleUnlockedColor   = QColor(0xE6, 0x8A, 0x00);   ///< 尖角模式曲线手柄
    QColor handleLockedColor     = QColor(0x00, 0xA8, 0xE1);   ///< 锁定切线曲线手柄
    QColor endpointHoverColor    = QColor(0, 172, 193);        ///< 端点悬停环 (青)
    QColor snapAimColor          = QColor(255, 193, 7);        ///< 瞄准吸附实心点 (黄)
    QColor snapAimOutlineColor   = QColor(140, 100, 0);        ///< 瞄准吸附描边
    QColor gizmoBaseColor        = QColor(120, 144, 156);      ///< 旋转量角器基准虚线
    QColor gizmoAccentColor      = QColor(251, 140, 0);        ///< 旋转量角器姿态虚线/扇形
    QColor gizmoBadgeBg          = QColor(33, 33, 33, 210);    ///< 旋转度数徽标底
    QColor gizmoBadgeFg          = QColor(255, 255, 255);      ///< 旋转度数徽标字
    QColor hudShadowColor        = QColor(20, 20, 19, 20);     ///< HUD 微阴影
    QColor hudBorderColor        = QColor(213, 208, 197, 220); ///< HUD 描边 (亮)
    QColor hudDarkPillBg         = QColor(31, 30, 29, 245);    ///< 暗色胶囊底
    QColor hudDarkPillFg         = QColor(236, 233, 226);      ///< 暗色胶囊字
    QColor hudDarkPillBorder     = QColor(77, 73, 67, 220);    ///< 暗色胶囊描边
    QColor guideLineColor        = QColor(150, 150, 150);      ///< 悬停对齐引导虚线

    // ── Theme factories ──
    static CanvasStyle lightTheme();
    static CanvasStyle darkTheme();
    static CanvasStyle printTheme();

    /// Default token table for painters that run without a scene style
    /// (offscreen / unit-test painting). Lets every painter site read one
    /// palette instead of falling back to a local QColor literal (audit P0-1).
    static const CanvasStyle& fallback();

    /// Dark-mode flag: role defaults (segment line colors) switch to the
    /// light-on-dark family when set by darkTheme().
    bool dark = false;

private:
    // State tokens — one accent family drives every interaction state:
    // hover = 55% blend toward the accent, selected = solid accent,
    // locked = accent + weight. Same hue, three clear steps.
    QColor  m_selectColor      = QColor(204, 120, 92);  // accent #CC785C
    double  m_selectWidthDelta = 0.6;
    double  m_lockedWidthDelta = 1.8;  ///< Extra width for confirmed (locked) selection.
    double  m_lockedPointDelta = 0.8;  ///< Extra point radius when locked.
    QColor  m_hoverTint        = QColor(204, 120, 92);  // accent #CC785C
    double  m_hoverTintRatio   = 0.55; ///< Blend strength; print sets 0.

    // Palette mirror (light defaults; darkTheme overrides): fills, washes and
    // soft borders for canvas overlays that QSS cannot reach (group
    // bounding boxes, HUD chips). Kept in sync with ThemeTokens by hand.
    QColor m_surfaceColor = QColor(255, 255, 255);   // surface #FFFFFF
    QColor m_accentWash   = QColor(249, 239, 235);   // accentTint #F9EFEB pre-blended wash
    QColor m_borderSoft   = QColor(229, 226, 218);   // border #E5E2DA

    // Point tokens — visual radius is a small marker (0.8), unified across
    // ALL point kinds (normal / auxiliary / curve anchor); the PICK radius in
    // BlockItem::shape() stays 2.5 so grabbing stays finger-friendly.
    // Auxiliary points render as solid green discs (绿色实心小圆).
    QColor  m_pointColor       = QColor(20, 20, 19);     // text1 #141413
    QColor  m_auxPointColor    = QColor(62, 137, 102);    // success #3E8966
    double  m_pointRadius      = 0.8;   // 对齐普通点
    double  m_auxPointRadius   = 0.8;   // 对齐普通点

    // Label tokens
    QColor  m_nameLabelColor   = QColor(92, 88, 80);     // text2 #5C5850
    QColor  m_lengthLabelColor = QColor(62, 137, 102);    // success #3E8966
    QColor  m_pointLabelColor  = QColor(92, 88, 80);

    // Interaction tokens
    double  m_hoverRadiusPx    = kHoverRadiusPx;
    int     m_transitionMs     = 150;
};
