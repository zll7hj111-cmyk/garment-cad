#include "MainWindow.h"

#include <QLabel>
#include <QStatusBar>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QMenuBar>
#include <QMessageBox>
#include <QUndoStack>
#include <QFileDialog>
#include <QSettings>
#include <QCloseEvent>
#include <QFileInfo>
#include <QApplication>
#include <QTimer>

#include "canvas/CanvasView.h"
#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "geometry/Units.h"
#include "tools/ToolManager.h"
#include "tools/ToolSelect.h"
#include "ui/VariablePanel.h"
#include "ui/SidePanel.h"
#include "ui/LayerPanel.h"
#include "ui/GroupPanel.h"
#include "ui/IconHelper.h"
#include "document/DocumentFile.h"

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
    QMessageBox::warning(parent, QString::fromUtf8("文件加载完成"),
        QString::fromUtf8("文件已加载，但有 %1 处内容无法识别，已按默认值恢复：\n\n%2")
            .arg(warnings.size()).arg(shown.join(QStringLiteral("\n"))));
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_paramDoc = new ParamDocument(this);
    m_undoStack = new QUndoStack(this);
    m_canvasScene = new CanvasScene(m_paramDoc, this);
    m_canvasView = new CanvasView(m_canvasScene, this);
    m_toolManager = new ToolManager(m_canvasScene, this);
    m_toolManager->setParamDocument(m_paramDoc);
    m_toolManager->setUndoStack(m_undoStack);

    // Connect tool manager to view for event dispatch
    m_canvasView->setToolManager(m_toolManager);
    m_canvasView->setParamDoc(m_paramDoc);

    setupUi();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupSidePanel();
    connectSignals();

    setWindowIcon(cad::ui::IconHelper::appIcon());
    resize(1280, 800);

    // Load recent files from settings.
    QSettings settings;
    m_recentFiles = settings.value(QStringLiteral("recentFiles")).toStringList();

    updateTitle();
}

MainWindow::~MainWindow()
{
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
    setCentralWidget(m_canvasView);
}

