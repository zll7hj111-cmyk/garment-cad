#include "ui/SegmentAngleRefCard.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>

#include "ElaText.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Serial.h"
#include "parametric/FollowerAngle.h"
#include "geometry/Angle.h"
#include "geometry/Units.h"
#include "ui/PointRefEdit.h"
#include "ui/FormScaffold.h"
#include "ui/TooltipFormatter.h"
#include "ui/Theme.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockCommands.h"

namespace cad::ui {

namespace {
constexpr int kFieldH = 30;
constexpr int kBtnW = 48;
constexpr int kRefEditW = 88;

const cad::param::Attachment* findFollowerAttachment(const cad::param::ParamDocument* doc,
                                                     const QUuid& blockId)
{
    if (!doc) return nullptr;
    for (const auto& att : doc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId == blockId)
            return &att;
    }
    return nullptr;
}
} // namespace

SegmentAngleRefCard::SegmentAngleRefCard(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    auto mkSentence = [this](const QString& text) -> ElaText* {
        auto* t = new ElaText(text, 11, this);
        return t;
    };

    m_lblDirWord = mkSentence(QString::fromUtf8("方向："));
    m_lblDirWord->setFixedWidth(34);
    row->addWidget(m_lblDirWord);

    m_angleRefPoint = new PointRefEdit(m_doc, this);
    m_angleRefPoint->setObjectName(QStringLiteral("angleRefPointEdit"));
    m_angleRefPoint->setFixedWidth(kRefEditW);
    m_angleRefPoint->setVisible(false);
    row->addWidget(m_angleRefPoint);

    m_lblArrow = mkSentence(QString::fromUtf8("→"));
    m_lblArrow->setVisible(false);
    row->addWidget(m_lblArrow);

    m_angleRefPoint2 = new PointRefEdit(m_doc, this);
    m_angleRefPoint2->setObjectName(QStringLiteral("angleRefPoint2Edit"));
    m_angleRefPoint2->setFixedWidth(kRefEditW);
    m_angleRefPoint2->setVisible(false);
    row->addWidget(m_angleRefPoint2);

    m_btnResetBenchmark = new QPushButton(QString::fromUtf8("重设基准"), this);
    m_btnResetBenchmark->setObjectName(QStringLiteral("resetBenchmarkBtn"));
    m_btnResetBenchmark->setFixedHeight(kFieldH);
    m_btnResetBenchmark->setStyleSheet(cad::ui::chipButtonStyle());
    m_btnResetBenchmark->setCursor(Qt::PointingHandCursor);
    m_btnResetBenchmark->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("重设基准"),
        QStringLiteral("以当前实际姿态重新反算基准角度。")));
    m_btnResetBenchmark->setVisible(false);
    row->addWidget(m_btnResetBenchmark);

    row->addStretch(1);

    m_btnIndependent = new QPushButton(QString::fromUtf8("独立"), this);
    m_btnIndependent->setObjectName(QStringLiteral("angleBaseToggleBtn"));
    m_btnIndependent->setCheckable(true);
    m_btnIndependent->setFixedSize(kBtnW, kFieldH);
    m_btnIndependent->setStyleSheet(cad::ui::chipButtonStyle());
    m_btnIndependent->setCursor(Qt::PointingHandCursor);
    m_btnIndependent->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("独立基准"),
        QStringLiteral("角度改用世界角度（不跟任何线）；再点还原上次的角度基准。")));
    row->addWidget(m_btnIndependent);

    m_btnLinkCurrent = new QPushButton(QString::fromUtf8("链接当前线"), this);
    m_btnLinkCurrent->setObjectName(QStringLiteral("linkCurrentLineBtn"));
    m_btnLinkCurrent->setFixedHeight(kFieldH);
    m_btnLinkCurrent->setStyleSheet(cad::ui::chipButtonStyle());
    m_btnLinkCurrent->setCursor(Qt::PointingHandCursor);
    m_btnLinkCurrent->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("链接当前线"),
        QStringLiteral("清空自定义基准，角度跟随所连线段的方向。")));
    m_btnLinkCurrent->setVisible(false);
    row->addWidget(m_btnLinkCurrent);

    connect(m_angleRefPoint, &PointRefEdit::pointResolved,
            this, &SegmentAngleRefCard::onAngleRefPointResolved);
    connect(m_angleRefPoint2, &PointRefEdit::pointResolved,
            this, &SegmentAngleRefCard::onAngleRefPoint2Resolved);
    connect(m_btnIndependent, &QPushButton::toggled,
            this, &SegmentAngleRefCard::onIndependentToggled);
    connect(m_btnLinkCurrent, &QPushButton::clicked,
            this, &SegmentAngleRefCard::onLinkCurrentLineClicked);
    connect(m_btnResetBenchmark, &QPushButton::clicked,
            this, &SegmentAngleRefCard::onResetBenchmarkClicked);
}

void SegmentAngleRefCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    if (m_angleRefPoint)  m_angleRefPoint->clearPoint();
    if (m_angleRefPoint2) m_angleRefPoint2->clearPoint();
    refresh();
}

void SegmentAngleRefCard::setShadowBasisMode(bool shadowBasis)
{
    m_shadowBasisMode = shadowBasis;
}

void SegmentAngleRefCard::refresh()
{
    if (!m_doc) return;
    const auto* block = m_doc->findBlock(m_blockId);
    const auto* att = findFollowerAttachment(m_doc, m_blockId);

    if (m_shadowBasisMode) {
        if (m_lblDirWord) m_lblDirWord->setVisible(false);
        if (m_angleRefPoint) m_angleRefPoint->setVisible(false);
        if (m_lblArrow) m_lblArrow->setVisible(false);
        if (m_angleRefPoint2) m_angleRefPoint2->setVisible(false);
        if (m_btnLinkCurrent) m_btnLinkCurrent->setVisible(false);
        if (m_btnResetBenchmark) m_btnResetBenchmark->setVisible(false);
        if (m_btnIndependent) m_btnIndependent->setVisible(true);
    } else {
        if (m_angleRefPoint) m_angleRefPoint->setVisible(false);
        if (m_lblArrow) m_lblArrow->setVisible(false);
        if (m_angleRefPoint2) m_angleRefPoint2->setVisible(false);
        if (m_btnLinkCurrent) m_btnLinkCurrent->setVisible(false);
        if (m_btnIndependent) m_btnIndependent->setVisible(true);

        if (m_lblDirWord) {
            m_lblDirWord->setVisible(true);
            if (att && !att->angleIndependent) {
                const double refDeg = cad::geo::normalizeDeg180(
                    cad::geo::radToDeg(cad::param::effectiveAngleRefWorld(m_doc, *att)));
                QString text = QString::fromUtf8("母线基准：%1°").arg(cad::geo::Units::formatDegValue(refDeg));
                if (block && block->preservedBenchmarkAngle.has_value()) {
                    text += QString::fromUtf8(" (保持原基准 %1°)").arg(
                        cad::geo::Units::formatDegValue(*block->preservedBenchmarkAngle));
                    if (m_btnResetBenchmark) m_btnResetBenchmark->setVisible(true);
                } else {
                    if (m_btnResetBenchmark) m_btnResetBenchmark->setVisible(false);
                }
                m_lblDirWord->setText(text);
                m_lblDirWord->setFixedWidth(block && block->preservedBenchmarkAngle.has_value() ? 240 : 130);
            } else {
                m_lblDirWord->setText(QString::fromUtf8("自由线 (世界角)"));
                m_lblDirWord->setFixedWidth(110);
                if (m_btnResetBenchmark) m_btnResetBenchmark->setVisible(false);
            }
        }
    }

    refreshAngleRefRow(att);
}

