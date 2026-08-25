#pragma once

#include <QUuid>
#include <QString>
#include <QPointer>
#include <QHash>
#include <vector>

#include "Tool.h"
#include "document/commands/ComponentCommands.h"
#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Duplicate.h"

class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QGraphicsSimpleTextItem;
class QGraphicsRectItem;

namespace cad::param { struct Attachment; }

namespace cad::tools {

class AngleHud;
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

    void activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void deactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClick(QGraphicsSceneMouseEvent* event) override;

    void keyPress(QKeyEvent* event) override;

    [[nodiscard]] const char* name() const override
    { return "\xe6\x97\x8b\xe8\xbd\xac"; }  // 旋转
    [[nodiscard]] RotateState state() const { return m_state; }
    /// W 键组件整组旋转模式 (2026-12 用户拍板): true = 目标组件整组绕任意
    /// 锚点旋转 (仅当目标线属于某组件时可切换).
    [[nodiscard]] bool groupMode() const { return m_groupMode; }

private:
    // ── Target selection ──
    void selectTarget(const QUuid& blockId);
    void clearTarget();
    /// Non-pin follower attachment whose follower is the target block
    /// (nullptr when the block is free).
    [[nodiscard]] cad::param::Attachment* followerAttachment();
    /// Non-pin follower attachment whose follower point is @p pointId
    /// (nullptr when no link hangs on that point).
    [[nodiscard]] cad::param::Attachment* attachmentAtPoint(const QUuid& pointId);
    /// Live editable attachment: real attachment in ordinary connected mode.
    [[nodiscard]] cad::param::Attachment* editableAttachment();
    [[nodiscard]] const cad::param::Attachment* editableAttachment() const;

    // ── Anchor point (锚心: 起点 ↔ 终点) ──
    /// Toggle the anchor between the start and end points (X key or a click
    /// on the other endpoint). Commits pending edits, then rebuilds the
    /// session state around the new pivot.
    void toggleAnchor();
    /// Rebuild every anchor-derived session field (m_pivot / m_connected /
    /// reference direction / base snapshots) for the current m_anchorIsEnd.
    /// Also marks the follower attachment for release when the pivot sits
    /// OFF the attachment point (旋转 = 放弃跟随).
    void rebuildAnchorState();
    /// The target's endpoint under @p worldPos (start or end), or null when
    /// the click is not near either endpoint (pixel threshold).
    [[nodiscard]] QUuid anchorPointAt(const cad::geo::Vec2& worldPos) const;
    /// Rotation start (plain or copy): if the pivot sits off the attachment
    /// point, detach the follower link (旋转 = 放弃跟随). The link is
    /// snapshotted and restored on Esc / undo. No-op once held.
    void releaseFollowerIfAnchorMoved();

    // ── Rotation gesture ──
    void beginRotation(const cad::geo::Vec2& pos);
    void updateRotation(const cad::geo::Vec2& pos, bool snap);
    void commitRotation();   ///< Mouse release: restore-then-replay via undo stack.
    void cancelRotation();   ///< Esc mid-drag: restore the session base.
    /// Shared restore-then-replay commit (used by mouse release AND HUD Enter).
    void commitCurrent();

    /// Apply an effective angle (degrees): connected → followerAngle (formula
    /// cleared), free → block transform (pivot held fixed).
    void applyAngleDeg(double deg);
    /// Apply a value in the current mode's unit (degrees for Angle, cm for
    /// ArcLength). Converts arc length → angle internally.
    void applyModeValue(double value);
    /// Restore the session base state (baseAngle/baseFormula or baseTf).
    void restoreBase();
    /// Current effective angle (degrees): connected → formula wins over the
    /// numeric offset; free → world angle of the first segment.
    [[nodiscard]] double currentAngleDeg() const;
    /// Current value in the active mode's unit (deg or cm).
    [[nodiscard]] double currentModeValue() const;
    /// Segment radius (mm) for arc-length conversion.
    [[nodiscard]] double segmentRadius() const;

