#include "FormulaCard.h"

#include "CopyChip.h"
#include "IconHelper.h"
#include "geometry/Units.h"
#include "Theme.h"
#include "TooltipFormatter.h"
#include "ui/NoteButton.h"

#include "ElaLineEdit.h"
#include "ElaText.h"
#include "ElaToolButton.h"
#include "ElaCheckBox.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QLocale>
#include <QApplication>
#include <QDrag>
#include <QMimeData>

FormulaCard::FormulaCard(const cad::param::FormulaVariable& formula,
                         bool alternate, QWidget* parent)
    : CardBase(alternate, parent)
    , m_id(formula.id)
    , m_groupId(formula.groupId)
    , m_conditions(formula.conditions)
    , m_conditionsEnabled(formula.conditionsEnabled)
{
    setAccentRole(cad::ui::CardAccent::Formula);  // 公式 = 深青竖线 (方案 A)
    setupUi(formula);
    setResult(formula.valid, cad::geo::Units::mmToCm(formula.value), formula.error);
}

cad::param::FormulaVariable FormulaCard::formula() const
{
    cad::param::FormulaVariable f;
    f.id = m_id;
    f.name = m_nameChip ? m_nameChip->text().trimmed() : QString();
    f.expression = m_exprEdit ? m_exprEdit->text().trimmed() : QString();
    const QString at = m_actualEdit ? m_actualEdit->text().trimmed() : QString();
    if (!at.isEmpty()) {
        bool ok = false;
        const double v = QLocale::c().toDouble(at, &ok);
        if (ok)
            f.actualValueCm = v;
    }
    f.comment = m_noteBtn ? m_noteBtn->note() : QString();
    f.conditions = m_conditions;
    f.conditionsEnabled = m_conditionsEnabled;
    f.groupId = m_groupId;
    return f;
}

void FormulaCard::focusName()
{
    if (m_nameChip)
        m_nameChip->focusEdit();
}

void FormulaCard::setResult(bool ok, double valueCm, const QString& error)
{
    if (!m_valueLabel) return;
    if (ok) {
        m_valueLabel->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: %2px; font-weight: 600; color: %3; background: transparent;")
            .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                 QString::number(cad::ui::ThemeTokens::FontMd),
                 cad::ui::Theme::tokens().text1.name()));
        m_valueLabel->setText(
            QStringLiteral("= %1").arg(cad::geo::Units::formatNumberTrimmed(valueCm)));
        m_valueLabel->setToolTip(cad::ui::TooltipFormatter::status(
            QStringLiteral("计算结果"),
            QStringLiteral("%1 cm（只读计算值）").arg(cad::geo::Units::formatNumberTrimmed(valueCm)),
            false));
    } else {
        m_valueLabel->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: %2px; font-weight: 600; color: %3; background: transparent;")
            .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                 QString::number(cad::ui::ThemeTokens::FontSm),
                 cad::ui::Theme::tokens().danger.name()));
        m_valueLabel->setText(QStringLiteral("! 错误"));
        m_valueLabel->setToolTip(cad::ui::TooltipFormatter::status(
            QStringLiteral("公式求值失败"),
            error.isEmpty() ? QStringLiteral("表达式无效或引用的变量不存在") : error,
            true));
    }
}

void FormulaCard::setConditions(const QList<cad::param::Condition>& conds, bool enabled)
{
    m_conditions = conds;
    m_conditionsEnabled = enabled;
    updateCondRow();
}

void FormulaCard::setGrouped(bool grouped)
{
    if (m_grouped == grouped)
        return;
    m_grouped = grouped;
    m_mainLayout->setContentsMargins((m_grouped ? kGroupIndent : 0) + 12, 5, 8, 5);
    update();
}

void FormulaCard::syncFromModel(const cad::param::FormulaVariable& f)
{
    m_groupId = f.groupId;
    if (m_nameChip)
        m_nameChip->setText(f.name);
    if (m_exprEdit && !m_exprEdit->hasFocus()) {
        m_exprEdit->blockSignals(true);
        m_exprEdit->setText(f.expression);
        m_exprEdit->blockSignals(false);
    }
    if (m_actualEdit && !m_actualEdit->hasFocus()) {
        m_actualEdit->blockSignals(true);
        m_actualEdit->setText(f.actualValueCm.has_value()
            ? cad::geo::Units::formatNumberTrimmed(*f.actualValueCm) : QString());
        m_actualEdit->blockSignals(false);
    }
    setCommentSilently(f.comment);
    m_conditions = f.conditions;
    m_conditionsEnabled = f.conditionsEnabled;
    updateCondRow();
    updateExprEnabled();
    setResult(f.valid, cad::geo::Units::mmToCm(f.value), f.error);
}

void FormulaCard::updateCondRow()
{
    const bool has = !m_conditions.isEmpty();
    if (!m_condCheck || !m_condInfo) return;

    m_condCheck->setEnabled(has);
    m_condGuard = true;
    m_condCheck->setChecked(has && m_conditionsEnabled);
    m_condGuard = false;

    if (has) {
        m_condInfo->setText(QStringLiteral("(%1条)").arg(m_conditions.size()));
    } else {
        m_condInfo->setText(QStringLiteral("（条件）"));
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
    if (!m_exprEdit || !m_actualEdit) return;
    const bool hasActual = !m_actualEdit->text().trimmed().isEmpty();
    m_exprEdit->setEnabled(!hasActual);
}

int FormulaCard::accentBarX() const
{
    return m_grouped ? kGroupIndent : 0;
}

bool FormulaCard::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (obj == m_condRow || obj == m_condInfo) {
            emit conditionsEditRequested(m_id);
            return true;
        }
    }

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

