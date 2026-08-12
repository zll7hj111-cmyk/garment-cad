#include "AuxPointForm.h"

#include "ElaCheckBox.h"
#include "ElaComboBox.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include <QSignalBlocker>
#include <QApplication>
#include <QClipboard>

#include "geometry/Units.h"
#include "parametric/ParamPoint.h"
#include "parametric/Serial.h"

namespace cad::tools {

AuxPointForm::AuxPointForm(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);

    m_editName = new ElaLineEdit(this);
    m_editName->setPlaceholderText(QString::fromUtf8("\u5982\u201c\u9886\u53e3\u70b9\u201d"));  // 如“领口点”
    layout->addRow(QString::fromUtf8("\u540d\u79f0:"), m_editName);  // 名称:

    // Direction reference: measure from the start or the end endpoint. Item
    // labels show the actual endpoint serials+names (setEndpointLabels) so
    // the user can tell which physical endpoint is the "start" vs the "end".
    m_cmbDir = new ElaComboBox(this);
    m_cmbDir->addItem(QString::fromUtf8("\u4ece\u8d77\u70b9"));  // 从起点
    m_cmbDir->addItem(QString::fromUtf8("\u4ece\u7ec8\u70b9"));  // 从终点
    m_cmbDir->setToolTip(QStringLiteral(
        "从所选端点开始计量百分比与常量"));
    layout->addRow(QStringLiteral("计算方向:"), m_cmbDir);

    // Measurement reference point: default "端点"(endpoint) means the traditional
    // behavior (measure from start/end per direction combo). Selecting another
    // point on the segment makes percent+constant measure from that point.
    m_cmbRefPoint = new ElaComboBox(this);
    m_cmbRefPoint->addItem(QString::fromUtf8("\u7aef\u70b9"), QVariant::fromValue(QUuid()));  // 端点
    m_cmbRefPoint->setToolTip(QString::fromUtf8(
        "\u8ba1\u91cf\u8d77\u70b9\uff1a\u9ed8\u8ba4\u4ece\u7ebf\u6bb5\u7aef\u70b9\u5f00\u59cb\u8ba1\u91cf\uff0c"
        "\u53ef\u9009\u62e9\u7ebf\u6bb5\u4e0a\u7684\u5176\u4ed6\u70b9\uff08\u8f85\u52a9\u70b9/\u4ea4\u70b9\uff09\u4f5c\u4e3a\u8d77\u59cb\u4f4d\u7f6e"));
    layout->addRow(QString::fromUtf8("\u8ba1\u91cf\u8d77\u70b9:"), m_cmbRefPoint);  // 计量起点:

    m_editPercent = new ElaLineEdit(this);
    m_editPercent->setPlaceholderText(QString::fromUtf8("\u5982 0.5 \u6216\u516c\u5f0f"));  // 如 0.5 或公式
    m_editPercent->setToolTip(QString::fromUtf8(
        "\u767e\u5206\u6bd4\uff1a0.5 = \u4e24\u70b9\u4e2d\u95f4\uff0c\u53ef\u8d85\u51fa [0,1]\uff0c\u652f\u6301\u516c\u5f0f\u3002"
        "\u4e0e\u5e38\u91cf\u76f8\u52a0\uff1a\u4f4d\u7f6e = \u7ebf\u6bb5\u957f\u00d7\u767e\u5206\u6bd4 + \u5e38\u91cf"));
    layout->addRow(QString::fromUtf8("\u767e\u5206\u6bd4:"), m_editPercent);  // 百分比:

    m_editConstant = new ElaLineEdit(this);
    m_editConstant->setPlaceholderText(QString::fromUtf8("\u5982 0.7 (cm)\u6216\u516c\u5f0f"));  // 如 0.7 (cm)或公式
    m_editConstant->setToolTip(QString::fromUtf8(
        "\u5e38\u91cf\u504f\u79fb(cm)\uff1a\u4e0e\u767e\u5206\u6bd4\u76f8\u52a0\uff08\u4f4d\u7f6e = \u7ebf\u6bb5\u957f\u00d7\u767e\u5206\u6bd4 + \u5e38\u91cf\uff09\uff0c"
        "\u767e\u5206\u6bd4\u4e3a 0 \u65f6\u5373\u7ebf\u6bb5\u4e0a\u7684\u7edd\u5bf9\u4f4d\u7f6e\uff0c\u652f\u6301\u516c\u5f0f"));
    // 填入: the quick-aux dialog is non-modal, so the user copies a value /
    // formula from the variable panel and pastes it here — clear first to
    // guarantee clean content.
    auto makePasteBtn = [this](QLineEdit* edit) {
        auto* btn = new ElaPushButton(QStringLiteral("填入"), this);
        btn->setToolTip(QStringLiteral("清空输入框并粘贴剪切板内容"));
        connect(btn, &QPushButton::clicked, this, [this, edit] {
            const QString clean = QString(QApplication::clipboard()->text())
                                      .remove(QLatin1Char('\r'))
                                      .remove(QLatin1Char('\n'))
                                      .trimmed();
            if (!clean.isEmpty())
                edit->setText(clean);
        });
        return btn;
    };
    auto* constantRow = new QHBoxLayout();
    constantRow->addWidget(m_editConstant, 1);
    constantRow->addWidget(makePasteBtn(m_editConstant));
    layout->addRow(QString::fromUtf8("\u5e38\u91cf(cm):"), constantRow);  // 常量(cm):

