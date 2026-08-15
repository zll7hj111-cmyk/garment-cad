#include "FormulaCard.h"

#include "CopyChip.h"
#include "IconHelper.h"
#include "geometry/Units.h"

#include "ElaLineEdit.h"
#include "ElaText.h"
#include "ElaToolButton.h"
#include "ElaCheckBox.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyleOption>
#include <QPainter>
#include <QMouseEvent>
#include <QLocale>
#include <QApplication>
#include <QDrag>
#include <QMimeData>

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
    , m_groupId(formula.groupId)
    , m_conditions(formula.conditions)
    , m_conditionsEnabled(formula.conditionsEnabled)
    , m_alternate(alternate)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setupUi(formula, alternate);
    setResult(formula.valid, cad::geo::Units::mmToCm(formula.value), formula.error);
}

cad::param::FormulaVariable FormulaCard::formula() const
{
    cad::param::FormulaVariable f;
    f.id = m_id;
    f.name = m_nameChip->text().trimmed();
    f.expression = m_exprEdit->text().trimmed();
    const QString at = m_actualEdit->text().trimmed();
    if (!at.isEmpty()) {
        bool ok = false;
        const double v = QLocale::c().toDouble(at, &ok);
        if (ok)
            f.actualValueCm = v;
    }
    f.comment = m_commentEdit->text().trimmed();
    f.conditions = m_conditions;
    f.conditionsEnabled = m_conditionsEnabled;
    f.groupId = m_groupId;
    return f;
}

void FormulaCard::focusName()
{
    m_nameChip->focusEdit();
}

void FormulaCard::setResult(bool ok, double valueCm, const QString& error)
{
    if (ok) {
        m_valueLabel->setText(QStringLiteral("= %1").arg(compactNumber(valueCm)));
        m_valueLabel->setToolTip(QStringLiteral("计算结果（只读）"));
        m_valueLabel->setStyleSheet(
            "font-family: 'Consolas','Courier New',monospace;"
            "font-size: 12px; font-weight: bold; background: transparent;");
    } else {
        m_valueLabel->setText(QStringLiteral("\u2717"));  // ✗
        m_valueLabel->setToolTip(error);
        m_valueLabel->setStyleSheet(
            "font-family: 'Consolas','Courier New',monospace;"
            "font-size: 12px; font-weight: bold; background: transparent;");
    }
}

void FormulaCard::setConditions(const QList<cad::param::Condition>& conds, bool enabled)
{
    m_conditions = conds;
    m_conditionsEnabled = enabled;
    updateCondRow();
}

void FormulaCard::setIndex(int index)
{
    m_indexLabel->setText(QString::number(index));
}

void FormulaCard::syncFromModel(const cad::param::FormulaVariable& f)
{
    m_groupId = f.groupId;
    m_nameChip->setText(f.name);
    if (!m_exprEdit->hasFocus()) {
        m_exprEdit->blockSignals(true);
        m_exprEdit->setText(f.expression);
        m_exprEdit->blockSignals(false);
    }
    if (!m_actualEdit->hasFocus()) {
        m_actualEdit->blockSignals(true);
        m_actualEdit->setText(f.actualValueCm.has_value()
            ? QString::number(*f.actualValueCm, 'f', 2) : QString());
        m_actualEdit->blockSignals(false);
    }
    m_commentEdit->blockSignals(true);
    m_commentEdit->setText(f.comment);
    m_commentEdit->blockSignals(false);
    m_conditions = f.conditions;
    m_conditionsEnabled = f.conditionsEnabled;
    updateCondRow();
    updateExprEnabled();
    setResult(f.valid, cad::geo::Units::mmToCm(f.value), f.error);
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
        m_condInfo->setStyleSheet("font-size: 10px; background: transparent;");
    } else {
        m_condInfo->setText(QString::fromUtf8("（双击添加）"));
        m_condInfo->setStyleSheet("font-size: 10px; background: transparent;");
    }
}

void FormulaCard::onCondToggled(bool checked)
{
    if (m_condGuard) return;
    m_conditionsEnabled = checked;
    emit edited(formula());
}

