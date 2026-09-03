#include "ToolSelect.h"
#include "ToolManager.h"

#include <QGraphicsSceneMouseEvent>
#include <QGuiApplication>
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
#include "HitTester.h"
#include "canvas/BlockItem.h"
#include "canvas/HudItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "parametric/FollowerAngle.h"
#include "ui/LinePropertyDialog.h"
#include "ConnectGesture.h"
#include "CopyDragController.h"
#include "MarqueeGesture.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/ComponentCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/LayerCommands.h"
#include "ui/DeleteImpactConfirm.h"

namespace cad::tools {

namespace {

/// 长按拖动判定阈值 (2026-09 取消确认基准): press 线身后移动超过该像素数
/// → 进入拖动 (锚点 = press 位置, 与旧"press 即拖"位移语义一致).
///
/// M6 (TOOL_SYSTEM_AUDIT): 原为单一 5.0px。5px 在触控板/高 DPI 屏上约等于
/// 一次手抖, 而"选中即操作"意味着抖一下就把几何整体平移了 (虽然可 undo,
/// 但用户往往事后才发现)。分档处理:
///   · press 未选中的块 —— 这次 press 本来就是"选中"动作, 拖动是次要意图,
///     5px 保持灵敏;
///   · press **已选中**的块 —— 选集已经建立, press 的唯一意图就是拖, 而
///     误拖的代价是静默改几何。放宽到 10px, 让"点一下保持选中"不会被抖成
///     一次移动。
constexpr double kDragThresholdPx = 5.0;         ///< press 未选中块
constexpr double kDragThresholdSelectedPx = 10.0; ///< press 已选中块 (M6)

/// 本次 press 的拖动阈值: 已选中的块用更宽的一档 (见 kDragThresholdPx 注释)。
[[nodiscard]] constexpr double dragThresholdPxFor(bool pressWasSelected)
{
    return pressWasSelected ? kDragThresholdSelectedPx : kDragThresholdPx;
}

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

/// True if segment [p1,p2] intersects (or is contained in) rect r.
/// Liang-Barsky clipping; also counts either endpoint inside the rect.
} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

ToolDescriptor ToolSelect::describe()
{
    ToolDescriptor d;
    d.id = ToolType::Select;
    d.displayName = QString::fromUtf8("选择(&V)");
    d.iconName = QStringLiteral("cursor-click");
    d.shortcut = QKeySequence(Qt::Key_V);
    // 静态默认文案与运行期覆盖同源 (modeIndicatorFor 的默认态 = 单选、无
    // 循环上下文), 避免"静态文案改了、状态栏没跟着改"的漂移。
    d.hintText = modeIndicatorFor(SelectionMode::Single, -1, 0)
                     .hint(reinterpret_cast<const char*>(u8"选择"));
    d.factory = [] { return std::make_unique<ToolSelect>(); };
    return d;
}

void ToolSelect::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)scene;
    (void)paramDoc;
    m_state = SelectState::Idle;
    // P2/L5 常驻实例: 每次进入回到单选模式 (旧"销毁重建"即此语义)。
    m_selectionMode = SelectionMode::Single;

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
        [this]() { return m_selection.isEmpty(); },
        // 二期: 连接角度会话 → 上下文属性条 (经 ToolHost 上报, 全 null = 结束)。
        [this](const QUuid& bid, const QUuid& sid, const QUuid& attId, double initial) {
            reportConnectAngleSession(bid, sid, attId, initial);
        },
        [this](bool valid) { reportConnectAngleValidity(valid); });
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
    // 常驻实例下每次进入都要把状态栏刷回单选 —— 上次会话可能停在多选,
    // 而模式在画布上没有任何持久标识, 不刷就是"看着像单选其实不是"。
    refreshModeIndicator();
}

