#pragma once

#include <QWidget>
#include <QUuid>

#include "parametric/MeasureVariable.h"
#include "CardBase.h"

class ElaLineEdit;
class ElaText;

namespace cad::ui { class CopyChip; }

/// Card widget for a MeasureVariable (two-point distance measurement).
/// Layout: [Name] [Value (read-only)] [🔒] [✕]
///         [refName chip (copy-only)] [source points (read-only)] [comment]
class MeasureCard : public CardBase
{
    Q_OBJECT

public:
    explicit MeasureCard(const cad::param::MeasureVariable& mv,
                         const QString& sourceLabel,
                         bool alternate = false,
                         QWidget* parent = nullptr);

    [[nodiscard]] QUuid measureId() const { return m_id; }
    [[nodiscard]] cad::param::MeasureVariable measureVar() const;

    /// Refresh the displayed value (called after resolve).
    void refreshValue(double valueMm, bool dangling);

    /// Update displayed fields from the model without emitting signals.
    void syncFromModel(const cad::param::MeasureVariable& mv, const QString& sourceLabel);

signals:
    void deleteRequested(const QUuid& id);
    void edited(const cad::param::MeasureVariable& mv);
    /// Emitted when the user hovers (or clicks) the card (payload: this
    /// measure's id). Hover is the primary trigger; the click path is kept as a
    /// fallback for input methods without hover.
    void sourceClicked(const QUuid& measureId);

protected:
    /// "测 N" prefix for the row ordinal.
    QString indexText(int n) const override;
    void enterEvent(QEnterEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void setupUi(const cad::param::MeasureVariable& mv, const QString& sourceLabel);

    QUuid m_id;
    QString m_refName;
    cad::param::MeasureKind m_kind = cad::param::MeasureKind::Distance;      ///< 测量模式 (值前缀)
    cad::param::MeasureKind m_lastShownKind = cad::param::MeasureKind::Distance;  ///< guard 的一部分 (虚拟化跨行复用)
};