void FormulaCard::updateExprEnabled()
{
    const bool hasActual = !m_actualEdit->text().trimmed().isEmpty();
    m_exprEdit->setEnabled(!hasActual);
}

void FormulaCard::paintEvent(QPaintEvent*)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    // Left accent bar — 行交替竖线: 偶数行蓝 / 奇数行橙 (2026-08 用户拍板
    // 统一蓝橙交替, 替代原类型色条与背景斑马纹).
    p.setPen(Qt::NoPen);
    p.setBrush(m_alternate ? QColor(0xF5, 0x9E, 0x0B)   // 橙
                           : QColor(0x2F, 0x6F, 0xED)); // 蓝
    p.drawRoundedRect(0, 2, 3, height() - 4, 1.5, 1.5);
}

void FormulaCard::enterEvent(QEnterEvent*)
{
    m_deleteBtn->setVisible(true);
    m_deleteBtnSlot->setVisible(false);
}

void FormulaCard::leaveEvent(QEvent*)
{
    m_deleteBtn->setVisible(false);
    m_deleteBtnSlot->setVisible(true);
}

bool FormulaCard::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (obj == m_condRow || obj == m_condInfo) {
            emit conditionsEditRequested(m_id);
            return true;
        }
    }

    // Drag handle: press + move beyond the threshold starts a card drag.
    if (obj == m_indexLabel) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragStartPos = me->pos();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if ((me->buttons() & Qt::LeftButton)
                && (me->pos() - m_dragStartPos).manhattanLength()
                       >= QApplication::startDragDistance()) {
                auto* mime = new QMimeData();
                mime->setData(kDragMimeType, m_id.toByteArray());
                auto* drag = new QDrag(this);
                drag->setMimeData(mime);
                drag->setPixmap(grab());
                drag->setHotSpot(m_indexLabel->geometry().center());
                drag->exec(Qt::MoveAction);
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

void FormulaCard::setupUi(const cad::param::FormulaVariable& formula, bool alternate)
{
    setObjectName(QStringLiteral("FormulaCard"));
    (void)alternate;  // 竖线颜色已按 alternate 存为 m_alternate (构造时).

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 7, 8, 7);
    mainLayout->setSpacing(3);

    // === Header row ===
    auto* header = new QHBoxLayout();
    header->setSpacing(6);

    m_indexLabel = new ElaText(QString(), 13, this);
    m_indexLabel->setAlignment(Qt::AlignCenter);
    m_indexLabel->setFixedWidth(18);
    m_indexLabel->setCursor(Qt::OpenHandCursor);
    m_indexLabel->setToolTip(QStringLiteral("拖动排序"));
    m_indexLabel->setStyleSheet(
        "QLabel { font-size: 10px; font-weight: bold;"
        "  background: transparent; border-radius: 3px; }"
        "QLabel:hover { background: rgba(0,0,0,0.06); }");
    m_indexLabel->installEventFilter(this);
    header->addWidget(m_indexLabel, 0);

    m_nameChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Formula, this);
    m_nameChip->setPlaceholderText(QStringLiteral("变量名"));
    m_nameChip->setText(formula.name);
    header->addWidget(m_nameChip, 1);

    m_valueLabel = new ElaText(QString(), 13, this);
    m_valueLabel->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace;"
        "font-size: 12px; background: transparent;");
    header->addWidget(m_valueLabel, 0);

    m_condDot = new ElaText(QStringLiteral("\u25CF"), 13, this);  // ●
    m_condDot->setStyleSheet("font-size: 8px; background: transparent;");
    m_condDot->setVisible(false);
    header->addWidget(m_condDot, 0);

    // 悬停占位: 与删除按钮同尺寸, 二者互斥显隐 → 布局空间恒定,
    // 按钮出现/消失不引起行宽挤压或行高变化 (VirtualCardList 不重测).
    m_deleteBtnSlot = new QWidget(this);
    m_deleteBtnSlot->setFixedSize(20, 20);
    header->addWidget(m_deleteBtnSlot, 0);

    m_deleteBtn = new ElaToolButton(this);
    m_deleteBtn->setIcon(cad::ui::IconHelper::icon2State(
        QStringLiteral("trash"), QColor(0xB0, 0xB0, 0xB0), Qt::white));
    m_deleteBtn->setIconSize(QSize(12, 12));
    m_deleteBtn->setToolTip(QStringLiteral("删除公式变量"));
    m_deleteBtn->setFixedSize(20, 20);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setVisible(false);
    header->addWidget(m_deleteBtn, 0);

    mainLayout->addLayout(header);

    // === Detail ===
    m_detail = new QWidget(this);
    m_detail->setVisible(true);
    auto* detailLayout = new QVBoxLayout(m_detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(3);

    // Expression.
    m_exprEdit = new ElaLineEdit(m_detail);     m_exprEdit->setText(formula.expression);
    m_exprEdit->setPlaceholderText(QStringLiteral("表达式，如: 胸围/2+6"));
    m_exprEdit->setFixedHeight(22);
    m_exprEdit->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace; font-size: 12px;");
    detailLayout->addWidget(m_exprEdit);

    // Actual value + condition row (compact).
    auto* optRow = new QHBoxLayout();
    optRow->setSpacing(4);

    m_actualEdit = new ElaLineEdit(m_detail);
    if (formula.actualValueCm.has_value())
        m_actualEdit->setText(QString::number(*formula.actualValueCm, 'f', 2));
    m_actualEdit->setPlaceholderText(QStringLiteral("实际值(留空=按公式)"));
    m_actualEdit->setFixedHeight(22);
    m_actualEdit->setFixedWidth(68);
    m_actualEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    optRow->addWidget(m_actualEdit, 0);

    // Condition widgets (inline, compact).
    m_condRow = new QWidget(m_detail);
    m_condRow->setFixedHeight(22);
    m_condRow->setCursor(Qt::PointingHandCursor);
    m_condRow->installEventFilter(this);
    auto* condLayout = new QHBoxLayout(m_condRow);
    condLayout->setContentsMargins(0, 0, 0, 0);
    condLayout->setSpacing(3);

    m_condCheck = new ElaCheckBox(QString::fromUtf8("条件"), m_condRow);
    m_condCheck->setCursor(Qt::PointingHandCursor);
    condLayout->addWidget(m_condCheck, 0);

    m_condInfo = new ElaText(QString(), 13, m_condRow);
    m_condInfo->installEventFilter(this);
    condLayout->addWidget(m_condInfo, 1);

    m_condEditBtn = new ElaToolButton(m_condRow);
    m_condEditBtn->setIcon(cad::ui::IconHelper::iconByName(
        QStringLiteral("funnel"), QColor(0x6C, 0x34, 0x83)));
    m_condEditBtn->setIconSize(QSize(12, 12));
    m_condEditBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_condEditBtn->setFixedSize(20, 20);
    m_condEditBtn->setCursor(Qt::PointingHandCursor);
    m_condEditBtn->setToolTip(QStringLiteral("编辑条件"));
    condLayout->addWidget(m_condEditBtn, 0);

    optRow->addWidget(m_condRow, 1);
    detailLayout->addLayout(optRow);

    // Comment.
    m_commentEdit = new ElaLineEdit(m_detail);     m_commentEdit->setText(formula.comment);
    m_commentEdit->setPlaceholderText(QStringLiteral("注释…"));
    m_commentEdit->setFixedHeight(22);
    detailLayout->addWidget(m_commentEdit);

    mainLayout->addWidget(m_detail);

    // === Init ===
    updateCondRow();
    updateExprEnabled();

    // === Connections ===
    connect(m_deleteBtn, &QToolButton::clicked, this,
            [this]() { emit deleteRequested(m_id); });
    connect(m_nameChip, &cad::ui::CopyChip::edited, this,
            [this](const QString&) { emit edited(this->formula()); });
    connect(m_exprEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(this->formula()); });
    connect(m_actualEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { updateExprEnabled(); });
    connect(m_actualEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(this->formula()); });
    connect(m_commentEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(this->formula()); });
    connect(m_condCheck, &QCheckBox::toggled, this, &FormulaCard::onCondToggled);
    connect(m_condEditBtn, &QToolButton::clicked, this,
            [this]() { emit conditionsEditRequested(m_id); });
}
