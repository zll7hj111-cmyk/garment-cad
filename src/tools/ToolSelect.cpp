#include "ToolSelect.h"
#include "ToolManager.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QMenu>
#include <QUndoStack>
#include <QPen>
#include <QFontMetrics>
#include <QLineEdit>
#include <QInputDialog>
#include <QWidget>
#include <QEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "parametric/FollowerAngle.h"
#include "LinePropertyDialog.h"
#include "ConnectGesture.h"
#include "CopyDragController.h"
#include "MarqueeGesture.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/ComponentCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "document/DeleteImpactConfirm.h"

namespace cad::tools {

namespace {

/// 长按拖动判定阈值 (2026-09 取消确认基准): press 线身后移动超过该像素数
/// → 进入拖动 (锚点 = press 位置, 与旧"press 即拖"位移语义一致).
constexpr double kDragThresholdPx = 5.0;

/// 线段角色 → 界面文案 (与 LinePropertyDialog 的角色下拉一致).
QString segmentRoleText(cad::param::SegmentRole role)
{
    using cad::param::SegmentRole;
    switch (role) {
    case SegmentRole::Outline:   return QString::fromUtf8("轮廓线");
    case SegmentRole::Internal:  return QString::fromUtf8("内部线");
    case SegmentRole::Auxiliary: return QString::fromUtf8("辅助线");
    }
    return QString::fromUtf8("轮廓线");
}

/// 重叠提示 HUD 的共用字体 (每帧创建 QFont 代价高).
const QFont& overlapHintFont()
{
    static QFont f = [] { QFont fnt; fnt.setPixelSize(11); return fnt; }();
    return f;
}

/// True if segment [p1,p2] intersects (or is contained in) rect r.
/// Liang-Barsky clipping; also counts either endpoint inside the rect.
} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene = &scene;
    m_paramDoc = paramDoc;
    m_state = SelectState::Idle;

    // (Re)create the extracted gestures with the current context. The tools
    // forward their state transitions / selection queries through callbacks.
    // (The document may be null during ToolManager construction; the gestures
    // no-op on missing doc until setParamDocument wires the real one in.)
    delete m_connectGesture;
    delete m_copyDrag;
    QUndoStack* const undo = m_paramDoc ? m_paramDoc->undoStack() : nullptr;
    m_connectGesture = new ConnectGesture(
        m_scene, m_paramDoc, undo,
        [this](SelectState s) { setState(s); },
        [this](const QString& t) { showToast(t); },
        [this]() { clearSelectionAndIdle(); },
        [this]() { return m_selection.isEmpty(); });
    m_copyDrag = new CopyDragController(
        m_scene, m_paramDoc, undo,
        [this](SelectState s) { setState(s); },
        [this]() {
            // 2026-09 取消确认基准: 选中即就绪 (Confirmed 由 setState 归一化).
            setState(m_selection.isEmpty() ? SelectState::Idle
                                           : SelectState::Selecting);
        },
        [this]() { clearSelectionAndIdle(); });
    m_marqueeGesture = new MarqueeGesture(m_scene, m_paramDoc);
}

void ToolSelect::deactivate()
{
    if (m_copyDrag && m_copyDrag->active())
        m_copyDrag->cancel();       // tool switch mid-copy: drop the clones
    if (m_connectGesture && m_connectGesture->active())
        m_connectGesture->cancel();  // abort any in-progress connection
    deactivateOverlapContext();  // 工具切换: 隐藏 HUD + 清循环状态
    delete m_connectGesture;
    m_connectGesture = nullptr;
    delete m_copyDrag;
    m_copyDrag = nullptr;
    delete m_marqueeGesture;
    m_marqueeGesture = nullptr;

    m_selection.clear();
    if (m_scene && m_paramDoc)
        syncSelectionVisual();   // 去掉选中高亮
    if (m_scene)
        m_scene->clearSelection();
    m_hoverCursor = Qt::ArrowCursor;  // 离开工具恢复默认光标
    if (m_scene && !m_scene->views().isEmpty())
        m_scene->views().first()->viewport()->setCursor(Qt::ArrowCursor);

    m_scene = nullptr;
    m_paramDoc = nullptr;
    m_state = SelectState::Idle;
}

// ═══════════════════════════════════════════════════════════════════════════════
// State management
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::setState(SelectState s)
{
    // 2026-09 取消确认基准: ConnectGesture/CopyDragController 等仍会发
    // Confirmed (选中可操作态), 归一化为 Selecting — 选中即就绪, 无需右键.
    if (s == SelectState::Confirmed) s = SelectState::Selecting;
    m_state = s;
}

void ToolSelect::clearSelectionAndIdle()
{
    deactivateOverlapContext();
    m_selection.clear();
    syncSelectionVisual();
    setState(SelectState::Idle);
    notifyEditTarget();
}

void ToolSelect::clearSelectionOnLayerChange()
{
    // Selection is scoped to the active layer: switching layers drops it so a
    // stale (now grayed) selection can never be dragged or edited.
    if (m_copyDrag && m_copyDrag->active())
        m_copyDrag->cancel();
    if (m_connectGesture && m_connectGesture->active())
        m_connectGesture->cancel();
    if (m_marqueeGesture) m_marqueeGesture->cancel();
    clearSelectionAndIdle();
}

