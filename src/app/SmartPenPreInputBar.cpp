#include "SmartPenPreInputBar.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QKeyEvent>

#include "ElaText.h"
#include "ElaLineEdit.h"

namespace cad::app {

SmartPenPreInputBar::SmartPenPreInputBar(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 0, 6, 0);
    lay->setSpacing(6);

    auto* caption = new ElaText(QString::fromUtf8("预输入:"), 13, this);
    caption->setObjectName(QStringLiteral("mutedText"));
    lay->addWidget(caption);

    auto addField = [this, lay](const QString& label, const QString& placeholder,
                                int width, const QString& objName) {
        auto* lbl = new ElaText(label, 13, this);
        lay->addWidget(lbl);
        auto* edit = new ElaLineEdit(this);
        edit->setObjectName(objName);
        edit->setPlaceholderText(placeholder);
        edit->setMaximumWidth(width);
        // ElaLineEdit hardcodes setFixedHeight(35); the bar lives inside the
        // 28px ElaStatusBar — shrink the fields like SegmentEditBar does.
        edit->setFixedHeight(24);
        lay->addWidget(edit);
        edit->installEventFilter(this);
        return edit;
    };

    m_nameEdit = addField(QString::fromUtf8("名称"),
                          QString::fromUtf8("线段名称"), 100, QStringLiteral("preInputName"));
    m_lenEdit = addField(QString::fromUtf8("长度(cm)"),
                         QString::fromUtf8("数值或公式"), 90, QStringLiteral("preInputLength"));
    m_lenEdit->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace;");
    m_angleEdit = addField(QString::fromUtf8("角度(°)"),
                           QString::fromUtf8("数值或公式"), 90, QStringLiteral("preInputAngle"));
    m_angleEdit->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace;");

    lay->addStretch();

    for (ElaLineEdit* edit : {m_nameEdit, m_lenEdit, m_angleEdit}) {
        connect(edit, &QLineEdit::textChanged,
                this, &SmartPenPreInputBar::valuesChanged);
    }
}

void SmartPenPreInputBar::setCanvasView(QWidget* canvasView)
{
    m_canvasView = canvasView;
}

void SmartPenPreInputBar::focusFirstNameField()
{
    if (m_nameEdit) {
        m_nameEdit->setFocus();
        m_nameEdit->selectAll();
    }
}

void SmartPenPreInputBar::focusLengthField()
{
    if (m_lenEdit) {
        m_lenEdit->setFocus();
        m_lenEdit->selectAll();
    }
}

void SmartPenPreInputBar::focusAngleField()
{
    if (m_angleEdit) {
        m_angleEdit->setFocus();
        m_angleEdit->selectAll();
    }
}

void SmartPenPreInputBar::clearAll()
{
    // Block the per-field signals and emit exactly one valuesChanged() so the
    // host pushes a single empty snapshot to the smart pen.
    const QSignalBlocker b1(m_nameEdit);
    const QSignalBlocker b2(m_lenEdit);
    const QSignalBlocker b3(m_angleEdit);
    m_nameEdit->clear();
    m_lenEdit->clear();
    m_angleEdit->clear();
    emit valuesChanged();
}

QString SmartPenPreInputBar::nameText() const
{
    return m_nameEdit->text();
}

QString SmartPenPreInputBar::lengthText() const
{
    return m_lenEdit->text();
}

QString SmartPenPreInputBar::angleText() const
{
    return m_angleEdit->text();
}

bool SmartPenPreInputBar::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(event);
        // 输入包含 (Input containment): Accept shortcut overrides inside pre-input
        // fields so window action shortcuts (L, V, C, R, B, I, A, H, Ctrl+Z, Ctrl+Y, etc.)
        // never fire or kick the user out of the active tool while typing.
        ke->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            auto* edit = qobject_cast<QLineEdit*>(watched);
            if (edit && !edit->text().isEmpty()) {
                edit->clear();
            } else {
                clearAll();
            }
            if (m_canvasView) {
                m_canvasView->setFocus();
            } else {
                clearFocus();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (watched == m_nameEdit) {
                m_lenEdit->setFocus();
                m_lenEdit->selectAll();
            } else if (watched == m_lenEdit) {
                m_angleEdit->setFocus();
                m_angleEdit->selectAll();
            } else if (watched == m_angleEdit) {
                if (m_canvasView) {
                    m_canvasView->setFocus();
                } else {
                    m_angleEdit->clearFocus();
                }
            }
            return true;
        }
        if (ke->key() == Qt::Key_Tab) {
            if (watched == m_nameEdit) {
                m_lenEdit->setFocus();
                m_lenEdit->selectAll();
            } else if (watched == m_lenEdit) {
                m_angleEdit->setFocus();
                m_angleEdit->selectAll();
            } else if (watched == m_angleEdit) {
                m_nameEdit->setFocus();
                m_nameEdit->selectAll();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Backtab) {
            if (watched == m_angleEdit) {
                m_lenEdit->setFocus();
                m_lenEdit->selectAll();
            } else if (watched == m_lenEdit) {
                m_nameEdit->setFocus();
                m_nameEdit->selectAll();
            } else if (watched == m_nameEdit) {
                m_angleEdit->setFocus();
                m_angleEdit->selectAll();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace cad::app

