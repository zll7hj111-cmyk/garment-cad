#pragma once

#include <QWidget>
#include <QUuid>
#include <QList>

#include "parametric/FormulaVariable.h"

class QLineEdit;
class QLabel;
class QToolButton;
class QCheckBox;

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

    /// MIME type carried by a card drag (payload = formula id string).
    static constexpr const char* kDragMimeType = "application/x-garmentcad-formula";

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

    QLabel*          m_indexLabel = nullptr;  ///< Ordinal + drag handle.
    QPoint           m_dragStartPos;
    cad::ui::CopyChip* m_nameChip = nullptr;
    QLabel*          m_valueLabel = nullptr;
    QLabel*          m_condDot = nullptr;   ///< Small dot indicator for conditions.
    QToolButton*     m_deleteBtn = nullptr;

    QWidget*         m_detail = nullptr;
    QLineEdit*       m_exprEdit = nullptr;
    QLineEdit*       m_actualEdit = nullptr; ///< Actual value override (cm).
    QWidget*         m_condRow = nullptr;
    QCheckBox*       m_condCheck = nullptr;
    QLabel*          m_condInfo = nullptr;
    QToolButton*     m_condEditBtn = nullptr;
    QLineEdit*       m_commentEdit = nullptr;
    bool             m_condGuard = false;

    QList<cad::param::Condition> m_conditions;
    bool m_conditionsEnabled = true;
};
