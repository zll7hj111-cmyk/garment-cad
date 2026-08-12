#pragma once

#include "ElaDialog.h"
#include "ElaText.h"
#include <QList>
#include <QHash>

#include "parametric/Condition.h"

class QVBoxLayout;
class ElaComboBox;
class ElaCheckBox;
class ElaDoubleSpinBox;
class ElaPushButton;
class ElaText;

/// Modal editor for the conditional adjustments attached to one formula.
///
/// The watched-variable combo only offers the identifiers actually referenced
/// by the formula expression (intersected with known variables), enforcing the
/// rule that a condition must target a variable the formula uses.
class ConditionDialog : public ElaDialog
{
    Q_OBJECT

public:
    ConditionDialog(const QString& formulaName,
                    const QString& expression,
                    const QList<cad::param::Condition>& conditions,
                    const QHash<QString, double>& knownVars,
                    QWidget* parent = nullptr);

    [[nodiscard]] QList<cad::param::Condition> conditions() const { return m_result; }

private:
    struct Row {
        cad::param::Condition cond;   ///< Source condition (keeps id).
        QWidget*        widget = nullptr;
        ElaComboBox*      watch  = nullptr;
        ElaCheckBox*      lowerOn = nullptr;
        ElaDoubleSpinBox* lower  = nullptr;
        ElaCheckBox*      upperOn = nullptr;
        ElaDoubleSpinBox* upper  = nullptr;
        ElaComboBox*      mode   = nullptr;
        ElaDoubleSpinBox* step   = nullptr;
        ElaText*        stepLbl = nullptr;
        ElaDoubleSpinBox* amount = nullptr;
        ElaPushButton*    remove = nullptr;
    };

    void setupUi(const QString& formulaName, const QString& expression);
    QStringList availableVars(const QString& expression) const;
    Row* buildRow(const cad::param::Condition& cond);
    void addRow(const cad::param::Condition& cond);
    void removeRow(Row* row);
    void syncMode(Row* row);
    void collectAndAccept();

    QHash<QString, double> m_knownVars;
    QStringList m_vars;
    QVBoxLayout* m_rowsLayout = nullptr;
    ElaText* m_emptyHint = nullptr;
    QList<Row*> m_rows;
    QList<cad::param::Condition> m_result;
};
