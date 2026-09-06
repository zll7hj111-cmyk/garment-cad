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
    const bool hasActual = m_actualEdit && !m_actualEdit->text().trimmed().isEmpty();
    const auto& tokens = cad::ui::Theme::tokens();
    if (ok) {
        const QColor valColor = hasActual ? tokens.warning : tokens.piece2;
        m_valueLabel->setStyleSheet(QStringLiteral(
            "font-family: %1; font-size: %2px; font-weight: 600; color: %3; background: transparent;")
            .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                 QString::number(cad::ui::ThemeTokens::FontLg),
                 valColor.name()));
        m_valueLabel->setText(cad::geo::Units::formatNumberTrimmed(valueCm));
        m_valueLabel->setToolTip(cad::ui::TooltipFormatter::status(
            hasActual ? QStringLiteral("实际覆盖结果") : QStringLiteral("计算结果"),
            QStringLiteral("%1 cm（%2）")
                .arg(cad::geo::Units::formatNumberTrimmed(valueCm),
                     hasActual ? QStringLiteral("实际覆盖值接管") : QStringLiteral("只读计算值")),
            false));
        if (m_unitLabel)
            m_unitLabel->setVisible(true);

        if (m_statusBadge) {
            if (hasActual) {
                m_statusBadge->setText(QStringLiteral("实际覆盖"));
                m_statusBadge->setStyleSheet(cad::ui::Theme::badgeStyle(tokens.warning, "QLabel"));
                m_statusBadge->setToolTip(cad::ui::TooltipFormatter::status(
                    QStringLiteral("实际覆盖中"),
                    QStringLiteral("当前填入了实际覆盖值，公式计算已被覆盖值临时接管"),
                    false));
            } else {
                m_statusBadge->setText(QStringLiteral("求值正常"));
                m_statusBadge->setStyleSheet(cad::ui::Theme::badgeStyle(tokens.success, "QLabel"));
                m_statusBadge->setToolTip(cad::ui::TooltipFormatter::status(
                    QStringLiteral("状态正常"),
                    QStringLiteral("公式求值成功，当前结果处于有效同步状态"),
                    false));
            }
        }
    } else {
        const bool isEmpty = m_exprEdit && m_exprEdit->text().trimmed().isEmpty();
        if (isEmpty) {
            m_valueLabel->setStyleSheet(QStringLiteral(
                "font-family: %1; font-size: %2px; font-weight: 600; color: %3; background: transparent;")
                .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                     QString::number(cad::ui::ThemeTokens::FontSm),
                     tokens.text3.name()));
            m_valueLabel->setText(QStringLiteral("—"));
            m_valueLabel->setToolTip(cad::ui::TooltipFormatter::status(
                QStringLiteral("公式未填写"),
                QStringLiteral("请在输入框中填入公式表达式（如：胸围/4 + 1.5）"),
                false));
            if (m_unitLabel)
                m_unitLabel->setVisible(false);

            if (m_statusBadge) {
                m_statusBadge->setText(QStringLiteral("待输入"));
                m_statusBadge->setStyleSheet(cad::ui::Theme::badgeStyle(tokens.text3, "QLabel"));
                m_statusBadge->setToolTip(cad::ui::TooltipFormatter::status(
                    QStringLiteral("待输入"),
                    QStringLiteral("公式表达式为空，请输入公式后自动求值"),
                    false));
            }
        } else {
            m_valueLabel->setStyleSheet(QStringLiteral(
                "font-family: %1; font-size: %2px; font-weight: 600; color: %3; background: transparent;")
                .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                     QString::number(cad::ui::ThemeTokens::FontSm),
                     tokens.danger.name()));
            m_valueLabel->setText(QStringLiteral("! 错误"));
            m_valueLabel->setToolTip(cad::ui::TooltipFormatter::status(
                QStringLiteral("公式求值失败"),
                error.isEmpty() ? QStringLiteral("表达式无效或引用的变量不存在") : error,
                true));
            if (m_unitLabel)
                m_unitLabel->setVisible(false);

            if (m_statusBadge) {
                m_statusBadge->setText(QStringLiteral("求值错误"));
                m_statusBadge->setStyleSheet(cad::ui::Theme::badgeStyle(tokens.danger, "QLabel"));
                m_statusBadge->setToolTip(cad::ui::TooltipFormatter::status(
                    QStringLiteral("公式错误"),
                    error.isEmpty() ? QStringLiteral("表达式无效或引用的变量不存在") : error,
                    true));
            }
        }
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
    if (!m_condCheck || !m_condBtn) return;

    m_condCheck->setVisible(has);
    m_condCheck->setEnabled(has);
    m_condGuard = true;
    m_condCheck->setChecked(has && m_conditionsEnabled);
    m_condGuard = false;

    const auto& tokens = cad::ui::Theme::tokens();
    if (has) {
        m_condBtn->setText(QStringLiteral("%1条").arg(m_conditions.size()));
        m_condBtn->setIcon(cad::ui::IconHelper::iconByName(
            QStringLiteral("funnel"),
            m_conditionsEnabled ? tokens.piece2 : tokens.text3));
        m_condBtn->setStyleSheet(QStringLiteral(
            "QToolButton { font-size: 10px; font-weight: bold; color: %1; background: %2; "
            "border: 1px solid %3; border-radius: 2px; padding: 0px 4px; }"
            "QToolButton:hover { background: %4; }")
            .arg((m_conditionsEnabled ? tokens.piece2 : tokens.text3).name(),
                 (m_conditionsEnabled ? tokens.piece2 : tokens.text3).name() + QStringLiteral("15"),
                 (m_conditionsEnabled ? tokens.piece2 : tokens.text3).name() + QStringLiteral("40"),
                 (m_conditionsEnabled ? tokens.piece2 : tokens.text3).name() + QStringLiteral("25")));
        m_condBtn->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("条件分支规则"),
            QStringLiteral("已配置 %1 条条件修正（点击编辑）").arg(m_conditions.size())));
    } else {
        m_condBtn->setText(QString());
        m_condBtn->setIcon(cad::ui::IconHelper::iconByName(
            QStringLiteral("funnel"), tokens.text3));
        m_condBtn->setStyleSheet(QStringLiteral(
            "QToolButton { font-size: 10px; color: %1; background: transparent; "
            "border: 1px solid transparent; border-radius: 2px; padding: 0px 2px; }"
            "QToolButton:hover { background: %2; border-color: %3; }")
            .arg(tokens.text3.name(),
                 tokens.surface2.name(),
                 tokens.border.name()));
        m_condBtn->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("添加条件规则"),
            QStringLiteral("打开条件规则对话框，配置公式在不同条件下的增减修正量")));
    }
}