    /// True when the target's follower angle is formula-driven (locked):
    /// drag rotation and plain-number HUD input are refused until the formula
    /// is removed. Based on the committed base formula, so transient HUD edits
    /// never flip the lock mid-session. Free lines are never locked.
    [[nodiscard]] bool isAngleLocked() const;

    // ── Mode switching ──
    void onHudModeChanged(cad::param::RotationMode newMode);

    // ── Angle HUD ──
    void showHud();
    void hideHud();
    void onHudTextChanged(const QString& text);   ///< Live preview.
    void onHudCommit();                            ///< Enter.
    void onHudCancel();                            ///< Esc.
    /// Sync the HUD text to the current effective angle (signals blocked).
    void refreshHudText();

    // ── Protractor gizmo (extracted: RotateGizmo) ──
    /// Rebuild the gizmo around the current pivot/reference direction.
    void buildGizmo();    ///< Pivot ring + reference dash + arc.
    void updateGizmo();   ///< Refresh arc/dash for the current angle.
    void removeGizmo();
    /// Original line's current world direction (deg) about the pivot — the
    /// rotate-copy base: the clone's relative 0° = overlap with the original.
    [[nodiscard]] double originalWorldRotDeg() const;

    // ── Hit testing / helpers ──
    [[nodiscard]] QUuid hitBlock(const cad::geo::Vec2& worldPos) const;
    [[nodiscard]] double currentZoom() const;

    // ── Endpoint aim snap (终点方向吸附) ──
    /// During rotation, search for a point near the rotating endpoint whose
    /// direction from the pivot aligns with the current segment direction.
    /// Returns the candidate's world position and snaps @p angleDeg (in/out,
    /// the mode-appropriate angle) to aim at it. Updates the highlight marker.
    void checkEndpointAimSnap(double& angleDeg);
    /// World position of the rotating segment's end point at a given angle.
    [[nodiscard]] cad::geo::Vec2 endpointAtAngle(double angleDeg) const;
    /// Clear the aim-candidate highlight and forget the pending target.
    void clearAimCandidate();

    // ── W 键组件整组旋转 (2026-12 用户拍板, ROTATE_COMPONENT_DESIGN.md) ──
    /// W 切换: 进入/退出整组旋转模式 (仅目标属组件).
    void toggleGroupMode();
    void enterGroupMode();
    /// 退出整组模式回单线 (保留目标; 若释放未提交先恢复).
    void exitGroupMode();
    /// 第一击: 设锚点 (吸附最近 8px/zoom 内 resolved 点, 否则自由位置).
    void beginGroupPivot(const cad::geo::Vec2& pos);
    /// 第二击按下: 起手整组旋转 (收集 + 释放外部约束, D7).
    void beginGroupRotation(const cad::geo::Vec2& pos);
    void updateGroupRotation(const cad::geo::Vec2& pos, bool snap);
    /// 提交 (restore-then-replay → RotateComponentCommand, 一步 undo).
    void commitGroupRotation();
    void cancelGroupRotation();
    /// 施加增量角 (全体成员绕锚点刚体变换, resolveForDrag 热路径).
    void applyGroupDelta(double deltaDeg);
    /// 收集并释放组件全部外部约束 (D7 判定表); 幂等 (已释放则 no-op).
    void collectAndReleaseComponentExternal();
    /// 恢复被释放的外部约束 + resolveAll (Esc/cancel 路径).
    void restoreComponentExternal();
    /// 任意可捕捉点吸附 (8px/zoom): 返回命中点 id, 失败返回 null.
    [[nodiscard]] QUuid snapAnyPoint(const cad::geo::Vec2& worldPos) const;
    /// 组件虚线包围盒高亮 (防呆: 转的是整组; 随旋转逐帧更新).
    void updateGroupHighlightBox();
    void removeGroupHighlightBox();

