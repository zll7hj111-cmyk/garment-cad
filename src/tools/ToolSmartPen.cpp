#include "ToolSmartPen.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPen>
#include <QBrush>
#include <QFontMetricsF>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "geometry/Units.h"
#include "LinePropertyDialog.h"

namespace cad::tools {

namespace {
/// Normalize an angle in degrees to the range (-180, 180].
double normalizeDeg180(double deg)
{
    double r = std::fmod(deg + 180.0, 360.0);
    if (r < 0.0) r += 360.0;
    return r - 180.0;
}
} // namespace

// ---------------------------------------------------------------------------
// HudItem
// ---------------------------------------------------------------------------

HudItem::HudItem(QGraphicsItem* parent)
    : QGraphicsItem(parent)
{
    setZValue(200.0);
}

void HudItem::setText(const QString& text)
{
    prepareGeometryChange();
    m_text = text;
    QFont f(QStringLiteral("Segoe UI"), 9);
    QFontMetricsF fm(f);
    m_rect = fm.boundingRect(m_text).adjusted(-4.0, -2.0, 4.0, 2.0);
}

void HudItem::moveToPoint(const cad::geo::Vec2& userPos, const QGraphicsView* view)
{
    setPos(cad::geo::Coord::toScene(userPos));  // user → scene
    double zoom = (view != nullptr) ? view->transform().m11() : 1.0;
    if (std::abs(zoom) < 1e-9) zoom = 1.0;
    setTransform(QTransform().scale(1.0 / zoom, 1.0 / zoom));
}

QRectF HudItem::boundingRect() const
{
    return m_rect.adjusted(-1.0, -1.0, 1.0, 1.0);
}

void HudItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if (m_text.isEmpty()) return;

    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    painter->setPen(QPen(QColor(180, 180, 180), 0.5));
    painter->setBrush(QColor(255, 255, 255, 215));
    painter->drawRect(m_rect);

    painter->setPen(QColor(30, 30, 30));
    painter->setFont(QFont(QStringLiteral("Segoe UI"), 9));
    painter->drawText(m_rect, Qt::AlignCenter, m_text);
}

// ---------------------------------------------------------------------------
// ToolSmartPen
// ---------------------------------------------------------------------------

void ToolSmartPen::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene    = &scene;
    m_paramDoc = paramDoc;
    m_state    = State::Idle;
    m_angleSnap = false;
    m_startSnap.reset();
    m_currentSnap.reset();
}

void ToolSmartPen::deactivate()
{
    cancelLine();
    m_scene    = nullptr;
    m_paramDoc = nullptr;
}

void ToolSmartPen::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene) return;

    if (event->button() == Qt::RightButton) {
        if (m_state == State::Drawing) cancelLine();
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    // event->scenePos() is already in user coords (Y-up) thanks to CanvasView conversion
    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 clickPos(sp.x(), sp.y());

    if (m_state == State::Idle) {
        // --- Set start point ---
        // Try snapping to existing point
        double zoom = 1.0;
        if (!m_scene->views().isEmpty())
            zoom = m_scene->views().first()->transform().m11();

        auto snap = m_snapEngine.findSnap(clickPos, m_paramDoc, zoom);
        if (snap) {
            m_startPoint = snap->worldPos;
            m_startSnap  = snap;
            // Cache the leader segment's world direction so the HUD and Shift
            // snap can work in construction-angle (included angle) space.
            m_refDirDeg = 0.0;
            if (const auto* lb = m_paramDoc->findBlock(snap->blockId))
                m_refDirDeg = (lb->transform.rotation
                               + lb->directionAtPoint(snap->pointId)) * 180.0 / M_PI;
        } else {
            m_startPoint = clickPos;
            m_startSnap.reset();
            m_refDirDeg = 0.0;
        }

        m_state = State::Drawing;

        // Create preview items
        m_previewLine = new QGraphicsLineItem();
        QPen pen(QColor(0, 120, 215), 1.5);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_previewLine->setPen(pen);
        m_previewLine->setZValue(100.0);
        m_scene->addItem(m_previewLine);

        constexpr double r = 3.0;
        m_startMarker = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
        m_startMarker->setPos(cad::geo::Coord::toScene(m_startPoint));
        m_startMarker->setPen(Qt::NoPen);
        m_startMarker->setBrush(m_startSnap ? QColor(0, 180, 80) : QColor(0, 120, 215));
        m_startMarker->setZValue(101.0);
        m_scene->addItem(m_startMarker);

        // Snap indicator (small square, hidden by default)
        constexpr double sr = 5.0;
        m_snapIndicator = new QGraphicsRectItem(-sr, -sr, sr * 2.0, sr * 2.0);
        m_snapIndicator->setPen(QPen(QColor(220, 50, 50), 1.5, Qt::SolidLine));
        m_snapIndicator->setBrush(Qt::NoBrush);
        m_snapIndicator->setZValue(102.0);
        m_snapIndicator->setVisible(false);
        m_scene->addItem(m_snapIndicator);

        m_hud = new HudItem();
        m_scene->addItem(m_hud);

        m_angleSnap = event->modifiers() & Qt::ShiftModifier;
    }
    else if (m_state == State::Drawing) {
        // --- Set end point and commit ---
        cad::geo::Vec2 end = applyAngleSnap(clickPos);

        // Check for end-point snap
        double zoom = 1.0;
        if (!m_scene->views().isEmpty())
            zoom = m_scene->views().first()->transform().m11();
        auto endSnap = m_snapEngine.findSnap(clickPos, m_paramDoc, zoom);
        if (endSnap) {
            end = endSnap->worldPos;
        }

        commitLine(end);
    }
}

