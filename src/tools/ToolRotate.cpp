#include "ToolRotate.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QGraphicsEllipseItem>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QPen>
#include "ElaLineEdit.h"
#include <QWidget>
#include <QScrollBar>
#include <QObject>   // connect/disconnect (M8 视图变换重定位连接)

#include <cmath>
#include <limits>
#include <algorithm>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "HitTester.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Attachment.h"
#include "parametric/Duplicate.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "ui/AngleHud.h"
#include "RotateCopyGesture.h"
#include "RotateGizmo.h"
#include "ui/LinePropertyDialog.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockCommands.h"
#include "parametric/ParamDocumentRaw.h"

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
    delete m_angleHud;
}

ToolDescriptor ToolRotate::describe()
{
    ToolDescriptor d;
    d.id = ToolType::Rotate;
    d.displayName = QString::fromUtf8("旋转(&R)");
    d.iconName = QStringLiteral("rotate");
    d.shortcut = QKeySequence(Qt::Key_R);
    // H2: 提示必须描述确认门 (选中 ≠ 可直接拖), 旧文案描述的是废弃行为。
    d.hintText = QString::fromUtf8("旋转：点击线段选中 | 右键或回车确认 | 拖动旋转(Shift吸附15°) | HUD输入角度/公式 | X切换锚心 | Esc反悔");
    d.factory = [] { return std::make_unique<ToolRotate>(); };
    return d;
}

void ToolRotate::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)paramDoc;
    m_state = RotateState::Idle;
    // P2/L5 常驻实例: 每次进入回到角度模式 (旧"销毁重建"即此语义)。
    m_rotationMode = cad::param::RotationMode::Angle;
    // (Re)create the rotate-copy gesture with the current context.
    delete m_copyGesture;
    m_copyGesture = new RotateCopyGesture(this);
    // (Re)create the protractor gizmo renderer (scene-bound).
    delete m_gizmo;
    m_gizmo = new RotateGizmo(&scene);
}

void ToolRotate::onDeactivate()
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
        cad::param::RawModelAccess::addAttachmentRaw(*m_paramDoc, m_releaseAttBackup);  // verbatim (keep snapshot isLocked)
        m_paramDoc->resolveAll();
        m_releaseAttId = QUuid();
        m_releaseAttHeld = false;
    }
    removeGizmo();
    removeSelHighlightBox();
    // M8: 断开视图变换 → HUD 重定位连接 (ToolRotate 常驻, 防悬垂 lambda)。
    // ToolRotate 非 QObject, 裸 connect/disconnect 不可用 —— 用限定名。
    QObject::disconnect(m_viewZoomConn);
    QObject::disconnect(m_viewHScrollConn);
    QObject::disconnect(m_viewVScrollConn);
    if (m_angleHud) { m_angleHud->hide(); delete m_angleHud; m_angleHud = nullptr; }
    m_managed.release(m_aimRing);   // 释放 + 影子置空 + 撤销登记 (P1/L1)
    m_blockId = QUuid();
    m_attId = QUuid();
    m_connected = false;
    m_anchorPointId = QUuid();
    m_state = RotateState::Idle;
    // 选集旋转会话重置 (无未提交残差: 提交外的一切出口都当场回滚).
    m_selectionRotate = false;
    m_selIds.clear();
    m_shadowAtts.clear();
    m_shadowBase.clear();
    m_selPivotSet = false;
    m_selPivotPointId = QUuid();
    m_selDelta = 0.0;
    m_selBaseTf.clear();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mouse input
// ═══════════════════════════════════════════════════════════════════════════════