void ToolSelect::selectBlocksExternally(const QList<QUuid>& blockIds)
{
    if (!m_paramDoc || blockIds.isEmpty()) return;
    deactivateOverlapContext();  // 面板联动选择与画布循环上下文互斥
    // Abort any in-flight gesture first (same hygiene as a layer switch).
    if (m_copyDrag && m_copyDrag->active())
        m_copyDrag->cancel();
    if (m_connectGesture && m_connectGesture->active())
        m_connectGesture->cancel();
    if (m_marqueeGesture) m_marqueeGesture->cancel();

    m_selection = QSet<QUuid>(blockIds.begin(), blockIds.end());
    syncSelectionVisual();
    setState(m_selection.isEmpty() ? SelectState::Idle : SelectState::Selecting);
    notifyEditTarget();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Selection mode (W toggle: 多选 ↔ 单选)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::toggleSelectionMode()
{
    const bool switchingToMulti = (m_selectionMode == SelectionMode::Single);
    m_selectionMode = switchingToMulti ? SelectionMode::Multi
                                       : SelectionMode::Single;
    // Single → Multi PRESERVES the current selection so the user can keep
    // adding to it (选中一个后按 W 继续加选 — clearing it here made the
    // toggle look like a no-op and silently dropped the picked blocks).
    // Multi → Single clears, because a multi-set under single-click-replace
    // semantics would leave stale highlights the next click cannot undo.
    if (!switchingToMulti)
        clearSelectionAndIdle();
    showModeToast();
}

void ToolSelect::showModeToast()
{
    // 2026-09 取消确认基准: 单击选中 / 按住拖动 / 端点按住连接 / 悬停提示.
    const QString text = (m_selectionMode == SelectionMode::Single)
        ? QString::fromUtf8("\xe5\x8d\x95\xe9\x80\x89\xe6\xa8\xa1\xe5\xbc\x8f\xef\xbc\x9a"
                            "\xe7\x82\xb9\xe5\x87\xbb\xe9\x80\x89\xe4\xb8\xad"
                            " | \xe6\x8c\x89\xe4\xbd\x8f\xe6\x8b\x96\xe5\x8a\xa8"
                            " | \xe7\xab\xaf\xe7\x82\xb9\xe6\x8c\x89\xe4\xbd\x8f\xe8\xbf\x9e\xe6\x8e\xa5")
                            // 单选模式：点击选中 | 按住拖动 | 端点按住连接
        : QString::fromUtf8("\xe5\xa4\x9a\xe9\x80\x89\xe6\xa8\xa1\xe5\xbc\x8f\xef\xbc\x9a"
                            "\xe7\x82\xb9\xe5\x87\xbb\xe5\x8a\xa0\xe5\x87\x8f\xe9\x80\x89"
                            " | \xe6\xa1\x86\xe9\x80\x89"
                            " | \xe5\xb7\xb2\xe9\x80\x89\xe6\x8c\x89\xe4\xbd\x8f\xe6\x8b\x96\xe5\x8a\xa8");
                            // 多选模式：点击加减选 | 框选 | 已选按住拖动
    showToast(text);
}

void ToolSelect::showToast(const QString& text)
{
    // Toast infrastructure lives on the scene (shared by every tool).
    if (m_scene)
        m_scene->showToast(text);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Selection management (toggle + red highlight)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::toggleBlock(const QUuid& blockId)
{
    // 点成员=单选线段: canvas 上的点击只切换被点击的这一条线段.
    const bool anySelected = m_selection.contains(blockId);
    if (anySelected) m_selection.remove(blockId);
    else             m_selection.insert(blockId);

    syncSelectionVisual();
    setState(m_selection.isEmpty() ? SelectState::Idle : SelectState::Selecting);
    notifyEditTarget();
}

void ToolSelect::syncSelectionVisual()
{
    if (!m_scene || !m_paramDoc) return;
    for (const auto& blk : m_paramDoc->blocks()) {
        if (BlockItem* bi = m_scene->findBlockItem(blk.id)) {
            const bool inSel = m_selection.contains(blk.id);
            bi->setToolSelected(inSel);
            // 2026-09 取消确认基准: 选中即加粗 (原 confirmed = bold).
            bi->setToolLocked(inSel);
        }
    }
}

void ToolSelect::setBlockHighlight(const QUuid& blockId, bool on)
{
    if (!m_scene) return;
    if (BlockItem* bi = m_scene->findBlockItem(blockId))
        bi->setToolSelected(on);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Mouse events — main dispatch
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    const QPointF up = event->scenePos();            // user coords (+Y up)
    const cad::geo::Vec2 pos(up.x(), up.y());

    // ── Connect gesture owns input while active (Connecting / ConfirmTarget /
    //    AngleInput): the HUD takes all input, ConfirmTarget clicks resolve
    //    the overlap, AngleInput forwards clicks so a component connection
    //    whose spot has overlapping endpoints can re-pick the follower's
    //    leader segment (点选重叠线段确定跟随对象), Connecting presses are
    //    button-already-down no-ops. ──
    if (m_connectGesture && m_connectGesture->active()) {
        if (m_state == SelectState::AngleInput) {
            // HUD owns keyboard input; clicks on the canvas still reach the
            // gesture so overlapping-leader re-picking works.
            if (event->button() == Qt::LeftButton)
                m_connectGesture->pressAngleTarget(pos);
            return;
        }
        if (event->button() == Qt::RightButton) {
            m_connectGesture->cancel();  // right-click cancels the gesture
            return;
        }
        if (event->button() == Qt::LeftButton
            && m_state == SelectState::ConfirmTarget)
            m_connectGesture->pressConfirmTarget(pos);
        else if (event->button() == Qt::LeftButton
                 && m_state == SelectState::ConfirmSource)
            m_connectGesture->pressConfirmSource(pos);
        return;
    }

    // ── Right button: 无选中空白=切智能笔, 有选中=上下文菜单; 重叠处
    //    追加「重叠候选」子菜单 (B' 方案: 显式点名选哪条, 复用右键菜单语言). ──
    // 2026-09 取消确认基准: 右键不再确认 (单击即选中 / 按住即拖动),
    // 右键直接弹上下文菜单; 无选中空白右键切回智能笔 (two-way 手势反向腿).
    if (event->button() == Qt::RightButton) {
        // 重叠候选先生成: 菜单项需要列出全部名单 (无论当前有无选中).
        const auto cands = collectOverlapCandidates(pos);
        QMenu menu;
        QAction* actCancel = nullptr;
        QAction* actComponent = nullptr;
        QMenu* overlapMenu = nullptr;
        if (!m_selection.isEmpty()) {
            if (m_selection.size() >= 2)
                actComponent = menu.addAction(QString::fromUtf8("创建组件"));
            actCancel = menu.addAction(QString::fromUtf8("取消选择"));
        }
        if (cands.size() >= 2) {
            overlapMenu = menu.addMenu(QString::fromUtf8("重叠候选 (%1 条)").arg(cands.size()));
            for (int i = 0; i < cands.size(); ++i) {
                const auto& c = cands[i];
                QString label = QString::fromUtf8("%1 %2").arg(c.roleText, c.name);
                if (!c.layerName.isEmpty())
                    label += QString::fromUtf8(" · %1").arg(c.layerName);
                if (c.lengthMm > 0.0)
                    label += QString::fromUtf8(" · %1").arg(
                        cad::geo::Units::formatLength(c.lengthMm));
                QAction* act = overlapMenu->addAction(label);
                act->setProperty("overlapPick", i);
            }
        }
        if (overlapMenu == nullptr && actCancel == nullptr && actComponent == nullptr
            && cands.size() < 2) {
            // 既有行为: 无选中 + 空白右键 = 切智能笔.
            if (m_selection.isEmpty() && hitBlock(pos).isNull())
                requestToolSwitch(ToolType::SmartPen);
            return;
        }
        QAction* chosen = menu.exec(QCursor::pos());
        if (chosen == actCancel) {
            deactivateOverlapContext();
            clearSelectionAndIdle();
        } else if (chosen && chosen == actComponent && actComponent) {
            deactivateOverlapContext();
            createComponentFromSelection();
        } else if (chosen && chosen->property("overlapPick").isValid()) {
            // 点名选定: 选中后退出循环上下文 (菜单是显式终点, 不再需要 W)。
            pickOverlapCandidate(chosen->property("overlapPick").toInt());
            deactivateOverlapContext();
        }
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    // ── Ctrl+press on a SELECTED block → quick copy drag (快捷复制) ──
    // 2026-09 取消确认基准: 选中即就绪, 无需右键确认.
    if ((event->modifiers() & Qt::ControlModifier)
        && !m_selection.isEmpty() && m_copyDrag) {
        const QUuid blockHit = hitBlock(pos);
        if (!blockHit.isNull() && m_selection.contains(blockHit)) {
            deactivateOverlapContext();  // 复制拖拽与循环上下文互斥
            m_copyDrag->begin(m_selection, blockHit, pos);
            return;
        }
    }

    // ── Point-level press first (端点连接手势 / 曲线锚点拖拽) ──
    // 用户直接抓取已选线端点即可发起端点连接 (直观一抓即连)
    if (tryPointOperation(pos)) {
        deactivateOverlapContext();  // 点级操作接管后退出循环上下文
        return;
    }

    // ── 线身 press: 单选=选中+待定拖动; 多选=增减+待定拖动 ──
    // 2026-09 取消确认基准: press 后不立即拖 — 移动超过 kDragThresholdPx
    // 由 mouseMove 判定进入 Dragging (锚点 = press 位置); release 未触发
    // = 单击语义 (单选保持选中; 多选按 wasSelected 增减).
    switch (m_state) {
    case SelectState::Idle:
    case SelectState::Selecting: {
        const QUuid blockHit = hitBlock(pos);
        if (m_selectionMode == SelectionMode::Single) {
            // Single mode: press selects, holding moves (no right-click step).
            // 点击空白清选; 无框选.
            if (!blockHit.isNull()) {
                m_lastHitSegmentId = hitSegmentAt(pos);
                m_selection = {blockHit};
                syncSelectionVisual();
                setState(SelectState::Selecting);
                notifyEditTarget();
                beginPressPending(pos, blockHit, /*wasSelected=*/false);
                // 重叠线段消歧 (2026-10): 单击集群 ≥2 候选 → 激活 W 循环上下文
                // (HUD 显示第 i/N + 候选名); 单条 / 异处点击 → 退出循环态.
                const auto cands = collectOverlapCandidates(pos);
                if (cands.size() >= 2)
                    activateOverlapContext(cands, blockHit, pos);
                else
                    deactivateOverlapContext();
            } else if (!m_selection.isEmpty()) {
                deactivateOverlapContext();
                clearSelectionAndIdle();
            }
            return;
        }
        // Multi mode: click toggles; press on an ALREADY-selected block is a
        // pending drag (release without moving = 减选), on an unselected one
        // it joins immediately (release without moving = 保持). W 循环仅在
        // 单选模式生效 — 多选有加减选语义, 循环上下文不激活.
        if (!blockHit.isNull()) {
            deactivateOverlapContext();
            const bool wasSelected = m_selection.contains(blockHit);
            if (wasSelected) {
                beginPressPending(pos, blockHit, /*wasSelected=*/true);
            } else {
                toggleBlock(blockHit);
                beginPressPending(pos, blockHit, /*wasSelected=*/false);
            }
            return;
        }
        // Empty space → marquee.
        deactivateOverlapContext();
        if (m_marqueeGesture) {
            m_marqueeGesture->begin(pos, m_selection);
            setState(SelectState::Marquee);
        }
        break;
    }

    default:
        break;  // Dragging / CopyDragging / Connecting: button already down.
    }
}

void ToolSelect::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 pos(up.x(), up.y());

    // Extracted gestures first (connect → copy), then the anchor-drag
    // override, then the built-in states.
    if (m_connectGesture && m_connectGesture->active()) {
        m_connectGesture->move(pos);
        return;
    }
    if (m_copyDrag && m_copyDrag->active()) {
        m_copyDrag->move(pos);
        return;
    }

    // ── press 后移动超阈值 → 进入拖动 (长按拖动, 2026-09) ──
    // 锚点 = press 位置 (m_pressPos), 与旧"press 即拖"的位移语义一致.
    // 不依赖 event->buttons() — 测试注入的 move 事件 buttons 可能为空,
    // 真实拖动中 press→release 期间的任意 move 都算拖动意图.
    if (m_pressPending) {
        double zoom = 1.0;
        if (!m_scene->views().isEmpty())
            zoom = m_scene->views().first()->transform().m11();
        const double thresh = kDragThresholdPx / (zoom > 1e-9 ? zoom : 1.0);
        if (pos.distanceTo(m_pressPos) > thresh) {
            cancelPressPending();
            beginDrag(m_pressPos);
        }
        return;   // pending 期间吞掉 move
    }

    // ── 悬停反馈 (2026-09): 无按钮移动 = 提示可操作区域 ──
    // 已选块端点 = 十字 (可连接), 线身 = 抓手 (可拖动), 空白 = 箭头.
    // 拖拽/框选/锚点拖等有按钮态由 buttons() 天然排除.
    if (event->buttons() == Qt::NoButton
        && !m_anchorDragging
        && m_state != SelectState::Dragging
        && m_state != SelectState::Marquee) {
        updateHoverCursor(pos);
        return;
    }

    // Curve anchor drag override
    if (m_anchorDragging) { updateAnchorDrag(pos); return; }

    switch (m_state) {
    case SelectState::Marquee:
        if (m_marqueeGesture) m_marqueeGesture->update(pos);
        break;
    case SelectState::Dragging:     updateDrag(pos);     break;
    default: break;
    }
}

void ToolSelect::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 pos(up.x(), up.y());

    // Extracted gestures first (copy → connect), then the anchor-drag
    // override, then the built-in states.
    if (m_copyDrag && m_copyDrag->active()) {
        m_copyDrag->release(pos);
        return;
    }
    if (m_connectGesture && m_connectGesture->active()
        && m_state == SelectState::Connecting) {
        m_connectGesture->release(pos);
        return;
    }

    // Curve anchor drag override
    if (m_anchorDragging) { endAnchorDrag(); return; }

    // ── press 未触发拖动 = 单击 (2026-09 长按拖动判定) ──
    if (m_pressPending) {
        const QUuid pendingBlock = m_pressBlockId;
        const bool wasSelected = m_pressWasSelected;
        cancelPressPending();
        // 多选: 单击已选块 = 减选 (press 时已选才减; press 时未选已在
        // mousePress 加入, release 保持). 单选: 单击保持选中 (无操作).
        if (m_selectionMode == SelectionMode::Multi
            && wasSelected && !pendingBlock.isNull())
            toggleBlock(pendingBlock);
        return;
    }

    switch (m_state) {
    case SelectState::Marquee: {
        // The gesture applies the toggle (base XOR hits);
        // the tool owns the selection-state transitions.
        if (m_marqueeGesture)
            m_selection = m_marqueeGesture->end(pos);
        syncSelectionVisual();
        setState(m_selection.isEmpty() ? SelectState::Idle
                                       : SelectState::Selecting);
        break;
    }
    case SelectState::Dragging:     endDrag(pos);     break;
    default: break;
    }
}

void ToolSelect::mouseDoubleClick(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;
    if (m_state == SelectState::AngleInput) return;

    const QPointF up = event->scenePos();
    const cad::geo::Vec2 clickPos(up.x(), up.y());

    // Pick rule identical to press (hitBlock): the FIRST ACTIVE-LAYER block
    // under the cursor wins — items on grayed (non-active) layers are skipped,
    // not a veto. The old loop stopped at the first BlockItem and bailed when
    // its layer was not active: at a crossing of an aux/working segment the
    // topmost item was often the off-layer (grayed) block, so the double-click
    // silently died even though an active-layer segment was right under the
    // cursor — and single-click selection (hitBlock scan) worked fine. 修复:
    // 辅助层激活时在交叉点双击辅助线也能稳定打开属性面板; 工作层激活时
    // 双击交叉点则稳定命中工作层线.
    const QUuid blockId = hitBlock(clickPos);
    if (blockId.isNull()) return;
    cad::param::Block* block = m_paramDoc->findBlock(blockId);
    if (!block || block->segments.empty()) return;
    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    if (zoom < 1e-9) zoom = 1.0;
    constexpr double kTolerancePx = 8.0;
    const double tolerance = kTolerancePx / zoom;

    QUuid bestSegId;
    double bestDist = std::numeric_limits<double>::max();
    for (const auto& seg : block->segments) {
        const auto* sp = block->findPoint(seg.startPointId);
        const auto* ep = block->findPoint(seg.endPointId);
        if (!sp || !ep || !sp->resolved || !ep->resolved) continue;

        double d;
        if (seg.isCurve()) {
            // Curve: distance to the actual Bézier path, not the chord — a bent
            // curve sits far from its chord, so a chord-only test would fail to
            // open the dialog when double-clicking the visible curve.
            std::vector<cad::geo::Vec2> pts;
            std::vector<cad::geo::Vec2> tIn, tOut;
            std::vector<bool> autoTan;
            pts.push_back(sp->resolvedPos);
            tIn.push_back(sp->tangentIn); tOut.push_back(sp->tangentOut); autoTan.push_back(sp->autoTangent);
            for (const auto& ppId : seg.passPointIds) {
                const auto* pp = block->findPoint(ppId);
                if (!pp || !pp->resolved) continue;
                pts.push_back(pp->resolvedPos);
                tIn.push_back(pp->tangentIn); tOut.push_back(pp->tangentOut); autoTan.push_back(pp->autoTangent);
            }
            pts.push_back(ep->resolvedPos);
            tIn.push_back(ep->tangentIn); tOut.push_back(ep->tangentOut); autoTan.push_back(ep->autoTangent);
            auto spans = cad::geo::buildBezierSpans(pts, tIn, tOut, autoTan, seg.tension,
                                                   cad::geo::AutoCurveMode::Hobby);
            if (spans.empty()) continue;
            auto proj = cad::geo::projectPointOnCurve(
                block->transform.toLocal(clickPos), spans);
            d = proj.valid ? proj.distance : std::numeric_limits<double>::max();
        } else {
            const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
            const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
            d = cad::geo::Vec2::distanceToSegment(clickPos, w1, w2);
        }
        if (d < bestDist) { bestDist = d; bestSegId = seg.id; }
    }
    if (bestSegId.isNull() || bestDist > tolerance) return;

    QWidget* parentWidget = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    auto* dlg = new LinePropertyDialog(blockId, bestSegId, m_paramDoc,
                                       m_scene, parentWidget);
    // NOTE: no WA_DeleteOnClose — the dialog schedules its own deleteLater()
    // on accept/reject (ElaAppBar's default-close path would delete it while
    // its own handler still reads window->windowHandle()).
    dlg->show();
}

void ToolSelect::keyPress(QKeyEvent* event)
{
    // Connect gesture owns keys while active (AngleInput HUD + Esc cancels).
    // Only keys the gesture CONSUMED are swallowed — anything else (Ctrl+Z /
    // Delete / tool hotkeys) must fall through to the normal handling.
    if (m_connectGesture && m_connectGesture->active()
        && m_connectGesture->keyPress(event)) {
        event->accept();
        return;
    }
    if (m_state == SelectState::CopyDragging) {
        if (event->key() == Qt::Key_Escape) {
            if (m_copyDrag) m_copyDrag->cancel();
            event->accept();
        }
        return;
    }

    if (event->key() == Qt::Key_Escape && m_overlapIndex >= 0) {
        // 重叠循环上下文优先: Esc 只退出循环 (保留当前选中), 不触发清除.
        deactivateOverlapContext();
        event->accept();
    } else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelectedBlocks();
        event->accept();
    } else if (event->key() == Qt::Key_W) {
        // 重叠循环上下文激活时 W = 循环候选 (2026-10); 其余场合照旧切换
        // 多选 ↔ 单选 mode (same leader key as other tools' mode switches).
        if (m_overlapIndex >= 0)
            cycleOverlapCandidate();
        else
            toggleSelectionMode();
        event->accept();
    } else if (event->key() == Qt::Key_D
               && !(m_connectGesture && m_connectGesture->active())) {
        // 快拆 (D = Detach, 用户拍板 2026-09): 解除选中线连接的位置吸附、
        // 保留角度跟随 (angleOnly)。连接手势激活时屏蔽 (手势内 D 不应拆
        // 刚建立的连接)。
        quickDetachSelection();
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
        if (m_state == SelectState::Dragging) {
            // Abort the move: restore pre-drag origins (cross-boundary
            // attachments are only broken at endDrag, so nothing to re-add).
            for (const QUuid& id : m_dragBlockIds) {
                if (auto* b = m_paramDoc->findBlock(id))
                    b->transform.origin = m_dragOrigins.value(id);
            }
            m_paramDoc->resolveAll();
            m_scene->refreshAllBlockItems();
            setState(SelectState::Selecting);  // 回到选中态 (2026-09 无确认态)
        } else {
            if (m_marqueeGesture) m_marqueeGesture->cancel();
            clearSelectionAndIdle();
        }
        event->accept();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Marquee selection (intersect = select; toggle add/remove)
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::beginDrag(const cad::geo::Vec2& pos)
{
    hideOverlapHint();  // 拖动期间隐藏重叠 HUD (悬停不刷新)
    m_dragStartPos = pos;
    m_detachedAttachments.clear();

    // Drag set = the confirmed selection (expanded by protected connections),
    // then expanded to full rigid components (dragging any member moves the
    // whole component — 刚体组件整体拖动).
    const QSet<QUuid> dragSet =
        m_paramDoc->componentClosure(m_paramDoc->lockedClosure(m_selection));
    m_dragBlockIds = dragSet.values();
    m_dragOrigins.clear();
    for (const QUuid& id : m_dragBlockIds) {
        if (const auto* b = m_paramDoc->findBlock(id))
            m_dragOrigins.insert(id, b->transform.origin);
    }

    // Auto-disconnect (方向感知拆除): only when the FOLLOWER itself is
    // dragged away from its leader (fromIn && !toIn) is a connection torn
    // apart — 拖跟随线 = 拆散 (门开着); dragging the LEADER keeps the
    // follower attached so it follows (跟随线跟随, 不拆). 拖动保护
    // (isLocked) attachments are welded: never detached by a drag.
    // 注: **新建连接默认焊接** (用户拍板 2026-08 复旧) — 拖动保护默认勾选,
    // 拖跟随线即整对移动不拆; 拆散 = D 键快拆 / 面板取消「拖动保护」, 或把
    // 多线打包成组件 (componentClosure 整体移动)。已拆开 (angleOnly) 的连接
    // 位置本就自由, 无需再拆。滑轨连接 (slideMode != None) 位置只留一轴
    // 自由度, 拖动必须保持滑轨约束 (抽屉式滑动) —— 也不拆, 由 Resolver
    // 每帧锁轴。
    // 组件级连接 (整组跟随外部线): 拖动组件任一部分 → 拆散外部跟随线
    // (与"拖 follower 拆散"语义一致 — 组件整体被拖走, 不再跟随外部线).
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromComponentId.isNull()) continue;
        const cad::param::Component* c = m_paramDoc->findComponent(att.fromComponentId);
        if (!c) continue;
        bool memberIn = false;
        for (const QUuid& mid : c->memberBlockIds)
            if (dragSet.contains(mid)) { memberIn = true; break; }
        if (memberIn) {
            m_detachedAttachments.append(att.id);
            continue;
        }
    }
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.isLocked || att.angleOnly
            || att.slideMode != cad::param::SlideMode::None) continue;
        const bool fromIn = dragSet.contains(att.fromBlockId);
        const bool toIn   = dragSet.contains(att.toBlockId);
        if (fromIn && !toIn)
            m_detachedAttachments.append(att.id);
    }

    // 滑轨模式 (抽屉式滑动, 用户拍板 2026-08): slide attachments are kept
    // ACTIVE (never detached — dragging a slide follower stays on the rail).
    // Snapshot their pre-drag rail coordinates so the endDrag macro can undo
    // the slide back to the pre-drag rail position.
    m_dragOldSlideOffsets.clear();
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.slideMode == cad::param::SlideMode::None) continue;
        if (!dragSet.contains(att.fromBlockId)) continue;
        m_dragOldSlideOffsets.insert(att.id,
            {att.slideAlongMm, att.slidePerpMm});
    }

    setState(SelectState::Dragging);
}

