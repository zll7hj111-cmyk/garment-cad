#pragma once

#include <QUuid>
#include <QSet>
#include <QPointer>

#include <functional>
#include <optional>
#include <vector>

#include "geometry/Vec2.h"
#include "parametric/Attachment.h"
#include "parametric/Group.h"
#include "tools/SnapEngine.h"
#include "tools/SelectState.h"

class QKeyEvent;
class QGraphicsEllipseItem;
class QGraphicsPathItem;
class QUndoStack;
class CanvasScene;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

class AngleHud;

/// Candidate leader segment for overlapping-target disambiguation
/// (ConfirmTarget state): a segment whose endpoint lies on the connection
/// spot. Clicking it confirms the leader point + reference segment.
struct ConfirmCandidate {
    QUuid blockId;
    QUuid segId;
    QUuid pointId;   ///< The endpoint ON the connection position.
};

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
    ConnectGesture(CanvasScene* scene, cad::param::ParamDocument* doc,
                   QUndoStack* undoStack,
                   const std::function<void(SelectState)>& setState,
                   const std::function<void(const QString&)>& showToast,
                   const std::function<void()>& clearSelectionAndIdle,
                   const std::function<bool()>& selectionEmpty);
    /// Release the viewport-overlay HUD (parented to the viewport, which
    /// outlives the gesture — the QPointer stays valid if the viewport is
    /// torn down first).
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
    /// spot (e.g. component members sharing a corner). The user clicks one of
    /// the candidate member segments to choose which member endpoint starts
    /// the connection.
    void beginSourceConfirm(std::vector<ConfirmCandidate> candidates,
                            const Vec2& pos);
    /// Press while ConfirmSource: click a candidate member segment; the chosen
    /// member endpoint then begins a normal connection drag. Blank /
    /// non-candidate click cancels.
    void pressConfirmSource(const Vec2& pos);
    /// Esc / right-click: abort the gesture and restore the pre-drag state.
    void cancel();
    /// Key events while AngleInput (Esc / Enter; the HUD owns all others).
    /// Returns true when the key was consumed.
    bool keyPress(QKeyEvent* event);

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
    /// Failed connect (released away from any target): commit the plain move
    /// (+ quick-detach) through the undo stack.
    void commitConnectMove();
    /// Mouse move during ConfirmTarget: highlight the hovered candidate line.
    void updateConfirmHighlight(const Vec2& pos);
    void removeConfirmHighlight();
    /// Show/move the small source-port marker at the selected source line's
    /// overlapping endpoint (replaces the old yellow line in ConfirmSource).
    void updateSourcePortMarker();
    void removeSourcePortMarker();
    /// Collect leader segments whose endpoint lies on the connection spot
    /// (overlapping-target disambiguation candidates).
    std::vector<ConfirmCandidate> collectConfirmCandidates(
        const Vec2& connWorldPos) const;

    void showAngleHud(const Vec2& anchorUser);
    void hideAngleHud();
    void onAngleTextChanged(const QString& text);   ///< Live preview.
    void onAngleModeChanged(cad::param::RotationMode mode);  ///< Mode toggled.
    void commitAngle();                              ///< Enter: finalize.
    void cancelAngle();                              ///< Esc: keep conn, angle 0.
    void finalizeConnection();                       ///< Snapshot→remove→push cmd.

    /// Show/move/hide the snap-target ring.
    void updateConnectMarker();
    void removeConnectMarker();
    /// Drop the source-point halo.
    void removeConnectHalo();
    /// Create/reposition the source-point halo: a dashed ring around the
    /// dragged point whose radius equals the connect snap radius (the
    /// magnet's reach) — a target entering the halo will be snapped.
    void updateConnectHalo();

    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;
    std::function<void(SelectState)> m_setState;
    std::function<void(const QString&)> m_showToast;
    std::function<void()> m_clearSelectionAndIdle;
    std::function<bool()> m_selectionEmpty;

    SelectState m_state = SelectState::Idle;   ///< This gesture's state (mirrors the tool's).

    // Connection state (block physically follows the cursor while connecting)
    QUuid m_connectFromBlock;
    QUuid m_connectFromPoint;
    QUuid m_connectGroupId;               ///< Group ID if connecting a group member.
    QHash<QUuid, Vec2> m_connectGroupOrigOrigins; ///< Group members' pre-drag origins.
    Vec2 m_connectGrabOffset;         ///< origin − fromPointWorld at press.
    Vec2 m_connectOrigOrigin;         ///< Pre-drag origin (undo restore).
    double m_connectOrigRotation = 0.0;  ///< Pre-drag rotation (undo restore).
    std::optional<cad::param::Attachment> m_connectOldAtt;  ///< Detached at drag start (快拆).
    // 滑轨模式拖动 (抽屉式滑动, 用户拍板 2026-08): the slide attachment stays
    // ACTIVE during the drag (never quick-detached) — the endpoint drag slides
    // along the rail. Snapshot its pre-drag rail coordinates for undo.
    QUuid m_connectSlideAttId;                    ///< Slide attachment of the dragged block.
    double m_connectOldSlideAlong = 0.0;          ///< Pre-drag slideAlongMm (undo restore).
    double m_connectOldSlidePerp  = 0.0;          ///< Pre-drag slidePerpMm (undo restore).
    SnapEngine m_snapEngine;
    std::optional<SnapResult> m_connectTarget;
    QGraphicsEllipseItem* m_connectMarker = nullptr;  ///< Snap-target ring (owned).
    QGraphicsEllipseItem* m_connectHalo   = nullptr;  ///< Source-point halo (owned).

    // Overlapping-target disambiguation (ConfirmTarget state): candidate
    // leader SEGMENTS whose endpoint lies on the connection spot.
    std::vector<ConfirmCandidate> m_confirmCandidates;
    /// Source-side disambiguation (ConfirmSource state): after the user picks
    /// a member segment, this remembers the chosen endpoint so the source-port
    /// marker persists until the connection is committed or cancelled.
    std::optional<ConfirmCandidate> m_selectedSourceCandidate;
    QGraphicsPathItem* m_confirmHighlight = nullptr;  ///< Hovered candidate line (ConfirmTarget only).
    QGraphicsEllipseItem* m_sourcePortMarker = nullptr;  ///< Selected source endpoint marker.

    // Angle HUD state
    QPointer<AngleHud> m_angleHud;   ///< Viewport-overlay angle input (owned).
    QUuid m_editingAttachmentId;            ///< Attachment being angle-tuned.
    bool m_connectIsComponentHinge = false;   ///< True when the pending connect is a component hinge (路线B).
    QUuid m_editingComponentGroupId;          ///< Group being hinge-connected.
    cad::param::ComponentHinge m_editingComponentHinge; ///< Snapshot for finalize/undo.
    double m_initialAngle = 0.0;            ///< Orientation-preserving angle at connect time.
    bool  m_angleValid = true;
    cad::param::RotationMode m_angleMode = cad::param::RotationMode::Angle;
};

} // namespace cad::tools
