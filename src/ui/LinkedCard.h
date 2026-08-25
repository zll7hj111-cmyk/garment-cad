#pragma once

#include <QWidget>
#include <QUuid>

#include "parametric/LinkedVariable.h"
#include "CardBase.h"

class ElaLineEdit;
class ElaText;

namespace cad::ui { class CopyChip; }

/// Card widget for a LinkedVariable (geometric measurement parameter).
/// Layout: [Name] [Value (read-only)] [🔒] [✕]
///         [refName chip (copy-only)] [source info (read-only)] [comment]
class LinkedCard : public CardBase
{
    Q_OBJECT

public:
    explicit LinkedCard(const cad::param::LinkedVariable& lv,
                        const QString& sourceLabel,
                        bool alternate = false,
                        QWidget* parent = nullptr);

    [[nodiscard]] QUuid linkedId() const { return m_id; }
    [[nodiscard]] cad::param::LinkedVariable linkedVar() const;

    /// Refresh the displayed value (called after resolve).
    void refreshValue(double valueMm, bool dangling);

    /// Update displayed fields from the model without emitting signals.
    void syncFromModel(const cad::param::LinkedVariable& lv, const QString& sourceLabel);

signals:
    void deleteRequested(const QUuid& id);
    void edited(const cad::param::LinkedVariable& lv);
    /// Emitted when the user clicks the card (to highlight the source on canvas).
    void sourceClicked(const QUuid& blockId);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void setupUi(const cad::param::LinkedVariable& lv, const QString& sourceLabel);

    QUuid m_id;
    QUuid m_sourceBlockId;
    QString m_refName;
    QString m_sourceLabel;
};