void ToolSelect::onDeactivate()
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

    // M2/N5 (TOOL_SYSTEM_AUDIT 复核 2026-08-29): 重叠提示图元在此销毁,
    // 而不是留给析构函数。P2/L5 之后工具实例常驻, 析构只在程序退出时跑 ——
    // 靠析构"清理"的话, 图元在整个会话里都会挂在场景图和 BSP 索引中
    // (被 hide 但没删)。累积泄漏虽然被"只有一个实例"顺带消除了, 但那是
    // 常驻实例的副产品, 不是这行代码的功劳: 一旦将来改回销毁重建,
    // 放析构的写法会立刻重新漏。放在 onDeactivate 才是真正的清理点
    // (基类清空 m_scene 之前, 此处 m_scene 仍有效)。
    delete m_overlapHint;   // QGraphicsItem 析构自行脱离 scene
    m_overlapHint = nullptr;

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

// ── 连接角度会话输入 (二期: 上下文属性条 → ConnectGesture) ──
// 条带是纯输入面, 连接语义全部留在手势里; 无会话时 (手势被取消/工具已切换)
// 这些调用是 no-op —— ConnectGesture 的 onAngleX 都会先查附件是否存在。

void ToolSelect::connectAngleTextChanged(const QString& text)
{
    if (m_connectGesture) m_connectGesture->onAngleTextChanged(text);
}

void ToolSelect::connectAngleModeChanged(cad::param::RotationMode mode)
{
    if (m_connectGesture) m_connectGesture->onAngleModeChanged(mode);
}

void ToolSelect::connectAngleCommitted()
{
    if (m_connectGesture) m_connectGesture->commitAngle();
}

void ToolSelect::connectAngleCancelled()
{
    if (m_connectGesture) m_connectGesture->cancelAngle();
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
    // 单一出口: toast 讲"刚刚变成什么了" (1.4s 后消失), 状态栏常驻讲
    // "现在是什么、W 会切到哪"。两者都来自下面同一份 ModeIndicator。
    announceModeChange();
}

ModeIndicator ToolSelect::modeIndicator() const
{
    return modeIndicatorFor(m_selectionMode, m_overlapIndex,
                            m_overlapCandidates.size());
}