void SegmentAngleRefCard::refreshAngleRefRow(const cad::param::Attachment* att)
{
    m_angleRefPoint->setExcludeBlock(m_blockId);
    m_angleRefPoint2->setExcludeBlock(m_blockId);
    const bool hasAtt = att != nullptr;
    const bool independent = hasAtt && att->angleIndependent;

    const QSignalBlocker ib(m_btnIndependent);
    m_btnIndependent->setChecked(independent);
    m_btnIndependent->setEnabled(hasAtt);
    if (independent) {
        m_btnIndependent->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("恢复基准跟随"),
            QStringLiteral("还原上次的角度基准，恢复角度跟随（反算零跳变）")));
    } else {
        m_btnIndependent->setToolTip(hasAtt
            ? cad::ui::TooltipFormatter::action(
                QStringLiteral("独立角度"),
                QStringLiteral("角度改用世界角度（不跟任何线）；输入框随之清空。"))
            : cad::ui::TooltipFormatter::action(
                QStringLiteral("独立角度"),
                QStringLiteral("需要先建立连接（自由线的角度本就是世界角度）。")));
    }

    m_angleRefPoint->setEnabled(true);
    m_angleRefPoint2->setEnabled(true);
    m_angleRefPoint->setAutoEcho(false);
    m_angleRefPoint2->setAutoEcho(false);

    if (m_btnLinkCurrent) {
        m_btnLinkCurrent->setEnabled(hasAtt && !att->angleRefBlockId.isNull());
        m_btnLinkCurrent->setToolTip(independent
            ? cad::ui::TooltipFormatter::action(
                QStringLiteral("链接当前线"),
                QStringLiteral("清空自定义基准并退出独立角，角度跟随所连线段的方向。"))
            : cad::ui::TooltipFormatter::action(
                QStringLiteral("链接当前线"),
                QStringLiteral("清空自定义基准，角度跟随所连线段的方向。")));
    }

    if (!att) return;

    if (independent) {
        m_angleRefPoint->clearPoint();
        m_angleRefPoint2->clearPoint();
        return;
    }

    if (att->angleRefBlockId.isNull()) {
        const auto* toBlk = m_doc->findBlock(att->toBlockId);
        if (toBlk && toBlk->isShadow) {
            m_angleRefPoint->clearPoint();
            m_angleRefPoint2->clearPoint();
            return;
        }
        m_angleRefPoint->setPoint(att->toBlockId, att->toPointId);
        m_angleRefPoint->setAutoEcho(true);
        if (const auto* host = m_doc->findBlock(att->toBlockId)) {
            if (const auto* hostSeg = host->findSegment(att->toSegmentId)) {
                const QUuid other = (hostSeg->startPointId == att->toPointId)
                    ? hostSeg->endPointId : hostSeg->startPointId;
                m_angleRefPoint2->setPoint(att->toBlockId, other);
                m_angleRefPoint2->setAutoEcho(true);
            } else {
                m_angleRefPoint2->clearPoint();
            }
        } else {
            m_angleRefPoint2->clearPoint();
        }
        return;
    }

    m_angleRefPoint->setPoint(att->angleRefBlockId, att->angleRefPointId);
    if (!att->angleRef2BlockId.isNull() && !att->angleRef2PointId.isNull())
        m_angleRefPoint2->setPoint(att->angleRef2BlockId, att->angleRef2PointId);
    else
        m_angleRefPoint2->clearPoint();
}

void SegmentAngleRefCard::onAngleRefPointResolved(const QUuid& blockId,
                                                  const QUuid& pointId)
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment(m_doc, m_blockId);
    if (!att) {
        m_angleRefPoint->setPoint(blockId, pointId);
        refreshAngleRefRow(nullptr);
        return;
    }

    const auto* leader = m_doc->findBlock(blockId);
    if (!leader || !leader->findPoint(pointId)) { refresh(); return; }
    if (blockId == att->fromBlockId) {
        emit rejectRequested(QString::fromUtf8("该点是本线自身的点，不能作为基准方向点"));
        return;
    }

    const QUuid segId = leader->exitSegmentAtPoint(pointId);

    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
            m_doc, att->id, blockId, segId, pointId));
    else
        m_doc->setAttachmentAngleRef(att->id, blockId, segId, pointId);

    refresh();
    emit changed();
}

