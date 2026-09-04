#include "ToolRotate.h"

#include <cmath>
#include <algorithm>
#include <limits>

#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QUndoStack>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "tools/HitTester.h"
#include "tools/MarqueeGesture.h"
#include "tools/RotateCopyGesture.h"
#include "tools/RotateGizmo.h"
#include "ui/LinePropertyDialog.h"
#include "parametric/ParamDocumentRaw.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace cad::tools {

ToolDescriptor ToolRotate::describe()
{
    ToolDescriptor d;
    d.id          = ToolType::Rotate;
    d.displayName = QString::fromUtf8("旋转(&R)");
    d.iconName    = QStringLiteral("rotate");
    d.shortcut    = QKeySequence(Qt::Key_R);
    d.hintText    = QString::fromUtf8("旋转：点击线段选中 | 右键或回车确认 | 拖动旋转(Shift吸附15°) | HUD输入角度/公式 | X切换锚心 | Esc反悔");
    d.factory     = []() { return std::make_unique<ToolRotate>(); };
    return d;
}

ToolRotate::~ToolRotate()
{
    delete m_copyGesture;
    delete m_gizmo;
    delete m_marqueeGesture;
}

void ToolRotate::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)paramDoc;
    m_state = RotateState::Idle;
    delete m_copyGesture;
    m_copyGesture = new RotateCopyGesture(this);
    delete m_gizmo;
    m_gizmo = new RotateGizmo(&scene);
    delete m_marqueeGesture;
    m_marqueeGesture = new MarqueeGesture(&scene, paramDoc);
}

void ToolRotate::onDeactivate()
{
    if (m_copyGesture && m_copyGesture->active())
        m_copyGesture->cancel();
    delete m_copyGesture;
    m_copyGesture = nullptr;
    delete m_gizmo;
    m_gizmo = nullptr;
    if (m_marqueeGesture) {
        m_marqueeGesture->cancel();
        delete m_marqueeGesture;
        m_marqueeGesture = nullptr;
    }
    m_input.teardown();
    m_aimSnap.teardown();
    m_multi.clear();
    syncSelectionVisual();

    if (m_session.anchor().releaseAttHeld && !m_session.anchor().releaseAttId.isNull() && m_paramDoc) {
        cad::param::RawModelAccess::addAttachmentRaw(*m_paramDoc, m_session.anchor().releaseAttBackup);
        m_paramDoc->resolveAll();
        m_session.anchor().releaseAttId = QUuid();
        m_session.anchor().releaseAttHeld = false;
    }
    removeGizmo();
    reportPinnedTarget(QUuid(), QUuid());
    reportHoverTarget(QUuid(), QUuid());
    reportHintOverride(QString());
    m_session.clear();
    m_state = RotateState::Idle;
    reportRotateAnchorState();
}

