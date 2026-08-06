#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

class QLabel;
class QToolBar;
class QAction;
class QMenu;
class QUndoStack;
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

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSceneMouseMoved(qreal x, qreal y);
    void onZoomChanged(double factor);
    void onToolChanged(cad::tools::ToolType type, const char* name);
    /// Refresh the status-bar diagnostic label after each resolve pass.
    void onDocumentChanged();

    void actionSelect();
    void actionSmartPen();
    void actionCurveEdit();
    void actionRotate();
    void actionBreak();
    void actionIntersection();
    void actionMeasure();
    void actionAngleMeasure();
    /// H: toggle the active layer between the auxiliary layer and the last
    /// working layer (辅助层 ↔ 最近工作层).
    void actionToggleAuxLayer();
    /// Track the last working layer for the H toggle and the action's
    /// checked state (checked = currently on the auxiliary layer).
    void onActiveLayerChanged(int index);

    // File operations
    void onNewDocument();
    void onOpenDocument();
    void onSaveDocument();
    void onSaveAsDocument();
    void onOpenRecentFile();

private:
    void setupUi();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupSidePanel();
    void connectSignals();

    void updateTitle();
    void addRecentFile(const QString& path);
    void updateRecentFilesMenu();
    bool maybeSave();
    void clearDocument();

    CanvasView*  m_canvasView = nullptr;
    CanvasScene* m_canvasScene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    cad::tools::ToolManager* m_toolManager = nullptr;
    QUndoStack* m_undoStack = nullptr;

    QToolBar* m_toolBar = nullptr;
    QAction*  m_actionSelect  = nullptr;
    QAction*  m_actionSmartPen = nullptr;
    QAction*  m_actionCurveEdit = nullptr;
    QAction*  m_actionRotate  = nullptr;
    QAction*  m_actionBreak   = nullptr;
    QAction*  m_actionIntersection = nullptr;
    QAction*  m_actionMeasure = nullptr;
    QAction*  m_actionAngleMeasure = nullptr;
    QAction*  m_actionToggleAuxLayer = nullptr;

    // File menu actions
    QMenu*    m_recentFilesMenu = nullptr;
    QAction*  m_actionUndo = nullptr;
    QAction*  m_actionRedo = nullptr;

    QLabel* m_coordLabel = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QLabel* m_toolHintLabel = nullptr;
    QLabel* m_diagLabel = nullptr;   ///< Attachment diagnostics warning (hidden when healthy).

    VariablePanel* m_variablePanel = nullptr;
    SidePanel*     m_sidePanel = nullptr;

    QString     m_currentFilePath;   ///< Empty = untitled.
    QStringList m_recentFiles;
    /// H 键切回的目标工作层（最近一次离开的工作层，初始为图层 1）。
    int         m_lastWorkingLayer = 1;
};
