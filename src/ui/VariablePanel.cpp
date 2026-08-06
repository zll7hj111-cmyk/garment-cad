#include "VariablePanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QFrame>
#include <QRect>
#include <QTimer>
#include <QSet>
#include <QHash>
#include <QUndoStack>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

#include "VariableCard.h"
#include "FormulaCard.h"
#include "FormulaGroupHeader.h"
#include "LinkedCard.h"
#include "MeasureCard.h"
#include "AngleMeasureCard.h"
#include "ConditionDialog.h"
#include "IconHelper.h"
#include "VirtualCardList.h"
#include "parametric/ParamDocument.h"
#include "parametric/PerfProbe.h"
#include "geometry/Units.h"
#include "document/commands/VariableCommands.h"

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

/// Build a "点A ↔ 点B" source label from a measure variable's two points.
QString measureSourceLabel(const cad::param::ParamDocument* doc,
                           const cad::param::MeasureVariable& mv)
{
    auto pointLabel = [doc](const QUuid& blockId, const QUuid& pointId) -> QString {
        const auto* blk = doc->findBlock(blockId);
        const auto* pt = blk ? blk->findPoint(pointId) : nullptr;
        if (!pt) return QStringLiteral("?");
        QString s = pt->serial;
        if (!pt->name.isEmpty())
            s += QStringLiteral("·") + pt->name;
        return s;
    };
    return pointLabel(mv.blockA, mv.pointA)
         + QStringLiteral(" \u2194 ")
         + pointLabel(mv.blockB, mv.pointB);
}

/// Build a "线A ∠ 线B" source label from an angle measure variable's two segments.
QString angleSourceLabel(const cad::param::ParamDocument* doc,
                         const cad::param::AngleMeasureVariable& am)
{
    auto segLabel = [doc](const QUuid& blockId, const QUuid& segmentId) -> QString {
        const auto* blk = doc->findBlock(blockId);
        const auto* seg = blk ? blk->findSegment(segmentId) : nullptr;
        if (!seg) return QStringLiteral("?");
        QString s = seg->serial;
        if (!seg->name.isEmpty())
            s += QStringLiteral("·") + seg->name;
        return s;
    };
    return segLabel(am.blockA, am.segmentA)
         + QStringLiteral(" \u2220 ")
         + segLabel(am.blockB, am.segmentB);
}

/// Number of formulas currently assigned to a group.
int formulaGroupMemberCount(const cad::param::ParamDocument* doc, const QUuid& groupId)
{
    int count = 0;
    for (const auto& f : doc->formulas())
        if (f.groupId == groupId)
            ++count;
    return count;
}

} // namespace

VariablePanel::VariablePanel(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    setMinimumWidth(280);

    setupUi();

    // Refresh UI when the document's variables/formulas change.
    // Smart sync: only rebuilds cards on structural changes (add/remove);
    // value-only changes update cards in-place, preserving focus and scroll.
    connect(m_doc, &cad::param::ParamDocument::variablesChanged,
            this, &VariablePanel::syncVariableCards);
    connect(m_doc, &cad::param::ParamDocument::formulasChanged,
            this, &VariablePanel::syncFormulaCards);
    connect(m_doc, &cad::param::ParamDocument::formulaGroupsChanged,
            this, &VariablePanel::syncFormulaCards);
    connect(m_doc, &cad::param::ParamDocument::linkedVarsChanged,
            this, &VariablePanel::syncLinkedCards);
    connect(m_doc, &cad::param::ParamDocument::measureVarsChanged,
            this, &VariablePanel::syncMeasureCards);
    connect(m_doc, &cad::param::ParamDocument::angleMeasureVarsChanged,
            this, &VariablePanel::syncMeasureCards);
    connect(m_doc, &cad::param::ParamDocument::resolved,
            this, &VariablePanel::syncLinkedCards);
    connect(m_doc, &cad::param::ParamDocument::resolved,
            this, &VariablePanel::syncMeasureCards);
}

