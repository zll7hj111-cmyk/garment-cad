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
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QLabel;
class QPushButton;
class QToolButton;
class QFrame;
class QUndoStack;
class VirtualCardList;

namespace cad::param { class ParamDocument; }

/// Sidebar page with two sub-tabs:
///   Tab 0 "尺寸变量": plain value variables (editable cards)
///   Tab 1 "公式变量": formula variables (expression -> computed value)
/// Data is owned by ParamDocument; this panel is a pure editor/view.
class VariablePanel : public QWidget
{
    Q_OBJECT

public:
    explicit VariablePanel(cad::param::ParamDocument* doc, QWidget* parent = nullptr);

    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }

signals:
    /// Emitted when the user clicks a linked card (highlight source on canvas).
    void highlightBlockRequested(const QUuid& blockId);
    /// Emitted when the user clicks a measure card (flash the measured points).
    void highlightMeasureRequested(const QUuid& measureId);

protected:
    /// Drag-and-drop handling for the formula list container.
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    /// One display row of the formula tab (card or group header).
    struct FormulaRow {
        bool  isHeader = false;
        QUuid id;         ///< Formula id (card) or group id (header).
        QUuid groupId;    ///< Owning group (cards only).
        int   localIndex = 0;  ///< Group-local ordinal (cards only).
    };

    void setupUi();
    QWidget* buildListPage(QScrollArea*& scrollOut, QWidget*& containerOut,
                           VirtualCardList*& hostOut, QLabel*& emptyHintOut,
                           const QString& emptyText);

    /// Wire the virtualized hosts' row factories / binders to the card types.
    void setupCardProviders();

    /// Smart sync: rebuild the row structure only when keys/order changed,
    /// otherwise rebind materialized cards in-place (preserves focus & scroll).
    void syncVariableCards();
    void syncFormulaCards();
    void syncLinkedCards();
    void syncMeasureCards();

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
    void moveFormulaTo(const QUuid& formulaId, const QUuid& targetGroupId,
                       int targetLocalIndex);

    void onVariableDeleted(const QUuid& id);
    void onVariableEdited(const cad::param::Variable& var);
    void onFormulaDeleted(const QUuid& id);
    void onFormulaEdited(const cad::param::FormulaVariable& formula);
    void onConditionsEditRequested(const QUuid& id);
    void onLinkedDeleted(const QUuid& id);
    void onLinkedEdited(const cad::param::LinkedVariable& lv);
    void onMeasureDeleted(const QUuid& id);
    void onMeasureEdited(const cad::param::MeasureVariable& mv);
    void onAngleMeasureDeleted(const QUuid& id);
    void onAngleMeasureEdited(const cad::param::AngleMeasureVariable& am);

    void updateCountLabel();
    [[nodiscard]] QString nextRefName() const;

    cad::param::ParamDocument* m_doc = nullptr;
    QUndoStack* m_undoStack = nullptr;

    QTabBar*        m_tabBar = nullptr;
    QStackedWidget* m_stack = nullptr;
    QPushButton*    m_addBtn = nullptr;
    QToolButton*    m_addGroupBtn = nullptr;  ///< "新建分组" (formula tab only).
    QLabel*         m_countLabel = nullptr;

    // Tab 0: plain variables
    QScrollArea* m_varScroll = nullptr;
    QWidget*     m_varContainer = nullptr;
    VirtualCardList* m_varHost = nullptr;
    QLabel*      m_varEmptyHint = nullptr;

    // Tab 1: formulas
    QScrollArea* m_formulaScroll = nullptr;
    QWidget*     m_formulaContainer = nullptr;
    VirtualCardList* m_formulaHost = nullptr;
    QLabel*      m_formulaEmptyHint = nullptr;
    QFrame*      m_dropIndicator = nullptr;   ///< 2px insert line during drags.

    // Tab 2: linked variables
    QScrollArea* m_linkedScroll = nullptr;
    QWidget*     m_linkedContainer = nullptr;
    VirtualCardList* m_linkedHost = nullptr;
    QLabel*      m_linkedEmptyHint = nullptr;

    // Tab 3: measure variables
    QScrollArea* m_measureScroll = nullptr;
    QWidget*     m_measureContainer = nullptr;
    VirtualCardList* m_measureHost = nullptr;
    QLabel*      m_measureEmptyHint = nullptr;

    /// Display-order row descriptors of the formula tab (cards + headers).
    QVector<FormulaRow> m_formulaRows;
};
