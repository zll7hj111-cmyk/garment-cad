#include "ui/SegmentConnectionCard.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include "ElaText.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "canvas/CanvasScene.h"

namespace cad::ui {

SegmentConnectionCard::SegmentConnectionCard(cad::param::ParamDocument* doc,
                                             CanvasScene* scene, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_scene(scene)
{
    // 纯行组 (2026-12 去卡框化): 无边框/无标题, 由属性页「连接」分区提供标题。
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    // ── 各行构建 (SegmentConnectionCardBuild.cpp) ──
    buildConnRow(lay);
    buildEndRow(lay);
    lay->addWidget(m_endRow);

    connectSignals();
}

void SegmentConnectionCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    // 换线时清终点拆开记忆 (记忆只属于当前编辑目标的生命周期)。
    m_endMemBlock = QUuid();
    m_endMemPoint = QUuid();
    m_endMemOffset = 0.0;
    refresh();
}

void SegmentConnectionCard::refresh()
{
    refreshCard();
}

const cad::param::Attachment* SegmentConnectionCard::findFollowerAttachment() const
{
    if (!m_doc) return nullptr;
    for (const auto& att : m_doc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId == m_blockId)
            return &att;
    }
    return nullptr;
}

void SegmentConnectionCard::refreshCard()
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);

    const cad::param::Attachment* att = findFollowerAttachment();

    // 统一表单: 无状态区分 (用户 2026-12-15 拍板), 行集与标题恒定;
    // 自由线 = 连接字段为空的同一张卡。
    refreshUnifiedState(att, block);
}

} // namespace cad::ui