    // ── W 键组件整组旋转状态 (2026-12) ──
    QUuid m_compId;                  ///< 当前目标的所属组件 (null = 非组件线).
    bool  m_groupMode = false;       ///< W 切换的整组旋转模式.
    bool  m_groupPivotSet = false;   ///< 第一击已设锚点.
    cad::geo::Vec2 m_groupPivot;     ///< 整组旋转锚点 (world mm).
    QUuid m_groupPivotPointId;       ///< 吸附到的点 id (null = 自由锚点).
    double m_groupDelta = 0.0;       ///< 当前增量角 (deg, 带符号; 0 = 原始位姿).
    double m_groupBaselineRad = 0.0; ///< 拖起始方向 (pivot → 按下点, rad).
    QHash<QUuid, cad::param::Transform2D> m_groupBaseTf;  ///< 成员位姿基准.
    std::vector<cad::cmd::AimRelease>  m_groupReleasedTargets; ///< 外部 endTarget 快照.
    std::vector<cad::cmd::DartRelease> m_groupReleasedDarts;   ///< 外部省道快照.
    std::vector<cad::param::Attachment> m_groupReleasedAtts;   ///< 外部 attachment 快照.
    bool m_groupReleasedHeld = false;  ///< 释放已执行未提交/未恢复.
    QGraphicsRectItem* m_groupHighlightBox = nullptr; ///< 组件虚线包围盒.

    // ── Core state ──
    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    RotateState m_state = RotateState::Idle;

    QUuid m_blockId;               ///< Rotation target.
    bool  m_connected = false;     ///< true = connected (followerAngle), false = free (transform).
    QUuid m_attId;                 ///< Follower attachment id (connected mode).
    cad::geo::Vec2 m_pivot;        ///< Rotation centre (world, mm).
    double m_refWorldRad = 0.0;    ///< Reference direction (rad): leader exit dir (connected) or 0 (free).

    // Rotate-copy gesture (Ctrl+drag 旋转复制)
    RotateCopyGesture* m_copyGesture = nullptr;

    // ── Anchor point (锚心: 起点 ↔ 终点) ──
    bool  m_anchorIsEnd = false;   ///< Anchor on the END point (default: start).
    QUuid m_anchorPointId;         ///< Current anchor point id.
    /// Follower attachment whose fromPoint is NOT the anchor (pivot moved off
    /// the link). Released when the rotation actually starts; restored by
    /// Esc / undo (旋转 = 放弃跟随).
    QUuid m_releaseAttId;
    cad::param::Attachment m_releaseAttBackup;
    bool m_releaseAttHeld = false; ///< Release already executed this session.

    // Session base state — captured at selection, refreshed after each commit.
    double  m_baseAngle = 0.0;     ///< connected: base numeric followerAngle.
    QString m_baseFormula;         ///< connected: base angle formula.
    cad::param::RotationMode m_rotationMode = cad::param::RotationMode::Angle;
    double  m_baseArcLength = 0.0; ///< connected: base arc length (mm).
    QString m_baseArcFormula;      ///< connected: base arc length formula.
    cad::param::Transform2D m_baseTf;  ///< free: base transform.
    QUuid m_baseEndTargetBlock;    ///< free: endpoint-aim constraint at selection
    QUuid m_baseEndTargetPoint;    ///< (released by rotation, restored on undo).

    // Free-mode local geometry snapshot (constant during rotation).
    cad::geo::Vec2 m_anchorLocal;  ///< Anchor point resolvedPos (local).
    double m_localDir = 0.0;       ///< Local direction of the start segment (rad).

    // Drag state.
    double m_dragCursorAngle0 = 0.0;  ///< Cursor polar angle at drag start (rad).
    double m_dragAngle0 = 0.0;        ///< Effective angle at drag start (deg).

    // Gizmo (extracted: RotateGizmo owns the items).
    RotateGizmo* m_gizmo = nullptr;

    // Angle HUD.
    QPointer<AngleHud> m_hud;
    bool m_hudValid = true;

    // Endpoint aim snap state.
    QUuid m_aimBlockId;              ///< Candidate target block (null = none).
    QUuid m_aimPointId;              ///< Candidate target point.
    QGraphicsEllipseItem* m_aimRing = nullptr;  ///< Highlight ring on the candidate.

    friend class RotateCopyGesture;
};

} // namespace cad::tools
