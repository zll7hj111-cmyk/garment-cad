#include "ToolAngleMeasure.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsLineItem>
#include <QGraphicsView>
#include <QPen>
#include <QKeyEvent>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/AngleMeasureVariable.h"
#include "parametric/Serial.h"
#include "geometry/Angle.h"
#include "geometry/Units.h"
#include "ToolSmartPen.h"  // HudItem
#include "document/commands/VariableCommands.h"

namespace cad::tools {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ToolAngleMeasure::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene = &scene;
    m_paramDoc = paramDoc;
    m_state = State::SelectA;
}

void ToolAngleMeasure::deactivate()
{
    clearPreview();
    if (m_highlightA) { m_scene->removeItem(m_highlightA); delete m_highlightA; m_highlightA = nullptr; }
    if (m_highlightB) { m_scene->removeItem(m_highlightB); delete m_highlightB; m_highlightB = nullptr; }
    if (m_hud)        { m_scene->removeItem(m_hud);        delete m_hud;        m_hud        = nullptr; }
    m_scene = nullptr;
    m_paramDoc = nullptr;
}

// ---------------------------------------------------------------------------
// Input events
// ---------------------------------------------------------------------------

void ToolAngleMeasure::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    if (event->button() == Qt::RightButton) {
        resetToSelectA();
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 clickPos(sp.x(), sp.y());
    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    auto snap = m_snapEngine.findSegmentSnap(clickPos, m_paramDoc, zoom);
    if (!snap) return;

    if (m_state == State::SelectA) {
        // Commit the reference line.
        m_snapA = snap;
        m_state = State::SelectB;
        updatePreview(clickPos);
    } else {
        // Second line: must be a different segment, then commit.
        if (m_snapA && snap->segmentId != m_snapA->segmentId) {
            m_hoverSnap = snap;
            commitAngleMeasure();
        }
    }
}

void ToolAngleMeasure::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 cursorPos(sp.x(), sp.y());
    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    updateHover(cursorPos, zoom);
    if (m_state == State::SelectB)
        updatePreview(cursorPos);
}

void ToolAngleMeasure::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    (void)event;
}

void ToolAngleMeasure::keyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
        resetToSelectA();
}

// ---------------------------------------------------------------------------
// Hover / preview
// ---------------------------------------------------------------------------

void ToolAngleMeasure::updateHover(const cad::geo::Vec2& pos, double zoom)
{
    auto snap = m_snapEngine.findSegmentSnap(pos, m_paramDoc, zoom);
    m_hoverSnap = snap;
    if (!m_scene->views().isEmpty()) {
        if (snap)
            m_scene->views().first()->setCursor(Qt::CrossCursor);
        else
            m_scene->views().first()->unsetCursor();
    }
}

void ToolAngleMeasure::updatePreview(const cad::geo::Vec2& cursorPos)
{
    if (!m_snapA) return;

    // Highlight the committed reference line A (amber).
    if (!m_highlightA) {
        m_highlightA = new QGraphicsLineItem();
        QPen pen(QColor(0xFF, 0x98, 0x00), 2.4);  // amber
        pen.setCosmetic(true);
        m_highlightA->setPen(pen);
        m_highlightA->setZValue(101.0);
        m_scene->addItem(m_highlightA);
    }
    cad::geo::Vec2 a0, a1;
    if (segmentWorldEndpoints(m_snapA->blockId, m_snapA->segmentId, a0, a1)) {
        m_highlightA->setLine(QLineF(cad::geo::Coord::toScene(a0),
                                     cad::geo::Coord::toScene(a1)));
        m_highlightA->setVisible(true);
    } else {
        m_highlightA->setVisible(false);
    }

    // Highlight the hovered line B (blue) + live angle readout.
    const bool hasB = m_hoverSnap &&
                      m_hoverSnap->segmentId != m_snapA->segmentId;
    if (!m_highlightB) {
        m_highlightB = new QGraphicsLineItem();
        QPen pen(QColor(0x2F, 0x6F, 0xED), 2.4);  // accent blue
        pen.setCosmetic(true);
        m_highlightB->setPen(pen);
        m_highlightB->setZValue(101.0);
        m_scene->addItem(m_highlightB);
    }
    if (hasB && segmentWorldEndpoints(m_hoverSnap->blockId, m_hoverSnap->segmentId, a0, a1)) {
        m_highlightB->setLine(QLineF(cad::geo::Coord::toScene(a0),
                                     cad::geo::Coord::toScene(a1)));
        m_highlightB->setVisible(true);
    } else {
        m_highlightB->setVisible(false);
    }

    if (!m_hud) {
        m_hud = new HudItem();
        m_scene->addItem(m_hud);
    }
    if (hasB) {
        const double deg = angleBetween(*m_snapA, *m_hoverSnap);
        m_hud->setText(QStringLiteral("%1\u00B0").arg(deg, 0, 'f', 1));
        QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
        m_hud->moveToPoint(cursorPos, view);
        m_hud->setVisible(true);
    } else {
        m_hud->setVisible(false);
    }
}

