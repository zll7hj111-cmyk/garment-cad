#include "ConditionDialog.h"

#include "Theme.h"
#include "parametric/ExpressionEvaluator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include "ElaScrollArea.h"
#include "ElaComboBox.h"
#include "ElaCheckBox.h"
#include "ElaDoubleSpinBox.h"
#include "ElaPushButton.h"
#include "ElaText.h"
#include <QFrame>
#include "ui/ElaDialogButtons.h"

namespace {

ElaDoubleSpinBox* makeSpin(QWidget* parent, double value = 0.0)
{
    auto* s = new ElaDoubleSpinBox(parent);
    s->setRange(-99999.0, 99999.0);
    s->setDecimals(2);
    s->setSuffix(QStringLiteral(" cm"));
    s->setSingleStep(0.5);
    s->setValue(value);
    s->setFixedWidth(86);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    s->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return s;
}

ElaText* makeText(QWidget* parent, const QString& text)
{
    auto* l = new ElaText(text, 13, parent);
    const auto& tk = cad::ui::Theme::tokens();
    l->setStyleSheet(QStringLiteral("font-size: 12px; color: %1; background: transparent;")
                         .arg(tk.text2.name()));
    return l;
}

} // namespace

ConditionDialog::ConditionDialog(const QString& formulaName,
                                 const QString& expression,
                                 const QList<cad::param::Condition>& conditions,
                                 const QHash<QString, double>& knownVars,
                                 QWidget* parent)
    : ElaDialog(parent)
    , m_knownVars(knownVars)
{
    setWindowTitle(QStringLiteral("条件修正 · %1").arg(
        formulaName.isEmpty() ? QStringLiteral("公式") : formulaName));
    setMinimumWidth(560);
    resize(640, 420);

    m_vars = availableVars(expression);
    setupUi(formulaName, expression);

    for (const auto& c : conditions)
        addRow(c);
    if (m_rows.isEmpty())
        addRow(cad::param::Condition{});
}

QStringList ConditionDialog::availableVars(const QString& expression) const
{
    QStringList out;
    const QStringList refs = cad::param::ExpressionEvaluator::referencedNames(expression);
    for (const QString& r : refs) {
        if (r.isEmpty() || out.contains(r))
            continue;
        if (m_knownVars.contains(r))
            out.append(r);
    }
    return out;
}

void ConditionDialog::setupUi(const QString& formulaName, const QString& expression)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    // Header / explanation — theme tokens (dark-mode safe).
    const auto& tk = cad::ui::Theme::tokens();
    auto* title = new ElaText(QStringLiteral("公式：%1").arg(
        formulaName.isEmpty() ? QStringLiteral("（未命名）") : formulaName), 13, this);
    title->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: bold;"));
    root->addWidget(title);

    auto* exprLbl = new ElaText(QStringLiteral("表达式：%1").arg(
        expression.isEmpty() ? QStringLiteral("（空）") : expression), 13, this);
    exprLbl->setStyleSheet(QStringLiteral(
        "font-family: 'Consolas','Courier New',monospace;"
        "font-size: 12px; background: %1;"
        "border: 1px solid %2; border-radius: 4px; padding: 3px 6px;")
        .arg(tk.surface2.name(), tk.border.name()));
    root->addWidget(exprLbl);

    auto* hint = new ElaText(QString(), 13, this);
    if (m_vars.isEmpty()) {
        hint->setText(QString::fromUtf8(
            "⚠ 表达式没有引用任何已知变量，无法设置条件。\n"
            "条件变量必须是表达式中用到的变量（名称或引用名）。"));
        hint->setStyleSheet(QStringLiteral(
            "font-size: 12px; background: %1;"
            "border: 1px solid %2; border-radius: 4px; padding: 6px;")
            .arg(QStringLiteral("rgba(%1,%2,%3,30)")
                     .arg(tk.warning.red()).arg(tk.warning.green()).arg(tk.warning.blue()),
                 QStringLiteral("rgba(%1,%2,%3,110)")
                     .arg(tk.warning.red()).arg(tk.warning.green()).arg(tk.warning.blue())));
    } else {
        hint->setText(QString::fromUtf8(
            "当被监视的变量落在区间内时，对结果做修正。多条条件叠加。\n"
            "可选变量：%1").arg(m_vars.join(QStringLiteral("、"))));
        hint->setStyleSheet("font-size: 11px; background: transparent; padding: 2px 0;");
        hint->setWordWrap(true);
    }
    root->addWidget(hint);

    // Scrollable rows.
    auto* scroll = new ElaScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto* container = new QWidget();
    m_rowsLayout = new QVBoxLayout(container);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(6);
    m_rowsLayout->addStretch();
    scroll->setWidget(container);
    root->addWidget(scroll, 1);

    // Add button.
    auto* addBtn = new ElaPushButton(QStringLiteral("＋ 添加条件"), this);
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setEnabled(!m_vars.isEmpty());
    connect(addBtn, &QPushButton::clicked, this,
            [this]() { addRow(cad::param::Condition{}); });
    root->addWidget(addBtn, 0, Qt::AlignLeft);

    // Dialog buttons.
    const auto btns = cad::ui::makeDialogButtons(this);
    QObject::disconnect(btns.ok, nullptr, this, nullptr);  // accept is replaced below
    QObject::connect(btns.ok, &ElaPushButton::clicked, this, &ConditionDialog::collectAndAccept);
    root->addWidget(btns.row);
}

