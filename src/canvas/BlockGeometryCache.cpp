#include "BlockGeometryCache.h"
#include "BlockItem.h"
#include "CurveItem.h"

#include <QPainterPath>
#include <QSet>
#include <cmath>
#include <numbers>

#include "parametric/ParamDocument.h"
#include "parametric/Serial.h"
#include "parametric/Block.h"
#include "parametric/PerfProbe.h"
#include "geometry/Units.h"   // cad::geo::Coord, cad::geo::Units
#include "geometry/CurveMath.h"

BlockGeometryCache::~BlockGeometryCache()
{
    clearCurveItems();
}

void BlockGeometryCache::clearCurveItems()
{
    for (auto* ci : m_curveItems) {
        delete ci;
    }
    m_curveItems.clear();
}

bool BlockGeometryCache::rebuild(const QUuid& blockId, cad::param::ParamDocument* doc,
                                 BlockItem* parentBlockItem)
{
    GCAD_PERF_SCOPE("cache.rebuild");
    m_lines.clear();
    clearCurveItems();
    m_points.clear();
    m_cachedBounds = QRectF();

    if (!doc)
        return false;

    const cad::param::Block* block = doc->findBlock(blockId);
    if (!block)
        return false;

    // Track rotation so syncFromBlock() can detect rigid-body rotation.
    m_lastRotation = block->transform.rotation;
    m_lastGeometryEpoch = block->geometryEpoch();
    m_lastPointCount = block->points.size();

    // Item position = block origin in scene coords.
    // All cached geometry is LOCAL (relative to block origin).
    cad::geo::Vec2 origin = block->transform.origin;
    m_originScene = cad::geo::Coord::toScene(origin);

    // Build line cache from segments.
    // Hidden segments (seg.visible == false) are deliberately KEPT in the cache
    // so they still contribute to shape()/hitTest() — the user must be able to
    // hover (transient reveal) and double-click them to re-open properties and
    // turn visibility back on. They are simply not painted unless hovered.
    for (const auto& seg : block->segments) {
        // --- Curve segment ---
        if (seg.isCurve()) {
            // Frame-level Bézier cache: spans, flattened polyline, label
            // midpoint/tangent and exact arc length are built ONCE per resolve
            // pass (Block::rebuildCurveCache) and shared with the snap engine /
            // tangent handles. This rebuild only applies rotation + Y-flip — no
            // re-solve, no re-flatten, no re-integration.
            const cad::param::CurveSpanEntry* entry = block->curveSpanEntry(seg.id);
            if (!entry || entry->spans.empty()) continue;

            // Convert to local scene coords: apply block transform (rotation),
            // subtract world origin, then Y-flip — same as point cache below.
            // cos/sin hoisted: the flatten polyline has dozens of points and
            // each would otherwise recompute the rotation trig.
            const double rot = block->transform.rotation;
            const double cosR = std::cos(rot), sinR = std::sin(rot);
            auto toLocal = [&](const cad::geo::Vec2& localPos) -> QPointF {
                const double rx = localPos.x * cosR - localPos.y * sinR;
                const double ry = localPos.x * sinR + localPos.y * cosR;
                return cad::geo::Coord::toScene(rx, ry);
            };

            // Render the curve as a dense POLYLINE (Seamly2D technique): the
            // Bézier spans were flattened ONCE per resolve into discrete
            // points (0.1 mm tolerance — below visual resolution at any zoom).
            // Painting line segments is far cheaper than a cubic QPainterPath
            // — the rasterizer / GL backend draws lines directly, while cubic
            // segments are recursively subdivided and triangulated on EVERY
            // repaint.
            const auto& flat = entry->flatLocal;
            if (flat.empty()) continue;
            QPainterPath curvePath;
            curvePath.moveTo(toLocal(flat.front()));
            for (size_t fi = 1; fi < flat.size(); ++fi)
                curvePath.lineTo(toLocal(flat[fi]));

            // Label position: parametric midpoint (t = 0.5), cached at resolve
            // time. For smooth garment curves this is visually close to the
            // arc-length midpoint but avoids the expensive arc-length bisection
            // on every rebuild (the length text below uses the exact cached
            // length).
            const QPointF labelPos = toLocal(entry->labelLocal);
            const cad::geo::Vec2& midTan = entry->labelLocalDir;
            // Rotate tangent by block rotation for correct label orientation.
            const cad::geo::Vec2 worldTan(midTan.x * cosR - midTan.y * sinR,
                                           midTan.x * sinR + midTan.y * cosR);
            const double labelAngle = std::atan2(-worldTan.y, worldTan.x);  // scene Y-flip

            // Pre-format arc-length label (exact arc length, cached at resolve).
            // ALWAYS formatted — the hold-to-show force (L key) reveals the
            // length even when seg.showLength is off.
            const QString lenText = cad::geo::Units::formatLength(entry->arcLengthMm);

            Qt::PenStyle ps = Qt::SolidLine;
            if (seg.lineStyle == cad::param::LineStyle::Dashed) ps = Qt::DashLine;
            else if (seg.lineStyle == cad::param::LineStyle::Dotted) ps = Qt::DotLine;

            // Hit-test shape: the SAME dense flattened path that is painted
            // (CurveItem::shape strokes it). The old "coarse control-polygon"
            // hit region deviates from the drawn curve by many millimetres on
            // strong curves — clicks ON the curve body missed the pick band
            // (选择工具对曲线判定失灵, 用户报告 2026-10). The item-level stroke
            // cache (CurveItem::shape) keeps the per-frame cost the same.
            auto* curveItem = new CurveItem(parentBlockItem, CurveItem::Data{
                seg.id, curvePath, labelPos, labelAngle,
                seg.color, seg.role, seg.weight, ps, seg.name,
                seg.showName, seg.showLength, lenText, seg.visible});
            m_curveItems.push_back(curveItem);

            m_cachedBounds |= curveItem->boundingRect();
            continue;
        }

        // --- Straight-line segment (existing logic) ---
        cad::geo::Vec2 w1 = block->worldPos(seg.startPointId);
        cad::geo::Vec2 w2 = block->worldPos(seg.endPointId);

        // Convert to local scene coords: subtract origin, then Y-flip
        QPointF p1 = cad::geo::Coord::toScene(w1.x - origin.x, w1.y - origin.y);
        QPointF p2 = cad::geo::Coord::toScene(w2.x - origin.x, w2.y - origin.y);

        Qt::PenStyle ps = Qt::SolidLine;
        if (seg.lineStyle == cad::param::LineStyle::Dashed) ps = Qt::DashLine;
        else if (seg.lineStyle == cad::param::LineStyle::Dotted) ps = Qt::DotLine;

        // Pre-format the length label (internal mm → display cm). ALWAYS
        // formatted — the hold-to-show force (L key) reveals the length even
        // when seg.showLength is off.
        QString lenText;
        {
            // 端点延长线：长度标注按"实际画出的长度"（本体+尾巴, D6）。
            const double lenMm = w1.distanceTo(w2);
            lenText = cad::geo::Units::formatLength(lenMm);
        }

        bool isOrtho = false;
        bool showAxis = seg.showOrthoAxis;
        QPointF pCenter;
        if (const auto* ep = block->findPoint(seg.endPointId)) {
            if (ep->constraint == cad::param::PointConstraint::OrthoOffset &&
                std::abs(ep->orthoOffsetDist) > 1e-6) {
                isOrtho = true;
                if (const auto* ref = block->findPoint(ep->refPointId)) {
                    if (ref->resolved) {
                        double ang = ep->angle;
                        double baseAngle = 0.0;
                        if (!ep->refSegmentId.isNull()) {
                            if (const auto* rseg = block->findSegment(ep->refSegmentId)) {
                                const auto* rsp = block->findPoint(rseg->startPointId);
                                const auto* rep = block->findPoint(rseg->endPointId);
                                if (rsp && rep && rsp->resolved && rep->resolved) {
                                    cad::geo::Vec2 dir = rep->resolvedPos - rsp->resolvedPos;
                                    baseAngle = std::atan2(dir.y, dir.x);
                                }
                            }
                        }
                        const double axisRad = baseAngle + ang * std::numbers::pi / 180.0;
                        const cad::geo::Vec2 axisDir{std::cos(axisRad), std::sin(axisRad)};
                        const cad::geo::Vec2 centerPos = ref->resolvedPos + axisDir * ep->distance;
                        const cad::geo::Vec2 wCenter = block->transform.toWorld(centerPos);
                        pCenter = cad::geo::Coord::toScene(wCenter.x - origin.x, wCenter.y - origin.y);
                    }
                }
            }
        }

        m_lines.push_back({seg.id, p1, p2, seg.color, seg.role, seg.weight, ps,
                           seg.name, seg.showName, seg.showLength, lenText,
                           seg.visible, isOrtho, showAxis, pCenter});

        QRectF lineBounds = QRectF(p1, p2).normalized();
        if (isOrtho && showAxis && !pCenter.isNull()) {
            lineBounds |= QRectF(p1, pCenter).normalized();
        }
        QPointF mid((p1.x() + p2.x()) / 2.0, (p1.y() + p2.y()) / 2.0);
        // 方向指示箭头预算入 bounds (中点半偏移臂长+离线距), 防出界裁剪。
        lineBounds |= QRectF(mid.x() - 10, mid.y() - 10, 20, 20);
        if (seg.showName && !seg.name.isEmpty()) {
            lineBounds |= QRectF(mid + QPointF(4, -14), mid + QPointF(4 + seg.name.length() * 7, 4));
        }
        if (seg.showLength && !lenText.isEmpty()) {
            lineBounds |= QRectF(mid + QPointF(4, 4), mid + QPointF(4 + lenText.length() * 7, 20));
        }
        m_cachedBounds |= lineBounds;
    }

    // Collect the points of this block that participate in a connection
    // (either side — leader or follower) so they get the anchor-ring marker.
    // PROTECTED connections get the amber ring (拖动保护视觉区分).
    QSet<QUuid> attachmentPoints;
    QSet<QUuid> lockedPoints;
    for (const auto& att : doc->attachments()) {
        if (att.fromBlockId == blockId) {
            attachmentPoints.insert(att.fromPointId);
            if (att.isLocked) lockedPoints.insert(att.fromPointId);
        }
        if (att.toBlockId == blockId) {
            attachmentPoints.insert(att.toPointId);
            if (att.isLocked) lockedPoints.insert(att.toPointId);
        }
    }

    // Build point cache
    for (const auto& pt : block->points) {
        if (!pt.resolved) continue;

        cad::geo::Vec2 w = block->transform.toWorld(block->effectiveLocalPos(pt.id));
        QPointF pos = cad::geo::Coord::toScene(w.x - origin.x, w.y - origin.y);  // local scene coords
        // Fall back to serial for unnamed aux/intersection points, otherwise the
        // "show name" checkbox has no visible effect.
        const QString pointLabel = pt.name.isEmpty()
            ? cad::param::Serial::tag(pt.serial) : pt.name;
        m_points.push_back({pt.id, pos, pt.isAuxiliary, pt.isPlaced, pointLabel, pt.showName,
                            attachmentPoints.contains(pt.id),
                            pt.constraint == cad::param::PointConstraint::CurveAnchor,
                            lockedPoints.contains(pt.id),
                            pt.visible});

        // Include label area in bounds to prevent ghosting during drag
        QRectF ptBounds(pos - QPointF(6, 6), pos + QPointF(6, 6));
        if (pt.showName && !pointLabel.isEmpty()) {
            ptBounds |= QRectF(pos + QPointF(5, -16), pos + QPointF(5 + pointLabel.length() * 8, 4));
        }
        m_cachedBounds |= ptBounds;
    }

    // Layer display mode: a manually hidden layer is not painted nor pickable;
    // any non-active layer renders GRAYED — including the auxiliary layer,
    // whose construction geometry stays visible as a reference draft (only
    // the active layer is full color). Hover feedback follows SNAP eligibility
    // (layerSnappable): grayed WORKING layers stay hoverable so connections
    // can be aimed from the auxiliary layer; a grayed auxiliary layer is
    // reference-only (never a hover/snap target).
    if (!doc->layersView().layerVisible(block->layer)) {
        m_layerMode = LayerMode::Hidden;
    } else if (block->layer != doc->layersView().activeLayer()) {
        m_layerMode = LayerMode::Grayed;
    } else {
        m_layerMode = LayerMode::Normal;
    }
    m_snapEligible = doc->layersView().layerSnappable(block->layer);

    // Curve children mirror the layer display mode (grayed reference layers
    // render at reduced opacity; hidden layers suppress everything via the
    // parent's visibility) and the hover eligibility.
    for (auto* ci : m_curveItems) {
        ci->setGrayed(m_layerMode == LayerMode::Grayed);
        ci->setAcceptHoverEvents(m_snapEligible);
    }

    return true;
}