void ToolAngleMeasure::clearPreview()
{
    if (m_highlightA) m_highlightA->setVisible(false);
    if (m_highlightB) m_highlightB->setVisible(false);
    if (m_hud)        m_hud->setVisible(false);
}

void ToolAngleMeasure::resetToSelectA()
{
    clearPreview();
    m_snapA.reset();
    m_state = State::SelectA;
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

double ToolAngleMeasure::segmentWorldDir(const QUuid& blockId,
                                         const QUuid& segmentId) const
{
    cad::geo::Vec2 w0, w1;
    if (!segmentWorldEndpoints(blockId, segmentId, w0, w1))
        return 0.0;
    return std::atan2(w1.y - w0.y, w1.x - w0.x);
}

bool ToolAngleMeasure::segmentWorldEndpoints(const QUuid& blockId,
                                             const QUuid& segmentId,
                                             cad::geo::Vec2& outA,
                                             cad::geo::Vec2& outB) const
{
    if (!m_paramDoc) return false;
    const auto* blk = m_paramDoc->findBlock(blockId);
    if (!blk) return false;
    const auto* seg = blk->findSegment(segmentId);
    if (!seg) return false;
    const auto* p0 = blk->findPoint(seg->startPointId);
    const auto* p1 = blk->findPoint(seg->endPointId);
    if (!p0 || !p1 || !p0->resolved || !p1->resolved) return false;
    outA = blk->transform.toWorld(p0->resolvedPos);
    outB = blk->transform.toWorld(p1->resolvedPos);
    return true;
}

double ToolAngleMeasure::angleBetween(const SegmentSnapResult& a,
                                      const SegmentSnapResult& b) const
{
    const double dirA = segmentWorldDir(a.blockId, a.segmentId);
    const double dirB = segmentWorldDir(b.blockId, b.segmentId);
    // Directed angle A→B, same semantics as the follower angle (跟随角度).
    return cad::geo::normalizeDeg180(cad::geo::radToDeg(dirB - dirA));
}

// ---------------------------------------------------------------------------
// Commit
// ---------------------------------------------------------------------------

void ToolAngleMeasure::commitAngleMeasure()
{
    if (!m_paramDoc || !m_snapA || !m_hoverSnap) return;
    if (m_snapA->segmentId == m_hoverSnap->segmentId) return;

    cad::param::AngleMeasureVariable am;
    am.blockA = m_snapA->blockId;
    am.segmentA = m_snapA->segmentId;
    am.blockB = m_hoverSnap->blockId;
    am.segmentB = m_hoverSnap->segmentId;
    am.value = angleBetween(*m_snapA, *m_hoverSnap);
    // Reference names are uppercase by convention (CopyChip force-uppercases
    // them for display/editing); generate uppercase so the stored refName
    // matches what the user sees and types back into formula fields.
    am.refName = QStringLiteral("MA_") + cad::param::Serial::randomPrefix().toUpper();

    // Commit through the undo stack (same pattern as ToolMeasure) — a plain
    // addAngleMeasure would leave the angle un-undoable and desync the stack.
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::AddAngleMeasureCommand(m_paramDoc, std::move(am)));
    else
        m_paramDoc->addAngleMeasure(std::move(am));

    // Stay active; ready for the next measurement.
    resetToSelectA();
}

} // namespace cad::tools
