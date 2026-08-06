#pragma once

#include <QUuid>
#include <QList>
#include <QHash>
#include <QSet>
#include <QRectF>
#include <optional>
#include <utility>
#include <vector>

#include "Tool.h"
#include "geometry/Vec2.h"
#include "parametric/Attachment.h"
#include "parametric/Duplicate.h"
#include "tools/SnapEngine.h"

class QGraphicsRectItem;
class QGraphicsEllipseItem;
class QGraphicsPathItem;

namespace cad::tools {

class AngleHud;   ///< Angle-entry HUD (shared widget, AngleHud.h).

/// Candidate leader segment for overlapping-target disambiguation
/// (ConfirmTarget state): a segment whose endpoint lies on the connection
/// spot. Clicking it confirms the leader point + reference segment.
struct ConfirmCandidate {
    QUuid blockId;
    QUuid segId;
    QUuid pointId;   ///< The endpoint ON the connection position.
};

/// Interaction state machine for the selection tool (ETCAD-inspired).
enum class SelectState {
    Idle,           ///< No selection, no active interaction.
    Selecting,      ///< Selection set non-empty but NOT yet confirmed.
    Confirmed,      ///< Selection confirmed via right-click; drag/connect-ready.
    Marquee,        ///< Left-drag on empty space: drawing selection rectangle.
    Dragging,       ///< Moving confirmed selection (anchored at blank-space press).
    CopyDragging,   ///< Ctrl+drag on a segment: dragging freshly cloned copies.
    Connecting,     ///< Dragging from an endpoint to establish a new connection.
    ConfirmTarget,  ///< Multiple overlapping target points: click a candidate
                    ///< segment (whose endpoint lies on the connection spot) to
                    ///< confirm the leader; Esc/blank cancels.
    AngleInput,     ///< Connection made; HUD active for construction-angle entry.
};

/// Selection behaviour mode (W toggles within the select tool).
enum class SelectionMode {
    Multi,   ///< Click toggles blocks in/out of the selection; marquee supported.
    Single,  ///< Click selects exactly ONE block (replaces previous); no marquee.
};

/// Selection tool with ETCAD-style interaction:
///   - Left-drag on empty space → marquee (intersect = select; toggle add/remove)
///   - Click an object → toggle it in/out of the selection (red highlight);
///     a GROUP member toggles the WHOLE group (group = minimal selection unit)
///   - SINGLE mode: clicking selects ONE block (replaces previous); no marquee
///   - Both modes share ONE 选中→确认→操作 flow: right-click confirms the
///     selection (stays red, becomes drag/connect-ready); right-click AGAIN on
///     the confirmed set opens the context menu (做成组 / 解散组 / 取消选择)
///   - After confirm, press on blank space → drag anchor; move whole selection
///     (internal group attachments are NEVER detached by a drag)
///   - Drag release → move committed, selection cleared, back to Idle
///   - Ctrl+press on a segment → quick copy (快捷复制): clones follow the
///     cursor, release drops them (one undo step), Esc aborts; copying a
///     WHOLE group yields a new group (副本成新组)
///   - After confirm, drag from an endpoint → connection preview → release on
///     target → attach, then angle HUD (live preview, formula-aware)
///   - Double-click segment → property dialog; Del → delete; Esc → clear
///   - 组是纯选择快捷方式 (零限制): 成组/解散不动几何, 连接/拆/删不受组约束,
///     组员整组进整组出 (badge/点击整选)
class ToolSelect : public Tool
{
public:
    void activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void deactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClick(QGraphicsSceneMouseEvent* event) override;

    void keyPress(QKeyEvent* event) override;

    [[nodiscard]] const char* name() const override { return "\xe9\x80\x89\xe6\x8b\xa9"; }
    [[nodiscard]] SelectState state() const { return m_state; }

    /// Drop the current selection (used when the active canvas layer switches,
    /// since selection is scoped to the active layer).
    void clearSelectionOnLayerChange();

    /// External selection entry (面板联动): select + CONFIRM the given blocks
    /// (expanded to whole groups) — used by the GroupPanel's card click.
    void selectBlocksExternally(const QList<QUuid>& blockIds);

    /// True when the given user-coordinate position hits a group badge —
    /// badge clicks belong to the badge (选中整组), the tool must not act
    /// on the block underneath.
    [[nodiscard]] bool badgeAt(const cad::geo::Vec2& pos) const;

private:
    // ── State transitions ──
    void setState(SelectState s);
    void clearSelectionAndIdle();

    // ── Selection mode (W toggle) ──
    void toggleSelectionMode();
    void showModeToast();   ///< Transient canvas toast showing the active mode.
    void showToast(const QString& text);  ///< Generic transient canvas toast.

