#pragma once

#include <QDockWidget>

class QTabBar;
class QStackedWidget;
class VariablePanel;
class GroupPanel;

namespace cad::param { class ParamDocument; }
class CanvasScene;

/// Right-side dock combining the variable panel and the group-structure panel
/// behind a single top-level tab bar (变量 / 组结构).
class SidePanel : public QDockWidget
{
    Q_OBJECT

public:
    SidePanel(cad::param::ParamDocument* paramDoc, CanvasScene* scene,
              QWidget* parent = nullptr);

    [[nodiscard]] VariablePanel* variablePanel() const { return m_variablePanel; }
    [[nodiscard]] GroupPanel*    groupPanel() const { return m_groupPanel; }

private:
    QTabBar*        m_tabBar = nullptr;
    QStackedWidget* m_stack = nullptr;
    VariablePanel*  m_variablePanel = nullptr;
    GroupPanel*     m_groupPanel = nullptr;
};
