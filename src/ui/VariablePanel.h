#pragma once

#include <QWidget>
#include <QUuid>

class QStackedWidget;
class ElaText;
class ElaPushButton;
class ElaToolButton;
class QUndoStack;

namespace cad::param { class ParamDocument; }

namespace cad::ui {
class PanelSubTabBar;
class VariableTab;
class FormulaTab;
class LinkedTab;
class MeasureTab;

/// Sidebar page with four sub-tabs (ui-redesign-2026-08 §4.2):
///   Tab 0 "变量": plain value variables (VariableTab)
///   Tab 1 "公式": formula variables (FormulaTab)
///   Tab 2 "关联": linked variables (LinkedTab, read-only)
///   Tab 3 "测量": measure variables (MeasureTab, read-only)
/// Data is owned by ParamDocument; this panel orchestrates the tabs.
class VariablePanel : public QWidget
{
    Q_OBJECT

public:
    explicit VariablePanel(cad::param::ParamDocument* doc, QWidget* parent = nullptr);
    ~VariablePanel() override = default;

    void setUndoStack(QUndoStack* stack);

    /// Rebuild theme-token driven styles after a theme change (light/dark).
    void applyTheme();

signals:
    /// Emitted when the user clicks a linked card (highlight source on canvas).
    void highlightBlockRequested(const QUuid& blockId);
    /// Emitted when the user clicks a measure card (flash the measured points).
    void highlightMeasureRequested(const QUuid& measureId);
    /// Emitted when the user hovers/clicks an angle measure card (flash the
    /// two source segments + half arc).
    void highlightAngleMeasureRequested(const QUuid& angleMeasureId);
    /// Emitted when measurement highlight should be cleared on canvas.
    void clearMeasureHighlightRequested(const QUuid& measureId = QUuid());

protected:
    void hideEvent(QHideEvent* event) override;

private:
    void setupUi();
    void onAddClicked();
    void onAddGroupClicked();
    void updateCountLabel();

    cad::param::ParamDocument* m_doc = nullptr;
    QUndoStack* m_undoStack = nullptr;

    PanelSubTabBar* m_tabBar = nullptr;
    QWidget*        m_header = nullptr;
    QStackedWidget* m_stack = nullptr;
    ElaPushButton*  m_addBtn = nullptr;
    ElaToolButton*  m_addGroupBtn = nullptr;  ///< "新建分组" (formula tab only).
    ElaText*        m_countLabel = nullptr;

    VariableTab* m_varTab = nullptr;
    FormulaTab*  m_formulaTab = nullptr;
    LinkedTab*   m_linkedTab = nullptr;
    MeasureTab*  m_measureTab = nullptr;
};

} // namespace cad::ui
