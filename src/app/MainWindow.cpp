#include "SegmentEditBar.h"
#include "SmartPenPreInputBar.h"

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

#include "canvas/CanvasView.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "geometry/Units.h"
#include "tools/ToolManager.h"
#include "tools/ToolSelect.h"
#include "tools/ToolSmartPen.h"
#include "tools/LinePropertyDialog.h"
#include "ui/VariablePanel.h"
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
    m_undoStack = new QUndoStack(this);
    m_canvasScene = new CanvasScene(m_paramDoc, this);
    m_canvasView = new CanvasView(m_canvasScene, this);
    m_toolManager = new ToolManager(m_canvasScene, this);
    m_toolManager->setParamDocument(m_paramDoc);
    m_toolManager->setUndoStack(m_undoStack);
    m_lastWorkingLayer = m_paramDoc->firstWorkingLayerId();

    // Connect tool manager to view for event dispatch
    m_canvasView->setToolManager(m_toolManager);
    m_canvasView->setParamDoc(m_paramDoc);

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
    // Release). Drop every child→window connection before that happens.
    if (m_undoStack) disconnect(m_undoStack, nullptr, this, nullptr);
    if (m_paramDoc)  disconnect(m_paramDoc,  nullptr, this, nullptr);
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

    m_actionSelect = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("cursor-click"), cad::ui::Theme::tokens().text2),
        QString::fromUtf8("选择(&V)"), this);
    m_actionSelect->setCheckable(true);
    m_actionSelect->setChecked(true);
    m_actionSelect->setShortcut(QKeySequence(Qt::Key_V));
    m_actionSelect->setShortcutContext(Qt::ApplicationShortcut);
    toolMenu->addAction(m_actionSelect);

    m_actionSmartPen = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("pen"), cad::ui::Theme::tokens().text2),
        QString::fromUtf8("智能笔(&L)"), this);
    m_actionSmartPen->setCheckable(true);
    m_actionSmartPen->setShortcut(QKeySequence(Qt::Key_L));
    m_actionSmartPen->setShortcutContext(Qt::ApplicationShortcut);
    toolMenu->addAction(m_actionSmartPen);

    m_actionCurveEdit = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("pen"), cad::ui::Theme::tokens().text2),
        QString::fromUtf8("曲线(&C)"), this);
    m_actionCurveEdit->setCheckable(true);
    m_actionCurveEdit->setShortcut(QKeySequence(Qt::Key_C));
    m_actionCurveEdit->setShortcutContext(Qt::ApplicationShortcut);
    toolMenu->addAction(m_actionCurveEdit);

    m_actionRotate = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("rotate"), cad::ui::Theme::tokens().text2),
        QString::fromUtf8("旋转(&R)"), this);
    m_actionRotate->setCheckable(true);
    m_actionRotate->setShortcut(QKeySequence(Qt::Key_R));
    m_actionRotate->setShortcutContext(Qt::ApplicationShortcut);
    toolMenu->addAction(m_actionRotate);

    m_actionBreak = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("scissors"), cad::ui::Theme::tokens().text2),
        QString::fromUtf8("打断(&B)"), this);
    m_actionBreak->setCheckable(true);
    m_actionBreak->setShortcut(QKeySequence(Qt::Key_B));
    m_actionBreak->setShortcutContext(Qt::ApplicationShortcut);
    toolMenu->addAction(m_actionBreak);

    m_actionIntersection = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("funnel"), cad::ui::Theme::tokens().text2),
        QString::fromUtf8("交点(&I)"), this);
    m_actionIntersection->setCheckable(true);
    m_actionIntersection->setShortcut(QKeySequence(Qt::Key_I));
    m_actionIntersection->setShortcutContext(Qt::ApplicationShortcut);
    toolMenu->addAction(m_actionIntersection);

    m_actionMeasure = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("ruler"), cad::ui::Theme::tokens().text2),
        QString::fromUtf8("测量(&M)"), this);
    m_actionMeasure->setCheckable(true);
    // M 快捷键让给画布长按显示长度（CanvasView::keyPressEvent），测量暂不设快捷键
    toolMenu->addAction(m_actionMeasure);

    m_actionAngleMeasure = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("rotate"), cad::ui::Theme::tokens().text2),
        QString::fromUtf8("角度测量(&A)"), this);
    m_actionAngleMeasure->setCheckable(true);
    m_actionAngleMeasure->setShortcut(QKeySequence(Qt::Key_A));
    m_actionAngleMeasure->setShortcutContext(Qt::ApplicationShortcut);
    toolMenu->addAction(m_actionAngleMeasure);

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

    // Exclusive tool selection shared between menu and toolbar.
    auto* group = new QActionGroup(this);
    group->setExclusive(true);
    group->addAction(m_actionSelect);
    group->addAction(m_actionSmartPen);
    group->addAction(m_actionCurveEdit);
    group->addAction(m_actionRotate);
    group->addAction(m_actionBreak);
    group->addAction(m_actionIntersection);
    group->addAction(m_actionMeasure);
    group->addAction(m_actionAngleMeasure);

    connect(m_actionSelect,   &QAction::triggered, this, &MainWindow::actionSelect);
    connect(m_actionSmartPen, &QAction::triggered, this, &MainWindow::actionSmartPen);
    connect(m_actionCurveEdit, &QAction::triggered, this, &MainWindow::actionCurveEdit);
    connect(m_actionRotate,   &QAction::triggered, this, &MainWindow::actionRotate);
    connect(m_actionBreak,    &QAction::triggered, this, &MainWindow::actionBreak);
    connect(m_actionIntersection, &QAction::triggered, this, &MainWindow::actionIntersection);
    connect(m_actionMeasure,  &QAction::triggered, this, &MainWindow::actionMeasure);
    connect(m_actionAngleMeasure, &QAction::triggered, this, &MainWindow::actionAngleMeasure);
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

    struct ToolSpec { QAction* act; ElaIconType::IconName icon; };
    const ToolSpec specs[] = {
        {m_actionSelect,       ElaIconType::ArrowPointer},
        {m_actionSmartPen,     ElaIconType::PenNib},
        {m_actionCurveEdit,    ElaIconType::BezierCurve},
        {m_actionRotate,       ElaIconType::Rotate},
        {m_actionBreak,        ElaIconType::Scissors},
        {m_actionIntersection, ElaIconType::Intersection},
        {m_actionMeasure,      ElaIconType::RulerCombined},
        {m_actionAngleMeasure, ElaIconType::Angle},
    };
    for (const auto& spec : specs) {
        auto* btn = new ElaToolButton(pill);
        btn->setDefaultAction(spec.act);
        btn->setElaIcon(spec.icon);
        btn->setIsTransparent(true);
        btn->setCursor(Qt::PointingHandCursor);
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

    const QUuid cur = m_paramDoc->activeLayer();
    const auto* layer = m_paramDoc->layerById(cur);
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
    // legible (normal = secondary text, active/checked = 墨黑).
    const auto& t = cad::ui::Theme::tokens();
    const QColor normal = t.text2;
    const QColor active = t.text1;
    auto setIcon = [&](QAction* act, const char* name) {
        act->setIcon(cad::ui::IconHelper::icon2State(
            QString::fromLatin1(name), normal, active));
    };
    setIcon(m_actionSelect,      "cursor-click");
    setIcon(m_actionSmartPen,    "pen");
    setIcon(m_actionCurveEdit,   "pen");
    setIcon(m_actionRotate,      "rotate");
    setIcon(m_actionBreak,       "scissors");
    setIcon(m_actionIntersection,"crosshair");
    setIcon(m_actionMeasure,     "ruler");
    setIcon(m_actionAngleMeasure,"rotate");
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
    refreshToolIcons();
    refreshLayerChip();
    syncPanelTabs();  // 激活指示圆点颜色跟随主题
}

