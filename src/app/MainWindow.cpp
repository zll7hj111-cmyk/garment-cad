#include "ContextStrip.h"
#include "ToolDockStyle.h"

#include "MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QSettings>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QVBoxLayout>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "canvas/CanvasView.h"
#include "geometry/Units.h"
#include "parametric/ParamDocument.h"
#include "tools/ToolManager.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolRotate.h"
#include "ui/ComponentTab.h"
#include "ui/IconHelper.h"
#include "ui/LayerPanel.h"
#include "ui/LinePropertyDialog.h"
#include "ui/Theme.h"
#include "ui/VariablePanel.h"
#include "ElaTheme.h"
#include "ElaToolButton.h"

using cad::param::ParamDocument;
using cad::tools::ToolManager;
using cad::tools::ToolType;

MainWindow::MainWindow(QWidget* parent)
    : ElaWindow(parent)
{
    m_paramDoc = new ParamDocument(this);
    // P0-1 (ARCHITECTURE_REVIEW): 单栈收口 —— 唯一 undo 栈由 ParamDocument 持有,
    // MainWindow 只做 UI 绑定（菜单动作/脏标记/状态栏都指向文档栈）。此前这里
    // new 了第二个 QUndoStack: CanvasView/ToolSelect/cards 推文档栈而 Ctrl+Z 与
    // isClean 只绑本窗口栈 → 文档栈命令撤不掉、不参与脏标记, clear() 不清栈还会
    // 跨文档污染。
    m_undoStack = m_paramDoc->undoStack();
    m_canvasScene = new CanvasScene(m_paramDoc, this);
    if (cad::ui::Theme::mode() == cad::ui::ThemeMode::Dark)
        m_canvasScene->setStyle(CanvasStyle::darkTheme());
    m_canvasView = new CanvasView(m_canvasScene, this);
    m_toolManager = new ToolManager(m_canvasScene, this);
    m_toolManager->setParamDocument(m_paramDoc);
    m_toolManager->setUndoStack(m_undoStack);
    m_lastWorkingLayer = m_paramDoc->layersView().firstWorkingLayerId();

    // Connect tool manager to view for event dispatch (through the canvas-layer
    // InputDispatcher interface — the canvas never includes tools/ headers).
    m_canvasView->setInputDispatcher(m_toolManager);
    m_canvasView->setParamDoc(m_paramDoc);

    // P1-6: whether the canvas context menu may appear is a TOOL policy, so the
    // app installs the predicate instead of the canvas reaching into tools/.
    // D15 单线确认流: 旋转工具持有目标时右键 = 确认/反悔, 发布菜单让位。
    m_canvasView->setContextMenuGuard([this]() {
        auto* rot = dynamic_cast<cad::tools::ToolRotate*>(m_toolManager->activeTool());
        return rot && rot->hasSessionTarget();
    });

    setupUi();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupPages();
    connectSignals();

    setWindowIcon(cad::ui::IconHelper::appIcon());
    resize(1280, 800);

    const bool isDark = (cad::ui::Theme::mode() == cad::ui::ThemeMode::Dark);
    m_canvasScene->setStyle(isDark ? CanvasStyle::darkTheme()
                                   : CanvasStyle::lightTheme());

    // Mirror the app theme into ElaWidgetTools so Ela widgets (menu bar,
    // dock title bar) follow the 视图 → 暗色主题 toggle.
    ElaTheme::getInstance()->setThemeMode(
        isDark ? ElaThemeType::Dark : ElaThemeType::Light);

    // Load recent files from settings.
    QSettings settings;
    m_recentFiles = settings.value(QStringLiteral("recentFiles")).toStringList();
    // setupMenuBar() 在加载设置之前调用了 updateRecentFilesMenu()（当时
    // m_recentFiles 为空），这里必须补一次，否则冷启动菜单恒为「(无)」。
    updateRecentFilesMenu();

    updateTitle();
    if (m_canvasView)
        m_canvasView->setFocus();
}

