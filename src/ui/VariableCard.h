#pragma once

#include <QWidget>
#include <QUuid>

#include "parametric/Variable.h"

class ElaLineEdit;
class QDoubleSpinBox;
class ElaToolButton;
class ElaText;

namespace cad::ui { class CopyChip; }

/// Single-row card for one variable (always expanded).
/// Layout: [Name] [Value]        [✕]
///         [refName chip] [value spin] [comment edit]
class VariableCard : public QWidget
{
    Q_OBJECT

public:
    explicit VariableCard(const cad::param::Variable& var, bool alternate = false,
                          QWidget* parent = nullptr);

    [[nodiscard]] QUuid variableId() const { return m_id; }
    [[nodiscard]] cad::param::Variable variable() const;

    /// Update displayed fields from the model without emitting signals.
    /// Skips the value spin when it has keyboard focus (user is typing).
    void syncFromModel(const cad::param::Variable& var);

    void focusName();

signals:
    void deleteRequested(const QUuid& id);
    void edited(const cad::param::Variable& var);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void setupUi(const cad::param::Variable& var, bool alternate);
    void updateValueLabel();

    QUuid m_id;

    cad::ui::CopyChip* m_nameChip = nullptr;
    ElaText*         m_valueLabel = nullptr;
    ElaToolButton*     m_deleteBtn = nullptr;

    QWidget*         m_detail = nullptr;
    cad::ui::CopyChip* m_refChip = nullptr;
    QDoubleSpinBox*  m_valueSpin = nullptr;
    ElaLineEdit*       m_commentEdit = nullptr;
};
