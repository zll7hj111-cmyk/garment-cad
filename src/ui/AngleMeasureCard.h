#pragma once

#include <QWidget>
#include <QUuid>

#include "parametric/AngleMeasureVariable.h"
#include "CardBase.h"

class ElaLineEdit;
class ElaText;

namespace cad::ui { class CopyChip; }

/// Card widget for an AngleMeasureVariable (two-segment relative angle).
/// Layout: [Name] [Value (read-only, degrees)] [🔒] [✕]
///         [refName chip (copy-only)] [source segments (read-only)] [comment]
class AngleMeasureCard : public CardBase
{
    Q_OBJECT

public:
    explicit AngleMeasureCard(const cad::param::AngleMeasureVariable& am,
                              const QString& sourceLabel,
                              bool alternate = false,
                              QWidget* parent = nullptr);

    [[nodiscard]] QUuid angleMeasureId() const { return m_id; }
    [[nodiscard]] cad::param::AngleMeasureVariable angleMeasureVar() const;

    /// Refresh the displayed value (called after resolve).
    void refreshValue(double valueDeg, bool dangling);

    /// Update displayed fields from the model without emitting signals.
    void syncFromModel(const cad::param::AngleMeasureVariable& am,
                       const QString& sourceLabel);

signals:
    void deleteRequested(const QUuid& id);
    void edited(const cad::param::AngleMeasureVariable& am);
    /// Emitted when the user hovers (or clicks) the card (payload: this
    /// angle measure's id). Hover is the primary trigger; the click path is
    /// kept as a fallback for input methods without hover.
    void sourceClicked(const QUuid& angleMeasureId);

protected:
    /// "角 N" prefix for the row ordinal.
    QString indexText(int n) const override;
    void enterEvent(QEnterEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void setupUi(const cad::param::AngleMeasureVariable& am, const QString& sourceLabel);

    QUuid m_id;
    QString m_refName;
};
