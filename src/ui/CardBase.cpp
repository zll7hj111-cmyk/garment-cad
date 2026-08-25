#include "CardBase.h"

#include <cmath>

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaToolButton.h"
#include <QHBoxLayout>
#include <QStyleOption>
#include <QPainter>
#include <QToolButton>

#include "CopyChip.h"
#include "IconHelper.h"
#include "Theme.h"

CardBase::CardBase(bool alternate, QWidget* parent)
    : QWidget(parent)
    , m_alternate(alternate)
{
    setAttribute(Qt::WA_StyledBackground, true);
}

void CardBase::setIndex(int n)
{
    if (!m_indexLabel) return;
    // Pure presentation: only touch the label when the ordinal changed.
    const QString text = indexText(n);
    if (m_indexLabel->text() != text)
        m_indexLabel->setText(text);
}

QString CardBase::indexText(int n) const
{
    return n > 0 ? QString::number(n) : QString();
}

void CardBase::setAlternate(bool alternate)
{
    if (m_alternate == alternate)
        return;
    m_alternate = alternate;
    update();
}

int CardBase::accentBarX() const
{
    return 0;
}

ElaText* CardBase::createIndexLabel(const QString& objectName, const QString& tooltip)
{
    m_indexLabel = new ElaText(QString(), 13, this);
    m_indexLabel->setObjectName(objectName);
    m_indexLabel->setStyleSheet(
        QStringLiteral("font-size: %1px; background: transparent;")
            .arg(cad::ui::ThemeTokens::FontSm));
    m_indexLabel->setToolTip(tooltip);
    return m_indexLabel;
}

cad::ui::CopyChip* CardBase::createNameChip(cad::ui::CopyChip::Variant variant,
                                            const QString& placeholder,
                                            const QString& text)
{
    m_nameChip = new cad::ui::CopyChip(variant, this);
    m_nameChip->setPlaceholderText(placeholder);
    m_nameChip->setText(text);
    return m_nameChip;
}

ElaText* CardBase::createValueLabel(bool bold)
{
    m_valueLabel = new ElaText(QString(), 13, this);
    m_valueLabel->setStyleSheet(
        QStringLiteral("%1font-size: %2px;%3 background: transparent;")
            .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                 QString::number(cad::ui::ThemeTokens::FontMd),
                 bold ? QStringLiteral(" font-weight: bold;") : QString()));
    return m_valueLabel;
}

void CardBase::setupLockIcon(const QString& tooltip)
{
    m_lockIcon = new ElaText(QString(), 13, this);
    m_lockIcon->setText(QStringLiteral("\xF0\x9F\x94\x92"));  // 🔒
    m_lockIcon->setStyleSheet(
        QStringLiteral("font-size: %1px; background: transparent;")
            .arg(cad::ui::ThemeTokens::FontXs));
    m_lockIcon->setToolTip(tooltip);
    m_lockIcon->setFixedWidth(16);
}

void CardBase::appendDeleteButton(QHBoxLayout* header, const QString& tooltip)
{
    // 悬停占位: 与删除按钮同尺寸, 二者互斥显隐 → 布局空间恒定,
    // 按钮出现/消失不引起行宽挤压或行高变化 (VirtualCardList 不重测).
    m_deleteBtnSlot = new QWidget(this);
    m_deleteBtnSlot->setFixedSize(20, 20);
    header->addWidget(m_deleteBtnSlot, 0);

    m_deleteBtn = new ElaToolButton(this);
    m_deleteBtn->setIcon(cad::ui::IconHelper::icon2State(
        QStringLiteral("trash"), QColor(0xB0, 0xB0, 0xB0), Qt::white));
    m_deleteBtn->setIconSize(QSize(12, 12));
    m_deleteBtn->setToolTip(tooltip);
    m_deleteBtn->setFixedSize(20, 20);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setVisible(false);  // revealed on card hover
    header->addWidget(m_deleteBtn, 0);
}

ElaLineEdit* CardBase::createCommentEdit(QWidget* parent)
{
    auto* edit = new ElaLineEdit(parent);
    edit->setPlaceholderText(QStringLiteral("注释…"));
    edit->setFixedHeight(22);
    return edit;
}