void ToolRotate::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    const cad::geo::Vec2 pos(event->scenePos().x(), event->scenePos().y());

    if (event->button() == Qt::RightButton) {
        if (m_state == RotateState::Rotating) {
            cancelRotation();
            if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
                m_input.setPivotPicked(false);
                removeGizmo();
                updateStatusHint();
            } else {
                applySelectionConfirmed(false);
            }
            event->accept();
            return;
        }
        if (m_input.pressPending()) {
            m_input.setPressPending(false);
        }
        if (m_state == RotateState::Ready && (!m_multi.selection().isEmpty() || !m_session.blockId().isNull())) {
            if (m_selectionConfirmed) {
                if ((isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) && m_input.pivotPicked()) {
                    m_input.setPivotPicked(false);
                    removeGizmo();
                    updateStatusHint();
                } else {
                    applySelectionConfirmed(false);
                }
            } else {
                applySelectionConfirmed(true);
            }
            event->accept();
            return;
        } else {
            clearTarget();
            event->accept();
            return;
        }
    }
    if (event->button() != Qt::LeftButton) return;

    switch (m_state) {
    case RotateState::Idle: {
        const QUuid id = hitBlock(pos);
        if (!id.isNull()) {
            selectTarget(id, pos);
            if (event->modifiers() & Qt::ControlModifier)
                m_copyGesture->begin(pos);
        } else {
            if (m_marqueeGesture) {
                m_marqueeGesture->begin(pos, m_multi.selection());
            }
        }
        break;
    }
    case RotateState::Ready: {
        if (!m_selectionConfirmed) {
            if (event->modifiers() & Qt::ShiftModifier) {
                const QUuid hit = hitBlock(pos);
                if (!hit.isNull()) {
                    if (m_multi.selection().contains(hit)) m_multi.selection().remove(hit);
                    else                                  m_multi.selection().insert(hit);
                    adoptSelection(m_multi.selection());
                } else if (m_marqueeGesture) {
                    m_marqueeGesture->begin(pos, m_multi.selection());
                }
                break;
            }

            if (isMultiSelect() || m_multi.isMarqueeSelected()) {
                const QUuid hit = hitBlock(pos);
                if (!hit.isNull()) {
                    if (!m_multi.selection().contains(hit)) {
                        selectTarget(hit, pos);
                    }
                } else if (m_marqueeGesture) {
                    m_marqueeGesture->begin(pos, m_multi.selection());
                }
                break;
            }

            const QUuid hitEnd = m_session.anchorPointAt(m_paramDoc, pos, currentZoom());
            bool endpointSwitched = false;
            if (!hitEnd.isNull() && hitEnd != m_session.anchor().pointId) {
                bool anchorLocked = false;
                if (const auto* blk = m_paramDoc->findBlock(m_session.blockId());
                    blk && !blk->segments.empty()) {
                    anchorLocked =
                        m_session.attachmentAtPoint(m_paramDoc, blk->segments.front().startPointId)
                        || m_session.attachmentAtPoint(m_paramDoc, blk->segments.front().endPointId);
                }
                if (!anchorLocked) {
                    commitCurrent();
                    m_session.anchor().isEnd = false;
                    if (const auto* blk = m_paramDoc->findBlock(m_session.blockId());
                        blk && !blk->segments.empty()) {
                        m_session.anchor().isEnd = (hitEnd == blk->segments.front().endPointId);
                    }
                    rebuildAnchorState();
                    reportRotateAnchorState();
                    updateStatusHint();
                    removeGizmo();
                    buildGizmo();
                    updateGizmo();
                    endpointSwitched = true;
                }
            }
            if (endpointSwitched) break;

            const QUuid self = hitBlock(pos);
            if (!self.isNull() && self != m_session.blockId()) {
                commitCurrent();
                selectTarget(self, pos);
                break;
            }

            const auto* tb = m_paramDoc ? m_paramDoc->findBlock(m_session.blockId()) : nullptr;
            if (!tb) { clearTarget(); break; }
            if (self.isNull()) {
                clearTarget();
            }
            break;
        }

        if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
            if (!m_input.pivotPicked()) {
                const cad::geo::Vec2 pivot = m_input.hoverSnapped() ? m_input.hoverSnapPoint() : pos;
                m_session.setPivot(pivot);
                m_input.setPivotPicked(true);
                m_input.hideHoverSnap();
                buildGizmo();
                updateGizmo();
                m_input.setPressPending(true);
                m_input.setPressPos(pos);
                m_input.setPendingPivot(pivot);
                updateStatusHint();
                break;
            } else {
                beginRotation(pos);
                break;
            }
        }

        const bool ctrl = (event->modifiers() & Qt::ControlModifier);
        if (ctrl && !isMultiSelect()) {
            m_copyGesture->begin(pos);
            break;
        }
        beginRotation(pos);
        break;
    }
    case RotateState::Rotating:
        break;
    }
}

