#pragma once

#include <QWidget>
#include <QUuid>

#include "parametric/AngleMeasureVariable.h"

class QLineEdit;
class QToolButton;
class QLabel;

namespace cad::ui { class CopyChip; }

/// Card widget for an AngleMeasureVariable (two-segment relative angle).
/// Layout: [Name] [Value (read-only, degrees)] [🔒] [✕]
///         [refName chip (copy-only)] [source segments (read-only)] [comment]
class AngleMeasureCard : public QWidget
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

    /// Set the view-row ordinal shown in the header ("角 N", N >= 1).
    /// Pure presentation — cards are virtualized and reused, so the panel
    /// re-applies this on every (re)bind.
    void setIndex(int n);

signals:
    void deleteRequested(const QUuid& id);
    void edited(const cad::param::AngleMeasureVariable& am);
    /// Emitted when the user clicks the card (to highlight the reference line A).
    void sourceClicked(const QUuid& blockId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void setupUi(const cad::param::AngleMeasureVariable& am,
                 const QString& sourceLabel, bool alternate);

    QUuid m_id;
    QUuid m_sourceBlockId;   ///< Block to highlight on click (reference line A).
    QString m_refName;
    bool m_danglingStyled = false;   ///< current value-label style state (avoids per-frame setStyleSheet)
    bool m_hasShownValue = false;   ///< value guard armed after the first refresh
    double m_lastValueDeg = 0.0;    ///< last shown value (no-op guard for per-frame sync)

    cad::ui::CopyChip* m_nameChip = nullptr;
    QLabel*          m_indexLabel = nullptr;
    QLabel*          m_valueLabel = nullptr;
    QLabel*          m_lockIcon = nullptr;
    QToolButton*     m_deleteBtn = nullptr;

    QWidget*         m_detail = nullptr;
    cad::ui::CopyChip* m_refChip = nullptr;
    QLabel*          m_sourceInfo = nullptr;
    QLineEdit*       m_commentEdit = nullptr;
};
