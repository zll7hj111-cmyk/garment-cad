#include "LinkedTab.h"

#include <QUndoStack>

#include "LinkedCard.h"
#include "VirtualCardList.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/PerfProbe.h"
#include "document/commands/VariableCommands.h"

namespace cad::ui {

namespace {

/// Build a "serial·name" source label from a linked variable's source segment.
QString linkedSourceLabel(const cad::param::ParamDocument* doc,
                          const cad::param::LinkedVariable& lv)
{
    const auto* blk = doc->findBlock(lv.sourceBlockId);
    const auto* seg = blk ? blk->findSegment(lv.sourceSegmentId) : nullptr;
    if (!seg)
        return QStringLiteral("(未知来源)");
    QString s = seg->serial;
    if (!seg->name.isEmpty())
        s += QStringLiteral("·") + seg->name;
    return s;
}

} // namespace

LinkedTab::LinkedTab(cad::param::ParamDocument* doc, QWidget* parent)
    : CardTabBase(doc, parent)
{
    setupListPage(
        QStringLiteral("关联画布线段"),
        QStringLiteral("将画布线段的实际长度发布为动态参数。\n右键点击画布线段 →「发布长度参数」，或在属性对话框中发布"),
        QString(), {});

    setupCardProviders();
    sync();
}

void LinkedTab::setupCardProviders()
{
    m_host->setProviders(
        [this](int row) -> QWidget* {
            const auto& linked = m_doc->linkedVars();
            if (row < 0 || row >= static_cast<int>(linked.size()))
                return nullptr;
            auto* card = new LinkedCard(linked[row],
                                        linkedSourceLabel(m_doc, linked[row]),
                                        row % 2 == 1, m_host);
            card->setIndex(row + 1);
            connect(card, &LinkedCard::deleteRequested,
                    this, &LinkedTab::onLinkedDeleted);
            connect(card, &LinkedCard::edited,
                    this, &LinkedTab::onLinkedEdited);
            connect(card, &LinkedCard::sourceClicked,
                    this, &LinkedTab::highlightBlockRequested);
            return card;
        },
        [this](int row, QWidget* w) {
            const auto& linked = m_doc->linkedVars();
            if (row < 0 || row >= static_cast<int>(linked.size()))
                return;
            auto* card = static_cast<LinkedCard*>(w);
            card->syncFromModel(
                linked[row], linkedSourceLabel(m_doc, linked[row]));
            card->setIndex(row + 1);
            card->setAlternate(row % 2 == 1);  // 缓存复用: 奇偶随新行号重设
        });

    // 2026-09 性能: resolved 每帧触发 syncLinkedCards → setRows(同 keys) →
    // 值级刷新路径 (只有 value label), 不再整卡 rebind。
    m_host->setValueBinder([this](int row, QWidget* w) {
        const auto& linked = m_doc->linkedVars();
        if (row < 0 || row >= static_cast<int>(linked.size()))
            return;
        auto* card = static_cast<LinkedCard*>(w);
        card->refreshValue(linked[row].value, linked[row].dangling);
    });
}

void LinkedTab::onLinkedDeleted(const QUuid& id)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveLinkedCommand(m_doc, id));
    else
        m_doc->removeLinked(id);
}

void LinkedTab::onLinkedEdited(const cad::param::LinkedVariable& lv)
{
    // Skip no-op edits.
    if (const auto* cur = m_doc->findLinked(lv.id)) {
        if (cur->name == lv.name && cur->comment == lv.comment)
            return;
    }
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::SetLinkedCommand(m_doc, lv));
    else
        m_doc->updateLinked(lv);
}

void LinkedTab::sync()
{
    GCAD_PERF_SCOPE("ui.syncLinked");
    const auto& linked = m_doc->linkedVars();
    QVector<QUuid> keys;
    keys.reserve(static_cast<int>(linked.size()));
    for (const auto& lv : linked)
        keys.append(lv.id);
    m_host->setRows(keys);
    setEmptyHintVisible(m_doc->linkedVars().empty());
}

} // namespace cad::ui
