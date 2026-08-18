#pragma once

#include <QWidget>
#include <QUuid>

#include "parametric/MeasureVariable.h"

class ElaLineEdit;
class ElaToolButton;
class ElaText;

namespace cad::ui { class CopyChip; }

/// Card widget for a MeasureVariable (two-point distance measurement).
/// Layout: [Name] [Value (read-only)] [🔒] [✕]
///         [refName chip (copy-only)] [source points (read-only)] [comment]
class MeasureCard : public QWidget
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

    /// Set the view-row ordinal shown in the header ("测 N", N >= 1).
    /// Pure presentation — cards are virtualized and reused, so the panel
    /// re-applies this on every (re)bind.
    void setIndex(int n);

    /// Set the alternating row parity (odd = orange bar, even = blue).
    /// Re-applied on every (re)bind — reused cards must not keep a stale
    /// parity from their previous row position.
    void setAlternate(bool alternate);

signals:
    void deleteRequested(const QUuid& id);
    void edited(const cad::param::MeasureVariable& mv);
    /// Emitted when the user clicks the card (payload: this measure's id).
    void sourceClicked(const QUuid& measureId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void setupUi(const cad::param::MeasureVariable& mv,
                 const QString& sourceLabel, bool alternate);

    QUuid m_id;
    QString m_refName;
    bool m_alternate = false;   ///< 行交替: 奇数行橙、偶数行蓝 (左侧竖线).
    QWidget* m_deleteBtnSlot = nullptr;  ///< 悬停占位: 与删除按钮同尺寸互斥显隐, 防布局跳动.
    bool m_danglingStyled = false;   ///< current value-label style state (avoids per-frame setStyleSheet)
    bool m_hasShownValue = false;   ///< value guard armed after the first refresh
    double m_lastValueMm = 0.0;     ///< last shown value (no-op guard for per-frame sync)
    cad::param::MeasureKind m_kind = cad::param::MeasureKind::Distance;      ///< 测量模式 (值前缀)
    cad::param::MeasureKind m_lastShownKind = cad::param::MeasureKind::Distance;  ///< guard 的一部分 (虚拟化跨行复用)

    cad::ui::CopyChip* m_nameChip = nullptr;
    ElaText*         m_indexLabel = nullptr;
    ElaText*         m_valueLabel = nullptr;
    ElaText*         m_lockIcon = nullptr;
    ElaToolButton*     m_deleteBtn = nullptr;

    QWidget*         m_detail = nullptr;
    cad::ui::CopyChip* m_refChip = nullptr;
    ElaText*         m_sourceInfo = nullptr;
    ElaLineEdit*       m_commentEdit = nullptr;
};
