#pragma once

#include <QWidget>
#include <QUuid>

#include "parametric/LinkedVariable.h"

class ElaLineEdit;
class ElaToolButton;
class ElaText;

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
    bool m_alternate = false;   ///< 行交替: 奇数行橙、偶数行蓝 (左侧竖线).
    QWidget* m_deleteBtnSlot = nullptr;  ///< 悬停占位: 与删除按钮同尺寸互斥显隐, 防布局跳动.
    bool m_danglingStyled = false;   ///< current value-label style state (avoids per-frame setStyleSheet)
    bool m_hasShownValue = false;   ///< value guard armed after the first refresh
    double m_lastValueMm = 0.0;     ///< last shown value (no-op guard for per-frame sync)

    cad::ui::CopyChip* m_nameChip = nullptr;
    ElaText*         m_valueLabel = nullptr;
    ElaText*         m_lockIcon = nullptr;
    ElaToolButton*     m_deleteBtn = nullptr;

    QWidget*         m_detail = nullptr;
    cad::ui::CopyChip* m_refChip = nullptr;
    ElaText*         m_sourceInfo = nullptr;
    ElaLineEdit*       m_commentEdit = nullptr;
};
