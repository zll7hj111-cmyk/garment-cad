#pragma once

#include <QUuid>
#include <QSet>
#include <QPointer>

#include <functional>
#include <optional>
#include <vector>

#include "geometry/Vec2.h"
#include "parametric/Attachment.h"
#include "tools/SnapEngine.h"
#include "tools/SelectState.h"
#include "tools/ConnectConfirm.h"       // ConfirmCandidate (阶段 3 拆分)
#include "tools/ConnectOverlapResolver.h"  // m_overlap (阶段 3 拆分)

class QKeyEvent;
class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QUndoStack;
class CanvasScene;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {
/// Connection snap reach (user units): the target-DROP magnet radius — also
/// drives the halo and target-ring visuals (WYSIWYG 铁律: 光环/目标环 = 吸附
/// 范围). Shared by ConnectGesture and ToolSelect (hover endpoint hint).
inline constexpr double kConnectSnapRadius = 7.5;
/// Source-GRAB radius (用户拍板 2026-09): grabbing an endpoint to START a
/// connection is deliberately more generous than the drop radius — grabbing
/// must feel easy (10px), dropping must stay precise (7.5px). ToolSelect's
/// hover endpoint hint uses the same value.
inline constexpr double kConnectGrabRadius = 10.0;

/// Connection gesture of the selection tool: dragging from an endpoint to
/// establish a new attachment, with snap-ring feedback, overlapping-target
/// disambiguation (ConfirmTarget) and the live angle/arc HUD (AngleInput).
/// The owning tool dispatches mouse/key events here while active() and
/// forwards state transitions through the injected callbacks.
class ConnectGesture
{
public:
    using Vec2 = cad::geo::Vec2;

    /// @p setState transitions the owning tool into Connecting /
    ///        ConfirmTarget / AngleInput (and back to Confirmed/Idle when
    ///        the gesture ends).
    /// @p showToast transient canvas toast (cross-layer feedback).
    /// @p clearSelectionAndIdle drop the selection lock after a committed
    ///        connection (same as a completed move).
    /// @p selectionEmpty the owner's selection emptiness query (decides
    ///        whether a failed connect returns to Confirmed or Idle).
    /// @p beginAngleSession 上下文属性条 (二期): 进入/退出连接角度会话。
    ///        (followerBlockId, followerSegmentId, attachmentId, initialAngleDeg);
    ///        四个参数全 null = 会话结束 (条带收起)。
    /// @p angleValidity 输入合法性 (二期): 公式解析失败时条带红边提示。
    ConnectGesture(CanvasScene* scene, cad::param::ParamDocument* doc,
                   QUndoStack* undoStack,
                   const std::function<void(SelectState)>& setState,
                   const std::function<void(const QString&)>& showToast,
                   const std::function<void()>& clearSelectionAndIdle,
                   const std::function<bool()>& selectionEmpty,
                   const std::function<void(const QUuid&, const QUuid&,
                                            const QUuid&, double)>& beginAngleSession,
                   const std::function<void(bool)>& angleValidity);
    ~ConnectGesture();

    [[nodiscard]] SelectState state() const { return m_state; }
    [[nodiscard]] bool active() const {
        return m_state == SelectState::Connecting
            || m_state == SelectState::ConfirmTarget
            || m_state == SelectState::ConfirmSource
            || m_state == SelectState::AngleInput;
    }

    /// Start the gesture (endpoint press on a selected block). No-op for
    /// bridges (pinned at both ends) and missing blocks.
    void beginConnect(const QUuid& fromBlockId, const QUuid& fromPointId,
                      const Vec2& pos);
    /// Mouse move while Connecting / ConfirmTarget.
    void move(const Vec2& pos);
    /// Left release while Connecting: attach to the snap target, enter
    /// ConfirmTarget on overlapping candidates, or commit the plain move.
    void release(const Vec2& pos);
    /// Press while ConfirmTarget: click a candidate segment to confirm the
    /// leader; blank / non-candidate click cancels the gesture.
    void pressConfirmTarget(const Vec2& pos);
    /// Enter ConfirmSource when several source endpoints overlap at the same
    /// spot. The user clicks one of
    /// the candidate member segments to choose which endpoint starts
    /// the connection.
    void beginSourceConfirm(std::vector<ConfirmCandidate> candidates,
                            const Vec2& pos);
    /// Press while ConfirmSource: click a candidate member segment; the chosen
    /// endpoint then begins a normal connection drag. Blank /
    /// non-candidate click cancels.
    void pressConfirmSource(const Vec2& pos);
    /// Press while AngleInput (组件级连接重叠切换, 用户要求 2026-09): when the
    /// connected point has several overlapping endpoints, click one of the
    /// candidate leader segments to re-pick the followed object (点选重叠点的
    /// 线段确定跟随对象). Non-candidate click keeps the current connection.
    void pressAngleTarget(const Vec2& pos);
    /// Esc / right-click: abort the gesture and restore the pre-drag state.
    void cancel();
    /// Key events while AngleInput (Esc / Enter; the strip owns all others).
    /// Returns true when the key was consumed.
    bool keyPress(QKeyEvent* event);

