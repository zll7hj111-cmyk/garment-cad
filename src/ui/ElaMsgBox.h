#pragma once

#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include "ElaContentDialog.h"
#include "ElaText.h"

/// Fluent-style modal message helpers built on ElaContentDialog.
///
/// ElaWidgetTools ships no QMessageBox equivalent, so we wrap
/// ElaContentDialog (left / middle / right button slots + signals) into the
/// blocking confirm/warning/critical/question API the rest of the app used
/// with QMessageBox. All functions are blocking (QDialog::exec).
namespace cad::ui {

namespace ElaMsgBox {

enum class Result { Left, Middle, Right };

inline ElaContentDialog* makeDialog(QWidget* parent, const QString& title,
                                    const QString& text)
{
    auto* dlg = new ElaContentDialog(parent);
    dlg->setWindowTitle(title);
    // ElaContentDialog 主布局无外边距，默认中央部件自带 (15,25,15,10)；
    // setCentralWidget 换掉默认部件后必须自己补边距，否则文字贴边框。
    auto* bodyHost = new QWidget(dlg);
    auto* bodyLay = new QVBoxLayout(bodyHost);
    bodyLay->setContentsMargins(15, 25, 15, 10);
    auto* body = new ElaText(text, 13, bodyHost);
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    body->setMinimumWidth(320);
    bodyLay->addWidget(body);
    dlg->setCentralWidget(bodyHost);
    return dlg;
}

inline Result show(QWidget* parent, const QString& title, const QString& text,
                   const QString& leftText, const QString& rightText,
                   const QString& middleText = QString())
{
    auto* dlg = makeDialog(parent, title, text);
    dlg->setLeftButtonText(leftText);
    dlg->setRightButtonText(rightText);
    // 空文本隐藏中间按钮（ElaContentDialog::setMiddleButtonText 已同步处理）。
    dlg->setMiddleButtonText(middleText);

    Result result = Result::Right;
    QObject::connect(dlg, &ElaContentDialog::leftButtonClicked, dlg,
                     [&result, dlg] { result = Result::Left; dlg->accept(); });
    QObject::connect(dlg, &ElaContentDialog::middleButtonClicked, dlg,
                     [&result, dlg] { result = Result::Middle; dlg->accept(); });
    QObject::connect(dlg, &ElaContentDialog::rightButtonClicked, dlg,
                     [&result, dlg] { result = Result::Right; dlg->accept(); });
    dlg->exec();
    delete dlg;
    return result;
}

/// Yes/Cancel question; returns true for the left ("是") button.
inline bool question(QWidget* parent, const QString& title, const QString& text)
{
    return show(parent, title, text, QString::fromUtf8("是"),
                QString::fromUtf8("取消")) == Result::Left;
}

/// Warning with a single OK button.
inline void warning(QWidget* parent, const QString& title, const QString& text)
{
    show(parent, title, text, QString::fromUtf8("确定"), QString());
}

/// Critical error with a single OK button.
inline void critical(QWidget* parent, const QString& title, const QString& text)
{
    show(parent, title, text, QString::fromUtf8("确定"), QString());
}

/// About box with a single OK button.
inline void about(QWidget* parent, const QString& title, const QString& text)
{
    show(parent, title, text, QString::fromUtf8("确定"), QString());
}

} // namespace ElaMsgBox

} // namespace cad::ui
