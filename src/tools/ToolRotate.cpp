#include "ToolRotate.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QGraphicsEllipseItem>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QPen>
#include <QWidget>

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
#include "parametric/FollowerAngle.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
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

ToolRotate::~ToolRotate() = default;

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
    // 工具退出清理 (ManagedItems 统一释放)
    removeGizmo();
    reportPinnedTarget(QUuid(), QUuid());
    reportHoverTarget(QUuid(), QUuid());
    reportHintOverride(QString());
    m_managed.release(m_aimRing);   // 释放 + 影子置空 + 撤销登记 (P1/L1)
    m_blockId = QUuid();
    m_attId = QUuid();
    m_connected = false;
    m_anchorPointId = QUuid();
    m_state = RotateState::Idle;
    // 会话结束上报必须放在 m_blockId/m_state 清空之后 —— reportRotateAnchorState
    // 用 m_state != Idle 判定会话激活, 提前调用会上报 active=true 的陈旧会话,
    // 条带残留旋转会话标志 (换向按钮错走切锚心语义)。
    reportRotateAnchorState();   // 条带: 旋转会话结束 (2026-12)
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
        // 空闲路径 (m_blockId 为空) = 清目标。
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
                    reportRotateAnchorState();   // 条带: 基准读数锚心端在前
                    updateStatusHint();          // 状态栏: 锚心端确定提示
                    removeGizmo();
                    buildGizmo();
                    updateGizmo();
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
    const cad::geo::Vec2 pos(event->scenePos().x(), event->scenePos().y());
    // 非拖动帧: 上报悬停候选给上下文属性条 (只读预览; 节流与焦点保护在
    // 条带内部)。拖动中不改焦点 —— 焦点始终是被旋转的那条线。
    if (m_state != RotateState::Rotating) {
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
    if (m_state == RotateState::Rotating)
        cancelRotation();
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
        // 拖动中 = 取消回位; 选中态 = 清目标。
        // (原"HUD 放弃编辑"那层已随 AngleHud 退场 —— 精确输入归上下文属性条。)
        if (m_state == RotateState::Rotating) {
            cancelRotation();
            applySelectionConfirmed(false);
        } else if (m_state == RotateState::Ready && m_selectionConfirmed) {
            applySelectionConfirmed(false);
        } else {
            restoreBase();
            clearTarget();
        }
        event->accept();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        // D15: 选中态回车 = 确认选择。确定态回车 no-op (无未提交内容不重复
        // 确认)。角度的键盘精确输入已归上下文属性条 —— 不再需要"焦点在
        // 输入框 → 应用数值 / 否则 → 确认"的双重身份仲裁。
        if (m_state == RotateState::Ready && !m_selectionConfirmed)
            applySelectionConfirmed(true);
        event->accept();
    } else if (event->key() == Qt::Key_X) {
        // X: toggle the anchor between the start and end points (锚心切换).
        // 确定态下不适用 —— 锚向切换是选中态专属动作.
        if (!m_selectionConfirmed) toggleAnchor();
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

    commitCurrent();                 // commit any uncommitted strip edit first
    m_anchorIsEnd = !m_anchorIsEnd;
    rebuildAnchorState();
    reportRotateAnchorState();       // 条带: 基准读数锚心端在前 (2026-12)
    updateStatusHint();              // 状态栏: 锚心端确定提示 (2026-12)

    removeGizmo();
    buildGizmo();
    updateGizmo();
    if (m_scene) m_scene->refreshAllBlockItems();
}

