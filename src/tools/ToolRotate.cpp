#include "ToolRotate.h"

#include <cmath>
#include <algorithm>

#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QUndoStack>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "tools/HitTester.h"
#include "tools/InteractionTolerances.h"  // kDragThresholdPx
#include "tools/MarqueeGesture.h"
#include "tools/RotateCopyGesture.h"
#include "tools/RotateGizmo.h"
#include "tools/RotateDragMath.h"
#include "tools/RotateHintTexts.h"
#include "parametric/ParamDocumentRaw.h"
#include "parametric/FollowerAngle.h"
#include "geometry/Epsilon.h"

namespace cad::tools {

ToolDescriptor ToolRotate::describe()
{
    return ToolDescriptor{
        .id          = ToolType::Rotate,
        .displayName = QString::fromUtf8("旋转(&R)"),
        .iconName    = QStringLiteral("rotate"),
        .shortcut    = QKeySequence(Qt::CTRL | Qt::Key_T),
        .hintText    = QString::fromUtf8("旋转：选择线段 | 右键或回车确认 | 点击指定中心 | 拖动旋转(Shift切换约束) | 回车/松开提交 | Esc取消"),
        .factory     = []() { return std::make_unique<ToolRotate>(); },
    };
}

ToolRotate::~ToolRotate() = default;

void ToolRotate::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)paramDoc;
    m_state = RotateState::Idle;
    m_constraintMode = RotateConstraintMode::None;
    m_copyGesture = std::make_unique<RotateCopyGesture>(this);
    m_gizmo = std::make_unique<RotateGizmo>(&scene);
    m_marqueeGesture = std::make_unique<MarqueeGesture>(&scene, paramDoc);
}

void ToolRotate::onDeactivate()
{
    if (m_copyGesture && m_copyGesture->active()) m_copyGesture->cancel();
    m_copyGesture.reset();
    m_gizmo.reset();
    if (m_marqueeGesture) { m_marqueeGesture->cancel(); m_marqueeGesture.reset(); }
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
    m_constraintMode = RotateConstraintMode::None;
    reportRotateAnchorState();
}

void ToolRotate::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    const cad::geo::Vec2 pos(event->scenePos().x(), event->scenePos().y());

    if (event->button() == Qt::RightButton) {
        handleRightButtonPress(event);
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    if (!m_selectionConfirmed) {
        handleSelectingPress(pos, event);
        return;
    }

    handlePivotOrRotatePress(pos, event);
}

void ToolRotate::handleRightButtonPress(QGraphicsSceneMouseEvent* event)
{
    if (m_copyGesture && m_copyGesture->active()) {
        m_copyGesture->cancel();
        m_phase = m_selectionConfirmed ? (m_input.pivotPicked() ? RotatePhase::ReadyToRotate : RotatePhase::PickingPivot) : RotatePhase::Selecting;
        updateGizmo();
        updateStatusHint();
        event->accept();
        return;
    }
    if (m_state == RotateState::Rotating) {
        cancelRotation();
        m_phase = m_input.pivotPicked() ? RotatePhase::ReadyToRotate : RotatePhase::PickingPivot;
        updateStatusHint();
        event->accept();
        return;
    }
    if (m_input.pressPending()) m_input.setPressPending(false);
    if (!m_multi.selection().isEmpty() || !m_session.blockId().isNull()) {
        if (m_selectionConfirmed) {
            if (m_input.pivotPicked()) {
                m_input.setPivotPicked(false);
                m_phase = RotatePhase::PickingPivot;
                removeGizmo();
                updateStatusHint();
            } else {
                applySelectionConfirmed(false);
            }
        } else {
            applySelectionConfirmed(true);
        }
        event->accept();
    } else {
        clearTarget();
        event->accept();
    }
}

bool ToolRotate::trySwitchAnchor(const QUuid& hitEnd)
{
    bool anchorLocked = false;
    if (const auto* blk = m_paramDoc->findBlock(m_session.blockId()); blk && !blk->segments.empty()) {
        anchorLocked = m_session.attachmentAtPoint(m_paramDoc, blk->segments.front().startPointId)
                    || m_session.attachmentAtPoint(m_paramDoc, blk->segments.front().endPointId);
    }
    if (anchorLocked) return false;

    commitCurrent();
    m_session.anchor().isEnd = false;
    if (const auto* blk = m_paramDoc->findBlock(m_session.blockId()); blk && !blk->segments.empty()) {
        m_session.anchor().isEnd = (hitEnd == blk->segments.front().endPointId);
    }
    rebuildAnchorState();
    reportRotateAnchorState();
    updateStatusHint();
    removeGizmo();
    buildGizmo();
    updateGizmo();
    return true;
}