    // Offset: construction-angle semantics — 0° = along the host segment
    // direction (CCW+); "从终点" flips the base. Both fields support formulas.
    m_editOffsetAngle = new ElaLineEdit(this);
    m_editOffsetAngle->setPlaceholderText(QString::fromUtf8("\u5982 45 \u6216\u516c\u5f0f"));  // 如 45 或公式
    m_editOffsetAngle->setToolTip(QString::fromUtf8(
        "\u504f\u79fb\u89d2\u5ea6(\u5ea6)\uff1a\u6784\u9020\u89d2\u8bed\u4e49\u2014\u20140\u00b0 = \u6cbf\u5bbf\u4e3b\u7ebf\u6bb5\u65b9\u5411\u76f4\u884c\uff0c"
        "\u9006\u65f6\u9488\u4e3a\u6b63\uff1b\u201c\u4ece\u7ec8\u70b9\u201d\u8ba1\u7b97\u65f6\u57fa\u51c6\u65b9\u5411\u7ffb\u8f6c\u3002\u652f\u6301\u516c\u5f0f\u3002"));  // 偏移角度(度)：跟随角度语义——0° = 沿宿主线段方向直行，逆时针为正；“从终点”计算时基准方向翻转。支持公式。
    layout->addRow(QString::fromUtf8("\u504f\u79fb\u89d2\u5ea6:"), m_editOffsetAngle);  // 偏移角度:

    m_editOffsetDist = new ElaLineEdit(this);
    m_editOffsetDist->setPlaceholderText(QString::fromUtf8("\u5982 1.5 (cm)\u6216\u516c\u5f0f"));  // 如 1.5 (cm)或公式
    m_editOffsetDist->setToolTip(QString::fromUtf8(
        "\u504f\u79fb\u8ddd\u79bb(cm)\uff1a\u6cbf\u504f\u79fb\u89d2\u5ea6\u65b9\u5411\u7684\u4f4d\u79fb\uff0c0 = \u70b9\u4ecd\u5728\u7ebf\u4e0a\u3002\u652f\u6301\u516c\u5f0f\u3002"));  // 偏移距离(cm)：沿偏移角度方向的位移，0 = 点仍在线上。支持公式。
    auto* offsetRow = new QHBoxLayout();
    offsetRow->addWidget(m_editOffsetDist, 1);
    offsetRow->addWidget(makePasteBtn(m_editOffsetDist));
    layout->addRow(QString::fromUtf8("\u504f\u79fb\u8ddd\u79bb(cm):"), offsetRow);  // 偏移距离(cm):

    m_chkShowName = new ElaCheckBox(QString::fromUtf8("\u663e\u793a\u540d\u79f0"), this);  // 显示名称
    layout->addRow(QString(), m_chkShowName);

    // textChanged → dirty (owner restarts debounce); commits → edited.
    for (auto* edit : {m_editName, m_editPercent, m_editConstant,
                       m_editOffsetAngle, m_editOffsetDist}) {
        connect(edit, &QLineEdit::textChanged,     this, &AuxPointForm::dirty);
        connect(edit, &QLineEdit::editingFinished, this, &AuxPointForm::edited);
    }
    connect(m_chkShowName, &QCheckBox::toggled, this, &AuxPointForm::edited);
    connect(m_cmbDir, &QComboBox::currentIndexChanged, this, [this](int index) {
        emit directionChanged(index == 1);
        emit edited();
    });
    connect(m_cmbRefPoint, &QComboBox::currentIndexChanged, this, [this](int) {
        emit edited();
    });
}

void AuxPointForm::setEndpointLabels(const cad::param::ParamPoint* startPt,
                                     const cad::param::ParamPoint* endPt)
{
    // Label = "从起点/从终点 (编号 · 名称)" so the user can tell which physical
    // endpoint is the start vs the end without having to inspect the canvas.
    auto label = [](const QString& prefix, const cad::param::ParamPoint* p) {
        QString txt = prefix;
        if (p) {
            txt += QStringLiteral(" (") + cad::param::Serial::tag(p->serial);
            if (!p->name.isEmpty())
                txt += QStringLiteral(" · ") + p->name;
            txt += QStringLiteral(")");
        }
        return txt;
    };
    m_cmbDir->setItemText(0, label(QStringLiteral("从起点"), startPt));  // 从起点
    m_cmbDir->setItemText(1, label(QStringLiteral("从终点"), endPt));    // 从终点
}

