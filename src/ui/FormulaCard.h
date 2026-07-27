#pragma once

#include <QWidget>
#include <QUuid>
#include <QList>

#include "parametric/FormulaVariable.h"

class QLineEdit;
class QLabel;
class QToolButton;
class QCheckBox;

namespace cad::ui { class CopyChip; }

/// Collapsible single-row card for one formula variable.
/// Collapsed: [▸] [Name] [= result] [条件●]  [✕]
/// Expanded:  [▾] [Name] [= result] [条件●]  [✕]
///            [expression edit] [cond row] [comment]
class FormulaCard : public QWidget
{
    Q_OBJECT

public:
    explicit FormulaCard(const cad::param::FormulaVariable& formula,
                         bool alternate = false, QWidget* parent = nullptr);

    [[nodiscard]] QUuid formulaId() const { return m_id; }
    [[nodiscard]] cad::param::FormulaVariable formula() const;

    void setResult(bool ok, double valueCm, const QString& error);
    void setConditions(const QList<cad::param::Condition>& conds, bool enabled);
    void focusName();
    void setExpanded(bool on);

signals:
    void deleteRequested(const QUuid& id);
    void edited(const cad::param::FormulaVariable& formula);
    void conditionsEditRequested(const QUuid& id);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUi(const cad::param::FormulaVariable& formula, bool alternate);
    void toggleExpanded();
    void updateCondRow();
    void onCondToggled(bool checked);

    QUuid m_id;
    bool  m_expanded = false;

    QLabel*          m_arrow = nullptr;
    cad::ui::CopyChip* m_nameChip = nullptr;
    QLabel*          m_valueLabel = nullptr;
    QLabel*          m_condDot = nullptr;   ///< Small dot indicator for conditions.
    QToolButton*     m_deleteBtn = nullptr;

    QWidget*         m_detail = nullptr;
    QLineEdit*       m_exprEdit = nullptr;
    QWidget*         m_condRow = nullptr;
    QCheckBox*       m_condCheck = nullptr;
    QLabel*          m_condInfo = nullptr;
    QToolButton*     m_condEditBtn = nullptr;
    QLineEdit*       m_commentEdit = nullptr;
    bool             m_condGuard = false;

    QList<cad::param::Condition> m_conditions;
    bool m_conditionsEnabled = true;
};