void FormulaCard::setupUi(const cad::param::FormulaVariable& formula)
{
    setObjectName(QStringLiteral("FormulaCard"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins((m_grouped ? kGroupIndent : 0) + 12, 5, 8, 5);
    mainLayout->setSpacing(3);
    m_mainLayout = mainLayout;

    // === Main Tier (推导式主轨) ===
    auto* mainRow = new QHBoxLayout();
    mainRow->setSpacing(5);

    // 拖拽把手兼序号 (保留 cardIndex 测试契约)
    m_indexLabel = createIndexLabel(QStringLiteral("cardIndex"),
                                    QStringLiteral("拖动排序"));
    m_indexLabel->setAlignment(Qt::AlignCenter);
    m_indexLabel->setFixedWidth(18);
    m_indexLabel->setCursor(Qt::OpenHandCursor);
    m_indexLabel->setStyleSheet(
        "QLabel { font-size: 10px; font-weight: bold;"
        "  background: transparent; border-radius: 3px; }"
        "QLabel:hover { background: rgba(0,0,0,0.06); }");
    m_indexLabel->installEventFilter(this);
    mainRow->addWidget(m_indexLabel, 0);

    // 公式变量名（放宽宽度以展示更完整名称）
    m_nameChip = createNameChip(cad::ui::CopyChip::Variant::Formula,
                                QStringLiteral("公式名"), formula.name);
    m_nameChip->setFixedWidth(105);
    mainRow->addWidget(m_nameChip, 0);

    // 等号
    auto* eqLabel = new ElaText(QStringLiteral("="), 12, this);
    eqLabel->setObjectName(QStringLiteral("dimText"));
    eqLabel->setFixedWidth(10);
    eqLabel->setAlignment(Qt::AlignCenter);
    mainRow->addWidget(eqLabel, 0);

    // 表达式输入框
    m_exprEdit = new ElaLineEdit(this);
    m_exprEdit->setText(formula.expression);
    m_exprEdit->setPlaceholderText(QStringLiteral("表达式，如: 胸围/2+6"));
    m_exprEdit->setFixedHeight(22);
    m_exprEdit->setStyleSheet(cad::ui::ThemeTokens::kMonospaceMd);
    mainRow->addWidget(m_exprEdit, 1);

    // 计算结果 Badge (createValueLabel)
    m_valueLabel = createValueLabel(/*bold=*/false);
    mainRow->addWidget(m_valueLabel, 0);

    // 删除按钮
    appendDeleteButton(mainRow, QStringLiteral("删除公式变量"));

    mainLayout->addLayout(mainRow);

    // === Meta Tier (副轨元信息胶囊行) ===
    auto* metaRow = new QHBoxLayout();
    metaRow->setSpacing(5);

    // 实际覆盖值
    m_actualEdit = new ElaLineEdit(this);
    if (formula.actualValueCm.has_value())
        m_actualEdit->setText(cad::geo::Units::formatNumberTrimmed(*formula.actualValueCm));
    m_actualEdit->setPlaceholderText(QStringLiteral("实际覆盖"));
    m_actualEdit->setFixedHeight(20);
    m_actualEdit->setFixedWidth(64);
    m_actualEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_actualEdit->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("实际覆盖值"),
        QStringLiteral("填入数值可临时覆盖公式求值结果；清空则恢复公式计算")));
    metaRow->addWidget(m_actualEdit, 0);

    // 条件分支组件
    m_condRow = new QWidget(this);
    m_condRow->setFixedHeight(20);
    m_condRow->setCursor(Qt::PointingHandCursor);
    m_condRow->installEventFilter(this);
    auto* condLayout = new QHBoxLayout(m_condRow);
    condLayout->setContentsMargins(0, 0, 0, 0);
    condLayout->setSpacing(2);

    m_condCheck = new ElaCheckBox(QStringLiteral("条件"), m_condRow);
    m_condCheck->setCursor(Qt::PointingHandCursor);
    condLayout->addWidget(m_condCheck, 0);

    m_condInfo = new ElaText(QString(), 11, m_condRow);
    m_condInfo->installEventFilter(this);
    condLayout->addWidget(m_condInfo, 0);

    m_condEditBtn = new ElaToolButton(m_condRow);
    m_condEditBtn->setIcon(cad::ui::IconHelper::iconByName(
        QStringLiteral("funnel"), QColor(0x6C, 0x34, 0x83)));
    m_condEditBtn->setIconSize(QSize(11, 11));
    m_condEditBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_condEditBtn->setFixedSize(18, 18);
    m_condEditBtn->setCursor(Qt::PointingHandCursor);
    m_condEditBtn->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("条件分支规则"),
        QStringLiteral("打开条件规则对话框，配置公式在不同条件下的增减修正量")));
    condLayout->addWidget(m_condEditBtn, 0);

    metaRow->addWidget(m_condRow, 0);

    // 注释便利贴按钮 (NoteButton, 20px 高度与元信息行对齐)
    m_noteBtn = createNoteButton(this, 20);
    m_noteBtn->setPlaceholder(QStringLiteral("公式说明…"));
    m_noteBtn->setNote(formula.comment);
    metaRow->addWidget(m_noteBtn, 0);
    metaRow->addStretch(1);

    mainLayout->addLayout(metaRow);

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
    connect(m_noteBtn, &cad::ui::NoteButton::noteEdited, this,
            [this](const QString&) { emit edited(this->formula()); });
    connect(m_condCheck, &QCheckBox::toggled, this, &FormulaCard::onCondToggled);
    connect(m_condEditBtn, &QToolButton::clicked, this,
            [this]() { emit conditionsEditRequested(m_id); });
}
