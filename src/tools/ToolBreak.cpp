#include "ToolBreak.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QPainterPath>
#include <QPen>
#include <QUndoStack>
#include <QCursor>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "geometry/Units.h"
#include "document/commands/BreakCommands.h"
#include "document/commands/BlockCommands.h"
#include "GroupGuard.h"
#include "QuickAuxDialog.h"

namespace cad::tools {

void ToolBreak::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene = &scene;
    m_paramDoc = paramDoc;
}

void ToolBreak::deactivate()
{
    if (m_auxDialog)
        m_auxDialog->close();
    hideMarkers();
    if (m_breakCircle) { m_scene->removeItem(m_breakCircle); delete m_breakCircle; m_breakCircle = nullptr; }
    if (m_segMarker)   { m_scene->removeItem(m_segMarker);   delete m_segMarker;   m_segMarker   = nullptr; }
    m_scene = nullptr;
    m_paramDoc = nullptr;
}

void ToolBreak::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (m_auxDialog) return;  // dialog open, ignore canvas clicks

    if (event->button() != Qt::LeftButton) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 clickPos(sp.x(), sp.y());

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    // Priority 1: click on a breakable point → break immediately.
    auto snap = m_snapEngine.findSnap(clickPos, m_paramDoc, zoom);
    if (snap && isBreakable(snap->blockId, snap->pointId)) {
        if (guardGroupedBlock(m_scene, m_paramDoc, snap->blockId,
                              QString::fromUtf8("\xe6\x89\x93\xe6\x96\xad"))) return;   // 打断
        const auto* block = m_paramDoc->findBlock(snap->blockId);
        const auto* pt = block ? block->findPoint(snap->pointId) : nullptr;
        if (pt && (pt->constraint == cad::param::PointConstraint::Interpolated
                || pt->constraint == cad::param::PointConstraint::Intersection
                || pt->constraint == cad::param::PointConstraint::CurveAnchor)) {
            executeBreak(snap->blockId, pt->hostSegmentId, snap->pointId);
            return;
        }
    }

    // Priority 2: click on segment body → create aux point then auto-break.
    auto segSnap = m_snapEngine.findSegmentSnap(
        clickPos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        if (guardGroupedBlock(m_scene, m_paramDoc, segSnap->blockId,
                              QString::fromUtf8("\xe6\x89\x93\xe6\x96\xad"))) return;   // 打断
        openAuxDialogForBreak(*segSnap);
        return;
    }

    // Priority 3: clicked a non-breakable aux point or empty space → ignore.
}

void ToolBreak::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (m_auxDialog) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 cursorPos(sp.x(), sp.y());
    updateHover(cursorPos);
}

void ToolBreak::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    (void)event;
}

// ---------------------------------------------------------------------------
// Hover feedback
// ---------------------------------------------------------------------------

void ToolBreak::updateHover(const cad::geo::Vec2& worldPos)
{
    m_hoverPoint.reset();
    m_hoverSeg.reset();
    m_hoverBreakable = false;

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    // Try point snap first.
    auto snap = m_snapEngine.findSnap(worldPos, m_paramDoc, zoom);
    if (snap) {
        m_hoverPoint = snap;
        m_hoverBreakable = isBreakable(snap->blockId, snap->pointId);

        if (m_hoverBreakable) {
            // Show green circle at the breakable point.
            if (!m_breakCircle) {
                constexpr double r = 7.0;
                m_breakCircle = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
                QPen pen(m_scene->style()->auxMarkerColor, 2.0);
                pen.setCosmetic(true);
                m_breakCircle->setPen(pen);
                m_breakCircle->setBrush(Qt::NoBrush);
                m_breakCircle->setZValue(102.0);
                m_scene->addItem(m_breakCircle);
            }
            m_breakCircle->setPos(cad::geo::Coord::toScene(snap->worldPos));
            m_breakCircle->setVisible(true);
            if (m_segMarker) m_segMarker->setVisible(false);

            // Scissors cursor would require a custom cursor; use cross for now.
            if (!m_scene->views().isEmpty())
                m_scene->views().first()->setCursor(Qt::CrossCursor);
            return;
        }
    }

    // No breakable point: hide circle.
    if (m_breakCircle) m_breakCircle->setVisible(false);

    // Try segment body snap for X marker.
    auto segSnap = m_snapEngine.findSegmentSnap(
        worldPos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        m_hoverSeg = segSnap;

        if (!m_segMarker) {
            constexpr double s = 4.0;
            QPainterPath cross;
            cross.moveTo(-s, -s); cross.lineTo(s, s);
            cross.moveTo(-s, s);  cross.lineTo(s, -s);
            m_segMarker = new QGraphicsPathItem(cross);
            QPen pen(m_scene->style()->auxMarkerColor, 1.6);
            pen.setCosmetic(true);
            m_segMarker->setPen(pen);
            m_segMarker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            m_segMarker->setZValue(102.0);
            m_scene->addItem(m_segMarker);
        }
        m_segMarker->setPos(cad::geo::Coord::toScene(segSnap->worldPos));
        m_segMarker->setVisible(true);

        if (!m_scene->views().isEmpty())
            m_scene->views().first()->setCursor(Qt::CrossCursor);
        return;
    }

    // Nothing hovered.
    hideMarkers();
    if (!m_scene->views().isEmpty())
        m_scene->views().first()->unsetCursor();
}