void ToolRotate::mouseMove(QGraphicsSceneMouseEvent* event)
{
    const cad::geo::Vec2 pos(event->scenePos().x(), event->scenePos().y());

    if (m_marqueeGesture && m_marqueeGesture->active()) {
        m_marqueeGesture->update(pos);
        return;
    }

    if (m_state != RotateState::Rotating) {
        if (m_input.pressPending()) {
            const double zoom = currentZoom();
            if ((pos - m_input.pressPos()).length() > 5.0 / zoom) {
                m_input.setPressPending(false);
                beginRotation(pos);
                const bool snap = (event->modifiers() & Qt::ShiftModifier);
                updateRotation(pos, snap);
                return;
            }
        }

        if (m_state == RotateState::Ready && m_selectionConfirmed &&
            (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) && !m_input.pivotPicked()) {
            m_input.updateHoverSnap(m_scene, m_paramDoc, pos);
        } else {
            m_input.hideHoverSnap();
        }

        const QUuid hover = hitBlock(pos);
        if (hover.isNull()) {
            reportHoverTarget(QUuid(), QUuid());
        } else if (m_paramDoc) {
            const auto* blk = m_paramDoc->findBlock(hover);
            reportHoverTarget(hover, (blk && !blk->segments.empty())
                                         ? blk->segments.front().id : QUuid());
        }
        return;
    }

    if (!isMultiSelect() && !m_copyGesture->active() && (event->modifiers() & Qt::ControlModifier)) {
        m_copyGesture->convert(pos);
        if (!m_copyGesture->active()) return;
    }
    const bool snap = (event->modifiers() & Qt::ShiftModifier);
    updateRotation(pos, snap);
}

void ToolRotate::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;

    if (m_marqueeGesture && m_marqueeGesture->active()) {
        const QPointF up = event->scenePos();
        const cad::geo::Vec2 pos(up.x(), up.y());
        const double zoom = currentZoom();
        const double dist = (pos - m_marqueeGesture->startPos()).length();
        QSet<QUuid> hits = m_marqueeGesture->end(pos);

        if (dist < 3.0 / zoom) {
            if (!(event->modifiers() & Qt::ShiftModifier)) {
                clearTarget();
                return;
            }
        } else {
            for (auto it = hits.begin(); it != hits.end(); ) {
                const auto* blk = m_paramDoc ? m_paramDoc->findBlock(*it) : nullptr;
                if (!blk || blk->isBridge || blk->segments.empty()) {
                    it = hits.erase(it);
                } else {
                    ++it;
                }
            }
            if (!hits.isEmpty()) {
                adoptSelection(hits);
                m_multi.setMarqueeSelected(true);
                applySelectionConfirmed(false);
            } else {
                clearTarget();
            }
            return;
        }
    }

    if (m_input.pressPending()) {
        m_input.setPressPending(false);
        m_input.setPivotPicked(true);
        buildGizmo();
        updateGizmo();
        updateStatusHint();
        return;
    }

    if (m_state != RotateState::Rotating) return;

    if (QGuiApplication::mouseButtons() & Qt::LeftButton) {
        return;
    }

    if (m_copyGesture->active()) {
        m_copyGesture->commit();
        applySelectionConfirmed(false);
    } else {
        commitRotation();
        clearTarget();
    }
}

void ToolRotate::mouseDoubleClick(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc || event->button() != Qt::LeftButton) return;

    if (m_state == RotateState::Rotating)
        cancelRotation();
    clearTarget();

    const auto hits = blockHitsAtScene(*m_scene, *m_paramDoc, event->scenePos());
    if (hits.empty() || hits.front().segmentId.isNull()) return;

    QWidget* parentWidget = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    auto* dlg = new cad::ui::LinePropertyDialog(hits.front().blockId, hits.front().segmentId,
                                                m_paramDoc, m_scene, parentWidget);
    dlg->show();
}

void ToolRotate::keyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_marqueeGesture && m_marqueeGesture->active()) {
            m_marqueeGesture->cancel();
        } else if (m_state == RotateState::Rotating) {
            cancelRotation();
            if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
                m_input.setPivotPicked(false);
                removeGizmo();
                updateStatusHint();
            } else {
                applySelectionConfirmed(false);
            }
        } else if (m_state == RotateState::Ready && m_selectionConfirmed) {
            if ((isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) && m_input.pivotPicked()) {
                m_input.setPivotPicked(false);
                removeGizmo();
                updateStatusHint();
            } else {
                applySelectionConfirmed(false);
                removeGizmo();
            }
        } else {
            restoreBase();
            clearTarget();
        }
        event->accept();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        if (m_state == RotateState::Ready && !m_selectionConfirmed)
            applySelectionConfirmed(true);
        event->accept();
    } else if (event->key() == Qt::Key_X) {
        if (!m_selectionConfirmed) toggleAnchor();
        event->accept();
    }
}