void VariablePanel::setupUi()
{
    setStyleSheet("background: #FFFFFF;");

    auto* wrapperLayout = new QVBoxLayout(this);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(0);

    // ===== Header: tabs + count + add =====
    auto* header = new QWidget(this);
    header->setStyleSheet("background: #FFFFFF;");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 6, 10, 0);
    headerLayout->setSpacing(8);

    m_tabBar = new QTabBar(header);
    m_tabBar->addTab(QStringLiteral("\u5c3a\u5bf8"));    // 尺寸
    m_tabBar->addTab(QStringLiteral("\u516c\u5f0f"));    // 公式
    m_tabBar->addTab(QStringLiteral("\u5173\u8054"));    // 关联
    m_tabBar->addTab(QStringLiteral("\u6d4b\u91cf"));    // 测量
    m_tabBar->setExpanding(false);
    m_tabBar->setDrawBase(false);
    m_tabBar->setCursor(Qt::PointingHandCursor);
    m_tabBar->setStyleSheet(
        "QTabBar::tab {"
        "  font-size: 12px; color: #7F8C8D;"
        "  background: transparent; border: none;"
        "  padding: 8px 10px; margin-right: 4px;"
        "}"
        "QTabBar::tab:selected {"
        "  font-weight: bold; color: #2E86C1;"
        "  border-bottom: 2px solid #2E86C1;"
        "}"
        "QTabBar::tab:hover:!selected { color: #34495E; }");
    headerLayout->addWidget(m_tabBar);

    m_countLabel = new QLabel(header);
    m_countLabel->setStyleSheet(
        "font-size: 11px; color: #85929E; background: #EAECEE;"
        "border-radius: 8px; padding: 1px 8px;");
    headerLayout->addWidget(m_countLabel);

    headerLayout->addStretch();

    m_addGroupBtn = new QToolButton(header);
    m_addGroupBtn->setIcon(cad::ui::IconHelper::iconByName(
        QStringLiteral("tree-structure"), QColor(0x2E, 0x86, 0xC1)));
    m_addGroupBtn->setIconSize(QSize(14, 14));
    m_addGroupBtn->setToolTip(QStringLiteral("新建分组"));
    m_addGroupBtn->setFixedSize(26, 26);
    m_addGroupBtn->setCursor(Qt::PointingHandCursor);
    m_addGroupBtn->setVisible(false);  // Formula tab only.
    m_addGroupBtn->setStyleSheet(
        "QToolButton { background: transparent; border: 1px solid #AED6F1;"
        "  border-radius: 5px; }"
        "QToolButton:hover { background: #EBF5FB; border: 1px solid #2E86C1; }");
    headerLayout->addWidget(m_addGroupBtn);

    m_addBtn = new QPushButton(QStringLiteral("添加"), header);
    m_addBtn->setIcon(cad::ui::IconHelper::iconByName(QStringLiteral("plus"), Qt::white));
    m_addBtn->setIconSize(QSize(12, 12));
    m_addBtn->setCursor(Qt::PointingHandCursor);
    m_addBtn->setStyleSheet(
        "QPushButton {"
        "  font-size: 12px; font-weight: bold; color: #FFFFFF;"
        "  background-color: #2E86C1; border: none; border-radius: 5px;"
        "  padding: 5px 14px;"
        "}"
        "QPushButton:hover { background-color: #2874A6; }"
        "QPushButton:pressed { background-color: #1B4F72; }");
    headerLayout->addWidget(m_addBtn);

    wrapperLayout->addWidget(header);

    // ===== Separator =====
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    sep->setStyleSheet("background: #E5E8E8; border: none;");
    wrapperLayout->addWidget(sep);

    // ===== Stacked pages =====
    m_stack = new QStackedWidget(this);

    m_stack->addWidget(buildListPage(
        m_varScroll, m_varContainer, m_varHost, m_varEmptyHint,
        QStringLiteral("暂无尺寸变量\n点击右上角「＋ 添加」创建")));

    m_stack->addWidget(buildListPage(
        m_formulaScroll, m_formulaContainer, m_formulaHost, m_formulaEmptyHint,
        QStringLiteral("暂无公式变量\n点击右上角「＋ 添加」创建\n\n表达式示例: 胸围/2+6 或 b/2+6")));

    // Formula page: accept card/header drops for reordering & grouping.
    m_formulaContainer->setAcceptDrops(true);
    m_formulaContainer->installEventFilter(this);
    m_dropIndicator = new QFrame(m_formulaContainer);
    m_dropIndicator->setFixedHeight(2);
    m_dropIndicator->setStyleSheet("background: #2E86C1; border: none;");
    m_dropIndicator->setVisible(false);

    m_stack->addWidget(buildListPage(
        m_linkedScroll, m_linkedContainer, m_linkedHost, m_linkedEmptyHint,
        QStringLiteral("暂无关联参数\n右键点击线段 →「发布长度参数」\n或在属性对话框中点击「发布」")));

    m_stack->addWidget(buildListPage(
        m_measureScroll, m_measureContainer, m_measureHost, m_measureEmptyHint,
        QStringLiteral("暂无测量变量\n使用智能笔连接两个点时自动创建\n（测量两点间距离）\n或使用「角度测量」工具测量两线夹角")));

    setupCardProviders();

    wrapperLayout->addWidget(m_stack, 1);

    // ===== Connections =====
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
        // Hide the add button on the linked/measure tabs (auto-generated).
        m_addBtn->setVisible(index != 2 && index != 3);
        m_addGroupBtn->setVisible(index == 1);
        updateCountLabel();
    });
    connect(m_addBtn, &QPushButton::clicked, this, &VariablePanel::onAddClicked);
    connect(m_addGroupBtn, &QToolButton::clicked,
            this, &VariablePanel::onAddGroupClicked);

    updateCountLabel();
}

