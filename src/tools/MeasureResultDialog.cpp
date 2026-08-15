#include "MeasureResultDialog.h"

#include <QFormLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaDialog.h"
#include <QVBoxLayout>

#include "geometry/Units.h"
#include "ui/ElaDialogButtons.h"

namespace cad::tools {

MeasureResultDialog::MeasureResultDialog(double valueMm,
                                         const QString& refName,
                                         const QString& name,
                                         const QString& comment,
                                         QWidget* parent)
    : ElaDialog(parent)
{
    // refName 是调用方预留的自动生成名, 只在用户留空时使用 —— 对话框本体
    // 不展示它 (输入框初始为空)。
    Q_UNUSED(refName);

    setWindowTitle(QStringLiteral("测量结果"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    // Read-only identity / readout rows.
    auto* infoForm = new QFormLayout();
    infoForm->addRow(QStringLiteral("实测值:"),
                     new ElaText(cad::geo::Units::formatLength(valueMm), 13, this));
    layout->addLayout(infoForm);

    // Editable annotations (optional; empty = keep defaults).
    auto* editForm = new QFormLayout();
    // 引用名: 初始为空, 不显示自动生成的 M_xxx; 留空则由调用方保留自动名。
    m_refEdit = new ElaLineEdit(this);
    m_refEdit->setPlaceholderText(QStringLiteral("留空自动生成"));
    m_refEdit->setToolTip(QStringLiteral("公式引用名（留空自动生成）"));
    connect(m_refEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        // 引用名大写约定（与 CopyChip Ref 变体一致）: 边输入边转大写。
        const QString up = text.toUpper();
        if (up != text) {
            const int cursor = m_refEdit->cursorPosition();
            m_refEdit->blockSignals(true);
            m_refEdit->setText(up);
            m_refEdit->setCursorPosition(cursor);
            m_refEdit->blockSignals(false);
        }
    });
    editForm->addRow(QStringLiteral("引用名:"), m_refEdit);

    m_nameEdit = new ElaLineEdit(this);
    m_nameEdit->setText(name);
    m_nameEdit->setPlaceholderText(QStringLiteral("可选，仅作显示名称"));
    editForm->addRow(QStringLiteral("名称:"), m_nameEdit);

    m_commentEdit = new ElaLineEdit(this);
    m_commentEdit->setText(comment);
    m_commentEdit->setPlaceholderText(QStringLiteral("可选注释"));
    editForm->addRow(QStringLiteral("注释:"), m_commentEdit);
    layout->addLayout(editForm);

    auto* hint = new ElaText(QStringLiteral("取消 / Esc：保留测量，跳过命名；确定：保存引用名/名称/注释"), 13, this);
    hint->setStyleSheet("color:#888; font-size:11px;");
    layout->addWidget(hint);

    layout->addWidget(cad::ui::makeDialogButtons(this).row);

    m_refEdit->setFocus();
}

QString MeasureResultDialog::enteredRefName() const
{
    return m_refEdit ? m_refEdit->text().trimmed().toUpper() : QString();
}

QString MeasureResultDialog::enteredName() const
{
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

QString MeasureResultDialog::enteredComment() const
{
    return m_commentEdit ? m_commentEdit->text().trimmed() : QString();
}

} // namespace cad::tools
