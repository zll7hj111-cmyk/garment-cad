#include "SegmentEditBar.h"

#include <cmath>

#include <QHBoxLayout>
#include "ElaText.h"
#include <QSignalBlocker>
#include <QTimer>
#include <QKeyEvent>
#include <QUndoStack>

#include "ElaLineEdit.h"

#include "ui/Theme.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/Attachment.h"
#include "parametric/Serial.h"
#include "parametric/ConditionEngine.h"
#include "parametric/MeasureVariable.h"
#include "geometry/Units.h"
#include "document/commands/BlockCommands.h"

namespace cad::app {

namespace {

/// Normalize an angle to [0, 360°) for display (存储保持原值, 显示不爆表).
double normalizeDeg(double deg)
{
    deg = std::fmod(deg, 360.0);
    if (deg < 0.0) deg += 360.0;
    return deg;
}

} // namespace

SegmentEditBar::SegmentEditBar(cad::param::ParamDocument* paramDoc, QWidget* parent)
    : QWidget(parent)
    , m_paramDoc(paramDoc)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 6, 0);
    lay->setSpacing(6);

    m_idLabel = new ElaText(QString(), 12, this);
    m_idLabel->setObjectName(QStringLiteral("accentText"));
    m_idLabel->setStyleSheet(QStringLiteral("%1 font-weight: bold;")
                                 .arg(cad::ui::ThemeTokens::kMonospaceFamily));
    lay->addWidget(m_idLabel);

    auto* lblName = new ElaText(QString::fromUtf8("名称:"), 12, this);
    lblName->setObjectName(QStringLiteral("mutedText"));
    lay->addWidget(lblName);
    m_nameEdit = new ElaLineEdit(this);
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));
    m_nameEdit->setPlaceholderText(QString::fromUtf8("线段名称"));
    m_nameEdit->setMaximumWidth(110);
    // ElaLineEdit hardcodes setFixedHeight(35); the bar lives inside the
    // 28px ElaStatusBar, so shrink the fields or their text gets pushed
    // toward the bottom edge (user: 输入框文字被往下挤).
    m_nameEdit->setFixedHeight(24);
    lay->addWidget(m_nameEdit);

    auto* lblLen = new ElaText(QString::fromUtf8("长度(cm):"), 12, this);
    lblLen->setObjectName(QStringLiteral("mutedText"));
    lay->addWidget(lblLen);
    m_lenEdit = new ElaLineEdit(this);
    m_lenEdit->setObjectName(QStringLiteral("lenEdit"));
    m_lenEdit->setPlaceholderText(QString::fromUtf8("数值或公式"));
    m_lenEdit->setMaximumWidth(110);
    m_lenEdit->setFixedHeight(24);
    m_lenEdit->setStyleSheet(cad::ui::ThemeTokens::kMonospaceFamily);
    lay->addWidget(m_lenEdit);

    auto* lblAng = new ElaText(QString::fromUtf8("角度(°):"), 12, this);
    lblAng->setObjectName(QStringLiteral("mutedText"));
    lay->addWidget(lblAng);
    m_angleEdit = new ElaLineEdit(this);
    m_angleEdit->setObjectName(QStringLiteral("angleEdit"));
    m_angleEdit->setPlaceholderText(QString::fromUtf8("数值或公式"));
    m_angleEdit->setMaximumWidth(110);
    m_angleEdit->setFixedHeight(24);
    m_angleEdit->setStyleSheet(cad::ui::ThemeTokens::kMonospaceFamily);
    lay->addWidget(m_angleEdit);

    lay->addStretch();

    // Name applies immediately; length/angle use a 200 ms debounce plus an
    // immediate apply on Enter/focus-loss (same pattern as LinePropertyDialog).
    // Programmatic population (refreshFields / showPreview) is signal-blocked,
    // so a plain textChanged connection is safe — no focus guard needed.
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this] { applyName(); });
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        if (m_lenEdit->hasFocus()) applyLength();
        if (m_angleEdit->hasFocus()) applyAngle();
    });
    connect(m_lenEdit, &QLineEdit::textChanged, this, [this] { m_debounce->start(); });
    connect(m_angleEdit, &QLineEdit::textChanged, this, [this] { m_debounce->start(); });
    connect(m_lenEdit, &QLineEdit::editingFinished, this, [this] { applyLength(); });
    connect(m_angleEdit, &QLineEdit::editingFinished, this, [this] { applyAngle(); });

    // Esc in any field = 撤销创建 (host undoes the creation command).
    for (ElaLineEdit* edit : {m_nameEdit, m_lenEdit, m_angleEdit})
        edit->installEventFilter(this);

    hideBar();
}

