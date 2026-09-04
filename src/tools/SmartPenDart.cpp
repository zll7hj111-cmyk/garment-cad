#include "ToolSmartPen.h"
#include "ToolManager.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QSet>
#include <QFontMetricsF>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QtMath>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

#include "canvas/BlockItem.h"
#include "HitTester.h"
#include "canvas/CanvasScene.h"
#include "canvas/HudItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/MeasureVariable.h"
#include "parametric/Serial.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "ui/QuickAuxDialog.h"
#include "LeaderCandidatePicker.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/DocumentCommands.h"

namespace cad::tools {

// ---------------------------------------------------------------------------
// 省道线模式 (dart line, 用户拍板 2026-08)
// ---------------------------------------------------------------------------

void ToolSmartPen::openDartDialog(const SnapResult& bSnap)
{
    if (!m_startSnap || !m_paramDoc || !m_scene || m_dartDialog) return;

    QWidget* parent = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    auto* dlg = new QDialog(parent);
    dlg->setWindowTitle(QStringLiteral("\u7701\u9053\u7ebf"));
    // NON-modal on purpose: the user must be able to switch to the
    // variable/formula panel and copy a name/expression while this dialog
    // stays open.
    dlg->setModal(false);
    auto* form = new QFormLayout(dlg);
    auto* nameEdit = new QLineEdit(dlg);
    nameEdit->setPlaceholderText(QStringLiteral("\u7ebf\u6bb5\u540d\u79f0\uff08\u53ef\u9009\uff09"));
    nameEdit->setText(m_preInput.name.trimmed());
    auto* offEdit = new QLineEdit(dlg);
    offEdit->setPlaceholderText(QStringLiteral("\u504f\u79fb\u8ddd\u79bb d\uff08\u6570\u5b57=mm\uff1b\u516c\u5f0f=cm \u57df\uff09"));
    auto* angEdit = new QLineEdit(dlg);
    angEdit->setText(QStringLiteral("90"));
    form->addRow(QStringLiteral("\u540d\u79f0"), nameEdit);
    form->addRow(QStringLiteral("\u504f\u79fb d"), offEdit);
    form->addRow(QStringLiteral("\u89d2\u5ea6 \u03b2"), angEdit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    form->addRow(buttons);

    // Validate + commit on OK, but keep the dialog open when the input is
    // invalid so the user can fix it (or go copy a variable name).
    QObject::connect(buttons, &QDialogButtonBox::accepted, dlg,
        [this, dlg, bSnap, nameEdit, offEdit, angEdit]() {
            if (!m_startSnap || !m_paramDoc || !m_lineFactory) return;

            // Offset d: a pure number is mm; anything else is a cm-domain formula.
            const QString offText = offEdit->text().trimmed();
            if (offText.isEmpty()) {
                if (m_scene)
                    m_scene->showToast(QStringLiteral("\u504f\u79fb\u8ddd\u79bb d \u4e0d\u80fd\u4e3a\u7a7a"));
                return;
            }
            QString offsetFormula;
            const auto parsedOffset = cad::geo::parseNumberOrFormula(offText);
            double offsetMm = parsedOffset.value;
            if (!parsedOffset.isNumber) {
                if (!cad::param::ConditionEngine::evaluateLengthMm(
                        parsedOffset.formula, m_paramDoc->parameters(), m_paramDoc->conditions(),
                        offsetMm)) {
                    if (m_scene)
                        m_scene->showToast(QStringLiteral("\u504f\u79fb\u8ddd\u79bb\u516c\u5f0f\u65e0\u6cd5\u8ba1\u7b97"));
                    return;
                }
                offsetFormula = parsedOffset.formula;
            }

            // Angle β relative to the reference segment (default 90°).
            QString angleFormula;
            double betaDeg = 90.0;
            const QString angText = angEdit->text().trimmed();
            if (!angText.isEmpty()) {
                const auto parsedAng = cad::geo::parseNumberOrFormula(angText);
                betaDeg = parsedAng.value;
                if (!parsedAng.isNumber) {
                    auto r = cad::param::ConditionEngine::evaluate(
                        parsedAng.formula, m_paramDoc->parameters(), m_paramDoc->conditions());
                    if (!r.ok) {
                        if (m_scene)
                            m_scene->showToast(QStringLiteral("\u89d2\u5ea6\u516c\u5f0f\u65e0\u6cd5\u8ba1\u7b97"));
                        return;
                    }
                    betaDeg = r.value;
                    angleFormula = parsedAng.formula;
                }
            }

            commitDartLine(m_startSnap->blockId, m_startSnap->pointId,
                           bSnap.blockId, bSnap.pointId,
                           offsetMm, betaDeg, nameEdit->text().trimmed(),
                           offsetFormula, angleFormula);
            dlg->accept();
        });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    m_dartDialog = dlg;
    showDialogBlockedFeedback();
    QObject::connect(dlg, &QDialog::finished, dlg, [this, dlg](int result) {
        if (m_dartDialog == dlg) {
            m_dartDialog = nullptr;
            clearDialogBlockedCursor();   // M10: 关闭即恢复画布光标
        }
        dlg->deleteLater();
        // Rejected / closed: nothing committed — Drawing keeps its rubber
        // band so the user may pick another offset point B.
        (void)result;
    });

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void ToolSmartPen::commitDartLine(const QUuid& aBlockId, const QUuid& aPointId,
                                  const QUuid& bBlockId, const QUuid& bPointId,
                                  double offsetMm, double angleDeg,
                                  const QString& name,
                                  const QString& offsetFormula,
                                  const QString& angleFormula)
{
    if (!m_lineFactory || !m_paramDoc || !m_startSnap) return;

    const SnapResult aSnap{m_startSnap->worldPos, aBlockId, aPointId};
    const SnapResult bSnap{cad::geo::Vec2(), bBlockId, bPointId};
    // The pre-input strip does not drive dart geometry (calculated from
    // A/B/d/β); only the dialog-provided name is forwarded.
    LineBuildOptions opts;
    opts.name = name;
    m_lineFactory->createDartLine(aSnap, bSnap, offsetMm, angleDeg,
                                  opts, offsetFormula, angleFormula);

    consumePreInput();
    m_leaderPicker->clear();
    clearPreview();
    setState(State::Idle);
    m_startSnap.reset();
    m_currentSnap.reset();
    resetStrokeTargets();

    if (m_scene && m_paramDoc && !m_paramDoc->blocks().empty()) {
        const auto& lastBlock = m_paramDoc->blocks().back();
        if (!lastBlock.segments.empty())
            m_scene->notifyLineCreated(lastBlock.id, lastBlock.segments.back().id);
    }
}

} // namespace cad::tools
