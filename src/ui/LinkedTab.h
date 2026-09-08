#pragma once

#include "CardTabBase.h"
#include <QUuid>

namespace cad::param {
struct LinkedVariable;
}

namespace cad::ui {

/// "关联" page of VariablePanel: linked variables (read-only cards).
class LinkedTab : public CardTabBase
{
    Q_OBJECT

public:
    explicit LinkedTab(cad::param::ParamDocument* doc, QWidget* parent = nullptr);
    ~LinkedTab() override = default;

    void sync();

signals:
    void highlightBlockRequested(const QUuid& blockId);

private:
    void setupCardProviders();
    void onLinkedDeleted(const QUuid& id);
    void onLinkedEdited(const cad::param::LinkedVariable& lv);
};

} // namespace cad::ui