    // ── User groups (成组) ──
    /// All members of the block's group, or {blockId} when ungrouped.
    [[nodiscard]] QSet<QUuid> wholeGroupSet(const QUuid& blockId) const;
    /// @p ids plus every member of every group touching it (single pass —
    /// groups never nest).
    [[nodiscard]] QSet<QUuid> expandedWithGroups(const QSet<QUuid>& ids) const;
    /// Display label of a group (user name, falling back to serial).
    [[nodiscard]] QString groupLabel(const QUuid& groupId) const;
    /// Validation for 做成组: >= 2 blocks, same layer, none already grouped.
    /// Fails with a toast explaining the reason; true = may group.
    [[nodiscard]] bool canMakeGroupWithToast();
    /// Non-null when the selection is EXACTLY one whole group.
    [[nodiscard]] QUuid wholeSelectedGroup() const;
    void showContextMenu();            ///< Confirmed-set right-click menu.
    void makeGroupFromSelection();     ///< Push MakeGroupCommand.
    void ungroupSelection();           ///< Push UngroupCommand.
    void renameSelectedGroup();        ///< Inline rename of the whole-selected group.

    // ── Selection management (add/remove, red highlight) ──
    void toggleBlock(const QUuid& blockId);
    void syncSelectionVisual();              ///< Apply m_selection → BlockItem flags.
    void setBlockHighlight(const QUuid& blockId, bool on);

    // ── Marquee selection ──
    void beginMarquee(const cad::geo::Vec2& pos);
    void updateMarquee(const cad::geo::Vec2& pos);
    void endMarquee(const cad::geo::Vec2& pos);
    /// Blocks whose geometry INTERSECTS the rect (partial overlap counts).
    [[nodiscard]] QList<QUuid> blocksIntersectingRect(const QRectF& rectUser) const;

    // ── Confirmation ──
    void confirmSelection();

    // ── Drag (move confirmed selection, anchored at press point) ──
    void beginDrag(const cad::geo::Vec2& pos);
    void updateDrag(const cad::geo::Vec2& pos);
    void endDrag(const cad::geo::Vec2& pos);

    // ── Quick copy (Ctrl+drag 快捷复制) ──
    void beginCopyDrag(const QUuid& hitBlockId, const cad::geo::Vec2& pos);
    void updateCopyDrag(const cad::geo::Vec2& pos);
    void endCopyDrag(const cad::geo::Vec2& pos);
    void cancelCopyDrag();              ///< Esc: discard the preview clones.
    void removeCopyPreview();           ///< Remove live preview clones from doc.
    void finishCopyDrag();              ///< Reset copy state + restore SelectState.

    // ── Connection (attach) + angle HUD ──
    void beginConnect(const QUuid& fromBlockId, const QUuid& fromPointId,
                      const cad::geo::Vec2& pos);
    void updateConnect(const cad::geo::Vec2& pos);
    void endConnect(const cad::geo::Vec2& pos);
    /// Failed connect (released away from any target): commit the plain move
    /// (+ quick-detach) through the undo stack.
    void commitConnectMove();
    /// Esc during Connecting: abort the gesture, restore the pre-drag state.
    void cancelConnect();
    /// Press during ConfirmTarget: click a candidate segment to confirm the
    /// leader; blank / non-candidate click cancels the gesture.
    void tryConfirmTarget(const cad::geo::Vec2& pos);
    /// Mouse move during ConfirmTarget: highlight the hovered candidate line.
    void updateConfirmHighlight(const cad::geo::Vec2& pos);
    void removeConfirmHighlight();
    /// Collect leader segments whose endpoint lies on the connection spot
    /// (overlapping-target disambiguation candidates).
    std::vector<ConfirmCandidate> collectConfirmCandidates(
        const cad::geo::Vec2& connWorldPos) const;
    /// Establish the attachment toward the given leader point/segment and move
    /// into AngleInput. Returns false when rejected (cycle etc.).
    bool attachToTarget(const QUuid& toBlockId, const QUuid& toPointId,
                        const QUuid& toSegmentId);

    void showAngleHud(const cad::geo::Vec2& anchorUser);
    void hideAngleHud();
    void onAngleTextChanged(const QString& text);   ///< Live preview.
    void onAngleModeChanged(cad::param::RotationMode mode);  ///< Mode toggled.
    void commitAngle();                              ///< Enter: finalize.
    void cancelAngle();                              ///< Esc: keep conn, angle 0.
    void finalizeConnection();                       ///< Snapshot→remove→push cmd.

    // ── Hit testing ──
    /// Nearest point endpoint within snap radius (for connect source/target).
    [[nodiscard]] std::optional<SnapResult> hitPoint(const cad::geo::Vec2& worldPos) const;
    /// Block (segment) under worldPos via scene hit-testing.
    [[nodiscard]] QUuid hitBlock(const cad::geo::Vec2& worldPos) const;

