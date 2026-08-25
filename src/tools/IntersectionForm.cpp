#include "IntersectionForm.h"

#include "ElaCheckBox.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include <QSignalBlocker>

#include "parametric/ParamPoint.h"

namespace cad::tools {

IntersectionForm::IntersectionForm(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);

    m_editName = new ElaLineEdit(this);
    m_editName->setPlaceholderText(QString::fromUtf8("\u5982\u201c\u80a9\u70b9\u201d"));  // 如"肩点"
    layout->addRow(QString::fromUtf8("\u540d\u79f0:"), m_editName);  // 名称:

    m_lblOrigin = new ElaText(QString::fromUtf8("\u2014"), 13, this);  // —
    m_lblOrigin->setObjectName(QStringLiteral("mutedText"));
    layout->addRow(QString::fromUtf8("\u5c04\u7ebf\u8d77\u70b9:"), m_lblOrigin);  // 射线起点:

    m_editAngle = new ElaLineEdit(this);
    m_editAngle->setPlaceholderText(QString::fromUtf8("\u5982 90 (\u00b0)\u6216\u516c\u5f0f"));  // 如 90 (°)或公式
    m_editAngle->setToolTip(QString::fromUtf8(
        "\u5c04\u7ebf\u89d2\u5ea6(\u00b0)\uff1a\u9ed8\u8ba4\u76f8\u5bf9\u76ee\u6807\u7ebf\u6bb5 start\u2192end \u65b9\u5411\uff0c"
        "90\u00b0=\u5782\u76f4\uff0c\u9006\u65f6\u9488\u4e3a\u6b63\uff0c\u652f\u6301\u516c\u5f0f"));
    layout->addRow(QString::fromUtf8("\u5c04\u7ebf\u89d2\u5ea6(\u00b0):"), m_editAngle);  // 射线角度(°):

    m_chkWorldAngle = new ElaCheckBox(QString::fromUtf8("按绝对角度输入"), this);  // 按绝对角度输入
    m_chkWorldAngle->setToolTip(QString::fromUtf8(
        "\u52fe\u9009\u540e\u89d2\u5ea6\u6846\u6309\u7edd\u5bf9\u89d2\u5ea6\u663e\u793a/\u8f93\u5165\uff0c"
        "\u4fdd\u5b58\u65f6\u81ea\u52a8\u53cd\u7b97\u4e3a\u76f8\u5bf9\u7ebf\u6bb5\u7684\u89d2\u5ea6\uff08\u5b58\u50a8\u59cb\u7ec8\u4e3a\u76f8\u5bf9\u503c\uff09"));
    layout->addRow(QString(), m_chkWorldAngle);

    // Aim point (指向点): read-only label + clear button. When set, the ray
    // direction points straight at this point instead of using the angle field.
    auto* aimRow = new QWidget(this);
    auto* aimLayout = new QHBoxLayout(aimRow);
    aimLayout->setContentsMargins(0, 0, 0, 0);
    m_lblAim = new ElaText(QString::fromUtf8("\u2014"), 13, aimRow);  // —
    m_lblAim->setObjectName(QStringLiteral("mutedText"));
    aimLayout->addWidget(m_lblAim, 1);
    m_btnClearAim = new ElaPushButton(QString::fromUtf8("\u6e05\u9664"), aimRow);  // 清除
    m_btnClearAim->setEnabled(false);
    m_btnClearAim->setToolTip(QString::fromUtf8(
        "\u53d6\u6d88\u6307\u5411\uff0c\u56de\u9000\u5230\u89d2\u5ea6\u6a21\u5f0f"));  // 取消指向，回退到角度模式
    aimLayout->addWidget(m_btnClearAim);
    layout->addRow(QString::fromUtf8("\u6307\u5411\u70b9:"), aimRow);  // 指向点:

    m_chkShowName = new ElaCheckBox(QString::fromUtf8("\u663e\u793a\u540d\u79f0"), this);  // 显示名称
    layout->addRow(QString(), m_chkShowName);

    // Signals.
    connect(m_editName, &QLineEdit::textChanged, this, &IntersectionForm::dirty);
    connect(m_editName, &QLineEdit::editingFinished, this, &IntersectionForm::edited);
    connect(m_editAngle, &QLineEdit::textChanged, this, &IntersectionForm::dirty);
    connect(m_editAngle, &QLineEdit::editingFinished, this, &IntersectionForm::edited);
    connect(m_chkWorldAngle, &QCheckBox::toggled, this, &IntersectionForm::onWorldAngleToggled);
    connect(m_chkShowName, &QCheckBox::toggled, this, &IntersectionForm::edited);
    connect(m_btnClearAim, &QPushButton::clicked, this, [this]() {
        setAimLabel(QString());
        emit aimCleared();
    });
}

void IntersectionForm::onWorldAngleToggled(bool checked)
{
    // Convert the current numeric text so the actual ray direction is unchanged.
    // Formulas are always segment-relative and are left untouched.
    QString text = m_editAngle->text().trimmed();
    bool isNum = false;
    const double val = text.toDouble(&isNum);
    if (!isNum) {
        emit edited();
        return;
    }
    const double converted = checked ? (val + m_segWorldDir)   // relative → world
                                     : (val - m_segWorldDir);  // world → relative
    m_editAngle->setText(QString::number(converted, 'g', 6));
    emit edited();
}

void IntersectionForm::loadFrom(const cad::param::ParamPoint& pt)
{
    const QSignalBlocker b1(m_editName), b2(m_editAngle),
                         b3(m_chkWorldAngle), b4(m_chkShowName);

    m_editName->setText(pt.name);

    // Show the stored angle frame (relative default, absolute if persisted).
    m_chkWorldAngle->setChecked(pt.interUseWorldAngle);

    // Angle: show formula if present, else numeric in the stored angle frame.
    if (!pt.interAngleFormula.isEmpty())
        m_editAngle->setText(pt.interAngleFormula);
    else
        m_editAngle->setText(QString::number(pt.interAngle, 'g', 6));

    m_chkShowName->setChecked(pt.showName);
}

void IntersectionForm::applyTo(cad::param::ParamPoint& pt) const
{
    // Angle (degrees). The checkbox selects the stored frame:
    //   checked = absolute world angle; unchecked = segment-relative.
    pt.interUseWorldAngle = m_chkWorldAngle->isChecked();
    QString angleText = m_editAngle->text().trimmed();
    bool isNum = false;
    double numVal = angleText.toDouble(&isNum);
    if (isNum) {
        pt.interAngle = numVal;
        pt.interAngleFormula.clear();
    } else if (!angleText.isEmpty()) {
        pt.interAngleFormula = angleText;  // formulas follow the selected frame
    }

    pt.showName = m_chkShowName->isChecked();
    pt.name = m_editName->text().trimmed();
}

void IntersectionForm::setOriginLabel(const QString& text)
{
    m_lblOrigin->setText(text);
}

void IntersectionForm::setAimLabel(const QString& text)
{
    m_lblAim->setText(text.isEmpty() ? QString::fromUtf8("\u2014") : text);  // —
    m_btnClearAim->setEnabled(!text.isEmpty());
}

} // namespace cad::tools