MainWindow::~MainWindow()
{
    // 面板窗事件过滤器在子对象析构前摘除, 避免析构期 Hide 事件回调
    // 触碰已部分销毁的成员。
    if (m_panelWindow)
        m_panelWindow->removeEventFilter(this);
    // QObject children are destroyed during ~QObject — AFTER this class's own
    // members are already gone. QUndoStack emits cleanChanged/indexChanged
    // while clearing its commands on destruction; a live connection would
    // then invoke a slot on this half-destroyed window (Debug assert, UB in
    // Release). Drop EVERY child→window connection before that happens.
    // P0-1: m_undoStack now aliases m_paramDoc->undoStack() (non-owning) —
    // the stack itself dies with the document, so disconnect before that.
    // 2026-12: 通扫全部子对象而非点名 undoStack/paramDoc —— ~ToolManager
    // 的 deactivate 链路同样会在子对象析构期 emit hintOverrideChanged,
    // 命中半销毁窗口的 onToolHintOverride (assertObjectType 断言, 见
    // TROUBLESHOOTING 第 3 组); 未来任何子对象析构期发信号都会踩同一个坑。
    if (m_undoStack) disconnect(m_undoStack, nullptr, this, nullptr);
    const auto childObjs = children();
    for (QObject* child : childObjs)
        disconnect(child, nullptr, this, nullptr);
}

void MainWindow::setupUi()
{
    // Central wrapper: floating pill toolbar strip above the canvas. This
    // becomes the FIRST ElaWindow page (画布, stack index 0 — the default).
    m_centerContainer = new QWidget(this);
    auto* v = new QVBoxLayout(m_centerContainer);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_toolDock = new QWidget(m_centerContainer);
    m_toolDock->setObjectName(QStringLiteral("toolDock"));
    v->addWidget(m_toolDock);
    v->addWidget(m_canvasView, 1);

    // 画布页作为自建标签页堆栈的第一个页面（setupPages 中 addWidget）。
    // 不再经 ElaWindow::addCentralWidget 注册（Ela 左侧导航已废弃）。

    // No user-info card in the navigation bar (not a login-type app).
    setUserInfoCardVisible(false);
}

void MainWindow::toggleTheme(bool dark)
{
    const auto mode = dark ? cad::ui::ThemeMode::Dark : cad::ui::ThemeMode::Light;
    if (cad::ui::Theme::mode() != mode) {
        cad::ui::Theme::apply(mode);
    }
    // Ela widgets (Fluent menu bar / dock title bar) follow the same toggle.
    if (ElaTheme::getInstance()->getThemeMode() != (dark ? ElaThemeType::Dark : ElaThemeType::Light)) {
        ElaTheme::getInstance()->setThemeMode(
            dark ? ElaThemeType::Dark : ElaThemeType::Light);
    }
    if (m_actionToggleTheme && m_actionToggleTheme->isChecked() != dark) {
        const QSignalBlocker blocker(m_actionToggleTheme);
        m_actionToggleTheme->setChecked(dark);
    }
    // Canvas tokens follow the chrome theme immediately.
    m_canvasScene->setStyle(dark ? CanvasStyle::darkTheme()
                                 : CanvasStyle::lightTheme());
    // Panels bake token colors at construction; re-derive them.
    if (m_variablePanel)
        m_variablePanel->applyTheme();
    if (m_layerPanel)
        m_layerPanel->applyTheme();
    if (m_componentTab)
        m_componentTab->applyTheme();

    const auto& tk = cad::ui::Theme::tokens();
    if (m_toolPill) {
        m_toolPill->setStyleSheet(QStringLiteral(
            "QFrame#toolPill {"
            "  background: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 2px;"
            "}"
        ).arg(tk.surface.name(), tk.borderStrong.name()));
    }
    if (m_editBand) {
        m_editBand->setStyleSheet(QStringLiteral(
            "#stripBand { background: %1; border: 1px solid %2; border-radius: 4px; }")
            .arg(tk.surface.name(), tk.border.name()));
    }
    if (m_contextStrip)
        m_contextStrip->applyTheme();

    refreshToolIcons();
    refreshLayerChip();
    refreshStatusBarChrome();
    refreshPanelChrome();
    syncPanelTabs();  // 激活指示圆点颜色跟随主题

    QSettings().setValue(QStringLiteral("view/darkMode"), dark);
}