void ToolRotate::toggleAnchor()
{
    if (m_state != RotateState::Ready || !m_paramDoc) return;
    const cad::param::Block* blk = m_paramDoc->findBlock(m_session.blockId());
    if (!blk || blk->segments.empty()) return;

    if (m_session.attachmentAtPoint(m_paramDoc, blk->segments.front().startPointId)
        || m_session.attachmentAtPoint(m_paramDoc, blk->segments.front().endPointId))
        return;

    commitCurrent();
    m_session.toggleAnchor(m_paramDoc);
    reportRotateAnchorState();
    updateStatusHint();

    removeGizmo();
    buildGizmo();
    updateGizmo();
    if (m_scene) m_scene->refreshAllBlockItems();
}

void ToolRotate::rebuildAnchorState()
{
    m_session.rebuildAnchorState(m_paramDoc);
    if (m_session.blockId().isNull()) {
        clearTarget();
    }
}

void ToolRotate::syncSelectionVisual()
{
    if (!m_scene || !m_paramDoc) return;
    for (const auto& blk : m_paramDoc->blocks()) {
        if (BlockItem* bi = m_scene->findBlockItem(blk.id)) {
            const bool inSel = m_multi.selection().contains(blk.id);
            bi->setToolSelected(inSel);
            bi->setToolLocked(inSel);
        }
    }
}

void ToolRotate::adoptSelection(const QSet<QUuid>& blockIds)
{
    if (!m_paramDoc || !m_scene) return;
    clearTarget();
    m_multi.adoptSelection(m_paramDoc, blockIds);
    if (m_multi.selection().isEmpty()) return;

    if (m_multi.selection().size() == 1) {
        m_session.setupTarget(m_paramDoc, *m_multi.selection().begin());
    } else {
        m_session.clear();
    }
    m_state = RotateState::Ready;
    applySelectionConfirmed(false);
    syncSelectionVisual();
    updateStatusHint();
    m_scene->refreshAllBlockItems();
}

void ToolRotate::selectTarget(const QUuid& blockId,
                              const std::optional<cad::geo::Vec2>& clickWorld)
{
    if (!m_paramDoc || !m_scene) return;
    cad::param::Block* blk = m_paramDoc->findBlock(blockId);
    if (!blk || blk->segments.empty()) return;
    if (blk->isBridge) return;

    if (m_state != RotateState::Idle)
        removeGizmo();

    m_session.setupTarget(m_paramDoc, blockId, clickWorld);
    m_multi.adoptSelection(m_paramDoc, {blockId});
    m_multi.setMarqueeSelected(false);
    syncSelectionVisual();

    applySelectionConfirmed(false);
    if (m_session.blockId().isNull()) return;

    m_state = RotateState::Ready;
    reportRotateAnchorState();
    buildGizmo();
    updateGizmo();
    reportStripTarget();
    updateStatusHint();
    m_scene->refreshAllBlockItems();
}

void ToolRotate::clearTarget()
{
    removeGizmo();
    clearAimCandidate();
    m_session.clear();
    m_multi.clear();
    m_input.resetPress();
    m_input.hideHoverSnap();
    syncSelectionVisual();
    applySelectionConfirmed(false);
    m_state = RotateState::Idle;
    reportStripTarget();
    reportRotateAnchorState();
    updateStatusHint();
    if (m_scene) m_scene->refreshAllBlockItems();
}

