#include "QuickAuxDialog.h"

#include <QFormLayout>
#include "ElaText.h"
#include <QVBoxLayout>

#include <cmath>

#include "AuxPointForm.h"
#include "parametric/Serial.h"
#include "ui/ElaDialogButtons.h"

namespace cad::tools {

QuickAuxDialog::QuickAuxDialog(const cad::param::ParamPoint& pt,
                               const cad::param::ParamPoint* startPt,
                               const cad::param::ParamPoint* endPt,
                               QWidget* parent)
    : ElaDialog(parent)
    , m_pt(pt)
    , m_prefillT(pt.interpPercent)
{
    setWindowTitle(QStringLiteral("新建辅助点"));
    // Deliberately NON-modal: the user needs to reach the variable panel to
    // look up / copy formulas while this dialog stays open (the smart pen
    // tool ignores canvas clicks until the dialog is answered).
    setModal(false);

    // ElaAppBar's default close path is `close(); processEvents();
    // windowHandle->close();` — with WA_DeleteOnClose the processEvents()
    // destroys the dialog mid-call (use-after-free crash). Route the X
    // button through the safe signal branch instead; the caller no longer
    // sets WA_DeleteOnClose (the dialog deletes itself on close).
    setIsDefaultClosed(false);
    connect(this, &ElaDialog::closeButtonClicked, this, [this]() {
        QDialog::reject();
        deleteLater();
    });

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    // Read-only identity row: the serial IS the point's ID (dual-track naming).
    auto* idRow = new QFormLayout();
    auto* lblId = new ElaText(cad::param::Serial::toHtml(m_pt.serial), 13, this);
    lblId->setTextFormat(Qt::RichText);
    lblId->setObjectName(QStringLiteral("dimText"));
    idRow->addRow(QStringLiteral("编号:"), lblId);
    layout->addLayout(idRow);

    m_form = new AuxPointForm(this);
    m_form->setEndpointLabels(startPt, endPt);
    m_form->loadFrom(m_pt);
    layout->addWidget(m_form);

    // Flipping the direction re-bases the percent: keep the point under the
    // click's X marker by swapping t ↔ 1−t, but only while the field still
    // holds the untouched prefill (a user-typed value/formula is preserved).
    connect(m_form, &AuxPointForm::directionChanged, this, [this](bool fromEnd) {
        bool isNum = false;
        const double cur = m_form->percentText().trimmed().toDouble(&isNum);
        const double expect = fromEnd ? m_prefillT : (1.0 - m_prefillT);
        if (isNum && std::abs(cur - expect) < 1e-9)
            m_form->setPercentText(QString::number(fromEnd ? (1.0 - m_prefillT)
                                                           : m_prefillT, 'g', 6));
    });

    auto* hint = new ElaText(QString::fromUtf8("\u4f4d\u7f6e = \u7ebf\u6bb5\u957f\u00d7\u767e\u5206\u6bd4 + \u5e38\u91cf\uff1b\u53ef\u518d\u8bbe\u504f\u79fb\u89d2\u5ea6/\u504f\u79fb\u8ddd\u79bb\uff08\u89d2\u5ea6\u4ee5\u7ebf\u6bb5\u65b9\u5411\u4e3a 0\u00b0\uff0c\u9006\u65f6\u9488\u4e3a\u6b63\uff09"), 13, // 位置 = 线段长×百分比 + 常量；可再设偏移角度/偏移距离（角度以线段方向为 0°，逆时针为正）
        this);
    hint->setObjectName(QStringLiteral("dimText"));
    layout->addWidget(hint);

    layout->addWidget(cad::ui::makeDialogButtons(this).row);
}

cad::param::ParamPoint QuickAuxDialog::point() const
{
    cad::param::ParamPoint pt = m_pt;
    m_form->applyTo(pt);
    return pt;
}

} // namespace cad::tools