void MainWindow::connectSignals()
{
    connect(ElaTheme::getInstance(), &ElaTheme::themeModeChanged, this, [this](ElaThemeType::ThemeMode mode) {
        const bool isDark = (mode == ElaThemeType::Dark);
        if ((cad::ui::Theme::mode() == cad::ui::ThemeMode::Dark) != isDark) {
            toggleTheme(isDark);
        }
    });

    connect(m_canvasView, &CanvasView::mouseScenePosChanged,
            this, &MainWindow::onSceneMouseMoved);
    connect(m_canvasView, &CanvasView::zoomFactorChanged,
            this, &MainWindow::onZoomChanged);
    // P1-6: the canvas reports a segment hit; the menu / dialogs / commands and
    // the tool follow-ups are app-layer policy (see showSegmentContextMenu).
    connect(m_canvasView, &CanvasView::segmentContextMenuRequested,
            this, &MainWindow::onSegmentContextMenu);
    connect(m_canvasScene, &CanvasScene::forceShowChanged,
            this, &MainWindow::onForceShowChanged);
    connect(m_toolManager, &ToolManager::activeToolChanged,
            this, &MainWindow::onToolChanged);
    // M5: 活动工具的运行期状态变化 (智能笔 直线/省道线) 覆盖状态栏提示;
    // 切换工具时由 onToolChanged 清掉并恢复 describe() 的默认文案。
    connect(m_toolManager, &ToolManager::hintOverrideChanged,
            this, &MainWindow::onToolHintOverride);
    connect(m_paramDoc, &ParamDocument::documentChanged,
            this, &MainWindow::onDocumentChanged);
    connect(m_undoStack, &QUndoStack::cleanChanged,
            this, &MainWindow::updateTitle);
    connect(m_paramDoc, &ParamDocument::activeLayerChanged,
            this, &MainWindow::onActiveLayerChanged);
    // Layer add/remove/rename must re-label the toolbar chip too.
    connect(m_paramDoc, &ParamDocument::layersChanged,
            this, &MainWindow::refreshLayerChip);

    // 创建后内嵌编辑条: 线段创建完成 → 显示; 创建中 → 只读实时读数;
    // Esc → 撤销创建 (删线).
    connect(m_canvasScene, &CanvasScene::lineCreated,
            this, &MainWindow::onLineCreated);
    connect(m_canvasScene, &CanvasScene::linePreviewChanged,
            this, &MainWindow::onLinePreview);
    connect(m_contextStrip, &cad::app::ContextStrip::cancelRequested,
            this, &MainWindow::onEditStripCancel);

    // 上下文属性条焦点 (CONTEXT_STRIP_DESIGN.md): 工具经 ToolHost 上报,
    // 条带只负责显示/编辑 —— 悬停 = 只读预览, 锁定 = 可编辑。
    connect(m_toolManager, &ToolManager::pinnedTargetChanged,
            this, &MainWindow::onPinnedTargetChanged);
    connect(m_toolManager, &ToolManager::hoverTargetChanged,
            this, &MainWindow::onHoverTargetChanged);

    // 连接角度会话 (二期): 手势 → 条带 (会话/合法性); 条带输入 → 手势
    // (击键/单位/Enter/Esc)。条带是纯输入面, 连接语义全部在工具侧。
    connect(m_toolManager, &ToolManager::connectAngleSessionChanged,
            this, &MainWindow::onConnectAngleSessionChanged);
    connect(m_toolManager, &ToolManager::connectAngleValidityChanged,
            this, &MainWindow::onConnectAngleValidityChanged);
    connect(m_contextStrip, &cad::app::ContextStrip::connectAngleTextChanged,
            m_toolManager, &ToolManager::forwardConnectAngleText);
    connect(m_contextStrip, &cad::app::ContextStrip::connectAngleModeChanged,
            m_toolManager, &ToolManager::forwardConnectAngleMode);
    connect(m_contextStrip, &cad::app::ContextStrip::connectAngleCommitted,
            m_toolManager, &ToolManager::forwardConnectAngleCommit);
    connect(m_contextStrip, &cad::app::ContextStrip::connectAngleCancelled,
            m_toolManager, &ToolManager::forwardConnectAngleCancel);

    // 旋转会话换向 (2026-12): 条带「换向」在旋转工具激活时 = 切换锚心 ——
    // 条带转发点击给激活工具 (ToolRotate 切锚心 + gizmo pivot 环移动);
    // 工具经 ToolHost 上报锚心状态给条带 (基准读数锚心端在前 + 按钮资格)。
    connect(m_toolManager, &ToolManager::rotateAnchorStateChanged,
            this, &MainWindow::onRotateAnchorStateChanged);
    connect(m_contextStrip, &cad::app::ContextStrip::reverseRequested,
            m_toolManager, &ToolManager::forwardReverseRequest);

    // 放置点状态与会话联动
    connect(m_toolManager, &ToolManager::placedPointTargetChanged,
            this, [this](const QUuid& b, const QUuid& p) {
                if (!m_contextStrip) return;
                if (p.isNull()) m_contextStrip->clearPlacedPoint();
                else m_contextStrip->setPlacedPointTarget(b, p);
                updateEditBand();
            });
    connect(m_toolManager, &ToolManager::placePointSessionChanged,
            this, [this](bool active, const QString& seg) {
                if (!m_contextStrip) return;
                if (active) m_contextStrip->beginPlacePointSession(seg);
                else m_contextStrip->endPlacePointSession();
                updateEditBand();
            });
    connect(m_toolManager, &ToolManager::placePointSessionUpdated,
            this, [this](double dist, double ang, bool dl, bool al) {
                if (m_contextStrip)
                    m_contextStrip->updatePlacePointValues(dist, ang, dl, al);
            });
    connect(m_toolManager, &ToolManager::placePointFocusNextField,
            this, [this]() {
                if (m_contextStrip)
                    m_contextStrip->focusNextPlacedPointField();
            });
    connect(m_contextStrip, &cad::app::ContextStrip::placePointDistChanged,
            m_toolManager, &ToolManager::forwardPlacePointDist);
    connect(m_contextStrip, &cad::app::ContextStrip::placePointAngleChanged,
            m_toolManager, &ToolManager::forwardPlacePointAngle);
    connect(m_contextStrip, &cad::app::ContextStrip::placePointCommitted,
            m_toolManager, &ToolManager::forwardPlacePointCommit);
    connect(m_contextStrip, &cad::app::ContextStrip::placedPointDeleted,
            this, [this](const QUuid&, const QUuid&) {
                if (m_canvasScene) m_canvasScene->refreshAllBlockItems();
                updateEditBand();
            });

    // 撤销/重做瞬时反馈 (§6.5): 「已撤销：创建线段」1.5s 还原。
    // 编辑条可见时跳过 (条带编辑的 SegmentEditBarCommand 高频提交不刷屏);
    // 新命令 push (idx == count) 与 clear (count == 0) 不算重做。
    m_lastUndoIndex = m_undoStack->index();
    connect(m_undoStack, &QUndoStack::indexChanged, this, [this](int idx) {
        const int prev = m_lastUndoIndex;
        m_lastUndoIndex = idx;
        if (idx == prev || m_undoStack->count() == 0)
            return;
        if (m_contextStrip && m_contextStrip->isVisible())
            return;
        const auto& tk = cad::ui::Theme::tokens();
        if (idx < prev) {
            if (idx >= 0)
                flashStatus(QStringLiteral("已撤销：%1").arg(m_undoStack->text(idx)),
                            tk.text2, 1500);
        } else if (idx < m_undoStack->count()) {
            flashStatus(QStringLiteral("已重做：%1").arg(m_undoStack->text(idx - 1)),
                        tk.text2, 1500);
        }
    });
}

