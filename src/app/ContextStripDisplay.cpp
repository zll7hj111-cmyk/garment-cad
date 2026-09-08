#include "ContextStrip.h"
#include "PlacedPointStripBar.h"

#include <cmath>

#include <QSignalBlocker>
#include <QStyle>
#include <QPushButton>

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"

#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"
#include "ui/FormScaffold.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/ParamPoint.h"
#include "parametric/Attachment.h"
#include "parametric/Serial.h"
#include "parametric/FollowerAngle.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "document/commands/BlockCommands.h"
#include "ui/UiStrings.h"

using cad::ui::kbdBadge;

namespace cad::app {

void ContextStrip::refreshFields()
{
    if (!m_paramDoc || m_blockId.isNull()) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    m_idLabel->setText(cad::param::Serial::tag(seg->serial));

    if (!m_nameEdit->hasFocus()) {
        const QSignalBlocker nb(m_nameEdit);
        m_nameEdit->setText(seg->name);
    }

    if (!m_lenEdit->hasFocus()) {
        const QSignalBlocker lb(m_lenEdit);
        if (!seg->lengthFormula.isEmpty()) {
            m_lenEdit->setText(seg->lengthFormula);
        } else {
            const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
            const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                const double mm = (ep->constraint == cad::param::PointConstraint::OrthoOffset)
                    ? ep->distance
                    : sp->resolvedPos.distanceTo(ep->resolvedPos);
                m_lenEdit->setText(cad::geo::Units::formatCm(mm));
            } else {
                m_lenEdit->clear();
            }
        }
    }

    const cad::param::Attachment* att = findEditAttachment();

    if (m_baseAngleEdit) {
        double baseDeg = 0.0;
        if (att && !att->angleIndependent) {
            const double refWorldRad = cad::param::effectiveAngleRefWorld(m_paramDoc, *att);
            baseDeg = cad::geo::normalizeDeg180(cad::geo::radToDeg(refWorldRad));
        } else if (m_rotateAnchor.active) {
            baseDeg = cad::geo::normalizeDeg180(m_rotateAnchor.baseAngleDeg);
        } else {
            const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
            const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                const cad::geo::Vec2 wd = block->transform.toWorld(ep->resolvedPos)
                                        - block->transform.toWorld(sp->resolvedPos);
                baseDeg = cad::geo::normalizeDeg180(cad::geo::radToDeg(wd.angle()));
            }
        }
        m_baseAngleEdit->setText(cad::geo::Units::formatDegValue(baseDeg));
    }

    if (!m_angleEdit->hasFocus()) {
        const QSignalBlocker ab(m_angleEdit);
        if (att && !att->angleIndependent) {
            const QString formula = att->rotationMode == cad::param::RotationMode::ArcLength
                ? att->arcLengthFormula
                : (att->rotationMode == cad::param::RotationMode::ChordLength
                       ? att->chordLengthFormula
                       : att->followerAngleFormula);
            // 2026-12 审计 P0-2: 数值回填统一走 parametric 域入口 ——
            // 角度折角显示域; 弧长/开度按存储值原样显示 (cm), 不再经
            // arc→deg→normalizeDeg180→arc 往返（等效角 >180° 时显示成负值）。
            m_angleEdit->setText(formula.isEmpty()
                ? cad::param::attachmentValueDisplayText(*att)
                : formula);
        } else if (const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId)) {
            const auto* sp = block->findPoint(seg->startPointId);
            const auto* driven = (ep && !ep->angleFormula.isEmpty()) ? ep
                : ((sp && !sp->angleFormula.isEmpty()) ? sp : ep);
            if (driven && !driven->angleFormula.isEmpty()) {
                m_angleEdit->setText(driven->angleFormula);
            } else {
                const double rotDeg = cad::geo::radToDeg(block->transform.rotation);
                double worldDeg = cad::geo::normalizeDeg360(ep->angle + rotDeg);
                if (m_rotateAnchor.active && m_rotateAnchor.anchorIsEnd)
                    worldDeg = cad::geo::normalizeDeg360(worldDeg + 180.0);
                m_angleEdit->setText(cad::geo::Units::formatDegValue(worldDeg));
            }
        } else {
            m_angleEdit->clear();
        }
    }
}