void ToolRotate::rebuildAnchorState()
{
    if (!m_paramDoc) return;
    m_shadowId = QUuid();          // 影子通道复位 (每帧重建时重判)
    m_shadowMounted = false;
    m_att1Id = QUuid();
    cad::param::Block* blk = m_paramDoc->findBlock(m_blockId);
    if (!blk || blk->segments.empty()) { clearTarget(); return; }

    const cad::param::Segment& seg = blk->segments.front();
    m_anchorPointId = m_anchorIsEnd ? seg.endPointId : seg.startPointId;
    const cad::param::ParamPoint* ap = blk->findPoint(m_anchorPointId);
    if (!ap || !ap->resolved) { clearTarget(); return; }

    cad::param::Attachment* att = attachmentAtPoint(m_anchorPointId);
    // 独立角度 (angleIndependent): 位置仍焊在宿主, 但角度自管 —— Resolver
    // 保留 from.transform.rotation, followerAngle 被忽略。因此旋转**走自由线
    // 路径** (直接写块 transform), 锚心仍取挂连接端 (位置钉点); 拖帧内 Resolver
    // 会把 from-point 重新吸回宿主点, 只留下旋转生效 (用户报告 2026-12:
    // 独立角度线旋转拖动无效, 根因 = 当普通跟随线写 followerAngle 被忽略)。
    const bool angleIndependent = att && att->angleIndependent;
    if (att && !angleIndependent) {
        // ── Connected mode: the anchor IS the attachment point, so the
        // rotation edits the follower angle. ──
        m_connected = true;
        m_attId = att->id;
        const cad::param::Block* fromBlk = m_paramDoc->findBlock(att->fromBlockId);
        m_pivot = fromBlk ? fromBlk->worldPos(att->fromPointId) : blk->worldPos(m_anchorPointId);
        // 有效基准方向 = 与 Resolver 同构 (自定义角度基准/两点连线全部生效,
        // 2026-09 审核 F3) —— 此前只取位置宿主出方向, 设了自定义基准后
        // Gizmo 基准虚线/折角弧/表盘读数画错基准。
        m_refWorldRad = cad::param::effectiveAngleRefWorld(m_paramDoc, *att);
        m_baseAngle = att->followerAngle;
        m_baseFormula = att->followerAngleFormula;
        m_rotationMode = att->rotationMode;
        m_baseArcLength = att->arcLength;
        m_baseArcFormula = att->arcLengthFormula;
        m_anchorLocal = ap->resolvedPos;  // 影子通道 R8 轴心回写需要 (连接态)
        // 影子角度通道 (R6/R8, 拆开影子基准): 基准是影子块时记录通道基线。
        // offset 公式锁定 → 旋转改写影子角度 (拆开态 rotation / 挂载态 Δ);
        // 数字 offset → m_shadowId 只作标记, applyAngleDeg 照旧写 followerAngle。
        if (const auto* toBlk = m_paramDoc->findBlock(att->toBlockId);
            toBlk && toBlk->isShadow) {
            m_shadowId = toBlk->id;
            for (const auto& a : m_paramDoc->attachments()) {
                if (!a.isPin && a.fromBlockId == m_shadowId) {
                    m_att1Id = a.id;
                    m_shadowMounted = true;
                    m_shadowDelta0 = a.followerAngle;
                    break;
                }
            }
            if (auto* sh = m_paramDoc->findBlock(m_shadowId)) {
                m_shadowRot0 = sh->transform.rotation;
                m_shadowTf0 = sh->transform;
            }
            m_followerTf0 = blk->transform;
        }
    } else {
        // ── Free mode: rotate rigidly about the anchor point. ──
        // (独立角度连接线同样走这里: 角度账本 = 块 transform.rotation。)
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
    // 独立角度连接线绝不释放 —— 位置焊点是独立角语义的一部分 (角度自由、
    // 位置仍被宿主钉住)。
    if (!m_releaseAttHeld) {
        m_releaseAttId = QUuid();
        if (!m_connected) {
            if (auto* fa = followerAttachment()) {
                if (!fa->angleIndependent) {
                    m_releaseAttId = fa->id;
                    m_releaseAttBackup = *fa;
                }
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
    if (m_state != RotateState::Idle)
        removeGizmo();

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
    // gizmo 样式 + 状态栏提示三同步, 不让"确认态视觉"在任何路径上残留。
    applySelectionConfirmed(false);   // D15: 新目标从选中态起步, 需确认才可拖
    m_releaseAttId = QUuid();
    m_releaseAttHeld = false;
    rebuildAnchorState();
    if (m_blockId.isNull()) return;   // rebuild cleared an invalid target

    m_state = RotateState::Ready;
    // 条带: 旋转会话激活, 锚心端在前 (2026-12)。必须放在 m_state=Ready 之后
    // —— reportRotateAnchorState 用 m_state != Idle 判定会话激活, 提前调用会
    // 上报 active=false, 条带不进入旋转会话, 换向按钮将走命令路径而非切锚心。
    reportRotateAnchorState();
    buildGizmo();
    updateGizmo();
    // 上下文属性条: 锁定到目标线段 (角度格随之可编辑, 并跟随拖动实时刷新)。
    reportStripTarget();
    updateStatusHint();
    m_scene->refreshAllBlockItems();
}

void ToolRotate::clearTarget()
{
    removeGizmo();
    clearAimCandidate();
    m_blockId = QUuid();
    m_attId = QUuid();
    m_connected = false;
    m_anchorPointId = QUuid();
    m_shadowId = QUuid();          // 影子通道复位
    m_shadowMounted = false;
    m_att1Id = QUuid();
    // N3: 同 selectTarget —— 走统一翻转入口。
    applySelectionConfirmed(false);   // D15
    m_releaseAttId = QUuid();
    m_releaseAttHeld = false;
    m_state = RotateState::Idle;
    reportStripTarget();     // 焦点清空 (null = 解除锁定)
    reportRotateAnchorState();   // 条带: 旋转会话结束 (2026-12)
    updateStatusHint();
    if (m_scene) m_scene->refreshAllBlockItems();
}


// ═══════════════════════════════════════════════════════════════════════════════
// Rotate-copy gesture (Ctrl+drag 旋转复制)
// ═══════════════════════════════════════════════════════════════════════════════


void ToolRotate::beginRotation(const cad::geo::Vec2& pos)
{
    if (m_state != RotateState::Ready) return;
    // 变量/公式驱动角度 = 锁定 (用户拍板 2026-12): 旋转工具不得改变角度 ——
    // 不烘焙公式、不覆盖变量。要改角度先移除公式 (属性面板)。旧实现在此
    // 把公式烘焙成数值后继续旋转, 导致变量被覆盖消失。
    // 影子通道例外 (R6, 拆开影子基准): 基准是影子块时锁定 offset 的旋转
    // 改写**影子角度** (拆开态 rotation / 挂载态 Att1 Δ), 公式原样存活。
    if (isAngleLocked() && m_shadowId.isNull()) {
        updateStatusHint();
        return;
    }
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

    // Resolver 按基准方向驱动跟随线, 无需工具逐帧回写。
    applyAngleDeg(target);
    updateGizmo();
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
        // 影子角度通道 (R6, 拆开影子基准): offset 公式/变量锁定时旋转改写
        // **影子角度** (拆开态 rotation / 挂载态 Att1 Δ), 公式原样存活;
        // 数字 offset 走下方常规路径 (写 followerAngle, 影子不动)。
        if (isAngleLocked() && !m_shadowId.isNull()) {
            applyShadowAngleDeg(deg);
        } else if (auto* a = editableAttachment()) {
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
    // 影子通道: 影子一并入种子 (挂载态影子需按新 Δ 重新钉回宿主, 随后 Att2
    // 链式带动跟随线; 拆开态影子无驱动, 入种子无害)。
    QList<QUuid> rotSeeds{m_blockId};
    if (!m_shadowId.isNull()) rotSeeds.append(m_shadowId);
    m_paramDoc->resolveForDrag(rotSeeds);
    if (m_scene) m_scene->syncBlockPositions();
}

// ── 影子角度通道 (R6/R8, 拆开影子基准, DETACH_SHADOW_DESIGN.md §7.2) ────────
// offset 被公式/变量锁定时的旋转落点: 公式原样不动, 改写影子角度 ——
//   · 拆开态: 写影子块 transform.rotation (冻结克隆 = 普通块), 并同步回写
//     跟随线 transform 使其绕 p3 原地转 (R8: p3 世界位置不变、方向变 ——
//     Resolver angleOnly 分支只写 rotation 不碰 origin, 回写得以存活)。
//   · 挂载态: 写 Att1 followerAngle Δ (Δ_new = Δ0 − δ, 与反算公式同构),
//     Resolver 把影子钉回宿主点 (绕接点转), Att2 链式带动跟随线。
// @param deg 连接分支折角目标 α_target; δ = α0 − α_target = 光标增量。
void ToolRotate::applyShadowAngleDeg(double deg)
{
    if (!m_paramDoc) return;
    const double deltaDeg = cad::geo::normalizeDeg180(m_dragAngle0 - deg);
    const double deltaRad = deltaDeg * M_PI / 180.0;
    if (m_shadowMounted) {
        if (auto* att1 = m_paramDoc->findAttachment(m_att1Id))
            att1->followerAngle = cad::geo::normalizeDeg180(m_shadowDelta0 - deltaDeg);
    } else {
        if (auto* sh = m_paramDoc->findBlock(m_shadowId))
            sh->transform.rotation = m_shadowRot0 + deltaRad;
        if (auto* blk = m_paramDoc->findBlock(m_blockId)) {
            const double newRot = m_followerTf0.rotation + deltaRad;
            blk->transform.rotation = newRot;
            blk->transform.origin = m_pivot - m_anchorLocal.rotated(newRot);
        }
    }
    if (const auto* sh = m_paramDoc->findBlock(m_shadowId))
        m_paramDoc->invalidateLayer(sh->layer);
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
}

void ToolRotate::commitCurrent()
{
    if (!m_paramDoc || !m_undoStack) return;

    if (m_connected) {
        // 影子角度通道提交 (R6): restore-then-replay 单步 undo。
        //   挂载态 = SetFollowerAngleCommand(Att1 Δ); 拆开态 = ShadowRotateCommand
        //   (影子 transform + 跟随线 transform 双 verbatim, R8 轴心一并还原)。
        if (isAngleLocked() && !m_shadowId.isNull()) {
            if (m_shadowMounted) {
                auto* att1 = m_paramDoc->findAttachment(m_att1Id);
                if (!att1) { updateGizmo(); return; }
                const double curDelta = att1->followerAngle;
                if (std::abs(curDelta - m_shadowDelta0) <= 1e-9) {
                    updateGizmo();
                    return;
                }
                att1->followerAngle = m_shadowDelta0;
                m_undoStack->push(new cad::cmd::SetFollowerAngleCommand(
                        m_paramDoc, m_att1Id, curDelta));
                m_shadowDelta0 = curDelta;
            } else {
                auto* sh = m_paramDoc->findBlock(m_shadowId);
                auto* blk = m_paramDoc->findBlock(m_blockId);
                if (!sh || !blk) { updateGizmo(); return; }
                const auto shNew = sh->transform;
                const auto blkNew = blk->transform;
                const bool changed =
                    std::abs(shNew.rotation - m_shadowTf0.rotation) > 1e-9
                    || shNew.origin.distanceTo(m_shadowTf0.origin) > 1e-6
                    || std::abs(blkNew.rotation - m_followerTf0.rotation) > 1e-9
                    || blkNew.origin.distanceTo(m_followerTf0.origin) > 1e-6;
                if (!changed) { updateGizmo(); return; }
                sh->transform = m_shadowTf0;
                blk->transform = m_followerTf0;
                m_undoStack->push(new cad::cmd::ShadowRotateCommand(
                        m_paramDoc, m_shadowId, m_shadowTf0, shNew,
                        m_blockId, m_followerTf0, blkNew));
                m_shadowTf0 = shNew;
                m_shadowRot0 = shNew.rotation;
                m_followerTf0 = blkNew;
            }
            updateGizmo();
            return;
        }

        cad::param::Attachment* att = editableAttachment();
        if (!att) {
            updateGizmo();
            return;
        }

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
        if (!changed) {
            updateGizmo();
            return;
        }

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
        if (!blk) {
            updateGizmo();
            return;
        }

        const cad::param::Transform2D curTf = blk->transform;
        const QUuid curEndBlock = blk->endTargetBlockId;
        const QUuid curEndPoint = blk->endTargetPointId;
        const bool changed = std::abs(curTf.rotation - m_baseTf.rotation) > 1e-9
                          || curTf.origin.distanceTo(m_baseTf.origin) > 1e-6
                          || curEndBlock != m_baseEndTargetBlock
                          || curEndPoint != m_baseEndTargetPoint
                          || m_releaseAttHeld;
        if (!changed) {
            updateGizmo();
            return;
        }

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
}

void ToolRotate::restoreBase()
{
    if (!m_paramDoc) return;
    if (m_connected) {
        // 影子角度通道 (Esc): 影子/跟随线回拖前基线 (拆开态) 或 Att1 Δ 回基线。
        if (isAngleLocked() && !m_shadowId.isNull()) {
            if (m_shadowMounted) {
                if (auto* att1 = m_paramDoc->findAttachment(m_att1Id))
                    att1->followerAngle = m_shadowDelta0;
            } else {
                if (auto* sh = m_paramDoc->findBlock(m_shadowId))
                    sh->transform = m_shadowTf0;
                if (auto* blk = m_paramDoc->findBlock(m_blockId))
                    blk->transform = m_followerTf0;
            }
        } else if (auto* a = editableAttachment()) {
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
    // free-hand editing. Based on the committed base formula so transient
    // strip edits never flip the lock mid-session. Free lines are never locked.
    if (!m_connected) return false;
    if (m_rotationMode == cad::param::RotationMode::ArcLength)
        return !m_baseArcFormula.isEmpty();
    return !m_baseFormula.isEmpty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 状态提示与焦点上报 (上下文属性条, CONTEXT_STRIP_DESIGN.md 一期)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolRotate::reportStripTarget()
{
    // 锁定到当前旋转目标 (null = 解除锁定)。条带的角度格随后可编辑, 并订阅
    // resolved 自动跟随拖动刷新 —— 工具不需要把角度值推给它。
    if (m_blockId.isNull() || !m_paramDoc) {
        reportPinnedTarget(QUuid(), QUuid());
        return;
    }
    const auto* blk = m_paramDoc->findBlock(m_blockId);
    if (!blk || blk->segments.empty()) {
        reportPinnedTarget(QUuid(), QUuid());
        return;
    }
    reportPinnedTarget(m_blockId, blk->segments.front().id);
}

void ToolRotate::reportRotateAnchorState()
{
    if (!m_host) return;
    // 旋转会话 = 有目标 (Ready/Rotating)。锚心切换资格 = Ready 且两端都无
    // 连接 (与 toggleAnchor 的守卫同源, 用户拍板 2026-08: 连接线禁切锚心)。
    const bool active = !m_blockId.isNull() && m_state != RotateState::Idle;
    bool canToggle = active && m_state == RotateState::Ready;
    QString reason;
    if (canToggle && m_paramDoc) {
        if (const auto* blk = m_paramDoc->findBlock(m_blockId);
            blk && !blk->segments.empty()) {
            const auto& seg = blk->segments.front();
            if (attachmentAtPoint(seg.startPointId)
                || attachmentAtPoint(seg.endPointId)) {
                canToggle = false;
                reason = QString::fromUtf8("已连接线段禁止切换锚心（先断开连接）");
            }
        }
    }
    m_host->setRotateAnchorState(active, m_anchorIsEnd, canToggle, reason);
}

/// 条带「换向」点击 (旋转会话内): 转义为切换锚心。toggleAnchor 自带全部守卫
/// (Ready 状态 + 连接线禁切 + commitCurrent), 转发即可 —— 条带侧按钮资格已
/// 由 reportRotateAnchorState 的 canToggle 预判, 这里只是兜底。
void ToolRotate::onReverseRequested(const QUuid& /*blockId*/, const QUuid& /*segmentId*/)
{
    toggleAnchor();
}

void ToolRotate::updateStatusHint()
{
    // 角度**数值**读数归上下文属性条 (订阅 resolved, 拖动时自动跟着变);
    // 这里只报"现在处于什么状态、该按什么"的离散提示。
    //
    // 唯一例外是旋转复制: 条带按拍板在复制态不显示角度, 相对角只能落在
    // 状态栏 —— Ctrl+拖动是低频精确操作, 每帧刷一次文本可接受 (普通旋转
    // 拖动时文本是常量, QLabel::setText 相同文本直接返回, 零开销)。
    if (m_copyGesture && m_copyGesture->active()) {
        reportHintOverride(QString::fromUtf8("旋转复制 %1°").arg(
            cad::geo::Units::formatDegValue(m_copyGesture->currentRelativeAngle())));
        return;
    }
    switch (m_state) {
    case RotateState::Idle:
        reportHintOverride(QString());       // 恢复 ToolDescriptor::hintText
        return;
    case RotateState::Rotating:
        reportHintOverride(QString::fromUtf8("旋转中 · 松手提交 · Esc 回位"));
        return;
    case RotateState::Ready: {
        // D15 确认门的文字表达 (H2 第三条) —— 原挂在 HUD caption 后缀。
        // 2026-12: 附带锚心端 (确定提示) —— 锚心 = 旋转基准端, 换向/点端点
        // 切换后提示同步刷新, 让"基准在哪端"常驻可见。
        QString anchorTag = QStringLiteral("?");
        if (m_paramDoc) {
            if (const auto* blk = m_paramDoc->findBlock(m_blockId);
                blk && !blk->segments.empty()) {
                const auto& seg = blk->segments.front();
                const auto* ap = blk->findPoint(m_anchorPointId);
                if (ap)
                    anchorTag = cad::param::Serial::tag(ap->serial);
                else
                    anchorTag = m_anchorIsEnd
                        ? cad::param::Serial::tag(
                              blk->findPoint(seg.endPointId)->serial)
                        : cad::param::Serial::tag(
                              blk->findPoint(seg.startPointId)->serial);
            }
        }
        if (isAngleLocked()) {
            reportHintOverride(QString::fromUtf8(
                "旋转：锚心 %1 · 角度由变量/公式驱动，已锁定（移除公式后可旋转）")
                .arg(anchorTag));
            return;
        }
        reportHintOverride(m_selectionConfirmed
            ? QString::fromUtf8("旋转：锚心 %1 · 拖动旋转（Shift 吸附 15°）")
                  .arg(anchorTag)
            : QString::fromUtf8("旋转：锚心 %1 · 右键或回车确认后拖动")
                  .arg(anchorTag));
        return;
    }
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
    updateStatusHint();   // 状态栏确认提示 (H2 第三条, 原挂在 HUD caption)
}

bool ToolRotate::gizmoConfirmed() const
{
    return m_gizmo && m_gizmo->confirmed();
}

void ToolRotate::updateGizmo()
{
    if (!m_gizmo) return;
    // D15 确认门可视 (H2): 单线选中未确认 = 虚线/空心; 确认态 (含复制手势 ——
    // 它本就是"无需再确认"的活跃会话) = 实线/实心。每次刷新同步一次, 翻转点
    // 另经 applySelectionConfirmed 推样式+caption。
    m_gizmo->setConfirmed(m_selectionConfirmed
                          || (m_copyGesture && m_copyGesture->active()));
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
    // 状态栏提示随 gizmo 同步 (拖动每帧都刷一次; 普通旋转的文案是常量,
    // QLabel 相同文本直接返回, 只有旋转复制的相对角会真正逐帧更新)。
    updateStatusHint();
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
