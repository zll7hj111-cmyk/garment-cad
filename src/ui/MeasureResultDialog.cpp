#include "ui/MeasureResultDialog.h"

#include <QFormLayout>
#include <QSignalBlocker>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaDialog.h"
#include <QVBoxLayout>

#include "geometry/Units.h"
#include "ui/ElaDialogButtons.h"
#include "ui/FormScaffold.h"

namespace cad::ui {

MeasureResultDialog::MeasureResultDialog(double valueMm,
                                         const QString& refName,
                                         const QString& name,
                                         const QString& comment,
                                         cad::param::MeasureKind kind,
                                         QWidget* parent)
    : ElaDialog(parent)
{
    // refName 是调用方预留的自动生成名, 只在用户留空时使用 —— 对话框本体
    // 不展示它 (输入框初始为空)。
    Q_UNUSED(refName);

    setWindowTitle(QStringLiteral("测量结果"));
    setModal(true);

    QString valueLabel = QStringLiteral("实测值:");
    switch (kind) {
        case cad::param::MeasureKind::Horizontal:
            valueLabel = QStringLiteral("水平实测值:");
            break;
        case cad::param::MeasureKind::Vertical:
            valueLabel = QStringLiteral("垂直实测值:");
            break;
        case cad::param::MeasureKind::Distance:
            break;
    }

    // ── 工具表单群统一骨架 (ui-redesign §4.5/§5.5): 分组标题 + 88px 栅格
    //    + 底部按钮条 (主操作右置实心黄)。
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* body = new QWidget(this);
    auto* bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(14, 12, 14, 12);
    bodyLay->setSpacing(10);

    // ── 读数组: 实测值 = 15px Semibold 等宽 (值第一焦点, §4.3 同纪律)。
    bodyLay->addWidget(cad::ui::makeFormGroupHeader(
        QStringLiteral("READOUT"), QString::fromUtf8("读数"), body));
    auto* readoutForm = new QFormLayout();
    auto* valueText = new ElaText(cad::geo::Units::formatLength(valueMm), 15, body);
    valueText->setStyleSheet(QStringLiteral(
        "%1 font-size: %2px; font-weight: 600; background: transparent;")
        .arg(cad::ui::ThemeTokens::kMonospaceFamily,
             QString::number(cad::ui::ThemeTokens::FontLg)));
    readoutForm->addRow(valueLabel, valueText);
    cad::ui::applyFormGrid(readoutForm);
    bodyLay->addLayout(readoutForm);

    // ── 标注组: 引用名 / 名称 / 注释 (可编辑; 留空保留默认)。
    bodyLay->addWidget(cad::ui::makeFormGroupHeader(
        QStringLiteral("ANNOTATE"), QString::fromUtf8("标注"), body));
    auto* editForm = new QFormLayout();
    // 引用名: 初始为空, 不显示自动生成的 M_xxx; 留空则由调用方保留自动名。
    m_refEdit = new ElaLineEdit(body);
    m_refEdit->setPlaceholderText(QStringLiteral("留空自动生成"));
    m_refEdit->setToolTip(QStringLiteral("公式引用名（留空自动生成）"));
    connect(m_refEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        // 引用名大写约定（与 CopyChip Ref 变体一致）: 边输入边转大写。
        const QString up = text.toUpper();
        if (up != text) {
            const int cursor = m_refEdit->cursorPosition();
            const QSignalBlocker blocker(m_refEdit);
            m_refEdit->setText(up);
            m_refEdit->setCursorPosition(cursor);
        }
    });
    editForm->addRow(QStringLiteral("引用名:"), m_refEdit);

    m_nameEdit = new ElaLineEdit(body);
    m_nameEdit->setText(name);
    m_nameEdit->setPlaceholderText(QStringLiteral("可选，仅作显示名称"));
    editForm->addRow(QStringLiteral("名称:"), m_nameEdit);

    m_commentEdit = new ElaLineEdit(body);
    m_commentEdit->setText(comment);
    m_commentEdit->setPlaceholderText(QStringLiteral("可选注释"));
    editForm->addRow(QStringLiteral("注释:"), m_commentEdit);
    cad::ui::applyFormGrid(editForm);
    bodyLay->addLayout(editForm);

    auto* hint = new ElaText(QStringLiteral("取消 / Esc：保留测量，跳过命名；确定：保存引用名/名称/注释"), body);
    hint->setObjectName(QStringLiteral("dimText"));
    bodyLay->addWidget(hint);

    layout->addWidget(body, 1);

    auto buttons = cad::ui::makeFormButtonBar(this);
    layout->addWidget(buttons.row);

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

} // namespace cad::ui
