#include "ContextStrip.h"
#include "ToolDockStyle.h"

#include "MainWindow.h"

#include "ElaText.h"
#include <QToolButton>
#include <QFrame>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QHBoxLayout>
#include <QAction>
#include <QActionGroup>
#include <QUndoStack>
#include <QFileDialog>
#include <QSettings>
#include <QCloseEvent>
#include <QFileInfo>
#include <QApplication>
#include <QTimer>
#include <QShortcut>
#include <QStackedWidget>
#include <QSplitter>
#include <QSizePolicy>
#include <QScreen>
#include <QFontMetrics>
#include <QHash>

#include "tools/ToolRegistry.h"
#include "canvas/CanvasView.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "geometry/Units.h"
#include "tools/ToolManager.h"
#include "tools/ToolSelect.h"
#include "tools/ToolRotate.h"
#include "tools/ToolSmartPen.h"
#include "ui/QuickAuxDialog.h"
#include "ui/LinePropertyDialog.h"
#include "parametric/LinkedVariable.h"
#include "parametric/ParamPoint.h"
#include "document/commands/VariableCommands.h"
#include "document/commands/DocumentCommands.h"
#include "ui/VariablePanel.h"
#include "ui/ComponentTab.h"
#include "ui/LayerPanel.h"
#include "ui/IconHelper.h"
#include "ui/Theme.h"
#include "ui/ElaMsgBox.h"
#include "document/DocumentFile.h"
#include "ElaWindow.h"
#include "ElaStatusBar.h"
#include "ElaMenuBar.h"
#include "ElaMenu.h"
#include "ElaTheme.h"
#include "ElaToolButton.h"
#include "ElaScrollPageArea.h"
#include "ElaTabBar.h"
#include "ElaNavigationBar.h"

using cad::param::ParamDocument;
using cad::tools::ToolManager;
using cad::tools::ToolType;

namespace {

/// Show deserialization degradation warnings after a SUCCESSFUL load
/// (unknown constraint / segment type / layer type were replaced by safe
/// defaults — the document is valid, just downgraded).
void showLoadWarnings(QWidget* parent, const QStringList& warnings)
{
    if (warnings.isEmpty()) return;
    constexpr int kMaxShown = 5;
    QStringList shown = warnings.mid(0, kMaxShown);
    if (warnings.size() > kMaxShown)
        shown << QString::fromUtf8("… 另有 %1 项降级").arg(warnings.size() - kMaxShown);
    cad::ui::ElaMsgBox::warning(parent, QString::fromUtf8("文件加载完成"),
        QString::fromUtf8("文件已加载，但有 %1 处内容无法识别，已按默认值恢复：\n\n%2")
            .arg(warnings.size()).arg(shown.join(QStringLiteral("\n"))));
}

/// 工具坞按钮的 Ela 字体图标 (app 层视觉偏好, 非工具元数据 —— 所以不放进
/// ToolDescriptor, 仍留在 MainWindow)。
///
/// N1 (TOOL_SYSTEM_AUDIT 复核 2026-08-29): 早先这里是
/// `std::array<IconName, 8>` 配 `arr[static_cast<int>(type)]` 的枚举序下标
/// —— 新增第 9 个工具就是**越界读 UB, 且编译不报错**; 它还让 AGENTS.md
/// 承诺的"新增工具不再改 MainWindow"变成假的 (实际必须回来改这个数组)。
/// 改键控表: 查不到 = 兜底图标 + 一次警告, 永远不会读越界。
[[nodiscard]] ElaIconType::IconName toolDockIcon(ToolType type)
{
    static const QHash<ToolType, ElaIconType::IconName> kIcons = {
        { ToolType::Select,       ElaIconType::ArrowPointer },
        { ToolType::SmartPen,     ElaIconType::PenNib },
        { ToolType::CurveEdit,    ElaIconType::BezierCurve },
        { ToolType::Rotate,       ElaIconType::Rotate },
        { ToolType::Break,        ElaIconType::Scissors },
        { ToolType::Intersection, ElaIconType::Intersection },
        { ToolType::Measure,      ElaIconType::RulerCombined },
        { ToolType::AngleMeasure, ElaIconType::Angle },
    };
    if (auto it = kIcons.constFind(type); it != kIcons.constEnd())
        return *it;
    // 未登记的新工具: 兜底成箭头图标 (而不是读越界), 并显式报警 —— 加工具
    // 的人会立刻看到, 不会被静默的乱码图标或崩溃骗过去。
    Q_ASSERT_X(false, "toolDockIcon",
               "tool dock icon missing for a registered ToolType");
    qWarning() << "MainWindow: no dock icon registered for ToolType"
               << static_cast<int>(type) << "- falling back to ArrowPointer.";
    return ElaIconType::ArrowPointer;
}

/// 面板打开时主窗口对应标签按钮上的激活指示: 主题强调色小圆点
/// (ElaTabBarStyle 不支持 per-tab 文字色, 用图标最稳)。
QIcon panelActiveDot(const QColor& color)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(2.5, 2.5, 9, 9));
    return QIcon(pm);
}

} // namespace

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

    // 默认白色（亮色）主题；若用户在 main() 之后已切换为暗色则镜像到画布。
    if (cad::ui::Theme::mode() == cad::ui::ThemeMode::Dark)
        m_canvasScene->setStyle(CanvasStyle::darkTheme());

    // Mirror the app theme into ElaWidgetTools so Ela widgets (menu bar,
    // dock title bar) follow the 视图 → 暗色主题 toggle.
    ElaTheme::getInstance()->setThemeMode(
        cad::ui::Theme::mode() == cad::ui::ThemeMode::Dark
            ? ElaThemeType::Dark : ElaThemeType::Light);

    // Load recent files from settings.
    QSettings settings;
    m_recentFiles = settings.value(QStringLiteral("recentFiles")).toStringList();

    updateTitle();
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

