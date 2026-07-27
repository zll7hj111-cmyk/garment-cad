#include "CopyChip.h"

#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QClipboard>
#include <QApplication>
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

    m_label = new QLabel(this);
    m_label->setCursor(Qt::PointingHandCursor);
    m_label->installEventFilter(this);
    layout->addWidget(m_label);

    // Edit overlay: hidden, positioned on top of label when editing.
    m_edit = new QLineEdit(this);
    m_edit->hide();
    m_edit->installEventFilter(this);

    m_clickTimer = new QTimer(this);
    m_clickTimer->setSingleShot(true);
    m_clickTimer->setInterval(200);
    connect(m_clickTimer, &QTimer::timeout, this, &CopyChip::copyText);
    connect(m_edit, &QLineEdit::editingFinished, this, &CopyChip::commitEdit);

    applyStyle();
    updateDisplay();
}

void CopyChip::setText(const QString& text)
{
    m_text = text;
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

void CopyChip::applyStyle()
{
    switch (m_variant) {
    case Variant::Name:
    case Variant::Formula:
        m_label->setStyleSheet(
            "QLabel { font-size: 12px; font-weight: bold; color: #C0392B;"
            "  background: transparent; padding: 0 2px; }"
            "QLabel:hover { background: #FDEDEC; border-radius: 2px; }");
        m_edit->setStyleSheet(
            "QLineEdit { font-size: 12px; font-weight: bold; color: #C0392B;"
            "  background: #FFFFFF; border: 1px solid #3498DB;"
            "  border-radius: 2px; padding: 0 2px; }");
        break;
    case Variant::Ref:
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setStyleSheet(
            "QLabel { font-family: 'Consolas','Courier New',monospace;"
            "  font-size: 11px; color: #1A5276; background: #EBF5FB;"
            "  border-radius: 2px; padding: 0 2px; }"
            "QLabel:hover { background: #D6EAF8; }");
        m_edit->setAlignment(Qt::AlignCenter);
        m_edit->setStyleSheet(
            "QLineEdit { font-family: 'Consolas','Courier New',monospace;"
            "  font-size: 11px; color: #1A5276; background: #FFFFFF;"
            "  border: 1px solid #3498DB; border-radius: 2px; padding: 0 2px; }");
        break;
    }
}

void CopyChip::updateDisplay()
{
    applyStyle();
    if (m_text.isEmpty()) {
        m_label->setText(m_placeholder);
        m_label->setStyleSheet(
            "QLabel { font-size: 11px; color: #ABB2B9; background: transparent;"
            "  padding: 0 2px; }"
            "QLabel:hover { background: #F8F9F9; border-radius: 2px; }");
    } else {
        m_label->setText(m_text);
    }
    m_label->setToolTip(m_text.isEmpty()
        ? QString::fromUtf8("双击编辑")
        : QString::fromUtf8("单击复制 · 双击编辑"));
}

void CopyChip::enterEdit()
{
    m_edit->setText(m_text);
    m_edit->setGeometry(m_label->geometry());
    m_edit->show();
    m_edit->raise();
    m_edit->setFocus();
    m_edit->selectAll();
}

void CopyChip::commitEdit()
{
    if (!m_edit->isVisible())
        return;
    const QString t = m_edit->text().trimmed();
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
        "QLabel { font-size: 11px; font-weight: bold; color: #1E8449;"
        "  background: #E9F7EF; border-radius: 2px; padding: 0 2px; }");
    QTimer::singleShot(800, this, [this]() { updateDisplay(); });
}

} // namespace cad::ui
