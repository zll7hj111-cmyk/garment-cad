#pragma once

#include <QMainWindow>
#include <QString>

#include "parametric/Variable.h"

class QLabel;
class QToolBar;
class QAction;
class CanvasView;
class CanvasScene;
class SidePanel;
class VariablePanel;

namespace cad::param { class ParamDocument; }
namespace cad::tools { class ToolManager; enum class ToolType; }

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSceneMouseMoved(qreal x, qreal y);
    void onZoomChanged(double factor);
    void onToolChanged(cad::tools::ToolType type, const char* name);
    void onVariableEdited(const cad::param::Variable& var);
    void onFormulasRecomputed();

    void actionSelect();
    void actionSmartPen();

private:
    void setupUi();
    void setupToolBar();
    void setupStatusBar();
    void setupSidePanel();
    void connectSignals();

    CanvasView*  m_canvasView = nullptr;
    CanvasScene* m_canvasScene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    cad::tools::ToolManager* m_toolManager = nullptr;

    QToolBar* m_toolBar = nullptr;
    QAction*  m_actionSelect  = nullptr;
    QAction*  m_actionSmartPen = nullptr;

    QLabel* m_coordLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QLabel* m_toolHintLabel = nullptr;

    VariablePanel* m_variablePanel = nullptr;
    SidePanel*     m_sidePanel = nullptr;
};