void MainWindow::setupMenuBar()
{
    // Fluent-styled menu bar (ElaWidgetTools): sits inside the ElaAppBar
    // (MiddleArea) — no classic menu strip above the central widget.
    auto* elaMenuBar = new ElaMenuBar(this);
    elaMenuBar->setFixedHeight(30);
    auto* menuHost = new QWidget(this);
    auto* menuLay = new QVBoxLayout(menuHost);
    menuLay->setContentsMargins(0, 0, 0, 0);
    menuLay->setSpacing(0);
    menuLay->addWidget(elaMenuBar);
    menuLay->addStretch();
    setCustomWidget(ElaAppBarType::MiddleArea, menuHost);

    // ===== 文件菜单 =====
    ElaMenu* fileMenu = elaMenuBar->addMenu(QString::fromUtf8("文件(&F)"));

    QAction* actNew = fileMenu->addAction(QString::fromUtf8("新建(&N)"));
    actNew->setShortcut(QKeySequence::New);
    connect(actNew, &QAction::triggered, this, &MainWindow::onNewDocument);

    QAction* actOpen = fileMenu->addAction(QString::fromUtf8("打开(&O)..."));
    actOpen->setShortcut(QKeySequence::Open);
    connect(actOpen, &QAction::triggered, this, &MainWindow::onOpenDocument);

    // Recent files submenu
    m_recentFilesMenu = fileMenu->addMenu(QString::fromUtf8("最近文件"));
    updateRecentFilesMenu();

    fileMenu->addSeparator();

    QAction* actSave = fileMenu->addAction(QString::fromUtf8("保存(&S)"));
    actSave->setShortcut(QKeySequence::Save);
    connect(actSave, &QAction::triggered, this, &MainWindow::onSaveDocument);

    QAction* actSaveAs = fileMenu->addAction(QString::fromUtf8("另存为(&A)..."));
    actSaveAs->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(actSaveAs, &QAction::triggered, this, &MainWindow::onSaveAsDocument);

    fileMenu->addSeparator();

    QAction* actExit = fileMenu->addAction(QString::fromUtf8("退出(&X)"));
    actExit->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F4));
    connect(actExit, &QAction::triggered, this, &QWidget::close);

    // ===== 编辑菜单 =====
    QMenu* editMenu = elaMenuBar->addMenu(QString::fromUtf8("编辑(&E)"));
    m_actionUndo = m_undoStack->createUndoAction(this, QString::fromUtf8("撤销(&U)"));
    m_actionUndo->setShortcut(QKeySequence::Undo);
    editMenu->addAction(m_actionUndo);

    m_actionRedo = m_undoStack->createRedoAction(this, QString::fromUtf8("重做(&R)"));
    m_actionRedo->setShortcut(QKeySequence::Redo);
    editMenu->addAction(m_actionRedo);

    // ===== 工具菜单 =====
    QMenu* toolMenu = elaMenuBar->addMenu(QString::fromUtf8("工具(&T)"));

    // P3 (TOOL_SYSTEM_AUDIT): 工具 action 遍历 ToolRegistry 生成 ——
    // 名字/图标/快捷键/提示/工厂由各工具 ToolDescriptor::describe() 携带,
    // 不再手工逐 action 对齐 (旧 8 个 QAction 成员 + 8 个 actionX 槽全删)。
    auto& toolReg = cad::tools::ToolRegistry::instance();
    m_toolOrder = QList<cad::tools::ToolType>(toolReg.order().begin(),
                                              toolReg.order().end());
    auto* group = new QActionGroup(this);
    group->setExclusive(true);
    for (ToolType type : m_toolOrder) {
        const auto* d = toolReg.descriptor(type);
        auto* act = new QAction(
            cad::ui::IconHelper::iconByName(d->iconName, cad::ui::Theme::tokens().text2),
            d->displayName, this);
        act->setCheckable(true);
        act->setChecked(type == ToolType::Select);
        if (!d->shortcut.isEmpty()) {
            act->setShortcut(d->shortcut);
            act->setShortcutContext(Qt::ApplicationShortcut);
        }
        // M3 (TOOL_SYSTEM_AUDIT): 悬停 tooltip = 操作说明全文。
        act->setToolTip(d->hintText);
        group->addAction(act);
        toolMenu->addAction(act);
        m_toolActions.insert(type, act);
        connect(act, &QAction::triggered, this, [this, type]() {
            m_toolManager->switchTool(type);
        });
    }

    toolMenu->addSeparator();

    // H: 辅助层 ↔ 最近工作层 快速切换（再按一次切回）。
    m_actionToggleAuxLayer = new QAction(
        QString::fromUtf8("切换辅助层(&H)"), this);
    m_actionToggleAuxLayer->setCheckable(true);
    m_actionToggleAuxLayer->setShortcut(QKeySequence(Qt::Key_H));
    // 面板是独立 Qt::Tool 窗口；默认 WindowShortcut 在面板获得焦点时会被
    // 阻塞。设为应用级，保证 H 在画布/面板/任意本应用子窗口中都能切换图层。
    m_actionToggleAuxLayer->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_actionToggleAuxLayer, &QAction::triggered,
            this, &MainWindow::actionToggleAuxLayer);
    toolMenu->addAction(m_actionToggleAuxLayer);

    // ===== 视图菜单 =====
    QMenu* viewMenu = elaMenuBar->addMenu(QString::fromUtf8("视图(&V)"));

    // 暗色主题: 全局 QSS + 画布 token 即时切换, 无需重启.
    m_actionToggleTheme = new QAction(QString::fromUtf8("暗色主题(&D)"), this);
    m_actionToggleTheme->setCheckable(true);
    m_actionToggleTheme->setChecked(
        cad::ui::Theme::mode() == cad::ui::ThemeMode::Dark);
    m_actionToggleTheme->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(m_actionToggleTheme, &QAction::toggled,
            this, &MainWindow::toggleTheme);
    viewMenu->addAction(m_actionToggleTheme);

    // ===== 帮助菜单 =====
    QMenu* helpMenu = elaMenuBar->addMenu(QString::fromUtf8("帮助(&H)"));
    QAction* aboutAction = helpMenu->addAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("t-shirt"), cad::ui::Theme::tokens().text1),
        QString::fromUtf8("关于(&A)"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        cad::ui::ElaMsgBox::about(this, QString::fromUtf8("关于 野风帖"),
            QString::fromUtf8("野风帖 - 参数化服装 CAD 系统 v0.1.0\n基于 C++23 / Qt6 构建"));
    });

}

void MainWindow::setupToolBar()
{
    // 悬浮胶囊工具栏: 圆角胶囊 + 居中. 按钮已换 Fluent ElaToolButton:
    // ElaIconType 字体图标 (eTheme 自动换色) + setIsSelected 高亮选中态,
    // 不再依赖 Phosphor SVG 双状态图标. 两侧对称 addStretch 才是真居中
    // (之前只有右侧 stretch, 胶囊实际贴左).
    auto* dockLay = new QHBoxLayout(m_toolDock);
    dockLay->setContentsMargins(10, 5, 10, 5);
    dockLay->setSpacing(0);

    auto* pill = new ElaScrollPageArea(m_toolDock);
    pill->setObjectName(QStringLiteral("toolPill"));
    pill->setFixedHeight(46);
    pill->setStyleSheet(QStringLiteral(
        "QFrame#toolPill {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 2px;"
        "}"
    ).arg(cad::ui::Theme::tokens().surface.name(), cad::ui::Theme::tokens().borderStrong.name()));
    auto* pillLay = new QHBoxLayout(pill);
    pillLay->setContentsMargins(4, 4, 4, 4);
    pillLay->setSpacing(2);

    // P3 (TOOL_SYSTEM_AUDIT): 按钮遍历 registry 注册序生成, 与菜单同序。
    // N1: 图标改键控查表 (见 toolDockIcon), 不再按枚举序下标 —— 新增工具
    // 漏配图标 = 兜底 + 警告, 不会越界读。
    for (ToolType type : m_toolOrder) {
        auto* btn = new ElaToolButton(pill);
        btn->setDefaultAction(m_toolActions.value(type));
        btn->setElaIcon(toolDockIcon(type));
        btn->setIsTransparent(true);
        btn->setCursor(Qt::PointingHandCursor);
        // §6.1 状态矩阵: 激活 = 实心 accent 黄底 + 墨色图标 (全屏唯一实心黄),
        // 悬停 surface2 / 按压 accentStrong —— 替换 Ela 默认灰蓝选中态。
        btn->setStyle(new cad::app::ToolDockStyle(btn));
        m_toolButtons.append(btn);
        pillLay->addWidget(btn);
    }

    // ── 图层指示芯片: 色点 + 当前活动图层名, 点击弹出切换菜单 ──
    // 画布上不标图层会让用户画错层, 这里把"当前往哪个层画"常驻工具栏.
    m_layerChipSeparator = new QFrame(pill);
    m_layerChipSeparator->setFrameShape(QFrame::VLine);
    m_layerChipSeparator->setFixedWidth(1);
    pillLay->addSpacing(6);
    pillLay->addWidget(m_layerChipSeparator);
    pillLay->addSpacing(2);

    m_layerChip = new ElaToolButton(pill);
    m_layerChip->setIsTransparent(true);
    m_layerChip->setCursor(Qt::PointingHandCursor);
    m_layerChip->setPopupMode(QToolButton::InstantPopup);
    m_layerChip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    pillLay->addWidget(m_layerChip);

    dockLay->addStretch(1);
    dockLay->addWidget(pill, 0, Qt::AlignHCenter);
    dockLay->addStretch(1);

    refreshLayerChip();
}

