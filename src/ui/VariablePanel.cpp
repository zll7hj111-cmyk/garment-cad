#include "VariablePanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QPushButton>
#include "ElaScrollArea.h"
#include <QScrollBar>
#include <QStackedWidget>
#include "ElaTabBar.h"
#include "ElaPushButton.h"
#include "ElaToolButton.h"
#include "ElaText.h"
#include "Theme.h"
#include "TooltipFormatter.h"
#include "PanelSubTabBar.h"
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
#include "MeasureTab.h"
#include "ConditionDialog.h"
#include "FormulaTabModel.h"
#include "FormulaTabModel.h"
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
namespace cad::ui {
VariablePanel::VariablePanel(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_formulaModel(new cad::ui::FormulaTabModel(doc))
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
    connect(m_doc, &cad::param::ParamDocument::resolved,
            this, &VariablePanel::syncLinkedCards);
}

VariablePanel::~VariablePanel()
{
    delete m_formulaModel;
}

void VariablePanel::applyTheme()
{
    // The recessed list areas bake their color at construction; re-derive
    // them. findChildren by objectName also covers the MeasureTab instances
    // (they reuse the same names), so one pass restyles every list page.
    // 列表底色 = 画布纸色 (canvasBg), 与画布背景呼应 (2026-08 用户要求).
    const QString scrollQss = QStringLiteral(
        "QScrollArea { background: %1; border: none; }")
        .arg(cad::ui::Theme::tokens().canvasBg.name());
    const QString containerQss = QStringLiteral("background: %1;")
        .arg(cad::ui::Theme::tokens().canvasBg.name());
    for (ElaScrollArea* sa : findChildren<ElaScrollArea*>(QStringLiteral("cardListArea")))
        sa->setStyleSheet(scrollQss);
    for (QWidget* c : findChildren<QWidget*>(QStringLiteral("cardListContainer")))
        c->setStyleSheet(containerQss);
    if (m_measureTab)
        m_measureTab->sync();
    update();  // 类型色竖线/悬停描边由 paintEvent 现读 token, 触发重绘即可
}

void VariablePanel::setUndoStack(QUndoStack* stack)
{
    m_undoStack = stack;
    m_formulaModel->setUndoStack(stack);
    if (m_measureTab) m_measureTab->setUndoStack(stack);
}

void VariablePanel::setupUi()
{
    auto* wrapperLayout = new QVBoxLayout(this);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(0);

    // ===== Header =====
    // 窄悬浮窗下头部重新设计为两行：
    //   行 1 = 变量/公式/关联/测量 四个子标签独占整行、自动拉伸铺满，
    //           不会再被计数标签或添加按钮挤出可视区（空间问题: 后两个
    //           标签页在 300~380px 宽的侧边栏窗口里看不到）。
    //   行 2 = 计数 pill + 新建分组 + 添加 按钮。
    auto* header = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(8, 6, 10, 4);
    headerLayout->setSpacing(4);

    // 子页签 (PanelSubTabBar): 激活下划线 = piece 类型色 (内容层信号),
    // 只读页签 (关联/测量) 恒 text2 不加粗 (§4.2/§6.3)。
    m_tabBar = new cad::ui::PanelSubTabBar(header);
    m_tabBar->addTab(QStringLiteral("\u53d8\u91cf"));    // 变量
    m_tabBar->addTab(QStringLiteral("\u516c\u5f0f"));    // 公式
    m_tabBar->addTab(QStringLiteral("\u5173\u8054"));    // 关联 (只读)
    m_tabBar->addTab(QStringLiteral("\u6d4b\u91cf"));    // 测量 (只读)
    {
        const auto& tk = cad::ui::Theme::tokens();
        m_tabBar->setTabProfile(0, tk.piece1, false);  // 变量 = 碳灰
        m_tabBar->setTabProfile(1, tk.piece2, false);  // 公式 = 深青
        m_tabBar->setTabProfile(2, tk.piece4, true);   // 关联 = 钴蓝 (只读)
        m_tabBar->setTabProfile(3, tk.piece3, true);   // 测量 = 陶土 (只读)
    }
    m_tabBar->setTabToolTip(0, cad::ui::TooltipFormatter::action(
        QStringLiteral("变量列表"),
        QStringLiteral("基础设计规格与固定尺寸数值")));
    m_tabBar->setTabToolTip(1, cad::ui::TooltipFormatter::action(
        QStringLiteral("公式列表"),
        QStringLiteral("由算术与几何关系计算得出的动态尺寸公式")));
    m_tabBar->setTabToolTip(2, cad::ui::TooltipFormatter::action(
        QStringLiteral("关联参数（只读）"),
        QStringLiteral("从当前线段或实体引用的实时长度与角度属性")));
    m_tabBar->setTabToolTip(3, cad::ui::TooltipFormatter::action(
        QStringLiteral("测量参数（只读）"),
        QStringLiteral("画布两点间距、角度测量生成的只读参数")));
    headerLayout->addWidget(m_tabBar);

    auto* metaRow = new QWidget(header);
    auto* metaLayout = new QHBoxLayout(metaRow);
    metaLayout->setContentsMargins(0, 0, 0, 0);
    metaLayout->setSpacing(8);

    m_countLabel = new ElaText(QString(), 13, metaRow);
    m_countLabel->setObjectName(QStringLiteral("panelCountPill"));
    m_countLabel->setToolTip(cad::ui::TooltipFormatter::status(
        QStringLiteral("项目统计"),
        QStringLiteral("当前子页签中的项目总数"),
        false));
    metaLayout->addWidget(m_countLabel);

    metaLayout->addStretch();

    m_addGroupBtn = new ElaToolButton(metaRow);
    m_addGroupBtn->setIcon(cad::ui::IconHelper::iconByName(
        QStringLiteral("tree-structure"), cad::ui::Theme::tokens().text1));
    m_addGroupBtn->setIconSize(QSize(14, 14));
    m_addGroupBtn->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("新建分组"),
        QStringLiteral("为公式变量创建逻辑分类分组，便于折叠和结构化管理")));
    m_addGroupBtn->setFixedSize(26, 26);
    m_addGroupBtn->setCursor(Qt::PointingHandCursor);
    m_addGroupBtn->setVisible(false);  // Formula tab only.
    m_addGroupBtn->setObjectName(QStringLiteral("outlineToolButton"));
    metaLayout->addWidget(m_addGroupBtn);

    m_addBtn = new ElaPushButton(QStringLiteral("添加"), metaRow);
    m_addBtn->setIcon(cad::ui::IconHelper::iconByName(QStringLiteral("plus"), Qt::white));
    m_addBtn->setIconSize(QSize(12, 12));
    m_addBtn->setCursor(Qt::PointingHandCursor);
    m_addBtn->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("添加新项"),
        QStringLiteral("在当前选中的子页签中创建新的变量或公式")));
    m_addBtn->setObjectName(QStringLiteral("primaryButton"));
    metaLayout->addWidget(m_addBtn);

    headerLayout->addWidget(metaRow);

    wrapperLayout->addWidget(header);

    // ===== Separator =====
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    sep->setObjectName(QStringLiteral("divider"));
    wrapperLayout->addWidget(sep);

    // ===== Stacked pages =====
    m_stack = new QStackedWidget(this);

    m_stack->addWidget(buildListPage(
        m_varScroll, m_varContainer, m_varHost, m_varEmptyHint,
        QStringLiteral("暂无变量"),
        QStringLiteral("点击下方按钮或右上角「＋ 添加」创建"),
        QStringLiteral("＋ 新建变量"),
        [this]() { onAddClicked(); }));

    m_stack->addWidget(buildListPage(
        m_formulaScroll, m_formulaContainer, m_formulaHost, m_formulaEmptyHint,
        QStringLiteral("暂无公式变量"),
        QStringLiteral("表达式示例: 胸围/2+6 或 b/2+6"),
        QStringLiteral("＋ 新建公式"),
        [this]() { onAddClicked(); }));

    // Formula page: accept card/header drops for reordering & grouping.
    m_formulaContainer->setAcceptDrops(true);
    m_formulaContainer->installEventFilter(this);
    m_dropIndicator = new QFrame(m_formulaContainer);
    m_dropIndicator->setFixedHeight(2);
    m_dropIndicator->setObjectName(QStringLiteral("accentBar"));
    m_dropIndicator->setVisible(false);

    m_stack->addWidget(buildListPage(
        m_linkedScroll, m_linkedContainer, m_linkedHost, m_linkedEmptyHint,
        QStringLiteral("暂无关联参数"),
        QStringLiteral("右键点击线段 →「发布长度参数」\n或在属性对话框中点击「发布」"),
        QString(), {}));

    // Tab 3: measure variables (length + angle cards, extracted).
    // Create the tab HERE — a missing new left m_measureTab null and crashed
    // in QStackedWidget::addWidget (access violation at startup).
    m_measureTab = new MeasureTab(m_doc, this);
    m_stack->addWidget(m_measureTab);

    connect(m_measureTab, &MeasureTab::highlightBlockRequested,
            this, &VariablePanel::highlightBlockRequested);
    connect(m_measureTab, &MeasureTab::highlightMeasureRequested,
            this, &VariablePanel::highlightMeasureRequested);
    connect(m_measureTab, &MeasureTab::highlightAngleMeasureRequested,
            this, &VariablePanel::highlightAngleMeasureRequested);
    // Measure data flow: must be wired AFTER the tab exists — connecting to
    // a null receiver would silently never fire (the tab would stay empty).
    connect(m_doc, &cad::param::ParamDocument::measureVarsChanged,
            m_measureTab, &MeasureTab::notifyMeasureDataChanged);
    connect(m_doc, &cad::param::ParamDocument::angleMeasureVarsChanged,
            m_measureTab, &MeasureTab::notifyMeasureDataChanged);
    connect(m_doc, &cad::param::ParamDocument::resolved,
            m_measureTab, &MeasureTab::sync);
    // Measure card source labels show layer names — refresh on layer
    // add/remove/rename/visibility so the labels never go stale.
    connect(m_doc, &cad::param::ParamDocument::layersChanged,
            m_measureTab, &MeasureTab::notifyMeasureDataChanged);
    // Component list rebuilds on componentsChanged (wired inside ComponentTab
    // at the panel-window big tab, queued) — undo/redo also re-emits it.

    setupCardProviders();

    wrapperLayout->addWidget(m_stack, 1);

    // ===== Connections =====
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
        // Hide the add button on the linked/measure tabs (they are
        // auto-generated / created from the canvas, not via the ＋ button).
        m_addBtn->setVisible(index != 2 && index != 3);
        m_addGroupBtn->setVisible(index == 1);
        updateCountLabel();
    });
    connect(m_addBtn, &QPushButton::clicked, this, &VariablePanel::onAddClicked);
    connect(m_addGroupBtn, &QToolButton::clicked,
            this, &VariablePanel::onAddGroupClicked);

    updateCountLabel();
}