void ToolSelect::updateDrag(const cad::geo::Vec2& pos)
{
    const cad::geo::Vec2 delta = pos - m_dragStartPos;
    for (const QUuid& id : m_dragBlockIds) {
        if (auto* b = m_paramDoc->findBlock(id))
            b->transform.origin = m_dragOrigins.value(id) + delta;
    }
    // Live resolve every frame: followers cascade in real time (same pattern
    // as updateConnect / updateAnchorDrag). resolveForDrag() emits resolved()
    // → the scene syncs block positions; panels stay silent until the gesture
    // commits (endDrag pushes an undo command whose resolveAll() refreshes).
    // Seeds = the dragged set; cross-selection attachments pending removal are
    // ignored so a dragged follower is not pulled back to its leader.
    for (const QUuid& id : m_dragBlockIds) {
        if (auto* b = m_paramDoc->findBlock(id))
            m_paramDoc->invalidateLayer(b->layer);
    }
    // 滑轨模式 (抽屉式滑动): 拖动跟随线时每帧把当前 (拖拽) 位置回写到
    // 自由轴坐标 (锁轴保持激活时快照), 随后 resolveForDrag 按新坐标落位,
    // 锁轴分量被弹回 —— 跟随线只沿滑轨动。
    for (const QUuid& attId : m_dragOldSlideOffsets.keys())
        m_paramDoc->updateSlideOffsetsFromCurrent(attId);
    m_paramDoc->resolveForDrag(m_dragBlockIds, m_detachedAttachments);
}