void MainWindow::refreshLayerChip()
{
    if (!m_layerChip || !m_paramDoc) return;

    const auto& tk = cad::ui::Theme::tokens();
    // Separator follows the current theme (re-polish is cheap at this rate).
    if (m_layerChipSeparator)
        m_layerChipSeparator->setStyleSheet(
            QStringLiteral("background: %1; border: none;").arg(tk.border.name()));

    // Dot: working layer = accent (activity), auxiliary = success (aux
    // geometry family — semantic colors only carry meaning).
    auto dotIcon = [](const QColor& c) {
        QPixmap pm(12, 12);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QRectF(1.5, 1.5, 9.0, 9.0));
        return QIcon(pm);
    };

    const QUuid cur = m_paramDoc->layersView().activeLayer();
    const auto* layer = m_paramDoc->layersView().byId(cur);
    if (!layer) return;

    const bool aux = layer->type == cad::param::LayerType::Auxiliary;
    const QColor dot = aux ? tk.success : tk.text1;
    m_layerChip->setIcon(dotIcon(dot));
    m_layerChip->setText(aux ? QStringLiteral("辅助：%1").arg(layer->name)
                             : QStringLiteral("图层：%1").arg(layer->name));
    m_layerChip->setToolTip(QStringLiteral("当前活动图层：%1（点击切换）").arg(layer->name));

    // Rebuild the popup menu: all layers, current one checked, click = switch.
    if (m_layerChipMenu) {
        m_layerChip->setMenu(nullptr);
        delete m_layerChipMenu;
        m_layerChipMenu = nullptr;
    }
    auto* menu = new ElaMenu(m_layerChip);
    for (const auto& l : m_paramDoc->layers()) {
        const bool lAux = l.type == cad::param::LayerType::Auxiliary;
        QString label = l.name;
        if (lAux) label += QStringLiteral("（辅助计算层）");
        auto* act = menu->addAction(dotIcon(lAux ? tk.success : tk.text1), label);
        act->setCheckable(true);
        act->setChecked(l.id == cur);
        connect(act, &QAction::triggered, this,
                [this, id = l.id]() { m_paramDoc->setActiveLayer(id); });
    }
    m_layerChipMenu = menu;
    m_layerChip->setMenu(menu);
}

void MainWindow::refreshToolIcons()
{
    // Rebuild the tool icons from the current theme so both themes stay
    // legible (normal = secondary text, active/checked = 墨黑). P3: 图标名
    // 由各工具 ToolDescriptor::describe() 携带, 遍历 registry 统一重建。
    const auto& t = cad::ui::Theme::tokens();
    const QColor normal = t.text2;
    const QColor active = t.text1;
    auto& reg = cad::tools::ToolRegistry::instance();
    for (ToolType type : m_toolOrder) {
        if (const auto* d = reg.descriptor(type)) {
            if (auto* act = m_toolActions.value(type)) {
                act->setIcon(cad::ui::IconHelper::icon2State(
                    d->iconName, normal, active));
            }
        }
    }
}

void MainWindow::toggleTheme(bool dark)
{
    const auto mode = dark ? cad::ui::ThemeMode::Dark : cad::ui::ThemeMode::Light;
    cad::ui::Theme::apply(mode);
    // Ela widgets (Fluent menu bar / dock title bar) follow the same toggle.
    ElaTheme::getInstance()->setThemeMode(
        dark ? ElaThemeType::Dark : ElaThemeType::Light);
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
    refreshToolIcons();
    refreshLayerChip();
    refreshStatusBarChrome();
    refreshPanelChrome();
    syncPanelTabs();  // 激活指示圆点颜色跟随主题
}

