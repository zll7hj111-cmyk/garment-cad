#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <vector>

#include "parametric/Segment.h"  // cad::param::SegmentRole

namespace cad::param {
class ParamDocument;
class Block;
}

class BlockItem;
class CurveItem;

/// How the owning block's canvas layer affects this item's rendering.
enum class LayerMode {
    Normal,  ///< Active layer: full color, selectable, hoverable.
    Grayed,  ///< Non-active visible layer: gray, not selectable, snappable.
    Hidden,  ///< Hidden layer: not painted, not pickable.
};

// Cached geometry in local coordinates for fast painting
struct LineCache {
    QUuid id;
    QPointF p1; QPointF p2;
    QColor color;      ///< Data-driven color (raw from the segment).
    cad::param::SegmentRole role = cad::param::SegmentRole::Outline;
    double weight = 1.0;
    Qt::PenStyle penStyle = Qt::SolidLine;
    QString name;
    bool showName = false;
    bool showLength = false;
    QString lengthText;  ///< Pre-formatted length label (cm).
    bool visible = true; ///< False = hidden: kept for hit-testing/hover but
                         ///< not painted unless transiently revealed (hover).
    bool isOrtho = false;  ///< 正交拐角偏置线
    bool showAxis = false; ///< 是否显示中心基准轴虚线
    QPointF pCenter;       ///< 中心主轴拐点本地坐标
};

struct PointCache {
    QUuid id; QPointF pos; bool isAuxiliary = false; bool isPlaced = false; QString label; bool showLabel = false;
    bool isAttachmentNode = false; bool isCurveAnchor = false; bool isLockedNode = false; bool visible = true;
};

class BlockGeometryCache {
public:
    BlockGeometryCache() = default;
    ~BlockGeometryCache();

    BlockGeometryCache(const BlockGeometryCache&) = delete;
    BlockGeometryCache& operator=(const BlockGeometryCache&) = delete;
    BlockGeometryCache(BlockGeometryCache&&) noexcept = default;
    BlockGeometryCache& operator=(BlockGeometryCache&&) noexcept = default;

    /// Rebuilds cached geometry from the document.
    /// Returns false if document or block is not found.
    bool rebuild(const QUuid& blockId, cad::param::ParamDocument* doc,
                 BlockItem* parentBlockItem);

    [[nodiscard]] const std::vector<LineCache>& lines() const { return m_lines; }
    [[nodiscard]] const std::vector<CurveItem*>& curveItems() const { return m_curveItems; }
    [[nodiscard]] const std::vector<PointCache>& points() const { return m_points; }
    [[nodiscard]] const QRectF& cachedBounds() const { return m_cachedBounds; }
    [[nodiscard]] LayerMode layerMode() const { return m_layerMode; }
    [[nodiscard]] bool snapEligible() const { return m_snapEligible; }
    [[nodiscard]] const QPointF& originScene() const { return m_originScene; }

    [[nodiscard]] double lastRotation() const { return m_lastRotation; }
    [[nodiscard]] quint64 lastGeometryEpoch() const { return m_lastGeometryEpoch; }
    [[nodiscard]] size_t lastPointCount() const { return m_lastPointCount; }

    void clearCurveItems();

private:
    std::vector<LineCache>  m_lines;
    std::vector<CurveItem*> m_curveItems;  ///< One child item per curve segment.
    std::vector<PointCache> m_points;
    QRectF m_cachedBounds;
    LayerMode m_layerMode = LayerMode::Normal;
    bool m_snapEligible = true;
    QPointF m_originScene;

    double m_lastRotation = 0.0;
    quint64 m_lastGeometryEpoch = 0;
    size_t m_lastPointCount = 0;
};
