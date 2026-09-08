#include "ui/QuickAuxDialog.h"

#include <QFormLayout>
#include "ElaText.h"
#include <QVBoxLayout>
#include <QLabel>

#include <cmath>

#include "ui/AuxPointForm.h"
#include "parametric/Serial.h"
#include "ui/ElaDialogButtons.h"
#include "ui/FormScaffold.h"
#include "geometry/Epsilon.h"
#include "geometry/Units.h"
#include "document/CommandTexts.h"

namespace cad::ui {

QuickAuxDialog::QuickAuxDialog(const cad::param::ParamPoint& pt,
                               const cad::param::ParamPoint* startPt,
                               const cad::param::ParamPoint* endPt,
                               QWidget* parent)
    : ElaDialog(parent)
    , m_pt(pt)
    , m_prefillT(pt.interpPercent)
{
    // Deliberately NON-modal: the user needs to reach the variable panel to
    // look up / copy formulas while this dialog stays open (the smart pen
    // tool ignores canvas clicks until the dialog is answered).
    setModal(false);
    setWindowTitle(cad::cmd::texts::kNewAuxPoint);

    // ElaAppBar's default close path is `close(); processEvents();
    // windowHandle->close();` — with the old self-deleteLater it destroyed the
    // dialog mid-call (use-after-free crash). Route the X button through the
    // safe signal branch; lifecycle is owned by the caller (tool / main
    // window), which deletes via QDialog::finished.
    setIsDefaultClosed(false);
    connect(this, &ElaDialog::closeButtonClicked, this, [this]() {
        QDialog::reject();
    });

    // ── 工具表单群统一骨架 (ui-redesign §4.5/§5.5): 分组标题 + 88px 栅格
    //    + 底部按钮条 (主操作右置实心黄)。窗口标题栏由 ElaAppBar 提供,
    //    不另加代号标题栏 (避免双重标题)。
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* body = new QWidget(this);
    auto* bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(14, 12, 14, 12);
    bodyLay->setSpacing(10);

    bodyLay->addWidget(cad::ui::makeFormGroupHeader(
        QStringLiteral("GEOMETRY"), QString::fromUtf8("几何"), body));

    // Read-only identity row: the serial IS the point's ID (dual-track naming).
    auto* idRow = new QFormLayout();
    auto* lblId = new ElaText(cad::param::Serial::toHtml(m_pt.serial), 13, body);
    lblId->setTextFormat(Qt::RichText);
    lblId->setObjectName(QStringLiteral("dimText"));
    idRow->addRow(QStringLiteral("编号:"), lblId);
    cad::ui::applyFormGrid(idRow);
    bodyLay->addLayout(idRow);

    m_form = new AuxPointForm(body);
    m_form->setEndpointLabels(startPt, endPt);
    m_form->loadFrom(m_pt);
    bodyLay->addWidget(m_form);

    // Flipping the direction re-bases the percent: keep the point under the
    // click's X marker by swapping t ↔ 1−t, but only while the field still
    // holds the untouched prefill (a user-typed value/formula is preserved).
    connect(m_form, &AuxPointForm::directionChanged, this, [this](bool fromEnd) {
        const auto parsed = cad::geo::parseNumberOrFormula(m_form->percentText());
        const double expect = fromEnd ? m_prefillT : (1.0 - m_prefillT);
        if (parsed.isNumber && std::abs(parsed.value - expect) < cad::geo::kGeomEps)
            m_form->setPercentText(QString::number(fromEnd ? (1.0 - m_prefillT)
                                                           : m_prefillT, 'g', 6));
    });

    auto* hint = new ElaText(QString::fromUtf8("位置 = 线段长×百分比 + 常量；可再设偏移角度/偏移距离（角度以线段方向为 0°，逆时针为正）"),
        body);
    hint->setObjectName(QStringLiteral("dimText"));
    bodyLay->addWidget(hint);

    layout->addWidget(body, 1);

    auto buttons = cad::ui::makeFormButtonBar(this);
    layout->addWidget(buttons.row);
}

cad::param::ParamPoint QuickAuxDialog::point() const
{
    cad::param::ParamPoint pt = m_pt;
    m_form->applyTo(pt);
    return pt;
}

} // namespace cad::ui