void AuxPointForm::setRefPointList(const std::vector<std::pair<QUuid, QString>>& points)
{
    const QSignalBlocker b(m_cmbRefPoint);
    m_cmbRefPoint->clear();
    // First item: "端点" (null UUID) = default endpoint-based measurement.
    m_cmbRefPoint->addItem(QString::fromUtf8("\u7aef\u70b9"),  // 端点
                           QVariant::fromValue(QUuid()));
    for (const auto& [id, label] : points) {
        m_cmbRefPoint->addItem(label, QVariant::fromValue(id));
    }
}

void AuxPointForm::loadFrom(const cad::param::ParamPoint& pt)
{
    const QSignalBlocker b1(m_editName), b2(m_cmbDir), b3(m_editPercent),
                         b4(m_editConstant), b5(m_editOffsetAngle),
                         b6(m_editOffsetDist),
                         b7(m_chkShowName), b8(m_cmbRefPoint);

    m_editName->setText(pt.name);

    // Direction reference (0 = from start, 1 = from end).
    m_cmbDir->setCurrentIndex(pt.interpFromEnd ? 1 : 0);

    // Measurement reference point.
    int refIdx = 0;  // Default: "端点"
    if (!pt.interpRefPointId.isNull()) {
        for (int i = 1; i < m_cmbRefPoint->count(); ++i) {
            if (m_cmbRefPoint->itemData(i).toUuid() == pt.interpRefPointId) {
                refIdx = i;
                break;
            }
        }
    }
    m_cmbRefPoint->setCurrentIndex(refIdx);

    // Percent: show formula if present, else numeric
    if (!pt.interpPercentFormula.isEmpty())
        m_editPercent->setText(pt.interpPercentFormula);
    else
        m_editPercent->setText(QString::number(pt.interpPercent, 'g', 6));

    // Constant: show formula if present, else convert mm→cm for display
    if (!pt.interpConstantFormula.isEmpty())
        m_editConstant->setText(pt.interpConstantFormula);
    else
        m_editConstant->setText(QString::number(cad::geo::Units::mmToCm(pt.interpConstant), 'g', 6));

    // Offset angle: show formula if present, else numeric (degrees)
    if (!pt.interpOffsetAngleFormula.isEmpty())
        m_editOffsetAngle->setText(pt.interpOffsetAngleFormula);
    else
        m_editOffsetAngle->setText(QString::number(pt.interpOffsetAngle, 'g', 6));

    // Offset distance: show formula if present, else convert mm→cm for display
    if (!pt.interpOffsetDistFormula.isEmpty())
        m_editOffsetDist->setText(pt.interpOffsetDistFormula);
    else
        m_editOffsetDist->setText(QString::number(cad::geo::Units::mmToCm(pt.interpOffsetDist), 'g', 6));

    m_chkShowName->setChecked(pt.showName);
}

void AuxPointForm::applyTo(cad::param::ParamPoint& pt) const
{
    // Percent
    QString percentText = m_editPercent->text().trimmed();
    bool isNum = false;
    double numVal = percentText.toDouble(&isNum);
    if (isNum) {
        pt.interpPercent = numVal;
        pt.interpPercentFormula.clear();
    } else if (!percentText.isEmpty()) {
        pt.interpPercentFormula = percentText;
    }

    // Constant (user inputs cm → store mm)
    QString constText = m_editConstant->text().trimmed();
    isNum = false;
    numVal = constText.toDouble(&isNum);
    if (isNum) {
        pt.interpConstant = cad::geo::Units::cmToMm(numVal);
        pt.interpConstantFormula.clear();
    } else if (!constText.isEmpty()) {
        pt.interpConstantFormula = constText;
    }

    // Offset angle (degrees, construction-angle semantics)
    QString angleText = m_editOffsetAngle->text().trimmed();
    isNum = false;
    numVal = angleText.toDouble(&isNum);
    if (isNum) {
        pt.interpOffsetAngle = numVal;
        pt.interpOffsetAngleFormula.clear();
    } else if (!angleText.isEmpty()) {
        pt.interpOffsetAngleFormula = angleText;
    }

    // Offset distance (user inputs cm → store mm)
    QString distText = m_editOffsetDist->text().trimmed();
    isNum = false;
    numVal = distText.toDouble(&isNum);
    if (isNum) {
        pt.interpOffsetDist = cad::geo::Units::cmToMm(numVal);
        pt.interpOffsetDistFormula.clear();
    } else if (!distText.isEmpty()) {
        pt.interpOffsetDistFormula = distText;
    }

    pt.showName = m_chkShowName->isChecked();

    // Direction reference (0 = from start, 1 = from end).
    pt.interpFromEnd = (m_cmbDir->currentIndex() == 1);

    // Measurement reference point.
    pt.interpRefPointId = m_cmbRefPoint->currentData().toUuid();

    // Name: display label only — the serial is the identity (dual-track).
    pt.name = m_editName->text().trimmed();
}

QString AuxPointForm::percentText() const
{
    return m_editPercent->text();
}

void AuxPointForm::setPercentText(const QString& text)
{
    const QSignalBlocker b(m_editPercent);
    m_editPercent->setText(text);
}

} // namespace cad::tools
