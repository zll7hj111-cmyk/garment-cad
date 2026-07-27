#pragma once

#include <QWidget>
#include <QUuid>

#include "parametric/Variable.h"

class QLineEdit;
class QDoubleSpinBox;
class QToolButton;
class QLabel;

namespace cad::ui { class CopyChip; }

/// Collapsible single-row card for one variable.
/// Collapsed: [▸] [Name] [Value]        [✕]
/// Expanded:  [▾] [Name] [Value]        [✕]
///            [refName chip] [comment edit]
class VariableCard : public QWidget
{
    Q_OBJECT

public:
    explicit VariableCard(const cad::param::Variable& var, bool alternate = false,
                          QWidget* parent = nullptr);

    [[nodiscard]] QUuid variableId() const { return m_id; }
    [[nodiscard]] cad::param::Variable variable() const;

    void focusName();
    void setExpanded(bool on);

signals:
    void deleteRequested(const QUuid& id);
    void edited(const cad::param::Variable& var);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUi(const cad::param::Variable& var, bool alternate);
    void toggleExpanded();
    void updateValueLabel();

    QUuid m_id;
    bool  m_expanded = false;

    QLabel*          m_arrow = nullptr;
    cad::ui::CopyChip* m_nameChip = nullptr;
    QLabel*          m_valueLabel = nullptr;
    QToolButton*     m_deleteBtn = nullptr;

    QWidget*         m_detail = nullptr;
    cad::ui::CopyChip* m_refChip = nullptr;
    QDoubleSpinBox*  m_valueSpin = nullptr;
    QLineEdit*       m_commentEdit = nullptr;
};