void MainWindow::setupStatusBar()
{
    // ElaStatusBar: Fluent-styled status bar (QStatusBar drop-in).
    auto* sb = new ElaStatusBar(this);
    setStatusBar(sb);

    m_toolHintLabel = new ElaText(QString(), 13, this);
    // M9 (TOOL_SYSTEM_AUDIT): 提示最长 66 个汉字, QLabel/ElaText 无原生
    // elide, 超宽即被状态栏硬裁成半句。横向 Ignored 解除"整句宽度撑布局",
    // 文本经 setToolHint/applyToolHintElide 输出 (省略号 + tooltip 全文兜底),
    // 宽度变化由 eventFilter 的 Resize 分支驱动重算。
    m_toolHintLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_toolHintLabel->installEventFilter(this);
    sb->addWidget(m_toolHintLabel, 1);
    setToolHint(cad::tools::toolHintText(cad::tools::ToolType::Select));

    // 瞬时反馈 (§6.5): 保存成功对勾 / 撤销重做文字, 数秒后自动消失。
    m_flashLabel = new ElaText(QString(), 12, this);
    m_flashLabel->setObjectName(QStringLiteral("coordLabel"));
    m_flashLabel->setStyleSheet(cad::ui::ThemeTokens::kMonospaceFamily);
    m_flashLabel->hide();

    // 诊断 badge (§6.5): danger 实心 ⚠ N, 点击弹出明细; 健康时完全不占位
    // (替代旧的内嵌 danger 长文本 —— 诊断不再与工具提示混排)。
    m_diagBadge = new QToolButton(this);
    m_diagBadge->setText(QStringLiteral("⚠"));
    m_diagBadge->setCursor(Qt::PointingHandCursor);
    m_diagBadge->setToolTip(QString::fromUtf8("存在连接问题，点击查看明细"));
    m_diagBadge->hide();
    connect(m_diagBadge, &QToolButton::clicked, this, [this]() {
        using cad::param::ResolveDiagnostic;
        const auto& diags = m_paramDoc->diagnostics();
        if (diags.empty()) return;
        QMenu menu(this);
        for (const auto& d : diags) {
            QString text;
            switch (d.kind) {
            case ResolveDiagnostic::Kind::DanglingBlock:
                text = QString::fromUtf8("连接引用了不存在的线段"); break;
            case ResolveDiagnostic::Kind::DanglingPoint:
                text = QString::fromUtf8("连接引用了不存在的端点"); break;
            case ResolveDiagnostic::Kind::NotConverged:
                text = QString::fromUtf8("连接存在冲突或循环，无法稳定求解"); break;
            }
            auto* act = menu.addAction(text);
            act->setDisabled(true);
        }
        menu.exec(m_diagBadge->mapToGlobal(
            QPoint(0, m_diagBadge->height())));
    });

    m_coordLabel = new ElaText("X: 0.000  Y: 0.000", 12, this);
    m_coordLabel->setObjectName(QStringLiteral("coordLabel"));
    m_coordLabel->setMinimumWidth(220);
    // CAD readouts: monospace digits so drag values never jitter.
    m_coordLabel->setStyleSheet(cad::ui::ThemeTokens::kMonospaceFamily);

    m_zoomLabel = new ElaText(QString::fromUtf8("缩放: 100%"), 12, this);
    m_zoomLabel->setObjectName(QStringLiteral("zoomLabel"));
    m_zoomLabel->setMinimumWidth(100);
    m_zoomLabel->setStyleSheet(cad::ui::ThemeTokens::kMonospaceFamily);

    m_flashTimer = new QTimer(this);
    m_flashTimer->setSingleShot(true);
    connect(m_flashTimer, &QTimer::timeout, this,
            [this]() { m_flashLabel->hide(); });

    sb->addPermanentWidget(m_flashLabel);
    sb->addPermanentWidget(m_diagBadge);
    sb->addPermanentWidget(m_coordLabel);
    sb->addPermanentWidget(m_zoomLabel);

    refreshStatusBarChrome();
}

void MainWindow::refreshStatusBarChrome()
{
    if (!m_diagBadge)
        return;
    const auto& tk = cad::ui::Theme::tokens();
    // danger 实心 badge (§6.5): 红底白字, 直角纪律 radius 2px。
    m_diagBadge->setStyleSheet(QStringLiteral(
        "QToolButton { background: %1; color: #FFFFFF; border: none;"
        "  border-radius: 2px; padding: 1px 8px; font-size: 11px; font-weight: 600; }"
        "QToolButton:hover { background: %2; }")
        .arg(tk.danger.name(), tk.danger.darker(110).name()));
}

void MainWindow::connectSignals()
{
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

// Segment right-click menu (P1-6: moved out of CanvasView — the menu, the
// dialog it opens and the tool/UI follow-ups are app-layer policy; the canvas
// only reports which segment was hit and where along it).
//
// Actions: 发布长度参数 / 添加辅助点 / 烘焙到操作层 (measure line on the aux
// layer → COPY onto a working layer; the source line stays the owner).
void MainWindow::onSegmentContextMenu(const cad::canvas::SegmentHit& hit)
{
    const auto* blk = m_paramDoc->findBlock(hit.blockId);
    const auto* seg = blk ? blk->findSegment(hit.segmentId) : nullptr;
    if (!blk || !seg) return;

    ElaMenu menu(this);

    // Check if already published.
    const bool alreadyPublished =
        m_paramDoc->findLinkedBySource(hit.blockId, hit.segmentId) != nullptr;
    QAction* publishAction = menu.addAction(QStringLiteral("发布长度参数"));
    publishAction->setEnabled(!alreadyPublished);
    if (alreadyPublished)
        publishAction->setText(QStringLiteral("已发布长度参数"));

    // --- 添加辅助点 ---
    QAction* auxPointAction = menu.addAction(QStringLiteral("添加辅助点"));

    // --- 烘焙到操作层 (measure line on the aux layer only) ---
    if (m_paramDoc->measurementsView().measureByOwner(hit.blockId)
        && m_paramDoc->layersView().isAuxLayer(blk->layer)) {
        QMenu* bakeMenu = menu.addMenu(QStringLiteral("烘焙到操作层"));
        const auto& layerList = m_paramDoc->layers();
        for (int i = 0; i < static_cast<int>(layerList.size()); ++i) {
            if (m_paramDoc->layersView().isAuxLayer(layerList[static_cast<size_t>(i)].id))
                continue;   // skip the aux layer
            QAction* act = bakeMenu->addAction(layerList[static_cast<size_t>(i)].name);
            act->setProperty("bakeTargetLayer",
                             layerList[static_cast<size_t>(i)].id.toString());
        }
    }

    QAction* chosen = menu.exec(hit.globalPos);
    if (!chosen) return;

    if (chosen == publishAction && !alreadyPublished) {
        // Publish the segment length as a linked variable (shared factory).
        cad::param::LinkedVariable lv =
            cad::param::LinkedVariable::fromSegment(*blk, *seg);
        // P0-3: 文档栈恒非空 —— 统一走命令。
        m_paramDoc->undoStack()->push(
            new cad::cmd::AddLinkedCommand(m_paramDoc, lv));
    } else if (chosen == auxPointAction) {
        const auto* pSp = blk->findPoint(seg->startPointId);
        const auto* pEp = blk->findPoint(seg->endPointId);
        if (!pSp || !pEp) return;

        // Auxiliary point at the clicked position along the segment
        // (same construction as ToolSmartPen::openAuxDialog).
        cad::param::ParamPoint pt;
        pt.constraint = cad::param::PointConstraint::Interpolated;
        pt.hostSegmentId = seg->id;
        pt.isAuxiliary = true;
        pt.visible = true;
        pt.showName = false;
        pt.interpPercent = hit.paramT;
        pt.serial = m_paramDoc->newPointSerial();

        cad::ui::QuickAuxDialog dlg(pt, pSp, pEp, this);
        if (dlg.exec() == QDialog::Accepted) {
            m_paramDoc->undoStack()->push(new cad::cmd::AddAuxPointCommand(
                m_paramDoc, hit.blockId, hit.segmentId, dlg.point()));
        }
    } else if (chosen->property("bakeTargetLayer").isValid()) {
        // Bake a COPY of the measure line onto the chosen working layer, then
        // switch to that layer and select the new line.
        const QUuid targetLayer =
            QUuid::fromString(chosen->property("bakeTargetLayer").toString());
        auto* bakeCmd = new cad::cmd::BakeMeasureCopyCommand(
            m_paramDoc, hit.blockId, targetLayer);
        if (!bakeCmd->isValid()) {
            delete bakeCmd;
            return;
        }
        const QUuid bakedId = bakeCmd->newBlockId();
        m_paramDoc->undoStack()->push(bakeCmd);
        m_paramDoc->setActiveLayer(targetLayer);
        m_toolManager->switchTool(ToolType::Select);
        if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(
                m_toolManager->activeTool()))
            ts->selectBlocksExternally(QList<QUuid>{bakedId});
        m_canvasScene->showToast(
            QStringLiteral("已烘焙到操作层「%1」").arg(chosen->text()));
    }
}

