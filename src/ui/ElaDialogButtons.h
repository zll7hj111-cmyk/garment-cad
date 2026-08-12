#pragma once

#include <QDialog>
#include <QHBoxLayout>
#include <QString>
#include <QWidget>

#include "ElaPushButton.h"

/// Fluent-style dialog button row built from ElaPushButton.
///
/// ElaWidgetTools has no QDialogButtonBox equivalent, so dialogs get a
/// right-aligned row of Ela buttons wired to accept()/reject() semantics.
/// Returns the button widgets so callers can connect extra slots.
namespace cad::ui {

struct DialogButtons
{
    ElaPushButton* ok = nullptr;
    ElaPushButton* cancel = nullptr;
    QWidget*       row = nullptr;
};

inline DialogButtons makeDialogButtons(
    QDialog* dlg, const QString& okText = QString::fromUtf8("确定"),
    const QString& cancelText = QString::fromUtf8("取消"))
{
    auto* row = new QWidget(dlg);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 4, 0, 0);
    lay->addStretch(1);

    DialogButtons out;
    out.ok = new ElaPushButton(okText, row);
    out.ok->setMinimumWidth(84);
    lay->addWidget(out.ok);

    out.cancel = new ElaPushButton(cancelText, row);
    out.cancel->setMinimumWidth(84);
    lay->addWidget(out.cancel);

    QObject::connect(out.ok,     &ElaPushButton::clicked, dlg, &QDialog::accept);
    QObject::connect(out.cancel, &ElaPushButton::clicked, dlg, &QDialog::reject);
    out.row = row;
    return out;
}

} // namespace cad::ui