void ToolRotate::beginRotation(const cad::geo::Vec2& pos)
{
    if (m_state != RotateState::Ready) return;

    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
        m_multi.captureBase(m_paramDoc);

        m_state = RotateState::Rotating;
        const cad::geo::Vec2 d = pos - m_session.pivot();
        m_dragCursorAngle0 = std::atan2(d.y, d.x);
        m_dragCursorAnglePrev = m_dragCursorAngle0;
        m_accumulatedAngleDeg = 0.0;
        m_dragAngle0 = 0.0;

        if (m_paramDoc) m_paramDoc->resolveAll();
        if (m_scene) m_scene->syncBlockPositions();
        updateStatusHint();
        return;
    }

    if (isAngleLocked() && !m_session.shadow().active()) {
        updateStatusHint();
        return;
    }

    m_session.releaseFollowerIfAnchorMoved(m_paramDoc, m_scene);

    if (m_paramDoc) {
        if (auto* blk = m_paramDoc->findBlock(m_session.blockId());
            blk && !blk->endTargetBlockId.isNull()) {
            blk->endTargetBlockId = QUuid();
            blk->endTargetPointId = QUuid();
            blk->endTargetOffset = 0.0;
            blk->endTargetOffsetFormula.clear();
            m_paramDoc->resolveAll();
            if (m_scene) m_scene->refreshAllBlockItems();
        }
    }
    m_state = RotateState::Rotating;
    const cad::geo::Vec2 d = pos - m_session.pivot();
    m_dragCursorAngle0 = std::atan2(d.y, d.x);
    m_dragCursorAnglePrev = m_dragCursorAngle0;
    m_accumulatedAngleDeg = 0.0;
    m_dragAngle0 = currentAngleDeg();
}

void ToolRotate::updateRotation(const cad::geo::Vec2& pos, bool snap)
{
    if (m_state != RotateState::Rotating) return;
    const cad::geo::Vec2 d = pos - m_session.pivot();
    const double theta = std::atan2(d.y, d.x);
    const double stepRad = cad::geo::normalizeRad(theta - m_dragCursorAnglePrev);
    m_dragCursorAnglePrev = theta;
    m_accumulatedAngleDeg += cad::geo::radToDeg(stepRad);

    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
        double deltaDeg = m_accumulatedAngleDeg;
        if (snap) deltaDeg = std::round(deltaDeg / 15.0) * 15.0;
        m_multi.applyModeValue(m_paramDoc, m_scene, m_session.pivot(), deltaDeg);
        updateGizmo();
        updateStatusHint();
        return;
    }

    double target;
    if (m_copyGesture->active()) {
        target = m_dragAngle0 + m_accumulatedAngleDeg;
        if (snap) target = std::round(target / 15.0) * 15.0;
    } else if (m_session.isConnected()) {
        const double alpha0 = cad::geo::normalizeDeg360(m_dragAngle0);
        double alpha = cad::geo::normalizeDeg360(alpha0 - m_accumulatedAngleDeg);
        if (snap) alpha = std::round(alpha / 15.0) * 15.0;
        target = cad::geo::normalizeDeg180(alpha);
    } else {
        target = m_dragAngle0 + m_accumulatedAngleDeg;
        if (snap) target = std::round(target / 15.0) * 15.0;
    }

    if (!snap) checkEndpointAimSnap(target);

    applyAngleDeg(target);
    updateGizmo();
}

void ToolRotate::applyAngleDeg(double deg)
{
    m_session.applyAngleDeg(m_paramDoc, m_scene, deg, m_copyGesture, m_dragAngle0);
}

void ToolRotate::applyShadowAngleDeg(double deg)
{
    m_session.applyShadowAngleDeg(m_paramDoc, deg, m_dragAngle0);
}

void ToolRotate::applyModeValue(double value)
{
    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
        m_multi.applyModeValue(m_paramDoc, m_scene, m_session.pivot(), value);
        updateGizmo();
        updateStatusHint();
        return;
    }
    m_session.applyModeValue(m_paramDoc, m_scene, value, m_copyGesture);
}

double ToolRotate::segmentRadius() const
{
    return m_session.segmentRadius(m_paramDoc);
}

double ToolRotate::currentModeValue() const
{
    return m_session.currentModeValue(m_paramDoc, m_copyGesture);
}

void ToolRotate::commitRotation()
{
    if (m_state != RotateState::Rotating) return;
    commitCurrent();
    m_state = RotateState::Ready;
    applySelectionConfirmed(false);
    updateGizmo();
    updateStatusHint();
}

void ToolRotate::cancelRotation()
{
    if (m_state != RotateState::Rotating) return;
    restoreBase();
    m_state = RotateState::Ready;
    clearAimCandidate();
    updateGizmo();
    updateStatusHint();
}