void ToolRotate::handleSelectingPress(const cad::geo::Vec2& pos, QGraphicsSceneMouseEvent* event)
{
    if (event->modifiers() & Qt::ShiftModifier) {
        const QUuid hit = hitBlock(pos);
        if (!hit.isNull()) {
            if (m_multi.selection().contains(hit)) m_multi.selection().remove(hit);
            else                                  m_multi.selection().insert(hit);
            adoptSelection(m_multi.selection());
        } else if (m_marqueeGesture) {
            m_marqueeGesture->begin(pos, m_multi.selection());
        }
        return;
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
        return;
    }

    const QUuid hitEnd = m_session.anchorPointAt(m_paramDoc, pos, currentZoom());
    if (!hitEnd.isNull() && hitEnd != m_session.anchor().pointId) {
        if (trySwitchAnchor(hitEnd)) return;
    }

    const QUuid self = hitBlock(pos);
    if (!self.isNull()) {
        if (self != m_session.blockId()) {
            commitCurrent();
            selectTarget(self, pos);
        }
        if ((event->modifiers() & Qt::ControlModifier) && !isMultiSelect()) {
            m_input.setPivotPicked(true);
            m_phase = RotatePhase::Rotating;
            m_copyGesture->begin(pos);
            return;
        }
    } else {
        if (m_state == RotateState::Idle) {
            if (m_marqueeGesture) {
                m_marqueeGesture->begin(pos, m_multi.selection());
            }
        } else {
            clearTarget();
        }
    }
}

void ToolRotate::handlePivotOrRotatePress(const cad::geo::Vec2& pos, QGraphicsSceneMouseEvent* event)
{
    const bool ctrl = (event->modifiers() & Qt::ControlModifier);
    if (ctrl && !isMultiSelect()) {
        m_input.setPivotPicked(true);
        m_phase = RotatePhase::Rotating;
        m_copyGesture->begin(pos);
        return;
    }

    if (!m_input.pivotPicked()) {
        const bool hasSingleDefaultAnchor = !isMultiSelect() && !m_multi.isMarqueeSelected() && !m_session.blockId().isNull();
        const cad::geo::Vec2 pivot = m_input.hoverSnapped() ? m_input.hoverSnapPoint() : pos;
        m_input.setPendingHoverSnapped(m_input.hoverSnapped());
        m_input.hideHoverSnap();
        m_input.setPressPending(true);
        m_input.setPressPos(pos);
        m_input.setPendingPivot(pivot);

        if (hasSingleDefaultAnchor) {
            beginRotation(pos);
        }
        updateStatusHint();
        return;
    }

    beginRotation(pos);
    m_phase = RotatePhase::Rotating;
}

