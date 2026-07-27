#include "VariablePanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QTimer>
#include <QSet>
#include <QHash>

#include "VariableCard.h"
#include "FormulaCard.h"
#include "ConditionDialog.h"
#include "parametric/ExpressionEvaluator.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"

using cad::param::ExpressionEvaluator;

VariablePanel::VariablePanel(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(280);

    setupUi();
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
    m_tabBar->addTab(QStringLiteral("尺寸变量"));
    m_tabBar->addTab(QStringLiteral("公式变量"));
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

    m_addBtn = new QPushButton(QStringLiteral("＋ 添加"), header);
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
        m_varScroll, m_varContainer, m_varLayout, m_varEmptyHint,
        QStringLiteral("暂无尺寸变量\n点击右上角「＋ 添加」创建")));

    m_stack->addWidget(buildListPage(
        m_formulaScroll, m_formulaContainer, m_formulaLayout, m_formulaEmptyHint,
        QStringLiteral("暂无公式变量\n点击右上角「＋ 添加」创建\n\n表达式示例: 胸围/2+6 或 b/2+6")));

    wrapperLayout->addWidget(m_stack, 1);

    // ===== Connections =====
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
        updateCountLabel();
    });
    connect(m_addBtn, &QPushButton::clicked, this, &VariablePanel::onAddClicked);

    updateCountLabel();
}

QWidget* VariablePanel::buildListPage(QScrollArea*& scrollOut, QWidget*& containerOut,
                                      QVBoxLayout*& layoutOut, QLabel*& emptyHintOut,
                                      const QString& emptyText)
{
    scrollOut = new QScrollArea(this);
    scrollOut->setWidgetResizable(true);
    scrollOut->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollOut->setFrameShape(QFrame::NoFrame);
    scrollOut->setStyleSheet(
        "QScrollArea { background: #F4F6F7; border: none; }"
        "QScrollBar:vertical { width: 8px; background: transparent; }"
        "QScrollBar::handle:vertical {"
        "  background: #CCD1D1; border-radius: 4px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: #AAB7B8; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");

    containerOut = new QWidget();
    containerOut->setStyleSheet("background: #F4F6F7;");
    layoutOut = new QVBoxLayout(containerOut);
    layoutOut->setContentsMargins(6, 4, 6, 4);
    layoutOut->setSpacing(4);

    emptyHintOut = new QLabel(emptyText, containerOut);
    emptyHintOut->setAlignment(Qt::AlignCenter);
    emptyHintOut->setStyleSheet(
        "font-size: 12px; color: #ABB2B9; background: transparent; padding: 30px 0;");
    layoutOut->addWidget(emptyHintOut);

    layoutOut->addStretch();

    scrollOut->setWidget(containerOut);
    return scrollOut;
}

// ============================================================
// Public setters
// ============================================================

void VariablePanel::setVariables(const QList<cad::param::Variable>& vars)
{
    m_variables = vars;
    rebuildVariableCards();
    recomputeFormulas();
}

void VariablePanel::setFormulas(const QList<cad::param::FormulaVariable>& formulas)
{
    m_formulas = formulas;
    rebuildFormulaCards();
    recomputeFormulas();
}

// ============================================================
// Add
// ============================================================

