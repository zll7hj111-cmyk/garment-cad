#pragma once

#include <QUuid>
#include <QList>
#include <QHash>
#include <QSet>
#include <QRectF>
#include <optional>
#include <functional>
#include <utility>
#include <vector>

#include "Tool.h"
#include "geometry/Vec2.h"
#include "parametric/Attachment.h"
#include "tools/SelectState.h"
#include "tools/SnapEngine.h"

class QGraphicsRectItem;
class QGraphicsEllipseItem;

namespace cad::tools {

class ConnectGesture;
class CopyDragController;
class MarqueeGesture;

/// Selection tool with ETCAD-style interaction:
///   - Left-drag on empty space → marquee (intersect = select; toggle add/remove)
///   - Click an object → toggle it in/out of the selection (red highlight);
///     a GROUP member toggles only THAT member (点成员=单选线段); the group is
///     NOT auto-expanded on click — whole-group works happen at 拖动/旋转/删除/
///     面板/包围框 (those entries expand via group membership as needed)
///   - SINGLE mode: clicking replaces the selection with the clicked block
///     only (member or not); no marquee
///   - Both modes share ONE 选中→确认→操作 flow: right-click confirms the
///     selection (stays red, becomes drag/connect-ready); right-click AGAIN on
///     the confirmed set opens the context menu (做成组 / 解散组 / 取消选择)
///   - After confirm, press on blank space → drag anchor; move whole selection
///     (a member drag expands to its whole group, so the entire group moves;
///     internal group attachments are NEVER detached by a drag)
///   - Drag release → move committed, selection cleared, back to Idle
///   - Ctrl+press on a segment → quick copy (快捷复制): clones follow the
///     cursor, release drops them (one undo step), Esc aborts; copying a
///     WHOLE group yields a new group (副本成新组) — handled by
///     CopyDragController
///   - After confirm, drag from an endpoint → connection preview → release on
///     target → attach, then angle HUD (live preview, formula-aware) —
///     handled by ConnectGesture
///   - Double-click segment → property dialog; Del → delete (expands to the
///     whole group); Esc → clear
///   - 组模型层零限制: 成组/解散不动几何/连接; 工具层提供整组拖动/旋转/删除、
///    结构编辑守卫 (打断/交点/曲线点/辅助点/终点指向), 删除不设保护且不足 2
///    成员自动解散.
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
    [[nodiscard]] const QSet<QUuid>& selection() const { return m_selection; }

    /// Drop the current selection (used when the active canvas layer switches,
    /// since selection is scoped to the active layer).
    void clearSelectionOnLayerChange();

    /// External selection entry (面板联动): select + CONFIRM the given blocks
    /// (expanded to whole groups) — used by the GroupPanel's card click.
    void selectBlocksExternally(const QList<QUuid>& blockIds);

    /// Inject the edit-target callback (status-bar segment editor). Fired
    /// whenever the single-selection target changes; both ids null = clear.
    void setEditTargetCallback(std::function<void(const QUuid&, const QUuid&)> cb)
    {
        m_editTargetCb = std::move(cb);
    }

    /// True when the given user-coordinate position hits a group badge outline.
    /// Badge clicks belong to the badge (GroupBadgeItem emits clicked()); the
    /// tool must not act on the block underneath.
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
    /// Display label of a group (user name, falling back to serial).
    [[nodiscard]] QString groupLabel(const QUuid& groupId);
    /// Pure 做成组 validation: >= 2 blocks, same layer, none already grouped.
    /// No toast — shared by the menu enable state and canMakeGroupWithToast.
    [[nodiscard]] bool selectionGroupable() const;
    /// Validation for 做成组: same rules as selectionGroupable, but fails
    /// with a toast explaining the reason; true = may group.
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

    // ── Confirmation ──
    void confirmSelection();

    // ── Drag (move confirmed selection, anchored at press point) ──
    void beginDrag(const cad::geo::Vec2& pos);
    void updateDrag(const cad::geo::Vec2& pos);
    void endDrag(const cad::geo::Vec2& pos);

    // ── Hit testing ──
    /// Block (segment) under worldPos via scene hit-testing.
    [[nodiscard]] QUuid hitBlock(const cad::geo::Vec2& worldPos) const;

    /// Segment under worldPos (segment-level hit, active layer only).
    [[nodiscard]] QUuid hitSegmentAt(const cad::geo::Vec2& worldPos) const;

    /// Re-emit the edit target from the current selection (single → the last
    /// clicked segment, else clear).
    void notifyEditTarget();

    /// Point-level press dispatch (曲线锚点=曲线点拖动, 端点=连接拖拽): runs
    /// against the CURRENT (already selected) set and consumes the press when
    /// a point operation starts. Multi-select members connect individually —
    /// a long-press on ANY selected member's endpoint starts that block's
    /// connection gesture (多选成员逐块连接). Bridges fall through (they
    /// cannot connect — the caller then starts a plain drag). Used by both
    /// the Idle long-press and the Confirmed-state press, so a single gesture
    /// covers 选中+操作.
    [[nodiscard]] bool tryPointOperation(const cad::geo::Vec2& pos);

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

    /// Last clicked segment (segment-level edit target for the status bar).
    QUuid m_lastHitSegmentId;
    std::function<void(const QUuid&, const QUuid&)> m_editTargetCb;

    // Drag state
    cad::geo::Vec2 m_dragStartPos;             ///< Anchor (blank-space press point).
    QList<QUuid>   m_dragBlockIds;
    QHash<QUuid, cad::geo::Vec2> m_dragOrigins;
    QList<QUuid>   m_detachedAttachments;      ///< Cross-boundary attachments to break.
    /// 滑轨模式 (抽屉式滑动): attachmentId → pre-drag (slideAlongMm, slidePerpMm)
    /// snapshot for slide attachments whose FOLLOWER is inside the drag set.
    QHash<QUuid, std::pair<double, double>> m_dragOldSlideOffsets;

    // Extracted gestures (connection + quick copy + marquee)
    ConnectGesture*    m_connectGesture = nullptr;
    CopyDragController* m_copyDrag = nullptr;
    MarqueeGesture*    m_marqueeGesture = nullptr;

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