bool ToolSelect::tryReattachOnDragEnd(const cad::geo::Vec2& pos)
{
    if (!m_paramDoc || !m_undoStack) return false;
    // 只在单一跟随线拖拽释放时做“位置重挂 + 角度基准保留”，避免多选拖动语义复杂化。
    if (m_detachedAttachments.size() != 1 || m_dragBlockIds.size() != 1)
        return false;

    const cad::param::Attachment* att = m_paramDoc->findAttachment(m_detachedAttachments.first());
    if (!att || att->isPin || !att->fromComponentId.isNull())
        return false;

    double zoom = 1.0;
    if (m_scene && !m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    const auto snap = m_snapEngine.findSnap(
        pos, m_paramDoc, zoom, kConnectSnapRadius, {}, &att->fromBlockId);
    if (!snap || snap->blockId == att->fromBlockId)
        return false;

    const auto* leader = m_paramDoc->findBlock(snap->blockId);
    const QUuid segId = leader ? leader->exitSegmentAtPoint(snap->pointId) : QUuid();
    if (segId.isNull()) return false;

    m_undoStack->beginMacro(QStringLiteral("重新挂接"));
    m_undoStack->push(new cad::cmd::ReattachAttachmentCommand(
        m_paramDoc, att->id, snap->blockId, snap->pointId, segId));
    m_undoStack->endMacro();
    return true;
}


void ToolSelect::endDrag(const cad::geo::Vec2& pos)
{
    const cad::geo::Vec2 delta = pos - m_dragStartPos;

    // Restore originals, then re-apply through the undo stack.
    for (const QUuid& id : m_dragBlockIds) {
        if (auto* b = m_paramDoc->findBlock(id))
            b->transform.origin = m_dragOrigins.value(id);
    }

    // A pure click (press + release without moving) is NOT a drag: keep the
    // selection — 按住移动才是拖动, 几乎没动的拖动按单击回退 (2026-09;
    // 双击编辑保持完好).
    if (delta.lengthSquared() <= 1e-10) {
        if (m_scene) m_scene->refreshAllBlockItems();
        setState(SelectState::Selecting);
        return;
    }

    if (m_undoStack && !tryReattachOnDragEnd(pos)) {
        m_undoStack->beginMacro(QStringLiteral(
            "\xe7\xa7\xbb\xe5\x8a\xa8 %1 \xe4\xb8\xaa\xe5\xaf\xb9\xe8\xb1\xa1").arg(m_dragBlockIds.size()));
        for (const QUuid& attId : m_detachedAttachments) {
            const cad::param::Attachment* att = m_paramDoc->findAttachment(attId);
            if (att && !att->isPin && att->fromComponentId.isNull())
                // 拆开保留角度 (用户拍板 2026-08): 只解除位置吸附, 角度跟随
                // 保留; 桥 pin / 组件级连接 无角度语义, 仍走彻底删除.
                m_undoStack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
                    m_paramDoc, attId, /*angleOnly=*/true));
            else
                m_undoStack->push(new cad::cmd::RemoveAttachmentCommand(
                    m_paramDoc, attId));
        }
        // 滑轨模式 (抽屉式滑动): 拖动沿滑轨走了 —— 自由轴坐标 old→new 与
        // 移动一起入栈 (undo 整体回到拖前滑轨位置).
        for (auto it = m_dragOldSlideOffsets.cbegin();
             it != m_dragOldSlideOffsets.cend(); ++it) {
            const cad::param::Attachment* att = m_paramDoc->findAttachment(it.key());
            if (!att) continue;
            const auto [oldAlong, oldPerp] = it.value();
            if (std::abs(att->slideAlongMm - oldAlong) <= 1e-9
                && std::abs(att->slidePerpMm - oldPerp) <= 1e-9) continue;
            m_undoStack->push(new cad::cmd::SetSlideOffsetsCommand(
                m_paramDoc, it.key(), oldAlong, oldPerp,
                att->slideAlongMm, att->slidePerpMm));
        }
        m_undoStack->push(new cad::cmd::MoveBlockCommand(m_paramDoc, m_dragBlockIds, delta));
        m_undoStack->endMacro();
    }

    m_scene->refreshAllBlockItems();

    // Requirement: after a move, drop the selection and return to Idle.
    clearSelectionAndIdle();
}

