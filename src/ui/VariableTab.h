#pragma once

#include "CardTabBase.h"
#include <QUuid>

namespace cad::param {
struct Variable;
}

namespace cad::ui {

/// "变量" page of VariablePanel: plain value variables (editable cards).
class VariableTab : public CardTabBase
{
    Q_OBJECT

public:
    explicit VariableTab(cad::param::ParamDocument* doc, QWidget* parent = nullptr);
    ~VariableTab() override = default;

    void sync();
    void addNewVariable();
    [[nodiscard]] QString nextRefName() const;

private:
    void setupCardProviders();
    void onVariableDeleted(const QUuid& id);
    void onVariableEdited(const cad::param::Variable& var);
};

} // namespace cad::ui