    /// Point-level press dispatch (曲线锚点=曲线点拖动, 端点=连接拖拽): runs
    /// against the CURRENT (already selected) set and consumes the press when
    /// a point operation starts. Multi-select members connect individually —
    /// a long-press on ANY selected member's endpoint starts that block's
    /// connection gesture (多选成员逐块连接). Bridges fall through (they
    /// cannot connect — the caller then starts a plain drag). Used by both
    /// the Idle long-press and the Confirmed-state press, so a single gesture
    /// covers 选中+操作.
    [[nodiscard]] bool tryPointOperation(const cad::geo::Vec2& pos);

    // ── Visual helpers ──
    void removeMarqueeRect();
    void updateConnectMarker();   ///< Show/move/hide the snap-target ring.
    void removeConnectMarker();
    void removeConnectHalo();     ///< Drop the source-point halo.

    /// Create/reposition the source-point halo: a dashed ring around the
    /// dragged point whose radius equals the connect snap radius (the
    /// magnet's reach) — a target entering the halo will be snapped.
    void updateConnectHalo();

    // ── Legacy helpers ──
    void deleteSelectedBlocks();

    // ── Core state ──
    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    SelectState m_state = SelectState::Idle;
    SelectionMode m_selectionMode = SelectionMode::Single;

    // Persistent, toggleable selection (block ids) + confirmation flag.
    QSet<QUuid> m_selection;      ///< Currently selected blocks (red).
    bool        m_confirmed = false;  ///< Right-click confirmed (drag-ready).

    // Marquee
    cad::geo::Vec2 m_marqueeStart;
    QGraphicsRectItem* m_marqueeItem = nullptr;
    QSet<QUuid> m_marqueeBase;    ///< Selection snapshot at marquee start (for toggle).

    // Drag state
    cad::geo::Vec2 m_dragStartPos;             ///< Anchor (blank-space press point).
    QList<QUuid>   m_dragBlockIds;
    QHash<QUuid, cad::geo::Vec2> m_dragOrigins;
    QList<QUuid>   m_detachedAttachments;      ///< Cross-boundary attachments to break.

    // Quick-copy state (clones live in the doc as preview until release)
    cad::param::DuplicateResult m_copyResult;  ///< Pristine clones for the command.
    cad::geo::Vec2 m_copyStartPos;             ///< Press point (drag anchor).
    QHash<QUuid, cad::geo::Vec2> m_copyOrigins; ///< Clone origins after initial add.

    // Connection state (block physically follows the cursor while connecting)
    QUuid m_connectFromBlock;
    QUuid m_connectFromPoint;
    cad::geo::Vec2 m_connectGrabOffset;   ///< origin − fromPointWorld at press.
    cad::geo::Vec2 m_connectOrigOrigin;   ///< Pre-drag origin (undo restore).
    double m_connectOrigRotation = 0.0;   ///< Pre-drag rotation (undo restore).
    std::optional<cad::param::Attachment> m_connectOldAtt;  ///< Detached at drag start (快拆).
    SnapEngine m_snapEngine;
    std::optional<SnapResult> m_connectTarget;
    QGraphicsEllipseItem* m_connectMarker = nullptr;  ///< Snap-target ring (owned).
    QGraphicsEllipseItem* m_connectHalo   = nullptr;  ///< Source-point halo (owned).

    // Overlapping-target disambiguation (ConfirmTarget state): candidate
    // leader SEGMENTS whose endpoint lies on the connection spot.
    std::vector<ConfirmCandidate> m_confirmCandidates;
    QGraphicsPathItem* m_confirmHighlight = nullptr;  ///< Hovered candidate line.

    // Angle HUD state
    AngleHud* m_angleHud = nullptr;   ///< Viewport-overlay angle input (owned).
    QUuid m_editingAttachmentId;            ///< Attachment being angle-tuned.
    double m_initialAngle = 0.0;            ///< Orientation-preserving angle at connect time.
    bool  m_angleValid = true;
    cad::param::RotationMode m_angleMode = cad::param::RotationMode::Angle;

    // Mode-switch toast: infrastructure lives on CanvasScene (showToast).

    // ── Curve anchor dragging (overrides normal state when active) ──
    bool    m_anchorDragging = false;
    QUuid   m_anchorBlockId;
    QUuid   m_anchorPointId;
    cad::geo::Vec2 m_anchorOrigPos;  ///< Original freePos before drag.

    /// Check if worldPos is near a pass point of a selected curve block.
    /// Returns the point ID if found, null otherwise.
    /// Nearest curve pass-point within 10px among the SELECTED blocks; returns
    /// {blockId, pointId} so multi-select drags know which block owns the hit.
    [[nodiscard]] std::optional<std::pair<QUuid, QUuid>> hitCurveAnchor(
        const cad::geo::Vec2& worldPos, double zoom) const;
    void beginAnchorDrag(const QUuid& blockId, const QUuid& pointId);
    void updateAnchorDrag(const cad::geo::Vec2& worldPos);
    void endAnchorDrag();
};

} // namespace cad::tools
