#pragma once

#include <QDockWidget>

class QTabBar;
class QStackedWidget;
class VariablePanel;
class LayerPanel;
class GroupPanel;

namespace cad::param { class ParamDocument; }
class CanvasScene;

/// Right-side dock combining the variable, layer and group panels behind a
/// single top-level tab bar (变量 / 图层 / 组).
class SidePanel : public QDockWidget
{
    Q_OBJECT

public:
    SidePanel(cad::param::ParamDocument* paramDoc, CanvasScene* scene,
              QWidget* parent = nullptr);

    [[nodiscard]] VariablePanel* variablePanel() const { return m_variablePanel; }
    [[nodiscard]] LayerPanel*    layerPanel() const { return m_layerPanel; }
    [[nodiscard]] GroupPanel*    groupPanel() const { return m_groupPanel; }

private:
    QTabBar*        m_tabBar = nullptr;
    QStackedWidget* m_stack = nullptr;
    VariablePanel*  m_variablePanel = nullptr;
    LayerPanel*     m_layerPanel = nullptr;
    GroupPanel*     m_groupPanel = nullptr;
};
