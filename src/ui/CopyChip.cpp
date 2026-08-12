#include "CopyChip.h"

#include "ElaText.h"
#include "ElaLineEdit.h"
#include <QVBoxLayout>
#include <QClipboard>
#include <QApplication>
#include <QStyle>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>

namespace cad::ui {

CopyChip::CopyChip(Variant variant, QWidget* parent)
    : QWidget(parent)
    , m_variant(variant)
{
    // Label is the only layout child — it defines the widget's natural size.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_label = new ElaText(QString(), 13, this);
    m_label->setObjectName(QStringLiteral("chipLabel"));
    m_label->setProperty("variant", variantKey());
    m_label->setCursor(Qt::PointingHandCursor);
    m_label->installEventFilter(this);
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
                m_edit->blockSignals(true);
                m_edit->setText(up);
                m_edit->setCursorPosition(cursor);
                m_edit->blockSignals(false);
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
        ? QString::fromUtf8("双击编辑")
        : QString::fromUtf8("单击复制 · 双击编辑"));
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
        "QLabel { font-size: 11px; font-weight: bold; color: #16A34A;"
        "  background: #E9F7EF; border-radius: 2px; padding: 0 2px; }");
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