void ToolRotate::commitCurrent()
{
    if (!m_paramDoc || !m_undoStack) return;

    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
        m_multi.commit(m_paramDoc, m_undoStack);
    } else {
        m_session.commit(m_paramDoc, m_undoStack);
    }
    updateGizmo();
}

void ToolRotate::restoreBase()
{
    if (!m_paramDoc) return;
    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
        m_multi.restoreBase(m_paramDoc, m_scene);
        return;
    }
    m_session.restoreBase(m_paramDoc, m_scene);
}

double ToolRotate::currentAngleDeg() const
{
    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected()))
        return m_multi.accumulatedAngleDeg();
    return m_session.currentAngleDeg(m_paramDoc, m_copyGesture);
}

bool ToolRotate::isAngleLocked() const
{
    return m_session.isAngleLocked(m_copyGesture);
}

void ToolRotate::reportStripTarget()
{
    if (m_session.blockId().isNull() || !m_paramDoc) {
        reportPinnedTarget(QUuid(), QUuid());
        return;
    }
    const auto* blk = m_paramDoc->findBlock(m_session.blockId());
    if (!blk || blk->segments.empty()) {
        reportPinnedTarget(QUuid(), QUuid());
        return;
    }
    reportPinnedTarget(m_session.blockId(), blk->segments.front().id);
}

void ToolRotate::reportRotateAnchorState()
{
    if (!m_host) return;
    const bool active = !m_session.blockId().isNull() && m_state != RotateState::Idle;
    bool canToggle = active && m_state == RotateState::Ready;
    QString reason;
    if (canToggle && m_paramDoc) {
        if (const auto* blk = m_paramDoc->findBlock(m_session.blockId());
            blk && !blk->segments.empty()) {
            const auto& seg = blk->segments.front();
            if (m_session.attachmentAtPoint(m_paramDoc, seg.startPointId)
                || m_session.attachmentAtPoint(m_paramDoc, seg.endPointId)) {
                canToggle = false;
                reason = QString::fromUtf8("已连接线段禁止切换锚心（先断开连接）");
            }
        }
    }
    m_host->setRotateAnchorState(active, m_session.anchor().isEnd, canToggle, reason);
}

void ToolRotate::onReverseRequested(const QUuid& /*blockId*/, const QUuid& /*segmentId*/)
{
    toggleAnchor();
}

void ToolRotate::updateStatusHint()
{
    if (m_copyGesture && m_copyGesture->active()) {
        reportHintOverride(QString::fromUtf8("旋转复制 %1°").arg(
            cad::geo::Units::formatDegValue(m_copyGesture->currentRelativeAngle())));
        return;
    }
    switch (m_state) {
    case RotateState::Idle:
        reportHintOverride(QString());
        return;
    case RotateState::Rotating:
        reportHintOverride(QString::fromUtf8("旋转中 · 松手提交 · Esc 回位"));
        return;
    case RotateState::Ready: {
        if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
            if (m_selectionConfirmed) {
                if (m_input.pivotPicked()) {
                    reportHintOverride(QString::fromUtf8("已指定旋转中心 · 拖动旋转(Shift吸附15°) | Esc重选锚点"));
                } else {
                    reportHintOverride(QString::fromUtf8("请指定旋转中心（锚点）：点击线段端点或画布任意位置 | Esc返回选区"));
                }
            } else {
                reportHintOverride(QString::fromUtf8("已选 %1 条线段 · 右键或回车确认选区 | Shift加减选 | Esc清除")
                    .arg(m_multi.selection().size()));
            }
            return;
        }

        const QString anchorTag = m_session.anchorTag(m_paramDoc);
        if (isAngleLocked()) {
            reportHintOverride(QString::fromUtf8(
                "旋转：锚心 %1 · 角度由变量/公式驱动，已锁定（移除公式后可旋转）")
                .arg(anchorTag));
            return;
        }
        reportHintOverride(m_selectionConfirmed
            ? QString::fromUtf8("旋转：锚心 %1 · 拖动旋转(Shift吸附) | 长按Ctrl拖动复制")
                  .arg(anchorTag)
            : QString::fromUtf8("旋转：锚心 %1 · 右键或回车确认后拖动")
                  .arg(anchorTag));
        return;
    }
    }
}