void ToolRotate::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    const cad::geo::Vec2 pos(event->scenePos().x(), event->scenePos().y());

    if (event->button() == Qt::RightButton) {
        if (m_state == RotateState::Rotating) {
            cancelRotation();
            applySelectionConfirmed(false);  // 拖中取消 = 回位并退出确定态 (D15)
        }
        // 单线确认门 (D15): 选中态右键 = 确认; 确定态右键 = 反悔回选中态。
        // 选集旋转会话 / 空闲路径 = 清目标 (选集会话的 m_blockId 恒为空)。
        if (m_state == RotateState::Ready && !m_blockId.isNull())
            applySelectionConfirmed(!m_selectionConfirmed);
        else
            clearTarget();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    switch (m_state) {
    case RotateState::Idle: {
        const QUuid id = hitBlock(pos);
        if (!id.isNull()) {
            selectTarget(id, pos);
            // Ctrl+press selects AND starts a rotate-copy in one gesture
            // (Ctrl = 复制意图, same language as ToolSelect).
            if (event->modifiers() & Qt::ControlModifier)
                m_copyGesture->begin(pos);
        }
        break;
    }
    case RotateState::Ready: {
        // ── 选集旋转 (选区继承): 一切按压 = 锚点/旋转, 不切换目标
        // (点别的线只是锚点所在地, 主流 CAD 语义).
        if (m_selectionRotate) {
            if (!m_selPivotSet)
                beginSelPivot(pos);
            else
                beginSelRotation(pos);
            break;
        }
        // ── 单线确认门 (D15, 用户拍板 2026-08-27): 已选未确认 = 不可拖动.
        if (!m_selectionConfirmed) {
            // 端点优先判定 (距离法, 同旧注释: BlockItem::shape() 对线段
            // 两端不可靠): 点目标线的另一端 = 切换锚向 (选中态专属;
            // 连接线端点被占用时仍禁切 —— 用户拍板 2026-08).
            const QUuid hitEnd = anchorPointAt(pos);
            bool endpointSwitched = false;
            if (!hitEnd.isNull() && hitEnd != m_anchorPointId) {
                bool anchorLocked = false;
                if (const auto* blk = m_paramDoc->findBlock(m_blockId);
                    blk && !blk->segments.empty()) {
                    anchorLocked =
                        attachmentAtPoint(blk->segments.front().startPointId)
                        || attachmentAtPoint(blk->segments.front().endPointId);
                }
                if (!anchorLocked) {
                    commitCurrent();
                    m_anchorIsEnd = false;
                    if (const auto* blk = m_paramDoc->findBlock(m_blockId);
                        blk && !blk->segments.empty()) {
                        m_anchorIsEnd = (hitEnd == blk->segments.front().endPointId);
                    }
                    rebuildAnchorState();
                    removeGizmo();
                    buildGizmo();
                    updateGizmo();
                    showHud();
                    endpointSwitched = true;
                }
            }
            if (endpointSwitched) break;

            // 点别的线 = 切换目标 (停留选中态).
            const QUuid self = hitBlock(pos);
            if (!self.isNull() && self != m_blockId) {
                commitCurrent();
                selectTarget(self, pos);     // 内部重置确认门
                break;
            }
            // 其余按压 (本线身/被禁的端点/空白) = no-op 或取消选择:
            // 空白 = 取消选择回 Idle; 本线身 = 保持选中.
            const auto* tb = m_paramDoc ? m_paramDoc->findBlock(m_blockId) : nullptr;
            if (!tb) { clearTarget(); break; }
            if (self.isNull()) clearTarget();
            break;
        }
        // ── 确定态: 一切按压 = 拖动起手; Ctrl = 旋转复制起手.
        const QUuid id = hitBlock(pos);
        const bool ctrl = (event->modifiers() & Qt::ControlModifier);
        if (ctrl) {
            if (id.isNull()) return;
            if (id != m_blockId) {
                commitCurrent();
                selectTarget(id, pos);  // 切目标即落回未确认, Ctrl 失效为普通选中
                break;
            }
            m_copyGesture->begin(pos);
            break;
        }
        beginRotation(pos);
        break;
    }
    case RotateState::Rotating:
        break;  // already rotating.
    }
}