void MainWindow::setupMenuBar()
{
    // ===== 文件菜单 =====
    QMenu* fileMenu = menuBar()->addMenu(QString::fromUtf8("文件(&F)"));

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
    QMenu* editMenu = menuBar()->addMenu(QString::fromUtf8("编辑(&E)"));
    m_actionUndo = m_undoStack->createUndoAction(this, QString::fromUtf8("撤销(&U)"));
    m_actionUndo->setShortcut(QKeySequence::Undo);
    editMenu->addAction(m_actionUndo);

    m_actionRedo = m_undoStack->createRedoAction(this, QString::fromUtf8("重做(&R)"));
    m_actionRedo->setShortcut(QKeySequence::Redo);
    editMenu->addAction(m_actionRedo);

    // ===== 工具菜单 =====
    QMenu* toolMenu = menuBar()->addMenu(QString::fromUtf8("工具(&T)"));

    m_actionSelect = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("cursor-click"), QColor(0x34, 0x49, 0x5E)),
        QString::fromUtf8("选择(&V)"), this);
    m_actionSelect->setCheckable(true);
    m_actionSelect->setChecked(true);
    m_actionSelect->setShortcut(QKeySequence(Qt::Key_V));
    toolMenu->addAction(m_actionSelect);

    m_actionSmartPen = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("pen"), QColor(0x34, 0x49, 0x5E)),
        QString::fromUtf8("智能笔(&L)"), this);
    m_actionSmartPen->setCheckable(true);
    m_actionSmartPen->setShortcut(QKeySequence(Qt::Key_L));
    toolMenu->addAction(m_actionSmartPen);

    m_actionCurveEdit = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("pen"), QColor(0xE9, 0x1E, 0x63)),
        QString::fromUtf8("曲线(&C)"), this);
    m_actionCurveEdit->setCheckable(true);
    m_actionCurveEdit->setShortcut(QKeySequence(Qt::Key_C));
    toolMenu->addAction(m_actionCurveEdit);

    m_actionRotate = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("rotate"), QColor(0x34, 0x49, 0x5E)),
        QString::fromUtf8("旋转(&R)"), this);
    m_actionRotate->setCheckable(true);
    m_actionRotate->setShortcut(QKeySequence(Qt::Key_R));
    toolMenu->addAction(m_actionRotate);

    m_actionBreak = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("scissors"), QColor(0x34, 0x49, 0x5E)),
        QString::fromUtf8("打断(&B)"), this);
    m_actionBreak->setCheckable(true);
    m_actionBreak->setShortcut(QKeySequence(Qt::Key_B));
    toolMenu->addAction(m_actionBreak);

    m_actionIntersection = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("funnel"), QColor(0x34, 0x49, 0x5E)),
        QString::fromUtf8("交点(&I)"), this);
    m_actionIntersection->setCheckable(true);
    m_actionIntersection->setShortcut(QKeySequence(Qt::Key_I));
    toolMenu->addAction(m_actionIntersection);

    m_actionMeasure = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("ruler"), QColor(0x34, 0x49, 0x5E)),
        QString::fromUtf8("测量(&M)"), this);
    m_actionMeasure->setCheckable(true);
    m_actionMeasure->setShortcut(QKeySequence(Qt::Key_M));
    toolMenu->addAction(m_actionMeasure);

    m_actionAngleMeasure = new QAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("rotate"), QColor(0x8E, 0x44, 0xAD)),
        QString::fromUtf8("角度测量(&A)"), this);
    m_actionAngleMeasure->setCheckable(true);
    m_actionAngleMeasure->setShortcut(QKeySequence(Qt::Key_A));
    toolMenu->addAction(m_actionAngleMeasure);

    toolMenu->addSeparator();

    // H: 辅助层 ↔ 最近工作层 快速切换（再按一次切回）。
    m_actionToggleAuxLayer = new QAction(
        QString::fromUtf8("切换辅助层(&H)"), this);
    m_actionToggleAuxLayer->setCheckable(true);
    m_actionToggleAuxLayer->setShortcut(QKeySequence(Qt::Key_H));
    connect(m_actionToggleAuxLayer, &QAction::triggered,
            this, &MainWindow::actionToggleAuxLayer);
    toolMenu->addAction(m_actionToggleAuxLayer);

    // ===== 帮助菜单 =====
    QMenu* helpMenu = menuBar()->addMenu(QString::fromUtf8("帮助(&H)"));
    QAction* aboutAction = helpMenu->addAction(
        cad::ui::IconHelper::iconByName(QStringLiteral("t-shirt"), QColor(0x2E, 0x86, 0xC1)),
        QString::fromUtf8("关于(&A)"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QString::fromUtf8("关于 服装CAD"),
            QString::fromUtf8("参数化服装 CAD 系统 v0.1.0\n基于 C++23 / Qt6 构建"));
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
    m_toolBar = addToolBar(QString::fromUtf8("工具"));
    m_toolBar->setMovable(false);
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // Reuse the menu actions (already checkable + exclusive via QActionGroup).
    m_toolBar->addAction(m_actionSelect);
    m_toolBar->addAction(m_actionSmartPen);
    m_toolBar->addAction(m_actionCurveEdit);
    m_toolBar->addAction(m_actionRotate);
    m_toolBar->addAction(m_actionBreak);
    m_toolBar->addAction(m_actionIntersection);
    m_toolBar->addAction(m_actionMeasure);
    m_toolBar->addAction(m_actionAngleMeasure);
}

