#include "CopyChip.h"

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "Theme.h"
#include "TooltipFormatter.h"
#include <QVBoxLayout>
#include <QClipboard>
#include <QSignalBlocker>
#include <QApplication>
#include <QStyle>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>

namespace cad::ui {

// ============================================================
// ChipLabel — 常驻输入框样式的承载控件
//
// 背景 + 描边画在 label 的 paintEvent 里 (与文本同一绘制上下文)。
// 为什么不在 chip 本体画: 2026-08 排查确认 chip 本体的 paintEvent 被
// Qt 合成器以空 clip 调用 —— visibleRegion / ev->region() / QPainter
// device / engine / deviceTransform 全部正常, 唯独 painter clip 为
// 0x0, 所有自绘静默丢弃 (grab / render / 屏幕全不可见), 而其子控件
// (ElaText label) 的绘制路径正常。故描边由 label 绘制, 编辑态覆盖层
// (ElaLineEdit) 收起后描边仍在, 空值/占位态也不会"空一块"。
// ============================================================

ChipLabel::ChipLabel(const QString& text, int pixelSize, CopyChip* host,
                     QWidget* parent)
    : ElaText(text, pixelSize, parent)
    , m_host(host)
{
}

void ChipLabel::setHovered(bool hovered)
{
    if (m_hovered == hovered)
        return;
    m_hovered = hovered;
    update();  // 重绘描边底色
}

void ChipLabel::paintEvent(QPaintEvent* event)
{
    // 先画输入框样式 (背景 + 1px 圆角描边), 再让 ElaText 画文本。
    // 描边用明显可见的中灰: borderStrong 在画布纸色底上太浅 (用户反馈
    // "看不出描边"), 亮/暗各取一档更深的专用色。
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& t = cad::ui::Theme::tokens();
    p.setPen(QPen(t.chipBorder, 1));
    p.setBrush(m_hovered ? t.surface2 : t.surface);
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 3, 3);
    ElaText::paintEvent(event);
}

// ============================================================
// CopyChip
// ============================================================

CopyChip::CopyChip(Variant variant, QWidget* parent)
    : QWidget(parent)
    , m_variant(variant)
{
    // Label is the only layout child — it defines the widget's natural size.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_label = new ChipLabel(QString(), 13, this, this);
    m_label->setObjectName(QStringLiteral("chipLabel"));
    m_label->setProperty("variant", variantKey());
    m_label->setCursor(Qt::PointingHandCursor);
    m_label->installEventFilter(this);
    // 静态态输入框样式 (浅色底+细边框, Theme QSS): 空文本时也要保持
    // 完整输入框高度, 不能塌成细条 (ElaText 不保证认 QSS min-height).
    m_label->setMinimumHeight(16);
    layout->addWidget(m_label);

    // Edit overlay: hidden, positioned on top of label when editing.
    m_edit = new ElaLineEdit(this);
    // ElaLineEdit's constructor hard-codes setFixedHeight(35), which would
    // defeat enterEdit()'s setGeometry(label rect): the overlay would stay
    // 35px tall while the chip row is only ~18px, pushing the text ~8px
    // below the label baseline (visible as "text squeezed down + clipped"
    // while typing) and jittering on focus-in. Lift the constraint so the
    // overlay matches the label's geometry exactly.
    m_edit->setMinimumHeight(0);
    m_edit->setMaximumHeight(QWIDGETSIZE_MAX);
    m_edit->setObjectName(QStringLiteral("chipEdit"));
    m_edit->setProperty("variant", variantKey());
    m_edit->hide();
    m_edit->installEventFilter(this);

    m_clickTimer = new QTimer(this);
    m_clickTimer->setSingleShot(true);
    m_clickTimer->setInterval(200);
    connect(m_clickTimer, &QTimer::timeout, this, &CopyChip::copyText);
    connect(m_edit, &QLineEdit::editingFinished, this, &CopyChip::commitEdit);

    // Reference names are uppercase by convention: convert live as the user
    // types (typing "b" immediately shows "B"), preserving the caret position.
    if (m_variant == Variant::Ref) {
        connect(m_edit, &QLineEdit::textChanged, this, [this](const QString& text) {
            const QString up = text.toUpper();
            if (up != text) {
                const int cursor = m_edit->cursorPosition();
                const QSignalBlocker blocker(m_edit);
                m_edit->setText(up);
                m_edit->setCursorPosition(cursor);
            }
        });
    }

    // Reference chips center their content; styles live in the global theme
    // QSS (chipLabel / chipEdit + [variant] attribute selector).
    if (m_variant == Variant::Ref) {
        m_label->setAlignment(Qt::AlignCenter);
        m_edit->setAlignment(Qt::AlignCenter);
    }

    updateDisplay();
}

