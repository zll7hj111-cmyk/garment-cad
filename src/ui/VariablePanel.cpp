#include "VariablePanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QFrame>

#include "ElaScrollArea.h"
#include "ElaTabBar.h"
#include "ElaPushButton.h"
#include "ElaToolButton.h"
#include "ElaText.h"
#include "Theme.h"
#include "TooltipFormatter.h"
#include "PanelSubTabBar.h"
#include "IconHelper.h"

#include "VariableTab.h"
#include "FormulaTab.h"
#include "LinkedTab.h"
#include "MeasureTab.h"

#include "parametric/ParamDocument.h"

namespace cad::ui {

VariablePanel::VariablePanel(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    setMinimumWidth(280);

    setupUi();

    // Refresh UI when the document's variables/formulas change.
    connect(m_doc, &cad::param::ParamDocument::variablesChanged, this, [this]() {
        if (m_varTab) m_varTab->sync();
        updateCountLabel();
    });
    connect(m_doc, &cad::param::ParamDocument::formulasChanged, this, [this]() {
        if (m_formulaTab) m_formulaTab->sync();
        updateCountLabel();
    });
    connect(m_doc, &cad::param::ParamDocument::formulaGroupsChanged, this, [this]() {
        if (m_formulaTab) m_formulaTab->sync();
        updateCountLabel();
    });
    connect(m_doc, &cad::param::ParamDocument::linkedVarsChanged, this, [this]() {
        if (m_linkedTab) m_linkedTab->sync();
        updateCountLabel();
    });
    connect(m_doc, &cad::param::ParamDocument::resolved, this, [this]() {
        if (m_linkedTab) m_linkedTab->sync();
    });
}

void VariablePanel::setUndoStack(QUndoStack* stack)
{
    m_undoStack = stack;
    if (m_varTab) m_varTab->setUndoStack(stack);
    if (m_formulaTab) m_formulaTab->setUndoStack(stack);
    if (m_linkedTab) m_linkedTab->setUndoStack(stack);
    if (m_measureTab) m_measureTab->setUndoStack(stack);
}

void VariablePanel::applyTheme()
{
    const auto& tk = cad::ui::Theme::tokens();
    setStyleSheet(QStringLiteral("background: %1;").arg(tk.surface.name()));
    if (m_header)
        m_header->setStyleSheet(QStringLiteral("background: %1;").arg(tk.surface.name()));
    if (m_stack)
        m_stack->setStyleSheet(QStringLiteral("QStackedWidget { background: %1; border: none; }").arg(tk.surface.name()));

    // Refresh sub-tab bar profiles
    if (m_tabBar) {
        m_tabBar->setTabProfile(0, tk.piece1, false);
        m_tabBar->setTabProfile(1, tk.piece2, false);
        m_tabBar->setTabProfile(2, tk.piece4, true);
        m_tabBar->setTabProfile(3, tk.piece3, true);
        m_tabBar->update();
    }

    if (m_addGroupBtn) {
        m_addGroupBtn->setIcon(cad::ui::IconHelper::iconByName(
            QStringLiteral("tree-structure"), tk.text1));
    }

    if (m_varTab) m_varTab->applyTheme();
    if (m_formulaTab) m_formulaTab->applyTheme();
    if (m_linkedTab) m_linkedTab->applyTheme();
    if (m_measureTab) m_measureTab->applyTheme();

    update();
}

void VariablePanel::setupUi()
{
    setAttribute(Qt::WA_StyledBackground, true);
    auto* wrapperLayout = new QVBoxLayout(this);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(0);

    // ===== Header =====
    auto* header = new QWidget(this);
    m_header = header;
    m_header->setAttribute(Qt::WA_StyledBackground, true);
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(8, 6, 10, 4);
    headerLayout->setSpacing(4);

    // SubTabBar: Tab profiles and tooltips
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

    m_varTab = new VariableTab(m_doc, this);
    m_stack->addWidget(m_varTab);

    m_formulaTab = new FormulaTab(m_doc, this);
    m_stack->addWidget(m_formulaTab);

    m_linkedTab = new LinkedTab(m_doc, this);
    m_stack->addWidget(m_linkedTab);
    connect(m_linkedTab, &LinkedTab::highlightBlockRequested,
            this, &VariablePanel::highlightBlockRequested);

    m_measureTab = new MeasureTab(m_doc, this);
    m_stack->addWidget(m_measureTab);
    connect(m_measureTab, &MeasureTab::highlightBlockRequested,
            this, &VariablePanel::highlightBlockRequested);
    connect(m_measureTab, &MeasureTab::highlightMeasureRequested,
            this, &VariablePanel::highlightMeasureRequested);
    connect(m_measureTab, &MeasureTab::highlightAngleMeasureRequested,
            this, &VariablePanel::highlightAngleMeasureRequested);
    connect(m_measureTab, &MeasureTab::clearMeasureHighlightRequested,
            this, &VariablePanel::clearMeasureHighlightRequested);

    connect(m_doc, &cad::param::ParamDocument::measureVarsChanged,
            m_measureTab, &MeasureTab::notifyMeasureDataChanged);
    connect(m_doc, &cad::param::ParamDocument::angleMeasureVarsChanged,
            m_measureTab, &MeasureTab::notifyMeasureDataChanged);
    connect(m_doc, &cad::param::ParamDocument::resolved,
            m_measureTab, &MeasureTab::sync);
    connect(m_doc, &cad::param::ParamDocument::layersChanged,
            m_measureTab, &MeasureTab::notifyMeasureDataChanged);

    wrapperLayout->addWidget(m_stack, 1);

    // ===== Connections =====
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
        m_addBtn->setVisible(index != 2 && index != 3);
        m_addGroupBtn->setVisible(index == 1);
        updateCountLabel();
    });
    connect(m_addBtn, &QPushButton::clicked, this, &VariablePanel::onAddClicked);
    connect(m_addGroupBtn, &QToolButton::clicked,
            this, &VariablePanel::onAddGroupClicked);

    applyTheme();
    updateCountLabel();
}

void VariablePanel::onAddClicked()
{
    if (m_tabBar->currentIndex() == 0 && m_varTab)
        m_varTab->addNewVariable();
    else if (m_tabBar->currentIndex() == 1 && m_formulaTab)
        m_formulaTab->addNewFormula();
}

void VariablePanel::onAddGroupClicked()
{
    if (m_formulaTab)
        m_formulaTab->addGroup();
}

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
}

void VariablePanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    emit clearMeasureHighlightRequested();
}

} // namespace cad::ui
