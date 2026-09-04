#pragma once

#include <QUuid>
#include <QString>
#include <QHash>
#include <QList>
#include <QSet>
#include <optional>
#include <vector>
#include <memory>

#include "Tool.h"
#include "ToolRegistry.h"
#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Duplicate.h"

class QGraphicsEllipseItem;
class QGraphicsSimpleTextItem;
#include "canvas/ManagedItems.h"

#include "tools/RotateAimSnap.h"
#include "tools/RotateInputTracker.h"
#include "tools/RotateSession.h"
#include "tools/MultiRotateSession.h"

namespace cad::tools {
class MarqueeGesture;
class RotateCopyGesture;
class RotateGizmo;

/// Rotate tool interaction state machine.
enum class RotateState {
    Idle,       ///< No target selected.
    Ready,      ///< Target selected; protractor gizmo + angle HUD visible.
    Rotating,   ///< Drag-rotation in progress.
};

/// Rotation tool — quickly adjust the follower angle of a connected line
/// or the world angle of a free line.
///
///   - Click a line to select it as the rotation target (pivot = the
///     connection point for a connected line, the start point for a free one).
///   - Drag to rotate (hold Shift to snap to 15°); the HUD shows the live angle.
///   - Type a number or a formula into the HUD for precise input (Enter commits).
///   - Release the mouse / press Enter to commit (undoable); Esc cancels the
///     current gesture; right-click deselects.
///   - Double-click opens the line property dialog.
///
/// Two modes:
///   - Connected line (non-pin follower attachment): edits the attachment's
///     follower angle (followerAngle); the resolver repositions the block.
///     Pivot = the snapped point; reference direction = the leader's exit
///     direction, so the displayed angle IS the follower angle.
///   - Free line (no attachment): rotates rigidly about its start point by
///     editing the block transform (rotation + origin). The displayed angle is
///     the world angle (reference direction = +X axis).
///
/// A connected line whose follower angle is driven by a formula/variable is
/// LOCKED: drag rotation and plain-number HUD input are refused, so the
/// parametric link is never silently broken by a free-hand gesture. Remove the
/// formula (e.g. in the property dialog) to unlock; editing the formula itself
/// is still allowed through the HUD. Free lines are never locked.
///
/// Bridge lines (Block::isBridge) have fully passive geometry and are refused.
class ToolRotate : public Tool
{
public:
    /// Release the angle HUD (parented to the viewport; ToolManager rebuilds
    /// this tool on every switch, so without this each switch would leak one).
    ~ToolRotate() override;

    void onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void onDeactivate() override;

    /// 单线确认流 (D15, 用户拍板 2026-08-27): 选中态 ⇄ 确定态之门。
    /// false = 已选未确认 (点空白取消/点端点切换锚向/不可拖动);
    /// true  = 已确认 (可拖动; 拖动提交后自动回落 false, 再拖需再确认)。
    [[nodiscard]] bool selectionConfirmed() const { return m_selectionConfirmed; }
    /// 当前 gizmo 的确认态视觉 (测试/诊断用; 无 gizmo = false)。RotateGizmo
    /// gizmo 确认态查询 (测试用: 选中=false, 确定=true).
    [[nodiscard]] bool gizmoConfirmed() const;
    [[nodiscard]] const RotateGizmo* gizmo() const { return m_gizmo.get(); }
    /// 旋转会话中 (有单线/选集目标且非 Idle): CanvasView 借此屏蔽右键
    /// 上下文菜单 —— 确认手势独占该按键 (D15 与发布菜单互斥)。
    [[nodiscard]] bool hasSessionTarget() const
    { return m_state == RotateState::Ready || m_state == RotateState::Rotating; }
    /// 当前锚心所在点 id (测试/诊断用; null = 无会话)。
    [[nodiscard]] QUuid anchorPointId() const { return m_session.anchor().pointId; }
    [[nodiscard]] cad::geo::Vec2 pivot() const { return m_session.pivot(); }
    [[nodiscard]] bool pivotPicked() const { return m_input.pivotPicked(); }
    [[nodiscard]] bool hoverSnapped() const { return m_input.hoverSnapped(); }
    [[nodiscard]] cad::geo::Vec2 hoverSnapPoint() const { return m_input.hoverSnapPoint(); }