QWidget* VariablePanel::buildListPage(QScrollArea*& scrollOut, QWidget*& containerOut,
                                      VirtualCardList*& hostOut, QLabel*& emptyHintOut,
                                      const QString& emptyText)
{
    scrollOut = new QScrollArea(this);
    scrollOut->setWidgetResizable(true);
    scrollOut->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollOut->setFrameShape(QFrame::NoFrame);
    scrollOut->setStyleSheet(
        "QScrollArea { background: #F0F2F5; border: none; }"
        "QScrollBar:vertical { width: 8px; background: transparent; }"
        "QScrollBar::handle:vertical {"
        "  background: #CCD1D1; border-radius: 4px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: #AAB7B8; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");

    containerOut = new QWidget();
    containerOut->setStyleSheet("background: #F0F2F5;");
    auto* layout = new QVBoxLayout(containerOut);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    emptyHintOut = new QLabel(emptyText, containerOut);
    emptyHintOut->setAlignment(Qt::AlignCenter);
    emptyHintOut->setStyleSheet(
        "font-size: 12px; color: #ABB2B9; background: transparent; padding: 40px 0;");
    layout->addWidget(emptyHintOut);

    // Virtualized card host: list margins live inside the host, and only
    // rows near the viewport are materialized (O(visible) widget count).
    hostOut = new VirtualCardList(containerOut);
    layout->addWidget(hostOut);
    hostOut->init(scrollOut);

    layout->addStretch();

    scrollOut->setWidget(containerOut);
    return scrollOut;
}