void MainWindow::onDocumentChanged()
{
    const auto& diags = m_paramDoc->diagnostics();
    if (diags.empty()) {
        m_diagBadge->setText(QStringLiteral("⚠"));
        m_diagBadge->setToolTip(QString::fromUtf8("无连接问题"));
        m_diagBadge->hide();
        return;
    }

    using cad::param::ResolveDiagnostic;
    QString first;
    switch (diags.front().kind) {
    case ResolveDiagnostic::Kind::DanglingBlock:
        first = QString::fromUtf8("连接引用了不存在的线段");
        break;
    case ResolveDiagnostic::Kind::DanglingPoint:
        first = QString::fromUtf8("连接引用了不存在的端点");
        break;
    case ResolveDiagnostic::Kind::NotConverged:
        first = QString::fromUtf8("连接存在冲突或循环，无法稳定求解");
        break;
    }

    m_diagBadge->setText(QStringLiteral("⚠ %1").arg(diags.size()));
    m_diagBadge->setToolTip(first);
    m_diagBadge->show();
}

void MainWindow::onToolHintOverride(const QString& hint)
{
    // M5: 工具上报的运行期提示覆盖 (智能笔 直线/省道线)。空串 = 忽略
    // (恢复默认由 onToolChanged 负责, 它在切工具时第一件事就是重设
    // describe() 的文案, 所以覆盖不会跨工具继承)。
    // 不另存"覆盖值"成员: setToolHint 已经把全文留在 m_toolHintFull 里,
    // 多一个只写不读的成员就是本报告 H2 点名的死状态。
    if (!hint.isEmpty())
        setToolHint(hint);
}

void MainWindow::setToolHint(const QString& text)
{
    m_toolHintFull = text;
    if (!m_toolHintLabel) return;
    m_toolHintLabel->setToolTip(text);   // M9 兜底: 悬停看全文
    applyToolHintElide();
}

