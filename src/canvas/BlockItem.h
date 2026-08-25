#pragma once

#include <QGraphicsObject>
#include <QPainterPath>
#include <QUuid>
#include <QSet>

#include "parametric/Segment.h"  // cad::param::SegmentRole (LineCache default)

namespace cad::param { class ParamDocument; class Block; }

class CurveItem;

/// Renders a parametric Block as a single QGraphicsItem.
/// Supports per-entity hover highlighting, selection animation, and
/// drag-to-move (modifies Block Transform). Visual parameters are resolved
/// through CanvasStyle + CanvasAnimator (owned by CanvasScene).
class BlockItem : public QGraphicsObject
{
    Q_OBJECT

public:
    /// Custom item type so qgraphicsitem_cast<BlockItem*> matches ONLY real
    /// BlockItems. Without this, the inherited type() (QGraphicsItem::Type)
    /// makes the cast "succeed" for every item — e.g. the attachment overlay
    /// hovering above a connection point would be mistaken for a BlockItem,
    /// breaking drag and double-click in ToolSelect.
    enum { Type = UserType + 1 };
    [[nodiscard]] int type() const override { return Type; }

    /// How the owning block's canvas layer affects this item's rendering.
    enum class LayerMode {
        Normal,  ///< Active layer: full color, selectable, hoverable.
        Grayed,  ///< Non-active visible layer: gray, not selectable, snappable.
        Hidden,  ///< Hidden layer: not painted, not pickable.
    };

    explicit BlockItem(const QUuid& blockId, cad::param::ParamDocument* doc,
                       QGraphicsItem* parent = nullptr);

    [[nodiscard]] const QUuid& blockId() const { return m_blockId; }

    /// Public wrapper of the private hitTest(): nearest LINE entity id at
    /// @p scenePos within the hover pick radius (screen px ÷ zoom). Null
    /// QUuid when the position misses every segment.
    [[nodiscard]] QUuid hitSegmentAtScene(const QPointF& scenePos) const
    {
        return hitTest(mapFromScene(scenePos), hoverThreshold());
    }

    QRectF boundingRect() const override;
    /// Precise pick region: strokes around segments/points with a screen-space
    /// tolerance. Without this, the default shape() (= bounding rect) lets a
    /// slanted line be selected from anywhere inside its diagonal box.
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    /// Refresh geometry cache from the document (call after resolve).
    void updateFromBlock();

    /// Lightweight sync after a resolve pass: if only the block's position
    /// changed (pure translation — the common drag case), just move the item
    /// without rebuilding the geometry cache. Falls back to a full
    /// updateFromBlock() when rotation or structure changed.
    void syncFromBlock();

    /// Mark a segment as the pen tool's current leader candidate (teal,
    /// color-only — no width change). Pass a null id to clear.
    void setLeaderHighlight(const QUuid& segmentId);

    /// Walk up the parent chain and return the BlockItem owning @p item
    /// (used by scene hit-testing now that curves are child items).
    [[nodiscard]] static BlockItem* containingItem(QGraphicsItem* item);

    /// Curve child hovered (called by CurveItem) — arbitrates curve vs line
    /// priority (a line closer than half the threshold still wins, matching
    /// the legacy hitTest rule) and lifts the block above siblings.
    void onCurveHover(CurveItem* item, const QPointF& localPos);
    /// Curve child left the cursor (called by CurveItem).
    void onCurveHoverLeave(CurveItem* item);

    /// Tool-managed selection flag (drives the red "Selected" state).
    /// Independent of Qt's built-in isSelected() so the selection tool has
    /// full manual control over add/remove selection without fighting the
    /// view's automatic item selection.
    void setToolSelected(bool selected);
    [[nodiscard]] bool toolSelected() const { return m_toolSelected; }

    /// Tool-managed lock flag (confirmed selection → bold "Locked" state).
    /// Takes visual priority over plain selection: red + thicker strokes.
    void setToolLocked(bool locked);
    [[nodiscard]] bool toolLocked() const { return m_toolLocked; }


protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    QUuid m_blockId;
    cad::param::ParamDocument* m_doc = nullptr;

    // Cached geometry in local coordinates for fast painting
    struct LineCache {
        QUuid id;
        QPointF p1; QPointF p2;
        QColor color;      ///< Data-driven color (raw from the segment).
        cad::param::SegmentRole role = cad::param::SegmentRole::Outline;
        double weight;
        Qt::PenStyle penStyle;
        QString name;
        bool showName;
        bool showLength;
        QString lengthText;  ///< Pre-formatted length label (cm).
        bool visible;        ///< False = hidden: kept for hit-testing/hover but
                             ///< not painted unless transiently revealed (hover).
    };
    struct PointCache { QUuid id; QPointF pos; bool isAuxiliary; QString label; bool showLabel;
                        bool isAttachmentNode; bool isCurveAnchor; bool isLockedNode; };

    std::vector<LineCache>  m_lines;
    std::vector<CurveItem*> m_curveItems;  ///< One child item per curve segment.
    std::vector<PointCache> m_points;
    QRectF m_cachedBounds;
    LayerMode m_layerMode = LayerMode::Normal;  ///< From owning block's layer.
    QUuid m_hoveredEntity;  ///< Currently hovered entity (null = none).
    QUuid m_hoveredPointId;  ///< Currently hovered point (null = none).
    QUuid m_leaderEntity;   ///< Segment highlighted as leader candidate (null = none).
    bool  m_toolSelected = false;  ///< Tool-managed selection (red highlight).
    bool  m_toolLocked   = false;  ///< Confirmed selection (red + bold).
    QSet<CurveItem*> m_curvesUnderCursor;  ///< Curve children currently hovered.

    /// True when @p entityId belongs to one of this block's curve children.
    [[nodiscard]] bool isCurveId(const QUuid& entityId) const;

    void rebuildCache();

    /// Cached shape() result — rebuilt only when zoom changes significantly
    /// or the geometry cache is rebuilt. shape() is called at high frequency
    /// by Qt's collision/hover machinery; building the stroker path each time
    /// is prohibitively expensive for multi-segment blocks.
    mutable QPainterPath m_cachedShape;
    mutable double m_cachedShapeTol = -1.0;  ///< Tolerance used for the cache (-1 = invalid).
    double m_lastRotation = 0.0;  ///< Block rotation at last rebuildCache() (rad).
    quint64 m_lastGeometryEpoch = 0;  ///< Block::geometryEpoch at last rebuildCache().
    size_t m_lastPointCount = 0;  ///< Block::points.size() at last rebuildCache().

    /// Hit-test: returns nearest LINE entity ID within threshold, or null
    /// QUuid (curves are hit-tested by their own child items). When
    /// @p bestDistOut is non-null it receives the best line distance — used
    /// by the curve-hover arbitration to compare against the curve's
    /// half-threshold priority.
    [[nodiscard]] QUuid hitTest(const QPointF& localPos, double threshold,
                                double* bestDistOut = nullptr) const;

    /// Hit-test: nearest POINT within @p radius (screen-derived scene units).
    /// Points are not selectable targets, but the cursor hovering near one
    /// highlights it — a visible "grab/connect point" affordance.
    [[nodiscard]] QUuid hitTestPoint(const QPointF& localPos, double radius) const;

    /// Pick tolerance in scene units: screen px ÷ view zoom.
    [[nodiscard]] double hoverThreshold() const;

    /// Resolve the effective state for an entity given current selection/hover.
    [[nodiscard]] int resolveState(const QUuid& entityId) const;

    /// Notify the animator about state changes when hover target changes.
    void updateHoverState(const QUuid& newHover);
};