void SegmentEditBar::setUndoStack(QUndoStack* stack)
{
    m_undoStack = stack;
    if (!stack) return;
    // Undo/redo of a strip commit must re-sync the fields (otherwise the
    // strip would keep showing the undone values).
    connect(stack, &QUndoStack::indexChanged, this, [this](int) {
        if (isVisible() && !m_blockId.isNull()
            && m_paramDoc && m_paramDoc->findBlock(m_blockId))
            refreshFields();
    });
}

void SegmentEditBar::cancelCreation()
{
    // Rewind to the creation point: undoes the creation command AND every
    // strip edit pushed since (the line disappears).
    if (m_undoStack && m_undoStack->index() > m_editStartIndex)
        m_undoStack->setIndex(m_editStartIndex);
    hideBar();
}

void SegmentEditBar::showForLine(const QUuid& blockId, const QUuid& segmentId,
                                 bool grabFocus)
{
    if (!m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(blockId);
    const auto* seg = block ? block->findSegment(segmentId) : nullptr;
    if (!block || !seg) return;

    m_blockId = blockId;
    m_segmentId = segmentId;
    // Remember where the creation command sits — cancelCreation() rewinds
    // here, dropping the creation together with any strip edits pushed since.
    m_editStartIndex = m_undoStack ? m_undoStack->index() : 0;

    // ID: human-friendly tag only (L3), never the random prefix.
    m_idLabel->setText(cad::param::Serial::tag(seg->serial));

    // Edit mode: all fields enabled.
    m_nameEdit->setReadOnly(false);
    m_lenEdit->setReadOnly(false);
    m_angleEdit->setReadOnly(false);

    refreshFields();

    show();
    if (grabFocus) {
        m_nameEdit->setFocus();
        m_nameEdit->selectAll();
    }
}

void SegmentEditBar::showPreview(double lenCm, double angleDeg)
{
    // 0,0 = cancel (stroke aborted): hide the readout entirely. Tolerance-based
    // compare — the caller may feed freshly computed geometry.
    if (std::abs(lenCm) < 1e-9 && std::abs(angleDeg) < 1e-9) { hideBar(); return; }

    // Read-only preview while a stroke is being drawn (创建中只读读数).
    m_idLabel->setText(QString::fromUtf8("新线"));
    {
        // Block signals: preview population must not apply (applyName is
        // driven by USER typing only).
        const QSignalBlocker nb(m_nameEdit);
        m_nameEdit->clear();
    }
    m_nameEdit->setReadOnly(true);
    m_lenEdit->setReadOnly(true);
    m_angleEdit->setReadOnly(true);
    m_lenEdit->setText(QString::number(lenCm, 'f', 2));
    m_angleEdit->setText(QString::number(normalizeDeg(angleDeg), 'f', 1));
    show();
}

void SegmentEditBar::hideBar()
{
    hide();
}

void SegmentEditBar::applyName()
{
    if (!m_paramDoc || m_blockId.isNull()) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!seg) return;

    cad::cmd::SegmentEditBarCommand::State st = snapshotState();
    st.segName = m_nameEdit->text().trimmed();
    commitState(std::move(st));
}

void SegmentEditBar::applyLength()
{
    if (!m_paramDoc || m_blockId.isNull()) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!seg) return;

    // Bridge lines (length = measure variable) have a passive length — never
    // overwrite the measurement link (applyBridgeReadOnly semantics).
    const bool isBridge = !seg->lengthFormula.isEmpty();
    if (isBridge) return;

    const QString text = m_lenEdit->text().trimmed();
    if (text.isEmpty()) return;

    auto* ep = block->findPoint(seg->endPointId);
    if (!ep) return;

    cad::cmd::SegmentEditBarCommand::State st = snapshotState();
    bool isNumber = false;
    const double numCm = text.toDouble(&isNumber);
    if (isNumber) {
        st.lengthFormula.clear();
        st.endDistanceFormula.clear();
        st.endDistance = cad::geo::Units::cmToMm(numCm);
    } else {
        st.lengthFormula = text;
        st.endDistanceFormula = text;
    }
    commitState(std::move(st));
}

