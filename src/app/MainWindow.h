#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include "tools/ToolRegistry.h"
#include "ElaWindow.h"
#include "ElaText.h"
#include "canvas/InputDispatcher.h"   // cad::canvas::SegmentHit (P1-6)

class QAction;
class QMenu;
class QUndoStack;
class QWidget;
class QEvent;
class QStackedWidget;
class QSplitter;
class QToolButton;
class QTimer;
class ElaToolButton;
class ElaTabBar;
class ElaMenu;
class QFrame;
class CanvasView;
class CanvasScene;
class LayerPanel;

namespace cad::ui { class VariablePanel; class ComponentTab; }

namespace cad::param { class ParamDocument; }
namespace cad::tools { class ToolManager; }
namespace cad::app { class ContextStrip; }
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
    /// 面板悬浮窗隐藏（X 关闭 / 程序 hide）时同步主窗口标签选中态。
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onSceneMouseMoved(qreal x, qreal y);
    void onZoomChanged(double factor);
    /// Right-click on a segment (reported by the canvas): owns the
    /// 发布长度参数 / 添加辅助点 / 烘焙到操作层 menu and its commands (P1-6).
    void onSegmentContextMenu(const cad::canvas::SegmentHit& hit);
    void onToolChanged(cad::tools::ToolType type, const char* name);
    /// M5: 活动工具上报的运行期提示覆盖 (智能笔 直线/省道线); 空串 = 恢复
    /// describe() 的默认文案。切工具时由 onToolChanged 清掉, 不跨工具继承。
    void onToolHintOverride(const QString& hint);
    /// Refresh the status-bar diagnostic label after each resolve pass.
    void onDocumentChanged();
    /// A segment was just created by the smart pen — show the status-bar
    /// edit strip (名称/长度/角度) instead of the old creation dialog.
    void onLineCreated(const QUuid& blockId, const QUuid& segmentId);
    /// Live readout while a stroke is being drawn (creation preview).
    void onLinePreview(double lenCm, double angleDeg);
    /// Esc in the edit strip = 撤销创建 (delete the line) + hide the strip.
    void onEditStripCancel();
    /// 上下文属性条焦点 (CONTEXT_STRIP_DESIGN.md): 工具点击锁定 → 条带可编辑。
    /// 两个 id 均为 null = 解除锁定 (取消选择 / 多选 / 点空白)。
    void onPinnedTargetChanged(const QUuid& blockId, const QUuid& segmentId);
    /// 光标悬停候选 → 条带只读预览。两个 id 均为 null = 移出所有线段。
    void onHoverTargetChanged(const QUuid& blockId, const QUuid& segmentId);
    /// 连接角度会话 (CONTEXT_STRIP_DESIGN.md 二期): 连接手势的角度输入并入
    /// 条带 —— 会话开始/结束 (attachmentId 空 = 结束) 与合法性 (红边提示)。
    void onConnectAngleSessionChanged(const QUuid& blockId, const QUuid& segmentId,
                                      const QUuid& attachmentId, double initialAngle);
    void onConnectAngleValidityChanged(bool valid);
    /// 旋转工具锚心状态 (2026-12): 旋转会话内条带换向 = 切换锚心 —— 基准
    /// 读数锚心端在前 + 换向按钮资格/原因。
    void onRotateAnchorStateChanged(bool active, bool anchorIsEnd, bool canToggle,
                                    const QString& reason);
    /// Hold-to-show (N/L keys) changed on canvas — keep open
    /// LinePropertyDialogs' display toggles in sync.
    void onForceShowChanged(bool showNames, bool showLengths);
    /// 网页式标签切换：变量/图层/组 标签不再对应主窗口页面, 而是切换
    /// 独立悬浮窗（面板窗）的大标签页。
    void onPageTabChanged(int index);

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
    /// Build the Fluent navigation pages (画布) + 面板悬浮窗 (变量/图层/组件) —
    /// the right-hand SidePanel dock was replaced by ElaWindow navigation pages.
    void setupPages();
    void connectSignals();

    /// 面板当前是否可见 (恒为悬浮窗形态)。
    [[nodiscard]] bool panelVisible() const;
    /// 编辑条带可见性同步 (上下文属性条可见即显示条带)。
    void updateEditBand();
    /// 状态栏瞬时反馈 (§6.5): 保存成功 / 撤销重做, @p ms 后自动消失。
    void flashStatus(const QString& text, const QColor& color, int ms);
    /// 主题切换后重建条带/徽章等状态栏 chrome 配色。
    void refreshStatusBarChrome();
    /// 重建面板头行隐藏按钮的图标与提示 (随主题)。
    void refreshPanelChrome();
    /// 面板悬浮窗（Qt::Tool 侧边栏样式）: 主窗口顶部 变量/图层 标签是
    /// 开关, 点击显示/切换大标签页, 再点当前分类隐藏。
    /// 同步主窗口标签选中态 + 激活指示圆点到当前面板状态。
    void syncPanelTabs();
    /// 首次显示时按侧边栏样式定位: 主窗口右侧、长竖条、不超屏幕可用区。
    void ensurePanelWindowPosition();

    /// Rebuild the eight tool-action icons from the current theme tokens
    /// (normal = secondary text, active = accent) so both themes stay legible.
    void refreshToolIcons();
    /// Rebuild the canvas-toolbar layer chip (dot icon + active layer name +
    /// popup layer-switch menu) from the current doc and theme tokens.
    void refreshLayerChip();

    /// 工具提示统一入口 (M9, TOOL_SYSTEM_AUDIT): 全文存 m_toolHintFull +
    /// tooltip 兜底, label 上只显示按当前宽度 elide 的文本 (QLabel 无原生
    /// elide, ElaText 亦无)。onToolChanged 与初始状态都走这里。
    void setToolHint(const QString& text);
    /// 按当前 label 宽度重算省略文本 (label 的 Resize 事件触发)。
    void applyToolHintElide();

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
    /// 变量/图层 标签不再是页面：它们是面板悬浮窗的开关, 打开时对应
    /// 按钮保持选中高亮 + 强调色圆点 (syncPanelTabs)。
    bool m_tabSyncGuard = false;       ///< 防 currentChanged 重入.
    /// Fluent tool buttons inside the pill (order matches the tool actions).
    QList<ElaToolButton*> m_toolButtons;
    /// Canvas-toolbar layer chip: dot + active layer name, popup = switch
    /// layer. Always visible so the user never paints on the wrong layer.
    ElaToolButton* m_layerChip = nullptr;
    ElaMenu*       m_layerChipMenu = nullptr;
    QFrame*        m_layerChipSeparator = nullptr;
    /// 工具 action 表 (TOOL_SYSTEM_AUDIT P3): 由 ToolRegistry 遍历生成,
    /// type → 菜单/工具坞共用的 QAction, 替代 8 个手写成员。
    QHash<cad::tools::ToolType, QAction*> m_toolActions;
    /// 工具注册序 (与工具坞按钮序一致, onToolChanged 索引用)。
    QList<cad::tools::ToolType> m_toolOrder;
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
    /// 工具提示全文 (M9): label 上只显示 elide 后的文本, 悬停 tooltip 看
    /// 全文; resize 时用它重算。Resize 事件经 eventFilter 驱动
    /// applyToolHintElide 重算。也是 M5 运行期覆盖的唯一落点 ——
    /// 不另设 "覆盖值" 成员 (只写不读的死状态, 见本报告 H2 的教训)。
    QString m_toolHintFull;
    /// 诊断 badge (§6.5): danger 实心 ⚠ N, 点击弹出明细; 健康时完全不占位。
    QToolButton* m_diagBadge = nullptr;
    /// 瞬时反馈文字 (保存成功/撤销重做, 数秒后自动消失)。
    ElaText* m_flashLabel = nullptr;
    QTimer*  m_flashTimer = nullptr;
    int      m_lastUndoIndex = 0;   ///< undo/redo 方向判定基准 (flashStatus).
    /// 编辑条带 (§4.6): 画布下方的独立条带, accentTint 底 + accentStrong
    /// 描边, 与坐标/缩放/诊断信息视觉分离。宿主上下文属性条 (CONTEXT_STRIP)。
    QWidget* m_editBand = nullptr;
    /// 上下文属性条: 显示"当前关注的线段" (悬停只读 / 锁定可编辑)。
    cad::app::ContextStrip* m_contextStrip = nullptr;

    cad::ui::VariablePanel* m_variablePanel = nullptr;
    /// 面板悬浮窗 (Qt::Tool, 侧边栏样式长竖条): 恒为悬浮形态 —— 贴主窗口
    /// 右缘悬浮、不进主窗口布局, 面板永不挤占/裁剪画布 (2026-08-28 拍板:
    /// 悬浮窗定位的本意就是不占画面; 停靠右栏形态已否决移除)。
    /// 头行 (大标签 + 隐藏 ✕) + 页面堆栈 (变量/图层/组件)。
    QWidget* m_panelHeader = nullptr;
    QToolButton* m_hidePanelBtn = nullptr; ///< ✕ 隐藏面板.
    QWidget* m_panelWindow = nullptr;
    ElaTabBar*     m_panelBigBar = nullptr;   ///< 悬浮窗内 变量/图层/组件 大标签.
    QStackedWidget* m_panelStack = nullptr;
    bool     m_panelWindowPositioned = false;  ///< 记忆位置/用户拖动后不再自动归位.
    LayerPanel* m_layerPanel = nullptr;
    cad::ui::ComponentTab* m_componentTab = nullptr;  ///< 组件大标签页 (升出变量面板).

    QString     m_currentFilePath;   ///< Empty = untitled.
    QStringList m_recentFiles;
    /// H 键切回的目标工作层（最近一次离开的工作层，初始为图层 1）。
    QUuid       m_lastWorkingLayer;   ///< First working layer id after doc reset.
};