void FormulaCard::onCondToggled(bool checked)
{
    if (m_condGuard) return;
    m_conditionsEnabled = checked;
    updateCondRow();
    emit edited(formula());
}

void FormulaCard::updateExprEnabled()
{
    if (!m_exprEdit || !m_actualEdit) return;
    const bool hasActual = !m_actualEdit->text().trimmed().isEmpty();
    m_exprEdit->setEnabled(!hasActual);

    const auto& tokens = cad::ui::Theme::tokens();
    if (hasActual) {
        m_exprEdit->setStyleSheet(QStringLiteral(
            "border: none; background: transparent; font-family: %1; font-size: 12px; color: %2; text-decoration: line-through;")
            .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                 tokens.text3.name()));
        m_actualEdit->setStyleSheet(QStringLiteral(
            "%1 font-weight: bold; color: %2; border: 1px solid %3;")
            .arg(cad::ui::ThemeTokens::kMonospaceMd,
                 tokens.warning.name(),
                 tokens.warning.name()));
        if (m_statusBadge) {
            m_statusBadge->setText(QStringLiteral("实际覆盖"));
            m_statusBadge->setStyleSheet(cad::ui::Theme::badgeStyle(tokens.warning, "QLabel"));
            m_statusBadge->setToolTip(cad::ui::TooltipFormatter::status(
                QStringLiteral("实际覆盖中"),
                QStringLiteral("当前填入了实际覆盖值，公式计算已被覆盖值临时接管"),
                false));
        }
    } else {
        m_exprEdit->setStyleSheet(QStringLiteral(
            "border: none; background: transparent; font-family: %1; font-size: 12px; color: %2;")
            .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                 tokens.text1.name()));
        m_actualEdit->setStyleSheet(cad::ui::ThemeTokens::kMonospaceMd);
        if (m_statusBadge) {
            const bool isEmpty = m_exprEdit && m_exprEdit->text().trimmed().isEmpty();
            if (isEmpty) {
                m_statusBadge->setText(QStringLiteral("待输入"));
                m_statusBadge->setStyleSheet(cad::ui::Theme::badgeStyle(tokens.text3, "QLabel"));
                m_statusBadge->setToolTip(cad::ui::TooltipFormatter::status(
                    QStringLiteral("待输入"),
                    QStringLiteral("公式表达式为空，请输入公式后自动求值"),
                    false));
            } else {
                m_statusBadge->setText(QStringLiteral("求值正常"));
                m_statusBadge->setStyleSheet(cad::ui::Theme::badgeStyle(tokens.success, "QLabel"));
                m_statusBadge->setToolTip(cad::ui::TooltipFormatter::status(
                    QStringLiteral("状态正常"),
                    QStringLiteral("公式求值成功，当前结果处于有效同步状态"),
                    false));
            }
        }
    }
}