void VariablePanel::setupCardProviders()
{
    // ---- Tab 0: plain variables ----
    m_varHost->setProviders(
        [this](int row) -> QWidget* {
            const auto& vars = m_doc->variables();
            if (row < 0 || row >= static_cast<int>(vars.size()))
                return nullptr;
            auto* card = new VariableCard(vars[row], row % 2 == 1, m_varHost);
            connect(card, &VariableCard::deleteRequested,
                    this, &VariablePanel::onVariableDeleted);
            connect(card, &VariableCard::edited,
                    this, &VariablePanel::onVariableEdited);
            return card;
        },
        [this](int row, QWidget* w) {
            const auto& vars = m_doc->variables();
            if (row < 0 || row >= static_cast<int>(vars.size()))
                return;
            static_cast<VariableCard*>(w)->syncFromModel(vars[row]);
        });

    // ---- Tab 1: formulas (cards + group headers) ----
    m_formulaHost->setProviders(
        [this](int row) -> QWidget* {
            if (row < 0 || row >= m_formulaRows.size())
                return nullptr;
            const FormulaRow& fr = m_formulaRows[row];
            if (fr.isHeader) {
                const auto* g = m_doc->findFormulaGroup(fr.id);
                if (!g)
                    return nullptr;
                auto* header = new FormulaGroupHeader(
                    g->id, g->name, g->collapsed,
                    formulaGroupMemberCount(m_doc, g->id), m_formulaHost);
                connect(header, &FormulaGroupHeader::toggleRequested,
                        this, &VariablePanel::onGroupToggled);
                connect(header, &FormulaGroupHeader::renameRequested,
                        this, &VariablePanel::onGroupRenamed);
                connect(header, &FormulaGroupHeader::dissolveRequested,
                        this, &VariablePanel::onGroupDissolved);
                connect(header, &FormulaGroupHeader::formulaDropped,
                        this, &VariablePanel::onFormulaDroppedOnHeader);
                return header;
            }
            const auto* f = m_doc->findFormula(fr.id);
            if (!f)
                return nullptr;
            auto* card = new FormulaCard(*f, fr.localIndex % 2 == 1, m_formulaHost);
            card->setIndex(fr.localIndex + 1);
            connect(card, &FormulaCard::deleteRequested,
                    this, &VariablePanel::onFormulaDeleted);
            connect(card, &FormulaCard::edited,
                    this, &VariablePanel::onFormulaEdited);
            connect(card, &FormulaCard::conditionsEditRequested,
                    this, &VariablePanel::onConditionsEditRequested);
            return card;
        },
        [this](int row, QWidget* w) {
            if (row < 0 || row >= m_formulaRows.size())
                return;
            const FormulaRow& fr = m_formulaRows[row];
            if (fr.isHeader) {
                const auto* g = m_doc->findFormulaGroup(fr.id);
                if (!g)
                    return;
                auto* header = static_cast<FormulaGroupHeader*>(w);
                header->setName(g->name);
                header->setCollapsed(g->collapsed);
                header->setCount(formulaGroupMemberCount(m_doc, g->id));
            } else {
                const auto* f = m_doc->findFormula(fr.id);
                if (!f)
                    return;
                auto* card = static_cast<FormulaCard*>(w);
                card->syncFromModel(*f);
                card->setIndex(fr.localIndex + 1);
            }
        });

    // ---- Tab 2: linked variables ----
    m_linkedHost->setProviders(
        [this](int row) -> QWidget* {
            const auto& linked = m_doc->linkedVars();
            if (row < 0 || row >= static_cast<int>(linked.size()))
                return nullptr;
            auto* card = new LinkedCard(linked[row],
                                        linkedSourceLabel(m_doc, linked[row]),
                                        row % 2 == 1, m_linkedHost);
            connect(card, &LinkedCard::deleteRequested,
                    this, &VariablePanel::onLinkedDeleted);
            connect(card, &LinkedCard::edited,
                    this, &VariablePanel::onLinkedEdited);
            connect(card, &LinkedCard::sourceClicked,
                    this, &VariablePanel::highlightBlockRequested);
            return card;
        },
        [this](int row, QWidget* w) {
            const auto& linked = m_doc->linkedVars();
            if (row < 0 || row >= static_cast<int>(linked.size()))
                return;
            static_cast<LinkedCard*>(w)->syncFromModel(
                linked[row], linkedSourceLabel(m_doc, linked[row]));
        });

    // ---- Tab 3: measure variables (length + angle cards) ----
    m_measureHost->setProviders(
        [this](int row) -> QWidget* {
            const auto& measures = m_doc->measureVars();
            const auto& angles = m_doc->angleMeasures();
            const int m = static_cast<int>(measures.size());
            if (row < 0 || row >= m + static_cast<int>(angles.size()))
                return nullptr;
            if (row < m) {
                auto* card = new MeasureCard(measures[row],
                    measureSourceLabel(m_doc, measures[row]),
                    row % 2 == 1, m_measureHost);
                card->setIndex(row + 1);
                connect(card, &MeasureCard::deleteRequested,
                        this, &VariablePanel::onMeasureDeleted);
                connect(card, &MeasureCard::edited,
                        this, &VariablePanel::onMeasureEdited);
                // The card emits its own measure id — flash the measured
                // points precisely (falls back to a whole-block flash in
                // MainWindow when the points are missing/unresolved).
                connect(card, &MeasureCard::sourceClicked,
                        this, &VariablePanel::highlightMeasureRequested);
                return card;
            }
            const int ai = row - m;
            auto* card = new AngleMeasureCard(angles[ai],
                angleSourceLabel(m_doc, angles[ai]),
                ai % 2 == 1, m_measureHost);
            card->setIndex(ai + 1);
            connect(card, &AngleMeasureCard::deleteRequested,
                    this, &VariablePanel::onAngleMeasureDeleted);
            connect(card, &AngleMeasureCard::edited,
                    this, &VariablePanel::onAngleMeasureEdited);
            connect(card, &AngleMeasureCard::sourceClicked,
                    this, &VariablePanel::highlightBlockRequested);
            return card;
        },
        [this](int row, QWidget* w) {
            const auto& measures = m_doc->measureVars();
            const auto& angles = m_doc->angleMeasures();
            const int m = static_cast<int>(measures.size());
            if (row < 0 || row >= m + static_cast<int>(angles.size()))
                return;
            if (row < m) {
                auto* card = static_cast<MeasureCard*>(w);
                card->syncFromModel(
                    measures[row], measureSourceLabel(m_doc, measures[row]));
                card->setIndex(row + 1);
            } else {
                const int ai = row - m;
                auto* card = static_cast<AngleMeasureCard*>(w);
                card->syncFromModel(
                    angles[ai], angleSourceLabel(m_doc, angles[ai]));
                card->setIndex(ai + 1);
            }
        });
}

