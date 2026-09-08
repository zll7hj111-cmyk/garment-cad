#include "ContextStrip.h"
#include "PlacedPointStripBar.h"

#include <QUndoStack>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"

#include "ui/Theme.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"

namespace cad::app {
namespace {

QString kbdBadge(const QString& key)
{
    const auto& tk = cad::ui::Theme::tokens();
    return QStringLiteral("<span style=\"background:%1; border:1px solid %2; border-radius:2px; padding:1px 4px; font-family:'Consolas',monospace; font-size:10px; font-weight:600; color:%3;\">%4</span>")
        .arg(tk.surface2.name(), tk.borderStrong.name(), tk.text2.name(), key);
}

} // namespace

void ContextStrip::setUndoStack(QUndoStack* stack)
{
    m_undoStack = stack;
    if (m_placedPointBar) {
        m_placedPointBar->setUndoStack(stack);
    }
    if (!stack) return;
    connect(stack, &QUndoStack::indexChanged, this, [this](int) {
        if (m_focus == StripFocus::Empty || m_strokePreview) return;
        if (m_paramDoc && m_paramDoc->findBlock(m_blockId)) {
            refreshFields();
            refreshChrome();
        }
    });
}

void ContextStrip::beginConnectAngleSession(const QUuid& blockId, const QUuid& segmentId,
                                            const QUuid& attachmentId, double initialAngle)
{
    if (attachmentId.isNull()) { endConnectAngleSession(); return; }
    const auto* blk = m_paramDoc ? m_paramDoc->findBlock(blockId) : nullptr;
    const auto* seg = blk ? blk->findSegment(segmentId) : nullptr;
    if (!blk || !seg) return;

    m_blockId = blockId;
    m_segmentId = segmentId;
    m_focus = StripFocus::Pinned;
    m_creationPinned = false;
    m_strokePreview = false;
    m_connectSession = true;
    m_connectAttId = attachmentId;
    m_connectInitialAngle = initialAngle;
    m_hoverTimer->stop();

    setReadOnlyFields(true);
    m_angleEdit->setReadOnly(false);
    setConnectAngleValid(true);
    refreshFields();
    refreshChrome();
    show();
    m_angleEdit->setFocus();
    m_angleEdit->selectAll();
}

void ContextStrip::endConnectAngleSession()
{
    if (!m_connectSession) return;
    m_connectSession = false;
    m_connectAttId = QUuid();
    m_connectInitialAngle = 0.0;
    hideBar();
    returnFocusToCanvas();
}

void ContextStrip::setConnectAngleValid(bool valid)
{
    if (!m_connectSession) return;
    const bool invalid = !valid;
    if (m_angleEdit->property("angleInvalid").toBool() == invalid) return;
    m_angleEdit->setProperty("angleInvalid", invalid);
    m_angleEdit->style()->unpolish(m_angleEdit);
    m_angleEdit->style()->polish(m_angleEdit);
}

void ContextStrip::setRotateAnchorState(bool active, bool anchorIsEnd,
                                        bool canToggle, const QString& reason)
{
    m_rotateAnchor.active = active;
    m_rotateAnchor.anchorIsEnd = anchorIsEnd;
    m_rotateAnchor.canToggle = canToggle;
    m_rotateAnchor.reason = reason;
    if (m_focus != StripFocus::Empty) {
        refreshFields();
        refreshChrome();
    }
}

void ContextStrip::showStrokePreview(double lenCm, double angleDeg)
{
    if (lenCm <= 0.0) {
        hideBar();
        return;
    }
    m_hoverTimer->stop();
    m_focus = StripFocus::Empty;
    m_strokePreview = true;
    m_creationPinned = false;
    m_connectSession = false;
    m_connectAttId = QUuid();
    m_blockId = QUuid();
    m_segmentId = QUuid();
    m_idLabel->setText(QString::fromUtf8("新线"));

    const QSignalBlocker nb(m_nameEdit);
    m_nameEdit->clear();
    setReadOnlyFields(true);
    m_lenEdit->setText(cad::geo::Units::formatNumberTrimmed(lenCm));
    m_angleEdit->setText(cad::geo::Units::formatDegValue(
        cad::geo::normalizeDeg360(angleDeg)));
    m_btnUnitAngle->setChecked(true);
    m_btnUnitArc->setChecked(false);
    m_btnUnitAngle->setEnabled(false);
    m_btnUnitArc->setEnabled(false);
    m_btnReverse->setEnabled(false);
    m_btnBasis->setText(QString::fromUtf8("起点 → 终点"));
    m_badge->setText(QString::fromUtf8("绘制中"));
    m_hint->setText(QStringLiteral("%1 取消 · 落点后自动锁定").arg(kbdBadge(QStringLiteral("Esc"))));
    show();
}

void ContextStrip::cancelCreation()
{
    if (m_undoStack && m_undoStack->index() > m_editStartIndex)
        m_undoStack->setIndex(m_editStartIndex);
    m_creationPinned = false;
    hideBar();
}

} // namespace cad::app