    /// 继承选择集 (从选择工具切换进入或外部多选设置)
    void adoptSelection(const QSet<QUuid>& blockIds);
    [[nodiscard]] const QSet<QUuid>& selection() const { return m_multi.selection(); }
    [[nodiscard]] bool isMultiSelect() const { return m_multi.isMultiSelect(); }

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClick(QGraphicsSceneMouseEvent* event) override;

    void keyPress(QKeyEvent* event) override;

    /// 条带「换向」点击 (旋转会话内, 2026-12): 转义为切换锚心 (起点 ↔ 终点),
    /// gizmo pivot 环移到另一端 —— 旋转工具的"换向"语义 = 换选择的锚点。
    void onReverseRequested(const QUuid& blockId, const QUuid& segmentId) override;

    /// 静态元数据 (TOOL_SYSTEM_AUDIT P3): id/显示名/图标/快捷键/提示/工厂。
    static ToolDescriptor describe();
    [[nodiscard]] const char* name() const override
    { return reinterpret_cast<const char*>(u8"旋转"); }
    [[nodiscard]] RotateState state() const { return m_state; }

private:
    // ── Target selection ──
    void selectTarget(const QUuid& blockId,
                      const std::optional<cad::geo::Vec2>& clickWorld = std::nullopt);
    void clearTarget();

    // ── Anchor point ──
    void toggleAnchor();
    void rebuildAnchorState();

    // ── Rotation gesture ──
    void beginRotation(const cad::geo::Vec2& pos);
    void updateRotation(const cad::geo::Vec2& pos, bool snap);
    void commitRotation();   ///< Mouse release: restore-then-replay via undo stack.
    void cancelRotation();   ///< Esc mid-drag: restore the session base.
    void commitCurrent();

    void applyAngleDeg(double deg);
    void applyShadowAngleDeg(double deg);
    void applyModeValue(double value);
    void restoreBase();
    [[nodiscard]] double currentAngleDeg() const;
    [[nodiscard]] double currentModeValue() const;
    [[nodiscard]] double segmentRadius() const;
    [[nodiscard]] bool isAngleLocked() const;

    // ── 状态提示与焦点上报 (CONTEXT_STRIP_DESIGN.md 一期) ──
    void updateStatusHint();
    void reportStripTarget();
    void reportRotateAnchorState();

    // ── Protractor gizmo (extracted: RotateGizmo) ──
    void buildGizmo();    ///< Pivot ring + reference dash + arc.
    void updateGizmo();   ///< Refresh arc/dash for the current angle.
    void removeGizmo();
    void applySelectionConfirmed(bool confirmed);
    [[nodiscard]] double originalWorldRotDeg() const;

    // ── Hit testing / helpers ──
    [[nodiscard]] QUuid hitBlock(const cad::geo::Vec2& worldPos) const;
    [[nodiscard]] double currentZoom() const;

    // ── Endpoint aim snap ──
    void checkEndpointAimSnap(double& angleDeg);
    [[nodiscard]] cad::geo::Vec2 endpointAtAngle(double angleDeg) const;
    void clearAimCandidate();

    void syncSelectionVisual();

    // ── Component sessions & trackers ──
    RotateSession m_session;
    RotateAimSnap m_aimSnap;
    RotateInputTracker m_input;
    MultiRotateSession m_multi;

    // ── Core tool state (single explicit gate) ──
    RotateState m_state = RotateState::Idle;
    bool m_selectionConfirmed = false;

    // ── Drag angle tracking ──
    double m_dragCursorAngle0 = 0.0;
    double m_dragCursorAnglePrev = 0.0;
    double m_accumulatedAngleDeg = 0.0;
    double m_dragAngle0 = 0.0;

    // ── Gestures & Gizmo ──
    std::unique_ptr<RotateCopyGesture> m_copyGesture;
    std::unique_ptr<MarqueeGesture> m_marqueeGesture;
    std::unique_ptr<RotateGizmo> m_gizmo;
    ManagedItems m_managed;

    friend class RotateCopyGesture;
};

} // namespace cad::tools