void SegmentAngleRefCard::onAngleRefPoint2Resolved(const QUuid& blockId,
                                                   const QUuid& pointId)
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment(m_doc, m_blockId);
    if (!att) {
        m_angleRefPoint2->setPoint(blockId, pointId);
        refreshAngleRefRow(nullptr);
        return;
    }

    const auto* leader = m_doc->findBlock(blockId);
    if (!leader || !leader->findPoint(pointId)) { refresh(); return; }
    if (blockId == att->fromBlockId) {
        emit rejectRequested(QString::fromUtf8("该点是本线自身的点，不能作为基准方向点"));
        return;
    }

    const bool autoMode = att->angleRefBlockId.isNull();
    const QUuid ref1Block = autoMode ? att->toBlockId : att->angleRefBlockId;
    const QUuid ref1Point = autoMode ? att->toPointId : att->angleRefPointId;
    if (blockId == ref1Block && pointId == ref1Point) {
        emit rejectRequested(QString::fromUtf8("点2 与点1 相同：两点连线为零长度，请换一个点"));
        return;
    }

    if (autoMode) {
        const auto* host = m_doc->findBlock(att->toBlockId);
        const QUuid hostSeg = host ? host->exitSegmentAtPoint(att->toPointId)
                                   : QUuid();
        if (auto* stack = m_doc->undoStack())
            stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
                m_doc, att->id,
                att->toBlockId, hostSeg, att->toPointId, blockId, pointId));
        else
            m_doc->setAttachmentAngleRef(att->id, att->toBlockId, hostSeg,
                                         att->toPointId, blockId, pointId);
    } else {
        if (auto* stack = m_doc->undoStack())
            stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
                m_doc, att->id,
                att->angleRefBlockId, att->angleRefSegmentId, att->angleRefPointId,
                blockId, pointId));
        else
            m_doc->setAttachmentAngleRef(att->id,
                                         att->angleRefBlockId,
                                         att->angleRefSegmentId,
                                         att->angleRefPointId,
                                         blockId, pointId);
    }
    refresh();
    emit changed();
}

void SegmentAngleRefCard::onIndependentToggled(bool checked)
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment(m_doc, m_blockId);
    if (!att) { refresh(); return; }
    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleIndependentCommand(
            m_doc, att->id, /*angleIndependent=*/checked));
    else
        m_doc->setAttachmentAngleIndependent(att->id, checked);
    refresh();
    emit changed();
}

void SegmentAngleRefCard::onLinkCurrentLineClicked()
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment(m_doc, m_blockId);
    if (!att || att->angleRefBlockId.isNull()) {
        refresh();
        return;
    }
    m_angleRefPoint->clearPoint();
    m_angleRefPoint2->clearPoint();
    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
            m_doc, att->id, QUuid(), QUuid()));
    else
        m_doc->setAttachmentAngleRef(att->id, QUuid(), QUuid());
    refresh();
    emit changed();
}

void SegmentAngleRefCard::onResetBenchmarkClicked()
{
    if (!m_doc) return;
    auto* blk = m_doc->findBlock(m_blockId);
    if (!blk) return;
    blk->preservedBenchmarkAngle.reset();
    const auto* att = findFollowerAttachment(m_doc, m_blockId);
    if (att) {
        const auto* toBlk = m_doc->findBlock(att->toBlockId);
        if (toBlk) {
            const double refWorld = cad::param::effectiveAngleRefWorld(m_doc, *att);
            const double localDir = blk->directionAtPoint(att->fromPointId);
            const double angleDeg = cad::param::backSolveFollowerAngle(
                blk->transform.rotation, localDir, refWorld);
            if (auto* stack = m_doc->undoStack()) {
                stack->push(new cad::cmd::SetFollowerAngleCommand(m_doc, att->id, angleDeg));
            } else {
                if (auto* mutAtt = m_doc->findAttachment(att->id)) {
                    mutAtt->followerAngle = angleDeg;
                }
                m_doc->resolveAll();
            }
        }
    }
    refresh();
    emit changed();
}

} // namespace cad::ui
