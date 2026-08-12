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
    setWindowTitle(QStringLiteral("测量结果"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    // Read-only identity / readout rows.
    auto* infoForm = new QFormLayout();
    infoForm->addRow(QStringLiteral("实测值:"),
                     new ElaText(cad::geo::Units::formatLength(valueMm), 13, this));

    auto* lblRef = new ElaText(refName, 13, this);
    lblRef->setStyleSheet("color:#999; font-size:11px;");
    infoForm->addRow(QStringLiteral("引用名:"), lblRef);
    layout->addLayout(infoForm);

    // Editable annotations (optional; empty = keep defaults).
    auto* editForm = new QFormLayout();
    m_nameEdit = new ElaLineEdit(this);
    m_nameEdit->setText(name);
    m_nameEdit->setPlaceholderText(QStringLiteral("可选，仅作显示名称"));
    editForm->addRow(QStringLiteral("名称:"), m_nameEdit);

    m_commentEdit = new ElaLineEdit(this);
    m_commentEdit->setText(comment);
    m_commentEdit->setPlaceholderText(QStringLiteral("可选注释"));
    editForm->addRow(QStringLiteral("注释:"), m_commentEdit);
    layout->addLayout(editForm);

    auto* hint = new ElaText(QStringLiteral("取消 / Esc：保留测量，跳过命名；确定：保存名称与注释"), 13, this);
    hint->setStyleSheet("color:#888; font-size:11px;");
    layout->addWidget(hint);

    layout->addWidget(cad::ui::makeDialogButtons(this).row);

    m_nameEdit->setFocus();
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
