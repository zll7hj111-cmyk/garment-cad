#include "FormulaTab.h"

#include <QFrame>
#include <QRect>
#include <QTimer>
#include <QHash>
#include <QUndoStack>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

#include "FormulaCard.h"
#include "FormulaGroupHeader.h"
#include "FormulaTabModel.h"
#include "VirtualCardList.h"
#include "ElaScrollArea.h"
#include "ConditionDialog.h"
#include "parametric/ParamDocument.h"
#include "geometry/Units.h"
#include "document/commands/VariableCommands.h"

namespace cad::ui {

namespace {

int formulaGroupMemberCount(const cad::param::ParamDocument* doc, const QUuid& groupId)
{
    int count = 0;
    for (const auto& f : doc->formulas())
        if (f.groupId == groupId)
            ++count;
    return count;
}

} // namespace

FormulaTab::FormulaTab(cad::param::ParamDocument* doc, QWidget* parent)
    : CardTabBase(doc, parent)
    , m_formulaModel(new cad::ui::FormulaTabModel(doc))
{
    setupListPage(
        QStringLiteral("建立推导公式"),
        QStringLiteral("支持四则运算与几何函数，例如：胸围/2 + 6 或 b/2 + 6。\n点击下方按钮新建第一条推导公式"),
        QStringLiteral("＋ 新建公式变量"),
        [this]() { addNewFormula(); });

    // Formula page: accept card/header drops for reordering & grouping.
    m_container->setAcceptDrops(true);
    m_container->installEventFilter(this);
    m_dropIndicator = new QFrame(m_container);
    m_dropIndicator->setFixedHeight(2);
    m_dropIndicator->setObjectName(QStringLiteral("accentBar"));
    m_dropIndicator->setVisible(false);

    setupCardProviders();
    sync();
}

FormulaTab::~FormulaTab()
{
    delete m_formulaModel;
}

void FormulaTab::setUndoStack(QUndoStack* stack)
{
    CardTabBase::setUndoStack(stack);
    m_formulaModel->setUndoStack(stack);
}

void FormulaTab::setupCardProviders()
{
    m_host->setProviders(
        [this](int row) -> QWidget* {
            if (row < 0 || row >= m_formulaModel->rows().size())
                return nullptr;
            const cad::ui::FormulaTabModel::Row& fr = m_formulaModel->rows()[row];
            if (fr.isHeader) {
                const auto* g = m_doc->variablesView().groupById(fr.id);
                if (!g)
                    return nullptr;
                auto* header = new FormulaGroupHeader(
                    g->id, g->name, g->collapsed,
                    formulaGroupMemberCount(m_doc, g->id), m_host);
                connect(header, &FormulaGroupHeader::toggleRequested,
                        this, &FormulaTab::onGroupToggled);
                connect(header, &FormulaGroupHeader::renameRequested,
                        this, &FormulaTab::onGroupRenamed);
                connect(header, &FormulaGroupHeader::dissolveRequested,
                        this, &FormulaTab::onGroupDissolved);
                connect(header, &FormulaGroupHeader::formulaDropped,
                        this, &FormulaTab::onFormulaDroppedOnHeader);
                return header;
            }
            const auto* f = m_doc->variablesView().formulaById(fr.id);
            if (!f)
                return nullptr;
            auto* card = new FormulaCard(*f, fr.localIndex % 2 == 1, m_host);
            card->setIndex(fr.localIndex + 1);
            card->setGrouped(!fr.groupId.isNull());
            connect(card, &FormulaCard::deleteRequested,
                    this, &FormulaTab::onFormulaDeleted);
            connect(card, &FormulaCard::edited,
                    this, &FormulaTab::onFormulaEdited);
            connect(card, &FormulaCard::conditionsEditRequested,
                    this, &FormulaTab::onConditionsEditRequested);
            return card;
        },
        [this](int row, QWidget* w) {
            if (row < 0 || row >= m_formulaModel->rows().size())
                return;
            const cad::ui::FormulaTabModel::Row& fr = m_formulaModel->rows()[row];
            if (fr.isHeader) {
                const auto* g = m_doc->variablesView().groupById(fr.id);
                if (!g)
                    return;
                auto* header = static_cast<FormulaGroupHeader*>(w);
                header->setName(g->name);
                header->setCollapsed(g->collapsed);
                header->setCount(formulaGroupMemberCount(m_doc, g->id));
            } else {
                const auto* f = m_doc->variablesView().formulaById(fr.id);
                if (!f)
                    return;
                auto* card = static_cast<FormulaCard*>(w);
                card->syncFromModel(*f);
                card->setIndex(fr.localIndex + 1);
                card->setGrouped(!fr.groupId.isNull());
                card->setAlternate(fr.localIndex % 2 == 1);  // 缓存复用: 奇偶重设
            }
        });
}

void FormulaTab::addNewFormula()
{
    cad::param::FormulaVariable f;
    f.name = QStringLiteral("新公式");
    f.expression.clear();

    if (m_undoStack)
        m_undoStack->push(new cad::cmd::AddFormulaCommand(m_doc, f));
    else
        m_doc->addFormula(f);

    // New formulas land at the end of the ungrouped section: focus the card.
    const QUuid id = f.id;
    QTimer::singleShot(0, this, [this, id]() {
        m_host->ensureMaterialized(id);
        const int row = m_host->rowOf(id);
        if (row >= 0) {
            const QRect r = m_host->rowRect(row).translated(m_host->pos());
            m_scroll->ensureVisible(r.center().x(), r.center().y());
        }
        if (auto* card = qobject_cast<FormulaCard*>(m_host->widgetFor(id)))
            card->focusName();
    });
}

void FormulaTab::addGroup()
{
    cad::param::FormulaGroup g;
    g.name = QStringLiteral("分组 %1").arg(m_doc->formulaGroups().size() + 1);

    if (m_undoStack)
        m_undoStack->push(new cad::cmd::AddFormulaGroupCommand(m_doc, g));
    else
        m_doc->addFormulaGroup(g);

    // Scroll to the new header and open the inline rename editor.
    const QUuid id = g.id;
    QTimer::singleShot(0, this, [this, id]() {
        m_host->ensureMaterialized(id);
        const int row = m_host->rowOf(id);
        if (row >= 0) {
            const QRect r = m_host->rowRect(row).translated(m_host->pos());
            m_scroll->ensureVisible(r.center().x(), r.center().y());
        }
        if (auto* header = qobject_cast<FormulaGroupHeader*>(m_host->widgetFor(id)))
            header->startRename();
    });
}

void FormulaTab::onGroupToggled(const QUuid& groupId)
{
    m_formulaModel->toggleCollapsed(groupId);
}

void FormulaTab::onGroupRenamed(const QUuid& groupId, const QString& newName)
{
    m_formulaModel->rename(groupId, newName);
}

void FormulaTab::onGroupDissolved(const QUuid& groupId)
{
    m_formulaModel->dissolve(groupId);
}

void FormulaTab::onFormulaDroppedOnHeader(const QUuid& formulaId, const QUuid& groupId)
{
    // Dropping on the header appends to the end of that group.
    m_formulaModel->moveFormula(formulaId, groupId,
                                m_formulaModel->formulaCountIn(groupId));
}

void FormulaTab::computeFormulaDropSlot(int y, QUuid& groupId, int& localIndex,
                                        int& indicatorY) const
{
    groupId = QUuid();
    localIndex = 0;
    const QPoint hostPos = m_host->pos();
    indicatorY = hostPos.y() + 8;  // host top margin, container coords

    QUuid curSection;
    int count = 0;
    for (int i = 0; i < static_cast<int>(m_formulaModel->rows().size()); ++i) {
        const QRect geo = m_host->rowRect(i).translated(hostPos);
        if (y < geo.center().y()) {
            groupId = curSection;
            localIndex = count;
            indicatorY = geo.top() - 4;
            return;
        }
        const cad::ui::FormulaTabModel::Row& fr = m_formulaModel->rows()[i];
        if (fr.isHeader) {
            curSection = fr.id;
            count = 0;
        } else {
            ++count;
        }
        indicatorY = geo.bottom() + 2;
    }
    groupId = curSection;
    localIndex = count;
}

void FormulaTab::computeGroupDropSlot(int y, int& insertIndex, int& indicatorY) const
{
    insertIndex = 0;
    const QPoint hostPos = m_host->pos();
    indicatorY = hostPos.y() + 8;

    int idx = 0;
    for (int i = 0; i < static_cast<int>(m_formulaModel->rows().size()); ++i) {
        const cad::ui::FormulaTabModel::Row& fr = m_formulaModel->rows()[i];
        if (!fr.isHeader)
            continue;
        const QRect geo = m_host->rowRect(i).translated(hostPos);
        if (y < geo.center().y()) {
            insertIndex = idx;
            indicatorY = geo.top() - 4;
            return;
        }
        ++idx;
    }
    insertIndex = idx;
    if (!m_formulaModel->rows().isEmpty()) {
        const QRect geo = m_host->rowRect(m_formulaModel->rows().size() - 1)
                              .translated(hostPos);
        indicatorY = geo.bottom() + 2;
    }
}

bool FormulaTab::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != m_container)
        return CardTabBase::eventFilter(obj, event);

    switch (event->type()) {
    case QEvent::DragEnter: {
        auto* e = static_cast<QDragEnterEvent*>(event);
        if (e->mimeData()->hasFormat(FormulaCard::kDragMimeType)
            || e->mimeData()->hasFormat(FormulaGroupHeader::kDragMimeType)) {
            e->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::DragMove: {
        auto* e = static_cast<QDragMoveEvent*>(event);
        const int y = e->position().toPoint().y();
        int indicatorY = 0;
        if (e->mimeData()->hasFormat(FormulaCard::kDragMimeType)) {
            QUuid gid;
            int local = 0;
            computeFormulaDropSlot(y, gid, local, indicatorY);
        } else if (e->mimeData()->hasFormat(FormulaGroupHeader::kDragMimeType)) {
            int idx = 0;
            computeGroupDropSlot(y, idx, indicatorY);
        } else {
            break;
        }
        const QPoint hostPos = m_host->pos();
        m_dropIndicator->setGeometry(
            hostPos.x() + 10, indicatorY,
            m_container->width() - 10 - 16, 2);
        m_dropIndicator->setVisible(true);
        m_dropIndicator->raise();
        e->acceptProposedAction();
        return true;
    }
    case QEvent::DragLeave:
        m_dropIndicator->setVisible(false);
        break;
    case QEvent::Drop: {
        m_dropIndicator->setVisible(false);
        auto* e = static_cast<QDropEvent*>(event);
        const int y = e->position().toPoint().y();
        int indicatorY = 0;
        if (e->mimeData()->hasFormat(FormulaCard::kDragMimeType)) {
            const QUuid id(QString::fromLatin1(
                e->mimeData()->data(FormulaCard::kDragMimeType)));
            QUuid gid;
            int local = 0;
            computeFormulaDropSlot(y, gid, local, indicatorY);
            if (!id.isNull())
                m_formulaModel->moveFormula(id, gid, local);
            e->acceptProposedAction();
            return true;
        }
        if (e->mimeData()->hasFormat(FormulaGroupHeader::kDragMimeType)) {
            const QUuid gid(QString::fromLatin1(
                e->mimeData()->data(FormulaGroupHeader::kDragMimeType)));
            int insertIdx = 0;
            computeGroupDropSlot(y, insertIdx, indicatorY);

            const auto& groups = m_doc->formulaGroups();
            int from = -1;
            for (int i = 0; i < static_cast<int>(groups.size()); ++i) {
                if (groups[i].id == gid) { from = i; break; }
            }
            if (from >= 0) {
                int to = insertIdx;
                if (to > from)
                    --to;  // Slots are pre-removal.
                if (to != from) {
                    if (m_undoStack)
                        m_undoStack->push(new cad::cmd::MoveFormulaGroupCommand(
                            m_doc, from, to));
                    else
                        m_doc->moveFormulaGroup(from, to);
                }
            }
            e->acceptProposedAction();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return CardTabBase::eventFilter(obj, event);
}

void FormulaTab::onFormulaDeleted(const QUuid& id)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveFormulaCommand(m_doc, id));
    else
        m_doc->removeFormula(id);
}

void FormulaTab::onFormulaEdited(const cad::param::FormulaVariable& formula)
{
    // Skip no-op edits.
    if (const auto* cur = m_doc->variablesView().formulaById(formula.id)) {
        if (cur->name == formula.name && cur->expression == formula.expression
            && cur->actualValueCm == formula.actualValueCm
            && cur->comment == formula.comment
            && cur->conditionsEnabled == formula.conditionsEnabled
            && cur->conditions.size() == formula.conditions.size())
            return;
    }
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::SetFormulaCommand(m_doc, formula));
    else
        m_doc->updateFormula(formula);
}

void FormulaTab::onConditionsEditRequested(const QUuid& id)
{
    const auto* f = m_doc->variablesView().formulaById(id);
    if (!f) return;

    QHash<QString, double> known;
    for (const auto& v : m_doc->variables()) {
        const double cm = cad::geo::Units::mmToCm(v.value);
        if (!v.name.isEmpty())
            known.insert(v.name, cm);
        if (!v.refName.isEmpty())
            known.insert(v.refName, cm);
    }
    for (const auto& lv : m_doc->linkedVars()) {
        if (!lv.dangling && !lv.refName.isEmpty()) {
            const double cm = cad::geo::Units::mmToCm(lv.value);
            known.insert(lv.refName, cm);
            if (!lv.name.isEmpty())
                known.insert(lv.name, cm);
        }
    }
    for (const auto& mv : m_doc->measureVars()) {
        if (!mv.refName.isEmpty()) {
            const double cm = cad::geo::Units::mmToCm(mv.value);
            known.insert(mv.refName, cm);
            if (!mv.name.isEmpty())
                known.insert(mv.name, cm);
        }
    }
    for (const auto& am : m_doc->angleMeasures()) {
        if (!am.dangling && !am.refName.isEmpty()) {
            known.insert(am.refName, am.value);
            if (!am.name.isEmpty())
                known.insert(am.name, am.value);
        }
    }

    ConditionDialog dlg(f->name, f->expression, f->conditions, known, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    cad::param::FormulaVariable updated = *f;
    updated.conditions = dlg.conditions();
    updated.conditionsEnabled = true;

    if (m_undoStack)
        m_undoStack->push(new cad::cmd::SetFormulaCommand(m_doc, updated));
    else
        m_doc->updateFormula(updated);
}

void FormulaTab::sync()
{
    m_formulaModel->rebuild();
    m_host->setRows(m_formulaModel->keys());
    setEmptyHintVisible(m_doc->formulas().empty()
                        && m_doc->formulaGroups().empty());
}

} // namespace cad::ui
