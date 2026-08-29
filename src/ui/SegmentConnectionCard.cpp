#include "ui/SegmentConnectionCard.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include "ElaText.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Serial.h"
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
    buildSlideRow();
    buildShadowRow();
    buildDartRow();
    lay->addLayout(buildConnControls());
    lay->addWidget(m_slideRow);
    lay->addWidget(m_shadowRow);
    lay->addWidget(m_dartRow);

    connectSignals();
}

void SegmentConnectionCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    // 宿主记忆只属于被编辑的这条线: 换目标按当前连接重新播种 (断开则为空).
    m_hostMemBlockId = {};
    m_hostMemPointId = {};
    m_modeCache.reset();
    if (const cad::param::Attachment* att = findFollowerAttachment()) {
        m_hostMemBlockId = att->toBlockId;
        m_hostMemPointId = att->toPointId;
        m_modeCache = *att;
    }
    refresh();
}

void SegmentConnectionCard::refresh()
{
    refreshCard();
    // 断开记忆快照跟随最新连接状态 (取消勾选/清除时先快照再删; 重连原样恢复)。
    if (const cad::param::Attachment* att = findFollowerAttachment())
        m_modeCache = *att;
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

QString SegmentConnectionCard::leaderRefLabel(const cad::param::Attachment& att) const
{
    if (!m_doc) return QString();

    QString segPart;
    const cad::param::Block* leader = m_doc->findBlock(att.toBlockId);
    if (leader) {
        const cad::param::Segment* lseg = leader->findSegment(att.toSegmentId);
        if (lseg) {
            segPart = cad::param::Serial::tag(lseg->serial);
            if (!lseg->name.isEmpty())
                segPart += QStringLiteral("·") + lseg->name;
        }
    }
    return segPart.isEmpty() ? QStringLiteral("?") : segPart;
}

void SegmentConnectionCard::refreshCard()
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;

    const cad::param::Attachment* att = findFollowerAttachment();

    // 省道线 (用户拍板 2026-08): 起点 A 已挂、终点 = B 偏移解算 — 独立于附件
    // 体系。角度反算只读、d/β 可改、B 所在线段只读不修改（单向挂靠）。
    if (block && block->isDart()) {
        showDartState(*block, seg);
        return;
    }
    m_dartRow->setVisible(false);

    // 统一表单: 无状态区分 (用户 2026-12-15 拍板), 行集与标题恒定;
    // 自由线 = 连接字段为空的同一张卡。
    refreshUnifiedState(att, block, seg);
    if (block && seg)
        refreshConnectionToggles(att);
}

} // namespace cad::ui
