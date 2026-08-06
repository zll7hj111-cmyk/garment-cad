#pragma once

#include <QWidget>
#include <QUuid>

#include "parametric/LinkedVariable.h"

class QLineEdit;
class QToolButton;
class QLabel;

namespace cad::ui { class CopyChip; }

/// Card widget for a LinkedVariable (geometric measurement parameter).
/// Layout: [Name] [Value (read-only)] [🔒] [✕]
///         [refName chip (copy-only)] [source info (read-only)] [comment]
class LinkedCard : public QWidget
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
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void setupUi(const cad::param::LinkedVariable& lv,
                 const QString& sourceLabel, bool alternate);

    QUuid m_id;
    QUuid m_sourceBlockId;
    QString m_refName;
    QString m_sourceLabel;
    bool m_danglingStyled = false;   ///< current value-label style state (avoids per-frame setStyleSheet)
    bool m_hasShownValue = false;   ///< value guard armed after the first refresh
    double m_lastValueMm = 0.0;     ///< last shown value (no-op guard for per-frame sync)

    cad::ui::CopyChip* m_nameChip = nullptr;
    QLabel*          m_valueLabel = nullptr;
    QLabel*          m_lockIcon = nullptr;
    QToolButton*     m_deleteBtn = nullptr;

    QWidget*         m_detail = nullptr;
    cad::ui::CopyChip* m_refChip = nullptr;
    QLabel*          m_sourceInfo = nullptr;
    QLineEdit*       m_commentEdit = nullptr;
};