void MainWindow::applyToolHintElide()
{
    if (!m_toolHintLabel || m_toolHintFull.isEmpty()) return;
    const int avail = m_toolHintLabel->width() - 8;   // 两侧留白
    if (avail <= 0) return;   // 尚未布局: 首个 Resize 事件再算
    const QFontMetrics fm(m_toolHintLabel->fontMetrics());
    const QString elided = fm.elidedText(m_toolHintFull, Qt::ElideRight, avail);
    if (elided == m_toolHintLabel->text()) return;   // 同值短路: 防 Resize 递归
    m_toolHintLabel->setText(elided);
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

void MainWindow::setupPages()
{
    // 画布页含 QOpenGLWidget：Ela 页面切换动画（Popup/Scale）会 grab() 整页
    // 离屏渲染，软渲染环境下 QOpenGLWidget 离屏 grab 段错误 → 点击导航即闪退。
    // 改用 None（无动画直切），规避 grab。
    setStackSwitchMode(ElaWindowType::StackSwitchMode::None);

    // 变量/图层/组件 面板统一进独立悬浮窗 (Qt::Tool): 侧边栏样式的长竖条
    // 窗口, 贴主窗口右缘悬浮 —— 特意不进主窗口布局, 面板永远不占用/裁剪
    // 画布 (2026-08-28 用户拍板: 悬浮窗定位的本意就是不占画面; 停靠右栏
    // 形态已否决移除)。悬浮位置经 QSettings 记忆, 初始隐藏, 点击主窗口
    // 顶部 变量/图层 标签显示并切到对应大标签页。
    m_panelWindow = new QWidget(this, Qt::Tool);
    m_panelWindow->setObjectName(QStringLiteral("panelFloatingWindow"));
    m_panelWindow->setWindowTitle(QString::fromUtf8("面板"));
    m_panelWindow->setMinimumSize(300, 360);
    m_panelWindow->setMaximumWidth(480);  // 保持侧边栏竖条观感, 高度自由.
    m_panelWindow->installEventFilter(this);  // X 关闭/隐藏 → 主标签同步回画布
    auto* winLay = new QVBoxLayout(m_panelWindow);
    winLay->setContentsMargins(8, 8, 8, 8);
    winLay->setSpacing(6);

    // 头行 = 分类大标签 (变量/图层/组件) + 隐藏 ✕。
    m_panelHeader = new QWidget(m_panelWindow);
    auto* headerLay = new QHBoxLayout(m_panelHeader);
    headerLay->setContentsMargins(0, 0, 0, 0);
    headerLay->setSpacing(2);

    m_panelBigBar = new ElaTabBar(m_panelHeader);
    m_panelBigBar->addTab(QStringLiteral("变量"));
    m_panelBigBar->addTab(QStringLiteral("图层"));
    m_panelBigBar->addTab(QStringLiteral("组件"));  // §4.2: 组件升为大标签
    m_panelBigBar->setTabSize(QSize(76, 32));
    m_panelBigBar->setExpanding(true);
    m_panelBigBar->setUsesScrollButtons(false);
    m_panelBigBar->setElideMode(Qt::ElideNone);
    m_panelBigBar->setDrawBase(false);
    m_panelBigBar->setTabsClosable(false);
    m_panelBigBar->setMovable(false);
    m_panelBigBar->setAcceptDrops(false);
    m_panelBigBar->setCursor(Qt::PointingHandCursor);
    m_panelBigBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    headerLay->addWidget(m_panelBigBar, 1);

    const auto& tk = cad::ui::Theme::tokens();
    // 幽灵小按钮 (§5.1 Ghost): 透明底, hover 出 surface2 底 + 描边。
    const QString ghostBtnQss = QStringLiteral(
        "QToolButton { background: transparent; border: 1px solid transparent;"
        "  border-radius: 2px; padding: 1px; }"
        "QToolButton:hover { background: %1; border: 1px solid %2; }")
        .arg(tk.surface2.name(), tk.border.name());
    m_hidePanelBtn = new QToolButton(m_panelHeader);
    m_hidePanelBtn->setCursor(Qt::PointingHandCursor);
    m_hidePanelBtn->setFixedSize(24, 24);
    m_hidePanelBtn->setToolTip(QString::fromUtf8("隐藏面板"));
    m_hidePanelBtn->setStyleSheet(ghostBtnQss);
    m_hidePanelBtn->setIcon(cad::ui::IconHelper::iconByName(
        QStringLiteral("x"), tk.text2));
    connect(m_hidePanelBtn, &QToolButton::clicked, this, [this]() {
        m_panelWindow->hide();  // Hide 事件经 eventFilter 同步主标签
    });
    headerLay->addWidget(m_hidePanelBtn);
    refreshPanelChrome();
    winLay->addWidget(m_panelHeader);

    m_panelStack = new QStackedWidget(m_panelWindow);
    m_variablePanel = new cad::ui::VariablePanel(m_paramDoc, m_panelStack);
    m_variablePanel->setUndoStack(m_undoStack);
    m_panelStack->addWidget(m_variablePanel);

    auto* layerPage = new QWidget(m_panelStack);
    auto* layerLay = new QVBoxLayout(layerPage);
    layerLay->setContentsMargins(12, 12, 12, 12);
    m_layerPanel = new LayerPanel(m_paramDoc, layerPage);
    layerLay->addWidget(m_layerPanel);
    m_panelStack->addWidget(layerPage);

    // 大标签 3: 组件 (从变量面板第 5 子标签升级, §4.2)。
    m_componentTab = new cad::ui::ComponentTab(m_paramDoc, m_panelStack);
    m_componentTab->setUndoStack(m_undoStack);
    m_panelStack->addWidget(m_componentTab);

    winLay->addWidget(m_panelStack, 1);

    // 悬浮窗内切换大标签 → 切换面板内容页; 主窗口对应按钮跟随高亮。
    connect(m_panelBigBar, &QTabBar::currentChanged, this, [this](int category) {
        if (m_panelStack && m_panelStack->currentIndex() != category)
            m_panelStack->setCurrentIndex(category);
        if (!m_tabSyncGuard && panelVisible())
            syncPanelTabs();
    });

    // 网页式标签页导航：顶部标签栏（画布/变量/图层/组）+ 自建页面堆栈，
    // 替代 ElaWindow 左侧竖列导航（隐藏导航栏，页面整体迁入自建堆栈）。
    // 变量/图层 标签是面板悬浮窗开关, 不进页面堆栈（堆栈只剩画布页）。
    m_pageTabs = new ElaTabBar(this);
    m_pageTabs->addTab(QStringLiteral("画布"));
    m_pageTabs->addTab(QStringLiteral("变量"));
    m_pageTabs->addTab(QStringLiteral("图层"));
    m_pageTabs->setTabSize(QSize(88, 30));
    m_pageTabs->setIconSize(QSize(14, 14));  // 面板打开时的激活指示圆点
    m_pageTabs->setTabToolTip(1,
        QString::fromUtf8("面板 · 变量页（点击显示/隐藏）"));
    m_pageTabs->setTabToolTip(2,
        QString::fromUtf8("面板 · 图层页（点击显示/隐藏）"));
    // ElaTabBar 默认 closable+movable+drag-drop：每个标签带 × 关闭按钮，
    // 标签还可拖拽换序 —— 换序后 currentChanged 的下标与 QStackedWidget
    // 页面错位（点图层会打开别的页）。锁死为固定网页式标签条。
    m_pageTabs->setTabsClosable(false);
    m_pageTabs->setMovable(false);
    m_pageTabs->setAcceptDrops(false);

    m_pageStack = new QStackedWidget(this);
    m_pageStack->addWidget(m_centerContainer);
    connect(m_pageTabs, &QTabBar::currentChanged,
            this, &MainWindow::onPageTabChanged);

    // 编辑条带 (§4.6): 画布下方的独立条带 —— accentTint 底 + accentStrong
    // 描边, 与坐标/缩放/诊断信息视觉分离。宿主上下文属性条 (CONTEXT_STRIP):
    // 悬停线段 = 只读预览, 点击锁定 = 可编辑 (选择/智能笔/旋转三工具共用)。
    m_editBand = new QWidget(this);
    m_editBand->setObjectName(QStringLiteral("editBand"));
    m_editBand->setAttribute(Qt::WA_StyledBackground, true);  // QSS 底色/描边生效
    auto* bandLay = new QHBoxLayout(m_editBand);
    bandLay->setContentsMargins(6, 4, 10, 4);
    bandLay->setSpacing(8);

    m_contextStrip = new cad::app::ContextStrip(m_paramDoc, m_editBand);
    m_contextStrip->setUndoStack(m_undoStack);
    m_contextStrip->setCanvasView(m_canvasView);
    m_contextStrip->hide();
    bandLay->addWidget(m_contextStrip, 1);
    m_contextStrip->installEventFilter(this);   // Show/Hide → updateEditBand
    m_editBand->hide();

    // 画布页独占主区 —— 面板只在悬浮窗里, 永不挤占画布。
    auto* pageHost = new QWidget(this);
    auto* pageLay = new QVBoxLayout(pageHost);
    pageLay->setContentsMargins(8, 6, 8, 0);
    pageLay->setSpacing(6);
    pageLay->addWidget(m_pageTabs);
    pageLay->addWidget(m_pageStack, 1);
    pageLay->addWidget(m_editBand);
    setCentralCustomWidget(pageHost);

    // ElaWindow 中央区在同一个 VBox 里同时放了 pageHost 和它自己那个（空的）
    // 导航页容器：两者 stretch 都是 0，多余纵向空间被 50/50 平分 —— 空容器
    // 吞掉窗口下半部，画布被挤到上半部（视觉上就是"导航页侵入画布"）。
    // 本应用从不向 Ela 注册导航页，把空容器从布局里拿掉，pageHost 独占整区。
    if (QWidget* innerCentral = pageHost->parentWidget()) {
        if (auto* box = qobject_cast<QBoxLayout*>(innerCentral->layout())) {
            box->setStretch(0, 1);
            box->setStretch(1, 0);
            if (auto* emptyContainer = innerCentral->findChild<QStackedWidget*>())
                emptyContainer->hide();
        }
    }

    // 左侧导航栏不再使用（标签页接管），隐藏以回收布局。
    if (auto* navBar = findChild<ElaNavigationBar*>())
        navBar->hide();

    for (int i = 0; i < m_pageTabs->count(); ++i) {
        auto* sc = new QShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)), this);
        sc->setContext(Qt::ApplicationShortcut);
        const int idx = i;
        // 只改标签选中: onPageTabChanged 统一负责 页面映射/变量悬浮窗切换。
        connect(sc, &QShortcut::activated, this, [this, idx] {
            if (m_pageTabs)
                m_pageTabs->setCurrentIndex(idx);
        });
    }

    // 恢复悬浮位置记忆 (§4.1: 关闭后记忆位置; 面板恒为悬浮窗形态)。
    {
        QSettings settings;
        settings.remove(QStringLiteral("panel/docked"));  // 清理已否决的停靠形态残留键
        const QByteArray geo =
            settings.value(QStringLiteral("panel/geo")).toByteArray();
        if (!geo.isEmpty()) {
            m_panelWindow->restoreGeometry(geo);
            m_panelWindowPositioned = true;  // 已有记忆位置, 不再自动归位
        }
    }

    // Click a linked card → flash the source block red on canvas.
    connect(m_variablePanel, &cad::ui::VariablePanel::highlightBlockRequested,
            this, [this](const QUuid& blockId) {
        auto* bi = m_canvasScene->findBlockItem(blockId);
        if (!bi) return;
        bi->setToolLocked(true);
        m_canvasScene->refreshAllBlockItems();
        // Auto-clear after 1.5 seconds.
        QTimer::singleShot(1500, this, [this, blockId]() {
            auto* item = m_canvasScene->findBlockItem(blockId);
            if (item) {
                item->setToolLocked(false);
                m_canvasScene->refreshAllBlockItems();
            }
        });
    });

    // Hover/click a measure card → flash the TWO measured points precisely.
    // Bridge-line measurements (ownerBlockId set) and dangling/unresolved
    // sources keep the existing whole-block highlight path.
    connect(m_variablePanel, &cad::ui::VariablePanel::highlightMeasureRequested,
            this, [this](const QUuid& measureId) {
        const auto* mv = m_paramDoc->findMeasure(measureId);
        if (!mv) return;

        QUuid fallbackBlock;
        if (!mv->ownerBlockId.isNull()) {
            // Owned (bridge) measurement: keep the whole-block flash.
            fallbackBlock = mv->ownerBlockId;
        } else if (!m_canvasScene->flashMeasure(mv->blockA, mv->pointA,
                                                 mv->blockB, mv->pointB,
                                                   mv->kind)) {
            // Points missing / unresolved: fall back to source block A.
            fallbackBlock = mv->blockA;
        } else {
            return;  // precise flash shown — done.
        }

        auto* bi = m_canvasScene->findBlockItem(fallbackBlock);
        if (!bi) return;
        bi->setToolLocked(true);
        m_canvasScene->refreshAllBlockItems();
        // Auto-clear after 1.5 seconds.
        QTimer::singleShot(1500, this, [this, fallbackBlock]() {
            auto* item = m_canvasScene->findBlockItem(fallbackBlock);
            if (item) {
                item->setToolLocked(false);
                m_canvasScene->refreshAllBlockItems();
            }
        });
    });

    // Hover/click an angle measure card → flash the two source segments plus
    // the half-arc (previously only the reference block was highlighted).
    connect(m_variablePanel, &cad::ui::VariablePanel::highlightAngleMeasureRequested,
            this, [this](const QUuid& angleMeasureId) {
        const auto* am = m_paramDoc->findAngleMeasure(angleMeasureId);
        if (!am) return;
        if (!m_canvasScene->flashAngleMeasure(am->blockA, am->segmentA,
                                              am->blockB, am->segmentB)) {
            auto* bi = m_canvasScene->findBlockItem(am->blockA);
            if (!bi) return;
            bi->setToolLocked(true);
            m_canvasScene->refreshAllBlockItems();
            QTimer::singleShot(1500, this, [this, blockId = am->blockA]() {
                auto* item = m_canvasScene->findBlockItem(blockId);
                if (item) {
                    item->setToolLocked(false);
                    m_canvasScene->refreshAllBlockItems();
                }
            });
        }
    });

}

