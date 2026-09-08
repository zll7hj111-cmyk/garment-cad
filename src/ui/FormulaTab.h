#pragma once

#include "CardTabBase.h"
#include <QUuid>

class QFrame;

namespace cad::param {
struct FormulaVariable;
}

namespace cad::ui {

class FormulaTabModel;

/// "公式" page of VariablePanel: formula variables (expression -> computed value)
/// with group headers and drag-to-reorder/group support.
class FormulaTab : public CardTabBase
{
    Q_OBJECT

public:
    explicit FormulaTab(cad::param::ParamDocument* doc, QWidget* parent = nullptr);
    ~FormulaTab() override;

    void setUndoStack(QUndoStack* stack) override;
    void sync();
    void addNewFormula();
    void addGroup();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupCardProviders();

    // Group interactions
    void onGroupToggled(const QUuid& groupId);
    void onGroupRenamed(const QUuid& groupId, const QString& newName);
    void onGroupDissolved(const QUuid& groupId);
    void onFormulaDroppedOnHeader(const QUuid& formulaId, const QUuid& groupId);

    // Drop-slot resolution
    void computeFormulaDropSlot(int y, QUuid& groupId, int& localIndex, int& indicatorY) const;
    void computeGroupDropSlot(int y, int& insertIndex, int& indicatorY) const;

    // Formula edit / delete / conditions
    void onFormulaDeleted(const QUuid& id);
    void onFormulaEdited(const cad::param::FormulaVariable& formula);
    void onConditionsEditRequested(const QUuid& id);

    FormulaTabModel* m_formulaModel = nullptr;
    QFrame*          m_dropIndicator = nullptr;
};

} // namespace cad::ui