void MainWindow::setupStatusBar()
{
    m_toolHintLabel = new QLabel(QString::fromUtf8("选择：点击选取（单选即操作）| W切换单选/多选 | 右键取消/确认 | 空白右键→智能笔 | 双击编辑 | Del删除"), this);
    statusBar()->addWidget(m_toolHintLabel, 1);

    m_coordLabel = new QLabel("X: 0.000  Y: 0.000", this);
    m_coordLabel->setMinimumWidth(220);

    m_zoomLabel = new QLabel(QString::fromUtf8("缩放: 100%"), this);
    m_zoomLabel->setMinimumWidth(100);

    // Attachment diagnostics (hidden while the document is healthy).
    m_diagLabel = new QLabel(this);
    m_diagLabel->setStyleSheet("color:#C0392B;");
    m_diagLabel->hide();

    statusBar()->addPermanentWidget(m_diagLabel);
    statusBar()->addPermanentWidget(m_coordLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
}

void MainWindow::connectSignals()
{
    connect(m_canvasView, &CanvasView::mouseScenePosChanged,
            this, &MainWindow::onSceneMouseMoved);
    connect(m_canvasView, &CanvasView::zoomFactorChanged,
            this, &MainWindow::onZoomChanged);
    connect(m_toolManager, &ToolManager::activeToolChanged,
            this, &MainWindow::onToolChanged);
    connect(m_paramDoc, &ParamDocument::documentChanged,
            this, &MainWindow::onDocumentChanged);
    connect(m_undoStack, &QUndoStack::cleanChanged,
            this, &MainWindow::updateTitle);
    connect(m_paramDoc, &ParamDocument::activeLayerChanged,
            this, &MainWindow::onActiveLayerChanged);
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

    if (m_toolManager->activeToolType() == ToolType::SmartPen) {
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
    const int cur = m_paramDoc->activeLayer();
    if (m_paramDoc->isAuxLayer(cur)) {
        // 已在辅助层 → 切回最近一次的工作层。
        m_paramDoc->setActiveLayer(m_lastWorkingLayer);
    } else {
        // 记住当前工作层，再切到辅助层（遍历查找，不假设索引 0）。
        m_lastWorkingLayer = cur;
        const auto& layers = m_paramDoc->layers();
        for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
            if (m_paramDoc->isAuxLayer(i)) {
                m_paramDoc->setActiveLayer(i);
                break;
            }
        }
    }
}

void MainWindow::onActiveLayerChanged(int index)
{
    // 任何路径切到工作层都更新记忆，H 总能回到最近的工作层。
    if (m_paramDoc && !m_paramDoc->isAuxLayer(index))
        m_lastWorkingLayer = index;
    if (m_actionToggleAuxLayer)
        m_actionToggleAuxLayer->setChecked(
            m_paramDoc && m_paramDoc->isAuxLayer(index));
}

void MainWindow::setupSidePanel()
{
    m_sidePanel = new SidePanel(m_paramDoc, m_canvasScene, this);
    addDockWidget(Qt::RightDockWidgetArea, m_sidePanel);
    m_variablePanel = m_sidePanel->variablePanel();
    m_variablePanel->setUndoStack(m_undoStack);
    m_sidePanel->layerPanel()->setUndoStack(m_undoStack);
    m_sidePanel->groupPanel()->setUndoStack(m_undoStack);

    // Click a group card → activate the selection tool and select the whole
    // group (confirmed, drag-ready).
    connect(m_sidePanel->groupPanel(), &GroupPanel::selectGroupRequested,
            this, [this](const QList<QUuid>& blockIds) {
        m_toolManager->switchTool(cad::tools::ToolType::Select);
        if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(
                m_toolManager->activeTool()))
            ts->selectBlocksExternally(blockIds);
    });

    // Click a group BADGE on canvas → select the whole group (same path).
    connect(m_canvasScene, &CanvasScene::groupBadgeClicked,
            this, [this](const QUuid& groupId) {
        m_toolManager->switchTool(cad::tools::ToolType::Select);
        if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(
                m_toolManager->activeTool()))
            ts->selectBlocksExternally(m_paramDoc->blocksInGroup(groupId));
    });

    // Click a linked card → flash the source block red on canvas.
    connect(m_variablePanel, &VariablePanel::highlightBlockRequested,
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

    // Click a measure card → flash the TWO measured points precisely.
    // Bridge-line measurements (ownerBlockId set) and dangling/unresolved
    // sources keep the existing whole-block highlight path.
    connect(m_variablePanel, &VariablePanel::highlightMeasureRequested,
            this, [this](const QUuid& measureId) {
        const auto* mv = m_paramDoc->findMeasure(measureId);
        if (!mv) return;

        QUuid fallbackBlock;
        if (!mv->ownerBlockId.isNull()) {
            // Owned (bridge) measurement: keep the whole-block flash.
            fallbackBlock = mv->ownerBlockId;
        } else if (!m_canvasScene->flashMeasure(mv->blockA, mv->pointA,
                                                 mv->blockB, mv->pointB)) {
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

    auto ret = QMessageBox::warning(this, QString::fromUtf8("服装CAD"),
        QString::fromUtf8("文档已修改，是否保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (ret == QMessageBox::Save) {
        onSaveDocument();
        return m_undoStack->isClean();  // false if save failed
    }
    if (ret == QMessageBox::Cancel)
        return false;
    return true;  // Discard
}

void MainWindow::updateTitle()
{
    QString name = m_currentFilePath.isEmpty()
        ? QString::fromUtf8("未命名")
        : QFileInfo(m_currentFilePath).fileName();
    QString dirty = m_undoStack->isClean() ? QString() : QStringLiteral("*");
    setWindowTitle(QStringLiteral("%1%2 - 服装CAD").arg(dirty, name));
}

void MainWindow::clearDocument()
{
    m_paramDoc->clear();   // 内部重置 activeLayer=1 但不发射信号
    m_lastWorkingLayer = 1;
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
        QMessageBox::critical(this, QString::fromUtf8("打开失败"), error);
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
        QMessageBox::critical(this, QString::fromUtf8("保存失败"), error);
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
        QMessageBox::critical(this, QString::fromUtf8("保存失败"), error);
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
        QMessageBox::critical(this, QString::fromUtf8("打开失败"), error);
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
