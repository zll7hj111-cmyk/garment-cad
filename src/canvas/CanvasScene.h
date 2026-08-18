#pragma once

#include <QGraphicsScene>
#include <QHash>
#include <QSet>
#include <QUuid>

#include "CanvasStyle.h"
#include "CanvasAnimator.h"

class QGraphicsRectItem;
class QTimer;

namespace cad::param { class ParamDocument; }

class BlockItem;
class GroupBadgeItem;

/// The scene that holds all entity items and the origin crosshair.
/// Owns the CanvasStyle (design tokens) and CanvasAnimator (transition engine).
class CanvasScene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit CanvasScene(cad::param::ParamDocument* paramDoc, QObject* parent = nullptr);
    ~CanvasScene() override;

    [[nodiscard]] cad::param::ParamDocument* paramDocument() const { return m_paramDoc; }

    /// Access the style token table.
    [[nodiscard]] const CanvasStyle* style() const { return &m_style; }

    /// Access the animation engine.
    [[nodiscard]] CanvasAnimator* animator() { return &m_animator; }

    /// Replace the active theme (triggers full scene repaint).
    void setStyle(const CanvasStyle& s);

    /// Create and add a BlockItem for the given block ID.
    void addBlockItem(const QUuid& blockId);

    /// Remove the BlockItem for the given block ID.
    void removeBlockItem(const QUuid& blockId);

    /// Remove every BlockItem (used on document reset / load).
    void clearAllBlockItems();

    /// Refresh all BlockItems (call after resolve).
    void refreshAllBlockItems();

    /// Lightweight position sync for all BlockItems: items whose block only
    /// translated are moved via setPos() (O(1) each — no cache rebuild, no
    /// string formatting, no allocation). Items whose block rotated fall back
    /// to a full refresh. Call after resolveAll() or direct transform edits.
    void syncBlockPositions();

    /// Targeted sync for a subset of blocks (drag hot-path: only the dragged
    /// blocks are touched — the rest of the scene is not even iterated).
    void syncBlockPositions(const QList<QUuid>& blockIds);

    /// Select exactly the given block on canvas (clears previous selection).
    /// Drives group highlighting via selectionChanged.
    void selectBlock(const QUuid& blockId);

    /// Find the BlockItem for a block (nullptr if none).
    [[nodiscard]] BlockItem* findBlockItem(const QUuid& blockId) const;

    /// Notify listeners (e.g. group panel) that group display info (names or
    /// follower angles) changed without a structural topology change.
    void notifyGroupInfoChanged();

    /// Transient hold-to-show overrides (N/L keys held on canvas). Purely
    /// visual — the model is untouched, no cache rebuild, no epoch bump.
    /// Painters overlay every name/length label while set; release restores
    /// the model's own show flags (snapshot semantics).
    [[nodiscard]] bool forceShowName() const { return m_forceShowName; }
    [[nodiscard]] bool forceShowLength() const { return m_forceShowLength; }
    void setForceShowName(bool on);
    void setForceShowLength(bool on);

    /// Transient toast pill anchored at the top-center of the first view
    /// (工具守卫提示, auto-hides after ~1.4 s). Shared by every tool.
    void showToast(const QString& text);

    /// Flash the two source points of a measurement: amber rings on both
    /// points plus an amber dashed connector (ToolMeasure preview style).
    /// The transient graphics self-destruct after ~1.5 s. Returns false when
    /// either block/point is missing or unresolved (caller falls back to a
    /// whole-block highlight).
    bool flashMeasure(const QUuid& blockA, const QUuid& pointA,
                      const QUuid& blockB, const QUuid& pointB);

    /// Group-hover coordination (成组悬停): nominate the block under the
    /// cursor as the hover source — every SIBLING member of its user group
    /// lights up (null id clears the broadcast).
    void setGroupHoverSource(const QUuid& blockId);

    /// Reconcile the group visual markers (组包围框): create/drop dashed
    /// bounding boxes for groups that appeared/vanished, refresh visibility
    /// (members all on manually hidden layers → marker hidden) and reposition.
    /// Runs on group registry / layer changes (LOW frequency) — marker
    /// geometry during live drags is handled by updateGroupBadgePositions()
    /// alone.
    void reconcileGroupBadges();
    /// Reposition every marker at its member union bounds. Cheap — the
    /// per-frame path after resolves; never creates/drops boxes or rewrites
    /// labels (membership/visibility changes land via reconcile).
    void updateGroupBadgePositions();
    /// The visual marker item of a group (null when the group has none).
    [[nodiscard]] GroupBadgeItem* groupBadge(const QUuid& groupId) const;
    /// Accent the badges of the given groups (selection state, driven by
    /// ToolSelect's confirmed selection).
    void setGroupSelected(const QSet<QUuid>& groupIds);

    /// Notify listeners (MainWindow) that a segment was just created by the
    /// smart pen. The host shows the status-bar edit strip (SegmentEditBar)
    /// for immediate naming/length/angle edits — no creation dialog.
    void notifyLineCreated(const QUuid& blockId, const QUuid& segmentId);
    /// Notify listeners of live length/angle readouts while a stroke is being
    /// drawn (creation-in-progress, read-only preview in the status bar).
    void notifyLinePreview(double lenCm, double angleDeg);

signals:
    void sceneMouseMoved(qreal x, qreal y);
    void groupInfoChanged();
    /// Badge clicked — the host window selects the whole group on canvas.
    void groupBadgeClicked(const QUuid& groupId);
    /// A segment was just created (smart pen commit). blockId/segmentId
    /// identify the NEW line; the host shows the status-bar edit strip.
    void lineCreated(const QUuid& blockId, const QUuid& segmentId);
    /// Live readout while a stroke is in progress (creation preview).
    void linePreviewChanged(double lenCm, double angleDeg);
    /// Hold-to-show overrides changed (N/L keys). Host forwards to open
    /// LinePropertyDialogs so their display toggles track the held state.
    void forceShowChanged(bool showNames, bool showLengths);

protected:
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;

private:
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QHash<QUuid, BlockItem*> m_blockItems;

    CanvasStyle m_style = CanvasStyle::lightTheme();
    CanvasAnimator m_animator;

    // Toast overlay (owned by the scene, lazily created).
    QGraphicsRectItem* m_toastItem = nullptr;
    QTimer* m_toastTimer = nullptr;

    // Group-hover source (block currently hovered, null = none).
    QUuid m_groupHoverSource;
    /// All members of the block's user group (empty when ungrouped).
    [[nodiscard]] QSet<QUuid> groupMemberSet(const QUuid& blockId) const;

    // Group visual markers (组包围框) — one item per group, reconciled by
    // reconcileGroupBadges.
    QHash<QUuid, GroupBadgeItem*> m_groupBadges;
    QSet<QUuid> m_selectedGroups;   ///< Groups whose markers are accented.
    void onBadgeHover(const QUuid& groupId, bool hovered);
    void updateBadgeAccents();

    // Hold-to-show overrides (N/L keys): transient view state, never written
    // to the model.
    bool m_forceShowName = false;
    bool m_forceShowLength = false;
};