void MainWindow::onSceneMouseMoved(qreal x, qreal y)
{
    m_coordLabel->setText(cad::geo::Units::formatPoint(x, y));
}

void MainWindow::onZoomChanged(double factor)
{
    m_zoomLabel->setText(
        QString::fromUtf8("缩放: %1%").arg(factor * 100.0, 0, 'f', 0));
}

void MainWindow::onToolChanged(ToolType type, const char* name)
{
    (void)name;

    // P3 (TOOL_SYSTEM_AUDIT): action 与按钮索引都经 registry 注册序对齐,
    // 无平行 switch。空白右键切工具 (智能笔 ↔ 选择) 不经 QAction trigger,
    // 此处手动 setChecked 让互斥 group 高亮跟随 (重设同 action 是 no-op)。
    if (auto* act = m_toolActions.value(type))
        act->setChecked(true);

    // Fluent tool buttons: the pill buttons are QAction-driven but their
    // highlight is drawn from setIsSelected, so mirror the active tool here.
    const int toolIndex = m_toolOrder.indexOf(type);
    for (int i = 0; i < m_toolButtons.size(); ++i)
        m_toolButtons.at(i)->setIsSelected(i == toolIndex);

    // H3 (TOOL_SYSTEM_AUDIT): 提示文本 = 各工具 ToolDescriptor::describe()
    // 的 hintText (经 cad::tools::toolHintText 查 registry), 8 个 ToolType
    // 一一对应。原 if/else 链没有 AngleMeasure 分支, else 兜底让角度测量
    // 一直显示「选择」的操作说明。
    // M5: 重设即覆盖掉上一个工具留下的运行期提示 (智能笔模式文案), 不会
    // 串台 —— 覆盖的唯一落点就是 m_toolHintFull。
    setToolHint(cad::tools::toolHintText(type));

    // 切换工具 = 焦点清空: 条带的锁定/悬停都不跨工具继承。
    if (m_contextStrip)
        m_contextStrip->hideBar();
    // 2026-12 修复: editBand 初始 hide, 父链隐藏时 setVisible/hide 都不触发
    // Show/Hide 事件 → updateEditBand 永远不跑, 静默消失。这里在每次工具
    // 切换后主动同步一次 (updateEditBand 已改用 !isHidden() 判显隐意图),
    // 不依赖事件驱动。
    updateEditBand();
    if (m_toolHintLabel)
        m_toolHintLabel->setVisible(true);
}