void ToolRotate::mouseMove(QGraphicsSceneMouseEvent* event)
{
    // 选集旋转: 拖动帧直通选集分支 (单线的 Ctrl 转换/吸附逻辑不适用).
    if (m_selectionRotate) {
        if (m_state == RotateState::Rotating) {
            const cad::geo::Vec2 gp(event->scenePos().x(), event->scenePos().y());
            updateSelRotation(gp, event->modifiers() & Qt::ShiftModifier);
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
    else if (m_selectionRotate)    commitSelRotation();
    else                           commitRotation();
    // 拖动提交 = 结束确定态 (D15): 回落选中态, 再次拖动需重新确认.
    applySelectionConfirmed(false);
}

void ToolRotate::mouseDoubleClick(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;

    // A double-click is preceded by two press/release pairs; the second press
    // may have started a rotation — cancel it and drop the target before
    // opening the property dialog.
    if (m_state == RotateState::Rotating) {
        if (m_selectionRotate) cancelSelRotation();
        else                   cancelRotation();
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
    auto* dlg = new cad::ui::LinePropertyDialog(blockId, bestSegId, m_paramDoc,
                                       m_scene, parentWidget);
    // NOTE: no WA_DeleteOnClose — see ToolSelect::openLineProperty.
    dlg->show();
}

void ToolRotate::keyPress(QKeyEvent* event)
{
    // Fallback path: the key reached the view instead of the HUD widget.
    if (event->key() == Qt::Key_Escape) {
        // D15 Esc 分层: 确定态反悔 = 退回选中态 (轻一步可反悔);
        // 其余 (拖动中取消 / 选中态清目标 / HUD 放弃编辑) 走既有链.
        if (m_state == RotateState::Ready && m_selectionConfirmed) {
            applySelectionConfirmed(false);
            event->accept();
            return;
        }
        onHudCancel();
        event->accept();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        // D15 回车双重身份仲裁: 焦点在输入框 = 应用角度值; 否则选中态 =
        // 确认选择. (确定态回车 no-op —— 无未提交内容时不重复确认.)
        const bool hudFocused = m_angleHud && m_angleHud->edit() && m_angleHud->edit()->hasFocus();
        if (!hudFocused && m_state == RotateState::Ready
            && !m_selectionRotate && !m_selectionConfirmed) {
            applySelectionConfirmed(true);
            event->accept();
            return;
        }
        onHudCommit();
        event->accept();
    } else if (event->key() == Qt::Key_X) {
        // X: toggle the anchor between the start and end points (锚心切换).
        // 选集旋转会话与确定态下不适用 —— 锚向切换是选中态专属动作.
        if (!m_selectionRotate && !m_selectionConfirmed) toggleAnchor();
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
    return m_paramDoc ? m_paramDoc->attachmentsView().byId(m_attId) : nullptr;
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

void ToolRotate::selectTarget(const QUuid& blockId,
                              const std::optional<cad::geo::Vec2>& clickWorld)
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
    // 锚心初值 (用户拍板 2026-08-27): 跟随点击端 ——
    //   · 连接线: 锚恒取挂连接的一端 (选中即入"编辑跟随角"安全模式, 杜绝
    //     "点空闲端 → 旋转即放弃跟随"误路径; 跟随保护 2026-08 同源);
    //   · 自由线: 取离点击更近的一端 (点击必在线身上, 无距离阈值 —— 点哪
    //     半段锚就在那半段的端点; 平局取起点).
    m_anchorIsEnd = false;
    if (!blk->segments.empty()) {
        const cad::param::Segment& seg0 = blk->segments.front();
        const bool attAtStart =
            attachmentAtPoint(seg0.startPointId) != nullptr;
        const bool attAtEnd = attachmentAtPoint(seg0.endPointId) != nullptr;
        if (attAtEnd && !attAtStart) {
            m_anchorIsEnd = true;
        } else if (!attAtStart && !attAtEnd && clickWorld.has_value()) {
            const auto* sp = blk->findPoint(seg0.startPointId);
            const auto* ep = blk->findPoint(seg0.endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                const double dS =
                    blk->worldPos(seg0.startPointId).distanceTo(*clickWorld);
                const double dE =
                    blk->worldPos(seg0.endPointId).distanceTo(*clickWorld);
                m_anchorIsEnd = (dE < dS);
            }
        }
    }
    // N3 (TOOL_SYSTEM_AUDIT 复核 2026-08-29): 改走统一翻转入口 —— 标志 +
    // gizmo 样式 + HUD caption 三同步, 不让"确认态视觉"在任何路径上残留。
    // (此处 m_gizmo 可能是上一会话残留 / 尚未 buildGizmo, updateGizmo 与
    //  refreshHudText 都有空守卫; 后面的 buildGizmo→updateGizmo→showHud
    //  会再刷一次, 结果一致。)
    applySelectionConfirmed(false);   // D15: 新目标从选中态起步, 需确认才可拖
    m_releaseAttId = QUuid();
    m_releaseAttHeld = false;
    // 选集旋转会话防漏清 (selectTarget 只会从 Idle/单线会话进入, 此处兜底).
    m_selectionRotate = false;
    m_selIds.clear();
    m_shadowAtts.clear();
    m_shadowBase.clear();
    m_selPivotSet = false;
    m_selPivotPointId = QUuid();
    m_selDelta = 0.0;
    m_selBaseTf.clear();
    removeSelHighlightBox();
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
    removeSelHighlightBox();
    m_blockId = QUuid();
    m_attId = QUuid();
    m_connected = false;
    m_anchorPointId = QUuid();
    // N3: 同 selectTarget —— 走统一翻转入口 (gizmo 已 remove 但未销毁,
    // HUD 已 hide 但未销毁, 两处刷新都有空守卫且对隐藏对象无害)。
    applySelectionConfirmed(false);   // D15
    m_releaseAttId = QUuid();
    m_releaseAttHeld = false;
    // 选集旋转会话重置.
    m_selectionRotate = false;
    m_selIds.clear();
    m_shadowAtts.clear();
    m_shadowBase.clear();
    m_selPivotSet = false;
    m_selPivotPointId = QUuid();
    m_selDelta = 0.0;
    m_selBaseTf.clear();
    m_state = RotateState::Idle;
    if (m_scene) m_scene->refreshAllBlockItems();
}


// ═══════════════════════════════════════════════════════════════════════════════
// 选集旋转 (选区继承 adoptSelection, 2026-08-27 泛化)
// 原「W 键组件整组旋转」模态已删除 (2026-08-29 用户拍板, 执行
// ROTATE_REDESIGN_DESIGN.md D1) —— 组件整体旋转改由 多选 → R 选区继承承担。
// 设计要点: 目标 = 选集 S; 锚点 = 任意点 (第一击设锚, 第二击起手拖动);
// S 全体绕锚点刚体变换; 影子偏转 (§2.6) 逐帧回写; 锚点不存储 (会话态).
// ═══════════════════════════════════════════════════════════════════════════════

void ToolRotate::adoptSelection(const QList<QUuid>& blockIds)
{
    // 选区继承 (D9): 选择工具框好的集合直接成为旋转选集, 跳过拾取进入锚点
    // 阶段。
    if (!m_paramDoc) return;
    QList<QUuid> ids;
    QHash<QUuid, cad::param::Transform2D> base;
    for (const QUuid& id : blockIds) {
        if (const auto* b = m_paramDoc->findBlock(id)) {
            if (b->isBridge) continue;            // 桥线拒入 (D10)
            base.insert(id, b->transform);
            ids << id;
        }
    }
    if (ids.isEmpty()) {
        if (m_scene)
            m_scene->showToast(QString::fromUtf8("选区为空，无法旋转"));
        return;
    }
    clearTarget();                   // 继承路径不拾取单线目标
    hideHud();
    m_selectionRotate = true;        // 选集会话: 两段式锚点/拖动交互
    m_selPivotSet = false;
    m_selPivotPointId = QUuid();
    m_selDelta = 0.0;
    m_selBaselineRad = 0.0;
    m_shadowAtts.clear();
    m_shadowBase.clear();
    m_selIds = ids;
    m_selBaseTf = base;
    m_state = RotateState::Ready;   // 选集 = 已确认目标, 直接进锚点阶段
    removeGizmo();
    updateSelHighlightBox();
    showHud();
}

void ToolRotate::collectShadowAttachments()
{
    // 影子会话收集 (§2.6, 用户拍板 2026-08-27): 凡 follower ∈ S、而角度基准
    // 方向在 S 外的活跃连接 —— 含 angleRef 显式外指 (B-C-A 场景) 与已拆开
    // 保留角 (angleOnly) 的跨界跟随 —— 记录 id 并快照旧 offset。提交时
    // new offset = base + δ 进命令; Esc 回写 base; 组内互连不收 (基准随组转,
    // 刚体平账)。滑轨连接照常收: 影子只影响驱动旋转的 refWorld。
    m_shadowAtts.clear();
    m_shadowBase.clear();
    if (!m_paramDoc) return;
    const QSet<QUuid> inS(m_selIds.cbegin(), m_selIds.cend());
    for (const auto& a : m_paramDoc->attachments()) {
        if (!inS.contains(a.fromBlockId)) continue;
        if (a.rotationMode != cad::param::RotationMode::Angle
            && a.rotationMode != cad::param::RotationMode::ArcLength)
            continue;
        if (a.angleIndependent) continue;       // 角度独立: 不被基准驱动
        if (a.isPin) continue;                  // 纯位置钉: 无角度驱动
        const QUuid refBlk = !a.angleRefBlockId.isNull() ? a.angleRefBlockId : a.toBlockId;
        if (refBlk.isNull() || inS.contains(refBlk)) continue;
        m_shadowAtts << a.id;
        m_shadowBase.insert(a.id, a.baselineOffsetDeg);
    }
}

void ToolRotate::beginSelPivot(const cad::geo::Vec2& pos)
{
    if (m_state != RotateState::Ready || !m_selectionRotate || !m_paramDoc) return;
    m_selPivotPointId = snapAnyPoint(pos);
    if (!m_selPivotPointId.isNull()) {
        // 吸附: 世界位置 = 命中点当前解析位.
        bool found = false;
        for (const auto& blk : m_paramDoc->blocks()) {
            for (const auto& pt : blk.points) {
                if (pt.id == m_selPivotPointId && pt.resolved) {
                    m_selPivot = blk.transform.toWorld(pt.resolvedPos);
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) m_selPivot = pos;   // 命中点在拖动间已消失: 回退自由锚点
    } else {
        m_selPivot = pos;               // 自由锚点 (任意位置)
    }
    m_selPivotSet = true;
    m_selDelta = 0.0;
    // 锚心环就位 (gizmo 复用单线态 pivot/ref 字段 — 选集模式不触碰单线逻辑).
    m_pivot = m_selPivot;
    m_refWorldRad = 0.0;
    removeGizmo();
    buildGizmo();
    updateGizmo();
    showHud();                           // HUD 移到锚点附近
}

void ToolRotate::beginSelRotation(const cad::geo::Vec2& pos)
{
    if (m_state != RotateState::Ready || !m_selectionRotate || !m_selPivotSet) return;
    // 影子会话 (§2.6): 收集"基准在 S 外"的连接, 逐帧回写 base+δ.
    collectShadowAttachments();
    m_state = RotateState::Rotating;
    const cad::geo::Vec2 d = pos - m_selPivot;
    m_selBaselineRad = (d.lengthSquared() > 1e-12) ? std::atan2(d.y, d.x) : 0.0;
    m_selDelta = 0.0;
    showHud();
}

void ToolRotate::updateSelRotation(const cad::geo::Vec2& pos, bool snap)
{
    if (m_state != RotateState::Rotating || !m_selectionRotate) return;
    const cad::geo::Vec2 d = pos - m_selPivot;
    const double theta = std::atan2(d.y, d.x);
    double delta = cad::geo::normalizeRad(theta - m_selBaselineRad);
    double deg = cad::geo::radToDeg(delta);
    if (snap) deg = std::round(deg / 15.0) * 15.0;
    applySelDelta(deg);
    updateGizmo();
    refreshHudText();
}

void ToolRotate::applySelDelta(double deg)
{
    if (!m_paramDoc || m_selIds.isEmpty()) return;
    const double deltaRad = deg * M_PI / 180.0;
    m_selDelta = deg;
    // 每帧热路径铁律 (2026-09 收敛): resolveForDrag + syncBlockPositions,
    // 禁止 resolveAll/refreshAllBlockItems. seeds = 选集 S 全体.
    QList<QUuid> seeds;
    for (const QUuid& mid : m_selIds) {
        auto* b = m_paramDoc->findBlock(mid);
        if (!b) continue;
        const auto it = m_selBaseTf.constFind(mid);
        if (it == m_selBaseTf.constEnd()) continue;
        cad::param::Transform2D nf = it.value();
        nf.rotation += deltaRad;
        nf.origin = m_selPivot + (nf.origin - m_selPivot).rotated(deltaRad);
        b->transform = nf;
        seeds << mid;
        m_paramDoc->invalidateLayer(b->layer);
    }
    // 影子偏转逐帧回写 (base+δ 非累加, 防浮点漂移; §2.6 实现要点):
    // 与 transform 同帧写入, 否则拖动中基准在外面的跟随线被解算器拽住抽搐.
    if (!m_shadowAtts.isEmpty()) {
        QHash<QUuid, double> offsets;
        for (const QUuid& attId : m_shadowAtts)
            offsets.insert(attId, m_shadowBase.value(attId) + deg);
        m_paramDoc->updateBaselineOffsets(offsets);
    }
    if (seeds.isEmpty()) return;
    m_paramDoc->resolveForDrag(seeds);
    if (m_scene) m_scene->syncBlockPositions();
    updateSelHighlightBox();
}

void ToolRotate::commitSelRotation()
{
    // 选集旋转提交 (2026-08-27 泛化): S = 任意块集, 影子偏转 + 位姿一步 undo.
    if (!m_paramDoc || !m_undoStack || m_selIds.isEmpty()) {
        if (m_state == RotateState::Rotating) m_state = RotateState::Ready;
        return;
    }
    // 1) 捕获终态位姿 + 影子终值 (恢复前!).
    QHash<QUuid, cad::param::Transform2D> newTf;
    bool moved = false;
    for (const QUuid& mid : m_selIds) {
        const auto* b = m_paramDoc->findBlock(mid);
        const auto it = m_selBaseTf.constFind(mid);
        if (!b || it == m_selBaseTf.constEnd()) continue;
        newTf.insert(mid, b->transform);
        const double dRot = std::abs(b->transform.rotation - it.value().rotation);
        const double dOrg = b->transform.origin.distanceTo(it.value().origin);
        if (dRot > 1e-9 || dOrg > 1e-6) moved = true;
    }
    for (const QUuid& attId : m_shadowAtts)
        moved = moved
            || std::abs(m_selDelta) > 1e-9;   // 影子会话只有伴随真实增量才计
    // (选集路径无结构释放; 空手势在此即被吞掉, 不产生空命令.)

    // 2) Restore-then-replay: 回基准现场 (位姿 + offset=base), 推命令重放.
    for (auto it = m_selBaseTf.cbegin(); it != m_selBaseTf.cend(); ++it) {
        if (auto* b = m_paramDoc->findBlock(it.key()))
            b->transform = it.value();
    }
    if (!m_shadowAtts.isEmpty()) {
        QHash<QUuid, double> baseOffsets;
        for (const QUuid& attId : m_shadowAtts)
            baseOffsets.insert(attId, m_shadowBase.value(attId));
        m_paramDoc->updateBaselineOffsets(baseOffsets);
    }
    m_state = RotateState::Ready;
    if (!moved) {
        m_selDelta = 0.0;
        updateGizmo();
        refreshHudText();
        return;
    }
    std::vector<cad::cmd::RotateBlocksCommand::ShadowAtt> shadows;
    for (const QUuid& attId : m_shadowAtts) {
        const auto* a = m_paramDoc->attachmentsView().byId(attId);
        if (!a) continue;
        cad::cmd::RotateBlocksCommand::ShadowAtt s;
        s.attId = attId;
        s.demoted = *a;                          // 基准现场 = 旋转前的原样连接
        s.oldOffset = m_shadowBase.value(attId);
        s.newOffset = s.oldOffset + m_selDelta;
        shadows.push_back(s);
    }
    m_undoStack->push(new cad::cmd::RotateBlocksCommand(
        m_paramDoc, m_selBaseTf, newTf, shadows,
        {}, {}, {}));
    m_selBaseTf = newTf;
    m_selDelta = 0.0;
    updateGizmo();
    refreshHudText();
}

void ToolRotate::cancelSelRotation()
{
    if (m_state != RotateState::Rotating || !m_selectionRotate) return;
    m_state = RotateState::Ready;
    for (auto it = m_selBaseTf.cbegin(); it != m_selBaseTf.cend(); ++it) {
        if (auto* b = m_paramDoc->findBlock(it.key()))
            b->transform = it.value();
    }
    // Esc: 影子偏转回基准值 (§2.6 边界 — 会话取消零残留).
    if (!m_shadowAtts.isEmpty() && m_paramDoc) {
        QHash<QUuid, double> baseOffsets;
        for (const QUuid& attId : m_shadowAtts)
            baseOffsets.insert(attId, m_shadowBase.value(attId));
        m_paramDoc->updateBaselineOffsets(baseOffsets);
        m_paramDoc->resolveAll();
    }
    m_shadowAtts.clear();
    m_shadowBase.clear();
    m_selDelta = 0.0;
    updateGizmo();
    refreshHudText();
    if (m_scene) m_scene->refreshAllBlockItems();
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

void ToolRotate::updateSelHighlightBox()
{
    if (!m_selectionRotate || !m_scene || !m_paramDoc) return;
    // 选集包围盒 = 成员几何并集.
    const cad::param::BBox box =
        m_paramDoc->componentsView().boundingBoxOfBlocks(m_selIds);
    if (!m_selHighlightBox) {
        m_selHighlightBox = new QGraphicsRectItem();
        QPen pen(QColor(72, 141, 255, 230));   // 选中色系 (与 CanvasStyle 同 token 来源)
        pen.setWidthF(1.5);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_selHighlightBox->setPen(pen);
        m_selHighlightBox->setBrush(Qt::NoBrush);
        m_selHighlightBox->setZValue(90.0);
        m_selHighlightBox->setFlag(QGraphicsItem::ItemIsSelectable, false);
        m_selHighlightBox->setFlag(QGraphicsItem::ItemIsFocusable, false);
        m_scene->addItem(m_selHighlightBox);
        m_managed.own(m_selHighlightBox, &m_selHighlightBox);
    }
    if (box.valid) {
        double zoom = currentZoom();
        if (zoom < 1e-9) zoom = 1.0;
        const double pad = 5.0 / zoom;
        m_selHighlightBox->setRect(QRectF(
            QPointF(box.min.x - pad, -box.max.y - pad),
            QPointF(box.max.x + pad, -box.min.y + pad)));
        m_selHighlightBox->show();
    } else {
        m_selHighlightBox->hide();
    }
}

void ToolRotate::removeSelHighlightBox()
{
    m_managed.release(m_selHighlightBox);   // 释放 + 影子置空 + 撤销登记 (P1/L1)
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
                // 求值失败保持 baseline (out 参数语义), 烘焙兜底值。
                (void)cad::param::ConditionEngine::evaluateLengthMm(
                    a->arcLengthFormula, m_paramDoc->parameters(), {}, arcMm);
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
                a->arcLength = cad::geo::degToArcMm(alpha, radius);
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
        // 求值失败时 arcMm 保持上面的 baseline 值 (out 参数语义), 显示端用
        // 兜底值即可; 显式 (void) 而非丢弃 [[nodiscard]] 的返回值。
        (void)cad::param::ConditionEngine::evaluateLengthMm(
            a->arcLengthFormula, m_paramDoc->parameters(), {}, arcMm);
        const double radius = segmentRadius();
        const double alphaDeg = (radius > 1e-9)
            ? cad::geo::arcMmToDeg(arcMm, radius) : 0.0;
        const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
        return cad::geo::Units::mmToCm(cad::geo::degToArcMm(foldDeg, radius));   // mm → cm
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
                cad::param::RawModelAccess::addAttachmentRaw(*m_paramDoc, m_releaseAttBackup);  // verbatim (keep snapshot isLocked)
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
        cad::param::RawModelAccess::addAttachmentRaw(*m_paramDoc, m_releaseAttBackup);  // verbatim (keep snapshot isLocked)
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
            // 同上: 求值失败保持 baseline, 显示端兜底。
            (void)cad::param::ConditionEngine::evaluateLengthMm(
                a->arcLengthFormula, m_paramDoc->parameters(), {}, arcMm);
            const double radius = segmentRadius();
            double deg = (radius > 1e-9)
                ? cad::geo::arcMmToDeg(arcMm, radius) : 0.0;
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
    deg = cad::geo::normalizeDeg360(deg);
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
    // 选集旋转恒为角度语义 (增量角) — 模式切换忽略并弹回 Angle.
    if (m_selectionRotate) {
        if (m_angleHud) m_angleHud->setMode(cad::param::RotationMode::Angle);
        return;
    }
    if (m_copyGesture && m_copyGesture->active()) {
        // Rotate-copy is always angle semantics (relative angle) — a mode
        // switch mid-gesture is ignored and the HUD snaps back to Angle.
        if (m_angleHud) m_angleHud->setMode(cad::param::RotationMode::Angle);
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
            if (m_angleHud) m_angleHud->setMode(m_rotationMode);   // 弹回原模式
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
            a->arcLength = cad::geo::degToArcMm(deg, radius);
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

    if (!m_angleHud) {
        // L7: CanvasStyle* 直接注入 (不再沿父链反查); M8: 挂视图变换重定位。
        m_angleHud = new cad::ui::AngleHud(viewport, m_scene->style());
        m_angleHud->onTextChanged = [this](const QString& t) { onHudTextChanged(t); };
        m_angleHud->onCommit      = [this] { onHudCommit(); };
        m_angleHud->onCancel      = [this] { onHudCancel(); };
        m_angleHud->onModeChanged = [this](cad::param::RotationMode m) { onHudModeChanged(m); };
        // 视图变换变化 (滚轮缩放 / 滚动条平移) → HUD 跟随锚心重定位。
        // context 用 view: 视图销毁即自动断连; ToolRotate 常驻于 ToolManager,
        // 关闭时由 onDeactivate 显式 disconnect, 双保险防悬垂。
        QObject::disconnect(m_viewZoomConn);
        QObject::disconnect(m_viewHScrollConn);
        QObject::disconnect(m_viewVScrollConn);
        // sender 必须是 CanvasView* 才能匹配 CanvasView 的成员信号 (QGraphicsView*
        // 下行转换不隐式, 故 static_cast)。
        auto* canvasView = static_cast<CanvasView*>(view);
        m_viewZoomConn = QObject::connect(canvasView, &CanvasView::zoomFactorChanged, view,
            [this] { repositionHud(); });
        m_viewHScrollConn = QObject::connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, view,
            [this] { repositionHud(); });
        m_viewVScrollConn = QObject::connect(view->verticalScrollBar(), &QScrollBar::valueChanged, view,
            [this] { repositionHud(); });
    } else {
        m_angleHud->setParent(viewport);
    }

    // Sync HUD mode with the attachment's rotation mode (connected only).
    if (m_connected)
        m_angleHud->setMode(m_rotationMode);

    // Position near the pivot (user → scene → viewport pixels).
    repositionHud();
    m_angleHud->adjustSize();

    m_hudValid = true;
    m_angleHud->setValid(true);
    refreshHudText();
    m_angleHud->show();
    m_angleHud->edit()->setFocus();
    m_angleHud->edit()->selectAll();   // typing immediately replaces the value
}

void ToolRotate::hideHud()
{
    if (m_angleHud) {
        m_angleHud->hide();
        if (m_scene && !m_scene->views().isEmpty())
            m_scene->views().first()->setFocus();
    }
}

void ToolRotate::repositionHud()
{
    if (!m_angleHud || !m_angleHud->isVisible()) return;
    if (!m_scene || m_scene->views().isEmpty()) return;
    QGraphicsView* view = m_scene->views().first();
    const QPointF scenePt = cad::geo::Coord::toScene(m_pivot.x, m_pivot.y);
    const QPoint vpPt = view->mapFromScene(scenePt);
    m_angleHud->move(vpPt + QPoint(16, 16));
}

void ToolRotate::refreshHudText()
{
    if (!m_angleHud) return;
    const QSignalBlocker signalBlocker(m_angleHud->edit());
    // ── 选集旋转: caption「选集旋转」, 值 = 绕锚点增量角 (0 = 原始位姿) ──
    if (m_selectionRotate) {
        m_angleHud->setCaption(QString::fromUtf8("选集旋转"));
        m_angleHud->edit()->setText(cad::geo::Units::formatDegValue(m_selDelta));
        return;
    }
    // D15 确认提示 (H2 ②): 单线已选未确认 = caption 追加确认方式 ——
    // 拖动被门禁拦截却毫无解释, 是审查报告点名的"功能被感知为 bug"。
    const bool confirmHint = m_state == RotateState::Ready
                          && !m_blockId.isNull() && !m_selectionConfirmed
                          && !(m_copyGesture && m_copyGesture->active());
    // Caption follows the ACTIVE angle semantics (术语统一: 跟随角度 /
    // 绝对角度 / 相对角度): 跟随线 = 跟随角度/弧长(默认); 自由线 = 绝对角度;
    // 旋转复制 = 相对角度。setMode 只在模式切换时刷新标签，锚心切换
    // (Connected↔Free) 不切模式，所以这里每帧显式同步。
    if (m_copyGesture && m_copyGesture->active())
        m_angleHud->setCaption(QString::fromUtf8("旋转角度"));  // 绕锚心角, 0° = 重叠
    else if (!m_connected)
        m_angleHud->setCaption(QString::fromUtf8("绝对角度")
                          + (confirmHint ? QString::fromUtf8("（右键/回车确认）")
                                         : QString()));
    else if (confirmHint)
        // 默认标签随模式 (跟随角度/弧长); 带后缀时需显式拼出, setCaption
        // 的空串语义是"恢复默认", 追加不了后缀。
        m_angleHud->setCaption(QString::fromUtf8("%1（右键/回车确认）").arg(
            m_rotationMode == cad::param::RotationMode::ArcLength
                ? QString::fromUtf8("弧长") : QString::fromUtf8("跟随角度")));
    else
        m_angleHud->setCaption(QString());   // 默认: 跟随角度 / 弧长
    if (m_copyGesture && m_copyGesture->active()) {
        // Rotate-copy: pivot-relative angle (方案 B, 用户拍板 2026-08) —
        // 0° = 副本与父线重叠, 拖多少度 = 转多少度, 起点/终点锚心一致。
        m_angleHud->edit()->setText(cad::geo::Units::formatDegValue(
            m_copyGesture->currentRelativeAngle()));
    } else if (m_rotationMode == cad::param::RotationMode::ArcLength && m_connected) {
        // Arc-length mode: show the value in cm (formula-driven values are
        // evaluated live — the HUD always shows a plain editable number).
        m_angleHud->edit()->setText(cad::geo::Units::formatDegValue(currentModeValue()));
    } else {
        // Angle mode: degrees (formula evaluated live, same idea).
        m_angleHud->edit()->setText(cad::geo::Units::formatDegValue(currentAngleDeg()));
    }
}

void ToolRotate::onHudTextChanged(const QString& text)
{
    if (!m_paramDoc) return;
    // 旋转复制提交/取消后 HUD 已隐藏（副本编辑语义终结）；隐藏期间忽略
    // 任何输入——防止输入框目标悄然切回原线角度造成“幽灵编辑”。
    if (!m_angleHud || !m_angleHud->isVisible()) return;
    const QString t = text.trimmed();

    // ── 选集旋转 HUD: 数值 = 绕锚点增量角 (键入即预览, Enter 提交) ──
    if (m_selectionRotate) {
        if (t.isEmpty()) {
            // 空 = 回到基准位姿 (影子偏转一并回 base).
            for (auto it = m_selBaseTf.cbegin(); it != m_selBaseTf.cend(); ++it) {
                if (auto* b = m_paramDoc->findBlock(it.key()))
                    b->transform = it.value();
            }
            if (!m_shadowAtts.isEmpty()) {
                QHash<QUuid, double> baseOffsets;
                for (const QUuid& attId : m_shadowAtts)
                    baseOffsets.insert(attId, m_shadowBase.value(attId));
                m_paramDoc->updateBaselineOffsets(baseOffsets);
                m_paramDoc->resolveAll();
            }
            m_selDelta = 0.0;
            m_hudValid = true;
            if (m_scene) m_scene->refreshAllBlockItems();
        } else {
            bool isNumber = false;
            const double v = t.toDouble(&isNumber);
            if (isNumber) {
                m_hudValid = true;
                m_hudError.clear();
                applySelDelta(v);
            } else {
                auto r = cad::param::ConditionEngine::evaluate(
                    t, m_paramDoc->parameters(), m_paramDoc->conditions());
                if (r.ok) {
                    m_hudValid = true;
                    m_hudError.clear();
                    applySelDelta(r.value);
                } else {
                    m_hudValid = false;   // 保留最后一次有效几何
                    m_hudError = r.error; // M8: 无效原因短文
                }
            }
        }
        if (m_angleHud) {
            m_angleHud->setValid(m_hudValid);
            m_angleHud->setError(m_hudValid ? QString() : m_hudError);
        }
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
        m_hudError.clear();
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
            m_hudError.clear();
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
                m_hudError.clear();
            } else {
                m_hudValid = false;           // keep the last valid geometry
                m_hudError = r.error;         // M8: 无效原因短文
            }
        }
    }

    if (m_angleHud) {
        m_angleHud->setValid(m_hudValid);
        m_angleHud->setError(m_hudValid ? QString() : m_hudError);
    }
    if (m_hudValid) updateGizmo();
}

void ToolRotate::onHudCommit()
{
    if (!m_hudValid) return;   // ignore Enter on an invalid formula
    if (m_copyGesture && m_copyGesture->active()) {
        m_copyGesture->commit();
    } else if (m_selectionRotate) {
        if (!m_selPivotSet) {
            if (m_scene)
                m_scene->showToast(QString::fromUtf8("请先在画布上点一下设定锚点"));
            return;
        }
        commitSelRotation();
    } else {
        commitCurrent();
        // D15 选中态数值提交后保持输入框可用 (精确输入是慎重动作,
        // 不占用确认门): 仅当选中会话仍在时回显 HUD.
        if (m_state == RotateState::Ready && !m_blockId.isNull())
            showHud();
    }
}

void ToolRotate::onHudCancel()
{
    if (m_copyGesture && m_copyGesture->active()) {
        m_copyGesture->cancel();    // drop the preview clone, keep the target
    } else if (m_selectionRotate && m_state == RotateState::Rotating) {
        cancelSelRotation();        // 中止选集拖动, 恢复基准位姿 + 影子基准
    } else if (m_selectionRotate && m_state == RotateState::Ready) {
        // 第一击设锚点"点错了"：Esc 先清锚点重选 (仍留选集会话);
        // 无锚点按下 Esc = 结束选集会话回 Idle.
        if (m_selPivotSet) {
            m_selPivotSet = false;
            m_selDelta = 0.0;
            m_selPivotPointId = QUuid();
            removeGizmo();
            refreshHudText();
        } else {
            clearTarget();
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
    if (m_scene) {
        const double z = m_scene->currentZoom();
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
        angleDeg = cad::geo::normalizeDeg360(dirToP * 180.0 / M_PI);
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
        m_managed.own(m_aimRing, &m_aimRing);
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

void ToolRotate::applySelectionConfirmed(bool confirmed)
{
    if (m_selectionConfirmed == confirmed) return;
    m_selectionConfirmed = confirmed;
    updateGizmo();      // gizmo 虚线/空心 ↔ 实线/实心 (H2 ①)
    refreshHudText();   // caption 确认提示后缀 (H2 ②)
}

bool ToolRotate::gizmoConfirmed() const
{
    return m_gizmo && m_gizmo->confirmed();
}

void ToolRotate::updateGizmo()
{
    if (!m_gizmo) return;
    // D15 确认门可视 (H2): 单线选中未确认 = 虚线/空心; 确认态 (含选集会话
    // 与复制手势 —— 两者本就是"无需再确认"的活跃会话) = 实线/实心。每次
    // 刷新同步一次, 翻转点另经 applySelectionConfirmed 推样式+caption。
    m_gizmo->setConfirmed(m_selectionConfirmed || m_selectionRotate
                          || (m_copyGesture && m_copyGesture->active()));
    // ── 选集旋转: 基准 = 拖起始方向 (pivot→按下点), 弧 = 0 → delta ──
    if (m_selectionRotate) {
        if (!m_selPivotSet) return;   // 锚点未定: 不画弧
        const double dashRad = m_selBaselineRad;
        double arcEnd = dashRad + m_selDelta * M_PI / 180.0;
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
    if (!m_scene || !m_paramDoc) return QUuid();
    // 统一命中 (P1/M7+L2): 与选择工具同一份规则源 —— 灰显基准层不可旋转。
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const auto hits = blockHitsAtScene(*m_scene, *m_paramDoc, scenePt);
    return hits.empty() ? QUuid() : hits.front().blockId;
}

} // namespace cad::tools