void MainWindow::onPageTabChanged(int index)
{
    if (m_tabSyncGuard || !m_pageTabs || !m_pageStack)
        return;

    if (index == 0) {
        // 画布页: 主区域恒为画布。面板开着时把高亮留在分类按钮上, 避免
        // 标签条高亮一个与面板状态无关的标签 (组件大标签无主窗口开关,
        // 高亮回画布)。
        if (panelVisible()) {
            const int cat = m_panelBigBar->currentIndex();
            m_tabSyncGuard = true;
            m_pageTabs->setCurrentIndex(cat < 2 ? 1 + cat : 0);
            m_tabSyncGuard = false;
        }
        return;
    }

    const int category = index - 1;  // 0=变量 1=图层
    if (panelVisible() && m_panelBigBar->currentIndex() == category) {
        // 再点当前分类 = 隐藏面板（开关语义）。Hide 事件经 eventFilter
        // 触发 syncPanelTabs 把主标签同步回画布。
        m_panelWindow->hide();
        return;
    }

    // 打开面板 / 切换分类: 大标签跟随, 悬浮窗显示并置前。
    m_panelBigBar->setCurrentIndex(category);
    ensurePanelWindowPosition();
    m_panelWindow->show();
    m_panelWindow->raise();
    m_panelWindow->activateWindow();
    syncPanelTabs();
}

void MainWindow::syncPanelTabs()
{
    if (!m_pageTabs || !m_panelWindow || !m_panelBigBar)
        return;

    // 主标签选中态 = 面板状态: 打开 → 当前分类按钮选中 (组件大标签无
    // 对应主窗口开关, 高亮留在画布); 关闭 → 画布。
    const bool open = panelVisible();
    const int cat = m_panelBigBar->currentIndex();
    const int target = (open && cat < 2) ? 1 + cat : 0;
    if (m_pageTabs->currentIndex() != target) {
        m_tabSyncGuard = true;
        m_pageTabs->setCurrentIndex(target);
        m_tabSyncGuard = false;
    }

    // 激活指示: 面板打开时当前分类按钮带强调色圆点, 其余清空。
    for (int i = 1; i < m_pageTabs->count(); ++i) {
        const bool active = open && cat < 2 && (i - 1) == cat;
        m_pageTabs->setTabIcon(i,
            active ? panelActiveDot(cad::ui::Theme::tokens().text1) : QIcon());
    }
}

bool MainWindow::panelVisible() const
{
    return m_panelWindow && m_panelWindow->isVisible();
}

