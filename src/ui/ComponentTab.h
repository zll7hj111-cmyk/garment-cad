#pragma once

#include <QWidget>

class QUndoStack;
class QVBoxLayout;
class QWidget;

namespace cad::param { class ParamDocument; }

namespace cad::ui {

/// Component (组件) management page — the 5th sub-tab of VariablePanel.
/// Lists rigid work groups with bounding-box toggle, reset-angle, dissolve and
/// delete actions. Rebuilds on ParamDocument::componentsChanged (queued so a
/// row widget is never deleted during its own signal emission).
class ComponentTab : public QWidget
{
    Q_OBJECT
public:
    explicit ComponentTab(cad::param::ParamDocument* doc, QWidget* parent = nullptr);
    ~ComponentTab() override;

    void setUndoStack(QUndoStack* stack);
    /// Rebuild the row list from the current component registry.
    void sync();
    /// Re-apply theme-token backgrounds after a light/dark switch.
    void applyTheme();

private:
    void rebuild();

    cad::param::ParamDocument* m_doc = nullptr;
    QUndoStack* m_undoStack = nullptr;
    QWidget* m_rowsContainer = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
};

} // namespace cad::ui