void ToolBreak::hideMarkers()
{
    if (m_breakCircle) m_breakCircle->setVisible(false);
    if (m_segMarker) m_segMarker->setVisible(false);
}

// ---------------------------------------------------------------------------
// Break execution
// ---------------------------------------------------------------------------

void ToolBreak::executeBreak(const QUuid& blockId, const QUuid& segmentId,
                             const QUuid& auxPointId)
{
    if (!m_paramDoc) return;

    auto* cmd = new cad::cmd::BreakSegmentCommand(
        m_paramDoc, blockId, segmentId, auxPointId);
    if (!cmd->isValid()) {
        delete cmd;
        return;
    }

    if (m_undoStack) {
        m_undoStack->push(cmd);
    } else {
        cmd->redo();
        delete cmd;
    }
}

bool ToolBreak::isBreakable(const QUuid& blockId, const QUuid& pointId) const
{
    if (!m_paramDoc) return false;
    const auto* block = m_paramDoc->findBlock(blockId);
    if (!block) return false;
    const auto* pt = block->findPoint(pointId);
    if (!pt) return false;

    // Intersection points on a Line/Bezier segment are breakable.
    if (pt->constraint == cad::param::PointConstraint::Intersection) {
        const auto* seg = block->findSegment(pt->hostSegmentId);
        if (!seg) return false;
        if (seg->type != cad::param::SegmentType::Line &&
            seg->type != cad::param::SegmentType::Bezier)
            return false;
        if (block->isBridge)
            return false;
        return true;
    }

    // Curve anchor points (曲线点) on a Bezier segment are breakable.
    if (pt->constraint == cad::param::PointConstraint::CurveAnchor) {
        const auto* seg = block->findSegment(pt->hostSegmentId);
        if (!seg) return false;
        if (seg->type != cad::param::SegmentType::Bezier)
            return false;
        if (block->isBridge)
            return false;
        return true;
    }

    // Must be an Interpolated auxiliary point.
    if (pt->constraint != cad::param::PointConstraint::Interpolated)
        return false;
    if (!pt->isAuxiliary)
        return false;

    // Must have no perpendicular offset.
    if (std::abs(pt->interpOffsetDist) > 1e-9)
        return false;
    if (!pt->interpOffsetDistFormula.isEmpty())
        return false;

    // Host segment must exist and be a Line/Bezier on a non-bridge block.
    const auto* seg = block->findSegment(pt->hostSegmentId);
    if (!seg) return false;
    if (seg->type != cad::param::SegmentType::Line &&
        seg->type != cad::param::SegmentType::Bezier)
        return false;
    if (block->isBridge)
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Quick-aux dialog → auto-break
// ---------------------------------------------------------------------------

void ToolBreak::openAuxDialogForBreak(const SegmentSnapResult& segSnap)
{
    if (!m_paramDoc || m_auxDialog) return;
    auto* block = m_paramDoc->findBlock(segSnap.blockId);
    auto* seg = block ? block->findSegment(segSnap.segmentId) : nullptr;
    if (!block || !seg) return;

    hideMarkers();

    // Prepare point with defaults (same as SmartPen).
    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::Interpolated;
    pt.hostSegmentId = seg->id;
    pt.isAuxiliary = true;
    pt.visible = true;
    pt.showName = false;
    pt.interpPercent = segSnap.t;
    pt.serial = m_paramDoc->newPointSerial();

    QWidget* parentWidget = m_scene && !m_scene->views().isEmpty()
        ? m_scene->views().first() : nullptr;
    auto* dlg = new QuickAuxDialog(pt, block->findPoint(seg->startPointId),
                                   block->findPoint(seg->endPointId), parentWidget);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_auxDialog = dlg;
    m_auxDialogSegSnap = segSnap;

    QObject::connect(dlg, &QDialog::finished, dlg, [this](int result) {
        auto* dlg = m_auxDialog.data();
        m_auxDialog = nullptr;
        if (result == QDialog::Accepted && dlg)
            onAuxDialogAccepted(dlg->point());
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void ToolBreak::onAuxDialogAccepted(const cad::param::ParamPoint& pt)
{
    if (!m_paramDoc || !m_scene) return;

    const QUuid blockId = m_auxDialogSegSnap.blockId;
    const QUuid segmentId = m_auxDialogSegSnap.segmentId;

    auto* block = m_paramDoc->findBlock(blockId);
    auto* seg = block ? block->findSegment(segmentId) : nullptr;
    if (!block || !seg) return;

    // Step 1: create the auxiliary point (own undo step).
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::AddAuxPointCommand(
            m_paramDoc, blockId, segmentId, pt));
    } else {
        block->addPoint(pt);
        seg->auxPointIds.push_back(pt.id);
        m_paramDoc->resolveAll();
    }

    // Step 2: verify the point was created and is breakable, then break.
    // Only break if the point has no offset (user might have set one in the dialog).
    const auto* hb = m_paramDoc->findBlock(blockId);
    const auto* created = hb ? hb->findPoint(pt.id) : nullptr;
    if (!created) return;

    if (isBreakable(blockId, pt.id)) {
        executeBreak(blockId, segmentId, pt.id);
    }
    // If not breakable (user added offset in the dialog), just leave the point.
}

} // namespace cad::tools
