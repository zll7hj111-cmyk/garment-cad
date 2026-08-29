#pragma once

#include <QUuid>
#include <QString>
#include <QPointer>
#include <QHash>
#include <QMetaObject>
#include <optional>
#include <vector>

#include "Tool.h"
#include "ToolRegistry.h"
#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Duplicate.h"

class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QGraphicsSimpleTextItem;
class QGraphicsRectItem;
#include "canvas/ManagedItems.h"

namespace cad::param { struct Attachment; }

// P2-4: these are QWidget components living in src/ui/ (cad::ui) now —
// tools/ keeps only gestures and state machines.
namespace cad::ui { class AngleHud; }

namespace cad::tools {
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

    /// 选区继承 (D9, 旋转重设计 2026-08-27): 选择工具里框好的选集 → 按 R /
    /// 右键菜单「旋转」进入, 跳过拾取直接进入选集锚点阶段。空选集 no-op。
    void adoptSelection(const QList<QUuid>& blockIds);
    /// True when an adopted/collected selection set is active (multi-block
    /// rigid rotation; single-line sessions still use the classic fields).
    [[nodiscard]] bool hasSelectionSet() const { return !m_selIds.isEmpty(); }
    /// 单线确认流 (D15, 用户拍板 2026-08-27): 选中态 ⇄ 确定态之门。
    /// false = 已选未确认 (点空白取消/点端点切换锚向/不可拖动);
    /// true  = 已确认 (可拖动; 拖动提交后自动回落 false, 再拖需再确认)。
    [[nodiscard]] bool selectionConfirmed() const { return m_selectionConfirmed; }
    /// 当前 gizmo 的确认态视觉 (测试/诊断用; 无 gizmo = false)。RotateGizmo
    /// 此处仅前向声明, 实现落 ToolRotate.cpp。
    [[nodiscard]] bool gizmoConfirmed() const;
    /// 旋转会话中 (有单线/选集目标且非 Idle): CanvasView 借此屏蔽右键
    /// 上下文菜单 —— 确认手势独占该按键 (D15 与发布菜单互斥)。
    [[nodiscard]] bool hasSessionTarget() const
    { return m_state == RotateState::Ready || m_state == RotateState::Rotating; }
    /// 当前锚心所在点 id (测试/诊断用; null = 无会话)。
    [[nodiscard]] QUuid anchorPointId() const { return m_anchorPointId; }

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClick(QGraphicsSceneMouseEvent* event) override;

    void keyPress(QKeyEvent* event) override;

    /// 静态元数据 (TOOL_SYSTEM_AUDIT P3): id/显示名/图标/快捷键/提示/工厂。
    static ToolDescriptor describe();
    [[nodiscard]] const char* name() const override
    { return reinterpret_cast<const char*>(u8"旋转"); }
    [[nodiscard]] RotateState state() const { return m_state; }
    /// 影子会话诊断 (测试/面板提示用): 当前收集的"基准在 S 外"连接数.
    [[nodiscard]] int shadowCount() const { return static_cast<int>(m_shadowAtts.size()); }

private:
    // ── Target selection ──
    /// Select a rotation target. @p clickWorld (可选): 本次选中的点击位置 ——
    /// 锚心初值跟随点击端 (自由线取近端; 连接线恒取挂连接端; 中段/无位置
    /// 保持起点默认)。
    void selectTarget(const QUuid& blockId,
                      const std::optional<cad::geo::Vec2>& clickWorld = std::nullopt);
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
    /// 视图缩放/平移时把 HUD 移回锚心旁 (M8): 旧实现只在 showHud 时 move
    /// 一次, 滚轮缩放/空格平移后悬浮框停在旧屏幕位置与锚心脱节。
    void repositionHud();
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
    /// D15 确认门可视同步 (TOOL_SYSTEM_AUDIT H2): 确认态翻转统一入口 ——
    /// 写标志 + gizmo 确认态样式 + HUD caption 确认提示, 三者缺一就是
    /// "无视觉表达的隐藏状态"。幂等 (同值 no-op)。
    void applySelectionConfirmed(bool confirmed);
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

