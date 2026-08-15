#pragma once

#include "Tool.h"
#include "SnapEngine.h"
#include "LineFactory.h"
#include "geometry/Vec2.h"

#include <QGraphicsItem>
#include <QPainter>
#include <QPointer>
#include <optional>

class QGraphicsLineItem;
class QGraphicsEllipseItem;
class QGraphicsRectItem;
class QGraphicsPathItem;
class QGraphicsView;

namespace cad::param { class ParamDocument; class Block; struct ParamPoint; struct Segment; }

namespace cad::tools {

class QuickAuxDialog;
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

/// Screen-space HUD label that follows the cursor during line drawing.
class HudItem : public QGraphicsItem
{
public:
    explicit HudItem(QGraphicsItem* parent = nullptr);

    void setText(const QString& text);
    void moveToPoint(const cad::geo::Vec2& userPos, const QGraphicsView* view);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

private:
    QString m_text;
    QRectF  m_rect;
};

/// Smart Pen tool — parametric line creation with snapping.
///
/// Flow B interaction:
///   Click → set start (auto-snap) → move → preview + HUD → click → set end (auto-snap)
///   → line created (host shows the status-bar edit strip, no dialog) → back to Idle.
///
/// Right-click / Esc cancels.  Hold Shift for 45° angle constraint.
class ToolSmartPen : public Tool
{
public:
    void activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void deactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void keyPress(QKeyEvent* event) override;
    void keyRelease(QKeyEvent* event) override;

    /// Replace the pending one-shot pre-input (预输入). The host pushes the
    /// status-bar field values here on every edit and after each tool switch.
    void setPreInput(const LinePreInput& input) { m_preInput = input; }
    [[nodiscard]] const LinePreInput& preInput() const { return m_preInput; }

    [[nodiscard]] const char* name() const override { return "\xe6\x99\xba\xe8\x83\xbd\xe7\xac\x94"; }

private:
    enum class State { Idle, Drawing };

    void commitLine(const cad::geo::Vec2& end,
                    const std::optional<SnapResult>& endSnap);
    void cancelLine();
    void clearPreview();

    [[nodiscard]] cad::geo::Vec2 applyAngleSnap(const cad::geo::Vec2& raw) const;
    void updatePreview(const cad::geo::Vec2& effectiveEnd);
    void updateSnapIndicator(const cad::geo::Vec2& worldPos);

    // --- Segment-body snap (线身 X 标记 / 快捷辅助点) ---
    /// Update m_segSnap + the X marker at the cursor's projection onto the
    /// nearest segment body. Suppressed while a point snap is active and on
    /// leader-candidate segments (their body click switches the reference).
    void updateSegMarker(const cad::geo::Vec2& worldPos);
    void hideSegMarker();

    /// Open the NON-modal quick-aux dialog for the snapped segment (percent
    /// pre-filled with the projection t). The user may freely switch to the
    /// variable panel to look up / copy formulas while it is open; canvas
    /// clicks are ignored until the dialog is answered. On accept the aux
    /// point is created (own undo step) and the stroke starts (@p forStart)
    /// or commits with the end pinned to the new point (bridge).
    void openAuxDialog(const SegmentSnapResult& segSnap, bool forStart);
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

    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    State m_state = State::Idle;

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
    QPointer<QuickAuxDialog> m_auxDialog;      ///< Open dialog (null when none).
    bool m_auxDialogForStart = false;          ///< true = start scenario, false = end.
    SegmentSnapResult m_auxDialogSegSnap;      ///< Host segment captured at open time.

    // Leader candidate state (valid while Drawing with a snapped start)
    std::vector<LeaderCandidate> m_leaderCandidates;
    int   m_leaderIndex = -1;        ///< Index into m_leaderCandidates (-1 = none).
    QUuid m_highlightBlockId;        ///< Block item currently showing the highlight.

    // Preview graphics
    QGraphicsLineItem*    m_previewLine  = nullptr;
    QGraphicsEllipseItem* m_startMarker  = nullptr;
    QGraphicsRectItem*    m_snapIndicator = nullptr;
    QGraphicsPathItem*    m_segMarker    = nullptr;  ///< X at the segment projection point.
    HudItem*              m_hud          = nullptr;
};

} // namespace cad::tools
