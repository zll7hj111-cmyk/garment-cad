#pragma once

#include <QDialog>
#include <QList>
#include <QHash>

#include "parametric/Condition.h"

class QVBoxLayout;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;

/// Modal editor for the conditional adjustments attached to one formula.
///
/// The watched-variable combo only offers the identifiers actually referenced
/// by the formula expression (intersected with known variables), enforcing the
/// rule that a condition must target a variable the formula uses.
class ConditionDialog : public QDialog
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
        QComboBox*      watch  = nullptr;
        QCheckBox*      lowerOn = nullptr;
        QDoubleSpinBox* lower  = nullptr;
        QCheckBox*      upperOn = nullptr;
        QDoubleSpinBox* upper  = nullptr;
        QComboBox*      mode   = nullptr;
        QDoubleSpinBox* step   = nullptr;
        QLabel*         stepLbl = nullptr;
        QDoubleSpinBox* amount = nullptr;
        QPushButton*    remove = nullptr;
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
    QLabel* m_emptyHint = nullptr;
    QList<Row*> m_rows;
    QList<cad::param::Condition> m_result;
};