void ToolRotate::mouseMove(QGraphicsSceneMouseEvent* event)
{
    const cad::geo::Vec2 pos(event->scenePos().x(), event->scenePos().y());
    m_lastMousePos = pos;

    if (m_marqueeGesture && m_marqueeGesture->active()) {
        m_marqueeGesture->update(pos);
        return;
    }

    if (m_state != RotateState::Rotating || m_input.pressPending()) {
        if (m_input.pressPending()) {
            const double zoom = currentZoom();
            if ((pos - m_input.pressPos()).length() > kDragThresholdPx / zoom) {
                m_input.setPressPending(false);
                m_input.setPivotPicked(true);
                const bool hasSingleDefaultAnchor = !isMultiSelect() && !m_multi.isMarqueeSelected() && !m_session.blockId().isNull();
                if (!hasSingleDefaultAnchor || m_input.pendingHoverSnapped()) {
                    m_session.setPivot(m_input.pendingPivot());
                    beginRotation(m_input.pressPos());
                }
                const bool ctrl = (event->modifiers() & Qt::ControlModifier);
                if (ctrl && !isMultiSelect()) {
                    m_phase = RotatePhase::Rotating;
                    m_copyGesture->begin(m_input.pressPos());
                } else {
                    m_phase = RotatePhase::Rotating;
                    if (!hasSingleDefaultAnchor && !m_input.pendingHoverSnapped()) {
                        beginRotation(m_input.pressPos());
                    }
                }
                buildGizmo();
                updateRotation(pos, false);
                return;
            }
        }

        if (m_state == RotateState::Ready && m_selectionConfirmed && !m_input.pivotPicked()) {
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
    updateRotation(pos, false);
}

void ToolRotate::handleMarqueeRelease(const cad::geo::Vec2& pos, QGraphicsSceneMouseEvent* event)
{
    const double zoom = currentZoom();
    const double dist = (pos - m_marqueeGesture->startPos()).length();
    QSet<QUuid> hits = m_marqueeGesture->end(pos);

    if (dist < kDragThresholdPx / zoom) {  // 同一「点击 vs 拖动」量纲，统一 5px
        if (!(event->modifiers() & Qt::ShiftModifier)) {
            clearTarget();
        }
        return;
    }
    for (auto it = hits.begin(); it != hits.end(); ) {
        const auto* blk = m_paramDoc ? m_paramDoc->findBlock(*it) : nullptr;
        if (!blk || blk->isBridge || blk->segments.empty()
            || !isInteractiveBlock(*blk, *m_paramDoc)) {
            it = hits.erase(it);
        } else {
            ++it;
        }
    }
    if (!hits.isEmpty()) {
        adoptSelection(hits);
        m_multi.setMarqueeSelected(hits.size() > 1);
        applySelectionConfirmed(false);
    } else {
        clearTarget();
    }
}

void ToolRotate::handlePendingPivotRelease()
{
    m_input.setPressPending(false);
    m_input.setPivotPicked(true);
    if (m_state == RotateState::Rotating) {
        restoreBase();
        m_state = RotateState::Ready;
    }
    if (!isMultiSelect() && !m_multi.isMarqueeSelected() && !m_session.blockId().isNull()) {
        const QUuid hitEnd = m_session.anchorPointAt(m_paramDoc, m_input.pendingPivot(), currentZoom());
        if (!hitEnd.isNull()) {
            if (const auto* blk = m_paramDoc->findBlock(m_session.blockId()); blk && !blk->segments.empty()) {
                m_session.anchor().isEnd = (hitEnd == blk->segments.front().endPointId);
                m_session.rebuildAnchorState(m_paramDoc);
            }
        }
    }
    m_session.setPivot(m_input.pendingPivot());
    m_phase = RotatePhase::ReadyToRotate;
    buildGizmo();
    updateGizmo();
    updateStatusHint();
}

void ToolRotate::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;

    if (m_marqueeGesture && m_marqueeGesture->active()) {
        const QPointF up = event->scenePos();
        handleMarqueeRelease(cad::geo::Vec2(up.x(), up.y()), event);
        return;
    }

    if (m_input.pressPending()) {
        handlePendingPivotRelease();
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

    openRotateLinePropertyDialog(m_scene, m_paramDoc, event->scenePos());
}

void ToolRotate::handleEscapeKey()
{
    if (m_marqueeGesture && m_marqueeGesture->active()) {
        m_marqueeGesture->cancel();
    } else if (m_copyGesture && m_copyGesture->active()) {
        m_copyGesture->cancel();
        m_phase = m_selectionConfirmed ? (m_input.pivotPicked() ? RotatePhase::ReadyToRotate : RotatePhase::PickingPivot) : RotatePhase::Selecting;
        updateGizmo();
        updateStatusHint();
    } else if (m_state == RotateState::Rotating) {
        cancelRotation();
        m_phase = m_input.pivotPicked() ? RotatePhase::ReadyToRotate : RotatePhase::PickingPivot;
        updateStatusHint();
    } else if (m_state == RotateState::Ready && m_selectionConfirmed) {
        if (m_input.pivotPicked()) {
            m_input.setPivotPicked(false);
            m_phase = RotatePhase::PickingPivot;
            removeGizmo();
            updateStatusHint();
        } else {
            applySelectionConfirmed(false);
        }
    } else {
        restoreBase();
        clearTarget();
    }
}

void ToolRotate::keyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        handleEscapeKey();
        event->accept();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        if (m_state == RotateState::Ready && !m_selectionConfirmed)
            applySelectionConfirmed(true);
        event->accept();
    } else if (event->key() == Qt::Key_X) {
        if (!m_selectionConfirmed) toggleAnchor();
        event->accept();
    } else if (event->key() == Qt::Key_Shift) {
        toggleConstraintMode();
        event->accept();
    }
}

