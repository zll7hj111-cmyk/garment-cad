#pragma once

#include <QWidget>
#include <QUuid>

#include "parametric/Variable.h"
#include "CardBase.h"

class ElaLineEdit;
class QDoubleSpinBox;
class ElaText;

namespace cad::ui { class CompoundChip; }

/// Streamlined single-row card for one plain variable (~32px height).
/// Layout: [varIndex] [CompoundChip: refName | name] [value spin] [cm] [comment edit] [✕]
class VariableCard : public CardBase
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

private:
    void setupUi(const cad::param::Variable& var);

    QUuid m_id;

    cad::ui::CompoundChip* m_compoundChip = nullptr;
    QDoubleSpinBox*        m_valueSpin = nullptr;
};
