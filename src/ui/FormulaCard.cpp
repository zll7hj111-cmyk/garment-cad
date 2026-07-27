#include "FormulaCard.h"

#include "CopyChip.h"

#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyleOption>
#include <QPainter>
#include <QMouseEvent>

namespace {

QString compactNumber(double v)
{
    QString s = QString::number(v, 'f', 2);
    while (s.endsWith(QLatin1Char('0'))) s.chop(1);
    if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    return s;
}

} // namespace

FormulaCard::FormulaCard(const cad::param::FormulaVariable& formula,
                         bool alternate, QWidget* parent)
    : QWidget(parent)
    , m_id(formula.id)
    , m_conditions(formula.conditions)
    , m_conditionsEnabled(formula.conditionsEnabled)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setupUi(formula, alternate);
}

cad::param::FormulaVariable FormulaCard::formula() const
{
    cad::param::FormulaVariable f;
    f.id = m_id;
    f.name = m_nameChip->text().trimmed();
    f.expression = m_exprEdit->text().trimmed();
    f.comment = m_commentEdit->text().trimmed();
    f.conditions = m_conditions;
    f.conditionsEnabled = m_conditionsEnabled;
    return f;
}

void FormulaCard::focusName()
{
    m_nameChip->focusEdit();
}

void FormulaCard::setExpanded(bool on)
{
    m_expanded = on;
    m_detail->setVisible(on);
    m_arrow->setText(on ? QStringLiteral("\u25BE") : QStringLiteral("\u25B8"));
}

void FormulaCard::toggleExpanded()
{
    setExpanded(!m_expanded);
}

void FormulaCard::setResult(bool ok, double valueCm, const QString& error)
{
    if (ok) {
        m_valueLabel->setText(QStringLiteral("= %1").arg(compactNumber(valueCm)));
        m_valueLabel->setToolTip(QStringLiteral("计算结果（只读）"));
        m_valueLabel->setStyleSheet(
            "font-size: 11px; font-weight: bold; color: #1E8449; background: transparent;");
    } else {
        m_valueLabel->setText(QStringLiteral("\u2717"));  // ✗
        m_valueLabel->setToolTip(error);
        m_valueLabel->setStyleSheet(
            "font-size: 11px; font-weight: bold; color: #C0392B; background: transparent;");
    }
}

void FormulaCard::setConditions(const QList<cad::param::Condition>& conds, bool enabled)
{
    m_conditions = conds;
    m_conditionsEnabled = enabled;
    updateCondRow();
}

void FormulaCard::updateCondRow()
{
    const bool has = !m_conditions.isEmpty();

    // Header dot indicator.
    m_condDot->setVisible(has);
    m_condDot->setToolTip(has
        ? QStringLiteral("%1条条件%2").arg(m_conditions.size())
              .arg(m_conditionsEnabled ? QString() : QStringLiteral("（已停用）"))
        : QString());

    // Detail row widgets.
    m_condCheck->setEnabled(has);
    m_condGuard = true;
    m_condCheck->setChecked(has && m_conditionsEnabled);
    m_condGuard = false;

    if (has) {
        m_condInfo->setText(QStringLiteral("(%1条)").arg(m_conditions.size()));
        m_condInfo->setStyleSheet("font-size: 10px; color: #6C3483; background: transparent;");
    } else {
        m_condInfo->setText(QString::fromUtf8("（双击添加）"));
        m_condInfo->setStyleSheet("font-size: 10px; color: #ABB2B9; background: transparent;");
    }
}

void FormulaCard::onCondToggled(bool checked)
{
    if (m_condGuard) return;
    m_conditionsEnabled = checked;
    emit edited(formula());
}

void FormulaCard::paintEvent(QPaintEvent*)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