    // ── 选集旋转 (选区继承 adoptSelection, 2026-08-27 泛化) ──
    /// 两段式: 第一击设锚点 (吸附最近 8px/zoom 内 resolved 点, 否则自由位置).
    void beginSelPivot(const cad::geo::Vec2& pos);
    /// 第二击按下: 起手选集旋转 (收集影子会话, §2.6).
    void beginSelRotation(const cad::geo::Vec2& pos);
    void updateSelRotation(const cad::geo::Vec2& pos, bool snap);
    /// 提交 (restore-then-replay → RotateBlocksCommand, 一步 undo).
    void commitSelRotation();
    void cancelSelRotation();
    /// 施加增量角 (S = m_selIds 全体绕锚点刚体变换 + 影子偏转回写,
    /// resolveForDrag 热路径).
    void applySelDelta(double deltaDeg);
    /// 影子会话收集 (§2.6): 找出 S 内块作为 follower、角度基准方向在 S 外
    /// 的活跃连接, 快照旧 offset 进 m_shadowBase。随 beginSelRotation 调用.
    void collectShadowAttachments();
    /// 任意可捕捉点吸附 (8px/zoom): 返回命中点 id, 失败返回 null.
    [[nodiscard]] QUuid snapAnyPoint(const cad::geo::Vec2& worldPos) const;
    /// 选集虚线包围盒高亮 (随旋转逐帧更新).
    void updateSelHighlightBox();
    void removeSelHighlightBox();

    // ── 选集旋转状态 (选区继承; 组件"W 键整组旋转"模态已删除, 2026-08-29) ──
    bool  m_selectionRotate = false; ///< 选集旋转会话激活 (adoptSelection 置位).
    QList<QUuid> m_selIds;           ///< 旋转选集 S (刚体变换对象全体).
    QHash<QUuid, double> m_shadowBase; ///< 受影响连接的 baselineOffsetDeg 基准.
    QList<QUuid> m_shadowAtts;       ///< 影子会话附件 id (基准在 S 外的活跃连接).
    bool  m_selPivotSet = false;     ///< 第一击已设锚点.
    cad::geo::Vec2 m_selPivot;       ///< 选集旋转锚点 (world mm).
    QUuid m_selPivotPointId;         ///< 吸附到的点 id (null = 自由锚点).
    double m_selDelta = 0.0;         ///< 当前增量角 (deg, 带符号; 0 = 原始位姿).
    double m_selBaselineRad = 0.0;   ///< 拖起始方向 (pivot → 按下点, rad).
    QHash<QUuid, cad::param::Transform2D> m_selBaseTf;  ///< 成员位姿基准.
    QGraphicsRectItem* m_selHighlightBox = nullptr; ///< 选集虚线包围盒.

    // ── Core state ──
    RotateState m_state = RotateState::Idle;

    QUuid m_blockId;               ///< Rotation target.
    bool  m_connected = false;     ///< true = connected (followerAngle), false = free (transform).
    QUuid m_attId;                 ///< Follower attachment id (connected mode).
    /// 单线确认门标志 (D15): 见 selectionConfirmed()。选集会话不使用。
    bool m_selectionConfirmed = false;
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

    // Angle HUD. 命名 m_angleHud 避免遮蔽基类 HudItem* m_hud (P2/H4)。
    QPointer<cad::ui::AngleHud> m_angleHud;
    bool m_hudValid = true;
    /// 最近一次公式解析错误 (M8): 无效时经 AngleHud::setError 展示原因。
    QString m_hudError;
    /// 视图变换 → HUD 重定位的连接 (M8; onDeactivate 断开, 防悬垂 lambda)。
    QMetaObject::Connection m_viewZoomConn;
    QMetaObject::Connection m_viewHScrollConn;
    QMetaObject::Connection m_viewVScrollConn;

    // Endpoint aim snap state.
    QUuid m_aimBlockId;              ///< Candidate target block (null = none).
    QUuid m_aimPointId;              ///< Candidate target point.
    QGraphicsEllipseItem* m_aimRing = nullptr;  ///< Highlight ring on the candidate.
    /// 临时图元统一登记 (deactivate 统一释放 + 影子置空, TOOL_SYSTEM_AUDIT P1/L1)。
    ManagedItems m_managed;

    friend class RotateCopyGesture;
};

} // namespace cad::tools
