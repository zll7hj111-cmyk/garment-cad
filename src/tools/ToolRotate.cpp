#include "ToolRotate.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QGraphicsEllipseItem>
#include <QKeyEvent>
#include <QUndoStack>
#include <QPen>
#include "ElaLineEdit.h"
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
#include "RotateCopyGesture.h"
#include "RotateGizmo.h"
#include "LinePropertyDialog.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/ComponentCommands.h"

namespace cad::tools {

// ── 角度显示约定（2026-08 v3 定稿，用户拍板）────────────────────────────
// 存储域 α ∈ [0, 360°)（Resolver/序列化零改动）；显示域分两种：
//  • 连接线（跟随角度/弧长）= 带符号折角 [−180°, +180°]：折叠 0°、
//    垂直 ±90°、开平 ±180°，符号 = 折向（α ≤ 180° → +α，否则 α−360°）。
//  • 自由线绝对角度 / 复制相对角度 = 0~360° 逆时针为正（行业默认，
//    AutoCAD 同）。
// 统一实现收口在 geometry/Angle.h（normalizeDeg360 / normalizeDeg180）与
// geometry/Units.h（formatDegValue），此处不再本地复制。

// ═══════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

ToolRotate::~ToolRotate()
{
    // The HUD is parented to the viewport (which outlives this tool); the
    // QPointer turns null if the viewport was already torn down, so deleting
    // it here is always safe. Without this each tool switch (ToolManager
    // rebuilds the tool) would leak a hidden AngleHud on the viewport.
    delete m_hud;
}

void ToolRotate::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene = &scene;
    m_paramDoc = paramDoc;
    m_state = RotateState::Idle;
    // (Re)create the rotate-copy gesture with the current context.
    delete m_copyGesture;
    m_copyGesture = new RotateCopyGesture(this);
    // (Re)create the protractor gizmo renderer (scene-bound).
    delete m_gizmo;
    m_gizmo = new RotateGizmo(&scene);
}

void ToolRotate::deactivate()
{
    // Mid-copy deactivation (tool switch): drop the preview clone, the
    // original block was never touched.
    if (m_copyGesture && m_copyGesture->active())
        m_copyGesture->cancel();
    delete m_copyGesture;
    m_copyGesture = nullptr;
    delete m_gizmo;
    m_gizmo = nullptr;
    // A follower link released by an in-flight rotation is restored — tool
    // switch abandons the gesture (nothing was committed).
    if (m_releaseAttHeld && !m_releaseAttId.isNull() && m_paramDoc) {
        m_paramDoc->addAttachmentRaw(m_releaseAttBackup);  // verbatim (keep snapshot isLocked)
        m_paramDoc->resolveAll();
        m_releaseAttId = QUuid();
        m_releaseAttHeld = false;
    }
    // 组件整组旋转: 未提交的整组释放同样恢复 (工具切换放弃手势).
    if (m_groupReleasedHeld && m_paramDoc)
        restoreComponentExternal();
    removeGizmo();
    removeGroupHighlightBox();
    if (m_hud) { m_hud->hide(); delete m_hud; m_hud = nullptr; }
    if (m_aimRing && m_scene) { m_scene->removeItem(m_aimRing); delete m_aimRing; m_aimRing = nullptr; }
    m_blockId = QUuid();
    m_attId = QUuid();
    m_connected = false;
    m_anchorPointId = QUuid();
    m_state = RotateState::Idle;
    // 组件整组旋转会话重置 (无释放残留: deactivate 前 rotate 未提交即 restore).
    m_compId = QUuid();
    m_groupMode = false;
    m_groupPivotSet = false;
    m_groupPivotPointId = QUuid();
    m_groupDelta = 0.0;
    m_groupBaseTf.clear();
    m_groupReleasedAtts.clear();
    m_groupReleasedTargets.clear();
    m_groupReleasedDarts.clear();
    m_groupReleasedHeld = false;
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
                m_copyGesture->begin(pos);
        }
        break;
    }
    case RotateState::Ready: {
        // ── W 键组件整组旋转 (2026-12): 整组模式下一切按压 = 锚点/旋转,
        // 不切换目标 (点别的线只是锚点所在地, 主流 CAD 语义).
        if (m_groupMode) {
            if (!m_groupPivotSet)
                beginGroupPivot(pos);
            else
                beginGroupRotation(pos);
            break;
        }
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
            m_copyGesture->begin(pos);
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
                // 已连接线段禁止点击切换锚心（用户拍板 2026-08）: 用户本意
                // 是点击头部旋转，切换会让跟随附着点跳变/断开——除非在属性
                // 面板中显式断开跟随。拦截后落到 beginRotation 直接旋转。
                bool anchorLocked = false;
                if (const auto* blk = m_paramDoc->findBlock(m_blockId);
                    blk && !blk->segments.empty()) {
                    anchorLocked =
                        attachmentAtPoint(blk->segments.front().startPointId)
                        || attachmentAtPoint(blk->segments.front().endPointId);
                }
                if (!anchorLocked) {
                    commitCurrent();
                    // Guard the block/segment lookup like the other paths in this
                    // file (toggleAnchor/rebuildAnchorState) — the anchor hit may
                    // belong to a target whose block vanished mid-gesture.
                    if (const auto* blk = m_paramDoc->findBlock(m_blockId);
                        blk && !blk->segments.empty()) {
                        m_anchorIsEnd = (hit
                            == blk->segments.front().endPointId);
                    }
                    rebuildAnchorState();
                    removeGizmo();
                    buildGizmo();
                    updateGizmo();
                    showHud();
                    if (m_scene) m_scene->refreshAllBlockItems();
                    break;
                }
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
    // 整组旋转: 拖动帧直通 group 分支 (单线的 Ctrl 转换/吸附逻辑不适用).
    if (m_groupMode) {
        if (m_state == RotateState::Rotating) {
            const cad::geo::Vec2 gp(event->scenePos().x(), event->scenePos().y());
            updateGroupRotation(gp, event->modifiers() & Qt::ShiftModifier);
        }
        return;
    }
    if (m_state != RotateState::Rotating) return;
    const cad::geo::Vec2 pos(event->scenePos().x(), event->scenePos().y());
    // 普通旋转拖动中途按下 Ctrl：转为旋转复制（原块回弹 + 副本接手当前角度）。
    // 判定放在 press 时刻之外，用户先按下鼠标再按 Ctrl 也能得到复制语义。
    // (Rotating 状态下的 move 必然在拖动中，无需再查 buttons()。)
    if (!m_copyGesture->active() && (event->modifiers() & Qt::ControlModifier)) {
        m_copyGesture->convert(pos);
        if (!m_copyGesture->active()) return;   // 转换被拒（跟随/解挂态）：保持普通旋转
    }
    const bool snap = (event->modifiers() & Qt::ShiftModifier);
    updateRotation(pos, snap);
}