// ============================================================
// Add
// ============================================================

QString VariablePanel::nextRefName() const
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

void VariablePanel::onAddClicked()
{
    if (m_tabBar->currentIndex() == 0)
        addNewVariable();
    else
        addNewFormula();
}

void VariablePanel::addNewVariable()
{
    cad::param::Variable v;
    v.name = QStringLiteral("新变量");
    v.value = 0.0;

    if (m_undoStack)
        m_undoStack->push(new cad::cmd::AddVariableCommand(m_doc, v));
    else
        m_doc->addVariable(v);

    // The new card may sit below the window: materialize it, scroll down,
    // then focus its name editor.
    const QUuid id = v.id;
    QTimer::singleShot(0, this, [this, id]() {
        m_varHost->ensureMaterialized(id);
        auto* bar = m_varScroll->verticalScrollBar();
        bar->setValue(bar->maximum());
        if (auto* card = qobject_cast<VariableCard*>(m_varHost->widgetFor(id)))
            card->focusName();
    });
}

void VariablePanel::addNewFormula()
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
        m_formulaHost->ensureMaterialized(id);
        const int row = m_formulaHost->rowOf(id);
        if (row >= 0) {
            const QRect r = m_formulaHost->rowRect(row)
                                .translated(m_formulaHost->pos());
            m_formulaScroll->ensureVisible(r.center().x(), r.center().y());
        }
        if (auto* card = qobject_cast<FormulaCard*>(m_formulaHost->widgetFor(id)))
            card->focusName();
    });
}

void VariablePanel::onAddGroupClicked()
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
        m_formulaHost->ensureMaterialized(id);
        const int row = m_formulaHost->rowOf(id);
        if (row >= 0) {
            const QRect r = m_formulaHost->rowRect(row)
                                .translated(m_formulaHost->pos());
            m_formulaScroll->ensureVisible(r.center().x(), r.center().y());
        }
        if (auto* header = qobject_cast<FormulaGroupHeader*>(m_formulaHost->widgetFor(id)))
            header->startRename();
    });
}