void ToolSmartPen::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || m_state != State::Drawing) return;

    m_angleSnap = event->modifiers() & Qt::ShiftModifier;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 cursorPos(sp.x(), sp.y());
    const cad::geo::Vec2 effectiveEnd = applyAngleSnap(cursorPos);

    updatePreview(effectiveEnd);
    updateSnapIndicator(cursorPos);
}

void ToolSmartPen::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    (void)event;
}

void ToolSmartPen::keyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Shift) {
        m_angleSnap = true;
    }
    else if (event->key() == Qt::Key_Escape) {
        if (m_state == State::Drawing) cancelLine();
    }
}

void ToolSmartPen::keyRelease(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Shift) {
        m_angleSnap = false;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ToolSmartPen::commitLine(const cad::geo::Vec2& end)
{
    if (m_startPoint.distanceSquaredTo(end) < 1e-10) {
        cancelLine();
        return;
    }

    QUuid blockId;
    QUuid segmentId;

    if (m_startSnap) {
        createAttachedLine(*m_startSnap, end);
        // Retrieve the last added block/segment IDs
        // (createAttachedLine stores them internally via paramDoc)
    } else {
        createFreeLine(m_startPoint, end);
    }

    clearPreview();
    m_state = State::Idle;
    m_startSnap.reset();
    m_currentSnap.reset();

    // addBlock/addAttachment already triggered resolveAll + refreshAllBlockItems
    // via ParamDocument signals — no manual re-resolve needed.

    // Show property dialog for the last created block/segment
    if (m_paramDoc && !m_paramDoc->blocks().empty()) {
        auto& lastBlock = m_paramDoc->blocks().back();
        blockId = lastBlock.id;
        if (!lastBlock.segments.empty()) {
            segmentId = lastBlock.segments.back().id;

            // Find the parent widget for the dialog
            QWidget* parentWidget = m_scene->views().isEmpty()
                ? nullptr : m_scene->views().first();

            LinePropertyDialog dlg(blockId, segmentId, m_paramDoc, m_scene, parentWidget);
            dlg.exec();

            // Refresh after dialog (user may have changed formulas)
            if (m_scene) {
                m_scene->refreshAllBlockItems();
            }
        }
    }
}

void ToolSmartPen::createFreeLine(const cad::geo::Vec2& start, const cad::geo::Vec2& end)
{
    if (!m_paramDoc) return;

    cad::param::Block block;
    // Segment names default to empty; the user assigns them via the property
    // dialog or the group panel.

    // Block origin at start point (local (0,0) = start)
    block.transform.origin = start;
    block.transform.rotation = 0.0;

    // Start point: Free at local (0,0)
    cad::param::ParamPoint ptStart;
    ptStart.constraint = cad::param::PointConstraint::Free;
    ptStart.freePos = cad::geo::Vec2::zero();
    QUuid startId = ptStart.id;

    // End point: Polar relative to start
    cad::geo::Vec2 delta = end - start;
    double dist = delta.length();
    double angleDeg = std::atan2(delta.y, delta.x) * 180.0 / M_PI;

    cad::param::ParamPoint ptEnd;
    ptEnd.constraint = cad::param::PointConstraint::Polar;
    ptEnd.refPointId = startId;
    ptEnd.distance = dist;
    ptEnd.angle = angleDeg;
    QUuid endId = ptEnd.id;

    block.addPoint(std::move(ptStart));
    block.addPoint(std::move(ptEnd));

    // Segment connecting them
    cad::param::Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    block.addSegment(std::move(seg));

    m_paramDoc->addBlock(std::move(block));
}

void ToolSmartPen::createAttachedLine(const SnapResult& snapStart, const cad::geo::Vec2& end)
{
    if (!m_paramDoc) return;

    cad::param::Block block;
    // Segment names default to empty (see createFreeLine).

    // Block origin at snap point
    block.transform.origin = snapStart.worldPos;
    block.transform.rotation = 0.0;

    // Start point: Free at local (0,0) — coincides with snapped point
    cad::param::ParamPoint ptStart;
    ptStart.constraint = cad::param::PointConstraint::Free;
    ptStart.freePos = cad::geo::Vec2::zero();
    QUuid startId = ptStart.id;

    // End point: Polar relative to start
    cad::geo::Vec2 delta = end - snapStart.worldPos;
    double dist = delta.length();
    double angleDeg = std::atan2(delta.y, delta.x) * 180.0 / M_PI;

    cad::param::ParamPoint ptEnd;
    ptEnd.constraint = cad::param::PointConstraint::Polar;
    ptEnd.refPointId = startId;
    ptEnd.distance = dist;
    ptEnd.angle = angleDeg;
    QUuid endId = ptEnd.id;

    block.addPoint(std::move(ptStart));
    block.addPoint(std::move(ptEnd));

    cad::param::Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    block.addSegment(std::move(seg));

    QUuid newBlockId = block.id;
    m_paramDoc->addBlock(std::move(block));

    // Create attachment: new block's start point snaps to the target block's point
    cad::param::Attachment att;
    att.fromBlockId = newBlockId;
    att.fromPointId = startId;
    att.toBlockId   = snapStart.blockId;
    att.toPointId   = snapStart.pointId;

    // Compute construction angle: angle of the new line relative to the leader
    // segment's world direction (start->end). 0 degrees = continue straight.
    // Reference world direction = target block rotation + local segment direction.
    // directionAtPoint handles snapping to either endpoint of the leader segment.
    double refWorldRad = 0.0;
    if (const auto* targetBlock = m_paramDoc->findBlock(snapStart.blockId))
        refWorldRad = targetBlock->transform.rotation
                    + targetBlock->directionAtPoint(snapStart.pointId);

    // Construction angle = new line's world angle - leader segment world direction
    att.angleOffset = angleDeg - refWorldRad * 180.0 / M_PI;

    m_paramDoc->addAttachment(std::move(att));
}

void ToolSmartPen::cancelLine()
{
    clearPreview();
    m_state = State::Idle;
    m_startSnap.reset();
    m_currentSnap.reset();
}

void ToolSmartPen::clearPreview()
{
    if (!m_scene) return;

    if (m_previewLine)   { m_scene->removeItem(m_previewLine);   delete m_previewLine;   m_previewLine   = nullptr; }
    if (m_startMarker)   { m_scene->removeItem(m_startMarker);   delete m_startMarker;   m_startMarker   = nullptr; }
    if (m_snapIndicator) { m_scene->removeItem(m_snapIndicator); delete m_snapIndicator; m_snapIndicator = nullptr; }
    if (m_hud)           { m_scene->removeItem(m_hud);           delete m_hud;           m_hud           = nullptr; }
}

cad::geo::Vec2 ToolSmartPen::applyAngleSnap(const cad::geo::Vec2& raw) const
{
    const cad::geo::Vec2 delta = raw - m_startPoint;
    const double dist = delta.length();
    if (dist < 1e-12) return raw;

    // Work in construction-angle space: angle relative to the leader segment.
    // m_refDirDeg == 0 for a free line, so this degenerates to the world angle.
    const double rawWorldDeg = std::atan2(delta.y, delta.x) * 180.0 / M_PI;
    const double relDeg      = rawWorldDeg - m_refDirDeg;

    if (!m_angleSnap) {
        m_snapAngleDeg = normalizeDeg180(relDeg);
        return raw;
    }

    const double snappedRel = std::round(relDeg / 45.0) * 45.0;
    m_snapAngleDeg          = normalizeDeg180(snappedRel);
    const double rad        = (snappedRel + m_refDirDeg) * M_PI / 180.0;
    return m_startPoint + cad::geo::Vec2(std::cos(rad) * dist, std::sin(rad) * dist);
}

void ToolSmartPen::updatePreview(const cad::geo::Vec2& effectiveEnd)
{
    if (m_previewLine) {
        QPointF p1 = cad::geo::Coord::toScene(m_startPoint);
        QPointF p2 = cad::geo::Coord::toScene(effectiveEnd);
        m_previewLine->setLine(p1.x(), p1.y(), p2.x(), p2.y());
    }

    if (!m_hud || !m_scene) return;

    const double lenMm = m_startPoint.distanceTo(effectiveEnd);
    QString text = cad::geo::Units::formatLength(lenMm, 1);
    // Show the construction angle: always when attached to a leader segment,
    // otherwise only while Shift angle-snap is active.
    if (m_startSnap || m_angleSnap) {
        text += QStringLiteral("  %1°").arg(m_snapAngleDeg, 0, 'f', 0);
    }
    if (m_currentSnap) {
        text += QStringLiteral("  → %1").arg(
            m_currentSnap->pointName.isEmpty()
                ? QStringLiteral("点")
                : m_currentSnap->pointName);
    }
    m_hud->setText(text);

    QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    m_hud->moveToPoint(effectiveEnd, view);
}

void ToolSmartPen::updateSnapIndicator(const cad::geo::Vec2& worldPos)
{
    if (!m_snapIndicator) return;

    double zoom = 1.0;
    if (m_scene && !m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    auto snap = m_snapEngine.findSnap(worldPos, m_paramDoc, zoom);
    m_currentSnap = snap;

    if (snap) {
        m_snapIndicator->setPos(cad::geo::Coord::toScene(snap->worldPos));
        m_snapIndicator->setVisible(true);
    } else {
        m_snapIndicator->setVisible(false);
    }
}

} // namespace cad::tools