    // ── 连接角度会话输入 (二期: 上下文属性条 → 手势) ──
    // 条带是纯输入面, 连接语义 (预览/换算/收尾) 全部留在这里 —— 与旧浮动
    // AngleHud 的四个回调一一对应。
    /// 角度输入框击键 (全文, 实时预览; 每键一报)。
    void onAngleTextChanged(const QString& text);
    /// ° / ⌒ 单位切换 (几何保持换算, 公式驱动拒绝切换)。
    void onAngleModeChanged(cad::param::RotationMode mode);
    /// Enter: 确认角度并收尾 (finalizeConnection)。
    void commitAngle();
    /// Esc: 保留连接、角度回退保向初值并收尾 (与旧 HUD 同语义)。
    void cancelAngle();

    /// Nearest point endpoint within snap radius (connect source / the
    /// point-level press dispatch in the owning tool).
    [[nodiscard]] std::optional<SnapResult> hitPoint(const Vec2& worldPos) const;

    /// All candidate endpoints within snap radius (for disambiguating stacked points).
    [[nodiscard]] std::vector<SnapResult> hitPointCandidates(const Vec2& worldPos) const;

private:
    /// Update the gesture's OWN state and forward it to the owning tool. The
    /// owner routes events back into this gesture through active()/move()/
    /// release()/keyPress(), all of which read m_state — so the local state
    /// MUST stay in sync with the tool's state (the tool dispatches on the
    /// value this gesture handed it via m_setState).
    void setState(SelectState s) { m_state = s; m_setState(s); }

    /// Establish the attachment toward the given leader point/segment and move
    /// into AngleInput. Returns false when rejected (cycle etc.).
    bool attachToTarget(const QUuid& toBlockId, const QUuid& toPointId,
                        const QUuid& toSegmentId);
    /// 连接候选校验 (move 磁铁判定 / release 候选池共用): 仅角度 (angleOnly)
    /// 源排除将被重挂的自身附件 (重挂 = 原附件换代, 不占新 follower 名额),
    /// 其余与 checkAttachment 完全同规 (环 / 跨层单向契约照旧).
    [[nodiscard]] bool lineConnectValid(const QUuid& toBlockId,
                                        const QUuid& toPointId) const;
    /// 该块现存的仅角度 (angleOnly) 跟随附件 id (位置自由、角度随基准线 =
    /// 使用了引用线段但没有连接线段); 无则空 id.
    [[nodiscard]] QUuid angleOnlyAttachmentId(const QUuid& fromBlockId) const;
    /// 仅角度线拖端点重挂: 旧角度基准保留为独立角度基准 (双基准), 位置挂到
    /// 新端点, 恢复完整连接并重新焊接. 旧附件态在此快照, finalizeConnection
    /// 经 ReconnectAttachmentCommand 整步撤销 (无 undo 栈时原地生效).
    bool reattachAngleOnly(const QUuid& attId, const QUuid& toBlockId,
                           const QUuid& toPointId, const QUuid& toSegmentId);
    /// 影子基准重挂路由 (拆开影子基准, DETACH_SHADOW_DESIGN.md §7.4):
    /// 拖影子基准跟随线的端点释放到目标线 —— 目标 = 影子本体 → ⑤ 挂回本体
    /// (删影子 + 活引用恢复, SetAttachmentAngleOnlyCommand(false) 影子模式);
    /// 目标 = 其他线 → ③ 影子挂载 (ShadowMountCommand: Att1 反算保向 +
    /// Att2 重新焊接, L3→影子→L2 链)。仅连接语义: 不进入角度输入会话。
    bool reattachShadowBased(const QUuid& attId, const cad::param::Block& shadow,
                             const QUuid& toBlockId, const QUuid& toPointId,
                             const QUuid& toSegmentId);
    /// True when the source is a component member and a COMPONENT-level
    /// connection should be created (整组作为 follower, 借用暴露端点) instead of
    /// a line-level one. Component-level connections do NOT occupy any member
    /// block's line-level follower slot (森林不变式 组件维度).
    [[nodiscard]] bool isComponentConnect() const { return !m_connectComponentId.isNull(); }
    /// Component-level attach pre-check: this component must not already follow
    /// an external line (一个组件至多一条外部跟随线).
    [[nodiscard]] bool componentCanConnect() const;
    /// Failed connect (released away from any target): commit the plain move
    /// (+ quick-detach) through the undo stack.
    void commitConnectMove();
    /// Mouse move during ConfirmTarget: highlight the hovered candidate line.
    void updateCandidateHighlight(const Vec2& pos,
                                  const std::vector<ConfirmCandidate>& candidates);
    /// 以下 7 个为薄转发 (阶段 3): 图元管理迁入 ConnectOverlapResolver,
    /// 调用点保持原签名, 实现只转发给 m_overlap。
    void removeConfirmHighlight();
    void updateSourcePortMarker();
    void removeSourcePortMarker();
    void updateConnectMarker();
    void removeConnectMarker();
    void updateConnectHalo();
    void removeConnectHalo();
    /// Re-target the in-flight component-level attachment (AngleInput stage)
    /// onto the confirmed candidate; uses the same angle back-solve as a fresh
    /// attach (zero visual jump) and rolls back on validation failure.
    bool switchComponentTarget(const ConfirmCandidate& cand);