// ============================================================
// Edit / delete handlers
// ============================================================

void VariablePanel::onVariableDeleted(const QUuid& id)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveVariableCommand(m_doc, id));
    else
        m_doc->removeVariable(id);
}

void VariablePanel::onVariableEdited(const cad::param::Variable& var)
{
    // Skip no-op edits (e.g. spinbox commit without actual change).
    if (const auto* cur = m_doc->findVariable(var.id)) {
        if (cur->name == var.name && cur->refName == var.refName
            && qFuzzyIsNull(cur->value - var.value) && cur->comment == var.comment)
            return;
    }
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::SetVariableCommand(m_doc, var));
    else
        m_doc->updateVariable(var);
}

void VariablePanel::onFormulaDeleted(const QUuid& id)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveFormulaCommand(m_doc, id));
    else
        m_doc->removeFormula(id);
}

void VariablePanel::onFormulaEdited(const cad::param::FormulaVariable& formula)
{
    // Skip no-op edits.
    if (const auto* cur = m_doc->findFormula(formula.id)) {
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

void VariablePanel::onLinkedDeleted(const QUuid& id)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveLinkedCommand(m_doc, id));
    else
        m_doc->removeLinked(id);
}

void VariablePanel::onLinkedEdited(const cad::param::LinkedVariable& lv)
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

void VariablePanel::onConditionsEditRequested(const QUuid& id)
{
    const auto* f = m_doc->findFormula(id);
    if (!f) return;

    // Known variables (cm) under both display name and reference name.
    QHash<QString, double> known;
    for (const auto& v : m_doc->variables()) {
        const double cm = cad::geo::Units::mmToCm(v.value);
        if (!v.name.isEmpty())
            known.insert(v.name, cm);
        if (!v.refName.isEmpty())
            known.insert(v.refName, cm);
    }

    ConditionDialog dlg(f->name, f->expression, f->conditions, known, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // Route through the formula command: updateFormula() emits
    // formulasChanged + recomputes, which rebinds the card display.
    cad::param::FormulaVariable updated = *f;
    updated.conditions = dlg.conditions();
    updated.conditionsEnabled = true;

    if (m_undoStack)
        m_undoStack->push(new cad::cmd::SetFormulaCommand(m_doc, updated));
    else
        m_doc->updateFormula(updated);
}

// ============================================================
// Formula groups: toggle / rename / dissolve / drag & drop
// ============================================================

void VariablePanel::onGroupToggled(const QUuid& groupId)
{
    // View state: not undoable, but persisted with the document.
    if (const auto* g = m_doc->findFormulaGroup(groupId))
        m_doc->setFormulaGroupCollapsed(groupId, !g->collapsed);
}

void VariablePanel::onGroupRenamed(const QUuid& groupId, const QString& newName)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RenameFormulaGroupCommand(m_doc, groupId, newName));
    else
        m_doc->renameFormulaGroup(groupId, newName);
}

void VariablePanel::onGroupDissolved(const QUuid& groupId)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveFormulaGroupCommand(m_doc, groupId));
    else
        m_doc->removeFormulaGroup(groupId);
}

void VariablePanel::onFormulaDroppedOnHeader(const QUuid& formulaId, const QUuid& groupId)
{
    // Dropping on the header appends to the end of that group.
    int count = 0;
    for (const auto& f : m_doc->formulas())
        if (f.groupId == groupId)
            ++count;
    moveFormulaTo(formulaId, groupId, count);
}