void ToolRotate::toggleConstraintMode()
{
    if (m_session.isConnected()) {
        m_constraintMode = (m_constraintMode == RotateConstraintMode::None) ? RotateConstraintMode::Baseline
                         : (m_constraintMode == RotateConstraintMode::Baseline) ? RotateConstraintMode::World
                         : RotateConstraintMode::None;
    } else {
        m_constraintMode = (m_constraintMode == RotateConstraintMode::None) ? RotateConstraintMode::World
                                                                            : RotateConstraintMode::None;
    }
    updateStatusHint();
    if (m_state == RotateState::Rotating) updateRotation(m_lastMousePos, false);
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
            bi->setToolLocked(inSel && m_selectionConfirmed);
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
        m_multi.setMarqueeSelected(false);
    } else {
        m_session.clear();
    }
    m_state = RotateState::Ready;
    m_phase = RotatePhase::Selecting;
    m_input.setPivotPicked(false);
    applySelectionConfirmed(false);
    syncSelectionVisual();
    if (m_multi.selection().size() == 1) {
        reportRotateAnchorState();
        reportStripTarget();
        buildGizmo();
        updateGizmo();
    }
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

    m_phase = RotatePhase::Selecting;
    m_input.setPivotPicked(false);
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
    m_input.setPivotPicked(false);
    syncSelectionVisual();
    applySelectionConfirmed(false);
    m_phase = RotatePhase::Selecting;
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
        m_dragCursorAngle0 = m_dragCursorAnglePrev = std::atan2(d.y, d.x);
        m_accumulatedAngleDeg = 0.0;
        m_dragAngle0 = 0.0;

        m_guidePoint = findClosestGuidePoint(m_paramDoc, m_multi.selection(), pos);

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
    m_dragCursorAngle0 = m_dragCursorAnglePrev = std::atan2(d.y, d.x);
    m_accumulatedAngleDeg = 0.0;
    m_dragAngle0 = currentAngleDeg();
}

void ToolRotate::updateRotation(const cad::geo::Vec2& pos, bool /*snap*/)
{
    if (m_state != RotateState::Rotating) return;
    m_lastMousePos = pos;

    auto step = computeDragStep(pos, m_session.pivot(), m_dragCursorAnglePrev);
    m_dragCursorAnglePrev = step.newCursorAngle;
    m_accumulatedAngleDeg += step.stepDeg;

    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
        double deltaDeg = computeMultiDeltaDeg(m_accumulatedAngleDeg, m_constraintMode);
        if (m_constraintMode == RotateConstraintMode::None) {
            m_aimSnap.checkGuideSnap(m_paramDoc, m_scene, m_multi.selection(),
                                     m_session.pivot(), m_guidePoint, currentZoom(), deltaDeg);
        }
        m_multi.applyModeValue(m_paramDoc, m_scene, m_session.pivot(), deltaDeg);
        updateGizmo();
        updateStatusHint();
        return;
    }

    DragSample sample{
        .isCopyGestureActive = (m_copyGesture && m_copyGesture->active()),
        .isConnected = m_session.isConnected(),
        .constraintMode = m_constraintMode,
        .dragAngle0 = m_dragAngle0,
        .accumulatedAngleDeg = m_accumulatedAngleDeg,
        .refWorldRad = m_session.refWorldRad(),
        .localDir = m_session.localDir(),
    };
    double target = computeDragTargetDeg(sample);

    if (m_constraintMode == RotateConstraintMode::None) checkEndpointAimSnap(target);

    applyAngleDeg(target);
    updateGizmo();
}