bool ToolSelect::tryPointOperation(const cad::geo::Vec2& pos)
{
    if (m_selection.isEmpty()) return false;

    // Curve anchor first: press near a pass point of any SELECTED curve.
    double zoom = 1.0;
    if (m_scene && !m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    const auto anchorHit = hitCurveAnchor(pos, zoom);
    if (anchorHit) {
        beginAnchorDrag(anchorHit->first, anchorHit->second);
        return true;
    }

    if (!m_connectGesture || !m_paramDoc) return false;

    // Endpoint: begin a connection drag. Gather all candidate points stacked at
    // this location so we can pick a valid point even when multiple endpoints
    // share the spot.
    const auto cands = m_connectGesture->hitPointCandidates(pos);
    if (cands.empty()) return false;

    // Filter to valid source candidates (same rules as before: eligible,
    // non-bridge, active layer, no external welded attachment).
    std::vector<SnapResult> validCands;
    for (const auto& c : cands) {
        if (!m_selection.contains(c.blockId))
            continue;
        const auto* blk = m_paramDoc->findBlock(c.blockId);
        if (!blk || blk->isBridge) continue;
        if (blk->layer != m_paramDoc->activeLayer()) continue;

        // 组件成员: 抓端点 = 组件级连接 (整组跟随, 借用端点), 不占用线级
        // follower 名额 —— 放行 (内部关系保持活性). 非组件成员: 线级连接
        // 检查照旧 (森林不变式 线级: 已是 follower 的块不能发起线级连接;
        // **例外**: 仅角度 angleOnly 跟随线位置自由 — 放行, 让它能拖端点
        // 重新建立位置连接, 用户报告: 使用了引用线段但没有连接线段的线,
        // 拖动端点没有任何吸附反应)。
        if (!m_paramDoc->componentOfBlock(c.blockId)) {
            bool externalWelded = false;
            for (const auto& att : m_paramDoc->attachments()) {
                if (!att.isLocked) continue;
                if (att.fromBlockId == c.blockId
                    || (att.toBlockId == c.blockId && att.toPointId == c.pointId)) {
                    externalWelded = true;
                    break;
                }
            }
            if (externalWelded) continue;

            bool isFollower = false;
            bool angleOnlyFree = false;
            for (const auto& att : m_paramDoc->attachments())
                if (att.fromBlockId == c.blockId) {
                    isFollower = true;
                    if (!att.isPin && att.angleOnly) angleOnlyFree = true;
                    break;
                }
            if (isFollower && !angleOnlyFree) continue;
        }

        validCands.push_back(c);
    }

    // 方案A: 源端重叠确认. When several valid source endpoints share the same
    // spot, do NOT silently pick the first one — enter ConfirmSource so the
    // user clicks the member segment whose endpoint should be the hinge point.
    if (validCands.size() >= 2) {
        const cad::geo::Vec2 refPos = validCands.front().worldPos;
        std::vector<SnapResult> overlap;
        for (const auto& c : validCands)
            if (c.worldPos.distanceTo(refPos) < kSnapOverlapEps)
                overlap.push_back(c);

        if (overlap.size() > 1) {
            std::vector<ConfirmCandidate> sourceCandidates;
            for (const auto& c : overlap) {
                const auto* blk = m_paramDoc->findBlock(c.blockId);
                if (!blk) continue;
                for (const auto& seg : blk->segments) {
                    if (seg.startPointId == c.pointId || seg.endPointId == c.pointId)
                        sourceCandidates.push_back({c.blockId, seg.id, c.pointId});
                }
            }
            if (!sourceCandidates.empty()) {
                m_connectGesture->beginSourceConfirm(
                    std::move(sourceCandidates), pos);
                return true;
            }
        }
    }

    // 所有候选都被过滤掉 (按下的是未选线段 / 桥 / 已焊接或 follower 的端点):
    // 这不是连接手势 — 退回普通选中/拖动路径 (未选线段的端点按下 = 普通线段
    // 选中)。缺此守卫 validCands.front() 会在空 vector 上断言崩溃.
    if (validCands.empty())
        return false;

    // 单选模式源点确定 (用户设计提案 2026-09, 已用 test_select_wkey 回归锁定):
    // 重叠点集合中属于当前选中线段的点恒胜出 — validCands 已按"选中块"过滤
    // (上面 for 循环), 无重叠时直接取选中块的最近端点即可 (validCands 按距离
    // 升序, front = 最近者). 行为与旧"for 循环首选选中块"完全等价, 但意图
    // 显式化: 先点选线段、再点击重叠部位 = 默认选中该线段的所属点.
    const SnapResult& pickedCand = validCands.front();
    m_connectGesture->beginConnect(pickedCand.blockId, pickedCand.pointId, pos);
    return true;
}

QUuid ToolSelect::hitBlock(const cad::geo::Vec2& worldPos) const
{
    if (!m_scene) return QUuid();
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const QList<QGraphicsItem*> hits = m_scene->items(scenePt);
    for (QGraphicsItem* item : hits) {
        // Curve children belong to their block — walk up to the BlockItem.
        if (auto* bi = BlockItem::containingItem(item)) {
            // Only blocks on the active layer are selectable.
            if (const auto* blk = m_paramDoc ? m_paramDoc->findBlock(bi->blockId()) : nullptr;
                blk && blk->layer == m_paramDoc->activeLayer())
                return bi->blockId();
        }
    }
    return QUuid();
}

QUuid ToolSelect::hitSegmentAt(const cad::geo::Vec2& worldPos) const
{
    if (!m_scene) return QUuid();
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const QList<QGraphicsItem*> hits = m_scene->items(scenePt);
    for (QGraphicsItem* item : hits) {
        if (auto* bi = BlockItem::containingItem(item)) {
            if (const auto* blk = m_paramDoc ? m_paramDoc->findBlock(bi->blockId()) : nullptr;
                blk && blk->layer == m_paramDoc->activeLayer())
                return bi->hitSegmentAtScene(scenePt);
        }
    }
    return QUuid();
}

void ToolSelect::notifyEditTarget()
{
    if (!m_editTargetCb) return;
    QUuid blockId, segId;
    if (m_selection.size() == 1) {
        blockId = *m_selection.begin();
        segId = m_lastHitSegmentId;
        if (segId.isNull() && m_paramDoc) {
            // Fallback: first segment of the block (e.g. external selection).
            if (const auto* blk = m_paramDoc->findBlock(blockId); blk && !blk->segments.empty())
                segId = blk->segments.front().id;
        }
    }
    m_editTargetCb(blockId, segId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Visual helpers
// ═══════════════════════════════════════════════════════════════════════════════

void ToolSelect::createComponentFromSelection()
{
    if (!m_paramDoc || m_selection.size() < 2) return;

    // Stable member order = document block order (anchor = earliest block).
    QList<QUuid> members;
    for (const auto& b : m_paramDoc->blocks())
        if (m_selection.contains(b.id))
            members.append(b.id);
    if (members.size() < 2) return;

    const QString name = QStringLiteral("组件 %1")
        .arg(m_paramDoc->components().size() + 1);
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::MakeComponentCommand(m_paramDoc, members, name));
    else {
        cad::param::Component c;
        c.name = name;
        c.memberBlockIds.assign(members.begin(), members.end());
        m_paramDoc->addComponent(c);
    }

    showToast(QStringLiteral("已创建组件（%1 条线段）——拖任一成员整组移动").arg(members.size()));
    clearSelectionAndIdle();
}

void ToolSelect::deleteSelectedBlocks()
{
    if (!m_scene || !m_paramDoc || m_selection.isEmpty()) return;

    const QList<QUuid> toRemove = m_selection.values();

    // Show the aggregated delete-impact report before committing (批量删除
    // 时善后可能合并, 报告计数为上限). The cascade itself is unchanged.
    // Parent = the scene view so the confirm box stays on the right screen.
    QWidget* const parent = m_scene->views().isEmpty()
        ? nullptr : m_scene->views().first();
    if (!cad::doc::confirmDeleteImpact(parent, m_paramDoc, toRemove))
        return;

    if (m_undoStack) {
        m_undoStack->beginMacro(QStringLiteral(
            "\xe5\x88\xa0\xe9\x99\xa4 %1 \xe6\x9d\xa1\xe7\xba\xbf\xe6\xae\xb5").arg(toRemove.size()));
        for (const QUuid& id : toRemove)
            m_undoStack->push(new cad::cmd::DeleteBlockCommand(m_paramDoc, id));
        m_undoStack->endMacro();
    } else {
        for (const QUuid& id : toRemove)
            m_paramDoc->removeBlock(id);
    }

    m_scene->refreshAllBlockItems();
    clearSelectionAndIdle();
}

void ToolSelect::quickDetachSelection()
{
    if (!m_paramDoc || m_selection.isEmpty()) return;

    // 快拆 = 拆开保留角度 (angleOnly): 位置约束解除、角度仍跟随基准线。
    // 与拖动保护 (isLocked) 无关 — 拆开会自动清锁 (位置自由 ↔ 焊接互斥,
    // setAttachmentAngleOnly 内部处理)。桥线 pin 无角度语义, 跳过
    // (彻底断开走「清除」)。已经是 angleOnly 的连接无需再拆。
    QList<QUuid> toDetach;
    for (const QUuid& blockId : m_selection) {
        for (const auto& att : m_paramDoc->attachments()) {
            if (att.fromBlockId != blockId || att.isPin) continue;
            if (!att.angleOnly) toDetach.append(att.id);
            break;  // 每块至多一条非 pin 跟随连接 (森林不变式)
        }
    }

    if (toDetach.isEmpty()) {
        showToast(QString::fromUtf8(
            "\xe9\x80\x89\xe4\xb8\xad\xe7\xba\xbf\xe6\xb2\xa1\xe6\x9c\x89\xe5\x8f\xaf"
            "\xe6\x8b\x86\xe5\xbc\x80\xe7\x9a\x84\xe8\xbf\x9e\xe6\x8e\xa5"));  // 选中线没有可拆开的连接
        return;
    }

    if (m_undoStack) {
        m_undoStack->beginMacro(QStringLiteral(
            "\xe6\x8b\x86\xe5\xbc\x80 %1 \xe4\xb8\xaa\xe8\xbf\x9e\xe6\x8e\xa5")
            .arg(toDetach.size()));  // 拆开 %1 个连接
        for (const QUuid& id : toDetach)
            m_undoStack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
                m_paramDoc, id, /*angleOnly=*/true));
        m_undoStack->endMacro();
    } else {
        for (const QUuid& id : toDetach)
            m_paramDoc->setAttachmentAngleOnly(id, true);
    }

    m_scene->refreshAllBlockItems();
}

