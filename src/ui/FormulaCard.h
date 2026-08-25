#pragma once

#include <QWidget>
#include <QUuid>
#include <QList>

#include "parametric/FormulaVariable.h"
#include "CardBase.h"

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
class FormulaCard : public CardBase
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

    /// Indent the card to mark it as a group member (内容+左侧竖线右移
    /// kGroupIndent，与组内公式区分于未分组公式).
    void setGrouped(bool grouped);

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
    /// Group-member cards shift the accent bar right by kGroupIndent.
    int accentBarX() const override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupUi(const cad::param::FormulaVariable& formula);
    void updateCondRow();
    void updateExprEnabled();
    void onCondToggled(bool checked);

    QUuid m_id;
    QUuid m_groupId;  ///< Mirrors the model's group membership.
    bool  m_grouped = false;     ///< 组内成员: 内容+竖线右移 kGroupIndent.

    QPoint           m_dragStartPos;
    ElaText*         m_condDot = nullptr;   ///< Small dot indicator for conditions.
    QVBoxLayout*     m_mainLayout = nullptr;
    ElaLineEdit*     m_exprEdit = nullptr;
    ElaLineEdit*     m_actualEdit = nullptr; ///< Actual value override (cm).
    QWidget*         m_condRow = nullptr;
    ElaCheckBox*     m_condCheck = nullptr;
    ElaText*         m_condInfo = nullptr;
    ElaToolButton*   m_condEditBtn = nullptr;
    bool             m_condGuard = false;

    QList<cad::param::Condition> m_conditions;
    bool m_conditionsEnabled = true;
};