bool FormulaCard::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        if (obj == m_arrow) { toggleExpanded(); return true; }
    }
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (obj == m_condRow || obj == m_condInfo) {
            emit conditionsEditRequested(m_id);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void FormulaCard::setupUi(const cad::param::FormulaVariable& formula, bool alternate)
{
    setObjectName(QStringLiteral("FormulaCard"));
    const QString bg = alternate ? QStringLiteral("#F4F6F7") : QStringLiteral("#FFFFFF");
    setStyleSheet(QStringLiteral(
        "QWidget#FormulaCard {"
        "  background-color: %1;"
        "  border: 1px solid #E5E8E8;"
        "  border-radius: 4px;"
        "}"
        "QWidget#FormulaCard:hover { border: 1px solid #8E44AD; }"
    ).arg(bg));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 2, 4, 2);
    mainLayout->setSpacing(0);

    // === Header row (always visible) ===
    auto* header = new QHBoxLayout();
    header->setSpacing(4);

    m_arrow = new QLabel(QStringLiteral("\u25B8"), this);
    m_arrow->setFixedWidth(12);
    m_arrow->setCursor(Qt::PointingHandCursor);
    m_arrow->setStyleSheet("font-size: 10px; color: #85929E; background: transparent;");
    m_arrow->setAlignment(Qt::AlignCenter);
    m_arrow->installEventFilter(this);
    header->addWidget(m_arrow, 0);

    m_nameChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Formula, this);
    m_nameChip->setPlaceholderText(QStringLiteral("变量名"));
    m_nameChip->setText(formula.name);
    header->addWidget(m_nameChip, 1);

    m_valueLabel = new QLabel(this);
    m_valueLabel->setStyleSheet("font-size: 11px; color: #85929E; background: transparent;");
    header->addWidget(m_valueLabel, 0);

    m_condDot = new QLabel(QStringLiteral("\u25CF"), this);  // ●
    m_condDot->setStyleSheet("font-size: 8px; color: #8E44AD; background: transparent;");
    m_condDot->setVisible(false);
    header->addWidget(m_condDot, 0);

    m_deleteBtn = new QToolButton(this);
    m_deleteBtn->setText(QStringLiteral("\u2715"));
    m_deleteBtn->setToolTip(QStringLiteral("删除公式变量"));
    m_deleteBtn->setFixedSize(14, 14);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setStyleSheet(
        "QToolButton { font-size: 9px; color: #B0B0B0;"
        "  background: transparent; border: none; border-radius: 7px; }"
        "QToolButton:hover { color: #FFFFFF; background: #E74C3C; }");
    header->addWidget(m_deleteBtn, 0);

    mainLayout->addLayout(header);

    // === Detail (collapsible) ===
    m_detail = new QWidget(this);
    m_detail->setVisible(false);
    auto* detailLayout = new QVBoxLayout(m_detail);
    detailLayout->setContentsMargins(16, 1, 0, 1);
    detailLayout->setSpacing(1);

    // Expression.
    m_exprEdit = new QLineEdit(formula.expression, m_detail);
    m_exprEdit->setPlaceholderText(QStringLiteral("表达式，如: 胸围/2+6"));
    m_exprEdit->setFixedHeight(16);
    m_exprEdit->setStyleSheet(
        "QLineEdit { font-family: 'Consolas','Courier New',monospace;"
        "  font-size: 11px; color: #4A235A;"
        "  background: #F8F5FB; border: 1px solid #E8DAEF; border-radius: 3px; padding: 0 3px; }"
        "QLineEdit:hover { background: #FFF; }"
        "QLineEdit:focus { border: 1px solid #8E44AD; background: #FFF; }");
    detailLayout->addWidget(m_exprEdit);

    // Condition row.
    m_condRow = new QWidget(m_detail);
    m_condRow->setFixedHeight(16);
    m_condRow->setCursor(Qt::PointingHandCursor);
    m_condRow->installEventFilter(this);
    auto* condLayout = new QHBoxLayout(m_condRow);
    condLayout->setContentsMargins(0, 0, 0, 0);
    condLayout->setSpacing(3);

    m_condCheck = new QCheckBox(QString::fromUtf8("条件"), m_condRow);
    m_condCheck->setCursor(Qt::PointingHandCursor);
    m_condCheck->setStyleSheet(
        "QCheckBox { font-size: 10px; color: #6C3483; }"
        "QCheckBox:disabled { color: #ABB2B9; }");
    condLayout->addWidget(m_condCheck, 0);

    m_condInfo = new QLabel(m_condRow);
    m_condInfo->installEventFilter(this);
    condLayout->addWidget(m_condInfo, 1);

    m_condEditBtn = new QToolButton(m_condRow);
    m_condEditBtn->setText(QString::fromUtf8("编辑"));
    m_condEditBtn->setFixedHeight(14);
    m_condEditBtn->setCursor(Qt::PointingHandCursor);
    m_condEditBtn->setStyleSheet(
        "QToolButton { font-size: 9px; color: #6C3483;"
        "  background: transparent; border: 1px solid #D7BDE2;"
        "  border-radius: 3px; padding: 0 4px; }"
        "QToolButton:hover { background: #F4ECF7; border: 1px solid #8E44AD; }");
    condLayout->addWidget(m_condEditBtn, 0);

    detailLayout->addWidget(m_condRow);

    // Comment.
    m_commentEdit = new QLineEdit(formula.comment, m_detail);
    m_commentEdit->setPlaceholderText(QStringLiteral("注释…"));
    m_commentEdit->setFixedHeight(16);
    m_commentEdit->setStyleSheet(
        "QLineEdit { font-size: 10px; font-style: italic; color: #85929E;"
        "  background: transparent; border: 1px solid transparent; border-radius: 3px; padding: 0 3px; }"
        "QLineEdit:hover { border: 1px solid #B0BEC5; background: #FFF; }"
        "QLineEdit:focus { border: 1px solid #8E44AD; background: #FFF; }");
    detailLayout->addWidget(m_commentEdit);

    mainLayout->addWidget(m_detail);

    // === Init ===
    updateCondRow();

    // === Connections ===
    connect(m_deleteBtn, &QToolButton::clicked, this,
            [this]() { emit deleteRequested(m_id); });
    connect(m_nameChip, &cad::ui::CopyChip::edited, this,
            [this](const QString&) { emit edited(this->formula()); });
    connect(m_exprEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(this->formula()); });
    connect(m_commentEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(this->formula()); });
    connect(m_condCheck, &QCheckBox::toggled, this, &FormulaCard::onCondToggled);
    connect(m_condEditBtn, &QToolButton::clicked, this,
            [this]() { emit conditionsEditRequested(m_id); });
}