// ── 长按/拖动判定与悬停反馈 (2026-09 取消确认基准) ──

void ToolSelect::beginPressPending(const cad::geo::Vec2& pos, const QUuid& blockId,
                                   bool wasSelected)
{
    m_pressPending = true;
    m_pressBlockId = blockId;
    m_pressWasSelected = wasSelected;
    m_pressPos = pos;
}

void ToolSelect::cancelPressPending()
{
    m_pressPending = false;
    m_pressBlockId = QUuid();
    m_pressWasSelected = false;
}

void ToolSelect::updateHoverCursor(const cad::geo::Vec2& pos)
{
    Qt::CursorShape cur = Qt::ArrowCursor;
    if (m_scene && m_paramDoc) {
        const QUuid blockHit = hitBlock(pos);
        if (!blockHit.isNull() && m_selection.contains(blockHit)) {
            // 已选块的端点 (抓取半径内) → 十字 (提示可连接);
            // 否则线身 → 抓手 (提示可拖动).
            double zoom = 1.0;
            if (!m_scene->views().isEmpty())
                zoom = m_scene->views().first()->transform().m11();
            const double worldR = kConnectGrabRadius / (zoom > 1e-9 ? zoom : 1.0);
            if (blockHasEndpointNear(blockHit, pos, worldR))
                cur = Qt::CrossCursor;
            else
                cur = Qt::OpenHandCursor;
        }
    }
    if (cur != m_hoverCursor) {
        m_hoverCursor = cur;
        if (m_scene && !m_scene->views().isEmpty())
            m_scene->views().first()->viewport()->setCursor(cur);
    }
    // 重叠提示 HUD: 无按钮悬停时跟随 (集群 ≥2 条才显示; 同值短路).
    refreshOverlapHint(pos);
}