void ToolRotate::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    if (m_state != RotateState::Rotating) return;
    if (m_copyGesture->active()) m_copyGesture->commit();
    else if (m_groupMode)          commitGroupRotation();
    else                           commitRotation();
}

void ToolRotate::mouseDoubleClick(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;

    // A double-click is preceded by two press/release pairs; the second press
    // may have started a rotation — cancel it and drop the target before
    // opening the property dialog.
    if (m_state == RotateState::Rotating) {
        if (m_groupMode) cancelGroupRotation();
        else             cancelRotation();
    }
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
    // NOTE: no WA_DeleteOnClose — see ToolSelect::openLineProperty.
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
        // 整组旋转模式下锚心概念不适用 — 禁用 (W 键专属整组语义).
        if (!m_groupMode) toggleAnchor();
        event->accept();
    } else if (event->key() == Qt::Key_W) {
        // W 键组件整组旋转 (2026-12 用户拍板, ROTATE_COMPONENT_DESIGN.md):
        // 默认单线; 目标线属于某组件时可切换整组旋转模式; 再按 W 回单线.
        if (!m_groupMode && m_compId.isNull()) {
            if (m_scene)
                m_scene->showToast(QString::fromUtf8("整组旋转仅适用于组件（请先选中组件中的线）"));
        } else {
            toggleGroupMode();
        }
        event->accept();
    }
}
// ═══════════════════════════════════════════════════════════════════════════════
// Target selection
// ═══════════════════════════════════════════════════════════════════════════════

cad::param::Attachment* ToolRotate::followerAttachment()
{
    if (!m_paramDoc || m_blockId.isNull()) return nullptr;
    for (const auto& a : m_paramDoc->attachments())
        if (a.fromBlockId == m_blockId && !a.isPin)
            return m_paramDoc->findAttachment(a.id);
    return nullptr;
}

cad::param::Attachment* ToolRotate::editableAttachment()
{
    return m_paramDoc ? m_paramDoc->findAttachment(m_attId) : nullptr;
}

const cad::param::Attachment* ToolRotate::editableAttachment() const
{
    return m_paramDoc ? m_paramDoc->findAttachment(m_attId) : nullptr;
}

cad::param::Attachment* ToolRotate::attachmentAtPoint(const QUuid& pointId)
{
    if (!m_paramDoc || m_blockId.isNull() || pointId.isNull()) return nullptr;
    for (const auto& a : m_paramDoc->attachments())
        if (a.fromBlockId == m_blockId && a.fromPointId == pointId && !a.isPin)
            return m_paramDoc->findAttachment(a.id);
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

    // 已连接线段禁止切换锚心（用户拍板 2026-08）: 切换会改变跟随附着点，
    // 连接语义复杂化甚至断开——只在属性面板显式断开跟随后可切。
    if (attachmentAtPoint(blk->segments.front().startPointId)
        || attachmentAtPoint(blk->segments.front().endPointId))
        return;

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
        const cad::param::Block* fromBlk = m_paramDoc->findBlock(att->fromBlockId);
        m_pivot = fromBlk ? fromBlk->worldPos(att->fromPointId) : blk->worldPos(m_anchorPointId);
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
    if (auto* a = m_paramDoc->findAttachment(m_releaseAttId))
        m_releaseAttBackup = *a;
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
    // 组件整组旋转 (W 键) 会话重置: 目标所属组件 / 模式 / 锚点 / 快照.
    m_compId = QUuid();
    m_groupMode = false;
    m_groupReleasedHeld = false;
    m_groupReleasedAtts.clear();
    m_groupReleasedTargets.clear();
    m_groupReleasedDarts.clear();
    m_groupPivotSet = false;
    m_groupPivotPointId = QUuid();
    m_groupDelta = 0.0;
    m_groupBaseTf.clear();
    removeGroupHighlightBox();
    if (m_paramDoc) {
        if (const auto* comp = m_paramDoc->componentOfBlock(blockId))
            m_compId = comp->id;
    }
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
    removeGroupHighlightBox();
    m_blockId = QUuid();
    m_attId = QUuid();
    m_connected = false;
    m_anchorPointId = QUuid();
    m_releaseAttId = QUuid();
    m_releaseAttHeld = false;
    // 组件整组旋转会话重置.
    m_compId = QUuid();
    m_groupMode = false;
    m_groupReleasedHeld = false;
    m_groupReleasedAtts.clear();
    m_groupReleasedTargets.clear();
    m_groupReleasedDarts.clear();
    m_groupPivotSet = false;
    m_groupPivotPointId = QUuid();
    m_groupDelta = 0.0;
    m_groupBaseTf.clear();
    m_state = RotateState::Idle;
    if (m_scene) m_scene->refreshAllBlockItems();
}


// ═══════════════════════════════════════════════════════════════════════════════
// W 键组件整组旋转 (2026-12 用户拍板, ROTATE_COMPONENT_DESIGN.md)
// 设计要点: 目标 = 整个组件; 锚点 = 任意点 (第一击设锚, 第二击起手拖动);
// 全体成员绕锚点刚体变换 (rotation += delta / origin 绕 pivot 旋转);
// 外部约束 (D7 判定表) 开始前快照 + 释放, commit restore-then-replay /
// undo 原样恢复; 锚点不存储 (会话态, 不进文档).
// ═══════════════════════════════════════════════════════════════════════════════

void ToolRotate::toggleGroupMode()
{
    if (m_groupMode) exitGroupMode();
    else if (m_state == RotateState::Ready && !m_compId.isNull()) enterGroupMode();
}

void ToolRotate::enterGroupMode()
{
    if (m_state != RotateState::Ready || m_compId.isNull()) return;
    commitCurrent();                 // 落盘任何未提交 HUD 编辑
    m_groupMode = true;
    m_groupPivotSet = false;
    m_groupPivotPointId = QUuid();
    m_groupDelta = 0.0;
    m_groupBaselineRad = 0.0;
    m_groupReleasedHeld = false;
    m_groupReleasedAtts.clear();
    m_groupReleasedTargets.clear();
    m_groupReleasedDarts.clear();
    m_groupBaseTf.clear();
    if (m_paramDoc) {
        const auto* comp = m_paramDoc->findComponent(m_compId);
        if (!comp) { m_groupMode = false; return; }
        for (const QUuid& mid : comp->memberBlockIds) {
            if (const auto* b = m_paramDoc->findBlock(mid))
                m_groupBaseTf.insert(mid, b->transform);
        }
        if (m_groupBaseTf.isEmpty()) { m_groupMode = false; return; }
    }
    removeGizmo();                   // 锚心环不适用 (锚点尚未确定)
    updateGroupHighlightBox();
    showHud();                       // refreshHudText → 整组旋转 caption
    if (m_scene) m_scene->refreshAllBlockItems();
}