void MainWindow::onLineCreated(const QUuid& blockId, const QUuid& segmentId)
{
    // 智能笔创建完成 → 锁定到新线段 (创建后编辑; Esc = 撤销创建)。
    if (!m_contextStrip) return;
    m_contextStrip->pinCreatedLine(blockId, segmentId);
    updateEditBand();  // 显隐收口: 父链隐藏时 show() 不触发 Show 事件
}

void MainWindow::onLinePreview(double lenCm, double angleDeg)
{
    if (!m_contextStrip) return;
    // 画线中显示"正在画的那条线"的实时读数 (不是悬停到的线)。
    m_contextStrip->showStrokePreview(lenCm, angleDeg);
    updateEditBand();  // 显隐收口: 父链隐藏时 show() 不触发 Show 事件
}

void MainWindow::onEditStripCancel()
{
    // 创建模式 Esc = 删除: rewind the undo stack to the creation point — this
    // drops the creation command AND any strip edits pushed since (strip
    // edits commit through SegmentEditBarCommand, so one rewind removes them
    // all).
    if (m_contextStrip)
        m_contextStrip->cancelCreation();
    updateEditBand();  // 显隐收口 (取消创建会隐藏条带)
}

void MainWindow::onPinnedTargetChanged(const QUuid& blockId, const QUuid& segmentId)
{
    if (!m_contextStrip) return;
    if (segmentId.isNull()) {
        // 取消选择 / 多选 / 点空白 —— 解除锁定。
        m_contextStrip->clearPinned();
        updateEditBand();
        return;
    }
    // 工具点击锁定: 不抢键盘焦点 (画布保留 W 切换 / Del / 框选等交互)。
    m_contextStrip->setPinnedTarget(blockId, segmentId, /*grabFocus=*/false);
    updateEditBand();
}

void MainWindow::onHoverTargetChanged(const QUuid& blockId, const QUuid& segmentId)
{
    if (!m_contextStrip) return;
    // 节流与焦点保护在条带内部 (CONTEXT_STRIP_DESIGN.md §4.2 / 实现铁律)。
    if (segmentId.isNull())
        m_contextStrip->clearHover();
    else
        m_contextStrip->setHoverTarget(blockId, segmentId);
    updateEditBand();
}

