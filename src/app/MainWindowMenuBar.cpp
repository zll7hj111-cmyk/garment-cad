#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QSettings>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWidget>

#include "ElaMenu.h"
#include "ElaMenuBar.h"
#include "canvas/CanvasScene.h"
#include "tools/ToolManager.h"
#include "tools/ToolRegistry.h"
#include "ui/ElaMsgBox.h"
#include "ui/IconHelper.h"
#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"

void MainWindow::setupMenuBar()
{
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

    auto& toolReg = cad::tools::ToolRegistry::instance();
    m_toolOrder = QList<cad::tools::ToolType>(toolReg.order().begin(),
                                              toolReg.order().end());
    auto* group = new QActionGroup(this);
    group->setExclusive(true);
    for (cad::tools::ToolType type : m_toolOrder) {
        const auto* d = toolReg.descriptor(type);
        auto* act = new QAction(
            cad::ui::IconHelper::iconByName(d->iconName, cad::ui::Theme::tokens().text2),
            d->displayName, this);
        act->setCheckable(true);
        act->setChecked(type == cad::tools::ToolType::Select);
        if (!d->shortcut.isEmpty()) {
            act->setShortcut(d->shortcut);
            act->setShortcutContext(Qt::ApplicationShortcut);
        }
        act->setToolTip(cad::ui::TooltipFormatter::tool(d->displayName, d->shortcut, d->hintText));
        group->addAction(act);
        toolMenu->addAction(act);
        m_toolActions.insert(type, act);
        connect(act, &QAction::triggered, this, [this, type]() {
            m_toolManager->switchTool(type);
        });
    }

    toolMenu->addSeparator();

    m_actionToggleAuxLayer = new QAction(
        QString::fromUtf8("切换辅助层(&H)"), this);
    m_actionToggleAuxLayer->setCheckable(true);
    m_actionToggleAuxLayer->setShortcut(QKeySequence(Qt::Key_H));
    m_actionToggleAuxLayer->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_actionToggleAuxLayer, &QAction::triggered,
            this, &MainWindow::actionToggleAuxLayer);
    toolMenu->addAction(m_actionToggleAuxLayer);

    // ===== 视图菜单 =====
    QMenu* viewMenu = elaMenuBar->addMenu(QString::fromUtf8("视图(&V)"));

    m_actionToggleTheme = new QAction(QString::fromUtf8("暗色主题(&D)"), this);
    m_actionToggleTheme->setCheckable(true);
    m_actionToggleTheme->setChecked(
        cad::ui::Theme::mode() == cad::ui::ThemeMode::Dark);
    m_actionToggleTheme->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    m_actionToggleTheme->setShortcutContext(Qt::ApplicationShortcut);
    this->addAction(m_actionToggleTheme);
    connect(m_actionToggleTheme, &QAction::toggled,
            this, &MainWindow::toggleTheme);
    viewMenu->addAction(m_actionToggleTheme);

    m_actionToggleDirArrows = new QAction(
        QString::fromUtf8("线段方向箭头(&A)"), this);
    m_actionToggleDirArrows->setCheckable(true);
    m_actionToggleDirArrows->setChecked(
        QSettings().value(QStringLiteral("view/directionArrows"), true).toBool());
    if (m_canvasScene)
        m_canvasScene->setDirectionArrowsEnabled(m_actionToggleDirArrows->isChecked());
    connect(m_actionToggleDirArrows, &QAction::toggled, this, [this](bool on) {
        if (m_canvasScene)
            m_canvasScene->setDirectionArrowsEnabled(on);
        QSettings().setValue(QStringLiteral("view/directionArrows"), on);
    });
    viewMenu->addAction(m_actionToggleDirArrows);

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
