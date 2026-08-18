#pragma once

#include <QWidget>
#include <QUuid>
#include <QList>

#include "parametric/FormulaVariable.h"

class ElaLineEdit;
class ElaText;
class ElaToolButton;
class ElaCheckBox;
class QVBoxLayout;

namespace cad::ui { class CopyChip; }

/// Single-row card for one formula variable (always expanded).
/// Layout: [#] [Name] [= result] [条件●]  [✕]
///         [expression edit]
///         [cond row] [comment]
/// The leading index label doubles as the drag handle for reordering.
class FormulaCard : public QWidget
{
    Q_OBJECT

public:
    explicit FormulaCard(const cad::param::FormulaVariable& formula,
                         bool alternate = false, QWidget* parent = nullptr);

    [[nodiscard]] QUuid formulaId() const { return m_id; }
    [[nodiscard]] QUuid groupId() const { return m_groupId; }
    [[nodiscard]] cad::param::FormulaVariable formula() const;

    void setResult(bool ok, double valueCm, const QString& error);
    void setConditions(const QList<cad::param::Condition>& conds, bool enabled);

    /// Group-local ordinal shown in the drag-handle area (1-based).
    void setIndex(int index);

    /// Indent the card to mark it as a group member (内容+左侧竖线右移
    /// kGroupIndent，与组内公式区分于未分组公式).
    void setGrouped(bool grouped);

    /// Set the alternating row parity (odd = orange bar, even = blue).
    /// Re-applied on every (re)bind — reused cards must not keep a stale
    /// parity from their previous row position.
    void setAlternate(bool alternate);

    /// MIME type carried by a card drag (payload = formula id string).
    static constexpr const char* kDragMimeType = "application/x-garmentcad-formula";

    /// Horizontal indent (px) applied to group-member cards.
    static constexpr int kGroupIndent = 16;

    /// Update displayed fields from the model without emitting signals.
    /// Skips text editors that have keyboard focus (user is typing).
    void syncFromModel(const cad::param::FormulaVariable& f);

    void focusName();

signals:
    void deleteRequested(const QUuid& id);
    void edited(const cad::param::FormulaVariable& formula);
    void conditionsEditRequested(const QUuid& id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUi(const cad::param::FormulaVariable& formula, bool alternate);
    void updateCondRow();
    void updateExprEnabled();
    void onCondToggled(bool checked);

    QUuid m_id;
    QUuid m_groupId;  ///< Mirrors the model's group membership.
    bool m_alternate = false;   ///< 行交替: 奇数行橙、偶数行蓝 (左侧竖线).
    bool m_grouped = false;     ///< 组内成员: 内容+竖线右移 kGroupIndent.

    ElaText*         m_indexLabel = nullptr;  ///< Ordinal + drag handle.
    QPoint           m_dragStartPos;
    cad::ui::CopyChip* m_nameChip = nullptr;
    ElaText*         m_valueLabel = nullptr;
    ElaText*         m_condDot = nullptr;   ///< Small dot indicator for conditions.
    ElaToolButton*     m_deleteBtn = nullptr;
    QWidget*         m_deleteBtnSlot = nullptr;  ///< 悬停占位: 与删除按钮同尺寸互斥显隐, 防布局跳动.

    QWidget*         m_detail = nullptr;
    QVBoxLayout*     m_mainLayout = nullptr;
    ElaLineEdit*       m_exprEdit = nullptr;
    ElaLineEdit*       m_actualEdit = nullptr; ///< Actual value override (cm).
    QWidget*         m_condRow = nullptr;
    ElaCheckBox*       m_condCheck = nullptr;
    ElaText*         m_condInfo = nullptr;
    ElaToolButton*     m_condEditBtn = nullptr;
    ElaLineEdit*       m_commentEdit = nullptr;
    bool             m_condGuard = false;

    QList<cad::param::Condition> m_conditions;
    bool m_conditionsEnabled = true;
};
