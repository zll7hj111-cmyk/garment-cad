#include "ui/SegmentAlignPointCard.h"

#include <QHBoxLayout>
#include <QTimer>

#include "ElaText.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "ui/PointRefEdit.h"
#include "ui/TooltipFormatter.h"
#include "ui/Theme.h"
#include "document/commands/AttachmentCommands.h"
#include "ui/UiStrings.h"

namespace cad::ui {

namespace {
constexpr int kAlignEditW = 56; ///< 对齐点输入框宽 (本线端点, 短 tag).

} // namespace

SegmentAlignPointCard::SegmentAlignPointCard(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    auto* lblAlign = new ElaText(QString::fromUtf8("对齐点"), 11, this);
    lblAlign->setFixedWidth(40);
    lblAlign->setToolTip(cad::ui::TooltipFormatter::action(
        cad::ui::str::kAlignPoint,
        QStringLiteral("本线段的哪个端点钉在目标点上（输入本线端点 P#；与调换进/出无关）")));
    row->addWidget(lblAlign);

    m_alignPointEdit = new PointRefEdit(m_doc, this);
    m_alignPointEdit->setObjectName(QStringLiteral("alignPointEdit"));
    m_alignPointEdit->setFixedWidth(kAlignEditW);
    m_alignPointEdit->setPlaceholderText(QStringLiteral("P#"));
    m_alignPointEdit->setToolTip(cad::ui::TooltipFormatter::action(
        cad::ui::str::kAlignPoint,
        QStringLiteral("本线段的哪个端点钉在目标点上。只接受本线端点（P#）；与调换进/出无关。")));
    row->addWidget(m_alignPointEdit);

    connect(m_alignPointEdit, &PointRefEdit::pointResolved,
            this, &SegmentAlignPointCard::onAlignPointResolved);
}

void SegmentAlignPointCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    if (m_alignPointEdit) m_alignPointEdit->clearPoint();
    refresh();
}

void SegmentAlignPointCard::refresh()
{
    if (!m_doc) return;
    m_alignPointEdit->setRestrictToBlock(m_blockId);
    m_alignPointEdit->setAutoEcho(false);
    const auto* block = m_doc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    const auto* att = m_doc->findFollowerAttachmentOf(m_blockId);
    const bool isBridge = block && block->isBridge;
    const bool hasEndTarget = block && !block->endTargetPointId.isNull();
    const bool isAngleOnly = att && att->angleOnly;

    if (isAngleOnly) {
        m_alignPointEdit->clearPoint();
    } else if (att && !att->fromPointId.isNull() && !isBridge && !hasEndTarget) {
        m_alignPointEdit->setPoint(att->fromBlockId, att->fromPointId);
    } else if (seg) {
        m_alignPointEdit->setPoint(m_blockId, seg->startPointId);
        m_alignPointEdit->setAutoEcho(true);
    } else {
        m_alignPointEdit->clearPoint();
    }
    const bool alignLocked = isBridge || hasEndTarget;
    m_alignPointEdit->setEnabled(!alignLocked);
    m_alignPointEdit->setToolTip(alignLocked
        ? (isBridge
               ? cad::ui::TooltipFormatter::status(QStringLiteral("桥接线对齐点"), QStringLiteral("两端都钉在宿主点上，没有进点（长度由两点距离决定）"), false)
               : cad::ui::TooltipFormatter::status(QStringLiteral("终点指向生效"), QStringLiteral("进点锁定在起点（终点端用于指向目标位置）"), false))
        : cad::ui::TooltipFormatter::action(
              QStringLiteral("对齐点 (进点)"),
              QStringLiteral("本线段的哪个端点钉在目标点上（输入本线端点 P#）。与调换进/出无关——箭头只是身份标签，对齐点才是实际钉点。")));
}

void SegmentAlignPointCard::onAlignPointResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_doc) return;
    const auto* att = m_doc->findFollowerAttachmentOf(m_blockId);
    if (!att) {
        emit rejectRequested(QString::fromUtf8("请先建立连接，再设置对齐点"));
        return;
    }
    if (blockId != att->fromBlockId || pointId == att->fromPointId) {
        refresh();
        return;
    }
    if (auto* stack = m_doc->undoStack()) {
        stack->push(new cad::cmd::SetAlignPointCommand(
            m_doc, att->id, pointId));
    } else {
        auto* mut = m_doc->findAttachment(att->id);
        if (mut) {
            cad::cmd::SetAlignPointCommand cmd(m_doc, att->id, pointId);
            cmd.redo();
        }
    }
    refresh();
    emit changed();
}

void SegmentAlignPointCard::flashRed(int ms)
{
    if (!m_alignPointEdit) return;
    const QString saved = m_alignPointEdit->styleSheet();
    m_alignPointEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { border: 1px solid %1; border-radius: 3px;"
        " background: %2; }")
        .arg(cad::ui::Theme::tokens().danger.name(),
             cad::ui::Theme::rgbaCss(cad::ui::Theme::tokens().danger, 0.125)));
    QTimer::singleShot(ms, this, [this, saved] {
        if (m_alignPointEdit) m_alignPointEdit->setStyleSheet(saved);
    });
}

} // namespace cad::ui
