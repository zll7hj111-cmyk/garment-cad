#include "SmartPenPreInputBar.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QSignalBlocker>

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
                                int width) {
        auto* lbl = new ElaText(label, 13, this);
        lay->addWidget(lbl);
        auto* edit = new ElaLineEdit(this);
        edit->setPlaceholderText(placeholder);
        edit->setMaximumWidth(width);
        // ElaLineEdit hardcodes setFixedHeight(35); the bar lives inside the
        // 28px ElaStatusBar — shrink the fields like SegmentEditBar does.
        edit->setFixedHeight(24);
        lay->addWidget(edit);
        return edit;
    };

    m_nameEdit = addField(QString::fromUtf8("名称"),
                          QString::fromUtf8("线段名称"), 100);
    m_lenEdit = addField(QString::fromUtf8("长度(cm)"),
                         QString::fromUtf8("数值或公式"), 90);
    m_lenEdit->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace;");
    m_angleEdit = addField(QString::fromUtf8("角度(°)"),
                           QString::fromUtf8("数值或公式"), 90);
    m_angleEdit->setStyleSheet(
        "font-family: 'Consolas','Courier New',monospace;");

    lay->addStretch();

    for (ElaLineEdit* edit : {m_nameEdit, m_lenEdit, m_angleEdit}) {
        connect(edit, &QLineEdit::textChanged,
                this, &SmartPenPreInputBar::valuesChanged);
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

} // namespace cad::app
