#pragma once

#include "Tool.h"
#include "ToolRegistry.h"
#include "SnapEngine.h"
#include "LineFactory.h"
#include "geometry/Vec2.h"

#include <QGraphicsItem>
#include <QPainter>
#include <QPointer>
#include <optional>

#include "canvas/ManagedItems.h"

class QGraphicsLineItem;
class QGraphicsEllipseItem;
class QGraphicsRectItem;
class QGraphicsPathItem;
class QGraphicsView;
class QDialog;

namespace cad::param { class ParamDocument; class Block; struct ParamPoint; struct Segment; }

// P2-4: these are QWidget components living in src/ui/ (cad::ui) now —
// tools/ keeps only gestures and state machines.
namespace cad::ui { class QuickAuxDialog; }
class HudItem;

namespace cad::tools {
class LeaderCandidatePicker;

/// One-shot pre-input for the NEXT line the smart pen creates (预输入).
/// Typed in the status-bar pre-input strip while the smart pen is active;
/// the values are consumed by the next committed line and then cleared.
/// Length is in cm and angle in degrees — both accept numbers or formulas,
/// matching the SegmentEditBar semantics.
struct LinePreInput
{
    QString name;      ///< Segment name for the next line.
    QString lengthCm;  ///< Length in cm: number or formula.
    QString angleDeg;  ///< Angle in degrees: number or formula.

    /// Angle display convention (与 HUD/闭合基准一致): with a snapped start
    /// it is the follower fold angle (0° = 折叠重叠, 180° = 直行延续);
    /// with a free start it is the absolute world angle (0~360° CCW).
    [[nodiscard]] bool hasLength() const { return !lengthCm.trimmed().isEmpty(); }
    [[nodiscard]] bool hasAngle() const { return !angleDeg.trimmed().isEmpty(); }
};

// HudItem 已收口到 src/canvas/HudItem.h（cad::canvas，TOOL_SYSTEM_AUDIT
// P1/M1）：1/zoom 缩放补偿全仓唯一实现，toast/重叠提示/跟随标签共用。

/// Smart Pen tool — parametric line creation with snapping.
///
/// Two drawing modes cycled with W while idle (直线 ↔ 省道线):
///   Flow B interaction (直线):
///     Click → set start (auto-snap) → move → preview + HUD → click → set end (auto-snap)
///     → line created (host shows the status-bar edit strip, no dialog) → back to Idle.
///   Dart-line flow (省道线, 用户拍板 2026-08):
///     Click A (must snap to an existing point) → click B (a point on a segment)
///     → dialog (offset d, angle β default 90°, name) → line created with its
///     END point computed as B + d·dir(segment at B + β); length/direction are
///     recalculated by the Resolver on every change (线是算出来的).
///
/// Right-click / Esc cancels.  Hold Shift for 45° angle constraint (line mode).
class ToolSmartPen : public Tool
{
public:
    void onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void onDeactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void keyPress(QKeyEvent* event) override;
    void keyRelease(QKeyEvent* event) override;

    /// Replace the pending one-shot pre-input (预输入). The host pushes the
    /// status-bar field values here on every edit and after each tool switch.
    void setPreInput(const LinePreInput& input) { m_preInput = input; }
    [[nodiscard]] const LinePreInput& preInput() const { return m_preInput; }

    /// 静态元数据 (TOOL_SYSTEM_AUDIT P3): id/显示名/图标/快捷键/提示/工厂。
    static ToolDescriptor describe();
    [[nodiscard]] const char* name() const override { return reinterpret_cast<const char*>(u8"智能笔"); }

private:
    enum class State { Idle, Drawing, ConfirmEnd };

    /// Construction mode, cycled with W while idle.
    enum class Mode {
        Line,  ///< Ordinary parametric line (直线).
        Dart,  ///< Dart line: computed end offset from reference point B (省道线).
    };

    /// 状态栏提示 (M5): 直线/省道线 两模式的操作序列完全不同, 提示必须带上
    /// 当前模式 —— 静态 describe() 用 Line 版做默认文案, cycleMode 切换时经
    /// ToolHost::setHintOverride 覆盖。两处同源, 避免文案漂移。
    [[nodiscard]] static QString hintForMode(Mode mode);

    void commitLine(const cad::geo::Vec2& end,
                    const std::optional<SnapResult>& endSnap);
    void cancelLine();
    void clearPreview();

    /// Cycle the construction mode (W while Idle) and announce it.
    void cycleMode();
    /// Commit a dart line from the picked start A and reference B.
    /// Offset/angle formulas (cm domain) are stored live-linked when non-empty.
    void commitDartLine(const QUuid& aBlockId, const QUuid& aPointId,
                        const QUuid& bBlockId, const QUuid& bPointId,
                        double offsetMm, double angleDeg, const QString& name,
                        const QString& offsetFormula = {},
                        const QString& angleFormula = {});
    /// Open the NON-modal dart parameter dialog (offset d, angle β default 90,
    /// name). Non-modal so the user can switch to the variable/formula panel
    /// and copy while it stays open; canvas clicks stay ignored until answered.
    void openDartDialog(const SnapResult& bSnap);

