#include "VariableTab.h"

#include <QSet>
#include <QTimer>
#include <QScrollBar>
#include <QUndoStack>

#include "VariableCard.h"
#include "VirtualCardList.h"
#include "ElaScrollArea.h"
#include "parametric/ParamDocument.h"
#include "document/commands/VariableCommands.h"

namespace cad::ui {

VariableTab::VariableTab(cad::param::ParamDocument* doc, QWidget* parent)
    : CardTabBase(doc, parent)
{
    setupListPage(
        QStringLiteral("从第一项规格开始"),
        QStringLiteral("录入人体胸围、腰围等基础围度，作为全衣片参数化推导的基准。\n点击下方按钮或右上角「＋ 添加」快速创建"),
        QStringLiteral("＋ 新建规格变量"),
        [this]() { addNewVariable(); });

    setupCardProviders();
    sync();
}

void VariableTab::setupCardProviders()
{
    m_host->setProviders(
        [this](int row) -> QWidget* {
            const auto& vars = m_doc->variables();
            if (row < 0 || row >= static_cast<int>(vars.size()))
                return nullptr;
            auto* card = new VariableCard(vars[row], row % 2 == 1, m_host);
            card->setIndex(row + 1);
            connect(card, &VariableCard::deleteRequested,
                    this, &VariableTab::onVariableDeleted);
            connect(card, &VariableCard::edited,
                    this, &VariableTab::onVariableEdited);
            return card;
        },
        [this](int row, QWidget* w) {
            const auto& vars = m_doc->variables();
            if (row < 0 || row >= static_cast<int>(vars.size()))
                return;
            auto* card = static_cast<VariableCard*>(w);
            card->syncFromModel(vars[row]);
            card->setIndex(row + 1);
            card->setAlternate(row % 2 == 1);  // 缓存复用: 奇偶随新行号重设
        });
}

QString VariableTab::nextRefName() const
{
    // Reference names are uppercase by convention; compare case-insensitively
    // so a legacy lowercase name (e.g. "v1") still blocks "V1".
    QSet<QString> used;
    for (const auto& v : m_doc->variables())
        used.insert(v.refName.toUpper());

    int n = 1;
    QString candidate;
    do {
        candidate = QStringLiteral("V%1").arg(n++);
    } while (used.contains(candidate));
    return candidate;
}

void VariableTab::addNewVariable()
{
    cad::param::Variable v;
    v.refName.clear(); // 新建变量代码默认为空
    v.name = QStringLiteral("新变量");
    v.value = 0.0;

    if (m_undoStack)
        m_undoStack->push(new cad::cmd::AddVariableCommand(m_doc, v));
    else
        m_doc->addVariable(v);

    // The new card may sit below the window: materialize it, scroll down,
    // then focus its ref editor.
    const QUuid id = v.id;
    QTimer::singleShot(0, this, [this, id]() {
        m_host->ensureMaterialized(id);
        auto* bar = m_scroll->verticalScrollBar();
        bar->setValue(bar->maximum());
        if (auto* card = qobject_cast<VariableCard*>(m_host->widgetFor(id)))
            card->focusRef();
    });
}

void VariableTab::onVariableDeleted(const QUuid& id)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveVariableCommand(m_doc, id));
    else
        m_doc->removeVariable(id);
}

void VariableTab::onVariableEdited(const cad::param::Variable& var)
{
    // Skip no-op edits (e.g. spinbox commit without actual change).
    if (const auto* cur = m_doc->variablesView().byId(var.id)) {
        if (cur->name == var.name && cur->refName == var.refName
            && qFuzzyIsNull(cur->value - var.value) && cur->comment == var.comment)
            return;
    }
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::SetVariableCommand(m_doc, var));
    else
        m_doc->updateVariable(var);
}

void VariableTab::sync()
{
    const auto& vars = m_doc->variables();
    QVector<QUuid> keys;
    keys.reserve(static_cast<int>(vars.size()));
    for (const auto& v : vars)
        keys.append(v.id);

    // setRows(): structural change → rebuild the window; identical key list
    // → in-place rebind only (preserves focus & scroll position).
    m_host->setRows(keys);
    setEmptyHintVisible(m_doc->variables().empty());
}

} // namespace cad::ui