void MainWindow::setupStatusBar()
{
    // ElaStatusBar: Fluent-styled status bar (QStatusBar drop-in).
    auto* sb = new ElaStatusBar(this);
    setStatusBar(sb);

    m_toolHintLabel = new ElaText(QString::fromUtf8("选择：点击选取（单选即操作）| W切换单选/多选 | 右键取消/确认 | 空白右键→智能笔 | 双击编辑 | Del删除"), 13, this);
    sb->addWidget(m_toolHintLabel, 1);

    m_coordLabel = new ElaText("X: 0.000  Y: 0.000", 12, this);
    m_coordLabel->setObjectName(QStringLiteral("coordLabel"));
    m_coordLabel->setMinimumWidth(220);
    // CAD readouts: monospace digits so drag values never jitter.
    m_coordLabel->setStyleSheet(cad::ui::ThemeTokens::kMonospaceFamily);

    m_zoomLabel = new ElaText(QString::fromUtf8("缩放: 100%"), 12, this);
    m_zoomLabel->setObjectName(QStringLiteral("zoomLabel"));
    m_zoomLabel->setMinimumWidth(100);
    m_zoomLabel->setStyleSheet(cad::ui::ThemeTokens::kMonospaceFamily);

    // Attachment diagnostics (hidden while the document is healthy).
    m_diagLabel = new ElaText(QString(), 13, this);
    m_diagLabel->setObjectName(QStringLiteral("dangerText"));
    m_diagLabel->hide();

    // 创建后内嵌编辑条 (SegmentEditBar): 替代旧的创建弹窗. 默认隐藏,
    // 智能笔创建线段后由 onLineCreated 显示.
    m_segmentEditBar = new cad::app::SegmentEditBar(m_paramDoc, this);
    m_segmentEditBar->setUndoStack(m_undoStack);
    m_segmentEditBar->hide();
    sb->addWidget(m_segmentEditBar, 1);

    // 智能笔预输入条: 切到智能笔时显示, 名称为/长度/角度供下一条线使用,
    // 内容被使用后清空 (onLineCreated). 默认隐藏 (默认工具是选择).
    m_preInputBar = new cad::app::SmartPenPreInputBar(this);
    m_preInputBar->setCanvasView(m_canvasView);
    m_preInputBar->hide();
    sb->addWidget(m_preInputBar, 1);

    sb->addPermanentWidget(m_diagLabel);
    sb->addPermanentWidget(m_coordLabel);
    sb->addPermanentWidget(m_zoomLabel);
}