void ToolRotate::applyAngleDeg(double deg) {
    m_session.applyAngleDeg(m_paramDoc, m_scene, deg, m_copyGesture.get(), m_dragAngle0);
}
void ToolRotate::applyShadowAngleDeg(double deg) {
    m_session.applyShadowAngleDeg(m_paramDoc, deg, m_dragAngle0);
}
void ToolRotate::applyModeValue(double value) {
    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) {
        m_multi.applyModeValue(m_paramDoc, m_scene, m_session.pivot(), value);
        updateGizmo();
        updateStatusHint();
        return;
    }
    m_session.applyModeValue(m_paramDoc, m_scene, value, m_copyGesture.get());
}
double ToolRotate::segmentRadius() const { return m_session.segmentRadius(m_paramDoc); }
double ToolRotate::currentModeValue() const { return m_session.currentModeValue(m_paramDoc, m_copyGesture.get()); }

void ToolRotate::commitRotation() {
    if (m_state != RotateState::Rotating) return;
    commitCurrent();
    m_state = RotateState::Ready;
    applySelectionConfirmed(false);
    updateGizmo();
    updateStatusHint();
}

void ToolRotate::cancelRotation() {
    if (m_state != RotateState::Rotating) return;
    restoreBase();
    m_state = RotateState::Ready;
    clearAimCandidate();
    updateGizmo();
    updateStatusHint();
}

void ToolRotate::commitCurrent() {
    if (!m_paramDoc || !m_undoStack) return;
    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) m_multi.commit(m_paramDoc, m_undoStack);
    else m_session.commit(m_paramDoc, m_undoStack);
    updateGizmo();
}

void ToolRotate::restoreBase() {
    if (!m_paramDoc) return;
    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected())) m_multi.restoreBase(m_paramDoc, m_scene);
    else m_session.restoreBase(m_paramDoc, m_scene);
}

double ToolRotate::currentAngleDeg() const {
    if (isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected()))
        return m_multi.accumulatedAngleDeg();
    return m_session.currentAngleDeg(m_paramDoc, m_copyGesture.get());
}
double ToolRotate::baseAngleDeg() const {
    if (m_session.blockId().isNull() || !m_paramDoc) return 0.0;
    if (m_session.isConnected())
        return cad::geo::normalizeDeg180(cad::geo::radToDeg(m_session.refWorldRad()));
    double orig = originalWorldRotDeg();
    if (m_session.anchor().isEnd) orig += 180.0;
    return cad::geo::normalizeDeg180(orig);
}
bool ToolRotate::isAngleLocked() const { return m_session.isAngleLocked(m_copyGesture.get()); }

void ToolRotate::reportStripTarget() {
    if (m_session.blockId().isNull() || !m_paramDoc) { reportPinnedTarget(QUuid(), QUuid()); return; }
    const auto* blk = m_paramDoc->findBlock(m_session.blockId());
    if (!blk || blk->segments.empty()) { reportPinnedTarget(QUuid(), QUuid()); return; }
    reportPinnedTarget(m_session.blockId(), blk->segments.front().id);
}

void ToolRotate::reportRotateAnchorState() {
    if (!m_host) return;
    const bool active = !m_session.blockId().isNull() && m_state != RotateState::Idle;
    bool canToggle = active && m_state == RotateState::Ready;
    QString reason;
    if (canToggle && m_paramDoc) {
        if (const auto* blk = m_paramDoc->findBlock(m_session.blockId()); blk && !blk->segments.empty()) {
            const auto& seg = blk->segments.front();
            if (m_session.attachmentAtPoint(m_paramDoc, seg.startPointId) || m_session.attachmentAtPoint(m_paramDoc, seg.endPointId)) {
                canToggle = false;
                reason = buildAnchorLockedReason();
            }
        }
    }
    m_host->setRotateAnchorState(active, m_session.anchor().isEnd, canToggle, reason, active ? baseAngleDeg() : 0.0);
}
void ToolRotate::onReverseRequested(const QUuid&, const QUuid&) { toggleAnchor(); }

void ToolRotate::updateStatusHint() {
    RotateHintSnapshot snap{
        .isCopyGestureActive = (m_copyGesture && m_copyGesture->active()),
        .copyRelativeAngle = (m_copyGesture ? m_copyGesture->currentRelativeAngle() : 0.0),
        .state = m_state,
        .constraintMode = m_constraintMode,
        .selectionConfirmed = m_selectionConfirmed,
        .hasSingleBlock = (!m_session.blockId().isNull() && m_multi.selection().size() <= 1),
        .selectionSize = static_cast<int>(m_multi.selection().size()),
        .anchorTag = m_session.anchorTag(m_paramDoc),
        .pivotPicked = m_input.pivotPicked(),
        .isAngleLocked = isAngleLocked(),
        .baseAngleDeg = baseAngleDeg(),
        .currentAngleDeg = currentAngleDeg(),
    };
    reportHintOverride(buildStatusHint(snap));
}