bool ToolSelect::blockHasEndpointNear(const QUuid& blockId,
                                      const cad::geo::Vec2& pos,
                                      double worldRadius) const
{
    const auto* blk = m_paramDoc ? m_paramDoc->findBlock(blockId) : nullptr;
    if (!blk) return false;
    const double rSq = worldRadius * worldRadius;
    for (const auto& pt : blk->points) {
        if (!pt.selectable || !pt.resolved) continue;
        if (blk->worldPos(pt.id).distanceSquaredTo(pos) < rSq)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 重叠线段消歧 (2026-10: 悬停提示 → 点选+W 循环 → 右键候选菜单)
// ---------------------------------------------------------------------------

QList<ToolSelect::OverlapCandidate> ToolSelect::collectOverlapCandidates(
    const cad::geo::Vec2& worldPos) const
{
    QList<OverlapCandidate> out;
    if (!m_scene || !m_paramDoc) return out;

    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    // 场景命中返回全部包含该点的 item (堆叠降序): 完全重合的多条线都会出现,
    // 与 hitBlock 取首项的顺序一致 (顶部/后建者在前).
    const QList<QGraphicsItem*> hits = m_scene->items(scenePt);
    QSet<QUuid> seen;
    for (QGraphicsItem* item : hits) {
        auto* bi = BlockItem::containingItem(item);
        if (!bi) continue;
        const QUuid bid = bi->blockId();
        if (seen.contains(bid)) continue;
        seen.insert(bid);
        const auto* blk = m_paramDoc->findBlock(bid);
        if (!blk) continue;
        // 与 hitBlock 同规: 只有活动层可选中 (跨层重叠靠图层过滤区分).
        if (blk->layer != m_paramDoc->activeLayer()) continue;
        out.append(makeOverlapCandidate(*blk, bi->hitSegmentAtScene(scenePt)));
    }
    return out;
}

ToolSelect::OverlapCandidate ToolSelect::makeOverlapCandidate(
    const cad::param::Block& blk, const QUuid& segmentId) const
{
    OverlapCandidate c;
    c.blockId = blk.id;
    c.segmentId = segmentId;
    const cad::param::Segment* seg = nullptr;
    if (!segmentId.isNull())
        seg = blk.findSegment(segmentId);
    if (!seg && !blk.segments.empty())
        seg = &blk.segments.front();

    // 显示名: 线段名 → 块名 → 线段 serial (可读 ID, 无名的重叠也有身份).
    if (seg && !seg->name.isEmpty())        c.name = seg->name;
    else if (!blk.name.isEmpty())           c.name = blk.name;
    else if (seg && !seg->serial.isEmpty()) c.name = seg->serial;
    else                                    c.name = QString::fromUtf8("(未命名)");

    c.roleText = segmentRoleText(seg ? seg->role : cad::param::SegmentRole::Outline);
    c.layerName.clear();
    for (const auto& l : m_paramDoc->layers())
        if (l.id == blk.layer) { c.layerName = l.name; break; }

    if (seg) {
        const auto* sp = blk.findPoint(seg->startPointId);
        const auto* ep = blk.findPoint(seg->endPointId);
        if (sp && ep && sp->resolved && ep->resolved)
            c.lengthMm = sp->resolvedPos.distanceTo(ep->resolvedPos);
    }
    return c;
}

void ToolSelect::activateOverlapContext(const QList<OverlapCandidate>& cands,
                                        const QUuid& hitBlockId,
                                        const cad::geo::Vec2& anchor)
{
    if (cands.size() < 2) { deactivateOverlapContext(); return; }
    m_overlapCandidates = cands;
    // 与单击命中一致: 首项 = hitBlock 首选; 找不到时取 0.
    int idx = 0;
    for (int i = 0; i < m_overlapCandidates.size(); ++i)
        if (m_overlapCandidates[i].blockId == hitBlockId) { idx = i; break; }
    m_overlapIndex = idx;
    m_overlapAnchor = anchor;
    applyOverlapPick(m_overlapIndex);
    // 常驻循环 HUD (锚定集群, 不随光标走).
    const auto& c = m_overlapCandidates[m_overlapIndex];
    showOverlapHint(QString::fromUtf8("重叠 %1 条 ｜ 第 %2/%3 ｜ %4 %5（W 循环）")
                        .arg(m_overlapCandidates.size())
                        .arg(m_overlapIndex + 1)
                        .arg(m_overlapCandidates.size())
                        .arg(c.roleText)
                        .arg(c.name),
                    m_overlapAnchor);
}

void ToolSelect::deactivateOverlapContext()
{
    m_overlapIndex = -1;
    m_overlapCandidates.clear();
    hideOverlapHint();
}

void ToolSelect::cycleOverlapCandidate()
{
    if (m_overlapIndex < 0 || m_overlapCandidates.isEmpty()) return;
    // 拖拽/框选/待定中不接收循环 (键盘事件与状态机无关联).
    if (m_state == SelectState::Dragging || m_state == SelectState::Marquee
        || m_pressPending) return;

    // 剔除已消失的块 (拖走/删除后名单失效), 实时重取身份信息.
    QList<OverlapCandidate> live;
    for (const auto& c : m_overlapCandidates) {
        const auto* blk = m_paramDoc->findBlock(c.blockId);
        if (!blk) continue;
        live.append(makeOverlapCandidate(*blk, c.segmentId));
    }
    if (live.isEmpty()) { deactivateOverlapContext(); return; }
    if (live.size() != m_overlapCandidates.size())
        m_overlapIndex = qBound(0, m_overlapIndex, live.size() - 1);
    m_overlapCandidates = live;

    m_overlapIndex = (m_overlapIndex + 1) % m_overlapCandidates.size();
    applyOverlapPick(m_overlapIndex);
    const auto& c = m_overlapCandidates[m_overlapIndex];
    showOverlapHint(QString::fromUtf8("重叠 %1 条 ｜ 第 %2/%3 ｜ %4 %5（W 循环）")
                        .arg(m_overlapCandidates.size())
                        .arg(m_overlapIndex + 1)
                        .arg(m_overlapCandidates.size())
                        .arg(c.roleText)
                        .arg(c.name),
                    m_overlapAnchor);
}

void ToolSelect::applyOverlapPick(int index)
{
    if (index < 0 || index >= m_overlapCandidates.size()) return;
    const auto& c = m_overlapCandidates[index];
    m_selection = {c.blockId};
    m_lastHitSegmentId = c.segmentId;
    syncSelectionVisual();
    setState(SelectState::Selecting);
    notifyEditTarget();
}

void ToolSelect::pickOverlapCandidate(int index)
{
    if (index < 0 || index >= m_overlapCandidates.size()) return;
    m_overlapIndex = index;
    applyOverlapPick(index);
    const auto& c = m_overlapCandidates[index];
    showOverlapHint(QString::fromUtf8("重叠 %1 条 ｜ 第 %2/%3 ｜ %4 %5（W 循环）")
                        .arg(m_overlapCandidates.size())
                        .arg(index + 1)
                        .arg(m_overlapCandidates.size())
                        .arg(c.roleText)
                        .arg(c.name),
                    m_overlapAnchor);
}

void ToolSelect::refreshOverlapHint(const cad::geo::Vec2& worldPos)
{
    if (m_overlapIndex >= 0 && !m_overlapCandidates.isEmpty()) {
        // 循环上下文已存在: HUD 锚定集群位置, 不随光标移动; 同值短路由
        // showOverlapHint 内部处理 (拖帧路径零重建).
        const auto& c = m_overlapCandidates[m_overlapIndex];
        showOverlapHint(QString::fromUtf8("重叠 %1 条 ｜ 第 %2/%3 ｜ %4 %5（W 循环）")
                            .arg(m_overlapCandidates.size())
                            .arg(m_overlapIndex + 1)
                            .arg(m_overlapCandidates.size())
                            .arg(c.roleText)
                            .arg(c.name),
                        m_overlapAnchor);
        return;
    }

    const auto cands = collectOverlapCandidates(worldPos);
    if (cands.size() >= 2) {
        QString text = QString::fromUtf8("此处重叠 %1 条 ｜").arg(cands.size());
        for (int i = 0; i < cands.size(); ++i) {
            if (i) text += QStringLiteral("、");
            text += cands[i].name;
        }
        text += QString::fromUtf8("（点选后按 W 循环）");
        showOverlapHint(text, worldPos);
    } else {
        hideOverlapHint();
    }
}

void ToolSelect::showOverlapHint(const QString& text, const cad::geo::Vec2& anchor)
{
    if (!m_scene) return;
    if (text == m_overlapHitText && m_overlapHintBox && m_overlapHintBox->isVisible())
        return;  // 同值短路: 悬停/拖帧路径不重构 HUD

    if (!m_overlapHintBox) {
        m_overlapHintBox = new QGraphicsRectItem();
        m_overlapHintBox->setZValue(9990.0);
        m_scene->addItem(m_overlapHintBox);
        m_overlapHintLabel = new QGraphicsSimpleTextItem(m_overlapHintBox);
        m_overlapHintLabel->setBrush(QColor(255, 255, 255));
        m_overlapHintLabel->setFont(overlapHintFont());
    }
    m_overlapHitText = text;
    m_overlapHintLabel->setText(text);

    const QFontMetrics fm(overlapHintFont());
    const QRectF tr = fm.boundingRect(text);
    const double w = tr.width() + 16.0, h = tr.height() + 8.0;
    const QPointF sp = cad::geo::Coord::toScene(anchor);
    // 锚点右下 12px, 保持恒定 (避免挡在光标正下方).
    m_overlapHintBox->setRect(sp.x() + 12.0, sp.y() + 12.0, w, h);
    m_overlapHintBox->setPen(QPen(QColor(0, 0, 0, 40)));
    m_overlapHintBox->setBrush(QColor(38, 50, 56, 225));
    m_overlapHintLabel->setPos(sp.x() + 12.0 + (w - tr.width()) / 2.0,
                               sp.y() + 12.0 + (h - tr.height()) / 2.0);
    m_overlapHintBox->show();
}

void ToolSelect::hideOverlapHint()
{
    if (m_overlapHintLabel) m_overlapHintLabel->setText(QString());
    if (m_overlapHintBox) m_overlapHintBox->hide();
    // 清缓存重触发: 每次都重新隐现
    m_overlapHitText.clear();
}

// ---------------------------------------------------------------------------
// Curve anchor dragging
// ---------------------------------------------------------------------------

std::optional<std::pair<QUuid, QUuid>> ToolSelect::hitCurveAnchor(
    const cad::geo::Vec2& worldPos, double zoom) const
{
    if (!m_paramDoc || m_selection.isEmpty()) return std::nullopt;

    const double radius = 10.0 / std::max(zoom, 1e-9);  // 10px screen radius
    const double rSq = radius * radius;

    for (const QUuid& blockId : m_selection) {
        const auto* block = m_paramDoc->findBlock(blockId);
        if (!block) continue;

        for (const auto& seg : block->segments) {
            if (!seg.isCurve()) continue;
            for (const auto& ppId : seg.passPointIds) {
                const auto* pp = block->findPoint(ppId);
                if (!pp || !pp->resolved) continue;
                // CurveAnchor points are dragged with the SmartPen (parametric
                // percent/offset); dragging them here would convert them to Free
                // points and break the chord link. Skip them.
                if (pp->constraint == cad::param::PointConstraint::CurveAnchor) continue;
                cad::geo::Vec2 w = block->transform.toWorld(pp->resolvedPos);
                if (worldPos.distanceSquaredTo(w) < rSq)
                    return std::make_pair(blockId, ppId);
            }
        }
    }
    return std::nullopt;
}

void ToolSelect::beginAnchorDrag(const QUuid& blockId, const QUuid& pointId)
{
    auto* block = m_paramDoc->findBlock(blockId);
    if (!block) return;
    auto* pt = block->findPoint(pointId);
    if (!pt) return;

    m_anchorDragging = true;
    m_anchorBlockId = blockId;
    m_anchorPointId = pointId;
    m_anchorOrigPos = pt->freePos;
}

void ToolSelect::updateAnchorDrag(const cad::geo::Vec2& worldPos)
{
    if (!m_anchorDragging || !m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_anchorBlockId);
    if (!block) return;
    auto* pt = block->findPoint(m_anchorPointId);
    if (!pt) return;

    // Convert world cursor to local coords and set as new freePos
    cad::geo::Vec2 local = block->transform.toLocal(worldPos);
    pt->freePos = local;
    pt->constraint = cad::param::PointConstraint::Free;

    m_paramDoc->invalidateLayer(block->layer);
    m_paramDoc->resolveForDrag({m_anchorBlockId});
    // resolveForDrag() emits resolved() → CanvasScene::syncBlockPositions(), which
    // rebuilds ONLY the blocks whose geometryEpoch changed. The old
    // refreshAllBlockItems() here rebuilt EVERY block item (all curves' C2
    // solve + cache) on every mouse-move — a full-document duplicate of the
    // sync that the signal already performs.
}

void ToolSelect::endAnchorDrag()
{
    if (!m_anchorDragging) return;
    m_anchorDragging = false;

    // Push undo command (old position → new position)
    if (m_undoStack && m_paramDoc) {
        auto* block = m_paramDoc->findBlock(m_anchorBlockId);
        auto* pt = block ? block->findPoint(m_anchorPointId) : nullptr;
        if (pt) {
            m_undoStack->push(new cad::cmd::MovePointCommand(
                m_paramDoc, m_anchorBlockId, m_anchorPointId,
                m_anchorOrigPos, pt->freePos));
        }
    }

    m_anchorBlockId = QUuid();
    m_anchorPointId = QUuid();
}

} // namespace cad::tools