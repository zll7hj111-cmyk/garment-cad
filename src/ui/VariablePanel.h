#pragma once

#include <QWidget>
#include <QList>
#include <QVector>
#include <QUuid>

#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "parametric/AngleMeasureVariable.h"

class QVBoxLayout;
class ElaScrollArea;
class QStackedWidget;
class ElaTabBar;
class ElaText;
class ElaPushButton;
class ElaToolButton;
class QFrame;
class QUndoStack;
class VirtualCardList;

namespace cad::param { class ParamDocument; }

namespace cad::ui {
class FormulaTabModel;
class MeasureTab;
class ComponentTab;

/// Sidebar page with four sub-tabs:
///   Tab 0 "变量": plain value variables (editable cards)
///   Tab 1 "公式": formula variables (expression -> computed value)
///   Tab 2 "关联": linked variables
///   Tab 3 "测量": measure variables (length + angle cards)
/// Data is owned by ParamDocument; this panel is a pure editor/view.
class VariablePanel : public QWidget
{
    Q_OBJECT

public:
    explicit VariablePanel(cad::param::ParamDocument* doc, QWidget* parent = nullptr);
    ~VariablePanel() override;

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

protected:
    /// Drag-and-drop handling for the formula list container.
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUi();
    QWidget* buildListPage(ElaScrollArea*& scrollOut, QWidget*& containerOut,
                           VirtualCardList*& hostOut, ElaText*& emptyHintOut,
                           const QString& emptyText);

    /// Wire the virtualized hosts' row factories / binders to the card types.
    void setupCardProviders();

    /// Smart sync: rebuild the row structure only when keys/order changed,
    /// otherwise rebind materialized cards in-place (preserves focus & scroll).
    void syncVariableCards();
    void syncFormulaCards();
    void syncLinkedCards();

    void onAddClicked();
    void addNewVariable();
    void addNewFormula();
    void onAddGroupClicked();

    // Formula group interactions.
    void onGroupToggled(const QUuid& groupId);
    void onGroupRenamed(const QUuid& groupId, const QString& newName);
    void onGroupDissolved(const QUuid& groupId);
    void onFormulaDroppedOnHeader(const QUuid& formulaId, const QUuid& groupId);

    // Drop-slot resolution inside the formula container (y in container coords).
    void computeFormulaDropSlot(int y, QUuid& groupId, int& localIndex,
                                int& indicatorY) const;
    void computeGroupDropSlot(int y, int& insertIndex, int& indicatorY) const;

    void onVariableDeleted(const QUuid& id);
    void onVariableEdited(const cad::param::Variable& var);
    void onFormulaDeleted(const QUuid& id);
    void onFormulaEdited(const cad::param::FormulaVariable& formula);
    void onConditionsEditRequested(const QUuid& id);
    void onLinkedDeleted(const QUuid& id);
    void onLinkedEdited(const cad::param::LinkedVariable& lv);

    void updateCountLabel();
    [[nodiscard]] QString nextRefName() const;

    cad::param::ParamDocument* m_doc = nullptr;
    QUndoStack* m_undoStack = nullptr;

    /// Formula tab display-order model (rows + group/move operations).
    cad::ui::FormulaTabModel* m_formulaModel = nullptr;

    ElaTabBar*        m_tabBar = nullptr;
    QStackedWidget* m_stack = nullptr;
    ElaPushButton*    m_addBtn = nullptr;
    ElaToolButton*    m_addGroupBtn = nullptr;  ///< "新建分组" (formula tab only).
    ElaText*        m_countLabel = nullptr;

    // Tab 0: plain variables
    ElaScrollArea* m_varScroll = nullptr;
    QWidget*     m_varContainer = nullptr;
    VirtualCardList* m_varHost = nullptr;
    ElaText*     m_varEmptyHint = nullptr;

    // Tab 1: formulas
    ElaScrollArea* m_formulaScroll = nullptr;
    QWidget*     m_formulaContainer = nullptr;
    VirtualCardList* m_formulaHost = nullptr;
    ElaText*     m_formulaEmptyHint = nullptr;
    QFrame*      m_dropIndicator = nullptr;   ///< 2px insert line during drags.

    // Tab 2: linked variables
    ElaScrollArea* m_linkedScroll = nullptr;
    QWidget*     m_linkedContainer = nullptr;
    VirtualCardList* m_linkedHost = nullptr;
    ElaText*     m_linkedEmptyHint = nullptr;

    /// Tab 3: measure variables (length + angle cards, extracted).
    MeasureTab* m_measureTab = nullptr;

    /// Tab 4: components (组件 rigid work groups).
    ComponentTab* m_componentTab = nullptr;
};

} // namespace cad::ui