void MainWindow::refreshPanelChrome()
{
    if (!m_hidePanelBtn)
        return;
    const auto& tk = cad::ui::Theme::tokens();
    // 幽灵小按钮 QSS 随主题重建 (透明底, hover 出 surface2 底 + 描边)。
    const QString ghostBtnQss = QStringLiteral(
        "QToolButton { background: transparent; border: 1px solid transparent;"
        "  border-radius: 2px; padding: 1px; }"
        "QToolButton:hover { background: %1; border: 1px solid %2; }")
        .arg(tk.surface2.name(), tk.border.name());
    m_hidePanelBtn->setStyleSheet(ghostBtnQss);
    m_hidePanelBtn->setIcon(cad::ui::IconHelper::iconByName(
        QStringLiteral("x"), tk.text2));
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

void MainWindow::flashStatus(const QString& text, const QColor& color, int ms)
{
    if (!m_flashLabel || !m_flashTimer)
        return;
    m_flashLabel->setText(text);
    m_flashLabel->setStyleSheet(
        QStringLiteral("%1 font-weight: 600; color: %2; background: transparent;")
            .arg(cad::ui::ThemeTokens::kMonospaceFamily, color.name()));
    m_flashLabel->show();
    m_flashTimer->start(ms);
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

void MainWindow::ensurePanelWindowPosition()
{
    if (!m_panelWindow)
        return;
    // 用户拖动 / QSettings 记忆位置后不再自动归位 (§4.1: 记忆位置)。
    if (m_panelWindowPositioned)
        return;
    m_panelWindowPositioned = true;

    QScreen* scr = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1280, 800);

    // 侧边栏样式: 长竖条但不要太长 —— 宽 ~360, 高不超过屏幕可用区的 80%
    // 且封顶 660px。高度留有余量, 用户仍可拖动边框自行调整。
    const int w = qBound(320, avail.width() / 5, 380);
    const int h = qBound(400, avail.height() * 8 / 10, 660);
    m_panelWindow->resize(w, h);

    // 贴主窗口右缘, 从标签栏下方起排, 像一个脱开的右侧边栏。
    const QRect mainGeo = geometry();
    int x = mainGeo.right() - m_panelWindow->width() - 12;
    int y = mainGeo.top() + 96;
    if (x + m_panelWindow->width() > avail.right())
        x = avail.right() - m_panelWindow->width() - 8;
    if (y + m_panelWindow->height() > avail.bottom())
        y = avail.bottom() - m_panelWindow->height() - 8;
    x = qMax(x, avail.left() + 8);
    y = qMax(y, avail.top() + 8);
    m_panelWindow->move(x, y);
}
// ============================================================
// File operations
// ============================================================

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

bool MainWindow::maybeSave()
{
    if (m_undoStack->isClean())
        return true;

    // 按钮布局: [取消] [不保存] [保存·主] —— 右按钮是主按钮(高亮)。
    // 2026-08 用户反馈: 旧布局 [保存] [不保存] [取消·主] 中"取消"是
    // 高亮主按钮, 用户按习惯点主按钮以为会退出, 实际却中止关闭,
    // 造成"点取消关闭不了程序"的困惑。主按钮必须是对应"退出"的动作。
    auto ret = cad::ui::ElaMsgBox::show(this, QString::fromUtf8("野风帖"),
        QString::fromUtf8("文档已修改，是否保存？"),
        QString::fromUtf8("取消"),    // Left: 中止关闭, 回到程序
        QString::fromUtf8("保存"),    // Right: 主按钮, 保存并退出
        QString::fromUtf8("不保存")); // Middle: 不保存退出

    if (ret == cad::ui::ElaMsgBox::Result::Right) {
        onSaveDocument();
        return m_undoStack->isClean();  // false if save failed
    }
    if (ret == cad::ui::ElaMsgBox::Result::Middle)
        return true;  // 不保存 → 退出
    return false;     // 取消 → 中止关闭
}

void MainWindow::updateTitle()
{
    QString name = m_currentFilePath.isEmpty()
        ? QString::fromUtf8("未命名")
        : QFileInfo(m_currentFilePath).fileName();
    QString dirty = m_undoStack->isClean() ? QString() : QStringLiteral("*");
    setWindowTitle(QStringLiteral("%1%2 - 野风帖 [P207-ABS]").arg(dirty, name));
}

void MainWindow::clearDocument()
{
    m_paramDoc->clear();   // 内部重置 activeLayer 为第一个工作层，但不发射信号
    m_lastWorkingLayer = m_paramDoc->layersView().firstWorkingLayerId();
    if (m_actionToggleAuxLayer)
        m_actionToggleAuxLayer->setChecked(false);
    m_undoStack->clear();
    m_undoStack->setClean();
    m_currentFilePath.clear();
    updateTitle();
}

void MainWindow::onNewDocument()
{
    if (!maybeSave()) return;
    clearDocument();
}

void MainWindow::onOpenDocument()
{
    if (!maybeSave()) return;

    QString path = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("打开文件"), QString(),
        QString::fromUtf8(cad::doc::DocumentFile::kFilter));
    if (path.isEmpty()) return;

    QString error;
    QStringList warnings;
    if (!cad::doc::DocumentFile::load(path, *m_paramDoc, &error, &warnings)) {
        cad::ui::ElaMsgBox::critical(this, QString::fromUtf8("打开失败"), error);
        return;
    }
    showLoadWarnings(this, warnings);

    m_currentFilePath = path;
    m_undoStack->clear();
    m_undoStack->setClean();
    addRecentFile(path);
    updateTitle();
    // 文档加载不会发射 activeLayerChanged，手动同步 H 动作的勾选状态。
    onActiveLayerChanged(m_paramDoc->layersView().activeLayer());
}

void MainWindow::onSaveDocument()
{
    if (m_currentFilePath.isEmpty()) {
        onSaveAsDocument();
        return;
    }

    QString error;
    if (!cad::doc::DocumentFile::save(m_currentFilePath, *m_paramDoc, &error)) {
        cad::ui::ElaMsgBox::critical(this, QString::fromUtf8("保存失败"), error);
        return;
    }

    m_undoStack->setClean();
    addRecentFile(m_currentFilePath);
    updateTitle();
    // 保存成功反馈 (§6.5): 状态栏 success 对勾 + 文件名, 2s 淡出。
    flashStatus(QStringLiteral("\u2713 已保存 %1")
                    .arg(QFileInfo(m_currentFilePath).fileName()),
                cad::ui::Theme::tokens().success, 2000);
}

void MainWindow::onSaveAsDocument()
{
    QString path = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("另存为"), QString(),
        QString::fromUtf8(cad::doc::DocumentFile::kFilter));
    if (path.isEmpty()) return;

    // Ensure .gcad extension.
    if (!path.endsWith(QStringLiteral(".gcad"), Qt::CaseInsensitive))
        path += QStringLiteral(".gcad");

    QString error;
    if (!cad::doc::DocumentFile::save(path, *m_paramDoc, &error)) {
        cad::ui::ElaMsgBox::critical(this, QString::fromUtf8("保存失败"), error);
        return;
    }

    m_currentFilePath = path;
    m_undoStack->setClean();
    addRecentFile(path);
    updateTitle();
    flashStatus(QStringLiteral("\u2713 已保存 %1").arg(QFileInfo(path).fileName()),
                cad::ui::Theme::tokens().success, 2000);
}

void MainWindow::onOpenRecentFile()
{
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) return;

    if (!maybeSave()) return;

    QString path = action->data().toString();
    QString error;
    QStringList warnings;
    if (!cad::doc::DocumentFile::load(path, *m_paramDoc, &error, &warnings)) {
        cad::ui::ElaMsgBox::critical(this, QString::fromUtf8("打开失败"), error);
        m_recentFiles.removeAll(path);
        updateRecentFilesMenu();
        return;
    }
    showLoadWarnings(this, warnings);

    m_currentFilePath = path;
    m_undoStack->clear();
    m_undoStack->setClean();
    addRecentFile(path);
    updateTitle();
}

void MainWindow::addRecentFile(const QString& path)
{
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > 10)
        m_recentFiles.removeLast();

    QSettings settings;
    settings.setValue(QStringLiteral("recentFiles"), m_recentFiles);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu()
{
    if (!m_recentFilesMenu) return;
    m_recentFilesMenu->clear();

    if (m_recentFiles.isEmpty()) {
        QAction* empty = m_recentFilesMenu->addAction(QString::fromUtf8("(无)"));
        empty->setEnabled(false);
        return;
    }

    for (const QString& path : m_recentFiles) {
        QAction* act = m_recentFilesMenu->addAction(QFileInfo(path).fileName());
        act->setData(path);
        act->setToolTip(path);
        connect(act, &QAction::triggered, this, &MainWindow::onOpenRecentFile);
    }
}