void ToolRotate::exitGroupMode()
{
    if (!m_groupMode) return;
    // 未提交的释放中途退出 = 取消手势: 恢复基准位姿 + 快照约束.
    if (m_groupReleasedHeld && m_paramDoc) {
        for (auto it = m_groupBaseTf.cbegin(); it != m_groupBaseTf.cend(); ++it) {
            if (auto* b = m_paramDoc->findBlock(it.key()))
                b->transform = it.value();
        }
        restoreComponentExternal();  // 内含 resolveAll
    }
    m_groupMode = false;
    m_groupPivotSet = false;
    m_groupPivotPointId = QUuid();
    m_groupDelta = 0.0;
    m_groupBaselineRad = 0.0;
    m_state = RotateState::Ready;    // 中途 W 退出 = 中止拖动
    removeGroupHighlightBox();
    removeGizmo();
    // 回单线: 以当前几何重建会话 (成员可能已被整组旋转移动).
    if (m_paramDoc && !m_blockId.isNull()) {
        rebuildAnchorState();
        if (!m_blockId.isNull()) {
            buildGizmo();
            updateGizmo();
            showHud();
            if (m_scene) m_scene->refreshAllBlockItems();
        }
    }
}

void ToolRotate::beginGroupPivot(const cad::geo::Vec2& pos)
{
    if (m_state != RotateState::Ready || !m_groupMode || !m_paramDoc) return;
    m_groupPivotPointId = snapAnyPoint(pos);
    if (!m_groupPivotPointId.isNull()) {
        // 吸附: 世界位置 = 命中点当前解析位.
        bool found = false;
        for (const auto& blk : m_paramDoc->blocks()) {
            for (const auto& pt : blk.points) {
                if (pt.id == m_groupPivotPointId && pt.resolved) {
                    m_groupPivot = blk.transform.toWorld(pt.resolvedPos);
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) m_groupPivot = pos;   // 命中点在拖动间已消失: 回退自由锚点
    } else {
        m_groupPivot = pos;               // 自由锚点 (任意位置)
    }
    m_groupPivotSet = true;
    m_groupDelta = 0.0;
    // 锚心环就位 (gizmo 复用单线态 pivot/ref 字段 — 整组模式不触碰单线逻辑).
    m_pivot = m_groupPivot;
    m_refWorldRad = 0.0;
    removeGizmo();
    buildGizmo();
    updateGizmo();
    showHud();                           // HUD 移到锚点附近
}

void ToolRotate::beginGroupRotation(const cad::geo::Vec2& pos)
{
    if (m_state != RotateState::Ready || !m_groupMode || !m_groupPivotSet) return;
    // D7: 收集 + 释放外部约束 (一次性; 幂等).
    collectAndReleaseComponentExternal();
    m_state = RotateState::Rotating;
    const cad::geo::Vec2 d = pos - m_groupPivot;
    m_groupBaselineRad = (d.lengthSquared() > 1e-12) ? std::atan2(d.y, d.x) : 0.0;
    m_groupDelta = 0.0;
    showHud();
}

void ToolRotate::updateGroupRotation(const cad::geo::Vec2& pos, bool snap)
{
    if (m_state != RotateState::Rotating || !m_groupMode) return;
    const cad::geo::Vec2 d = pos - m_groupPivot;
    const double theta = std::atan2(d.y, d.x);
    double delta = cad::geo::normalizeRad(theta - m_groupBaselineRad);
    double deg = cad::geo::radToDeg(delta);
    if (snap) deg = std::round(deg / 15.0) * 15.0;
    applyGroupDelta(deg);
    updateGizmo();
    refreshHudText();
}

void ToolRotate::applyGroupDelta(double deg)
{
    if (!m_paramDoc || m_compId.isNull()) return;
    const double deltaRad = deg * M_PI / 180.0;
    m_groupDelta = deg;
    const auto* comp = m_paramDoc->findComponent(m_compId);
    if (!comp) return;
    // 每帧热路径铁律 (2026-09 收敛): resolveForDrag + syncBlockPositions,
    // 禁止 resolveAll/refreshAllBlockItems.
    QList<QUuid> seeds;
    for (const QUuid& mid : comp->memberBlockIds) {
        auto* b = m_paramDoc->findBlock(mid);
        if (!b) continue;
        const auto it = m_groupBaseTf.constFind(mid);
        if (it == m_groupBaseTf.constEnd()) continue;
        cad::param::Transform2D nf = it.value();
        nf.rotation += deltaRad;
        nf.origin = m_groupPivot + (nf.origin - m_groupPivot).rotated(deltaRad);
        b->transform = nf;
        seeds << mid;
        m_paramDoc->invalidateLayer(b->layer);
    }
    if (seeds.isEmpty()) return;
    m_paramDoc->resolveForDrag(seeds);
    if (m_scene) m_scene->syncBlockPositions();
    updateGroupHighlightBox();
}

void ToolRotate::commitGroupRotation()
{
    if (!m_paramDoc || !m_undoStack || m_compId.isNull()) {
        if (m_state == RotateState::Rotating) m_state = RotateState::Ready;
        return;
    }
    const auto* comp = m_paramDoc->findComponent(m_compId);
    if (!comp) { m_state = RotateState::Ready; return; }

    // 1) 捕获最终位姿 (恢复前!).
    QHash<QUuid, cad::param::Transform2D> newTf;
    bool moved = false;
    for (const QUuid& mid : comp->memberBlockIds) {
        const auto* b = m_paramDoc->findBlock(mid);
        const auto it = m_groupBaseTf.constFind(mid);
        if (!b || it == m_groupBaseTf.constEnd()) continue;
        newTf.insert(mid, b->transform);
        const double dRot = std::abs(b->transform.rotation - it.value().rotation);
        const double dOrg = b->transform.origin.distanceTo(it.value().origin);
        if (dRot > 1e-9 || dOrg > 1e-6) moved = true;
    }
    if (!m_groupReleasedAtts.empty() || !m_groupReleasedTargets.empty()
        || !m_groupReleasedDarts.empty())
        moved = true;

    // 2) Restore-then-replay: 恢复基准现场 (位姿 + 快照约束), 再推命令重放.
    for (auto it = m_groupBaseTf.cbegin(); it != m_groupBaseTf.cend(); ++it) {
        if (auto* b = m_paramDoc->findBlock(it.key()))
            b->transform = it.value();
    }
    if (m_groupReleasedHeld && !m_groupReleasedAtts.empty())
        m_paramDoc->addAttachmentsRaw(m_groupReleasedAtts);
    for (const auto& r : m_groupReleasedTargets) {
        if (auto* b = m_paramDoc->findBlock(r.blockId)) {
            b->endTargetBlockId = r.endTargetBlockId;
            b->endTargetPointId = r.endTargetPointId;
            b->endTargetOffset = r.endTargetOffset;
            b->endTargetOffsetFormula = r.endTargetOffsetFormula;
        }
    }
    for (const auto& r : m_groupReleasedDarts) {
        if (auto* b = m_paramDoc->findBlock(r.blockId)) {
            b->dartStartBlockId = r.dartStartBlockId;
            b->dartStartPointId = r.dartStartPointId;
            b->dartRefBlockId = r.dartRefBlockId;
            b->dartRefPointId = r.dartRefPointId;
            b->dartRefSegmentId = r.dartRefSegmentId;
            b->dartOffsetMm = r.dartOffsetMm;
            b->dartOffsetFormula = r.dartOffsetFormula;
            b->dartAngleDeg = r.dartAngleDeg;
            b->dartAngleFormula = r.dartAngleFormula;
        }
    }
    m_state = RotateState::Ready;
    if (!moved) {
        // 无位移也无释放: 现场完全恢复, 无需命令.
        m_groupReleasedHeld = false;
        m_groupDelta = 0.0;
        updateGizmo();
        refreshHudText();
        return;
    }
    m_undoStack->push(new cad::cmd::RotateComponentCommand(
        m_paramDoc, m_compId, m_groupBaseTf, newTf,
        m_groupReleasedAtts, m_groupReleasedTargets, m_groupReleasedDarts));
    m_groupBaseTf = newTf;
    m_groupReleasedHeld = false;
    m_groupDelta = 0.0;
    updateGizmo();
    refreshHudText();
}

void ToolRotate::cancelGroupRotation()
{
    if (m_state != RotateState::Rotating || !m_groupMode) return;
    m_state = RotateState::Ready;
    for (auto it = m_groupBaseTf.cbegin(); it != m_groupBaseTf.cend(); ++it) {
        if (auto* b = m_paramDoc->findBlock(it.key()))
            b->transform = it.value();
    }
    restoreComponentExternal();
    m_groupDelta = 0.0;
    updateGizmo();
    refreshHudText();
    if (m_scene) m_scene->refreshAllBlockItems();
}

void ToolRotate::collectAndReleaseComponentExternal()
{
    if (!m_paramDoc || m_compId.isNull() || m_groupReleasedHeld) return;
    const auto* comp = m_paramDoc->findComponent(m_compId);
    if (!comp) return;
    // D7 判定表: 组件级 attachment / 成员线→组外 leader (含 pin) → 释放;
    // 组内连接保持 (整组刚体旋转下相对关系自洽).
    const auto isMember = [comp](const QUuid& id) { return comp->isMember(id); };
    QList<QUuid> toRemove;
    for (const auto& a : m_paramDoc->attachments()) {
        if (a.fromComponentId == m_compId
            || (isMember(a.fromBlockId) && !isMember(a.toBlockId))) {
            m_groupReleasedAtts.push_back(a);
            toRemove << a.id;
        }
    }
    if (!toRemove.isEmpty())
        m_paramDoc->removeAttachments(toRemove);   // 批处理: 组件级不清 exposedPointId

    // 成员指向组外点 endTarget / 引用组外点的省道线 → 释放 (快照全字段).
    for (const QUuid& mid : comp->memberBlockIds) {
        auto* b = m_paramDoc->findBlock(mid);
        if (!b) continue;
        if (!b->endTargetBlockId.isNull() && !isMember(b->endTargetBlockId)) {
            cad::cmd::AimRelease r;
            r.blockId = mid;
            r.endTargetBlockId = b->endTargetBlockId;
            r.endTargetPointId = b->endTargetPointId;
            r.endTargetOffset = b->endTargetOffset;
            r.endTargetOffsetFormula = b->endTargetOffsetFormula;
            m_groupReleasedTargets.push_back(r);
            b->endTargetBlockId = QUuid();
            b->endTargetPointId = QUuid();
            b->endTargetOffset = 0.0;
            b->endTargetOffsetFormula.clear();
        }
        if (b->isDart()
            && (!isMember(b->dartStartBlockId) || !isMember(b->dartRefBlockId))) {
            cad::cmd::DartRelease r;
            r.blockId = mid;
            r.dartStartBlockId = b->dartStartBlockId;
            r.dartStartPointId = b->dartStartPointId;
            r.dartRefBlockId = b->dartRefBlockId;
            r.dartRefPointId = b->dartRefPointId;
            r.dartRefSegmentId = b->dartRefSegmentId;
            r.dartOffsetMm = b->dartOffsetMm;
            r.dartOffsetFormula = b->dartOffsetFormula;
            r.dartAngleDeg = b->dartAngleDeg;
            r.dartAngleFormula = b->dartAngleFormula;
            m_groupReleasedDarts.push_back(r);
            b->dartStartBlockId = QUuid();
            b->dartStartPointId = QUuid();
            b->dartRefBlockId = QUuid();
            b->dartRefPointId = QUuid();
            b->dartRefSegmentId = QUuid();
        }
    }
    m_groupReleasedHeld = true;
    m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();
}

void ToolRotate::restoreComponentExternal()
{
    if (!m_paramDoc || !m_groupReleasedHeld) return;
    if (!m_groupReleasedAtts.empty())
        m_paramDoc->addAttachmentsRaw(m_groupReleasedAtts);
    for (const auto& r : m_groupReleasedTargets) {
        if (auto* b = m_paramDoc->findBlock(r.blockId)) {
            b->endTargetBlockId = r.endTargetBlockId;
            b->endTargetPointId = r.endTargetPointId;
            b->endTargetOffset = r.endTargetOffset;
            b->endTargetOffsetFormula = r.endTargetOffsetFormula;
        }
    }
    for (const auto& r : m_groupReleasedDarts) {
        if (auto* b = m_paramDoc->findBlock(r.blockId)) {
            b->dartStartBlockId = r.dartStartBlockId;
            b->dartStartPointId = r.dartStartPointId;
            b->dartRefBlockId = r.dartRefBlockId;
            b->dartRefPointId = r.dartRefPointId;
            b->dartRefSegmentId = r.dartRefSegmentId;
            b->dartOffsetMm = r.dartOffsetMm;
            b->dartOffsetFormula = r.dartOffsetFormula;
            b->dartAngleDeg = r.dartAngleDeg;
            b->dartAngleFormula = r.dartAngleFormula;
        }
    }
    m_groupReleasedHeld = false;
    m_paramDoc->resolveAll();
}

QUuid ToolRotate::snapAnyPoint(const cad::geo::Vec2& worldPos) const
{
    if (!m_paramDoc) return QUuid();
    const double tol = 8.0 / currentZoom();
    QUuid best;
    double bestDist = tol;
    for (const auto& blk : m_paramDoc->blocks()) {
        for (const auto& pt : blk.points) {
            if (!pt.resolved || !pt.selectable) continue;
            const cad::geo::Vec2 w = blk.transform.toWorld(pt.resolvedPos);
            const double d = w.distanceTo(worldPos);
            if (d <= bestDist) { bestDist = d; best = pt.id; }
        }
    }
    return best;
}

void ToolRotate::updateGroupHighlightBox()
{
    if (!m_groupMode || !m_scene || !m_paramDoc || m_compId.isNull()) return;
    const cad::param::BBox box = m_paramDoc->boundingBoxOf(m_compId);
    if (!m_groupHighlightBox) {
        m_groupHighlightBox = new QGraphicsRectItem();
        QPen pen(QColor(72, 141, 255, 230));   // 选中色系 (与 CanvasStyle 同 token 来源)
        pen.setWidthF(1.5);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_groupHighlightBox->setPen(pen);
        m_groupHighlightBox->setBrush(Qt::NoBrush);
        m_groupHighlightBox->setZValue(90.0);
        m_groupHighlightBox->setFlag(QGraphicsItem::ItemIsSelectable, false);
        m_groupHighlightBox->setFlag(QGraphicsItem::ItemIsFocusable, false);
        m_scene->addItem(m_groupHighlightBox);
    }
    if (box.valid) {
        double zoom = currentZoom();
        if (zoom < 1e-9) zoom = 1.0;
        const double pad = 5.0 / zoom;
        m_groupHighlightBox->setRect(QRectF(
            QPointF(box.min.x - pad, -box.max.y - pad),
            QPointF(box.max.x + pad, -box.min.y + pad)));
        m_groupHighlightBox->show();
    } else {
        m_groupHighlightBox->hide();
    }
}

void ToolRotate::removeGroupHighlightBox()
{
    if (!m_groupHighlightBox) return;
    if (m_scene) m_scene->removeItem(m_groupHighlightBox);
    delete m_groupHighlightBox;
    m_groupHighlightBox = nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Rotate-copy gesture (Ctrl+drag 旋转复制)
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
        if (auto* a = editableAttachment()) {
            const double cur = currentAngleDeg();
            if (m_rotationMode == cad::param::RotationMode::ArcLength) {
                // Bake the CURRENT arc value (formula evaluated) — never
                // back-derive from the normalized display angle, which would
                // collapse multi-turn arcs to 0 (用户回归 2026-08).
                double arcMm = a->arcLength;
                if (!a->arcLengthFormula.isEmpty()) {
                    auto r = cad::param::ConditionEngine::evaluate(
                        a->arcLengthFormula, m_paramDoc->parameters(), {});
                    if (r.ok) arcMm = geo::Units::cmToMm(r.value);
                }
                a->arcLength = arcMm;
                a->arcLengthFormula.clear();
            } else {
                a->followerAngle = cur;
                a->followerAngleFormula.clear();
            }
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

// ── 角度显示约定（2026-08 v3 定稿，用户拍板）────────────────────────────
// 存储域 α ∈ [0, 360°)（Resolver/序列化零改动）；显示域分两种：
//  • 连接线（跟随角度/弧长）= 带符号折角 [−180°, +180°]：折叠 0°、
//    垂直 ±90°、开平 ±180°，符号 = 折向（α ≤ 180° → +α，否则 α−360°）。
//  • 自由线绝对角度 / 复制相对角度 = 0~360° 逆时针为正（行业默认，
//    AutoCAD 同）。
// 统一实现收口在 geometry/Angle.h（normalizeDeg360 / normalizeDeg180）与
// geometry/Units.h（formatDegValue），此处不再本地复制。

void ToolRotate::updateRotation(const cad::geo::Vec2& pos, bool snap)
{
    if (m_state != RotateState::Rotating) return;
    const cad::geo::Vec2 d = pos - m_pivot;
    const double theta = std::atan2(d.y, d.x);
    double delta = theta - m_dragCursorAngle0;
    // Normalise to (-π, π] so crossing the −X axis never causes a jump.
    delta = cad::geo::normalizeRad(delta);

    // 拖动约定（2026-08 v3 定稿）：全模式 WYSIWYG（线跟光标）。
    // 复制：相对角（逆时针为正）→ target = 起始 + 增量（复制中 connected 原线
    // 仍挂接，必须先判 copy 再判 connected）。
    // 自由线：显示 = 世界角（逆时针为正）→ target = 起始 + 增量。
    // 连接线：存储 α = 闭合基准角（世界角 = refWorld + π − α），线跟光标 →
    // α 随光标 CCW 减小；显示 = 带符号折角（表盘式：从折叠位逆时针拖折角
    // 增大 0→±180，过开平后回落另一侧）。跨 ±180° 缝先归一再包符号。
    double target;
    if (m_copyGesture->active()) {
        target = m_dragAngle0 + cad::geo::radToDeg(delta);
        if (snap) target = std::round(target / 15.0) * 15.0;
    } else if (m_connected) {
        const double alpha0 = cad::geo::normalizeDeg360(m_dragAngle0);
        double alpha = cad::geo::normalizeDeg360(alpha0 - cad::geo::radToDeg(delta));
        if (snap) alpha = std::round(alpha / 15.0) * 15.0;
        target = cad::geo::normalizeDeg180(alpha);
    } else {
        target = m_dragAngle0 + cad::geo::radToDeg(delta);
        if (snap) target = std::round(target / 15.0) * 15.0;
    }

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
    if (m_copyGesture && m_copyGesture->active()) {
        // Rotate-copy: deg is the angle RELATIVE to the original; the stored
        // follower angle adds the 180° start-point exit-direction offset
        // (relative 0° = overlap). Formula cleared by direct manipulation.
        m_copyGesture->applyAngle(deg);
        return;
    }
    if (m_connected) {
        if (auto* a = editableAttachment()) {
            // 输入 = 带符号折角（−180~+180，含折向）→ 存储 α ∈ [0, 360°)。
            const double alpha = cad::geo::normalizeDeg360(deg);
            if (m_rotationMode == cad::param::RotationMode::ArcLength) {
                // 弧长 = 线夹角恒等映射（2026-08 定稿）：弧长 0 = 0° 折叠、
                // πr = 180° 开平，与 Resolver 一致，不再反转。
                const double radius = segmentRadius();
                a->arcLength = alpha * M_PI / 180.0 * radius;
                a->arcLengthFormula.clear();
                a->rotationMode = cad::param::RotationMode::ArcLength;
            } else {
                a->followerAngle = alpha;
                a->followerAngleFormula.clear();   // direct manipulation overrides formula
            }
        }
    } else {
        cad::param::Block* blk = m_paramDoc->findBlock(m_blockId);
        if (!blk) return;
        // 自由线显示 = 绝对角度（0~360°，逆时针为正，行业默认；2026-08 v3
        // 定稿）。起点锚心 = 线方向、终点锚心 = 线方向+180°；存储旋转角 =
        // 显示角 − 锚偏移 − localDir；保持 ANCHOR 点钉在 pivot。
        const double anchorOffsetRad = m_anchorIsEnd ? M_PI : 0.0;
        const double newRot = deg * M_PI / 180.0
                              - anchorOffsetRad - m_localDir;
        const double deltaRot = newRot - m_baseTf.rotation;
        blk->transform.rotation = newRot;
        blk->transform.origin = m_pivot - m_anchorLocal.rotated(newRot);
    }
    // Per-frame hot path (旋转拖动每帧): resolve ONLY the rotated block's dirty
    // subgraph (followers/referencers via collectAffected) and sync the scene
    // cheaply — syncFromBlock rebuilds just the blocks whose rotation/epoch
    // changed. The old resolveAll() + refreshAllBlockItems() re-resolved the
    // WHOLE document and rebuilt EVERY block item on every frame.
    m_paramDoc->resolveForDrag(QList<QUuid>{m_blockId});
    if (m_scene) m_scene->syncBlockPositions();
}

void ToolRotate::applyModeValue(double value)
{
    if (m_rotationMode == cad::param::RotationMode::ArcLength && m_connected) {
        // Arc-length input stores the arc DIRECTLY (never round-trips through
        // the normalized angle, which would collapse multi-turn arcs to 0;
        // 用户回归 2026-08). Formula is cleared by direct manipulation.
        if (auto* a = editableAttachment()) {
            a->arcLength = geo::Units::cmToMm(value);
            a->arcLengthFormula.clear();
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
    if (const auto* a = editableAttachment())
        return blk->segmentLengthAtPoint(a->fromPointId);
    return 0.0;
}

double ToolRotate::currentModeValue() const
{
    if (m_rotationMode == cad::param::RotationMode::ArcLength && m_connected) {
        // 显示 = 带符号折角弧长（2026-08 v3 定稿）：先按恒等映射得 α
        // （多圈折叠回落到 0° 侧），包符号后换算弧长 cm。存储读原值
        // （公式实时求值），不做显示域回算 —— 防多圈爆表。
        const auto* a = editableAttachment();
        if (!a) return 0.0;
        double arcMm = a->arcLength;
        if (!a->arcLengthFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                a->arcLengthFormula, m_paramDoc->parameters(), {});
            if (r.ok) arcMm = geo::Units::cmToMm(r.value);
        }
        const double radius = segmentRadius();
        const double alphaDeg = (radius > 1e-9)
            ? (arcMm / radius) * 180.0 / M_PI : 0.0;
        const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
        return foldDeg * M_PI / 180.0 * radius * 0.1;   // mm → cm
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
        cad::param::Attachment* att = editableAttachment();
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
                m_paramDoc->addAttachmentRaw(m_releaseAttBackup);  // verbatim (keep snapshot isLocked)
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
        if (auto* a = editableAttachment()) {
            a->followerAngle = m_baseAngle;
            a->followerAngleFormula = m_baseFormula;
            a->rotationMode = m_rotationMode;
            a->arcLength = m_baseArcLength;
            a->arcLengthFormula = m_baseArcFormula;
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
        m_paramDoc->addAttachmentRaw(m_releaseAttBackup);  // verbatim (keep snapshot isLocked)
        m_releaseAttId = QUuid();
        m_releaseAttHeld = false;
    }
    m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();
}

double ToolRotate::currentAngleDeg() const
{
    if (!m_paramDoc) return 0.0;

    if (m_copyGesture && m_copyGesture->active())
        return m_copyGesture->currentRelativeAngle();

    if (m_connected) {
        const auto* a = editableAttachment();
        if (!a) return 0.0;
        if (a->rotationMode == cad::param::RotationMode::ArcLength) {
            // Arc-length mode: derive the effective angle from the arc.
            // 弧长 = 线夹角恒等映射（2026-08 定稿）：弧长 0 = 0° 折叠、
            // πr = 180° 开平，与 Resolver 一致，不再反转。显示 = 带符号
            // 折角（v3）：α 归一化 [0, 360°) 防多圈爆表后包符号。
            double arcMm = a->arcLength;
            if (!a->arcLengthFormula.isEmpty()) {
                auto r = cad::param::ConditionEngine::evaluate(
                    a->arcLengthFormula, m_paramDoc->parameters(), {});
                if (r.ok) arcMm = geo::Units::cmToMm(r.value);
            }
            const double radius = segmentRadius();
            double deg = (radius > 1e-9)
                ? (arcMm / radius) * 180.0 / M_PI : 0.0;
            return cad::geo::normalizeDeg180(deg);
        }
        if (!a->followerAngleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                a->followerAngleFormula, m_paramDoc->parameters(), {});
            if (r.ok) return cad::geo::normalizeDeg180(r.value);
        }
        // 存储 α ∈ [0, 360°) → 显示带符号折角（2026-08 v3 定稿）。
        return cad::geo::normalizeDeg180(a->followerAngle);
    }

    // Free: 绝对角度 = 世界角，逆时针为正（0° = 水平向右，行业默认，
    // 2026-08 v3 定稿）。起点锚心 = 线的世界方向；终点锚心 = 终点→起点
    // 方向 = 线方向+180°。统一语义：线总是“指向光标”，任一锚心拖拽
    // 手感一致（线跟随光标）。
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
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0) deg += 360.0;
    return deg;
}

bool ToolRotate::isAngleLocked() const
{
    // A rotate-copy clone carries no committed formula (unless the user typed
    // one into the HUD mid-gesture) — never lock the copy gesture.
    if (m_copyGesture && m_copyGesture->active()) return false;
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
    // 整组旋转恒为角度语义 (增量角) — 模式切换忽略并弹回 Angle.
    if (m_groupMode) {
        if (m_hud) m_hud->setMode(cad::param::RotationMode::Angle);
        return;
    }
    if (m_copyGesture && m_copyGesture->active()) {
        // Rotate-copy is always angle semantics (relative angle) — a mode
        // switch mid-gesture is ignored and the HUD snaps back to Angle.
        if (m_hud) m_hud->setMode(cad::param::RotationMode::Angle);
        return;
    }
    if (!m_paramDoc || !m_connected) return;
    // 公式驱动（角度/弧长表达式）：模式切换只是显示单位变化，绝不换算
    // 烘焙公式——表达式必须原样保留（用户要求）。公式存在时拒绝切换。
    const auto* a = editableAttachment();
    if (a) {
        const bool hasFormula =
            (m_rotationMode == cad::param::RotationMode::ArcLength)
                ? !a->arcLengthFormula.isEmpty()
                : !a->followerAngleFormula.isEmpty();
        if (hasFormula && newMode != m_rotationMode) {
            if (m_hud) m_hud->setMode(m_rotationMode);   // 弹回原模式
            return;
        }
    }
    // Geometry-preserving switch: convert current angle ↔ arc length.
    // 换算必须走存储域 α（显示 = 带符号折角，2026-08 v3 定稿）——否则
    // α > 180° 时会把负数折角烘焙进存储，几何静默翻转。
    const double deg = cad::geo::normalizeDeg360(currentAngleDeg());
    const double radius = segmentRadius();

    if (auto* a = editableAttachment()) {
        if (newMode == cad::param::RotationMode::ArcLength) {
            // Angle → ArcLength: preserve geometry. 弧长 = 线夹角恒等映射
            // （2026-08 定稿）：弧长角 = 显示角，不再反转。归一化 [0, 360°)。
            a->rotationMode = cad::param::RotationMode::ArcLength;
            a->arcLength = std::fmod(deg, 360.0) * M_PI / 180.0 * radius;
            a->arcLengthFormula.clear();
        } else {
            // ArcLength → Angle: preserve geometry.
            a->rotationMode = cad::param::RotationMode::Angle;
            a->followerAngle = deg;
            a->followerAngleFormula.clear();
        }
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
    // ── 组件整组旋转: caption「整组旋转」, 值 = 绕锚点增量角 (0 = 原始位姿) ──
    if (m_groupMode) {
        m_hud->setCaption(QString::fromUtf8("整组旋转"));
        m_hud->edit()->setText(cad::geo::Units::formatDegValue(m_groupDelta));
        m_hud->edit()->blockSignals(false);
        return;
    }
    // Caption follows the ACTIVE angle semantics (术语统一: 跟随角度 /
    // 绝对角度 / 相对角度): 跟随线 = 跟随角度/弧长(默认); 自由线 = 绝对角度;
    // 旋转复制 = 相对角度。setMode 只在模式切换时刷新标签，锚心切换
    // (Connected↔Free) 不切模式，所以这里每帧显式同步。
    if (m_copyGesture && m_copyGesture->active())
        m_hud->setCaption(QString::fromUtf8("旋转角度"));  // 绕锚心角, 0° = 重叠
    else if (!m_connected)
        m_hud->setCaption(QString::fromUtf8("绝对角度"));
    else
        m_hud->setCaption(QString());   // 默认: 跟随角度 / 弧长
    if (m_copyGesture && m_copyGesture->active()) {
        // Rotate-copy: pivot-relative angle (方案 B, 用户拍板 2026-08) —
        // 0° = 副本与父线重叠, 拖多少度 = 转多少度, 起点/终点锚心一致。
        m_hud->edit()->setText(cad::geo::Units::formatDegValue(
            m_copyGesture->currentRelativeAngle()));
    } else if (m_rotationMode == cad::param::RotationMode::ArcLength && m_connected) {
        // Arc-length mode: show the value in cm (formula-driven values are
        // evaluated live — the HUD always shows a plain editable number).
        m_hud->edit()->setText(cad::geo::Units::formatDegValue(currentModeValue()));
    } else {
        // Angle mode: degrees (formula evaluated live, same idea).
        m_hud->edit()->setText(cad::geo::Units::formatDegValue(currentAngleDeg()));
    }
    m_hud->edit()->blockSignals(false);
}

void ToolRotate::onHudTextChanged(const QString& text)
{
    if (!m_paramDoc) return;
    // 旋转复制提交/取消后 HUD 已隐藏（副本编辑语义终结）；隐藏期间忽略
    // 任何输入——防止输入框目标悄然切回原线角度造成“幽灵编辑”。
    if (!m_hud || !m_hud->isVisible()) return;
    const QString t = text.trimmed();

    // ── 组件整组旋转 HUD: 数值 = 绕锚点增量角 (键入即预览, Enter 提交) ──
    if (m_groupMode) {
        if (t.isEmpty()) {
            // 空 = 回到基准位姿 (释放约束一并恢复).
            for (auto it = m_groupBaseTf.cbegin(); it != m_groupBaseTf.cend(); ++it) {
                if (auto* b = m_paramDoc->findBlock(it.key()))
                    b->transform = it.value();
            }
            restoreComponentExternal();
            m_groupDelta = 0.0;
            m_hudValid = true;
            if (m_scene) m_scene->refreshAllBlockItems();
        } else {
            bool isNumber = false;
            const double v = t.toDouble(&isNumber);
            if (isNumber) {
                m_hudValid = true;
                if (!m_groupReleasedHeld) collectAndReleaseComponentExternal();
                applyGroupDelta(v);
            } else {
                auto r = cad::param::ConditionEngine::evaluate(
                    t, m_paramDoc->parameters(), m_paramDoc->conditions());
                if (r.ok) {
                    m_hudValid = true;
                    if (!m_groupReleasedHeld) collectAndReleaseComponentExternal();
                    applyGroupDelta(r.value);
                } else {
                    m_hudValid = false;   // 保留最后一次有效几何
                }
            }
        }
        if (m_hud) m_hud->setValid(m_hudValid);
        if (m_hudValid) updateGizmo();
        return;
    }

    if (t.isEmpty()) {
        if (m_copyGesture && m_copyGesture->active()) {
            m_copyGesture->applyAngle(0.0);   // empty = revert the clone onto the original
        } else {
            restoreBase();        // empty = revert to the session base
        }
        m_hudValid = true;
    } else {
        bool isNumber = false;
        const double numVal = t.toDouble(&isNumber);
        if (isNumber) {
            // Rotate-copy: HUD value IS the pivot-relative angle (方案 B),
            // applied directly (副本自动去公式同规则).
            if (m_copyGesture && m_copyGesture->active())
                m_copyGesture->applyAngle(numVal);
            else
                applyModeValue(numVal);
            m_hudValid = true;
        } else {
            auto r = cad::param::ConditionEngine::evaluate(
                t, m_paramDoc->parameters(), {});
            if (r.ok) {
                if (m_copyGesture && m_copyGesture->active()) {
                    // Formula typed into the copy HUD: the clone never keeps
                    // a formula (用户拍板: 副本自动去公式) — apply the value
                    // as a pivot-relative angle.
                    m_copyGesture->applyFormulaValue(r.value);
                } else if (m_connected) {
                    // Persist the formula (the resolver lets the formula win).
                    if (auto* a = editableAttachment()) {
                        if (m_rotationMode == cad::param::RotationMode::ArcLength) {
                            a->arcLength = geo::Units::cmToMm(r.value);
                            a->arcLengthFormula = t;
                        } else {
                            a->followerAngle = r.value;
                            a->followerAngleFormula = t;
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
    if (m_copyGesture && m_copyGesture->active()) {
        m_copyGesture->commit();
    } else if (m_groupMode) {
        if (!m_groupPivotSet) {
            if (m_scene)
                m_scene->showToast(QString::fromUtf8("请先在画布上点一下设定锚点"));
            return;
        }
        commitGroupRotation();
    } else {
        commitCurrent();
    }
}

void ToolRotate::onHudCancel()
{
    if (m_copyGesture && m_copyGesture->active()) {
        m_copyGesture->cancel();    // drop the preview clone, keep the target
    } else if (m_groupMode && m_state == RotateState::Rotating) {
        cancelGroupRotation();      // 中止整组拖动, 恢复基准位姿 + 约束
    } else if (m_groupMode && m_state == RotateState::Ready) {
        // 第一击设锚点"点错了"：Esc 先清锚点重选 (仍留整组模式);
        // 无锚点按下 Esc 才退出整组模式回单线 (保留目标).
        if (m_groupPivotSet) {
            m_groupPivotSet = false;
            m_groupDelta = 0.0;
            m_groupPivotPointId = QUuid();
            removeGizmo();
            refreshHudText();
        } else {
            exitGroupMode();
        }
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
    if (m_copyGesture && m_copyGesture->active())
        // Copy mode: HUD 角度 = 相对角（0° = 重叠，逆时针为正，v3），
        // 直接交给手势层换算世界方向。
        worldDirRad = m_copyGesture->relToWorldRad(angleDeg);
    else if (m_connected)
        // 闭合基准（2026-08 定稿）：世界角 = refWorld + π − α（镜像
        // 修正：旧代码 refWorld + α 只在 90° 时正确）。输入 = 带符号
        // 折角，先回存储域 α（v3）。
        worldDirRad = m_refWorldRad + M_PI
                      - cad::geo::normalizeDeg360(angleDeg) * M_PI / 180.0;
    else
        // Free: 显示角 = 远端方向（起点锚心 = 线方向，终点锚心 = 终点→
        // 起点方向）；绝对角度逆时针为正（v3），无镜像。
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

            // Current world direction of the rotating FREE END (the direction
            // the far end points from the pivot — for both anchors this IS
            // the display angle: 起点锚心 = 线方向, 终点锚心 = 线方向+180°；
            // 旧代码终点锚心比较线方向导致永远不吸附).
            double worldDirRad;
            if (m_copyGesture && m_copyGesture->active())
                worldDirRad = m_copyGesture->relToWorldRad(angleDeg);
            else if (m_connected)
                // 闭合基准（2026-08 定稿）：世界角 = refWorld + π − α。
                worldDirRad = m_refWorldRad + M_PI
                              - cad::geo::normalizeDeg360(angleDeg) * M_PI / 180.0;
            else
                worldDirRad = angleDeg * M_PI / 180.0;

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

    // Snap the angle so the free end aims exactly at the candidate.
    const cad::geo::Vec2 aim = bestPos - m_pivot;
    const double dirToP = std::atan2(aim.y, aim.x);
    if (m_copyGesture && m_copyGesture->active()) {
        double rel = cad::geo::normalizeDeg360(m_copyGesture->worldRadToRel(dirToP));
        angleDeg = rel;
    } else if (m_connected) {
        // 闭合基准镜像（2026-08 定稿）：α = (refWorld + π − dirToP) 转角度，
        // 显示 = 带符号折角（v3）。
        angleDeg = cad::geo::normalizeDeg180(
            (m_refWorldRad + M_PI - dirToP) * 180.0 / M_PI);
    } else {
        // Free: 显示 = 远端方向（绝对角度，逆时针为正，v3）。
        angleDeg = std::fmod(dirToP * 180.0 / M_PI, 360.0);
        if (angleDeg < 0.0) angleDeg += 360.0;
    }

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
    if (m_gizmo)
        m_gizmo->build(m_pivot, m_refWorldRad, currentZoom());
}

double ToolRotate::originalWorldRotDeg() const
{
    // 原线绕锚心的当前世界朝向（旋转复制基准：副本 0° = 与原线重叠）：
    // 连接线 = refWorld + π − α（闭合基准，α 取 live 存储值）；自由线 =
    // 首段世界方向，终点锚心 + 180°。
    if (m_connected) {
        double alpha = m_baseAngle;
        if (m_paramDoc) {
            if (const auto* a = editableAttachment())
                alpha = a->followerAngle;
        }
        return (m_refWorldRad + M_PI - alpha * M_PI / 180.0) * 180.0 / M_PI;
    }
    double baseDeg = 0.0;
    if (m_paramDoc) {
        if (const auto* blk = m_paramDoc->findBlock(m_blockId);
            blk && !blk->segments.empty()) {
            const auto& seg = blk->segments.front();
            const auto* sp = blk->findPoint(seg.startPointId);
            const auto* ep = blk->findPoint(seg.endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                const cad::geo::Vec2 wd =
                    blk->transform.toWorld(ep->resolvedPos)
                    - blk->transform.toWorld(sp->resolvedPos);
                baseDeg = wd.angle() * 180.0 / M_PI;
            }
        }
    }
    if (m_anchorIsEnd) baseDeg += 180.0;
    return baseDeg;
}

void ToolRotate::updateGizmo()
{
    if (!m_gizmo) return;
    // ── 组件整组旋转: 基准 = 拖起始方向 (pivot→按下点), 弧 = 0 → delta ──
    if (m_groupMode) {
        if (!m_groupPivotSet) return;   // 锚点未定: 不画弧
        const double dashRad = m_groupBaselineRad;
        double arcEnd = dashRad + m_groupDelta * M_PI / 180.0;
        double span = arcEnd - dashRad;
        while (span >  M_PI) span -= 2.0 * M_PI;
        while (span < -M_PI) span += 2.0 * M_PI;
        arcEnd = dashRad + span;
        m_gizmo->update(currentZoom(), dashRad, dashRad, arcEnd);
        return;
    }
    const double deg = currentAngleDeg();
    // 黄弧永远画“真实夹角”（2026-08 用户拍板，可去掉黄弧上的角度文字）：
    // 连接线 = 折线 → 直行延续方向（跨度 = 折角 α，含 α > 180° 的 CW 侧）；
    // 自由线 = 0° → 显示角；复制 = 原线 → 克隆线（基准 = 原线世界方向，
    // span 归一化到 (−180°, +180°] 取内弧）。
    double dashRad = m_refWorldRad;
    double arcStart;
    double arcEnd;
    if (m_copyGesture && m_copyGesture->active()) {
        const double origRad = std::fmod(originalWorldRotDeg(), 360.0) * M_PI / 180.0;
        dashRad = origRad;                       // 灰虚线随原线重定位
        arcStart = origRad;
        arcEnd = m_copyGesture->currentWorldRad();
        double span = arcEnd - arcStart;
        while (span >  M_PI) span -= 2.0 * M_PI;
        while (span < -M_PI) span += 2.0 * M_PI;
        arcEnd = arcStart + span;                // 内弧
    } else if (m_connected) {
        const double aRad = cad::geo::normalizeDeg360(deg) * M_PI / 180.0;
        arcStart = m_refWorldRad + M_PI - aRad;  // 折线方向
        arcEnd = m_refWorldRad + M_PI;           // 直行延续方向
        double span = arcEnd - arcStart;         // = ±α
        while (span >  M_PI) span -= 2.0 * M_PI;
        while (span < -M_PI) span += 2.0 * M_PI;
        arcEnd = arcStart + span;                // α > 180° 取 CW 短侧
    } else {
        arcStart = 0.0;
        arcEnd = deg * M_PI / 180.0;
    }
    m_gizmo->update(currentZoom(), dashRad, arcStart, arcEnd);
}

void ToolRotate::removeGizmo()
{
    if (m_gizmo) m_gizmo->remove();
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