    /// 进入连接角度会话 (二期): 向宿主上报跟随线段 + 附件, 条带进入
    /// 角度编辑模式。会话内逐键预览/收尾都经宿主回传 (onAngleTextChanged 等)。
    void beginAngleSession(const QUuid& attachmentId, double initialAngle);
    /// 退出连接角度会话 (二期): 上报全 null (条带收起、焦点回画布)。
    void endAngleSession();

    void finalizeConnection();                       ///< Snapshot→remove→push cmd.

    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;
    std::function<void(SelectState)> m_setState;
    std::function<void(const QString&)> m_showToast;
    std::function<void()> m_clearSelectionAndIdle;
    std::function<bool()> m_selectionEmpty;
    /// 连接角度会话上报 (二期): (followerBlockId, followerSegmentId,
    /// attachmentId, initialAngleDeg); 全 null = 会话结束。
    std::function<void(const QUuid&, const QUuid&, const QUuid&, double)>
        m_beginAngleSession;
    /// 连接角度输入合法性上报 (二期, 红边提示)。
    std::function<void(bool)> m_angleValidity;

    SelectState m_state = SelectState::Idle;   ///< This gesture's state (mirrors the tool's).

    // Connection state (block physically follows the cursor while connecting)
    QUuid m_connectFromBlock;
    QUuid m_connectFromPoint;
    /// Non-null when the source endpoint belongs to a component member: the
    /// connection is COMPONENT-level (整组作为 follower, 借用端点).
    QUuid m_connectComponentId;
    /// Component-level drag preview: pre-drag origins of every member (the
    /// WHOLE component follows the cursor as one rigid unit).
    QHash<QUuid, Vec2> m_connectOrigOrigins;
    Vec2 m_connectGrabOffset;         ///< origin − fromPointWorld at press.
    Vec2 m_connectOrigOrigin;         ///< Pre-drag origin (undo restore).
    double m_connectOrigRotation = 0.0;  ///< Pre-drag rotation (undo restore).
    // 滑轨模式拖动 (抽屉式滑动, 用户拍板 2026-08): the slide attachment stays
    // ACTIVE during the drag (never quick-detached) — the endpoint drag slides
    // along the rail. Snapshot its pre-drag rail coordinates for undo.
    QUuid m_connectSlideAttId;                    ///< Slide attachment of the dragged block.
    double m_connectOldSlideAlong = 0.0;          ///< Pre-drag slideAlongMm (undo restore).
    double m_connectOldSlidePerp  = 0.0;          ///< Pre-drag slidePerpMm (undo restore).
    SnapEngine m_snapEngine;
    std::optional<SnapResult> m_connectTarget;
    /// 画布图元 + 重叠候选收集 (阶段 3 拆分): ConfirmTarget/Source 高亮/标记、
    /// 吸附环、源光晕, 以及候选线段收集。same-lifetime as this gesture.
    ConnectOverlapResolver m_overlap;

    // Overlapping-target disambiguation (ConfirmTarget state): candidate
    // leader SEGMENTS whose endpoint lies on the connection spot.
    std::vector<ConfirmCandidate> m_confirmCandidates;
    /// Source-side disambiguation (ConfirmSource state): after the user picks
    /// a segment, this remembers the chosen endpoint so the source-port
    /// marker persists until the connection is committed or cancelled.
    std::optional<ConfirmCandidate> m_selectedSourceCandidate;

    /// AngleInput 重叠切换候选 (组件级连接, 用户要求 2026-09): leader segments
    /// whose endpoints stack on the connection spot. Empty for line-level
    /// connections / no overlaps. Clicking one re-picks the followed object.
    std::vector<ConfirmCandidate> m_componentSwitchCandidates;

    // 仅角度重挂 (2026-12): 本连接由原附件重挂建立 (true) — finalizeConnection
    // 走 ReconnectAttachmentCommand 单步撤销 (含 HUD 角度调整); 重挂前的
    // 仅角度附件态在 reattachAngleOnly 时快照。
    bool m_reattachActive = false;
    cad::param::Attachment m_reattachOldAtt;

    // Angle session state (二期: 输入面 = 上下文属性条)
    QUuid m_editingAttachmentId;            ///< Attachment being angle-tuned.
    double m_initialAngle = 0.0;            ///< Orientation-preserving angle at connect time.
    bool  m_angleValid = true;
    cad::param::RotationMode m_angleMode = cad::param::RotationMode::Angle;

};


} // namespace cad::tools