ConditionDialog::Row* ConditionDialog::buildRow(const cad::param::Condition& cond)
{
    auto* row = new Row();
    row->cond = cond;

    row->widget = new QWidget(this);
    const auto& tk = cad::ui::Theme::tokens();
    row->widget->setStyleSheet(QStringLiteral(
        "background: %1; border: 1px solid %2; border-radius: 6px;")
        .arg(tk.surface3.name(), tk.border.name()));
    auto* lay = new QHBoxLayout(row->widget);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->setSpacing(5);

    // Watched variable.
    row->watch = new ElaComboBox(row->widget);
    row->watch->addItems(m_vars);
    const int idx = m_vars.indexOf(cond.watchVar);
    row->watch->setCurrentIndex(idx >= 0 ? idx : 0);
    row->watch->setToolTip(QString::fromUtf8("被监视的变量（表达式中引用的）"));
    lay->addWidget(makeText(row->widget, QStringLiteral("变量")));
    lay->addWidget(row->watch, 0);

    // Lower bound.
    row->lowerOn = new ElaCheckBox(QStringLiteral("≥"), row->widget);
    row->lowerOn->setChecked(cond.lowerOn);
    row->lower = makeSpin(row->widget, cond.lower);
    row->lower->setEnabled(cond.lowerOn);
    connect(row->lowerOn, &QCheckBox::toggled, this,
            [row](bool on) { row->lower->setEnabled(on); });
    lay->addWidget(row->lowerOn, 0);
    lay->addWidget(row->lower, 0);

    // Upper bound.
    row->upperOn = new ElaCheckBox(QStringLiteral("≤"), row->widget);
    row->upperOn->setChecked(cond.upperOn);
    row->upper = makeSpin(row->widget, cond.upper);
    row->upper->setEnabled(cond.upperOn);
    connect(row->upperOn, &QCheckBox::toggled, this,
            [row](bool on) { row->upper->setEnabled(on); });
    lay->addWidget(row->upperOn, 0);
    lay->addWidget(row->upper, 0);

    // Mode.
    row->mode = new ElaComboBox(row->widget);
    row->mode->addItem(QString::fromUtf8("一次性"), int(cad::param::AdjustMode::Flat));
    row->mode->addItem(QString::fromUtf8("逐档"), int(cad::param::AdjustMode::PerStep));
    row->mode->setCurrentIndex(cond.mode == cad::param::AdjustMode::PerStep ? 1 : 0);
    lay->addWidget(row->mode, 0);

    // Step (PerStep only).
    row->stepLbl = makeText(row->widget, QString::fromUtf8("每"));
    row->step = makeSpin(row->widget, cond.step > 0 ? cond.step : 1.0);
    lay->addWidget(row->stepLbl, 0);
    lay->addWidget(row->step, 0);

    // Amount.
    lay->addWidget(makeText(row->widget, QString::fromUtf8("则结果")));
    row->amount = makeSpin(row->widget, cond.amount);
    row->amount->setToolTip(QString::fromUtf8("修正量（cm，可为负）"));
    lay->addWidget(row->amount, 0);

    lay->addStretch();

    // Remove.
    row->remove = new ElaPushButton(QStringLiteral("✕"), row->widget);
    row->remove->setFixedSize(22, 22);
    row->remove->setCursor(Qt::PointingHandCursor);
    row->remove->setToolTip(QString::fromUtf8("删除该条件"));
    connect(row->remove, &QPushButton::clicked, this,
            [this, row]() { removeRow(row); });
    lay->addWidget(row->remove, 0);

    syncMode(row);
    connect(row->mode, &QComboBox::currentIndexChanged, this,
            [this, row](int) { syncMode(row); });

    return row;
}

void ConditionDialog::syncMode(Row* row)
{
    const bool perStep =
        row->mode->currentData().toInt() == int(cad::param::AdjustMode::PerStep);
    row->step->setVisible(perStep);
    row->stepLbl->setVisible(perStep);
}

void ConditionDialog::addRow(const cad::param::Condition& cond)
{
    auto* row = buildRow(cond);
    m_rows.append(row);
    m_rowsLayout->insertWidget(m_rowsLayout->count() - 1, row->widget);
}

void ConditionDialog::removeRow(Row* row)
{
    m_rows.removeOne(row);
    m_rowsLayout->removeWidget(row->widget);
    row->widget->deleteLater();
    delete row;
}

void ConditionDialog::collectAndAccept()
{
    m_result.clear();
    for (auto* row : m_rows) {
        cad::param::Condition c = row->cond;   // preserve id
        c.watchVar = row->watch->currentText().trimmed();
        if (c.watchVar.isEmpty())
            continue;
        c.lowerOn = row->lowerOn->isChecked();
        c.lower   = row->lower->value();
        c.upperOn = row->upperOn->isChecked();
        c.upper   = row->upper->value();
        c.mode    = row->mode->currentData().toInt() == int(cad::param::AdjustMode::PerStep)
                        ? cad::param::AdjustMode::PerStep
                        : cad::param::AdjustMode::Flat;
        c.step    = row->step->value();
        c.amount  = row->amount->value();
        m_result.append(c);
    }
    accept();
}
