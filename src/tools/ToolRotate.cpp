#include "ToolRotate.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QUndoStack>
#include <QPen>
#include <QPainterPath>
#include <QLineEdit>
#include <QWidget>

#include <cmath>
#include <limits>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Attachment.h"
#include "parametric/Duplicate.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "AngleHud.h"
#include "LinePropertyDialog.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockCommands.h"

namespace cad::tools {

namespace {

/// Format degrees for the HUD/label: integers without a trailing ".0".
QString formatDeg(double deg)
{
    QString s = QString::number(deg, 'f', 1);
    if (s.endsWith(QLatin1String(".0")))
        s.chop(2);
    return s;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void ToolRotate::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene = &scene;
    m_paramDoc = paramDoc;
    m_state = RotateState::Idle;
}

void ToolRotate::deactivate()
{
    // Mid-copy deactivation (tool switch): drop the preview clone, the
    // original block was never touched.
    if (m_copyMode) {
        removeCopyPreview();
        finishRotateCopy();
    }
    // A follower link released by an in-flight rotation is restored — tool
    // switch abandons the gesture (nothing was committed).
    if (m_releaseAttHeld && !m_releaseAttId.isNull() && m_paramDoc) {
        m_paramDoc->addAttachment(m_releaseAttBackup);
        m_releaseAttId = QUuid();
        m_releaseAttHeld = false;
    }
    removeGizmo();
    if (m_hud) { m_hud->hide(); delete m_hud; m_hud = nullptr; }
    if (m_aimRing && m_scene) { m_scene->removeItem(m_aimRing); delete m_aimRing; m_aimRing = nullptr; }
    m_blockId = QUuid();
    m_attId = QUuid();
    m_connected = false;
    m_anchorPointId = QUuid();
    m_state = RotateState::Idle;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mouse input
// ═══════════════════════════════════════════════════════════════════════════════

void ToolRotate::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    const cad::geo::Vec2 pos(event->scenePos().x(), event->scenePos().y());

    if (event->button() == Qt::RightButton) {
        // Right-click: abort any in-flight drag, then deselect.
        if (m_state == RotateState::Rotating) cancelRotation();
        clearTarget();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    switch (m_state) {
    case RotateState::Idle: {
        const QUuid id = hitBlock(pos);
        if (!id.isNull()) {
            selectTarget(id);
            // Ctrl+press selects AND starts a rotate-copy in one gesture
            // (Ctrl = 复制意图, same language as ToolSelect).
            if (event->modifiers() & Qt::ControlModifier)
                beginRotateCopy(pos);
        }
        break;
    }
    case RotateState::Ready: {
        const QUuid id = hitBlock(pos);
        const bool ctrl = (event->modifiers() & Qt::ControlModifier);
        if (ctrl) {
            // Ctrl+press: copy-rotate. Empty space does nothing (no target
            // to copy); a hit on ANOTHER block switches the target first.
            if (id.isNull()) return;
            if (id != m_blockId) {
                commitCurrent();
                selectTarget(id);
            }
            beginRotateCopy(pos);
            break;
        }
        // A click on the target's OTHER endpoint switches the anchor
        // (锚心切换: X 键或直接点击起点/终点); the current-anchor
        // endpoint and the line body fall through to rotation. The endpoint
        // test is DISTANCE-based (anchorPointAt) — BlockItem::shape() does
        // not reliably cover the segment ends (Qt path-merge hole), so an
        // items()-based hitBlock would swallow the click as a rotation start.
        {
            const QUuid hit = anchorPointAt(pos);
            if (!hit.isNull() && hit != m_anchorPointId) {
                commitCurrent();
                m_anchorIsEnd = (hit
                    == m_paramDoc->findBlock(m_blockId)->segments.front().endPointId);
                rebuildAnchorState();
                removeGizmo();
                buildGizmo();
                updateGizmo();
                showHud();
                if (m_scene) m_scene->refreshAllBlockItems();
                break;
            }
        }
        if (!id.isNull() && id != m_blockId) {
            commitCurrent();      // commit pending edits on the current target…
            selectTarget(id);     // …then switch.
        } else {
            beginRotation(pos);
        }
        break;
    }
    case RotateState::Rotating:
        break;  // already rotating.
    }
}

void ToolRotate::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (m_state != RotateState::Rotating) return;
    const cad::geo::Vec2 pos(event->scenePos().x(), event->scenePos().y());
    const bool snap = (event->modifiers() & Qt::ShiftModifier);
    updateRotation(pos, snap);
}

void ToolRotate::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    if (m_state != RotateState::Rotating) return;
    if (m_copyMode) commitRotateCopy();
    else            commitRotation();
}

void ToolRotate::mouseDoubleClick(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;

    // A double-click is preceded by two press/release pairs; the second press
    // may have started a rotation — cancel it and drop the target before
    // opening the property dialog.
    if (m_state == RotateState::Rotating) cancelRotation();
    clearTarget();

    const cad::geo::Vec2 clickPos(event->scenePos().x(), event->scenePos().y());
    const QUuid blockId = hitBlock(clickPos);
    if (blockId.isNull()) return;
    cad::param::Block* block = m_paramDoc->findBlock(blockId);
    if (!block || block->segments.empty()) return;

    const double tolerance = 8.0 / currentZoom();
    QUuid bestSegId;
    double bestDist = std::numeric_limits<double>::max();
    for (const auto& seg : block->segments) {
        const auto* sp = block->findPoint(seg.startPointId);
        const auto* ep = block->findPoint(seg.endPointId);
        if (!sp || !ep || !sp->resolved || !ep->resolved) continue;
        const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
        const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
        const double d = cad::geo::Vec2::distanceToSegment(clickPos, w1, w2);
        if (d < bestDist) { bestDist = d; bestSegId = seg.id; }
    }
    if (bestSegId.isNull() || bestDist > tolerance) return;

    QWidget* parentWidget = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    auto* dlg = new LinePropertyDialog(blockId, bestSegId, m_paramDoc,
                                       m_scene, parentWidget);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void ToolRotate::keyPress(QKeyEvent* event)
{
    // Fallback path: the key reached the view instead of the HUD widget.
    if (event->key() == Qt::Key_Escape) {
        onHudCancel();
        event->accept();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onHudCommit();
        event->accept();
    } else if (event->key() == Qt::Key_X) {
        // X: toggle the anchor between the start and end points (锚心切换).
        toggleAnchor();
        event->accept();
    }
}
// ═══════════════════════════════════════════════════════════════════════════════
// Target selection
// ═══════════════════════════════════════════════════════════════════════════════

cad::param::Attachment* ToolRotate::followerAttachment()
{
    if (!m_paramDoc || m_blockId.isNull()) return nullptr;
    auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
    for (auto& a : atts) {
        if (a.fromBlockId == m_blockId && !a.isPin)
            return &a;
    }
    return nullptr;
}

cad::param::Attachment* ToolRotate::attachmentAtPoint(const QUuid& pointId)
{
    if (!m_paramDoc || m_blockId.isNull() || pointId.isNull()) return nullptr;
    auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
    for (auto& a : atts) {
        if (a.fromBlockId == m_blockId && a.fromPointId == pointId && !a.isPin)
            return &a;
    }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Anchor point (锚心: 起点 ↔ 终点)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolRotate::toggleAnchor()
{
    if (m_state != RotateState::Ready || !m_paramDoc) return;
    const cad::param::Block* blk = m_paramDoc->findBlock(m_blockId);
    if (!blk || blk->segments.empty()) return;

    commitCurrent();                 // commit any uncommitted HUD edit first
    m_anchorIsEnd = !m_anchorIsEnd;
    rebuildAnchorState();

    removeGizmo();
    buildGizmo();
    updateGizmo();
    showHud();                       // HUD follows the new pivot
    if (m_scene) m_scene->refreshAllBlockItems();
}

void ToolRotate::rebuildAnchorState()
{
    if (!m_paramDoc) return;
    cad::param::Block* blk = m_paramDoc->findBlock(m_blockId);
    if (!blk || blk->segments.empty()) { clearTarget(); return; }

    const cad::param::Segment& seg = blk->segments.front();
    m_anchorPointId = m_anchorIsEnd ? seg.endPointId : seg.startPointId;
    const cad::param::ParamPoint* ap = blk->findPoint(m_anchorPointId);
    if (!ap || !ap->resolved) { clearTarget(); return; }

    cad::param::Attachment* att = attachmentAtPoint(m_anchorPointId);
    if (att) {
        // ── Connected mode: the anchor IS the attachment point, so the
        // rotation edits the follower angle. ──
        m_connected = true;
        m_attId = att->id;
        m_pivot = blk->worldPos(att->fromPointId);
        const cad::param::Block* leader = m_paramDoc->findBlock(att->toBlockId);
        m_refWorldRad = leader
            ? leader->transform.rotation
                  + leader->exitDirectionAtPoint(att->toPointId, att->toSegmentId)
            : 0.0;
        m_baseAngle = att->followerAngle;
        m_baseFormula = att->followerAngleFormula;
        m_rotationMode = att->rotationMode;
        m_baseArcLength = att->arcLength;
        m_baseArcFormula = att->arcLengthFormula;
    } else {
        // ── Free mode: rotate rigidly about the anchor point. ──
        m_connected = false;
        m_attId = QUuid();
        m_anchorLocal = ap->resolvedPos;
        m_pivot = blk->worldPos(m_anchorPointId);
        m_localDir = blk->directionAtPoint(m_anchorPointId);
        m_refWorldRad = 0.0;
        m_baseTf = blk->transform;
        m_baseEndTargetBlock = blk->endTargetBlockId;
        m_baseEndTargetPoint = blk->endTargetPointId;
    }

    // The pivot moved OFF the follower link (anchor != attachment point): the
    // rotation will RELEASE the link (旋转 = 放弃跟随). Snapshot it now so the
    // release can happen at drag start and be restored by Esc / undo.
    if (!m_releaseAttHeld) {
        m_releaseAttId = QUuid();
        if (!m_connected) {
            if (auto* fa = followerAttachment()) {
                m_releaseAttId = fa->id;
                m_releaseAttBackup = *fa;
            }
        }
    }
}

QUuid ToolRotate::anchorPointAt(const cad::geo::Vec2& worldPos) const
{
    if (!m_paramDoc || m_blockId.isNull()) return QUuid();
    const cad::param::Block* blk = m_paramDoc->findBlock(m_blockId);
    if (!blk || blk->segments.empty()) return QUuid();

    const double zoom = currentZoom();
    const double tol = 8.0 / zoom;   // pixel threshold
    for (const QUuid& pid : {blk->segments.front().startPointId,
                             blk->segments.front().endPointId}) {
        const cad::param::ParamPoint* p = blk->findPoint(pid);
        if (p && p->resolved && blk->worldPos(pid).distanceTo(worldPos) <= tol)
            return pid;
    }
    return QUuid();
}

void ToolRotate::releaseFollowerIfAnchorMoved()
{
    if (!m_paramDoc || m_releaseAttId.isNull() || m_releaseAttHeld) return;
    auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
    for (auto& a : atts) {
        if (a.id == m_releaseAttId) { m_releaseAttBackup = a; break; }
    }
    m_paramDoc->removeAttachment(m_releaseAttId);
    m_releaseAttHeld = true;
    m_connected = false;   // the line is now independent
    m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();
}

void ToolRotate::selectTarget(const QUuid& blockId)
{
    if (!m_paramDoc || !m_scene) return;
    cad::param::Block* blk = m_paramDoc->findBlock(blockId);
    if (!blk || blk->segments.empty()) return;
    if (blk->isBridge) return;   // bridge geometry is passive — refuse.

    // Clean up any previous target's chrome.
    if (m_state != RotateState::Idle) {
        removeGizmo();
        hideHud();
    }

    m_blockId = blockId;
    m_anchorIsEnd = false;        // default anchor: the START point
    m_releaseAttId = QUuid();
    m_releaseAttHeld = false;
    rebuildAnchorState();
    if (m_blockId.isNull()) return;   // rebuild cleared an invalid target

    m_state = RotateState::Ready;
    buildGizmo();
    updateGizmo();
    showHud();
    m_scene->refreshAllBlockItems();
}

void ToolRotate::clearTarget()
{
    removeGizmo();
    hideHud();
    clearAimCandidate();
    m_blockId = QUuid();
    m_attId = QUuid();
    m_connected = false;
    m_anchorPointId = QUuid();
    m_releaseAttId = QUuid();
    m_releaseAttHeld = false;
    m_state = RotateState::Idle;
    if (m_scene) m_scene->refreshAllBlockItems();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Rotate-copy gesture (Ctrl+drag 旋转复制)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolRotate::beginRotateCopy(const cad::geo::Vec2& pos)
{
    if (m_state != RotateState::Ready || !m_paramDoc) return;

    const cad::param::Block* orig = m_paramDoc->findBlock(m_blockId);
    if (!orig || orig->segments.empty()) return;

    // 挂接点 = 当前锚心点（起点或终点，X/点击可切换）。
    // 克隆挂回原块，跟随角度 = 相对原线当前朝向的角度。
    m_pivotPointId = m_anchorPointId;
    // 参考线段 = pivot 点的出口线段（跟随角度 0° = 沿原线继续直行）。
    m_leaderSegmentId = orig->exitSegmentAtPoint(m_pivotPointId);
    if (m_leaderSegmentId.isNull() && !orig->segments.empty())
        m_leaderSegmentId = orig->segments.front().id;

    // Clone the single target block: outside attachments/groups are dropped,
    // so the clone starts as an independent copy overlapping the original.
    m_copyResult = cad::param::duplicateBlocks(*m_paramDoc, {m_blockId});
    if (m_copyResult.isEmpty()) return;
    const cad::param::Block& clone = m_copyResult.blocks.front();
    m_cloneBlockId = clone.id;

    // Locate the clone-side counterpart of the pivot point (duplicateBlocks
    // preserves point order — the deep copy keeps every parametric field).
    m_clonePivotPointId = {};
    for (size_t i = 0; i < orig->points.size(); ++i) {
        if (orig->points[i].id == m_pivotPointId) {
            if (i < clone.points.size())
                m_clonePivotPointId = clone.points[i].id;
            break;
        }
    }
    if (m_clonePivotPointId.isNull()) { m_copyResult = {}; return; }

    // The clone must NOT inherit the original's endpoint-aim constraint — the
    // resolver would pull the copy straight back to the target point on every
    // frame (旋转复制 = 副本相对原线自由转动, 用户拍板).
    m_copyResult.blocks.front().endTargetBlockId = QUuid();
    m_copyResult.blocks.front().endTargetPointId = QUuid();
    m_copyResult.blocks.front().endTargetOffset = 0.0;
    m_copyResult.blocks.front().endTargetOffsetFormula.clear();

    // Live preview (no undo): linked vars → clone → clone→original attachment.
    // Follower angle 180° puts the clone EXACTLY on the original: the
    // leader's exit direction at the anchor point extends BACKWARD for the
    // START point (模型语义: 起点处“继续直行”= 反向) and FORWARD for the END
    // point — 180° folds both back onto the original's own direction. The
    // drag then adds a RELATIVE angle on top (0 = overlap).
    for (const auto& lv : m_copyResult.newLinked)
        m_paramDoc->addLinked(lv);
    m_paramDoc->addBlock(clone);
    cad::param::Attachment cloneAtt;
    cloneAtt.fromBlockId = m_cloneBlockId;
    cloneAtt.fromPointId = m_clonePivotPointId;
    cloneAtt.toBlockId = m_blockId;
    cloneAtt.toPointId = m_pivotPointId;
    cloneAtt.toSegmentId = m_leaderSegmentId;
    cloneAtt.followerAngle = 180.0;   // relative 0° = overlap the original
    cloneAtt.rotationMode = cad::param::RotationMode::Angle;
    m_cloneAttId = cloneAtt.id;
    if (!m_paramDoc->addAttachment(cloneAtt)) {
        // Rejected (should not happen for same-layer original→clone): drop.
        removeCopyPreview();
        m_copyResult = {};
        m_cloneBlockId = QUuid();
        m_cloneAttId = QUuid();
        return;
    }

    // Drag base: the clone starts at 0° relative to the ORIGINAL's current
    // world direction (captured now, so later original rotations keep the
    // copy's relative angle). m_refWorldRad is left untouched — the copy
    // branches (endpointAtAngle / gizmo) read m_copyRefWorldRad directly.
    // NOTE: for the end anchor the DISPLAY angle is line direction + 180°,
    // but the copy reference must be the raw line direction itself.
    double refLineDeg = currentAngleDeg();
    if (!m_connected && m_anchorIsEnd) refLineDeg -= 180.0;
    m_copyRefWorldRad = m_refWorldRad + refLineDeg * M_PI / 180.0;
    const cad::geo::Vec2 d = pos - m_pivot;
    m_dragCursorAngle0 = std::atan2(d.y, d.x);
    m_dragAngle0 = 0.0;   // relative angle starts at 0 (clone on original)

    m_copyMode = true;
    m_state = RotateState::Rotating;
    if (m_hud) m_hud->setMode(cad::param::RotationMode::Angle);
    refreshHudText();
    if (m_scene) m_scene->refreshAllBlockItems();
}

void ToolRotate::commitRotateCopy()
{
    if (m_state != RotateState::Rotating || !m_copyMode) return;

    // Read the final clone-attachment state (angle and optional formula).
    double finalAngle = 0.0;
    QString finalFormula;
    const auto& atts = m_paramDoc->attachments();
    for (const auto& a : atts) {
        if (a.id == m_cloneAttId) {
            finalAngle = a.followerAngle;
            finalFormula = a.followerAngleFormula;
            break;
        }
    }
    // Normalise to (-180, 180] so a full 360° swing back counts as "no
    // rotation" (转回原位 = 不复制). Relative 0° = stored 180°.
    double n = std::fmod(finalAngle - 180.0, 360.0);
    if (n < 0.0) n += 360.0;
    if (n > 180.0) n -= 360.0;
    const bool zeroAngle = std::abs(n) < 1e-6 && finalFormula.isEmpty();

    removeCopyPreview();   // drop the live clone (original untouched)

    if (zeroAngle || !m_undoStack) {
        finishRotateCopy();   // discard: back to Ready, nothing created
        updateGizmo();
        refreshHudText();
        return;
    }

    // Restore-then-replay: ONE undo step re-creates clone + attachment with
    // the final relative angle.
    m_undoStack->push(new cad::cmd::RotateCopyCommand(
        m_paramDoc, std::move(m_copyResult), m_blockId, m_pivotPointId,
        m_clonePivotPointId, m_leaderSegmentId, finalAngle, finalFormula));
    finishRotateCopy();
    updateGizmo();
    refreshHudText();
}

void ToolRotate::cancelRotateCopy()
{
    removeCopyPreview();
    finishRotateCopy();
    clearAimCandidate();
    updateGizmo();
    refreshHudText();
}

void ToolRotate::removeCopyPreview()
{
    if (!m_paramDoc) return;
    if (!m_cloneAttId.isNull())
        m_paramDoc->removeAttachment(m_cloneAttId);
    if (!m_cloneBlockId.isNull())
        m_paramDoc->removeBlock(m_cloneBlockId);
    for (const auto& lv : m_copyResult.newLinked)
        m_paramDoc->removeLinked(lv.id);
    if (m_scene) m_scene->refreshAllBlockItems();
}

void ToolRotate::finishRotateCopy()
{
    m_copyResult = {};
    m_cloneBlockId = QUuid();
    m_cloneAttId = QUuid();
    m_clonePivotPointId = QUuid();
    m_copyMode = false;
    m_state = RotateState::Ready;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Rotation gesture
// ═══════════════════════════════════════════════════════════════════════════════

void ToolRotate::beginRotation(const cad::geo::Vec2& pos)
{
    if (m_state != RotateState::Ready) return;
    // 旋转 = 放弃跟随 (用户拍板): the pivot was moved OFF the attachment
    // point (anchor switched to the far endpoint), so the rotation detaches
    // the follower link — the resolver would otherwise yank the line back to
    // its leader on every frame. The link is snapshotted; Esc / undo restore
    // it (undo 一步恢复挂接).
    releaseFollowerIfAnchorMoved();
    // Endpoint-aim constraint (终点指向): free-hand rotation RELEASES the aim
    // — the resolver would otherwise pull the direction straight back to the
    // target point on every frame (用户拍板: 旋转 = 放弃终点指向).
    if (m_paramDoc) {
        if (auto* blk = m_paramDoc->findBlock(m_blockId);
            blk && !blk->endTargetBlockId.isNull()) {
            blk->endTargetBlockId = QUuid();
            blk->endTargetPointId = QUuid();
            blk->endTargetOffset = 0.0;
            blk->endTargetOffsetFormula.clear();
            m_paramDoc->resolveAll();
            if (m_scene) m_scene->refreshAllBlockItems();
        }
    }
    // Formula-driven follower angle: free-hand rotation is ALLOWED and
    // means "I no longer want this formula to drive the angle" (用户拍板:
    // 旋转 = 放弃公式约束). The formula's current value is baked into a
    // plain number (geometry unchanged), then rotation proceeds freely.
    if (isAngleLocked() && m_paramDoc) {
        auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
        for (auto& a : atts) {
            if (a.id != m_attId) continue;
            const double cur = currentAngleDeg();
            if (m_rotationMode == cad::param::RotationMode::ArcLength) {
                // Bake the CURRENT arc value (formula evaluated) — never
                // back-derive from the normalized display angle, which would
                // collapse multi-turn arcs to 0 (用户回归 2026-08).
                double arcMm = a.arcLength;
                if (!a.arcLengthFormula.isEmpty()) {
                    auto r = cad::param::ConditionEngine::evaluate(
                        a.arcLengthFormula, m_paramDoc->parameters(), {});
                    if (r.ok) arcMm = geo::Units::cmToMm(r.value);
                }
                a.arcLength = arcMm;
                a.arcLengthFormula.clear();
            } else {
                a.followerAngle = cur;
                a.followerAngleFormula.clear();
            }
            break;
        }
        // NOTE: m_baseFormula/m_baseArcFormula stay UNTOUCHED — they are the
        // undo-restore target (撤销恢复公式), and the commit diff needs the
        // formula-vs-empty change to be visible.
        m_paramDoc->resolveAll();
        if (m_scene) m_scene->refreshAllBlockItems();
    }
    m_state = RotateState::Rotating;
    const cad::geo::Vec2 d = pos - m_pivot;
    m_dragCursorAngle0 = std::atan2(d.y, d.x);
    m_dragAngle0 = currentAngleDeg();
}

void ToolRotate::updateRotation(const cad::geo::Vec2& pos, bool snap)
{
    if (m_state != RotateState::Rotating) return;
    const cad::geo::Vec2 d = pos - m_pivot;
    const double theta = std::atan2(d.y, d.x);
    double delta = theta - m_dragCursorAngle0;
    // Normalise to (-π, π] so crossing the −X axis never causes a jump.
    delta = cad::geo::normalizeRad(delta);

    double target = m_dragAngle0 + cad::geo::radToDeg(delta);
    if (snap) target = std::round(target / 15.0) * 15.0;

    // Endpoint aim snap: if Shift is NOT held, try to align the rotating
    // endpoint's direction with a nearby point.
    if (!snap) checkEndpointAimSnap(target);

    applyAngleDeg(target);
    updateGizmo();
    refreshHudText();
}

void ToolRotate::applyAngleDeg(double deg)
{
    if (!m_paramDoc) return;
    // Per-frame hot path: narrow the resolve scope to the rotated block's
    // layer group (the aux layer stays frozen unless it is the one rotated).
    if (const auto* scopeBlk = m_paramDoc->findBlock(m_blockId))
        m_paramDoc->invalidateLayer(scopeBlk->layer);
    if (m_copyMode) {
        // Rotate-copy: deg is the angle RELATIVE to the original; the stored
        // follower angle adds the 180° start-point exit-direction offset
        // (relative 0° = overlap). Formula cleared by direct manipulation.
        auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
        for (auto& a : atts) {
            if (a.id == m_cloneAttId) {
                a.followerAngle = deg + 180.0;
                a.followerAngleFormula.clear();
                break;
            }
        }
        m_paramDoc->resolveAll();
        if (m_scene) m_scene->refreshAllBlockItems();
        return;
    }
    if (m_connected) {
        auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
        for (auto& a : atts) {
            if (a.id == m_attId) {
                if (m_rotationMode == cad::param::RotationMode::ArcLength) {
                // Convert angle → arc length and store in arc fields. Arc is
                // measured from the REVERSE direction: 弧长 0 = 角度 180°.
                const double radius = segmentRadius();
                double degFromReverse = std::fmod(deg - 180.0, 360.0);
                if (degFromReverse < 0.0) degFromReverse += 360.0;
                a.arcLength = degFromReverse * M_PI / 180.0 * radius;
                a.arcLengthFormula.clear();
                a.rotationMode = cad::param::RotationMode::ArcLength;
                } else {
                    a.followerAngle = deg;
                    a.followerAngleFormula.clear();   // direct manipulation overrides formula
                }
                break;
            }
        }
    } else {
        cad::param::Block* blk = m_paramDoc->findBlock(m_blockId);
        if (!blk) return;
        // Display angle is measured FROM the anchor toward the line
        // (终点锚心 = 线方向+180°), so the stored rotation drops the 180°
        // end-anchor offset: rotation = deg − anchorOffset − localDir;
        // keep the ANCHOR point pinned to the pivot (起点或终点锚心).
        const double anchorOffsetRad = m_anchorIsEnd ? M_PI : 0.0;
        const double newRot = deg * M_PI / 180.0 - anchorOffsetRad - m_localDir;
        blk->transform.rotation = newRot;
        blk->transform.origin = m_pivot - m_anchorLocal.rotated(newRot);
    }
    m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();
}

void ToolRotate::applyModeValue(double value)
{
    if (m_rotationMode == cad::param::RotationMode::ArcLength && m_connected) {
        // Arc-length input stores the arc DIRECTLY (never round-trips through
        // the normalized angle, which would collapse multi-turn arcs to 0;
        // 用户回归 2026-08). Formula is cleared by direct manipulation.
        auto& atts = const_cast<std::vector<cad::param::Attachment>&>(
            m_paramDoc->attachments());
        for (auto& a : atts) {
            if (a.id != m_attId) continue;
            a.arcLength = geo::Units::cmToMm(value);
            a.arcLengthFormula.clear();
            break;
        }
        m_paramDoc->resolveAll();
        if (m_scene) m_scene->refreshAllBlockItems();
    } else {
        applyAngleDeg(value);
    }
}

double ToolRotate::segmentRadius() const
{
    if (!m_paramDoc || !m_connected) return 0.0;
    const cad::param::Block* blk = m_paramDoc->findBlock(m_blockId);
    if (!blk) return 0.0;
    const auto& atts = m_paramDoc->attachments();
    for (const auto& a : atts) {
        if (a.id == m_attId)
            return blk->segmentLengthAtPoint(a.fromPointId);
    }
    return 0.0;
}

double ToolRotate::currentModeValue() const
{
    if (m_rotationMode == cad::param::RotationMode::ArcLength && m_connected) {
        // Read the arc directly — never back-derive it from the NORMALIZED
        // display angle (multi-turn arcs would collapse to 0; 用户回归 2026-08).
        const auto& atts = m_paramDoc ? m_paramDoc->attachments()
                                      : std::vector<cad::param::Attachment>{};
        for (const auto& a : atts) {
            if (a.id != m_attId) continue;
            double arcMm = a.arcLength;
            if (!a.arcLengthFormula.isEmpty()) {
                auto r = cad::param::ConditionEngine::evaluate(
                    a.arcLengthFormula, m_paramDoc->parameters(), {});
                if (r.ok) arcMm = geo::Units::cmToMm(r.value);
            }
            return geo::Units::mmToCm(arcMm);
        }
        return 0.0;
    }
    return currentAngleDeg();
}

void ToolRotate::commitRotation()
{
    if (m_state != RotateState::Rotating) return;
    m_state = RotateState::Ready;
    commitCurrent();

    // 终点方向吸附只是旋转过程中的临时对齐 (角度吸附 + 高亮环), 绝不落成
    // endTarget 持久约束 —— 用户继续旋转说明不想锁定, checkEndpointAimSnap
    // 每帧重评, 一旦偏离容差吸附即解除. 需要“持续指向某点”时改用属性
    // 对话框的终点指向开关.
    clearAimCandidate();
}

void ToolRotate::cancelRotation()
{
    if (m_state != RotateState::Rotating) return;
    m_state = RotateState::Ready;
    restoreBase();
    clearAimCandidate();
    updateGizmo();
    refreshHudText();
}

void ToolRotate::commitCurrent()
{
    if (!m_paramDoc || !m_undoStack) return;

    if (m_connected) {
        auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
        cad::param::Attachment* att = nullptr;
        for (auto& a : atts) { if (a.id == m_attId) { att = &a; break; } }
        if (!att) return;

        // Snapshot current state.
        const double curAngle = att->followerAngle;
        const QString curFormula = att->followerAngleFormula;
        const auto curMode = att->rotationMode;
        const double curArc = att->arcLength;
        const QString curArcFormula = att->arcLengthFormula;

        const bool changed = std::abs(curAngle - m_baseAngle) > 1e-9
                          || curFormula != m_baseFormula
                          || curMode != m_rotationMode
                          || std::abs(curArc - m_baseArcLength) > 1e-6
                          || curArcFormula != m_baseArcFormula;
        if (!changed) { updateGizmo(); refreshHudText(); return; }

        // Restore-then-replay: put the base state back, then push a command
        // whose redo() re-applies the current state (single undo step).
        att->followerAngle = m_baseAngle;
        att->followerAngleFormula = m_baseFormula;
        att->rotationMode = m_rotationMode;
        att->arcLength = m_baseArcLength;
        att->arcLengthFormula = m_baseArcFormula;
        m_undoStack->push(new cad::cmd::SetFollowerAngleCommand(
            m_paramDoc, m_attId, curAngle, curFormula,
            curMode, curArc, curArcFormula));
        m_baseAngle = curAngle;
        m_baseFormula = curFormula;
        m_rotationMode = curMode;
        m_baseArcLength = curArc;
        m_baseArcFormula = curArcFormula;
    } else {
        cad::param::Block* blk = m_paramDoc->findBlock(m_blockId);
        if (!blk) return;

        const cad::param::Transform2D curTf = blk->transform;
        const QUuid curEndBlock = blk->endTargetBlockId;
        const QUuid curEndPoint = blk->endTargetPointId;
        const bool changed = std::abs(curTf.rotation - m_baseTf.rotation) > 1e-9
                          || curTf.origin.distanceTo(m_baseTf.origin) > 1e-6
                          || curEndBlock != m_baseEndTargetBlock
                          || curEndPoint != m_baseEndTargetPoint
                          || m_releaseAttHeld;
        if (!changed) { updateGizmo(); refreshHudText(); return; }

        blk->transform = m_baseTf;
        blk->endTargetBlockId = m_baseEndTargetBlock;
        blk->endTargetPointId = m_baseEndTargetPoint;
        // A released follower link is restored here so the command's redo()
        // can re-release it — ONE undo step covers 解挂接 + 旋转 together.
        if (m_releaseAttHeld && !m_releaseAttId.isNull())
            m_paramDoc->addAttachment(m_releaseAttBackup);
        m_undoStack->push(new cad::cmd::RotateBlockCommand(
            m_paramDoc, m_blockId, m_baseTf, curTf,
            m_baseEndTargetBlock, m_baseEndTargetPoint,
            curEndBlock, curEndPoint,
            m_releaseAttHeld ? m_releaseAttId : QUuid(),
            m_releaseAttBackup));
        m_baseTf = curTf;
        m_baseEndTargetBlock = curEndBlock;
        m_baseEndTargetPoint = curEndPoint;
        m_releaseAttId = QUuid();
        m_releaseAttHeld = false;
    }

    updateGizmo();
    refreshHudText();
}

void ToolRotate::restoreBase()
{
    if (!m_paramDoc) return;
    if (m_connected) {
        auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
        for (auto& a : atts) {
            if (a.id == m_attId) {
                a.followerAngle = m_baseAngle;
                a.followerAngleFormula = m_baseFormula;
                a.rotationMode = m_rotationMode;
                a.arcLength = m_baseArcLength;
                a.arcLengthFormula = m_baseArcFormula;
                break;
            }
        }
    } else {
        if (auto* blk = m_paramDoc->findBlock(m_blockId)) {
            blk->transform = m_baseTf;
            // The aim constraint cleared by beginRotation is restored too.
            blk->endTargetBlockId = m_baseEndTargetBlock;
            blk->endTargetPointId = m_baseEndTargetPoint;
        }
    }
    // 旋转 = 放弃跟随: undo the release on Esc / empty HUD (nothing was
    // committed, so the follower link comes back).
    if (m_releaseAttHeld && !m_releaseAttId.isNull()) {
        m_paramDoc->addAttachment(m_releaseAttBackup);
        m_releaseAttId = QUuid();
        m_releaseAttHeld = false;
    }
    m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();
}

double ToolRotate::currentAngleDeg() const
{
    if (!m_paramDoc) return 0.0;

    if (m_copyMode) {
        // Rotate-copy: the clone's angle RELATIVE to the original (stored
        // follower angle minus the 180° start-point exit offset).
        const auto& atts = m_paramDoc->attachments();
        for (const auto& a : atts) {
            if (a.id != m_cloneAttId) continue;
            return a.followerAngle - 180.0;
        }
        return 0.0;
    }

    if (m_connected) {
        const auto& atts = m_paramDoc->attachments();
        for (const auto& a : atts) {
            if (a.id != m_attId) continue;
            if (a.rotationMode == cad::param::RotationMode::ArcLength) {
                // Arc-length mode: derive the effective angle from the arc.
                // Arc is measured from the REVERSE direction: 弧长 0 = 角度 180°.
                // Normalized to [0, 360°) so multi-turn arcs never display
                // absurd angles (用户报告: 400°+ 爆表回归 2026-08).
                double arcMm = a.arcLength;
                if (!a.arcLengthFormula.isEmpty()) {
                    auto r = cad::param::ConditionEngine::evaluate(
                        a.arcLengthFormula, m_paramDoc->parameters(), {});
                    if (r.ok) arcMm = geo::Units::cmToMm(r.value);
                }
                const double radius = segmentRadius();
                double deg = (radius > 1e-9)
                    ? 180.0 + (arcMm / radius) * 180.0 / M_PI : 180.0;
                deg = std::fmod(deg, 360.0);
                if (deg < 0.0) deg += 360.0;
                return deg;
            }
            if (!a.followerAngleFormula.isEmpty()) {
                auto r = cad::param::ConditionEngine::evaluate(
                    a.followerAngleFormula, m_paramDoc->parameters(), {});
                if (r.ok) {
                    double v = std::fmod(r.value, 360.0);
                    if (v < 0.0) v += 360.0;
                    return v;
                }
            }
            // Normalize the display to [0, 360°) (存储保持原值; 显示层统一
            // 不爆表 —— 用户报告 400°+ 回归 2026-08).
            double deg = std::fmod(a.followerAngle, 360.0);
            if (deg < 0.0) deg += 360.0;
            return deg;
        }
        return 0.0;
    }

    // Free: display angle = direction FROM the anchor toward the line
    // (起点锚心: 线的世界方向; 终点锚心: 终点→起点方向 = 线方向+180°).
    // Unified semantics: the line always “points at the cursor”, so dragging
    // feels identical with either anchor (线跟随光标).
    const cad::param::Block* blk = m_paramDoc->findBlock(m_blockId);
    if (!blk || blk->segments.empty()) return 0.0;
    const cad::param::Segment& seg = blk->segments.front();
    const auto* sp = blk->findPoint(seg.startPointId);
    const auto* ep = blk->findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return 0.0;
    const cad::geo::Vec2 w1 = blk->transform.toWorld(sp->resolvedPos);
    const cad::geo::Vec2 w2 = blk->transform.toWorld(ep->resolvedPos);
    double deg = (w2 - w1).angle() * 180.0 / M_PI;
    if (m_anchorIsEnd) deg += 180.0;
    return deg;
}

bool ToolRotate::isAngleLocked() const
{
    // A rotate-copy clone carries no committed formula (unless the user typed
    // one into the HUD mid-gesture) — never lock the copy gesture.
    if (m_copyMode) return false;
    // A follower angle/arc driven by a formula/variable is locked against
    // free-hand editing. Based on the committed base formula so transient HUD
    // edits never flip the lock mid-session. Free lines are never locked.
    if (!m_connected) return false;
    if (m_rotationMode == cad::param::RotationMode::ArcLength)
        return !m_baseArcFormula.isEmpty();
    return !m_baseFormula.isEmpty();
}

void ToolRotate::onHudModeChanged(cad::param::RotationMode newMode)
{
    if (m_copyMode) {
        // Rotate-copy is always angle semantics (relative angle) — a mode
        // switch mid-gesture is ignored and the HUD snaps back to Angle.
        if (m_hud) m_hud->setMode(cad::param::RotationMode::Angle);
        return;
    }
    if (!m_paramDoc || !m_connected) return;
    // Geometry-preserving switch: convert current angle ↔ arc length.
    const double deg = currentAngleDeg();
    const double radius = segmentRadius();

    auto& atts = const_cast<std::vector<cad::param::Attachment>&>(m_paramDoc->attachments());
    for (auto& a : atts) {
        if (a.id != m_attId) continue;
        if (newMode == cad::param::RotationMode::ArcLength) {
            // Angle → ArcLength: preserve geometry. Arc is measured from the
            // REVERSE direction (弧长 0 = 角度 180°), normalized to [0, 360°).
            a.rotationMode = cad::param::RotationMode::ArcLength;
            double degFromReverse = std::fmod(deg - 180.0, 360.0);
            if (degFromReverse < 0.0) degFromReverse += 360.0;
            a.arcLength = degFromReverse * M_PI / 180.0 * radius;
            a.arcLengthFormula.clear();
        } else {
            // ArcLength → Angle: preserve geometry.
            a.rotationMode = cad::param::RotationMode::Angle;
            a.followerAngle = deg;
            a.followerAngleFormula.clear();
        }
        break;
    }
    m_rotationMode = newMode;
    m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();
    refreshHudText();
    updateGizmo();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Angle HUD
// ═══════════════════════════════════════════════════════════════════════════════

void ToolRotate::showHud()
{
    if (!m_scene || m_scene->views().isEmpty()) return;
    QGraphicsView* view = m_scene->views().first();
    QWidget* viewport = view->viewport();

    if (!m_hud) {
        m_hud = new AngleHud(viewport);
        m_hud->onTextChanged = [this](const QString& t) { onHudTextChanged(t); };
        m_hud->onCommit      = [this] { onHudCommit(); };
        m_hud->onCancel      = [this] { onHudCancel(); };
        m_hud->onModeChanged = [this](cad::param::RotationMode m) { onHudModeChanged(m); };
    } else {
        m_hud->setParent(viewport);
    }

    // Sync HUD mode with the attachment's rotation mode (connected only).
    if (m_connected)
        m_hud->setMode(m_rotationMode);

    // Position near the pivot (user → scene → viewport pixels).
    const QPointF scenePt = cad::geo::Coord::toScene(m_pivot.x, m_pivot.y);
    const QPoint vpPt = view->mapFromScene(scenePt);
    m_hud->move(vpPt + QPoint(16, 16));
    m_hud->adjustSize();

    m_hudValid = true;
    m_hud->setValid(true);
    refreshHudText();
    m_hud->show();
    m_hud->edit()->setFocus();
    m_hud->edit()->selectAll();   // typing immediately replaces the value
}

void ToolRotate::hideHud()
{
    if (m_hud) {
        m_hud->hide();
        if (m_scene && !m_scene->views().isEmpty())
            m_scene->views().first()->setFocus();
    }
}

void ToolRotate::refreshHudText()
{
    if (!m_hud) return;
    m_hud->edit()->blockSignals(true);
    // Caption follows the ACTIVE angle semantics (术语统一: 跟随角度 /
    // 绝对角度 / 相对角度): 跟随线 = 跟随角度/弧长(默认); 自由线 = 绝对角度;
    // 旋转复制 = 相对角度。setMode 只在模式切换时刷新标签，锚心切换
    // (Connected↔Free) 不切模式，所以这里每帧显式同步。
    if (m_copyMode)
        m_hud->setCaption(QString::fromUtf8("相对角度"));
    else if (!m_connected)
        m_hud->setCaption(QString::fromUtf8("绝对角度"));
    else
        m_hud->setCaption(QString());   // 默认: 跟随角度 / 弧长
    if (m_copyMode) {
        // Rotate-copy: live relative angle (degrees).
        m_hud->edit()->setText(formatDeg(currentAngleDeg()));
    } else if (m_rotationMode == cad::param::RotationMode::ArcLength && m_connected) {
        // Arc-length mode: show the value in cm (formula-driven values are
        // evaluated live — the HUD always shows a plain editable number).
        m_hud->edit()->setText(formatDeg(currentModeValue()));
    } else {
        // Angle mode: degrees (formula evaluated live, same idea).
        m_hud->edit()->setText(formatDeg(currentAngleDeg()));
    }
    m_hud->edit()->blockSignals(false);
}

void ToolRotate::onHudTextChanged(const QString& text)
{
    if (!m_paramDoc) return;
    const QString t = text.trimmed();

    if (t.isEmpty()) {
        if (m_copyMode) {
            applyAngleDeg(0.0);   // empty = revert the clone onto the original
        } else {
            restoreBase();        // empty = revert to the session base
        }
        m_hudValid = true;
    } else {
        bool isNumber = false;
        const double numVal = t.toDouble(&isNumber);
        if (isNumber) {
            // A plain number always applies: if the angle was formula-driven,
            // applyAngleDeg bakes the formula away (用户拍板: 输入 = 放弃公式).
            applyModeValue(numVal);
            m_hudValid = true;
        } else {
            auto r = cad::param::ConditionEngine::evaluate(
                t, m_paramDoc->parameters(), {});
            if (r.ok) {
                if (m_copyMode) {
                    // Formula typed into the copy HUD: the clone never keeps
                    // a formula (用户拍板: 副本自动去公式) — apply the value
                    // as a relative angle instead.
                    auto& atts = const_cast<std::vector<cad::param::Attachment>&>(
                        m_paramDoc->attachments());
                    for (auto& a : atts) {
                        if (a.id == m_cloneAttId) {
                            a.followerAngle = r.value + 180.0;
                            a.followerAngleFormula.clear();
                            break;
                        }
                    }
                    m_paramDoc->resolveAll();
                    if (m_scene) m_scene->refreshAllBlockItems();
                } else if (m_connected) {
                    // Persist the formula (the resolver lets the formula win).
                    auto& atts = const_cast<std::vector<cad::param::Attachment>&>(
                        m_paramDoc->attachments());
                    for (auto& a : atts) {
                        if (a.id == m_attId) {
                            if (m_rotationMode == cad::param::RotationMode::ArcLength) {
                                a.arcLength = geo::Units::cmToMm(r.value);
                                a.arcLengthFormula = t;
                            } else {
                                a.followerAngle = r.value;
                                a.followerAngleFormula = t;
                            }
                            break;
                        }
                    }
                    m_paramDoc->resolveAll();
                    if (m_scene) m_scene->refreshAllBlockItems();
                } else {
                    applyAngleDeg(r.value);   // free block: one-shot numeric apply
                }
                m_hudValid = true;
            } else {
                m_hudValid = false;           // keep the last valid geometry
            }
        }
    }

    if (m_hud) m_hud->setValid(m_hudValid);
    if (m_hudValid) updateGizmo();
}

void ToolRotate::onHudCommit()
{
    if (!m_hudValid) return;   // ignore Enter on an invalid formula
    if (m_copyMode) commitRotateCopy();
    else            commitCurrent();
}

void ToolRotate::onHudCancel()
{
    if (m_copyMode) {
        cancelRotateCopy();     // drop the preview clone, keep the target
    } else if (m_state == RotateState::Rotating) {
        cancelRotation();       // abort the drag, keep the target
    } else {
        restoreBase();          // drop any uncommitted HUD preview
        clearTarget();          // back to Idle
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Protractor gizmo
// ═══════════════════════════════════════════════════════════════════════════════

double ToolRotate::currentZoom() const
{
    if (m_scene && !m_scene->views().isEmpty()) {
        const double z = m_scene->views().first()->transform().m11();
        if (z > 1e-9) return z;
    }
    return 1.0;
}

// ---------------------------------------------------------------------------
// Endpoint aim snap (终点方向吸附)
// ---------------------------------------------------------------------------

cad::geo::Vec2 ToolRotate::endpointAtAngle(double angleDeg) const
{
    // World direction of the segment at the given mode-appropriate angle.
    double worldDirRad;
    if (m_copyMode)
        // Copy mode: angle is measured RELATIVE to the original's current
        // direction (captured at copy start into m_copyRefWorldRad).
        worldDirRad = m_copyRefWorldRad + angleDeg * M_PI / 180.0;
    else if (m_connected)
        worldDirRad = m_refWorldRad + angleDeg * M_PI / 180.0;
    else
        worldDirRad = angleDeg * M_PI / 180.0;

    // Segment length (local, constant during rotation): use the LONGEST
    // segment — blocks are normally single-segment lines, but for the rare
    // multi-segment block the endpoint estimate should track the dominant
    // extent rather than blindly the first segment.
    double segLen = 0.0;
    if (m_paramDoc) {
        const auto* blk = m_paramDoc->findBlock(m_blockId);
        if (blk) {
            for (const auto& seg : blk->segments) {
                const auto* sp = blk->findPoint(seg.startPointId);
                const auto* ep = blk->findPoint(seg.endPointId);
                if (sp && ep && sp->resolved && ep->resolved)
                    segLen = std::max(segLen, sp->resolvedPos.distanceTo(ep->resolvedPos));
            }
        }
    }
    return m_pivot + cad::geo::Vec2{std::cos(worldDirRad), std::sin(worldDirRad)} * segLen;
}

void ToolRotate::checkEndpointAimSnap(double& angleDeg)
{
    if (!m_paramDoc || !m_scene) return;

    const cad::geo::Vec2 endPos = endpointAtAngle(angleDeg);
    const double zoom = currentZoom();
    // Search radius: the rotating endpoint must come CLOSE to the candidate
    // point for the aim to engage (tightened from 30px — the old value made
    // the snap fire on any nearby point and feel sticky).
    constexpr double searchRadiusPx = 15.0;
    const double searchRadius = searchRadiusPx / zoom;

    // Angular tolerance for direction alignment (radians): tightened to 2°
    // so only a near-exact alignment snaps. The aim is a TRANSIENT assist —
    // keep rotating past the tolerance and the snap releases (用户继续旋转
    // = 解除吸附, 见 commitRotation).
    constexpr double alignTolRad = 2.0 * M_PI / 180.0;

    QUuid bestBlockId, bestPointId;
    cad::geo::Vec2 bestPos;
    double bestDist = searchRadius;

    for (const auto& blk : m_paramDoc->blocks()) {
        if (blk.id == m_blockId) continue;  // Skip own block's points.
        for (const auto& pt : blk.points) {
            if (!pt.resolved || !pt.selectable) continue;
            const cad::geo::Vec2 wpos = blk.transform.toWorld(pt.resolvedPos);

            // Must be near the rotating endpoint.
            const double d = wpos.distanceTo(endPos);
            if (d > searchRadius) continue;

            // Direction from pivot to this point.
            const cad::geo::Vec2 aim = wpos - m_pivot;
            if (aim.lengthSquared() < 1e-12) continue;
            const double dirToP = std::atan2(aim.y, aim.x);

            // Current world direction of the SEGMENT (line direction, NOT the
            // display angle: end-anchor display carries a 180° offset).
            double worldDirRad;
            if (m_copyMode)
                worldDirRad = m_copyRefWorldRad + angleDeg * M_PI / 180.0;
            else if (m_connected)
                worldDirRad = m_refWorldRad + angleDeg * M_PI / 180.0;
            else
                worldDirRad = (angleDeg - (m_anchorIsEnd ? 180.0 : 0.0)) * M_PI / 180.0;

            double diff = dirToP - worldDirRad;
            while (diff >  M_PI) diff -= 2.0 * M_PI;
            while (diff <= -M_PI) diff += 2.0 * M_PI;

            if (std::abs(diff) < alignTolRad && d < bestDist) {
                bestDist = d;
                bestBlockId = blk.id;
                bestPointId = pt.id;
                bestPos = wpos;
            }
        }
    }

    if (bestPointId.isNull()) {
        clearAimCandidate();
        return;
    }

    // Snap the angle to aim exactly at the candidate.
    const cad::geo::Vec2 aim = bestPos - m_pivot;
    const double dirToP = std::atan2(aim.y, aim.x);
    if (m_copyMode)
        angleDeg = (dirToP - m_copyRefWorldRad) * 180.0 / M_PI;
    else if (m_connected)
        angleDeg = (dirToP - m_refWorldRad) * 180.0 / M_PI;
    else
        // Line direction = dirToP; end-anchor display adds the 180° offset.
        angleDeg = dirToP * 180.0 / M_PI + (m_anchorIsEnd ? 180.0 : 0.0);

    m_aimBlockId = bestBlockId;
    m_aimPointId = bestPointId;

    // Highlight ring on the candidate point.
    if (!m_aimRing) {
        constexpr double r = 8.0;
        m_aimRing = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
        QPen pen(QColor(255, 152, 0));  // amber
        pen.setWidthF(2.0);
        pen.setCosmetic(true);
        m_aimRing->setPen(pen);
        m_aimRing->setBrush(Qt::NoBrush);
        m_aimRing->setZValue(105.0);
        m_scene->addItem(m_aimRing);
    }
    m_aimRing->setPos(cad::geo::Coord::toScene(bestPos));
    m_aimRing->setVisible(true);
}

void ToolRotate::clearAimCandidate()
{
    m_aimBlockId = QUuid();
    m_aimPointId = QUuid();
    if (m_aimRing) m_aimRing->setVisible(false);
}

void ToolRotate::buildGizmo()
{
    if (!m_scene) return;
    removeGizmo();

    const double zoom = currentZoom();
    const QPointF c = cad::geo::Coord::toScene(m_pivot.x, m_pivot.y);

    // Pivot ring (teal).
    m_pivotRing = new QGraphicsEllipseItem();
    QPen ringPen(QColor(38, 166, 154));
    ringPen.setWidthF(2.0);
    ringPen.setCosmetic(true);
    m_pivotRing->setPen(ringPen);
    m_pivotRing->setBrush(QColor(38, 166, 154, 40));
    m_pivotRing->setZValue(9998);
    const double r = 6.0 / zoom;
    m_pivotRing->setRect(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);

    // Reference dashed line along the reference direction (60 px).
    m_refLine = new QGraphicsPathItem();
    QPen refPen(QColor(120, 144, 156));
    refPen.setWidthF(1.0);
    refPen.setCosmetic(true);
    refPen.setStyle(Qt::DashLine);
    m_refLine->setPen(refPen);
    m_refLine->setZValue(9997);
    const double refLen = 60.0 / zoom;
    QPainterPath refPath;
    refPath.moveTo(c);
    refPath.lineTo(c.x() + refLen * std::cos(m_refWorldRad),
                   c.y() - refLen * std::sin(m_refWorldRad));  // scene is y-down
    m_refLine->setPath(refPath);

    // Angle arc (amber) — geometry refreshed by updateGizmo().
    m_arc = new QGraphicsPathItem();
    QPen arcPen(QColor(251, 140, 0));
    arcPen.setWidthF(2.0);
    arcPen.setCosmetic(true);
    m_arc->setPen(arcPen);
    m_arc->setZValue(9998);

    // Angle label.
    m_label = new QGraphicsSimpleTextItem();
    m_label->setBrush(QColor(251, 140, 0));
    m_label->setZValue(9999);

    m_scene->addItem(m_pivotRing);
    m_scene->addItem(m_refLine);
    m_scene->addItem(m_arc);
    m_scene->addItem(m_label);
}

void ToolRotate::updateGizmo()
{
    if (!m_scene || !m_arc || !m_label) return;
    const double zoom = currentZoom();
    const QPointF c = cad::geo::Coord::toScene(m_pivot.x, m_pivot.y);
    const double deg = currentAngleDeg();
    const double endRad = (m_copyMode ? m_copyRefWorldRad : m_refWorldRad)
                          + deg * M_PI / 180.0;

    // Arc sweeping from the reference direction to the current angle.
    const double arcR = 40.0 / zoom;
    QPainterPath arcPath;
    constexpr int kSamples = 40;
    for (int i = 0; i <= kSamples; ++i) {
        const double t = m_refWorldRad + (endRad - m_refWorldRad) * i / kSamples;
        const QPointF p(c.x() + arcR * std::cos(t),
                        c.y() - arcR * std::sin(t));           // scene is y-down
        if (i == 0) arcPath.moveTo(p); else arcPath.lineTo(p);
    }
    m_arc->setPath(arcPath);

    // The gizmo always shows the LIVE effective value — formula-driven
    // angles evaluate their formula (用户拍板: HUD/gizmo 与可操作性一致).
    QPen arcPen(QColor(251, 140, 0));
    arcPen.setWidthF(2.0);
    arcPen.setCosmetic(true);
    m_arc->setPen(arcPen);

    // Label just outside the arc's mid-angle.
    const double midRad = m_refWorldRad + (endRad - m_refWorldRad) * 0.5;
    const double labelR = arcR + 14.0 / zoom;
    if (m_copyMode) {
        m_label->setText(QStringLiteral("%1\xc2\xb0").arg(formatDeg(deg)));
    } else if (m_rotationMode == cad::param::RotationMode::ArcLength && m_connected) {
        m_label->setText(QStringLiteral("%1cm").arg(formatDeg(currentModeValue())));
    } else {
        m_label->setText(QStringLiteral("%1\xc2\xb0").arg(formatDeg(deg)));
    }
    m_label->setBrush(QColor(251, 140, 0));
    m_label->setPos(c.x() + labelR * std::cos(midRad),
                    c.y() - labelR * std::sin(midRad));
    m_label->setScale(1.0 / zoom);   // constant screen size
}

void ToolRotate::removeGizmo()
{
    if (!m_scene) return;
    if (m_pivotRing) { m_scene->removeItem(m_pivotRing); delete m_pivotRing; m_pivotRing = nullptr; }
    if (m_refLine)   { m_scene->removeItem(m_refLine);   delete m_refLine;   m_refLine = nullptr; }
    if (m_arc)       { m_scene->removeItem(m_arc);       delete m_arc;       m_arc = nullptr; }
    if (m_label)     { m_scene->removeItem(m_label);     delete m_label;     m_label = nullptr; }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Hit testing
// ═══════════════════════════════════════════════════════════════════════════════

QUuid ToolRotate::hitBlock(const cad::geo::Vec2& worldPos) const
{
    if (!m_scene) return QUuid();
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const QList<QGraphicsItem*> hits = m_scene->items(scenePt);
    for (QGraphicsItem* item : hits) {
        // Curve children belong to their block — walk up to the BlockItem.
        if (auto* bi = BlockItem::containingItem(item)) {
            // Only blocks on the active layer are rotatable — same rule as
            // the select tool's hitBlock: grayed reference layers must stay
            // untouched (editing invisible-to-user geometry is a trap).
            if (m_paramDoc) {
                const auto* blk = m_paramDoc->findBlock(bi->blockId());
                if (blk && blk->layer == m_paramDoc->activeLayer())
                    return bi->blockId();
            }
        }
    }
    return QUuid();
}

} // namespace cad::tools
