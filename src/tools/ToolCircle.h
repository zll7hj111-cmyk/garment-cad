#pragma once

#include "Tool.h"
#include "ToolRegistry.h"
#include "SnapEngine.h"
#include "canvas/ManagedItems.h"
#include "geometry/Vec2.h"

class QGraphicsPathItem;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

/// 绘制模式 (D5): 圆心+半径（默认）↔ 两点直径，W 键切换。
enum class CircleMode { CenterRadius, TwoPointDiameter };

/// ToolCircle — draws a parametric circle: press to place the center, drag out
/// the radius, release to commit; W switches to the two-point diameter gesture
/// (docs/design/CIRCLE_TOOL_DESIGN.md D5/D6).
///
/// 两类手势语义不同（一个拖半径、一个点两点），不允许跨模式续命 —— 切模式
/// 即取消进行中的橡皮筋（§5.2）。
class ToolCircle : public Tool
{
public:
    void onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void onDeactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void keyPress(QKeyEvent* event) override;

    // ── 绘制会话输入 (一期补充, CIRCLE_TOOL_DESIGN.md §5.5) ──
    /// 条带半径框的实时输入 (cm 域; locked = 数值/公式已求值, 半径定值)。
    void circleRadiusInput(double radiusCm, bool locked) override;
    /// 条带 Enter: 以当前有效半径落圆。
    void circleCommitted() override;
    /// 条带 Esc: 丢弃橡皮筋 (不落圆)。
    void circleCancelled() override;

    static ToolDescriptor describe();
    [[nodiscard]] ModeIndicator modeIndicator() const override;
    /// {mode, state} 共同决定三层显示文案 (TROUBLESHOOTING.md §0)：
    /// gestureActive = 会话进行中（圆心模式拖半径 / 直径模式已落首点）。
    [[nodiscard]] static ModeIndicator modeIndicatorFor(CircleMode mode, bool gestureActive);
    [[nodiscard]] CircleMode mode() const { return m_mode; }
    [[nodiscard]] const char* name() const override
    { return reinterpret_cast<const char*>(u8"画圆"); }

private:
    /// 会话状态机 (§5.1)。直径模式在第二次**按下**时提交，故 Armed 与
    /// Dragging 必须分开：只有 Dragging 的 mouseRelease 才提交。
    enum class Session { Idle, Dragging, Armed };

    void resetToIdle();
    void toggleMode();
    void updatePreview(const cad::geo::Vec2& cursorWorld);
    void clearPreview();
    /// 以当前有效半径落圆 (半径 < kMinCircleRadiusMm 时静默丢弃)。
    void commitCurrentCircle();
    /// 落圆收口 (一期补充②, CIRCLE_TOOL_DESIGN.md §5.6): 半径校验 → 建圆 →
    /// 走智能笔同一条创建通道上报宿主，条带据 fitKind 路由到圆专属条带。
    void commitCircle(const cad::geo::Vec2& center, double radiusMm);
    /// 锁定半径优先，否则取光标距离。
    [[nodiscard]] double effectiveRadiusMm() const
    { return m_radiusLocked ? m_lockedRadiusMm : m_radiusMm; }

    CircleMode m_mode = CircleMode::CenterRadius;
    Session m_session = Session::Idle;
    cad::geo::Vec2 m_anchor;   ///< 手势锚点：圆心 / 直径首点 A。
    cad::geo::Vec2 m_center;   ///< 待提交的圆心（圆心模式 = anchor；直径模式 = AB 中点）。
    double m_radiusMm = 0.0;
    /// 条带输入定的半径 (mm): 锁定后画布不再跟随光标 (§5.5)。
    double m_lockedRadiusMm = 0.0;
    bool m_radiusLocked = false;
    /// 最近一次光标位置: 锁定半径后重画预览、回车落圆都还需要它。
    cad::geo::Vec2 m_lastCursor;
    /// 条带是否已被通知进入绘制态 (避免重复上报/重复收起)。
    bool m_sessionReported = false;

    SnapEngine m_snap;
    ManagedItems m_managed;
    QGraphicsPathItem* m_previewCircle = nullptr;
    QGraphicsPathItem* m_previewCenter = nullptr;
    QGraphicsPathItem* m_previewGuide  = nullptr;
};

} // namespace cad::tools