QString VariablePanel::nextRefName() const
{
    QSet<QString> used;
    for (const auto& v : m_variables)
        used.insert(v.refName);

    int n = 1;
    QString candidate;
    do {
        candidate = QStringLiteral("v%1").arg(n++);
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
    v.refName = nextRefName();
    v.value = 0.0;

    m_variables.append(v);
    rebuildVariableCards();
    recomputeFormulas();
    emit variableAdded(v);

    if (!m_varCards.isEmpty()) {
        m_varCards.last()->focusName();
        scrollToBottom(m_varScroll);
    }
}

void VariablePanel::addNewFormula()
{
    cad::param::FormulaVariable f;
    f.name = QStringLiteral("新公式");
    f.expression.clear();

    m_formulas.append(f);
    rebuildFormulaCards();
    recomputeFormulas();
    emit formulaAdded(f);

    if (!m_formulaCards.isEmpty()) {
        m_formulaCards.last()->focusName();
        scrollToBottom(m_formulaScroll);
    }
}

void VariablePanel::scrollToBottom(QScrollArea* area)
{
    QTimer::singleShot(0, this, [area]() {
        area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
    });
}

// ============================================================
// Edit / delete handlers
// ============================================================

void VariablePanel::onVariableDeleted(const QUuid& id)
{
    m_variables.erase(
        std::remove_if(m_variables.begin(), m_variables.end(),
            [&id](const cad::param::Variable& v) { return v.id == id; }),
        m_variables.end());
    rebuildVariableCards();
    recomputeFormulas();
    emit variableDeleted(id);
}

void VariablePanel::onVariableEdited(const cad::param::Variable& var)
{
    for (auto& v : m_variables) {
        if (v.id == var.id) {
            v = var;
            break;
        }
    }
    recomputeFormulas();
    emit variableEdited(var);
}

void VariablePanel::onFormulaDeleted(const QUuid& id)
{
    m_formulas.erase(
        std::remove_if(m_formulas.begin(), m_formulas.end(),
            [&id](const cad::param::FormulaVariable& f) { return f.id == id; }),
        m_formulas.end());
    rebuildFormulaCards();
    recomputeFormulas();
    emit formulaDeleted(id);
}

void VariablePanel::onFormulaEdited(const cad::param::FormulaVariable& formula)
{
    for (auto& f : m_formulas) {
        if (f.id == formula.id) {
            f.name = formula.name;
            f.expression = formula.expression;
            f.comment = formula.comment;
            f.conditions = formula.conditions;
            f.conditionsEnabled = formula.conditionsEnabled;
            break;
        }
    }
    recomputeFormulas();
    emit formulaEdited(formula);
}

void VariablePanel::onConditionsEditRequested(const QUuid& id)
{
    int index = -1;
    for (int i = 0; i < m_formulas.size(); ++i) {
        if (m_formulas[i].id == id) { index = i; break; }
    }
    if (index < 0)
        return;

    // Known variables (cm) under both display name and reference name.
    QHash<QString, double> known;
    for (const auto& v : m_variables) {
        const double cm = cad::geo::Units::mmToCm(v.value);
        if (!v.name.isEmpty())
            known.insert(v.name, cm);
        if (!v.refName.isEmpty())
            known.insert(v.refName, cm);
    }

    const cad::param::FormulaVariable& f = m_formulas[index];
    ConditionDialog dlg(f.name, f.expression, f.conditions, known, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_formulas[index].conditions = dlg.conditions();
    m_formulas[index].conditionsEnabled = true;

    for (auto* card : m_formulaCards) {
        if (card->formulaId() == id) {
            card->setConditions(m_formulas[index].conditions, true);
            break;
        }
    }

    recomputeFormulas();
    emit formulaEdited(m_formulas[index]);
}

// ============================================================
// Formula evaluation
// ============================================================

void VariablePanel::recomputeFormulas()
{
    // Base value map (cm): variables under display name + reference name.
    QHash<QString, double> baseMap;
    for (const auto& v : m_variables) {
        const double cm = cad::geo::Units::mmToCm(v.value);
        if (!v.name.isEmpty())
            baseMap.insert(v.name, cm);
        if (!v.refName.isEmpty())
            baseMap.insert(v.refName, cm);
    }

    // Condition table: formulaName -> conditions (enabled & non-empty only).
    QHash<QString, QList<cad::param::Condition>> condByName;
    for (const auto& f : m_formulas) {
        if (f.conditionsEnabled && !f.conditions.isEmpty() && !f.name.isEmpty())
            condByName.insert(f.name, f.conditions);
    }

    // Fixpoint iteration so formulas may reference other formulas regardless of
    // order. ConditionEngine applies a formula's conditions ONLY on standalone
    // references; composite references use base values (no propagation).
    const int passes = qMax(1, static_cast<int>(m_formulas.size()));
    for (int pass = 0; pass < passes; ++pass) {
        bool progressed = false;
        for (auto& f : m_formulas) {
            const auto r = cad::param::ConditionEngine::evaluate(
                f.expression, baseMap, condByName);
            if (r.ok) {
                f.valid = true;
                f.error.clear();
                f.baseValue = cad::geo::Units::cmToMm(r.value);
                if (!f.name.isEmpty()) {
                    auto it = baseMap.find(f.name);
                    if (it == baseMap.end()) {
                        baseMap.insert(f.name, r.value);
                        progressed = true;
                    } else if (qAbs(it.value() - r.value) > 1e-9) {
                        it.value() = r.value;
                        progressed = true;
                    }
                }
            } else {
                f.valid = false;
                f.error = r.error;
            }
        }
        if (!progressed)
            break;
    }

    // Final adjusted values (conditions applied) for display + standalone use.
    for (auto& f : m_formulas) {
        if (!f.valid) continue;
        const double baseCm = cad::geo::Units::mmToCm(f.baseValue);
        const double adjCm = condByName.contains(f.name)
            ? cad::param::ConditionEngine::applyConditions(baseCm, f.conditions, baseMap)
            : baseCm;
        f.value = cad::geo::Units::cmToMm(adjCm);
    }

    // Push adjusted results into the visible cards.
    for (auto* card : m_formulaCards) {
        for (const auto& f : m_formulas) {
            if (f.id == card->formulaId()) {
                card->setResult(f.valid, cad::geo::Units::mmToCm(f.value), f.error);
                break;
            }
        }
    }

    emit formulasRecomputed();
}

// ============================================================
// Card list rebuilding
// ============================================================

void VariablePanel::rebuildVariableCards()
{
    for (auto* card : m_varCards) {
        m_varLayout->removeWidget(card);
        card->deleteLater();
    }
    m_varCards.clear();

    int insertPos = m_varLayout->count() - 1;  // before trailing stretch
    for (int i = 0; i < m_variables.size(); ++i) {
        auto* card = new VariableCard(m_variables[i], i % 2 == 1, m_varContainer);
        m_varLayout->insertWidget(insertPos++, card);
        m_varCards.append(card);

        connect(card, &VariableCard::deleteRequested,
                this, &VariablePanel::onVariableDeleted);
        connect(card, &VariableCard::edited,
                this, &VariablePanel::onVariableEdited);
    }

    updateCountLabel();
}

void VariablePanel::rebuildFormulaCards()
{
    for (auto* card : m_formulaCards) {
        m_formulaLayout->removeWidget(card);
        card->deleteLater();
    }
    m_formulaCards.clear();

    int insertPos = m_formulaLayout->count() - 1;  // before trailing stretch
    for (int i = 0; i < m_formulas.size(); ++i) {
        auto* card = new FormulaCard(m_formulas[i], i % 2 == 1, m_formulaContainer);
        m_formulaLayout->insertWidget(insertPos++, card);
        m_formulaCards.append(card);

        connect(card, &FormulaCard::deleteRequested,
                this, &VariablePanel::onFormulaDeleted);
        connect(card, &FormulaCard::edited,
                this, &VariablePanel::onFormulaEdited);
        connect(card, &FormulaCard::conditionsEditRequested,
                this, &VariablePanel::onConditionsEditRequested);
    }

    updateCountLabel();
}

void VariablePanel::updateCountLabel()
{
    const bool onVarTab = (m_tabBar->currentIndex() == 0);
    m_countLabel->setText(QString::number(
        onVarTab ? m_variables.size() : m_formulas.size()));
    m_varEmptyHint->setVisible(m_variables.isEmpty());
    m_formulaEmptyHint->setVisible(m_formulas.isEmpty());
}
