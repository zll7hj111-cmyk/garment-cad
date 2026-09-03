#pragma once

#include <QUuid>
#include <QString>
#include <QHash>
#include <QList>
#include <QSet>
#include <optional>
#include <vector>

#include "Tool.h"
#include "ToolRegistry.h"
#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Duplicate.h"

class QGraphicsEllipseItem;
class QGraphicsSimpleTextItem;
#include "canvas/ManagedItems.h"

namespace cad::param { struct Attachment; }

// P2-4: QWidget components live in src/ui/ (cad::ui) — tools/ keeps only
// gestures and state machines. 一期起旋转工具不再持有任何 QWidget: 角度的
// 精确输入归上下文属性条 (app 层), 状态提示经 ToolHost::setHintOverride。

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
    [[nodiscard]] const RotateGizmo* gizmo() const { return m_gizmo; }
    /// 旋转会话中 (有单线/选集目标且非 Idle): CanvasView 借此屏蔽右键
    /// 上下文菜单 —— 确认手势独占该按键 (D15 与发布菜单互斥)。
    [[nodiscard]] bool hasSessionTarget() const
    { return m_state == RotateState::Ready || m_state == RotateState::Rotating; }
    /// 当前锚心所在点 id (测试/诊断用; null = 无会话)。
    [[nodiscard]] QUuid anchorPointId() const { return m_anchorPointId; }
    [[nodiscard]] cad::geo::Vec2 pivot() const { return m_pivot; }
    [[nodiscard]] bool pivotPicked() const { return m_pivotPicked; }
    [[nodiscard]] bool hoverSnapped() const { return m_hoverSnapped; }
    [[nodiscard]] cad::geo::Vec2 hoverSnapPoint() const { return m_hoverSnapPoint; }

    /// 继承选择集 (从选择工具切换进入或外部多选设置)
    void adoptSelection(const QSet<QUuid>& blockIds);
    [[nodiscard]] const QSet<QUuid>& selection() const { return m_selection; }
    [[nodiscard]] bool isMultiSelect() const { return m_selection.size() > 1; }

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
    /// 影子角度通道 (R6/R8): 锁定 offset 的旋转落点 —— 拆开态写影子 rotation
    /// + 跟随线绕 p3 原地转; 挂载态写 Att1 Δ (链式随动)。
    void applyShadowAngleDeg(double deg);
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
    /// drag rotation and plain-number strip input are refused until the formula
    /// is removed. Based on the committed base formula, so transient strip
    /// edits never flip the lock mid-session. Free lines are never locked.
    [[nodiscard]] bool isAngleLocked() const;

    // ── 状态提示与焦点上报 (CONTEXT_STRIP_DESIGN.md 一期) ──
    /// 状态栏 L1 提示的唯一出口: 确认门 / 拖动中 / 旋转复制读数。
    ///
    /// 角度**数值**读数归上下文属性条 (它订阅 resolved, 拖动时自动跟着变);
    /// 这里是"现在处于什么状态、该按什么"的离散提示 —— 只有旋转复制会带
    /// 数值 (条带在复制态按拍板不显示角度, 相对角只能落这里)。
    void updateStatusHint();
    /// 把当前目标线段报给上下文属性条 (null = 解除锁定)。
    void reportStripTarget();
    /// 上报锚心状态给上下文属性条 (2026-12): 旋转会话内换向 = 切换锚心,
    /// 条带据此显示锚心端在前 + 换向按钮资格 (含禁用原因)。
    void reportRotateAnchorState();

    // ── Protractor gizmo (extracted: RotateGizmo) ──
    /// Rebuild the gizmo around the current pivot/reference direction.
    void buildGizmo();    ///< Pivot ring + reference dash + arc.
    void updateGizmo();   ///< Refresh arc/dash for the current angle.
    void removeGizmo();
    /// D15 确认门可视同步 (TOOL_SYSTEM_AUDIT H2): 确认态翻转统一入口 ——
    /// 写标志 + gizmo 确认态样式 + 状态栏确认提示, 三者缺一就是
    /// "无视觉表达的隐藏状态"。幂等 (同值 no-op)。
    /// (H2 的第三种表达原是 HUD caption 后缀, 一期随 AngleHud 退场迁入状态栏。)
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

    // ── 影子角度通道 (R6/R8, 拆开影子基准) ──
    /// offset 被公式/变量锁定 (isAngleLocked) 且基准是影子块时, 旋转不再
    /// 拒绝 —— 改写影子角度 (拆开态=影子 transform.rotation, 挂载态=Att1
    /// followerAngle Δ), 不碰公式。数字 offset 时 m_shadowId 为空 = 照旧写
    /// followerAngle (影子不动)。
    QUuid m_shadowId;              ///< 影子块 id (空 = 非影子通道)。
    bool  m_shadowMounted = false; ///< true = 挂载态 (Att1 存在, 写 Δ)。
    QUuid m_att1Id;                ///< 挂载态: Att1 (影子→宿主) id。
    double m_shadowRot0 = 0.0;     ///< 拆开态: 影子 rotation 基线 (rad)。
    double m_shadowDelta0 = 0.0;   ///< 挂载态: Att1 followerAngle 基线 (deg)。
    cad::param::Transform2D m_shadowTf0;      ///< 拆开态: 影子完整基线 transform。
    cad::param::Transform2D m_followerTf0;    ///< 拆开态: 跟随线基线 transform (R8 p3 轴心回写)。

    // Free-mode local geometry snapshot (constant during rotation).
    cad::geo::Vec2 m_anchorLocal;  ///< Anchor point resolvedPos (local).
    double m_localDir = 0.0;       ///< Local direction of the start segment (rad).

    // Drag state.
    double m_dragCursorAngle0 = 0.0;     ///< Cursor polar angle at drag start (rad).
    double m_dragCursorAnglePrev = 0.0;  ///< Cursor polar angle on previous move frame (rad).
    double m_accumulatedAngleDeg = 0.0;  ///< Continuous accumulated angle delta (deg).
    double m_dragAngle0 = 0.0;           ///< Effective angle at drag start (deg).

    // Gizmo (extracted: RotateGizmo owns the items).
    RotateGizmo* m_gizmo = nullptr;

    // Endpoint aim snap state.
    QUuid m_aimBlockId;              ///< Candidate target block (null = none).
    QUuid m_aimPointId;              ///< Candidate target point.
    QGraphicsEllipseItem* m_aimRing = nullptr;  ///< Highlight ring on the candidate.
    /// 临时图元统一登记 (deactivate 统一释放 + 影子置空, TOOL_SYSTEM_AUDIT P1/L1)。
    ManagedItems m_managed;

    // ── Multi-selection & Marquee ──
    QSet<QUuid> m_selection;
    bool m_isMarqueeSelected = false;
    MarqueeGesture* m_marqueeGesture = nullptr;
    void syncSelectionVisual();

    // ── Yellow Snap Ring (悬停端点黄色预览圈) ──
    QGraphicsEllipseItem* m_hoverSnapRing = nullptr;
    cad::geo::Vec2 m_hoverSnapPoint;
    bool m_hoverSnapped = false;
    void ensureHoverSnapRing();
    void updateHoverSnapRing(const cad::geo::Vec2& worldPos);
    void hideHoverSnapRing();

    // ── Press-pending (点下直接拖动 vs 点下定锚点) ──
    bool m_pivotPicked = false;
    bool m_pressPending = false;
    cad::geo::Vec2 m_pressPos;
    cad::geo::Vec2 m_pendingPivot;
    bool m_pendingHoverSnapped = false;

    // ── Multi-block rotation base state ──
    struct MultiBlockBase {
        cad::param::Transform2D tf;
        QUuid endTargetBlock;
        QUuid endTargetPoint;
    };
    QHash<QUuid, MultiBlockBase> m_multiBaseTf;
    std::vector<cad::param::Attachment> m_multiReleasedAtts;

    friend class RotateCopyGesture;
};

} // namespace cad::tools
