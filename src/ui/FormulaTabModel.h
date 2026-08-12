#ifndef GCAD_FORMULA_TAB_MODEL_H
#define GCAD_FORMULA_TAB_MODEL_H

#include <QUuid>
#include <QVector>

class QUndoStack;

namespace cad::param {
class ParamDocument;
}

namespace cad::ui {

/// Display-order model of the formula tab: ungrouped section first, then per
/// group header + members (members of collapsed groups get no rows — the
/// virtualized list makes hiding them free). Also encapsulates the group /
/// move operations with their undo wrappers (same restore-then-replay rule
/// as the card editors).
class FormulaTabModel
{
public:
    /// One display row of the formula tab (card or group header).
    struct Row {
        bool  isHeader = false;
        QUuid id;         ///< Formula id (card) or group id (header).
        QUuid groupId;    ///< Owning group (cards only).
        int   localIndex = 0;  ///< Group-local ordinal (cards only).
    };

    explicit FormulaTabModel(cad::param::ParamDocument* doc);

    /// Rebuild the row descriptors from the document's formulas + groups.
    void rebuild();
    [[nodiscard]] const QVector<Row>& rows() const { return m_rows; }
    /// Display keys for the virtualized host (header ids + formula ids).
    [[nodiscard]] QVector<QUuid> keys() const;

    /// Collapse/expand toggle (view state, persisted; not undoable).
    void toggleCollapsed(const QUuid& groupId);
    /// Rename a group (undoable via RenameFormulaGroupCommand).
    void rename(const QUuid& groupId, const QString& newName);
    /// Dissolve a group (undoable via RemoveFormulaGroupCommand).
    void dissolve(const QUuid& groupId);
    /// Move a formula to targetGroupId (null = ungrouped) at the given local
    /// index (undoable via MoveFormulaCommand; drop slots are pre-removal so
    /// the same-group case is corrected here).
    void moveFormula(const QUuid& formulaId, const QUuid& targetGroupId,
                     int targetLocalIndex);
    /// Number of formulas currently in @p groupId (append target for a drop
    /// on the group header).
    [[nodiscard]] int formulaCountIn(const QUuid& groupId) const;

    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }

private:
    cad::param::ParamDocument* m_doc = nullptr;
    QUndoStack* m_undoStack = nullptr;

    QVector<Row> m_rows;   ///< Display-order row descriptors (cards + headers).
};

} // namespace cad::ui

#endif // GCAD_FORMULA_TAB_MODEL_H