void CardBase::buildReadOnlySkeleton(const ReadOnlySkeletonSpec& spec,
                                     std::function<void()> onDelete,
                                     std::function<void()> onEdited)
{
    setObjectName(spec.objectName);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 7, 8, 7);
    mainLayout->setSpacing(3);

    // === Header row ===
    auto* header = new QHBoxLayout();
    header->setSpacing(6);

    // 视图行序号 (虚拟化跨行复用, 每次 (re)bind 重设 — 见 setIndex).
    header->addWidget(createIndexLabel(spec.indexObjectName, spec.indexTooltip), 0);

    header->addWidget(createNameChip(cad::ui::CopyChip::Variant::Name,
                                     spec.namePlaceholder, spec.nameText), 1);

    m_valueLabel = createValueLabel();
    header->addWidget(m_valueLabel, 0);

    // Lock icon (read-only indicator)
    setupLockIcon(spec.lockTooltip);
    header->addWidget(m_lockIcon, 0);

    appendDeleteButton(header, spec.deleteTooltip);

    mainLayout->addLayout(header);

    // === Detail row ===
    m_detail = new QWidget(this);
    m_detail->setVisible(true);
    auto* detailLayout = new QHBoxLayout(m_detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(6);

    m_refChip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Ref, m_detail);
    m_refChip->setPlaceholderText(QString());  // 无引用名时保持纯空, 不显示占位文字
    m_refChip->setText(spec.refName);
    m_refChip->setCopyEnabled(true);
    m_refChip->setFixedWidth(spec.refChipWidth);
    detailLayout->addWidget(m_refChip, 0);

    m_sourceInfo = new ElaText(QString(), 13, m_detail);
    m_sourceInfo->setText(spec.sourceLabel);
    m_sourceInfo->setStyleSheet(cad::ui::ThemeTokens::kCaptionSm);
    m_sourceInfo->setToolTip(spec.sourceTooltip);
    detailLayout->addWidget(m_sourceInfo, 0);

    m_commentEdit = createCommentEdit(m_detail);
    m_commentEdit->setText(spec.commentText);
    detailLayout->addWidget(m_commentEdit, 1);

    mainLayout->addWidget(m_detail);

    // === Connections ===
    connect(m_deleteBtn, &QToolButton::clicked, this, std::move(onDelete));
    connect(m_nameChip, &cad::ui::CopyChip::edited, this, std::move(onEdited));
    connect(m_commentEdit, &QLineEdit::editingFinished, this, std::move(onEdited));
}

void CardBase::setValueLabelDangling(bool dangling, const QString& tooltip)
{
    if (dangling) {
        m_valueLabel->setStyleSheet(
            QStringLiteral("font-size: %1px; font-weight: bold; background: transparent;")
                .arg(cad::ui::ThemeTokens::FontSm));
        m_valueLabel->setToolTip(tooltip);
    } else {
        m_valueLabel->setStyleSheet(
            QStringLiteral("%1font-size: %2px; font-weight: bold; background: transparent;")
                .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                     QString::number(cad::ui::ThemeTokens::FontMd)));
        m_valueLabel->setToolTip(QString());
    }
}

bool CardBase::refreshValueGuard(double value, bool dangling)
{
    if (m_hasShownValue && dangling == m_danglingStyled &&
        (dangling || std::abs(value - m_lastValue) < 1e-3))
        return true;
    m_hasShownValue = true;
    m_lastValue = value;
    return false;
}

void CardBase::paintEvent(QPaintEvent*)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    const auto& tk = cad::ui::Theme::tokens();

    // 1px technical border around the card for crisp industrial separation
    p.setPen(QPen(tk.border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0));

    // Left accent bar — 行交替竖线: 偶数行蓝 / 奇数行橙
    p.setPen(Qt::NoPen);
    p.setBrush(m_alternate ? tk.rowOdd : tk.rowEven);
    const int barX = accentBarX();
    p.drawRoundedRect(barX, 2, 3, height() - 4, 1.0, 1.0);
}

void CardBase::enterEvent(QEnterEvent*)
{
    m_deleteBtn->setVisible(true);
    m_deleteBtnSlot->setVisible(false);
}

void CardBase::leaveEvent(QEvent*)
{
    m_deleteBtn->setVisible(false);
    m_deleteBtnSlot->setVisible(true);
}