void SegmentEditBar::applyAngle()
{
    if (!m_paramDoc || m_blockId.isNull()) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!seg) return;

    // Bridge lines are passive — never rewrite their angle (see applyLength).
    if (!seg->lengthFormula.isEmpty()) return;

    const QString text = m_angleEdit->text().trimmed();
    if (text.isEmpty()) return;

    bool isNumber = false;
    double targetDeg = text.toDouble(&isNumber);
    if (!isNumber) {
        auto r = cad::param::ConditionEngine::evaluate(
            text, m_paramDoc->parameters(), {});
        if (!r.ok) return;
        targetDeg = r.value;
    }

    cad::cmd::SegmentEditBarCommand::State st = snapshotState();

    // Follower: edit the follower angle / arc length on the attachment
    // (matching LinePropertyDialog::onAngleApply — the follower angle is
    // owned by the attachment, never by the segment).
    if (const cad::param::Attachment* att = findEditAttachment()) {
        st.attId = att->id;
        if (att->rotationMode == cad::param::RotationMode::ArcLength) {
            st.arcLength = cad::geo::Units::cmToMm(targetDeg);
            st.arcLengthFormula = isNumber ? QString() : text;
        } else {
            st.followerAngle = targetDeg;
            st.followerAngleFormula = isNumber ? QString() : text;
        }
        commitState(std::move(st));
        return;
    }

    // Free block: set the endpoint's Polar angle. The stored angle is LOCAL
    // (relative to block rotation); the field shows the WORLD (absolute) angle
    // — same compensation as the property dialog.
    auto* ep = block->findPoint(seg->endPointId);
    if (!ep) return;

    if (ep->constraint != cad::param::PointConstraint::Polar) {
        const auto* sp = block->findPoint(seg->startPointId);
        if (!sp || !sp->resolved || !ep->resolved) return;
        const double dist = sp->resolvedPos.distanceTo(ep->resolvedPos);
        st.endConstraint = static_cast<int>(cad::param::PointConstraint::Polar);
        st.endRefPointId = seg->startPointId;
        st.endDistance = dist;
    }

    const double rotDeg = block->transform.rotation * 180.0 / M_PI;
    st.endAngle = targetDeg - rotDeg;
    st.endAngleFormula.clear();
    if (!isNumber) {
        st.endAngleFormula = (std::abs(rotDeg) > 1e-9)
            ? QStringLiteral("(%1)-%2").arg(text).arg(rotDeg, 0, 'g', 12)
            : text;
    }
    commitState(std::move(st));
}

const cad::param::Attachment* SegmentEditBar::findEditAttachment() const
{
    if (!m_paramDoc || m_blockId.isNull()) return nullptr;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return nullptr;

    // The angle of THIS segment is driven by the follower attachment anchored
    // at this segment's start or end point. A block may own one attachment
    // while having SEVERAL lines (the follower can sit on a different line's
    // endpoint) — a block-wide first match would then show and edit the WRONG
    // line's angle. Pins (bridge endpoints) never drive an angle here.
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId != m_blockId || att.isPin) continue;
        if (att.fromPointId == seg->startPointId
            || att.fromPointId == seg->endPointId)
            return &att;
    }
    return nullptr;
}

cad::cmd::SegmentEditBarCommand::State SegmentEditBar::snapshotState() const
{
    cad::cmd::SegmentEditBarCommand::State st;
    if (!m_paramDoc || m_blockId.isNull()) return st;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return st;

    st.segName = seg->name;
    st.lengthFormula = seg->lengthFormula;
    if (const auto* ep = block->findPoint(seg->endPointId)) {
        st.endDistance = ep->distance;
        st.endDistanceFormula = ep->distanceFormula;
        st.endAngle = ep->angle;
        st.endAngleFormula = ep->angleFormula;
        st.endConstraint = static_cast<int>(ep->constraint);
        st.endRefPointId = ep->refPointId;
    }
    if (const auto* att = findEditAttachment()) {
        st.attId = att->id;
        st.followerAngle = att->followerAngle;
        st.followerAngleFormula = att->followerAngleFormula;
        st.arcLength = att->arcLength;
        st.arcLengthFormula = att->arcLengthFormula;
        st.rotationMode = static_cast<int>(att->rotationMode);
    }
    return st;
}

