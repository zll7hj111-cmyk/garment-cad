#include "ui/AuxPointForm.h"

#include "ElaCheckBox.h"
#include "ElaComboBox.h"
#include "ElaText.h"
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
#include "ui/FormScaffold.h"
#include "ui/TooltipFormatter.h"

namespace cad::ui {

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
    m_cmbDir->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("计算方向"),
        QStringLiteral("从所选端点开始计量百分比与常量偏移")));
    layout->addRow(QStringLiteral("计算方向:"), m_cmbDir);

    // Measurement reference point: default "端点"(endpoint) means the traditional
    // behavior (measure from start/end per direction combo). Selecting another
    // point on the segment makes percent+constant measure from that point.
    m_cmbRefPoint = new ElaComboBox(this);
    m_cmbRefPoint->addItem(QString::fromUtf8("\u7aef\u70b9"), QVariant::fromValue(QUuid()));  // 端点
    m_cmbRefPoint->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("计量起点"),
        QStringLiteral("默认从线段端点开始计量，可选择线段上的其他点（辅助点/交点）作为起始位置")));
    layout->addRow(QString::fromUtf8("\u8ba1\u91cf\u8d77\u70b9:"), m_cmbRefPoint);  // 计量起点:

    m_editPercent = new ElaLineEdit(this);
    m_editPercent->setPlaceholderText(QString::fromUtf8("\u5982 0.5 \u6216\u516c\u5f0f"));  // 如 0.5 或公式
    m_editPercent->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("百分比"),
        QStringLiteral("0.5 = 两点中间，可超出 [0,1]，支持公式；位置 = 线段长 × 百分比 + 常量")));
    layout->addRow(QString::fromUtf8("\u767e\u5206\u6bd4:"), m_editPercent);  // 百分比:

    m_editConstant = new ElaLineEdit(this);
    m_editConstant->setPlaceholderText(QString::fromUtf8("\u5982 0.7 (cm)\u6216\u516c\u5f0f"));  // 如 0.7 (cm)或公式
    m_editConstant->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("常量偏移 (cm)"),
        QStringLiteral("与百分比相加；百分比为 0 时即线段上的绝对位置，支持公式")));
    // 填入: the quick-aux dialog is non-modal, so the user copies a value /
    // formula from the variable panel and pastes it here — clear first to
    // guarantee clean content.
    auto makePasteBtn = [this](QLineEdit* edit) {
        auto* btn = new ElaPushButton(QStringLiteral("填入"), this);
        btn->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("填入剪贴板"),
            QStringLiteral("清空输入框并粘贴剪切板内容")));
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

    m_lblMountInfo = new ElaText(QString::fromUtf8("无挂载"), 12, this);
    m_lblMountInfo->setObjectName(QStringLiteral("auxMountInfo"));
    m_btnDetachMount = new ElaPushButton(QString::fromUtf8("拆开"), this);
    m_btnDetachMount->setObjectName(QStringLiteral("auxDetachBtn"));
    m_btnDetachMount->setEnabled(false);
    connect(m_btnDetachMount, &QPushButton::clicked, this, &AuxPointForm::detachRequested);

    auto* mountRow = new QHBoxLayout();
    mountRow->addWidget(m_lblMountInfo, 1);
    mountRow->addWidget(m_btnDetachMount);
    layout->addRow(QString::fromUtf8("挂载线段:"), mountRow);

    m_chkShowName = new ElaCheckBox(QString::fromUtf8("\u663e\u793a\u540d\u79f0"), this);  // 显示名称
    layout->addRow(QString(), m_chkShowName);

    // textChanged → dirty (owner restarts debounce); commits → edited.
    cad::ui::applyFormGrid(layout);  // 88px 标签栅格 (ui-redesign §4.5)
    for (auto* edit : {m_editName, m_editPercent, m_editConstant}) {
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
                         b4(m_editConstant),
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

    // 纯线上辅助点：偏移角度与偏移距离置零
    pt.interpOffsetAngle = 0.0;
    pt.interpOffsetAngleFormula.clear();
    pt.interpOffsetDist = 0.0;
    pt.interpOffsetDistFormula.clear();

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

void AuxPointForm::setMountInfo(const QString& info, bool hasIncoming, bool /*isDetached*/)
{
    if (!m_lblMountInfo || !m_btnDetachMount) return;
    if (!hasIncoming) {
        m_lblMountInfo->setText(QString::fromUtf8("无挂载"));
        m_btnDetachMount->setEnabled(false);
        m_btnDetachMount->setToolTip(cad::ui::TooltipFormatter::status(
            QStringLiteral("连接状态"),
            QStringLiteral("当前辅助点没有连接关系（未被挂载也未跟随外部线）"), false));
        return;
    }

    m_lblMountInfo->setText(info);
    m_btnDetachMount->setEnabled(true);
    m_btnDetachMount->setText(QString::fromUtf8("拆开"));
    m_btnDetachMount->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("拆开连接"),
        QStringLiteral("彻底释放当前辅助点的连接关系，恢复为自由线段")));
}

} // namespace cad::ui