int FormulaCard::accentBarX() const
{
    return m_grouped ? kGroupIndent : 0;
}

bool FormulaCard::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (obj == m_condRow) {
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

    const auto& tokens = cad::ui::Theme::tokens();

    // === Tier 1: Header (顶部状态栏: 序号 + 名称 + FORMULA 类型标 + 求值状态微标 + 删除) ===
    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(4);

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
    topRow->addWidget(m_indexLabel, 0);

    // 公式变量名
    m_nameChip = createNameChip(cad::ui::CopyChip::Variant::Formula,
                                QStringLiteral("公式名"), formula.name);
    m_nameChip->setFixedWidth(95);
    topRow->addWidget(m_nameChip, 0);

    // 类型徽标 [FORMULA]
    auto* typeTag = new ElaText(QStringLiteral("FORMULA"), 10, this);
    typeTag->setStyleSheet(QStringLiteral(
        "QLabel { font-family: %1; font-size: 9px; font-weight: bold; color: %2; "
        "background: %3; border: 1px solid %4; border-radius: 2px; padding: 0px 3px; }")
        .arg(cad::ui::ThemeTokens::kMonospaceFamily,
             tokens.text3.name(),
             tokens.surface2.name(),
             tokens.border.name()));
    topRow->addWidget(typeTag, 0);

    topRow->addStretch(1);

    // 求值状态微标 [求值正常 / 实际覆盖 / 求值错误 / 待输入]
    const bool isInitEmpty = formula.expression.trimmed().isEmpty();
    m_statusBadge = new ElaText(isInitEmpty ? QStringLiteral("待输入") : QStringLiteral("求值正常"), 10, this);
    m_statusBadge->setStyleSheet(cad::ui::Theme::badgeStyle(isInitEmpty ? tokens.text3 : tokens.success, "QLabel"));
    topRow->addWidget(m_statusBadge, 0);

    // 悬停删除按钮
    appendDeleteButton(topRow, QStringLiteral("删除公式变量"));

    mainLayout->addLayout(topRow);

    // === Tier 2: Code Formula Inset Box (中间代码块风格公式行) ===
    auto* codeBox = new QWidget(this);
    codeBox->setObjectName(QStringLiteral("formulaCodeBox"));
    codeBox->setFixedHeight(24);
    codeBox->setStyleSheet(QStringLiteral(
        "QWidget#formulaCodeBox { background: %1; border: 1px solid %2; border-radius: 2px; }")
        .arg(tokens.surface2.name(), tokens.border.name()));
    auto* codeLayout = new QHBoxLayout(codeBox);
    codeLayout->setContentsMargins(5, 1, 4, 1);
    codeLayout->setSpacing(4);

    // f = 前缀引导符
    auto* fLabel = new ElaText(QStringLiteral("f ="), 11, codeBox);
    fLabel->setFixedWidth(18);
    fLabel->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 11px; font-weight: bold; color: %2; background: transparent;")
        .arg(cad::ui::ThemeTokens::kMonospaceFamily,
             tokens.text3.name()));
    codeLayout->addWidget(fLabel, 0);

    // 表达式输入框
    m_exprEdit = new ElaLineEdit(codeBox);
    m_exprEdit->setText(formula.expression);
    m_exprEdit->setPlaceholderText(QStringLiteral("输入公式，如: 胸围/4+1.5"));
    m_exprEdit->setFixedHeight(20);
    m_exprEdit->setStyleSheet(QStringLiteral(
        "border: none; background: transparent; font-family: %1; font-size: 12px; color: %2;")
        .arg(cad::ui::ThemeTokens::kMonospaceFamily,
             tokens.text1.name()));
    codeLayout->addWidget(m_exprEdit, 1);

    mainLayout->addWidget(codeBox);

    // === Tier 3: Readout & Toolset (底部读数与工具栏: 计算值 + 覆盖 + 条件 + 注释) ===
    auto* bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->setSpacing(4);

    // 计算值标签
    auto* valCaption = new ElaText(QStringLiteral("计算值:"), 10, this);
    valCaption->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: 10px; font-weight: bold; color: %2; background: transparent;")
        .arg(cad::ui::ThemeTokens::kMonospaceFamily,
             tokens.text3.name()));
    bottomRow->addWidget(valCaption, 0);

    // 核心计算读数 (14px Semibold Monospace)
    m_valueLabel = createValueLabel(/*bold=*/true);
    bottomRow->addWidget(m_valueLabel, 0);

    // 单位标签 ("cm")
    m_unitLabel = createUnitLabel(QStringLiteral("cm"));
    bottomRow->addWidget(m_unitLabel, 0);

    bottomRow->addStretch(1);

    // 实际覆盖输入框
    m_actualEdit = new ElaLineEdit(this);
    if (formula.actualValueCm.has_value())
        m_actualEdit->setText(cad::geo::Units::formatNumberTrimmed(*formula.actualValueCm));
    m_actualEdit->setPlaceholderText(QStringLiteral("实际覆盖"));
    m_actualEdit->setFixedHeight(20);
    m_actualEdit->setFixedWidth(56);
    m_actualEdit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_actualEdit->setStyleSheet(cad::ui::ThemeTokens::kMonospaceMd);
    m_actualEdit->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("实际覆盖值"),
        QStringLiteral("填入数值可临时覆盖公式求值结果；清空则恢复公式计算")));
    bottomRow->addWidget(m_actualEdit, 0);

    // 条件分支组件
    m_condRow = new QWidget(this);
    m_condRow->setFixedHeight(20);
    m_condRow->installEventFilter(this);
    auto* condLayout = new QHBoxLayout(m_condRow);
    condLayout->setContentsMargins(0, 0, 0, 0);
    condLayout->setSpacing(2);

    m_condCheck = new ElaCheckBox(m_condRow);
    m_condCheck->setCursor(Qt::PointingHandCursor);
    m_condCheck->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("启用条件规则"),
        QStringLiteral("勾选启用此公式的条件规则修正量；取消勾选则暂不应用")));
    condLayout->addWidget(m_condCheck, 0);

    m_condBtn = new ElaToolButton(m_condRow);
    m_condBtn->setIcon(cad::ui::IconHelper::iconByName(
        QStringLiteral("funnel"), tokens.piece2));
    m_condBtn->setIconSize(QSize(11, 11));
    m_condBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_condBtn->setFixedHeight(18);
    m_condBtn->setCursor(Qt::PointingHandCursor);
    condLayout->addWidget(m_condBtn, 0);
    bottomRow->addWidget(m_condRow, 0);

    // 注释便利贴按钮
    m_noteBtn = createNoteButton(this, 20);
    m_noteBtn->setPlaceholder(QStringLiteral("公式说明…"));
    m_noteBtn->setNote(formula.comment);
    bottomRow->addWidget(m_noteBtn, 0);

    mainLayout->addLayout(bottomRow);

    // === Init ===
    updateCondRow();
    updateExprEnabled();

    // === Connections ===
    connect(m_deleteBtn, &QToolButton::clicked, this,
            [this]() { emit deleteRequested(m_id); });
    connect(m_nameChip, &cad::ui::CopyChip::edited, this,
            [this](const QString&) { emit edited(this->formula()); });
    connect(m_exprEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.trimmed().isEmpty() && m_statusBadge) {
            const auto& tokens = cad::ui::Theme::tokens();
            m_statusBadge->setText(QStringLiteral("待输入"));
            m_statusBadge->setStyleSheet(cad::ui::Theme::badgeStyle(tokens.text3, "QLabel"));
            if (m_valueLabel) {
                m_valueLabel->setText(QStringLiteral("—"));
                m_valueLabel->setStyleSheet(QStringLiteral(
                    "font-family: %1; font-size: %2px; font-weight: 600; color: %3; background: transparent;")
                    .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                         QString::number(cad::ui::ThemeTokens::FontSm),
                         tokens.text3.name()));
            }
        }
    });
    connect(m_exprEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(this->formula()); });
    connect(m_actualEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { updateExprEnabled(); });
    connect(m_actualEdit, &QLineEdit::editingFinished, this,
            [this]() { emit edited(this->formula()); });
    connect(m_noteBtn, &cad::ui::NoteButton::noteEdited, this,
            [this](const QString&) { emit edited(this->formula()); });
    connect(m_condCheck, &QCheckBox::toggled, this, &FormulaCard::onCondToggled);
    connect(m_condBtn, &QToolButton::clicked, this,
            [this]() { emit conditionsEditRequested(m_id); });
}