QWidget* VariablePanel::buildListPage(ElaScrollArea*& scrollOut, QWidget*& containerOut,
                                      VirtualCardList*& hostOut, QWidget*& emptyHintOut,
                                      const QString& emptyTitle, const QString& emptyGuide,
                                      const QString& ghostAddText,
                                      const std::function<void()>& onGhostAdd)
{
    scrollOut = new ElaScrollArea(this);
    scrollOut->setWidgetResizable(true);
    scrollOut->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollOut->setFrameShape(QFrame::NoFrame);
    // Recessed list background: 画布纸色 (canvasBg), 与画布背景呼应.
    scrollOut->setStyleSheet(QStringLiteral(
        "QScrollArea { background: %1; border: none; }")
        .arg(cad::ui::Theme::tokens().canvasBg.name()));
    scrollOut->setObjectName(QStringLiteral("cardListArea"));

    containerOut = new QWidget();
    containerOut->setStyleSheet(QStringLiteral(
        "background: %1;").arg(cad::ui::Theme::tokens().canvasBg.name()));
    containerOut->setObjectName(QStringLiteral("cardListContainer"));
    auto* layout = new QVBoxLayout(containerOut);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ===== 空状态 (§5.4): 18px Semibold 主文案 + 13px 引导语 + 幽灵「＋新建」 =====
    auto* emptyBox = new QWidget(containerOut);
    auto* emptyLay = new QVBoxLayout(emptyBox);
    emptyLay->setContentsMargins(0, 28, 0, 28);
    emptyLay->setSpacing(6);
    const auto& tk = cad::ui::Theme::tokens();

    auto* title = new ElaText(emptyTitle, cad::ui::ThemeTokens::FontXl, emptyBox);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral(
        "font-size: %1px; font-weight: 600; color: %2; background: transparent;")
        .arg(QString::number(cad::ui::ThemeTokens::FontXl), tk.text1.name()));
    emptyLay->addWidget(title);

    auto* guide = new ElaText(emptyGuide, 13, emptyBox);
    guide->setAlignment(Qt::AlignCenter);
    guide->setObjectName(QStringLiteral("dimText"));
    emptyLay->addWidget(guide);

    if (!ghostAddText.isEmpty()) {
        // 幽灵按钮 (§5.1 Ghost): 透明底, hover 出 surface2 底 + 描边。
        auto* ghost = new QPushButton(ghostAddText, emptyBox);
        ghost->setCursor(Qt::PointingHandCursor);
        ghost->setFixedHeight(26);
        ghost->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: 1px solid transparent;"
            "  border-radius: 2px; padding: 0 12px; font-size: 12px; color: %1; }"
            "QPushButton:hover { background: %2; border: 1px solid %3; color: %4; }")
            .arg(tk.text2.name(), tk.surface.name(),
                 tk.borderStrong.name(), tk.text1.name()));
        connect(ghost, &QPushButton::clicked, emptyBox, onGhostAdd);
        emptyLay->addWidget(ghost, 0, Qt::AlignHCenter);
    }
    emptyHintOut = emptyBox;
    layout->addWidget(emptyBox);

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
            card->setIndex(row + 1);
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
            auto* card = static_cast<VariableCard*>(w);
            card->syncFromModel(vars[row]);
            card->setIndex(row + 1);
            card->setAlternate(row % 2 == 1);  // 缓存复用: 奇偶随新行号重设
        });

    // ---- Tab 1: formulas (cards + group headers) ----
    m_formulaHost->setProviders(
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
            const auto* f = m_doc->variablesView().formulaById(fr.id);
            if (!f)
                return nullptr;
            auto* card = new FormulaCard(*f, fr.localIndex % 2 == 1, m_formulaHost);
            card->setIndex(fr.localIndex + 1);
            card->setGrouped(!fr.groupId.isNull());
            connect(card, &FormulaCard::deleteRequested,
                    this, &VariablePanel::onFormulaDeleted);
            connect(card, &FormulaCard::edited,
                    this, &VariablePanel::onFormulaEdited);
            connect(card, &FormulaCard::conditionsEditRequested,
                    this, &VariablePanel::onConditionsEditRequested);
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

    // ---- Tab 2: linked variables ----
    m_linkedHost->setProviders(
        [this](int row) -> QWidget* {
            const auto& linked = m_doc->linkedVars();
            if (row < 0 || row >= static_cast<int>(linked.size()))
                return nullptr;
            auto* card = new LinkedCard(linked[row],
                                        linkedSourceLabel(m_doc, linked[row]),
                                        row % 2 == 1, m_linkedHost);
            card->setIndex(row + 1);
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
            auto* card = static_cast<LinkedCard*>(w);
            card->syncFromModel(
                linked[row], linkedSourceLabel(m_doc, linked[row]));
            card->setIndex(row + 1);
            card->setAlternate(row % 2 == 1);  // 缓存复用: 奇偶随新行号重设
        });
    // 2026-09 性能: resolved 每帧触发 syncLinkedCards → setRows(同 keys) →
    // 值级刷新路径 (只有 value label), 不再整卡 rebind。
    m_linkedHost->setValueBinder([this](int row, QWidget* w) {
        const auto& linked = m_doc->linkedVars();
        if (row < 0 || row >= static_cast<int>(linked.size()))
            return;
        auto* card = static_cast<LinkedCard*>(w);
        card->refreshValue(linked[row].value, linked[row].dangling);
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
    v.refName = nextRefName();
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
    const auto* f = m_doc->variablesView().formulaById(id);
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
    m_formulaModel->toggleCollapsed(groupId);
}

void VariablePanel::onGroupRenamed(const QUuid& groupId, const QString& newName)
{
    m_formulaModel->rename(groupId, newName);
}

void VariablePanel::onGroupDissolved(const QUuid& groupId)
{
    m_formulaModel->dissolve(groupId);
}

void VariablePanel::onFormulaDroppedOnHeader(const QUuid& formulaId, const QUuid& groupId)
{
    // Dropping on the header appends to the end of that group.
    m_formulaModel->moveFormula(formulaId, groupId,
                               m_formulaModel->formulaCountIn(groupId));
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
    for (int i = 0; i < static_cast<int>(m_formulaModel->rows().size()); ++i) {
        const QRect geo = m_formulaHost->rowRect(i).translated(hostPos);
        if (y < geo.center().y()) {
            // Insert before this row: still in the section seen so far.
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
    for (int i = 0; i < static_cast<int>(m_formulaModel->rows().size()); ++i) {
        const cad::ui::FormulaTabModel::Row& fr = m_formulaModel->rows()[i];
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
    if (!m_formulaModel->rows().isEmpty()) {
        const QRect geo = m_formulaHost->rowRect(m_formulaModel->rows().size() - 1)
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
    // Rebuild the display-order descriptors, then hand the key list to the
    // virtualized host (structural change → rebuild window; identical keys
    // → in-place rebind only, preserving focus & scroll).
    m_formulaModel->rebuild();
    m_formulaHost->setRows(m_formulaModel->keys());
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

void VariablePanel::updateCountLabel()
{
    const int tab = m_tabBar->currentIndex();
    int count = 0;
    if (tab == 0)      count = static_cast<int>(m_doc->variables().size());
    else if (tab == 1) count = static_cast<int>(m_doc->formulas().size());
    else if (tab == 2) count = static_cast<int>(m_doc->linkedVars().size());
    else if (tab == 3) count = static_cast<int>(m_doc->measureVars().size()
                                              + m_doc->angleMeasures().size());
    m_countLabel->setText(QString::number(count));
    m_varEmptyHint->setVisible(m_doc->variables().empty());
    m_formulaEmptyHint->setVisible(m_doc->formulas().empty()
                                   && m_doc->formulaGroups().empty());
    m_linkedEmptyHint->setVisible(m_doc->linkedVars().empty());
}
} // namespace cad::ui