void CopyChip::setText(const QString& text)
{
    // Reference names are uppercase by convention (separates them from the
    // lowercase reserved function names cos/sin/tan).
    const QString t = (m_variant == Variant::Ref) ? text.toUpper() : text;
    if (t == m_text) return;   // no-op guard: sync paths call this every frame
    m_text = t;
    updateDisplay();
}

void CopyChip::setPlaceholderText(const QString& ph)
{
    m_placeholder = ph;
    m_edit->setPlaceholderText(ph);
    updateDisplay();
}

void CopyChip::focusEdit()
{
    enterEdit();
}

bool CopyChip::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_label) {
        if (event->type() == QEvent::Enter) {
            setHovered(true);
            return false;  // 继续交给 label 默认处理 (光标/工具提示)
        }
        if (event->type() == QEvent::Leave) {
            setHovered(false);
            return false;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && m_copyEnabled && !m_text.isEmpty())
                m_clickTimer->start();
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            m_clickTimer->stop();
            enterEdit();
            return true;
        }
    } else if (obj == m_edit) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                commitEdit();
                return true;
            }
            if (ke->key() == Qt::Key_Escape) {
                m_edit->hide();
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut) {
            commitEdit();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void CopyChip::setHovered(bool hovered)
{
    if (m_hovered == hovered)
        return;
    m_hovered = hovered;
    // 描边/底色画在 label 的 paintEvent 里, hover 状态同步给 label 重绘。
    m_label->setHovered(hovered);
}

void CopyChip::updateDisplay()
{
    // The placeholder/normal look is a QSS attribute (chipLabel[placeholder])
    // — flip the property and re-polish only when it actually changes.
    const bool showPlaceholder = m_text.isEmpty();
    if (showPlaceholder != m_placeholderStyled) {
        m_placeholderStyled = showPlaceholder;
        m_label->setProperty("placeholder", showPlaceholder);
        m_label->style()->unpolish(m_label);
        m_label->style()->polish(m_label);
    }
    m_label->setText(showPlaceholder ? m_placeholder : m_text);
    m_label->setToolTip(showPlaceholder
        ? cad::ui::TooltipFormatter::action(QStringLiteral("编辑项目"), QStringLiteral("双击设置名称"))
        : cad::ui::TooltipFormatter::action(QStringLiteral("名称 / 代号"), QStringLiteral("单击复制 · 双击编辑")));
}

void CopyChip::enterEdit()
{
    m_edit->setText(m_text);
    QRect geo = m_label->geometry();
    geo.adjust(-3, -1, 3, 1);  // compensate for border + padding so text isn't clipped
    m_edit->setGeometry(geo);
    m_edit->show();
    m_edit->raise();
    m_edit->setFocus();
    m_edit->selectAll();
}

void CopyChip::commitEdit()
{
    if (!m_edit->isVisible())
        return;
    QString t = m_edit->text().trimmed();
    if (m_variant == Variant::Ref)
        t = t.toUpper();
    // 覆盖层 (ElaLineEdit) 隐藏后, 其覆盖区域重新暴露 —— Qt 会自动重绘
    // 下方 label (描边+文本在 label 的 paintEvent 里, 正常上屏)。
    m_edit->hide();
    const bool changed = (t != m_text);
    m_text = t;
    updateDisplay();
    if (changed)
        emit edited(m_text);
}

void CopyChip::copyText()
{
    if (m_text.isEmpty()) return;
    QApplication::clipboard()->setText(m_text);
    emit copied(m_text);

    m_label->setText(QString::fromUtf8("✓ 已复制"));
    m_label->setStyleSheet(
        Theme::badgeStyle(Theme::tokens().success, "QLabel"));
    QTimer::singleShot(800, this, [this]() {
        m_label->setStyleSheet(QString());  // clear the flash, back to global QSS
        m_placeholderStyled = false;
        updateDisplay();
    });
}

QString CopyChip::variantKey() const
{
    switch (m_variant) {
    case Variant::Name:    return QStringLiteral("name");
    case Variant::Ref:     return QStringLiteral("ref");
    case Variant::Formula: return QStringLiteral("formula");
    }
    return QStringLiteral("name");
}

} // namespace cad::ui