double ToolRotate::currentZoom() const {
    return m_scene ? m_scene->safeZoom() : 1.0;
}

cad::geo::Vec2 ToolRotate::endpointAtAngle(double angleDeg) const {
    return RotateAimSnap::endpointAtAngle(m_paramDoc, m_session.blockId(), m_session.pivot(),
        m_session.refWorldRad(), m_session.isConnected(), m_copyGesture.get(), angleDeg);
}
void ToolRotate::checkEndpointAimSnap(double& angleDeg) {
    m_aimSnap.checkSnap(m_paramDoc, m_scene, m_session.blockId(), m_session.pivot(),
        m_session.refWorldRad(), m_session.isConnected(), currentZoom(), m_copyGesture.get(), angleDeg);
}
void ToolRotate::clearAimCandidate() { m_aimSnap.clear(); }

GizmoPoseInput ToolRotate::makeGizmoInput(bool isRotating) const
{
    return GizmoPoseInput{
        .isMultiOrMarquee = isMultiSelect() || (m_multi.isMarqueeSelected() && !m_session.isConnected()),
        .isCopyGestureActive = (m_copyGesture && m_copyGesture->active()),
        .isConnected = m_session.isConnected(),
        .isRotating = isRotating,
        .originalWorldRotRad = cad::geo::degToRad(originalWorldRotDeg()),
        .isAnchorEnd = m_session.anchor().isEnd,
        .refWorldRad = m_session.refWorldRad(),
        .localDir = m_session.localDir(),
        .baseAngleDeg = m_session.base().baseAngle,
        .currentAngleDeg = currentAngleDeg(),
        .dragAngle0 = m_dragAngle0,
        .dragCursorAngle0 = m_dragCursorAngle0,
        .accumulatedAngleDeg = m_accumulatedAngleDeg,
        .copyRelativeAngle = (m_copyGesture ? m_copyGesture->currentRelativeAngle() : 0.0),
    };
}

void ToolRotate::buildGizmo() {
    if (!m_gizmo) return;
    auto pose = computeGizmoPose(makeGizmoInput(false));
    m_gizmo->build(m_session.pivot(), pose.refBaseRad, pose.currentPoseRad, currentZoom());
}

double ToolRotate::originalWorldRotDeg() const { return m_session.originalWorldRotDeg(m_paramDoc); }

void ToolRotate::applySelectionConfirmed(bool confirmed) {
    if (m_selectionConfirmed == confirmed) return;
    m_selectionConfirmed = confirmed;
    m_input.resetPress();
    m_input.setPivotPicked(false);
    if (!confirmed) {
        m_input.hideHoverSnap();
        m_phase = RotatePhase::Selecting;
    } else {
        m_phase = RotatePhase::PickingPivot;
    }
    removeGizmo();
    syncSelectionVisual();
    updateGizmo();
    updateStatusHint();
}

bool ToolRotate::gizmoConfirmed() const {
    return m_selectionConfirmed || (m_gizmo && m_gizmo->confirmed());
}

void ToolRotate::updateGizmo() {
    if (!m_gizmo) return;
    const bool shouldShow = (m_selectionConfirmed && m_input.pivotPicked()) || (m_copyGesture && m_copyGesture->active());
    m_gizmo->setConfirmed(shouldShow);
    auto pose = computeGizmoPose(makeGizmoInput(m_state == RotateState::Rotating));
    m_gizmo->update(currentZoom(), pose.refBaseRad, pose.currentPoseRad, pose.deltaDeg, pose.badgeText);
    updateStatusHint();
}

void ToolRotate::removeGizmo() { if (m_gizmo) m_gizmo->remove(); }

QUuid ToolRotate::hitBlock(const cad::geo::Vec2& worldPos) const {
    if (!m_scene || !m_paramDoc) return QUuid();
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const auto hits = blockHitsAtScene(*m_scene, *m_paramDoc, scenePt);
    return hits.empty() ? QUuid() : hits.front().blockId;
}

} // namespace cad::tools