double ToolRotate::currentZoom() const
{
    if (m_scene) {
        const double z = m_scene->currentZoom();
        if (z > 1e-9) return z;
    }
    return 1.0;
}

cad::geo::Vec2 ToolRotate::endpointAtAngle(double angleDeg) const
{
    return RotateAimSnap::endpointAtAngle(m_paramDoc, m_session.blockId(),
                                          m_session.pivot(), m_session.refWorldRad(),
                                          m_session.isConnected(), m_copyGesture, angleDeg);
}

void ToolRotate::checkEndpointAimSnap(double& angleDeg)
{
    m_aimSnap.checkSnap(m_paramDoc, m_scene, m_session.blockId(),
                        m_session.pivot(), m_session.refWorldRad(),
                        m_session.isConnected(), currentZoom(),
                        m_copyGesture, angleDeg);
}

void ToolRotate::clearAimCandidate()
{
    m_aimSnap.clear();
}

void ToolRotate::buildGizmo()
{
    if (m_gizmo)
        m_gizmo->build(m_session.pivot(), m_session.refWorldRad(), currentZoom());
}

double ToolRotate::originalWorldRotDeg() const
{
    return m_session.originalWorldRotDeg(m_paramDoc);
}

void ToolRotate::applySelectionConfirmed(bool confirmed)
{
    if (m_selectionConfirmed == confirmed) return;
    m_selectionConfirmed = confirmed;
    if (!confirmed) {
        m_input.resetPress();
        m_input.hideHoverSnap();
        if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
            removeGizmo();
        }
    }
    syncSelectionVisual();
    updateGizmo();
    updateStatusHint();
}

bool ToolRotate::gizmoConfirmed() const
{
    return m_gizmo && m_gizmo->confirmed();
}

void ToolRotate::updateGizmo()
{
    if (!m_gizmo) return;
    m_gizmo->setConfirmed(m_selectionConfirmed
                          || (m_copyGesture && m_copyGesture->active()));

    double dashRad = m_session.refWorldRad();
    double arcStart = 0.0;
    double arcEnd = 0.0;

    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
        dashRad = m_dragCursorAngle0;
        arcStart = m_dragCursorAngle0;
        arcEnd = m_dragCursorAngle0 + cad::geo::degToRad(m_accumulatedAngleDeg);
    } else if (m_copyGesture && m_copyGesture->active()) {
        double origRad = std::fmod(originalWorldRotDeg(), 360.0) * M_PI / 180.0;
        if (m_session.anchor().isEnd) origRad += M_PI;
        origRad = cad::geo::normalizeRad(origRad);

        double cloneRad = m_copyGesture->currentWorldRad();
        if (m_session.anchor().isEnd) cloneRad += M_PI;
        cloneRad = cad::geo::normalizeRad(cloneRad);

        dashRad = origRad;
        arcStart = origRad;
        double span = cloneRad - arcStart;
        while (span >  M_PI) span -= 2.0 * M_PI;
        while (span < -M_PI) span += 2.0 * M_PI;
        arcEnd = arcStart + span;
    } else {
        const auto ga = m_session.calculateGizmoAngles(
            currentAngleDeg(), m_state == RotateState::Rotating, m_dragAngle0);
        dashRad = ga.dashRad;
        arcStart = ga.arcStart;
        arcEnd = ga.arcEnd;
    }
    m_gizmo->update(currentZoom(), dashRad, arcStart, arcEnd);
    updateStatusHint();
}

void ToolRotate::removeGizmo()
{
    if (m_gizmo) m_gizmo->remove();
}

QUuid ToolRotate::hitBlock(const cad::geo::Vec2& worldPos) const
{
    if (!m_scene || !m_paramDoc) return QUuid();
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const auto hits = blockHitsAtScene(*m_scene, *m_paramDoc, scenePt);
    return hits.empty() ? QUuid() : hits.front().blockId;
}

} // namespace cad::tools