void MainWindow::onConnectAngleSessionChanged(const QUuid& blockId, const QUuid& segmentId,
                                              const QUuid& attachmentId, double initialAngle)
{
    // 二期: 连接手势进入/退出角度会话 —— 条带显示跟随线段并进入角度编辑。
    if (!m_contextStrip) return;
    if (attachmentId.isNull())
        m_contextStrip->endConnectAngleSession();
    else
        m_contextStrip->beginConnectAngleSession(blockId, segmentId,
                                                 attachmentId, initialAngle);
    updateEditBand();
}

void MainWindow::onConnectAngleValidityChanged(bool valid)
{
    if (m_contextStrip)
        m_contextStrip->setConnectAngleValid(valid);
}

void MainWindow::onRotateAnchorStateChanged(bool active, bool anchorIsEnd,
                                            bool canToggle, const QString& reason)
{
    // 旋转会话 (2026-12): 锚心端在前 + 换向按钮转义为切锚心。active=false =
    // 会话结束, 条带恢复普通换向语义。
    if (m_contextStrip)
        m_contextStrip->setRotateAnchorState(active, anchorIsEnd, canToggle, reason);
}

void MainWindow::onForceShowChanged(bool showNames, bool showLengths)
{
    // Keep every open LinePropertyDialog's display toggles in sync while the
    // N/M hold-to-show keys are pressed (parent chain: dialogs live under the
    // canvas view). No-op when none are open.
    const auto dialogs = m_canvasView->findChildren<cad::ui::LinePropertyDialog*>();
    for (auto* dlg : dialogs)
        dlg->applyHoldOverride(showNames, showLengths);
}

void MainWindow::actionToggleAuxLayer()
{
    if (!m_paramDoc) return;
    const QUuid cur = m_paramDoc->layersView().activeLayer();
    if (m_paramDoc->layersView().isAuxLayer(cur)) {
        // 已在辅助层 → 切回最近一次的工作层。首次启动/打开文件时记忆可能为
        // 空或已失效，回退到第一个工作层，避免 H 被“静默无效”。
        QUuid target = m_lastWorkingLayer;
        if (target.isNull() || !m_paramDoc->layersView().byId(target) ||
            m_paramDoc->layersView().isAuxLayer(target))
            target = m_paramDoc->layersView().firstWorkingLayerId();
        m_paramDoc->setActiveLayer(target);
    } else {
        // 记住当前工作层，再切到辅助层。
        m_lastWorkingLayer = cur;
        m_paramDoc->setActiveLayer(m_paramDoc->layersView().auxLayerId());
    }
}

void MainWindow::onActiveLayerChanged(const QUuid& layerId)
{
    // 任何路径切到工作层都更新记忆，H 总能回到最近的工作层。
    if (m_paramDoc && !m_paramDoc->layersView().isAuxLayer(layerId))
        m_lastWorkingLayer = layerId;
    if (m_actionToggleAuxLayer)
        m_actionToggleAuxLayer->setChecked(
            m_paramDoc && m_paramDoc->layersView().isAuxLayer(layerId));
    refreshLayerChip();
}

void MainWindow::updateEditBand()
{
    if (!m_editBand || !m_contextStrip)
        return;
    // 2026-12 修复: 判**显隐意图** (!isHidden) 而非实际可见 (isVisible) ——
    // isVisible 受父链影响: editBand 自身隐藏时子条 isVisible 恒 false,
    // 死锁 (条带静默消失)。初始 editBand->hide() 时条带隐藏 = 意图隐藏。
    m_editBand->setVisible(!m_contextStrip->isHidden());
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_panelWindow && event->type() == QEvent::Hide && !m_tabSyncGuard)
        syncPanelTabs();  // X 关闭 / hide() → 主标签回画布、清激活指示
    if (obj == m_contextStrip
        && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
        updateEditBand();  // 编辑条带随上下文属性条的显隐整体显隐
    if (obj == m_toolHintLabel && event->type() == QEvent::Resize)
        applyToolHintElide();  // M9: 宽度变化重算省略文本 (同值守卫防递归)
    return QObject::eventFilter(obj, event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // 悬浮位置记忆 (§4.1: 关闭后记忆位置)。
    QSettings settings;
    settings.setValue(QStringLiteral("panel/geo"), m_panelWindow->saveGeometry());

    if (maybeSave())
        event->accept();
    else
        event->ignore();
}