    [[nodiscard]] cad::geo::Vec2 applyAngleSnap(const cad::geo::Vec2& raw) const;
    void updatePreview(const cad::geo::Vec2& effectiveEnd);
    void updateSnapIndicator(const cad::geo::Vec2& worldPos);

    // --- Segment-body snap (线身 X 标记 / 快捷辅助点) ---
    /// Update m_segSnap + the X marker at the cursor's projection onto the
    /// nearest segment body. Suppressed while a point snap is active and on
    /// leader-candidate segments (their body click switches the reference).
    void updateSegMarker(const cad::geo::Vec2& worldPos,
                         const std::optional<SnapResult>* knownPointSnap = nullptr);
    void hideSegMarker();

    // --- 落点确认 (stacked-point disambiguation, 2026-08) ---
    /// All snappable candidates at the SAME spot as @p snap (within
    /// kSnapOverlapEps), nearest first. Empty when the spot is unambiguous.
    /// findSnapCandidates applies the same layer policy as findSnap, so the
    /// pool only ever contains LEGAL attachment targets.
    [[nodiscard]] std::vector<SnapResult> overlapPool(
        const cad::geo::Vec2& spot, const SnapResult& snap) const;
    /// Prefer the active layer's point among @p pool (先选活动层: at a
    /// stacked spot the default pick is the layer the user is working in);
    /// fall back to @p fallback (the raw findSnap result) when none matches.
    [[nodiscard]] std::optional<SnapResult> preferActiveLayer(
        const std::vector<SnapResult>& pool,
        const std::optional<SnapResult>& fallback) const;
    /// The end placement hit several stacked candidates → ConfirmEnd state.
    /// Default target = active-layer pick; clicking a candidate segment
    /// commits with THAT point; blank click accepts the default; Esc cancels
    /// the whole stroke.
    void enterEndConfirm(const SnapResult& snap, const std::vector<SnapResult>& pool);
    /// ConfirmEnd left-click: candidate segment → that point, else default.
    void handleConfirmEndPress(const cad::geo::Vec2& clickPos);
    /// Commit the stroke end pinned to @p endSnap — flips the line when the
    /// start was free (same rule as a plain end snap).
    void commitEndSnap(const SnapResult& endSnap);
    /// Click on a leader-candidate segment during the rubber band: when the
    /// candidate's point is a DIFFERENT stacked candidate at the start spot
    /// (and thus a legal attachment target), switch the START POINT to it.
    /// Returns false when the click stays a pure angle-reference switch.
    bool trySwitchStartPoint(const LeaderCandidate& cand, int candIndex);
    /// Hover highlight of confirmable end candidates (teal — same visual
    /// language as the leader-candidate highlight).
    void updateEndConfirmHighlight(const cad::geo::Vec2& worldPos);
    void clearEndConfirmHighlight();
    /// Drop all stacked-point bookkeeping (start pool / end confirm state).
    void resetStrokeTargets();

    /// Open the NON-modal quick-aux dialog for the snapped segment (percent
    /// pre-filled with the projection t). The user may freely switch to the
    /// variable panel to look up / copy formulas while it is open; canvas
    /// clicks are ignored until the dialog is answered. On accept the aux
    /// point is created (own undo step) and the stroke starts (@p forStart)
    /// or commits with the end pinned to the new point (bridge).
    void openAuxDialog(const SegmentSnapResult& segSnap, bool forStart);
    /// M10 (TOOL_SYSTEM_AUDIT): 非模态弹窗打开期间画布不是"卡死" —— 光标
    /// 置 Forbidden + 一次性 toast 说明原因; 事件早退路径调用以兜底维持。
    void showDialogBlockedFeedback();
    /// 弹窗关闭/工具切走时恢复默认画布光标 (M10 配套)。
    void clearDialogBlockedCursor();
    /// Dialog accepted: create the point and drive the stroke state machine.
    void onAuxDialogAccepted(const cad::param::ParamPoint& pt);
    /// Push AddAuxPointCommand (own undo step — 建点与建线分开撤销) and
    /// synthesize a SnapResult on the resolved point so the normal
    /// attached/bridge flow can pin to it. Nullopt if the host vanished.
    [[nodiscard]] std::optional<SnapResult> commitAuxPoint(
        const cad::param::ParamPoint& pt,
        const QUuid& blockId, const QUuid& segmentId);

