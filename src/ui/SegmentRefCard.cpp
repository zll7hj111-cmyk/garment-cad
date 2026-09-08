#include "ui/SegmentRefCard.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include "ui/SegmentAlignPointCard.h"
#include "ui/SegmentShadowBasisCard.h"
#include "ui/SegmentAngleRefCard.h"
#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"

namespace cad::ui {

namespace {
} // namespace

SegmentRefCard::SegmentRefCard(cad::param::ParamDocument* doc,
                               CanvasScene* scene, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_scene(scene)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    m_alignCard = new SegmentAlignPointCard(m_doc, this);
    row->addWidget(m_alignCard);

    m_shadowCard = new SegmentShadowBasisCard(m_doc, this);
    m_shadowCard->setVisible(false);
    row->addWidget(m_shadowCard);

    m_angleCard = new SegmentAngleRefCard(m_doc, this);
    row->addWidget(m_angleCard, 1);

    lay->addLayout(row);

    connect(m_alignCard, &SegmentAlignPointCard::changed,
            this, &SegmentRefCard::changed);
    connect(m_shadowCard, &SegmentShadowBasisCard::changed,
            this, &SegmentRefCard::changed);
    connect(m_angleCard, &SegmentAngleRefCard::changed,
            this, &SegmentRefCard::changed);

    connect(m_alignCard, &SegmentAlignPointCard::rejectRequested,
            this, &SegmentRefCard::rejectRefInput);
    connect(m_angleCard, &SegmentAngleRefCard::rejectRequested,
            this, &SegmentRefCard::rejectRefInput);
}

void SegmentRefCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    if (m_alignCard) m_alignCard->setTarget(blockId, segmentId);
    if (m_shadowCard) m_shadowCard->setTarget(blockId, segmentId);
    if (m_angleCard) m_angleCard->setTarget(blockId, segmentId);
    refresh();
}

void SegmentRefCard::refresh()
{
    if (!m_doc) return;
    const auto* block = m_doc->findBlock(m_blockId);
    const auto* att = m_doc->findFollowerAttachmentOf(m_blockId);

    setVisible(true);

    const bool hasEnd = block && !block->endTargetPointId.isNull();
    const bool isBridge = block && block->isBridge;
    const bool dirHidden = hasEnd || isBridge;

    const auto* toBlkChk = att ? m_doc->findBlock(att->toBlockId) : nullptr;
    const bool shadowBasis = toBlkChk && toBlkChk->isShadow;

    if (m_alignCard) {
        m_alignCard->refresh();
    }

    if (dirHidden) {
        if (m_shadowCard) m_shadowCard->setVisible(false);
        if (m_angleCard) m_angleCard->setVisible(false);
    } else if (shadowBasis) {
        if (m_shadowCard) {
            m_shadowCard->setVisible(true);
            m_shadowCard->refresh();
        }
        if (m_angleCard) {
            m_angleCard->setVisible(true);
            m_angleCard->setShadowBasisMode(true);
            m_angleCard->refresh();
        }
    } else {
        if (m_shadowCard) m_shadowCard->setVisible(false);
        if (m_angleCard) {
            m_angleCard->setVisible(true);
            m_angleCard->setShadowBasisMode(false);
            m_angleCard->refresh();
        }
    }
}

void SegmentRefCard::rejectRefInput(const QString& reason)
{
    if (m_scene)
        m_scene->showToast(reason);
    if (m_alignCard)
        m_alignCard->flashRed(900);
    refresh();
}

} // namespace cad::ui
