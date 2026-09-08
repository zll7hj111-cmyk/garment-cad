#pragma once

#include <memory>
#include <QColor>

#include "canvas/CanvasStyle.h"
#include "geometry/Vec2.h"

class CanvasScene;

namespace cad::canvas {

/// 瞬态视觉生命周期层级 (Tier)
enum class OverlayTier {
    Hover,    ///< 悬停级：鼠标按下 (Press)、击键 (Key)、工具切换时自动清空
    Gesture,  ///< 手势级：拖拽完成 (Release)、手势取消 (Esc) 时清空
    Session,  ///< 会话级：随工具退出或会话重置清空 (如旋转 Gizmo)
    All
};

/// 屏幕像素强类型包装（保持屏幕视口像素恒定，不随缩放改变尺寸）
struct ScreenPx {
    double value = 0.0;
    constexpr explicit ScreenPx(double v) : value(v) {}
};

/// 版型物理毫米强类型包装（随视口缩放而缩放）
struct WorldMm {
    double value = 0.0;
    constexpr explicit WorldMm(double v) : value(v) {}
};

/// 统一瞬态视觉层管线 (Transient Visual Overlay Pipeline)
///
/// 位于 gcad_canvas 模块内部，与 CanvasScene 绑定。
/// 核心契约：
/// 1. 工具层彻底消灭 QGraphicsItem* 裸指针与手动 hide()；
/// 2. 对外输入严格采用数学世界坐标 (Vec2, +Y 向上) 与数学世界弧度 (逆时针为正)；
/// 3. 底层统一处理 Coord::toScene 坐标映射与 Y 轴反转角度变换；
/// 4. 基于对象池插槽 (Slot Reuse) 实现 O(1) 极速复用与原子清空。
class TransientOverlay {
public:
    explicit TransientOverlay(CanvasScene* scene);
    ~TransientOverlay();

    TransientOverlay(const TransientOverlay&) = delete;
    TransientOverlay& operator=(const TransientOverlay&) = delete;

    // ─── 生命周期总线（框架自动调度） ─────────────────────────
    /// 原子清空指定层级的图元（O(1) 状态复位，对象池隐式复用）
    void clear(OverlayTier tier);
    void clearAll() { clear(OverlayTier::All); }

    // ─── Tier 1: 常用 Hover 视觉反馈（输入均为数学世界坐标 Vec2） ──
    /// 端点悬停指示青圈（自动维持屏幕像素大小，自动 toScene 映射）
    void showEndpointHover(const cad::geo::Vec2& worldPos, ScreenPx radius = ScreenPx(6.0));

    /// 瞄准吸附指示实心小黄点（旋转/智能笔等端点瞄准通用）
    void showSnapAim(const cad::geo::Vec2& worldPos, ScreenPx radius = ScreenPx(4.0));

    /// 空心标记圆环（自定义颜色，如 ToolBreak 可打断点绿色环）
    void showMarkerRing(const cad::geo::Vec2& worldPos, const QColor& color, ScreenPx radius = ScreenPx(7.0), double penWidth = 2.0);

    /// 吸附/标记圆点（自定义颜色）
    void showMarkerPoint(const cad::geo::Vec2& worldPos, const QColor& color, ScreenPx radius = ScreenPx(4.0));

    /// 吸附/标记十字或叉号（ToolBreak 线身吸附等）
    void showMarkerCross(const cad::geo::Vec2& worldPos, const QColor& color, ScreenPx halfSize = ScreenPx(4.0));

    /// 悬停对齐/引导虚线
    void showGuideLine(const cad::geo::Vec2& p1World, const cad::geo::Vec2& p2World,
                       const QColor& color = CanvasStyle::fallback().guideLineColor, bool dashed = true,
                       OverlayTier tier = OverlayTier::Hover);

    // ─── Tier 2: 手势级视觉反馈 ──────────────────────────────
    /// 框选矩形框（世界坐标）
    void showMarqueeBox(const cad::geo::Vec2& p1World, const cad::geo::Vec2& p2World);

    // ─── Tier 3: 复合 Gizmo（彻底消灭坐标混乱） ────────────────
    /// 旋转量角器手柄（世界 0° 灰虚线 + 起手姿态黄虚线 + 起手→当前黄弧 + 度数徽标）
    /// 2026-09 统一 M2（docs/design/ROTATE_ANGLE_UNIFY_DESIGN.md）：
    ///   灰虚线 = 世界 0° 射线，恒定，不依赖入参；
    ///   黄虚线 = 起手姿态（按下瞬间冻结）；
    ///   黄弧   = 起手姿态 → 当前姿态（活动边贴着线段当前方向）。
    /// @param pivotWorld 旋转轴心世界坐标
    /// @param startPoseWorldRad 黄虚线：起手姿态方向弧度（按下瞬间冻结）
    /// @param currentPoseWorldRad 黄弧活动边 + 徽标锚点：当前姿态方向弧度
    ///        （黄弧跨度 = normalizeRad(currentPoseWorldRad − startPoseWorldRad)，
    ///         故不再需要单独传旋转量 —— 姿态角是唯一真相，2026-09 统一 M2）
    /// @param badgeText 画布浮动度数徽标文本（空则隐藏）
    void showRotateGizmo(const cad::geo::Vec2& pivotWorld,
                         double startPoseWorldRad,
                         double currentPoseWorldRad,
                         const QString& badgeText = QString());

    void hideRotateGizmo();

    // ─── 诊断与状态查询（测试用） ────────────────────────────
    [[nodiscard]] bool isEndpointHoverVisible() const;
    [[nodiscard]] bool isSnapAimVisible() const;
    [[nodiscard]] bool isMarkerCrossVisible() const;
    [[nodiscard]] bool isRotateGizmoVisible() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace cad::canvas