    /// Adopt a snap result as the stroke's start point (anchor position,
    /// leader direction cache, leader candidates).
    void setupSnappedStart(const SnapResult& snap);
    /// True when @p userPos does NOT hit any block geometry — the blank-space
    /// right-click gesture (切换工具) only fires there; entity right-clicks
    /// stay reserved for a future context menu.
    [[nodiscard]] bool isBlankSpace(const QPointF& userPos) const;
    /// Transition Idle → Drawing: create the preview/rubber-band items.
    void beginStroke(Qt::KeyboardModifiers mods);
    /// Resolve the pending pre-input into stroke constraints; when BOTH length
    /// and angle are valid the line is committed immediately (one click).
    /// Invalid entries are ignored (toast) without blocking the stroke.
    void startStroke(Qt::KeyboardModifiers mods);
    /// Parse/evaluate the current pre-input into m_strokeInput.
    void captureStrokeInput();
    /// Apply active pre-input constraints to the cursor position (length-only:
    /// keeps the cursor direction; angle-only: projects onto the fixed ray).
    [[nodiscard]] cad::geo::Vec2 applyPreInputConstraints(const cad::geo::Vec2& cursor) const;
    /// Fully determined endpoint (both length and angle present).
    [[nodiscard]] cad::geo::Vec2 fixedPreInputEnd() const;
    /// Input-space angle → world angle: follower convention for a snapped
    /// start, absolute world angle for a free start.
    [[nodiscard]] double toWorldAngleDeg(double displayDeg) const;
    /// Build the LineFactory options snapshot from m_strokeInput.
    [[nodiscard]] LineBuildOptions strokeBuildOptions() const;
    /// One-shot consumption: clear every field that was actually used.
    void consumePreInput();

    State m_state = State::Idle;
    Mode  m_mode  = Mode::Line;  ///< Construction mode (W cycles while Idle).

    LinePreInput m_preInput;       ///< Pending pre-input from the status bar.
    struct StrokeInput {
        LinePreInput raw;          ///< Snapshot taken at stroke start.
        bool   hasLength  = false;
        double lengthMm   = 0.0;   ///< Evaluated length (mm).
        QString lengthFormula;     ///< Non-empty when the length was a formula.
        bool   hasAngle   = false;
        double displayAngleDeg = 0.0;  ///< Input-space angle (follower/world).
        QString angleFormula;      ///< Non-empty when the angle was a formula.
    };
    StrokeInput m_strokeInput;     ///< Active stroke's pre-input snapshot.

    cad::geo::Vec2 m_startPoint;
    bool m_angleSnap = false;
    mutable double m_snapAngleDeg = 0.0;  ///< Follower angle (relative to leader) for HUD.

    // Snap state
    SnapEngine m_snapEngine;
    std::optional<SnapResult> m_startSnap;  ///< If start was snapped to existing point.
    std::optional<SnapResult> m_currentSnap; ///< Current hover snap (for indicator).
    std::optional<SegmentSnapResult> m_segSnap; ///< Current segment-body hover (for X marker).

    // Extracted collaborators: line construction + leader-candidate selection.
    LineFactory* m_lineFactory = nullptr;
    LeaderCandidatePicker* m_leaderPicker = nullptr;

    // Non-modal quick-aux dialog state (open between click and accept)
    QPointer<cad::ui::QuickAuxDialog> m_auxDialog;      ///< Open dialog (null when none).
    bool m_auxDialogForStart = false;          ///< true = start scenario, false = end.
    SegmentSnapResult m_auxDialogSegSnap;      ///< Host segment captured at open time.

    // Non-modal dart parameter dialog (省道线弹窗). Kept non-modal so the
    // user can switch to the variable/formula panel and copy while it is open.
    QPointer<QDialog> m_dartDialog;            ///< Open dialog (null when none).
    /// M10: 上次弹窗阻塞提示文案 (同值守卫 —— mouseMove 每帧早退不刷 toast)。
    QString m_lastDialogToast;

    // Leader candidate state (valid while Drawing with a snapped start)
    std::vector<LeaderCandidate> m_leaderCandidates;
    int   m_leaderIndex = -1;        ///< Index into m_leaderCandidates (-1 = none).
    QUuid m_highlightBlockId;        ///< Block item currently showing the highlight.

    // Stacked-point disambiguation state (落点确认)
    std::vector<SnapResult> m_startPool;  ///< Coincident candidates at the start spot.
    struct EndConfirmCandidate {
        QUuid blockId;
        QUuid segId;
        QUuid pointId;
        SnapResult snap;   ///< The point as a snap target (worldPos = spot).
    };
    std::vector<EndConfirmCandidate> m_endCands; ///< Confirmable segments at the end spot.
    std::optional<SnapResult> m_endAutoPick;     ///< Default end target (active layer preferred).
    QUuid m_endHighlightBlockId;                 ///< End-confirm hover highlight.
    QUuid m_endHighlightSegId;

    // Preview graphics
    QGraphicsLineItem*    m_previewLine  = nullptr;
    QGraphicsEllipseItem* m_startMarker  = nullptr;
    QGraphicsRectItem*    m_snapIndicator = nullptr;
    QGraphicsPathItem*    m_segMarker    = nullptr;  ///< X at the segment projection point.
    // m_hud (canvas/HudItem.h) 现为基类 Tool 的受保护成员 (P2/H4 去遮蔽),
    // 智能笔在其 stroke 期间 new + ManagedItems 登记, 不再单独声明。
    /// 临时图元统一登记 (deactivate 统一释放 + 影子置空, TOOL_SYSTEM_AUDIT P1/L1)。
    ManagedItems m_managed;
};

} // namespace cad::tools
