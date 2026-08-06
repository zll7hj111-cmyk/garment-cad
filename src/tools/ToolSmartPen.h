#pragma once

#include "Tool.h"
#include "SnapEngine.h"
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

/// Smart Pen tool — parametric line creation with snapping and property dialog.
///
/// Flow B interaction:
///   Click → set start (auto-snap) → move → preview + HUD → click → set end (auto-snap)
///   → property dialog pops up → confirm → line created → back to Idle.
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

    /// Create a parametric Block for a free line (no snap).
    void createFreeLine(const cad::geo::Vec2& start, const cad::geo::Vec2& end);

    /// Create a parametric Block attached to an existing block (snapped start).
    void createAttachedLine(const SnapResult& snapStart, const cad::geo::Vec2& end);

    /// Create a bridge line (桥接线): both endpoints pinned to existing points
    /// (snapped start AND end). Length/angle are passive — fully determined by
    /// the two host points; the Resolver re-derives them every pass.
    void createBridgeLine(const SnapResult& snapStart, const SnapResult& snapEnd);

    // --- Leader candidate selection (基准线点选) ---
    /// A leader-segment candidate at the snapped start: any segment incident
    /// to any point coincident with the snap position (within snap radius,
    /// across blocks — attached points from several blocks stack on one spot).
    struct LeaderCandidate {
        QUuid blockId;
        QUuid pointId;    ///< Attachment target point on that block.
        QUuid segmentId;  ///< Segment providing the reference direction.
    };

    /// Collect + rank candidates around the snapped start point.
    void collectLeaderCandidates(const SnapResult& snap);
    /// Choose candidate by index: teal-highlight it and update m_refDirDeg.
    void setLeaderIndex(int index);
    /// Clear highlight and candidate list.
    void clearLeaderState();
    /// Candidate whose segment body is within pick tolerance of worldPos
    /// (nearest wins), or -1. Used for click-to-switch during rubber band.
    [[nodiscard]] int leaderCandidateAt(const cad::geo::Vec2& worldPos,
                                        double zoom) const;

    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    State m_state = State::Idle;

    cad::geo::Vec2 m_startPoint;
    bool m_angleSnap = false;
    mutable double m_snapAngleDeg = 0.0;  ///< Follower angle (relative to leader) for HUD.
    double m_refDirDeg = 0.0;             ///< Leader segment world direction (deg) at snapped start; 0 when free.

    // Snap state
    SnapEngine m_snapEngine;
    std::optional<SnapResult> m_startSnap;  ///< If start was snapped to existing point.
    std::optional<SnapResult> m_currentSnap; ///< Current hover snap (for indicator).
    std::optional<SegmentSnapResult> m_segSnap; ///< Current segment-body hover (for X marker).

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
