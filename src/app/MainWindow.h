#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include "ElaWindow.h"
#include "ElaText.h"

class QAction;
class QMenu;
class QUndoStack;
class QWidget;
class QStackedWidget;
class ElaToolButton;
class ElaTabBar;
class ElaMenu;
class QFrame;
class CanvasView;
class CanvasScene;
class LayerPanel;
class GroupPanel;

namespace cad::ui { class VariablePanel; }

namespace cad::param { class ParamDocument; }
namespace cad::tools { class ToolManager; enum class ToolType; }
namespace cad::app { class SegmentEditBar; }
class MainWindow : public ElaWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// 视图 → 暗色主题: swap the global theme + canvas tokens live.
    void toggleTheme(bool dark);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSceneMouseMoved(qreal x, qreal y);
    void onZoomChanged(double factor);
    void onToolChanged(cad::tools::ToolType type, const char* name);
    /// Refresh the status-bar diagnostic label after each resolve pass.
    void onDocumentChanged();
    /// A segment was just created by the smart pen — show the status-bar
    /// edit strip (名称/长度/角度) instead of the old creation dialog.
    void onLineCreated(const QUuid& blockId, const QUuid& segmentId);
    /// Live readout while a stroke is being drawn (creation preview).
    void onLinePreview(double lenCm, double angleDeg);
    /// Esc in the edit strip = 撤销创建 (delete the line) + hide the strip.
    void onEditStripCancel();
    /// Selection tool's single-segment pick → edit strip (both ids null =
    /// clear, e.g. multi-select or deselection).
    void onEditTargetChanged(const QUuid& blockId, const QUuid& segmentId);
    /// Hold-to-show (N/L keys) changed on canvas — keep open
    /// LinePropertyDialogs' display toggles in sync.
    void onForceShowChanged(bool showNames, bool showLengths);

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
    void onActiveLayerChanged(const QUuid& layerId);

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
    /// Build the Fluent navigation pages (画布 / 变量 / 图层 / 组) — the
    /// right-hand SidePanel dock was replaced by ElaWindow navigation pages.
    void setupPages();
    void connectSignals();

    /// Rebuild the eight tool-action icons from the current theme tokens
    /// (normal = secondary text, active = accent) so both themes stay legible.
    void refreshToolIcons();
    /// Rebuild the canvas-toolbar layer chip (dot icon + active layer name +
    /// popup layer-switch menu) from the current doc and theme tokens.
    void refreshLayerChip();

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

    QWidget* m_centerContainer = nullptr;  ///< Central wrapper: pill dock + canvas.
    QWidget* m_toolDock = nullptr;         ///< Floating pill toolbar strip.
    /// 网页式标签页导航：顶部标签栏 + 自建页面堆栈（替代 Ela 左侧导航）。
    ElaTabBar* m_pageTabs = nullptr;
    QStackedWidget* m_pageStack = nullptr;
    /// Fluent tool buttons inside the pill (order matches the tool actions).
    QList<ElaToolButton*> m_toolButtons;
    /// Canvas-toolbar layer chip: dot + active layer name, popup = switch
    /// layer. Always visible so the user never paints on the wrong layer.
    ElaToolButton* m_layerChip = nullptr;
    ElaMenu*       m_layerChipMenu = nullptr;
    QFrame*        m_layerChipSeparator = nullptr;
    QAction*  m_actionSelect  = nullptr;
    QAction*  m_actionSmartPen = nullptr;
    QAction*  m_actionCurveEdit = nullptr;
    QAction*  m_actionRotate  = nullptr;
    QAction*  m_actionBreak   = nullptr;
    QAction*  m_actionIntersection = nullptr;
    QAction*  m_actionMeasure = nullptr;
    QAction*  m_actionAngleMeasure = nullptr;
    QAction*  m_actionToggleAuxLayer = nullptr;
    /// 视图 → 暗色主题 (checkable, mirrors Theme::mode()).
    QAction*  m_actionToggleTheme = nullptr;

    // File menu actions
    QMenu*    m_recentFilesMenu = nullptr;
    QAction*  m_actionUndo = nullptr;
    QAction*  m_actionRedo = nullptr;

    ElaText* m_coordLabel = nullptr;
    ElaText* m_zoomLabel = nullptr;
    ElaText* m_toolHintLabel = nullptr;
    ElaText* m_diagLabel = nullptr;   ///< Attachment diagnostics warning (hidden when healthy).
    cad::app::SegmentEditBar* m_segmentEditBar = nullptr;  ///< 创建后内嵌编辑条 (状态栏).

    cad::ui::VariablePanel* m_variablePanel = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    GroupPanel* m_groupPanel = nullptr;

    QString     m_currentFilePath;   ///< Empty = untitled.
    QStringList m_recentFiles;
    /// H 键切回的目标工作层（最近一次离开的工作层，初始为图层 1）。
    QUuid       m_lastWorkingLayer;   ///< First working layer id after doc reset.
};
