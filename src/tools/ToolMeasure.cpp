#include "ToolMeasure.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsView>
#include <QPen>
#include <QKeyEvent>
#include <QUndoStack>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "parametric/ParamDocument.h"
#include "parametric/MeasureVariable.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "document/commands/VariableCommands.h"
#include "MeasureResultDialog.h"
#include "ToolSmartPen.h"  // HudItem

namespace cad::tools {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ToolMeasure::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene = &scene;
    m_paramDoc = paramDoc;
    m_state = State::SelectA;
}

void ToolMeasure::deactivate()
{
    clearPreview();
    if (m_previewLine) { m_scene->removeItem(m_previewLine); delete m_previewLine; m_previewLine = nullptr; }
    if (m_markerA)     { m_scene->removeItem(m_markerA);     delete m_markerA;     m_markerA     = nullptr; }
    if (m_hud)         { m_scene->removeItem(m_hud);         delete m_hud;         m_hud         = nullptr; }
    m_scene = nullptr;
    m_paramDoc = nullptr;
}

// ---------------------------------------------------------------------------
// Input events
// ---------------------------------------------------------------------------

void ToolMeasure::mousePress(QGraphicsSceneMouseEvent* event)
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

    auto snap = m_snapEngine.findSnap(clickPos, m_paramDoc, zoom);
    if (!snap) return;

    if (m_state == State::SelectA) {
        // Commit first point.
        m_snapA = snap;
        m_state = State::SelectB;

        // Marker at A.
        if (!m_markerA) {
            constexpr double r = 5.0;
            m_markerA = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
            QPen pen(m_scene->style()->snapPointColor, 2.0);
            pen.setCosmetic(true);
            m_markerA->setPen(pen);
            m_markerA->setBrush(Qt::NoBrush);
            m_markerA->setZValue(102.0);
            m_scene->addItem(m_markerA);
        }
        m_markerA->setPos(cad::geo::Coord::toScene(snap->worldPos));
        m_markerA->setVisible(true);

        if (!m_hud) {
            m_hud = new HudItem();
            m_scene->addItem(m_hud);
        }
    } else {
        // Second point: ensure it is a different point, then commit.
        if (m_snapA && snap->pointId != m_snapA->pointId) {
            m_hoverSnap = snap;
            commitMeasure();
        }
    }
}

void ToolMeasure::mouseMove(QGraphicsSceneMouseEvent* event)
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

void ToolMeasure::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    (void)event;
}

void ToolMeasure::keyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
        resetToSelectA();
}

// ---------------------------------------------------------------------------
// Hover / preview
// ---------------------------------------------------------------------------

void ToolMeasure::updateHover(const cad::geo::Vec2& pos, double zoom)
{
    auto snap = m_snapEngine.findSnap(pos, m_paramDoc, zoom);
    m_hoverSnap = snap;
    if (!m_scene->views().isEmpty()) {
        if (snap)
            m_scene->views().first()->setCursor(Qt::CrossCursor);
        else
            m_scene->views().first()->unsetCursor();
    }
}

void ToolMeasure::updatePreview(const cad::geo::Vec2& cursorPos)
{
    if (!m_snapA) return;

    // Prefer the snapped hover point's position for a stable readout.
    cad::geo::Vec2 endPos = cursorPos;
    if (m_hoverSnap) endPos = m_hoverSnap->worldPos;

    if (!m_previewLine) {
        m_previewLine = new QGraphicsLineItem();
        QPen pen(QColor(0xFF, 0x98, 0x00), 1.4);  // amber
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_previewLine->setPen(pen);
        m_previewLine->setZValue(101.0);
        m_scene->addItem(m_previewLine);
    }
    m_previewLine->setLine(QLineF(cad::geo::Coord::toScene(m_snapA->worldPos),
                                  cad::geo::Coord::toScene(endPos)));
    m_previewLine->setVisible(true);

    if (m_hud) {
        const double distMm = m_snapA->worldPos.distanceTo(endPos);
        m_hud->setText(cad::geo::Units::formatLength(distMm));
        QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
        m_hud->moveToPoint(endPos, view);
        m_hud->setVisible(true);
    }
}

void ToolMeasure::clearPreview()
{
    if (m_previewLine) m_previewLine->setVisible(false);
    if (m_markerA)     m_markerA->setVisible(false);
    if (m_hud)         m_hud->setVisible(false);
}

void ToolMeasure::resetToSelectA()
{
    clearPreview();
    m_snapA.reset();
    m_state = State::SelectA;
}

// ---------------------------------------------------------------------------
// Commit
// ---------------------------------------------------------------------------

void ToolMeasure::commitMeasure()
{
    if (!m_paramDoc || !m_snapA || !m_hoverSnap) return;

    cad::param::MeasureVariable mv;
    mv.blockA = m_snapA->blockId;
    mv.pointA = m_snapA->pointId;
    mv.blockB = m_hoverSnap->blockId;
    mv.pointB = m_hoverSnap->pointId;
    mv.value = m_snapA->worldPos.distanceTo(m_hoverSnap->worldPos);
    // Reference names are uppercase by convention (CopyChip force-uppercases
    // them for display/editing); generate uppercase so the stored refName
    // matches what the user sees and types back into formula fields.
    mv.refName = QStringLiteral("M_") + cad::param::Serial::randomPrefix().toUpper();

    // 1) Commit the measure through the undo stack (undoable). Fall back to
    //    a direct add when no stack is injected (headless unit tests).
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::AddMeasureCommand(m_paramDoc, mv));
    else
        m_paramDoc->addMeasure(mv);

    // 2) Result dialog (pure data in/out — it never touches the document).
    //    Accepted → write name/comment via the existing SetMeasureCommand as
    //    a second, independent undo step; Rejected/Esc keeps the measure as
    //    committed and continues silently.
    QWidget* parent = (m_scene && !m_scene->views().isEmpty())
        ? static_cast<QWidget*>(m_scene->views().first()) : nullptr;
    MeasureResultDialog dlg(mv.value, mv.refName, QString(), QString(), parent);
    if (dlg.exec() == QDialog::Accepted) {
        const QString newName = dlg.enteredName();
        const QString newComment = dlg.enteredComment();
        if (!newName.isEmpty() || !newComment.isEmpty()) {
            cad::param::MeasureVariable updated = mv;
            if (!newName.isEmpty())    updated.name    = newName;
            if (!newComment.isEmpty()) updated.comment = newComment;
            if (m_undoStack)
                m_undoStack->push(new cad::cmd::SetMeasureCommand(m_paramDoc, updated));
            else
                m_paramDoc->updateMeasure(updated);
        }
    }

    // 3) Status feedback through the shared canvas toast channel.
    if (m_scene)
        m_scene->showToast(QStringLiteral("\xe5\xb7\xb2\xe6\xb5\x8b\xe9\x87\x8f ") + mv.refName);  // 已测量 M_xxx

    // Stay active; ready for the next measurement.
    resetToSelectA();
}

} // namespace cad::tools