ModeIndicator ToolSelect::modeIndicatorFor(SelectionMode mode, int overlapIndex,
                                           int overlapCount)
{
    // 循环上下文激活时 W = 循环候选, **不是**切模式 —— 整句都要换掉, 否则
    // 状态栏会给出「W 切多选」这种此刻根本不成立的指引。
    if (overlapIndex >= 0) {
        ModeIndicator mi;
        mi.modeName = QString::fromUtf8("重叠候选");
        mi.detail   = QString::fromUtf8("第 %1/%2 项 | 点击确认 | Esc/空白取消")
                          .arg(overlapIndex + 1).arg(overlapCount);
        mi.wAction  = QString::fromUtf8("W 循环候选");
        // 不设 toast: 循环状态由画布常驻 HUD 表达 (「重叠 N 条 ｜ 第 x/y」),
        // 再弹一句 toast 纯属重复打扰。
        // 角标同理 —— 画布上已经有更详细的常驻 HUD 了, 再挂一个「重叠候选」
        // 胶囊是重复占像素。状态栏仍会显示这一态 (L1 与 HUD 不冲突)。
        mi.isDefault = true;
        return mi;
    }

    // 2026-09 取消确认基准: 单击选中 / 按住拖动 / 端点按住连接 / 悬停提示。
    // 模式无关的固定操作 (双击编辑 / Del / 空白右键) 两个模式都要带, 否则
    // 换模式就等于丢提示。
    ModeIndicator mi;
    if (mode == SelectionMode::Multi) {
        mi.modeName = QString::fromUtf8("多选");
        mi.detail   = QString::fromUtf8("点击加减选 | 框选 | 已选按住拖动 | 双击编辑 | Del删除 | 空白右键→智能笔");
        mi.wAction  = QString::fromUtf8("W 切单选");
        mi.toast    = QString::fromUtf8("多选模式：点击加减选 | 框选 | 已选按住拖动");
    } else {
        mi.modeName = QString::fromUtf8("单选");
        mi.detail   = QString::fromUtf8("点击选中 | 按住拖动 | 端点按住连接 | 双击编辑 | Del删除 | 空白右键→智能笔");
        mi.wAction  = QString::fromUtf8("W 切多选");
        mi.toast    = QString::fromUtf8("单选模式：点击选中 | 按住拖动 | 端点按住连接");
        // 单选是默认态 → 画布角标不显示。四个工具里只有选择工具的模式
        // 错了会直接误操作 (多选点空白=加选 / 单选=清选), 所以它是唯一
        // 拿到 L2 角标的那一个 —— 而角标只在切到多选时才出现。
        mi.isDefault = true;
    }
    return mi;
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
        QMenu* layerMenu = nullptr;
        QList<std::pair<QAction*, QUuid>> layerActions;
        bool layerMoved = false;
        QMenu* overlapMenu = nullptr;

        if (!m_selection.isEmpty()) {
            if (m_selection.size() >= 2)
                actComponent = menu.addAction(QString::fromUtf8("创建组件"));

            layerMenu = menu.addMenu(QString::fromUtf8("移动到图层"));
            const QUuid activeLayerId = m_paramDoc ? m_paramDoc->activeLayer() : QUuid();
            if (m_paramDoc) {
                // 工作图层
                for (const auto& layer : m_paramDoc->layersView().all()) {
                    if (m_paramDoc->layersView().isAuxLayer(layer.id))
                        continue;
                    if (layer.id == activeLayerId)
                        continue;
                    auto* act = layerMenu->addAction(layer.name);
                    const QUuid targetId = layer.id;
                    QObject::connect(act, &QAction::triggered, [this, targetId, &layerMoved]() {
                        if (!layerMoved) {
                            layerMoved = true;
                            deactivateOverlapContext();
                            moveSelectionToLayer(targetId);
                        }
                    });
                    layerActions.append({act, layer.id});
                }

                // 辅助层 (若当前不在辅助层，则允许移入辅助层)
                const QUuid auxId = m_paramDoc->layersView().auxLayerId();
                if (!auxId.isNull() && auxId != activeLayerId) {
                    if (!layerActions.isEmpty())
                        layerMenu->addSeparator();
                    auto* act = layerMenu->addAction(QString::fromUtf8("辅助层"));
                    QObject::connect(act, &QAction::triggered, [this, auxId, &layerMoved]() {
                        if (!layerMoved) {
                            layerMoved = true;
                            deactivateOverlapContext();
                            moveSelectionToLayer(auxId);
                        }
                    });
                    layerActions.append({act, auxId});
                }
            }
            if (layerActions.isEmpty()) {
                layerMenu->setEnabled(false);
            }

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
        // L6 (TOOL_SYSTEM_AUDIT): 末项 cands.size() < 2 与首项 overlapMenu
        // == nullptr 等价 (overlapMenu 仅在候选 ≥2 时创建), 冗余 —— 删。
        if (overlapMenu == nullptr && actCancel == nullptr && actComponent == nullptr && layerMenu == nullptr) {
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
        } else if (chosen && !layerMoved) {
            for (const auto& [act, targetLayerId] : layerActions) {
                if (chosen == act) {
                    deactivateOverlapContext();
                    moveSelectionToLayer(targetLayerId);
                    break;
                }
            }
        }
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    // ── Ctrl+press on a block → quick copy drag (快捷复制) ──
    // 按住 Ctrl 在任意活动层图元上按下拖动即触发快捷复制 (未预选时自动作为复制源,
    // 避免误走普通移动逻辑破坏原版型; 闭包展开保证刚体组件/锁定连接整组复制)。
    if ((event->modifiers() & Qt::ControlModifier) && m_copyDrag) {
        const QUuid blockHit = hitBlock(pos);
        if (!blockHit.isNull()) {
            deactivateOverlapContext();  // 复制拖拽与循环上下文互斥
            if (!m_selection.contains(blockHit)) {
                m_lastHitSegmentId = hitSegmentAt(pos);
                m_selection = {blockHit};
                syncSelectionVisual();
                setState(SelectState::Selecting);
                notifyEditTarget();
            }
            const QSet<QUuid> copySet =
                m_paramDoc->componentsView().closure(
                    m_paramDoc->attachmentsView().lockedClosure(m_selection));
            m_copyDrag->begin(copySet, blockHit, pos);
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
                // M6: "press 前是否已选中"必须在覆写 m_selection 之前取 ——
                // 它决定本次 press 的拖动阈值档位 (已选中的块误拖代价更高)。
                const bool wasSelected = m_selection.contains(blockHit);
                m_lastHitSegmentId = hitSegmentAt(pos);
                m_selection = {blockHit};
                syncSelectionVisual();
                setState(SelectState::Selecting);
                notifyEditTarget();
                beginPressPending(pos, blockHit, wasSelected);
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
        double zoom = m_scene->currentZoom();
        // M6: 已选中的块用更宽的阈值 (抖动 5px 不该把几何拖走)。
        const double thresh =
            dragThresholdPxFor(m_pressWasSelected) / (zoom > 1e-9 ? zoom : 1.0);
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
    double zoom = m_scene->currentZoom();
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
            // Spans via the unified entry (memoized: reuses the frame solve).
            const auto spans = block->spansForSegment(seg, /*skipUnresolvedPassPoints=*/true);
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
    auto* dlg = new cad::ui::LinePropertyDialog(blockId, bestSegId, m_paramDoc,
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
        m_paramDoc->componentsView().closure(m_paramDoc->attachmentsView().lockedClosure(m_selection));
    m_dragBlockIds = dragSet.values();
    m_dragOrigins.clear();
    for (const QUuid& id : m_dragBlockIds) {
        if (const auto* b = m_paramDoc->blocksView().byId(id))
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
        const cad::param::Component* c = m_paramDoc->componentsView().byId(att.fromComponentId);
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

    const cad::param::Attachment* att = m_paramDoc->attachmentsView().byId(m_detachedAttachments.first());
    if (!att || att->isPin || !att->fromComponentId.isNull())
        return false;

    double zoom = m_scene ? m_scene->currentZoom() : 1.0;

    const auto snap = m_snapEngine.findSnap(
        pos, m_paramDoc, zoom, kConnectSnapRadius, {}, &att->fromBlockId);
    if (!snap || snap->blockId == att->fromBlockId)
        return false;

    const auto* leader = m_paramDoc->blocksView().byId(snap->blockId);
    const QUuid segId = leader ? leader->exitSegmentAtPoint(snap->pointId) : QUuid();
    if (segId.isNull()) return false;

    // 影子基准拓扑 (拆开影子基准, DETACH_SHADOW_DESIGN.md §7.4): 被拖拆的
    // 连接以影子为基准 (挂载态跟随线拖离影子) —— 挂回本体 = ⑤ 删影子+活引用;
    // 挂到其他线 = ③ 影子挂载链 (Att1 反算保向 + Att2 重新焊接)。
    if (const auto* curTo = m_paramDoc->blocksView().byId(att->toBlockId);
        curTo && curTo->isShadow) {
        const bool toMaster = (snap->blockId == curTo->shadowMasterBlockId);
        m_undoStack->beginMacro(QStringLiteral("重新挂接"));
        if (toMaster) {
            m_undoStack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
                m_paramDoc, att->id, /*angleOnly=*/false, snap->pointId, segId));
        } else {
            m_undoStack->push(new cad::cmd::ShadowMountCommand(
                m_paramDoc, curTo->id, snap->blockId, snap->pointId, segId));
        }
        m_undoStack->endMacro();
        return true;
    }

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
            const cad::param::Attachment* att = m_paramDoc->attachmentsView().byId(attId);
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
            const cad::param::Attachment* att = m_paramDoc->attachmentsView().byId(it.key());
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
    double zoom = m_scene ? m_scene->currentZoom() : 1.0;
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
        const auto* blk = m_paramDoc->blocksView().byId(c.blockId);
        if (!blk || blk->isBridge) continue;
        if (blk->layer != m_paramDoc->layersView().activeLayer()) continue;

        // 组件成员: 抓端点 = 组件级连接 (整组跟随, 借用端点), 不占用线级
        // follower 名额 —— 放行 (内部关系保持活性). 非组件成员: 线级连接
        // 检查照旧 (森林不变式 线级: 已是 follower 的块不能发起线级连接;
        // **例外**: 仅角度 angleOnly 跟随线位置自由 — 放行, 让它能拖端点
        // 重新建立位置连接, 用户报告: 使用了引用线段但没有连接线段的线,
        // 拖动端点没有任何吸附反应)。
        if (!m_paramDoc->componentsView().ofBlock(c.blockId)) {
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
                const auto* blk = m_paramDoc->blocksView().byId(c.blockId);
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
    if (!m_scene || !m_paramDoc) return QUuid();
    // 统一命中 (P1/M7+L2): 一次扫描, 活动层规则唯一源 (HitTester)。
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const auto hits = blockHitsAtScene(*m_scene, *m_paramDoc, scenePt);
    return hits.empty() ? QUuid() : hits.front().blockId;
}

QUuid ToolSelect::hitSegmentAt(const cad::geo::Vec2& worldPos) const
{
    if (!m_scene || !m_paramDoc) return QUuid();
    const QPointF scenePt = cad::geo::Coord::toScene(worldPos.x, worldPos.y);
    const auto hits = blockHitsAtScene(*m_scene, *m_paramDoc, scenePt);
    return hits.empty() ? QUuid() : hits.front().segmentId;
}

void ToolSelect::notifyEditTarget()
{
    if (!m_host) return;
    QUuid blockId, segId;
    if (m_selection.size() == 1) {
        blockId = *m_selection.begin();
        segId = m_lastHitSegmentId;
        if (segId.isNull() && m_paramDoc) {
            // Fallback: first segment of the block (e.g. external selection).
            if (const auto* blk = m_paramDoc->blocksView().byId(blockId); blk && !blk->segments.empty())
                segId = blk->segments.front().id;
        }
    }
    // 上下文属性条 (CONTEXT_STRIP_DESIGN.md): 单选 = 锁定焦点 (条带可编辑);
    // 多选/取消选择 = 解除锁定 (两个 id 均 null)。
    reportPinnedTarget(blockId, segId);
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

void ToolSelect::moveSelectionToLayer(const QUuid& targetLayerId)
{
    if (!m_paramDoc || m_selection.isEmpty() || targetLayerId.isNull()) return;

    QString targetName;
    for (const auto& l : m_paramDoc->layersView().all()) {
        if (l.id == targetLayerId) {
            targetName = l.name;
            break;
        }
    }
    if (targetName.isEmpty())
        targetName = QStringLiteral("其他图层");

    const QList<QUuid> blockIds = m_selection.values();
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::MoveBlocksToLayerCommand(
            m_paramDoc, blockIds, targetLayerId));
    } else {
        for (const auto& id : blockIds) {
            if (auto* b = m_paramDoc->findBlock(id)) {
                b->layer = targetLayerId;
            }
        }
        emit m_paramDoc->layersChanged();
    }

    showToast(QStringLiteral("已将 %1 条线段移动到「%2」")
                  .arg(blockIds.size())
                  .arg(targetName));
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
    // (彻底自由走面板双拆开: 连接点拆开 + 基准点拆开)。已经是 angleOnly
    // 的连接无需再拆。
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
    QList<OverlapCandidate> hoverCands;
    if (m_scene && m_paramDoc) {
        // 单次扫描 (P1/M7): 光标与重叠提示共用一份命中结果 —— 悬停每帧
        // 曾做 2~3 遍全场景 items()。
        const QPointF scenePt = cad::geo::Coord::toScene(pos.x, pos.y);
        const auto hits = blockHitsAtScene(*m_scene, *m_paramDoc, scenePt);
        const QUuid blockHit = hits.empty() ? QUuid() : hits.front().blockId;
        if (!blockHit.isNull() && (QGuiApplication::keyboardModifiers() & Qt::ControlModifier)) {
            // Ctrl 悬停在线元上 → 提示可快捷复制
            cur = Qt::DragCopyCursor;
        } else if (!blockHit.isNull() && m_selection.contains(blockHit)) {
            // 已选块的端点 (抓取半径内) → 十字 (提示可连接);
            // 否则线身 → 抓手 (提示可拖动).
            double zoom = m_scene->currentZoom();
            const double worldR = kConnectGrabRadius / (zoom > 1e-9 ? zoom : 1.0);
            if (blockHasEndpointNear(blockHit, pos, worldR))
                cur = Qt::CrossCursor;
            else
                cur = Qt::OpenHandCursor;
        }
        for (const auto& h : hits) {
            if (const auto* blk = m_paramDoc->blocksView().byId(h.blockId))
                hoverCands.append(makeOverlapCandidate(*blk, h.segmentId));
        }
        // 上下文属性条: 上报悬停候选 (只读预览)。节流与焦点保护在条带内部;
        // 已锁定的线段不会被悬停抢走 (条带 Pinned 优先于 Hover)。
        if (hits.empty())
            reportHoverTarget(QUuid(), QUuid());
        else
            reportHoverTarget(hits.front().blockId, hits.front().segmentId);
    }
    if (cur != m_hoverCursor) {
        m_hoverCursor = cur;
        if (m_scene && !m_scene->views().isEmpty())
            m_scene->views().first()->viewport()->setCursor(cur);
    }
    // 重叠提示 HUD: 无按钮悬停时跟随 (集群 ≥2 条才显示; 同值短路).
    refreshOverlapHint(pos, &hoverCands);
}

bool ToolSelect::blockHasEndpointNear(const QUuid& blockId,
                                      const cad::geo::Vec2& pos,
                                      double worldRadius) const
{
    const auto* blk = m_paramDoc ? m_paramDoc->blocksView().byId(blockId) : nullptr;
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
    // 统一命中 (P1/M7+L2): 堆叠降序 + 去重 + 活动层过滤都在 HitTester 一处。
    for (const auto& h : blockHitsAtScene(*m_scene, *m_paramDoc, scenePt)) {
        const auto* blk = m_paramDoc->blocksView().byId(h.blockId);
        if (!blk) continue;
        out.append(makeOverlapCandidate(*blk, h.segmentId));
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
    // 进入循环上下文: W 的语义从"切模式"变成"循环候选", 状态栏整句要跟着换。
    refreshModeIndicator();
}

void ToolSelect::deactivateOverlapContext()
{
    m_overlapIndex = -1;
    m_overlapCandidates.clear();
    hideOverlapHint();
    // 状态没变但"此刻按 W 会发生什么"变了 (从循环候选回到切模式)。
    refreshModeIndicator();
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
        const auto* blk = m_paramDoc->blocksView().byId(c.blockId);
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
    // 循环后状态栏的「第 x/y 项」要跟着走 —— 这是持久层相对 HUD 的价值:
    // HUD 锚在集群上可能被遮挡, 状态栏永远在读同一份索引。
    refreshModeIndicator();
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

void ToolSelect::refreshOverlapHint(const cad::geo::Vec2& worldPos,
                                    const QList<OverlapCandidate>* precomputed)
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

    const QList<OverlapCandidate> cands =
        precomputed ? *precomputed : collectOverlapCandidates(worldPos);
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
    if (text == m_overlapHitText && m_overlapHint && m_overlapHint->isVisible())
        return;  // 同值短路: 悬停/拖帧路径不重构 HUD

    if (!m_overlapHint) {
        m_overlapHint = new HudItem();
        m_overlapHint->setLook(HudItem::Look::DarkPill);
        m_overlapHint->setZValue(9990.0);
        m_scene->addItem(m_overlapHint);
    }
    m_overlapHitText = text;
    m_overlapHint->setText(text);
    // 锚点右下 12px 屏幕常量 (HudItem 内部除以 zoom; 原实现写 12 场景单位,
    // 缩放下与光标忽远忽近 —— M1)。
    QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    m_overlapHint->moveToPoint(anchor, view, QPointF(12.0, 12.0));
    m_overlapHint->show();
}

void ToolSelect::hideOverlapHint()
{
    if (m_overlapHint) m_overlapHint->setText(QString());
    if (m_overlapHint) m_overlapHint->hide();
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
        const auto* block = m_paramDoc->blocksView().byId(blockId);
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