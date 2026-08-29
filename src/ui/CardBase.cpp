#include "CardBase.h"

#include <cmath>

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaToolButton.h"
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QStyleOption>
#include <QPainter>
#include <QToolButton>

#include "CopyChip.h"
#include "IconHelper.h"
#include "Theme.h"

namespace {

/// 悬停占位槽 (与删除按钮同尺寸互斥显隐, 防布局跳动)。默认态不再纯空白:
/// 画一枚 24% 透明度的 ✕ 轮廓 (§4.3), 消除「空白→出现」的视觉跳变感。
class GhostDeleteSlot : public QWidget
{
public:
    explicit GhostDeleteSlot(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(20, 20);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QColor c = cad::ui::Theme::tokens().text3;
        c.setAlphaF(0.24);  // 24% 透明 ✕ 轮廓
        QPen pen(c, 1.4);
        pen.setCapStyle(Qt::FlatCap);
        p.setPen(pen);
        p.drawLine(6.5, 6.5, 13.5, 13.5);
        p.drawLine(13.5, 6.5, 6.5, 13.5);
    }
};

} // namespace

CardBase::CardBase(bool alternate, QWidget* parent)
    : QWidget(parent)
    , m_alternate(alternate)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_Hover, true);  // 悬停描边转 borderStrong (§6.2)
}

void CardBase::setAccentRole(cad::ui::CardAccent role)
{
    if (m_accentRole == role)
        return;
    m_accentRole = role;
    update();
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
    // 行奇偶不再驱动视觉 (类型色竖线, 方案 A) — 仅保留虚拟列表 bind 契约。
    m_alternate = alternate;
}

int CardBase::accentBarX() const
{
    return 0;
}

ElaText* CardBase::createIndexLabel(const QString& objectName, const QString& tooltip)
{
    // §4.3: 序号 = 10px 等宽 text3 (FontXs), 图纸编号感。
    m_indexLabel = new ElaText(QString(), 13, this);
    m_indexLabel->setObjectName(objectName);
    m_indexLabel->setStyleSheet(
        QStringLiteral("font-size: %1px; background: transparent; %2")
            .arg(QString::number(cad::ui::ThemeTokens::FontXs),
                 cad::ui::ThemeTokens::kMonospaceFamily));
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
    // §4.3 值区强化: 15px Semibold 等宽 (FontLg) — 卡片第一视觉焦点。
    m_valueLabel = new ElaText(QString(), 13, this);
    m_valueLabel->setStyleSheet(
        QStringLiteral("%1font-size: %2px;%3 background: transparent;")
            .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                 QString::number(cad::ui::ThemeTokens::FontLg),
                 QStringLiteral(" font-weight: %1;")
                     .arg(bold ? 600 : 500)));
    return m_valueLabel;
}

ElaText* CardBase::createUnitLabel(const QString& unit)
{
    // 单位缩小至 10px text3, 退居值之后的元信息位 (§4.3)。
    m_unitLabel = new ElaText(unit, 13, this);
    m_unitLabel->setStyleSheet(
        QStringLiteral("font-size: %1px; color: %2; background: transparent;")
            .arg(QString::number(cad::ui::ThemeTokens::FontXs),
                 cad::ui::Theme::tokens().text3.name()));
    return m_unitLabel;
}

void CardBase::appendDanglingBadge(QHBoxLayout* header)
{
    // ⚠ 引用失效 badge (§6.2 dangling 行): 值变色之外再显性化, 默认隐藏。
    m_danglingBadge = new ElaText(QStringLiteral("\u26A0"), 13, this);  // ⚠
    m_danglingBadge->setStyleSheet(
        QStringLiteral("font-size: %1px; color: %2; background: transparent;")
            .arg(QString::number(cad::ui::ThemeTokens::FontSm),
                 cad::ui::Theme::tokens().danger.name()));
    m_danglingBadge->setVisible(false);
    header->addWidget(m_danglingBadge, 0);
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
    // 占位默认画 24% 透明 ✕ 轮廓 (GhostDeleteSlot), 消除视觉跳变.
    m_deleteBtnSlot = new GhostDeleteSlot(this);
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

void CardBase::setCommentSilently(const QString& text)
{
    if (!m_commentEdit) return;
    if (m_commentEdit->hasFocus()) return;  // 用户正在编辑: 不打断输入
    const QSignalBlocker blocker(m_commentEdit);
    m_commentEdit->setText(text);
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

    // 单位 caption (spec.unit 为空则不占位) + ⚠ dangling badge。
    if (!spec.unit.isEmpty())
        header->addWidget(createUnitLabel(spec.unit), 0);
    appendDanglingBadge(header);

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
        // §6.2 dangling 行: 值区 danger 字 + danger 8% 浅底 + ⚠ badge 显性化。
        QColor wash = cad::ui::Theme::tokens().danger;
        wash.setAlphaF(0.08);
        m_valueLabel->setStyleSheet(
            QStringLiteral("font-size: %1px; font-weight: 600; color: %2;"
                           " background-color: rgba(%3,%4,%5,%6);")
                .arg(QString::number(cad::ui::ThemeTokens::FontSm),
                     cad::ui::Theme::tokens().danger.name(),
                     QString::number(wash.red()), QString::number(wash.green()),
                     QString::number(wash.blue()), QString::number(wash.alpha())));
        m_valueLabel->setToolTip(tooltip);
        if (m_danglingBadge) {
            m_danglingBadge->setToolTip(tooltip);
            m_danglingBadge->setVisible(true);
        }
    } else {
        m_valueLabel->setStyleSheet(
            QStringLiteral("%1font-size: %2px; font-weight: 600; background: transparent;")
                .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                     QString::number(cad::ui::ThemeTokens::FontLg)));
        m_valueLabel->setToolTip(QString());
        if (m_danglingBadge)
            m_danglingBadge->setVisible(false);
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

    // 1px technical border; hover 转描边 (§6.2: 默认 border / 悬停 borderStrong)
    p.setPen(QPen(m_hovered ? tk.borderStrong : tk.border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0));

    // Left accent bar — 类型色竖线 (方案 A): 变量=piece1 / 公式=piece2 /
    // 测量=piece3 / 关联=piece4。token 每帧现读, 主题切换免重绑。
    QColor bar;
    switch (m_accentRole) {
    case cad::ui::CardAccent::Variable: bar = tk.piece1; break;
    case cad::ui::CardAccent::Formula:  bar = tk.piece2; break;
    case cad::ui::CardAccent::Measure:  bar = tk.piece3; break;
    case cad::ui::CardAccent::Linked:   bar = tk.piece4; break;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(bar);
    const int barX = accentBarX();
    p.drawRoundedRect(barX, 2, 3, height() - 4, 1.0, 1.0);
}

void CardBase::enterEvent(QEnterEvent*)
{
    m_hovered = true;
    m_deleteBtn->setVisible(true);
    m_deleteBtnSlot->setVisible(false);
    update();  // 描边转 borderStrong
}

void CardBase::leaveEvent(QEvent*)
{
    m_hovered = false;
    m_deleteBtn->setVisible(false);
    m_deleteBtnSlot->setVisible(true);
    update();
}