void VariablePanel::moveFormulaTo(const QUuid& formulaId, const QUuid& targetGroupId,
                                  int targetLocalIndex)
{
    const auto* f = m_doc->findFormula(formulaId);
    if (!f)
        return;

    // Current local index within its own group.
    int curLocal = 0;
    for (const auto& other : m_doc->formulas()) {
        if (other.id == formulaId)
            break;
        if (other.groupId == f->groupId)
            ++curLocal;
    }

    if (f->groupId == targetGroupId) {
        // Drop slots are pre-removal; account for the card leaving its slot.
        if (targetLocalIndex > curLocal)
            --targetLocalIndex;
        if (targetLocalIndex == curLocal)
            return;  // No-op drop.
    }

    if (m_undoStack)
        m_undoStack->push(new cad::cmd::MoveFormulaCommand(
            m_doc, formulaId, targetGroupId, targetLocalIndex));
    else
        m_doc->moveFormula(formulaId, targetGroupId, targetLocalIndex);
}

void VariablePanel::computeFormulaDropSlot(int y, QUuid& groupId, int& localIndex,
                                           int& indicatorY) const
{
    groupId = QUuid();
    localIndex = 0;
    const QPoint hostPos = m_formulaHost->pos();
    indicatorY = hostPos.y() + 8;  // host top margin, container coords

    // Walk display rows top -> bottom, tracking the current section.
    // Row geometry comes from the host's height cache, so the computation
    // works even for rows outside the materialization window.
    QUuid curSection;
    int count = 0;
    for (int i = 0; i < static_cast<int>(m_formulaRows.size()); ++i) {
        const QRect geo = m_formulaHost->rowRect(i).translated(hostPos);
        if (y < geo.center().y()) {
            // Insert before this row: still in the section seen so far.
            groupId = curSection;
            localIndex = count;
            indicatorY = geo.top() - 4;
            return;
        }
        const FormulaRow& fr = m_formulaRows[i];
        if (fr.isHeader) {
            curSection = fr.id;
            count = 0;
        } else {
            ++count;
        }
        indicatorY = geo.bottom() + 2;
    }
    // Past the last row: append to the last section.
    groupId = curSection;
    localIndex = count;
}

void VariablePanel::computeGroupDropSlot(int y, int& insertIndex, int& indicatorY) const
{
    insertIndex = 0;
    const QPoint hostPos = m_formulaHost->pos();
    indicatorY = hostPos.y() + 8;

    int idx = 0;
    for (int i = 0; i < static_cast<int>(m_formulaRows.size()); ++i) {
        const FormulaRow& fr = m_formulaRows[i];
        if (!fr.isHeader)
            continue;
        const QRect geo = m_formulaHost->rowRect(i).translated(hostPos);
        if (y < geo.center().y()) {
            insertIndex = idx;
            indicatorY = geo.top() - 4;
            return;
        }
        ++idx;
    }
    insertIndex = idx;
    if (!m_formulaRows.isEmpty()) {
        const QRect geo = m_formulaHost->rowRect(m_formulaRows.size() - 1)
                              .translated(hostPos);
        indicatorY = geo.bottom() + 2;
    }
}

bool VariablePanel::eventFilter(QObject* obj, QEvent* event)
{
    if (obj != m_formulaContainer)
        return QWidget::eventFilter(obj, event);

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
        const QPoint hostPos = m_formulaHost->pos();
        m_dropIndicator->setGeometry(
            hostPos.x() + 10, indicatorY,
            m_formulaContainer->width() - 10 - 16, 2);
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
                moveFormulaTo(id, gid, local);
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
    return QWidget::eventFilter(obj, event);
}

// ============================================================
// Smart sync (structure check + in-place update)
// ============================================================

void VariablePanel::syncVariableCards()
{
    const auto& vars = m_doc->variables();
    QVector<QUuid> keys;
    keys.reserve(static_cast<int>(vars.size()));
    for (const auto& v : vars)
        keys.append(v.id);

    // setRows(): structural change → rebuild the window; identical key list
    // → in-place rebind only (preserves focus & scroll position).
    m_varHost->setRows(keys);
    updateCountLabel();
}