void SegmentEditBar::commitState(cad::cmd::SegmentEditBarCommand::State st)
{
    if (!m_paramDoc || m_blockId.isNull()) return;
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::SegmentEditBarCommand(
            m_paramDoc, m_blockId, m_segmentId, std::move(st)));
    } else {
        // No undo stack (headless tests): apply + resolve directly.
        auto* cmd = new cad::cmd::SegmentEditBarCommand(
            m_paramDoc, m_blockId, m_segmentId, std::move(st));
        cmd->redo();
        delete cmd;
    }
}

void SegmentEditBar::refreshFields()
{
    if (!m_paramDoc || m_blockId.isNull()) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    {
        // Block signals: populating the field must not apply it (applyName
        // is driven by USER typing only).
        const QSignalBlocker nb(m_nameEdit);
        m_nameEdit->setText(seg->name);
    }

    // Bridge lines (length = measure variable) are passive: both length and
    // angle are read-only (applyBridgeReadOnly semantics).
    const bool isBridge = !seg->lengthFormula.isEmpty();
    m_lenEdit->setReadOnly(isBridge);
    m_angleEdit->setReadOnly(isBridge);

    // Length: formula wins, else the resolved distance in cm.
    const auto* ep = block->findPoint(seg->endPointId);
    if (!seg->lengthFormula.isEmpty()) {
        m_lenEdit->setText(seg->lengthFormula);
    } else if (ep) {
        m_lenEdit->setText(QString::number(
            cad::geo::Units::mmToCm(ep->distance), 'f', 2));
    } else {
        m_lenEdit->clear();
    }

    // Angle: follower angle/arc from the attachment, else the free-line
    // world angle (local angle + block rotation).
    if (const cad::param::Attachment* att = findEditAttachment()) {
        if (att->rotationMode == cad::param::RotationMode::ArcLength) {
            m_angleEdit->setText(att->arcLengthFormula.isEmpty()
                ? QString::number(cad::geo::Units::mmToCm(att->arcLength), 'f', 2)
                : att->arcLengthFormula);
        } else {
            m_angleEdit->setText(att->followerAngleFormula.isEmpty()
                ? QString::number(normalizeDeg(att->followerAngle), 'f', 1)
                : att->followerAngleFormula);
        }
    } else if (ep) {
        if (!ep->angleFormula.isEmpty()) {
            m_angleEdit->setText(ep->angleFormula);
        } else {
            const double rotDeg = block->transform.rotation * 180.0 / M_PI;
            m_angleEdit->setText(
                QString::number(normalizeDeg(ep->angle + rotDeg), 'f', 1));
        }
    } else {
        m_angleEdit->clear();
    }
}

bool SegmentEditBar::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(event);
        // 输入包含 (Input containment): Accept shortcut overrides inside line edits
        // so window action shortcuts (L, V, C, R, B, I, A, H, etc.) never fire while editing.
        ke->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            emit cancelRequested();
            return true;  // swallow — the host owns the creation-undo.
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (watched == m_nameEdit) {
                m_lenEdit->setFocus();
                m_lenEdit->selectAll();
            } else if (watched == m_lenEdit) {
                m_angleEdit->setFocus();
                m_angleEdit->selectAll();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Tab) {
            if (watched == m_nameEdit) {
                m_lenEdit->setFocus();
                m_lenEdit->selectAll();
            } else if (watched == m_lenEdit) {
                m_angleEdit->setFocus();
                m_angleEdit->selectAll();
            } else if (watched == m_angleEdit) {
                m_nameEdit->setFocus();
                m_nameEdit->selectAll();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Backtab) {
            if (watched == m_angleEdit) {
                m_lenEdit->setFocus();
                m_lenEdit->selectAll();
            } else if (watched == m_lenEdit) {
                m_nameEdit->setFocus();
                m_nameEdit->selectAll();
            } else if (watched == m_nameEdit) {
                m_angleEdit->setFocus();
                m_angleEdit->selectAll();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace cad::app

