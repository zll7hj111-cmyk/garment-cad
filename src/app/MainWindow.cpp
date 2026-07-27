#include "MainWindow.h"

#include <QLabel>
#include <QStatusBar>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>

#include "DemoData.h"
#include "canvas/CanvasView.h"
#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"
#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "geometry/Units.h"
#include "tools/ToolManager.h"
#include "ui/VariablePanel.h"
#include "ui/SidePanel.h"

using cad::param::ParamDocument;
using cad::tools::ToolManager;
using cad::tools::ToolType;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_paramDoc = new ParamDocument(this);
    m_canvasScene = new CanvasScene(m_paramDoc, this);
    m_canvasView = new CanvasView(m_canvasScene, this);
    m_toolManager = new ToolManager(m_canvasScene, this);
    m_toolManager->setParamDocument(m_paramDoc);

    // Connect tool manager to view for event dispatch
    m_canvasView->setToolManager(m_toolManager);

    setupUi();
    setupToolBar();
    setupStatusBar();
    setupSidePanel();
    connectSignals();

    setWindowTitle(QString::fromUtf8("服装CAD"));
    resize(1280, 800);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    setCentralWidget(m_canvasView);
}

void MainWindow::setupToolBar()
{
    m_toolBar = addToolBar(QString::fromUtf8("工具"));
    m_toolBar->setMovable(false);
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // Action group for exclusive tool selection
    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    m_actionSelect = m_toolBar->addAction(QString::fromUtf8("选择 (V)"));
    m_actionSelect->setCheckable(true);
    m_actionSelect->setChecked(true);
    m_actionSelect->setShortcut(QKeySequence(Qt::Key_V));
    group->addAction(m_actionSelect);

    m_actionSmartPen = m_toolBar->addAction(QString::fromUtf8("智能笔 (L)"));
    m_actionSmartPen->setCheckable(true);
    m_actionSmartPen->setShortcut(QKeySequence(Qt::Key_L));
    group->addAction(m_actionSmartPen);

    connect(m_actionSelect,   &QAction::triggered, this, &MainWindow::actionSelect);
    connect(m_actionSmartPen, &QAction::triggered, this, &MainWindow::actionSmartPen);
}

void MainWindow::setupStatusBar()
{
    m_toolHintLabel = new QLabel(QString::fromUtf8("选择：点击选取实体 | 双击线段编辑属性 | Del删除"), this);
    statusBar()->addWidget(m_toolHintLabel, 1);

    m_coordLabel = new QLabel("X: 0.000  Y: 0.000", this);
    m_coordLabel->setMinimumWidth(220);

    m_zoomLabel = new QLabel(QString::fromUtf8("缩放: 100%"), this);
    m_zoomLabel->setMinimumWidth(100);

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
    connect(m_variablePanel, &VariablePanel::variableEdited,
            this, &MainWindow::onVariableEdited);
    connect(m_variablePanel, &VariablePanel::formulasRecomputed,
            this, &MainWindow::onFormulasRecomputed);
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
    (void)type;
    (void)name;
    if (m_toolManager->activeToolType() == ToolType::SmartPen) {
        m_toolHintLabel->setText(
            QString::fromUtf8("智能笔：点击设起点 | 再点击设终点 | 右键/Esc取消 | 长按Shift角度约束(45°)"));
    } else {
        m_toolHintLabel->setText(
            QString::fromUtf8("选择：点击选取实体 | 双击线段编辑属性 | Del删除"));
    }
}

void MainWindow::onVariableEdited(const cad::param::Variable& var)
{
    // Sync edited variable into ParamDocument and re-resolve all formulas.
    // Parameters are stored in cm — the formula domain unit.
    const double cm = cad::geo::Units::mmToCm(var.value);
    QHash<QString, double> batch;
    batch.insert(var.name, cm);
    if (!var.refName.isEmpty())
        batch.insert(var.refName, cm);
    m_paramDoc->setParameters(batch);  // single resolveAll(), triggers refresh via signal
}

void MainWindow::onFormulasRecomputed()
{
    // Sync formula BASE values (conditions NOT applied) so composite segment
    // formulas never inherit condition adjustments (no propagation). Conditions
    // are synced separately and only fire on standalone references.
    QHash<QString, double> baseCm;
    QHash<QString, QList<cad::param::Condition>> condByName;
    for (const auto& f : m_variablePanel->formulas()) {
        if (f.valid && !f.name.isEmpty()) {
            baseCm.insert(f.name, cad::geo::Units::mmToCm(f.baseValue));
            if (f.conditionsEnabled && !f.conditions.isEmpty())
                condByName.insert(f.name, f.conditions);
        }
    }
    m_paramDoc->syncFormulaConditions(condByName);  // store first (no resolve)
    m_paramDoc->syncFormulaParameters(baseCm);       // store + resolve, triggers refresh via signal
}

void MainWindow::actionSelect()
{
    m_toolManager->switchTool(ToolType::Select);
}

void MainWindow::actionSmartPen()
{
    m_toolManager->switchTool(ToolType::SmartPen);
}

void MainWindow::setupSidePanel()
{
    m_sidePanel = new SidePanel(m_paramDoc, m_canvasScene, this);
    addDockWidget(Qt::RightDockWidgetArea, m_sidePanel);
    m_variablePanel = m_sidePanel->variablePanel();

    // Load demo variables and sync into ParamDocument.
    const auto demoVars = cad::demo::defaultVariables();
    m_variablePanel->setVariables(demoVars);

    QHash<QString, double> initParams;
    for (const auto& v : demoVars) {
        const double cm = cad::geo::Units::mmToCm(v.value);
        initParams.insert(v.name, cm);
        if (!v.refName.isEmpty())
            initParams.insert(v.refName, cm);
    }
    m_paramDoc->setParameters(initParams);

    // Load demo formulas.
    m_variablePanel->setFormulas(cad::demo::defaultFormulas());

    // Initial sync of formula results into ParamDocument.
    onFormulasRecomputed();
}