void VariablePanel::syncFormulaCards()
{
    const auto& formulas = m_doc->formulas();
    const auto& groups = m_doc->formulaGroups();

    // Display order: ungrouped section first, then per-group header + members.
    // Members of collapsed groups get no rows at all (virtualization makes
    // hiding them free — no widgets, no visibility bookkeeping).
    QVector<FormulaRow> rows;
    QVector<QUuid> keys;
    rows.reserve(static_cast<int>(formulas.size() + groups.size()));
    keys.reserve(rows.capacity());

    int local = 0;
    for (const auto& f : formulas) {
        if (f.groupId.isNull()) {
            rows.append({false, f.id, QUuid(), local++});
            keys.append(f.id);
        }
    }
    for (const auto& g : groups) {
        rows.append({true, g.id, QUuid(), 0});
        keys.append(g.id);
        if (g.collapsed)
            continue;
        local = 0;
        for (const auto& f : formulas) {
            if (f.groupId == g.id) {
                rows.append({false, f.id, g.id, local++});
                keys.append(f.id);
            }
        }
    }

    // Descriptors must be in place before setRows(): the host's factory /
    // binder closures read them during materialization.
    m_formulaRows = rows;
    m_formulaHost->setRows(keys);
    updateCountLabel();
}

void VariablePanel::syncLinkedCards()
{
    GCAD_PERF_SCOPE("ui.syncLinked");
    const auto& linked = m_doc->linkedVars();
    QVector<QUuid> keys;
    keys.reserve(static_cast<int>(linked.size()));
    for (const auto& lv : linked)
        keys.append(lv.id);
    m_linkedHost->setRows(keys);
    updateCountLabel();
}

// ============================================================
// Measure variables (测量变量)
// ============================================================

void VariablePanel::syncMeasureCards()
{
    GCAD_PERF_SCOPE("ui.syncMeasure");
    const auto& measures = m_doc->measureVars();
    const auto& angles = m_doc->angleMeasures();

    QVector<QUuid> keys;
    keys.reserve(static_cast<int>(measures.size() + angles.size()));
    for (const auto& mv : measures)
        keys.append(mv.id);
    for (const auto& am : angles)
        keys.append(am.id);
    m_measureHost->setRows(keys);
    updateCountLabel();
}

void VariablePanel::onMeasureDeleted(const QUuid& id)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveMeasureCommand(m_doc, id));
    else
        m_doc->removeMeasure(id);
}

void VariablePanel::onMeasureEdited(const cad::param::MeasureVariable& mv)
{
    // Skip no-op edits.
    if (const auto* cur = m_doc->findMeasure(mv.id)) {
        if (cur->name == mv.name && cur->comment == mv.comment)
            return;
    }
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::SetMeasureCommand(m_doc, mv));
    else
        m_doc->updateMeasure(mv);
}

void VariablePanel::onAngleMeasureDeleted(const QUuid& id)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveAngleMeasureCommand(m_doc, id));
    else
        m_doc->removeAngleMeasure(id);
}

void VariablePanel::onAngleMeasureEdited(const cad::param::AngleMeasureVariable& am)
{
    // Skip no-op edits.
    if (const auto* cur = m_doc->findAngleMeasure(am.id)) {
        if (cur->name == am.name && cur->comment == am.comment)
            return;
    }
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::SetAngleMeasureCommand(m_doc, am));
    else
        m_doc->updateAngleMeasure(am);
}

void VariablePanel::updateCountLabel()
{
    const int tab = m_tabBar->currentIndex();
    int count = 0;
    if (tab == 0)      count = static_cast<int>(m_doc->variables().size());
    else if (tab == 1) count = static_cast<int>(m_doc->formulas().size());
    else if (tab == 2) count = static_cast<int>(m_doc->linkedVars().size());
    else               count = static_cast<int>(m_doc->measureVars().size()
                                              + m_doc->angleMeasures().size());
    m_countLabel->setText(QString::number(count));
    m_varEmptyHint->setVisible(m_doc->variables().empty());
    m_formulaEmptyHint->setVisible(m_doc->formulas().empty()
                                   && m_doc->formulaGroups().empty());
    m_linkedEmptyHint->setVisible(m_doc->linkedVars().empty());
    m_measureEmptyHint->setVisible(m_doc->measureVars().empty()
                                   && m_doc->angleMeasures().empty());
}
