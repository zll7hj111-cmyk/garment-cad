#pragma once

#include <QWidget>
#include <QList>
#include <QUuid>

#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"

class QVBoxLayout;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QLabel;
class QPushButton;
class VariableCard;
class FormulaCard;

/// Sidebar page with two sub-tabs:
///   Tab 0 "尺寸变量": plain value variables (editable cards)
///   Tab 1 "公式变量": formula variables (expression -> computed value)
/// Formulas can reference variables (and other formulas) by name or refName.
/// Hosted inside SidePanel as a plain widget page.
class VariablePanel : public QWidget
{
    Q_OBJECT

public:
    explicit VariablePanel(QWidget* parent = nullptr);

    void setVariables(const QList<cad::param::Variable>& vars);
    void setFormulas(const QList<cad::param::FormulaVariable>& formulas);

    [[nodiscard]] const QList<cad::param::Variable>& variables() const { return m_variables; }
    [[nodiscard]] const QList<cad::param::FormulaVariable>& formulas() const { return m_formulas; }

signals:
    void variableAdded(const cad::param::Variable& var);
    void variableDeleted(const QUuid& id);
    void variableEdited(const cad::param::Variable& var);

    void formulaAdded(const cad::param::FormulaVariable& formula);
    void formulaDeleted(const QUuid& id);
    void formulaEdited(const cad::param::FormulaVariable& formula);
    void formulasRecomputed();  ///< Emitted after formula values are re-evaluated.

private:
    void setupUi();
    QWidget* buildListPage(QScrollArea*& scrollOut, QWidget*& containerOut,
                           QVBoxLayout*& layoutOut, QLabel*& emptyHintOut,
                           const QString& emptyText);

    void rebuildVariableCards();
    void rebuildFormulaCards();

    void onAddClicked();
    void addNewVariable();
    void addNewFormula();

    void onVariableDeleted(const QUuid& id);
    void onVariableEdited(const cad::param::Variable& var);
    void onFormulaDeleted(const QUuid& id);
    void onFormulaEdited(const cad::param::FormulaVariable& formula);
    void onConditionsEditRequested(const QUuid& id);

    /// Re-evaluate all formulas against current variables and update cards.
    void recomputeFormulas();

    void updateCountLabel();
    void scrollToBottom(QScrollArea* area);
    [[nodiscard]] QString nextRefName() const;

    QTabBar*        m_tabBar = nullptr;
    QStackedWidget* m_stack = nullptr;
    QPushButton*    m_addBtn = nullptr;
    QLabel*         m_countLabel = nullptr;

    // Tab 0: plain variables
    QScrollArea* m_varScroll = nullptr;
    QWidget*     m_varContainer = nullptr;
    QVBoxLayout* m_varLayout = nullptr;
    QLabel*      m_varEmptyHint = nullptr;

    // Tab 1: formulas
    QScrollArea* m_formulaScroll = nullptr;
    QWidget*     m_formulaContainer = nullptr;
    QVBoxLayout* m_formulaLayout = nullptr;
    QLabel*      m_formulaEmptyHint = nullptr;

    QList<cad::param::Variable>        m_variables;
    QList<cad::param::FormulaVariable> m_formulas;
    QList<VariableCard*>               m_varCards;
    QList<FormulaCard*>                m_formulaCards;
};