void MainWindow::connectSignals()
{
    connect(m_canvasView, &CanvasView::mouseScenePosChanged,
            this, &MainWindow::onSceneMouseMoved);
    connect(m_canvasView, &CanvasView::zoomFactorChanged,
            this, &MainWindow::onZoomChanged);
    connect(m_canvasScene, &CanvasScene::forceShowChanged,
            this, &MainWindow::onForceShowChanged);
    connect(m_toolManager, &ToolManager::activeToolChanged,
            this, &MainWindow::onToolChanged);
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
    connect(m_segmentEditBar, &cad::app::SegmentEditBar::cancelRequested,
            this, &MainWindow::onEditStripCancel);

    // 预输入条 → 活动智能笔: 每次编辑都同步 (含清空), 工具在起笔时快照。
    connect(m_preInputBar, &cad::app::SmartPenPreInputBar::valuesChanged,
            this, &MainWindow::onPreInputChanged);

    // Selection tool picked a single segment → show it in the edit strip.
    // Both ids null = clear (multi-select / deselection).
    connect(m_toolManager, &ToolManager::editTargetChanged,
            this, &MainWindow::onEditTargetChanged);
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

void MainWindow::onDocumentChanged()
{
    const auto& diags = m_paramDoc->diagnostics();
    if (diags.empty()) {
        m_diagLabel->clear();
        m_diagLabel->hide();
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

    m_diagLabel->setText(diags.size() == 1
        ? QStringLiteral("⚠ %1").arg(first)
        : QStringLiteral("⚠ %1 个连接问题：%2")
              .arg(diags.size()).arg(first));
    m_diagLabel->show();
}

void MainWindow::onToolChanged(ToolType type, const char* name)
{
    (void)name;

    // Sync the menu/toolbar check state. The blank-space right-click switch
    // (智能笔 ↔ 选择) goes through switchTool() WITHOUT a QAction trigger, so
    // the exclusive QActionGroup would otherwise keep highlighting the
    // previous tool. The group is exclusive: setChecked(true) unchecks the
    // rest automatically; re-setting the same action is a harmless no-op.
    switch (type) {
    case ToolType::Select:        m_actionSelect->setChecked(true); break;
    case ToolType::SmartPen:      m_actionSmartPen->setChecked(true); break;
    case ToolType::CurveEdit:     m_actionCurveEdit->setChecked(true); break;
    case ToolType::Rotate:        m_actionRotate->setChecked(true); break;
    case ToolType::Break:         m_actionBreak->setChecked(true); break;
    case ToolType::Intersection:  m_actionIntersection->setChecked(true); break;
    case ToolType::Measure:       m_actionMeasure->setChecked(true); break;
    case ToolType::AngleMeasure:  m_actionAngleMeasure->setChecked(true); break;
    }

    // Fluent tool buttons: the pill buttons are QAction-driven but their
    // highlight is drawn from setIsSelected, so mirror the active tool here.
    int toolIndex = -1;
    switch (type) {
    case ToolType::Select:        toolIndex = 0; break;
    case ToolType::SmartPen:      toolIndex = 1; break;
    case ToolType::CurveEdit:     toolIndex = 2; break;
    case ToolType::Rotate:        toolIndex = 3; break;
    case ToolType::Break:         toolIndex = 4; break;
    case ToolType::Intersection:  toolIndex = 5; break;
    case ToolType::Measure:       toolIndex = 6; break;
    case ToolType::AngleMeasure:  toolIndex = 7; break;
    }
    for (int i = 0; i < m_toolButtons.size(); ++i)
        m_toolButtons.at(i)->setIsSelected(i == toolIndex);

    const bool smartPenActive =
        m_toolManager->activeToolType() == ToolType::SmartPen;

    if (smartPenActive) {
        m_toolHintLabel->setText(
            QString::fromUtf8("智能笔：点设起点 | 再点设终点 | Shift约束45° | 右键/Esc取消 | 空白右键→选择"));
    } else if (m_toolManager->activeToolType() == ToolType::CurveEdit) {
        m_toolHintLabel->setText(
            QString::fromUtf8("曲线：点线身加曲线点 | 拖曲线点弯曲 | 拖手柄调切线 | Ctrl加点 Shift删点 | Esc取消"));
    } else if (m_toolManager->activeToolType() == ToolType::Rotate) {
        m_toolHintLabel->setText(
            QString::fromUtf8("旋转：点击线段选中 | 拖动旋转(Shift吸附15°) | HUD输入角度/公式 | 右键/Esc取消"));
    } else if (m_toolManager->activeToolType() == ToolType::Break) {
        m_toolHintLabel->setText(
            QString::fromUtf8("打断：点击辅助点打断线段 | 点击线段空白处先建点再打断"));
    } else if (m_toolManager->activeToolType() == ToolType::Intersection) {
        m_toolHintLabel->setText(
            QString::fromUtf8("交点：点选目标线段 | 点选射线起点 | 点击借用点直接创建交点 | 悬停点预览指向 | W切换跟随角度/绝对角度 | 右键/Esc取消"));
    } else if (m_toolManager->activeToolType() == ToolType::Measure) {
        m_toolHintLabel->setText(
            QString::fromUtf8("测量：点选第一个点 | 点选第二个点 → 自动发布距离变量 | 右键/Esc取消"));
    } else {
        m_toolHintLabel->setText(
            QString::fromUtf8("选择：点击选取实体（单选即操作）| W切换单选/多选 | 右键取消/确认 | 空白右键→智能笔 | 双击编辑 | Del删除"));
    }

    // 创建后编辑条只在智能笔创建场景有意义; 切换工具即隐藏.
    if (m_segmentEditBar)
        m_segmentEditBar->hideBar();

    // 智能笔预输入条: 仅智能笔激活时显示 (替换长提示文本, 状态栏给输入框
    // 让位); 切回时把已输入的值推给新创建的工具实例。
    if (m_preInputBar) {
        m_preInputBar->setVisible(smartPenActive);
        if (smartPenActive)
            onPreInputChanged();
    }
    if (m_toolHintLabel)
        m_toolHintLabel->setVisible(!smartPenActive);
}

void MainWindow::onLineCreated(const QUuid& blockId, const QUuid& segmentId)
{
    // 预输入一次性语义：内容已被本次创建使用 → 清空, 恢复空白的预输入状态。
    if (m_preInputBar)
        m_preInputBar->clearAll();
    if (!m_segmentEditBar) return;
    m_segmentEditBar->showForLine(blockId, segmentId);
}

void MainWindow::onPreInputChanged()
{
    if (!m_preInputBar || !m_toolManager) return;
    if (auto* pen = dynamic_cast<cad::tools::ToolSmartPen*>(
            m_toolManager->activeTool())) {
        pen->setPreInput({
            m_preInputBar->nameText(),
            m_preInputBar->lengthText(),
            m_preInputBar->angleText()
        });
    }
}

void MainWindow::onLinePreview(double lenCm, double angleDeg)
{
    if (!m_segmentEditBar) return;
    m_segmentEditBar->showPreview(lenCm, angleDeg);
}

void MainWindow::onEditStripCancel()
{
    // 创建模式 Esc = 删除: rewind the undo stack to the creation point — this
    // drops the creation command AND any strip edits pushed since (strip
    // edits now commit through SegmentEditBarCommand, so one rewind removes
    // them all). Selection mode: rewinds only edits made since the pick.
    if (m_segmentEditBar)
        m_segmentEditBar->cancelCreation();
}

void MainWindow::onEditTargetChanged(const QUuid& blockId, const QUuid& segmentId)
{
    if (!m_segmentEditBar) return;
    if (segmentId.isNull()) {
        // Multi-select, deselection or blank click — nothing to edit.
        m_segmentEditBar->hideBar();
        return;
    }
    // Selection-mode pick: show without stealing keyboard focus (the canvas
    // keeps focus for W-toggle / Del / marquee interactions).
    m_segmentEditBar->showForLine(blockId, segmentId, false);
}

void MainWindow::onForceShowChanged(bool showNames, bool showLengths)
{
    // Keep every open LinePropertyDialog's display toggles in sync while the
    // N/M hold-to-show keys are pressed (parent chain: dialogs live under the
    // canvas view). No-op when none are open.
    const auto dialogs = m_canvasView->findChildren<cad::tools::LinePropertyDialog*>();
    for (auto* dlg : dialogs)
        dlg->applyHoldOverride(showNames, showLengths);
}

void MainWindow::actionSelect()
{
    m_toolManager->switchTool(ToolType::Select);
}

void MainWindow::actionSmartPen()
{
    m_toolManager->switchTool(ToolType::SmartPen);
}

void MainWindow::actionCurveEdit()
{
    m_toolManager->switchTool(ToolType::CurveEdit);
}

void MainWindow::actionRotate()
{
    m_toolManager->switchTool(ToolType::Rotate);
}

void MainWindow::actionBreak()
{
    m_toolManager->switchTool(ToolType::Break);
}

void MainWindow::actionIntersection()
{
    m_toolManager->switchTool(ToolType::Intersection);
}

void MainWindow::actionMeasure()
{
    m_toolManager->switchTool(ToolType::Measure);
}

void MainWindow::actionAngleMeasure()
{
    m_toolManager->switchTool(ToolType::AngleMeasure);
}

void MainWindow::actionToggleAuxLayer()
{
    if (!m_paramDoc) return;
    const QUuid cur = m_paramDoc->activeLayer();
    if (m_paramDoc->isAuxLayer(cur)) {
        // 已在辅助层 → 切回最近一次的工作层。首次启动/打开文件时记忆可能为
        // 空或已失效，回退到第一个工作层，避免 H 被“静默无效”。
        QUuid target = m_lastWorkingLayer;
        if (target.isNull() || !m_paramDoc->layerById(target) ||
            m_paramDoc->isAuxLayer(target))
            target = m_paramDoc->firstWorkingLayerId();
        m_paramDoc->setActiveLayer(target);
    } else {
        // 记住当前工作层，再切到辅助层。
        m_lastWorkingLayer = cur;
        m_paramDoc->setActiveLayer(m_paramDoc->auxLayerId());
    }
}

void MainWindow::onActiveLayerChanged(const QUuid& layerId)
{
    // 任何路径切到工作层都更新记忆，H 总能回到最近的工作层。
    if (m_paramDoc && !m_paramDoc->isAuxLayer(layerId))
        m_lastWorkingLayer = layerId;
    if (m_actionToggleAuxLayer)
        m_actionToggleAuxLayer->setChecked(
            m_paramDoc && m_paramDoc->isAuxLayer(layerId));
    refreshLayerChip();
}

void MainWindow::setupPages()
{
    // 画布页含 QOpenGLWidget：Ela 页面切换动画（Popup/Scale）会 grab() 整页
    // 离屏渲染，软渲染环境下 QOpenGLWidget 离屏 grab 段错误 → 点击导航即闪退。
    // 改用 None（无动画直切），规避 grab。
    setStackSwitchMode(ElaWindowType::StackSwitchMode::None);

    // 变量/图层/组 面板统一进独立悬浮窗 (Qt::Tool): 侧边栏样式的长竖条窗口,
    // 频繁编辑/复制公式、切图层/组时不再来回切主窗口标签页。初始隐藏,
    // 点击主窗口顶部 变量/图层/组 标签显示并切到对应大标签页; 窗口可自由
    // 拖动、缩放, X 关闭即隐藏 (标签可再开)。
    m_panelWindow = new QWidget(this, Qt::Tool);
    m_panelWindow->setObjectName(QStringLiteral("panelFloatingWindow"));
    m_panelWindow->setWindowTitle(QString::fromUtf8("面板"));
    m_panelWindow->setMinimumSize(300, 360);
    m_panelWindow->setMaximumWidth(480);  // 保持侧边栏竖条观感, 高度自由.
    m_panelWindow->installEventFilter(this);  // X 关闭/隐藏 → 主标签同步回画布
    auto* winLay = new QVBoxLayout(m_panelWindow);
    winLay->setContentsMargins(8, 8, 8, 8);
    winLay->setSpacing(6);

    // 分类大标签: 变量 / 图层, 与主窗口后两个标签一一对应。
    m_panelBigBar = new ElaTabBar(m_panelWindow);
    m_panelBigBar->addTab(QStringLiteral("变量"));
    m_panelBigBar->addTab(QStringLiteral("图层"));
    m_panelBigBar->setTabSize(QSize(84, 32));
    m_panelBigBar->setExpanding(true);
    m_panelBigBar->setUsesScrollButtons(false);
    m_panelBigBar->setElideMode(Qt::ElideNone);
    m_panelBigBar->setDrawBase(false);
    m_panelBigBar->setTabsClosable(false);
    m_panelBigBar->setMovable(false);
    m_panelBigBar->setAcceptDrops(false);
    m_panelBigBar->setCursor(Qt::PointingHandCursor);
    m_panelBigBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    winLay->addWidget(m_panelBigBar);

    m_panelStack = new QStackedWidget(m_panelWindow);
    m_variablePanel = new cad::ui::VariablePanel(m_paramDoc, m_panelStack);
    m_variablePanel->setUndoStack(m_undoStack);
    m_panelStack->addWidget(m_variablePanel);

    auto* layerPage = new QWidget(m_panelStack);
    auto* layerLay = new QVBoxLayout(layerPage);
    layerLay->setContentsMargins(12, 12, 12, 12);
    m_layerPanel = new LayerPanel(m_paramDoc, layerPage);
    m_layerPanel->setUndoStack(m_undoStack);
    layerLay->addWidget(m_layerPanel);
    m_panelStack->addWidget(layerPage);

    winLay->addWidget(m_panelStack, 1);

    // 悬浮窗内切换大标签 → 切换面板内容页; 主窗口对应按钮跟随高亮。
    connect(m_panelBigBar, &QTabBar::currentChanged, this, [this](int category) {
        if (m_panelStack && m_panelStack->currentIndex() != category)
            m_panelStack->setCurrentIndex(category);
        if (!m_tabSyncGuard && m_panelWindow->isVisible())
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

    auto* pageHost = new QWidget(this);
    auto* pageLay = new QVBoxLayout(pageHost);
    pageLay->setContentsMargins(8, 6, 8, 0);
    pageLay->setSpacing(6);
    pageLay->addWidget(m_pageTabs);
    pageLay->addWidget(m_pageStack, 1);
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
        // 标签条高亮一个与面板状态无关的标签。
        if (m_panelWindow->isVisible()) {
            m_tabSyncGuard = true;
            m_pageTabs->setCurrentIndex(1 + m_panelBigBar->currentIndex());
            m_tabSyncGuard = false;
        }
        return;
    }

    const int category = index - 1;  // 0=变量 1=图层
    if (m_panelWindow->isVisible() && m_panelBigBar->currentIndex() == category) {
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

    // 主标签选中态 = 面板状态: 打开 → 当前分类按钮选中; 关闭 → 画布。
    const bool open = m_panelWindow->isVisible();
    const int target = open ? 1 + m_panelBigBar->currentIndex() : 0;
    if (m_pageTabs->currentIndex() != target) {
        m_tabSyncGuard = true;
        m_pageTabs->setCurrentIndex(target);
        m_tabSyncGuard = false;
    }

    // 激活指示: 面板打开时当前分类按钮带强调色圆点, 其余清空。
    for (int i = 1; i < m_pageTabs->count(); ++i) {
        const bool active = open && (i - 1) == m_panelBigBar->currentIndex();
        m_pageTabs->setTabIcon(i,
            active ? panelActiveDot(cad::ui::Theme::tokens().text1) : QIcon());
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_panelWindow && event->type() == QEvent::Hide && !m_tabSyncGuard)
        syncPanelTabs();  // X 关闭 / hide() → 主标签回画布、清激活指示
    return QObject::eventFilter(obj, event);
}

void MainWindow::ensurePanelWindowPosition()
{
    if (!m_panelWindow)
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
    m_lastWorkingLayer = m_paramDoc->firstWorkingLayerId();
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
    onActiveLayerChanged(m_paramDoc->activeLayer());
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