void ContextStrip::refreshChrome()
{
    if (!m_paramDoc || m_blockId.isNull()) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    const cad::param::Attachment* att = findEditAttachment();
    const bool isBridge = block->isBridge;

    if (m_connectSession) {
        m_nameEdit->setReadOnly(true);
        m_lenEdit->setReadOnly(true);
        m_angleEdit->setReadOnly(false);
    } else if (m_focus == StripFocus::Pinned) {
        m_lenEdit->setReadOnly(isBridge);
        m_angleEdit->setReadOnly(isBridge);
    }

    const bool isArc = att && !att->angleIndependent
        && att->rotationMode == cad::param::RotationMode::ArcLength;
    const bool isChord = att && !att->angleIndependent
        && att->rotationMode == cad::param::RotationMode::ChordLength;
    const bool isAngle = att && !att->angleIndependent
        && att->rotationMode == cad::param::RotationMode::Angle;
    m_btnUnitAngle->setChecked(isAngle || (!isArc && !isChord));
    m_btnUnitArc->setChecked(isArc);
    m_btnUnitChord->setChecked(isChord);
    const bool unitEnabled = (att != nullptr) && !att->angleIndependent
                             && m_focus == StripFocus::Pinned;
    m_btnUnitAngle->setEnabled(unitEnabled);
    m_btnUnitArc->setEnabled(unitEnabled);
    m_btnUnitChord->setEnabled(unitEnabled);

    const bool dimEditable = m_focus == StripFocus::Pinned
        && !m_connectSession && !m_rotateAnchor.active;
    const bool hasAtt = att != nullptr;
    m_btnPosDetach->setText(hasAtt && att->angleOnly
        ? QString::fromUtf8("重连") : QString::fromUtf8("拆开"));
    m_btnPosDetach->setEnabled(hasAtt && dimEditable);
    m_btnPosDetach->setToolTip(hasAtt && att->angleOnly
        ? cad::ui::TooltipFormatter::action(
            cad::ui::str::kReconnectPosition,
            QStringLiteral("吸附回原宿主点并重新焊接，角度基准保留"))
        : cad::ui::TooltipFormatter::action(
            cad::ui::str::kDetachPositionLink,
            QStringLiteral("解除位置吸附（角度仍跟随基准线）；配合基准「拆开」可转为自由线")));
    m_btnAngleDetach->setText(hasAtt && att->angleIndependent
        ? QString::fromUtf8("重连") : QString::fromUtf8("拆开"));
    m_btnAngleDetach->setEnabled(hasAtt && dimEditable);
    m_btnAngleDetach->setToolTip(hasAtt && att->angleIndependent
        ? cad::ui::TooltipFormatter::action(
            QStringLiteral("重新连接角度基准"),
            QStringLiteral("恢复角度跟随（原基准或位置宿主，反算零跳变）"))
        : cad::ui::TooltipFormatter::action(
            QStringLiteral("拆开角度基准"),
            QStringLiteral("角度不再跟随基准线（独立角，位置保持吸附）")));

    QString reason;
    bool canRev;
    if (m_rotateAnchor.active) {
        canRev = m_rotateAnchor.canToggle && m_focus == StripFocus::Pinned;
        reason = canRev
            ? cad::ui::TooltipFormatter::actionWithShortcut(
                cad::ui::str::kToggleAnchorCenter, QStringLiteral("X"),
                QStringLiteral("切换旋转中心（起点 ↔ 终点）：旋转将绕另一端展开，画布箭头随之翻转"))
            : cad::ui::TooltipFormatter::actionWithShortcut(
                cad::ui::str::kToggleAnchorCenter, QStringLiteral("X"),
                QStringLiteral("切换旋转中心（起点 ↔ 终点）"),
                m_rotateAnchor.reason.isEmpty() ? QStringLiteral("当前状态不可切换锚心") : m_rotateAnchor.reason);
    } else {
        canRev = m_focus == StripFocus::Pinned
                 && !m_connectSession
                 && cad::cmd::ReverseSegmentCommand::canReverse(
                        m_paramDoc, m_blockId, m_segmentId, &reason);
        if (canRev) {
            reason = cad::ui::TooltipFormatter::action(
                cad::ui::str::kReverseSegment,
                QStringLiteral("交换起点与终点身份：换向后修改长度/角度驱动另一端，几何位置不变"));
        } else {
            reason = cad::ui::TooltipFormatter::action(
                cad::ui::str::kReverseSegment,
                QStringLiteral("交换起点与终点驱动角色"),
                reason.isEmpty() ? QStringLiteral("当前状态不可换向") : reason);
        }
    }
    m_btnReverse->setEnabled(canRev);
    m_btnReverse->setToolTip(reason);

    const auto* shadowBlk = (att && m_paramDoc)
        ? m_paramDoc->findBlock(att->toBlockId) : nullptr;
    const bool shadowBasis = shadowBlk && shadowBlk->isShadow;
    const QString shadowMark = shadowBasis
        ? QString::fromUtf8(" · 影子基准") : QString();
    QString modeWord;
    if (att && !att->angleIndependent) {
        if (att->rotationMode == cad::param::RotationMode::ArcLength)
            modeWord = QString::fromUtf8(" · 弧长");
        else if (att->rotationMode == cad::param::RotationMode::ChordLength)
            modeWord = QString::fromUtf8(" · 开度");
        else
            modeWord = QString::fromUtf8(" · 角度");
    }
    if (isBridge) {
        m_badge->setText(QString::fromUtf8("桥线"));
    } else if (seg->isCurve()) {
        m_badge->setText(QString::fromUtf8("曲线"));
    } else if (att && att->angleIndependent && att->angleOnly) {
        m_badge->setText(QString::fromUtf8("自由") + shadowMark);
    } else if (att && att->angleIndependent) {
        const auto* leader = m_paramDoc->findBlock(att->toBlockId);
        const auto* leaderSeg = leader ? leader->findSegment(att->toSegmentId) : nullptr;
        m_badge->setText((leaderSeg
            ? QString::fromUtf8("独立角 · 位置跟随 %1").arg(cad::param::Serial::tag(leaderSeg->serial))
            : QString::fromUtf8("独立角")) + shadowMark);
    } else if (att && att->angleOnly) {
        QString basisText;
        if (shadowBasis) {
            const cad::param::Attachment* att1 = nullptr;
            for (const auto& a : m_paramDoc->attachments()) {
                if (!a.isPin && a.fromBlockId == shadowBlk->id) { att1 = &a; break; }
            }
            if (att1) {
                if (const auto* host = m_paramDoc->findBlock(att1->toBlockId)) {
                    if (const auto* hs = host->findSegment(att1->toSegmentId))
                        basisText = cad::param::Serial::tag(hs->serial);
                }
            } else if (const auto* master =
                           m_paramDoc->findBlock(shadowBlk->shadowMasterBlockId);
                       master && !master->segments.empty()) {
                if (const auto* ms = master->findSegment(master->segments.front().id))
                    basisText = cad::param::Serial::tag(ms->serial);
            }
        }
        if (!basisText.isEmpty())
            m_badge->setText(QString::fromUtf8("仅角度 · %1%2")
                                 .arg(basisText, QString::fromUtf8("（影子）") + modeWord));
        else {
            const auto* leader = m_paramDoc->findBlock(att->toBlockId);
            const auto* leaderSeg = leader ? leader->findSegment(att->toSegmentId) : nullptr;
            m_badge->setText((leaderSeg
                ? QString::fromUtf8("仅角度 · 跟随 %1%2").arg(cad::param::Serial::tag(leaderSeg->serial), modeWord)
                : QString::fromUtf8("仅角度%1").arg(modeWord)) + shadowMark);
        }
    } else if (att) {
        const auto* leader = m_paramDoc->findBlock(att->toBlockId);
        const auto* leaderSeg = leader ? leader->findSegment(att->toSegmentId) : nullptr;
        m_badge->setText((leaderSeg
            ? QString::fromUtf8("跟随 %1%2").arg(cad::param::Serial::tag(leaderSeg->serial), modeWord)
            : QString::fromUtf8("跟随%1").arg(modeWord)) + shadowMark);
    } else {
        m_badge->setText(QString::fromUtf8("自由"));
    }
    if (shadowBasis)
        m_badge->setToolTip(cad::ui::TooltipFormatter::status(
            QStringLiteral("影子基准"),
            QStringLiteral("角度基准为隐藏影子线（本体拆开瞬间的方向快照）。旋转本体不再影响本线；把本线拖到其他线上时影子随之挂载（链式跟随）。可在属性面板改基准角，或点「清除影子」转为自由线。"),
            false));

    const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
    const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
    const auto tagOf = [](const cad::param::ParamPoint* p) {
        return p ? cad::param::Serial::tag(p->serial)
                 : QStringLiteral("?");
    };
    m_btnBasis->setText(m_rotateAnchor.active
        ? QString::fromUtf8("%1 → %2").arg(
              tagOf(m_rotateAnchor.anchorIsEnd ? ep : sp),
              tagOf(m_rotateAnchor.anchorIsEnd ? sp : ep))
        : QString::fromUtf8("%1 → %2").arg(tagOf(sp), tagOf(ep)));
    m_btnBasis->setToolTip(m_rotateAnchor.active
        ? cad::ui::TooltipFormatter::action(
            QStringLiteral("旋转锚心"),
            QStringLiteral("当前旋转支点所在端。点击换向按钮可切换旋转锚心端。"))
        : cad::ui::TooltipFormatter::action(
            cad::ui::str::kAngleBasis,
            cad::ui::str::kStartToEndTip));

    setProperty("stripFocus", m_focus == StripFocus::Pinned ? "pinned" : "hover");
    style()->unpolish(this);
    style()->polish(this);
    m_hint->setText(m_connectSession
        ? QStringLiteral("%1 确认 · %2 取消").arg(kbdBadge(QStringLiteral("Enter")), kbdBadge(QStringLiteral("Esc")))
        : m_focus == StripFocus::Pinned
            ? QStringLiteral("%1 解除锁定 · %2 确认").arg(kbdBadge(QStringLiteral("Esc")), kbdBadge(QStringLiteral("Enter")))
            : QString::fromUtf8("点击线段锁定后可编辑"));

    if (m_btnPasteLen) m_btnPasteLen->setEnabled(!m_lenEdit->isReadOnly());
    if (m_btnPasteAngle) m_btnPasteAngle->setEnabled(!m_angleEdit->isReadOnly());
}

void ContextStrip::applyTheme()
{
    const auto& tk = cad::ui::Theme::tokens();
    if (m_idLabel) {
        m_idLabel->setStyleSheet(QStringLiteral(
            "#stripSerial { %1 font-size: 10px; font-weight: 600; color: %2; background: %3; border: 1px solid %4; border-radius: 2px; padding: 2px 6px; }")
            .arg(cad::ui::ThemeTokens::kMonospaceFamily, tk.text1.name(), tk.surface3.name(), tk.borderStrong.name()));
    }
    if (m_placedPointBar) {
        m_placedPointBar->applyTheme();
    }
    const QString unitChip = cad::ui::chipButtonStyle();
    if (m_btnUnitAngle)
        m_btnUnitAngle->setStyleSheet(unitChip);
    if (m_btnUnitArc)
        m_btnUnitArc->setStyleSheet(unitChip);
    if (m_btnUnitChord)
        m_btnUnitChord->setStyleSheet(unitChip);

    refreshChrome();
    update();
}

QString ContextStrip::badgeText() const
{
    return m_badge ? m_badge->text() : QString();
}

QString ContextStrip::basisText() const
{
    return m_btnBasis ? m_btnBasis->text() : QString();
}

} // namespace cad::app
