#include "ui/SegmentConnectionCard.h"

#include <algorithm>
#include <cmath>

#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"   // m_editEndOffset (指向偏移° 输入框)
#include <QSignalBlocker>

#include "ui/PointRefEdit.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "geometry/Units.h"

namespace cad::ui {

// ── refresh 分块 (SegmentConnectionCardRefresh.cpp): 统一连接表单刷新 ──
// 2026-12-15 用户拍板: 不做面板状态区分 (同一张卡、同样行集、标题恒定),
// 自由线 = 连接字段为空; 无连接时 清除/拆开 禁用仍显示。
// 2026-xx 用户再拍板: 「连接线段」「独立角度」复选框删除 —— 状态完全由
// 行内表达 (连接行 + 基准行 + 拆开/重连双面按钮)。
// 角度/引用线段/指向点 已抽出 (SegmentAngleCard / SegmentRefCard)。
// 滑轨已上移到 LinePropertyDialog「摆放」分区 (2026-xx §3)。

void SegmentConnectionCard::refreshUnifiedState(const cad::param::Attachment* att,
                                                cad::param::Block* block)
{
    const bool hasAtt = att != nullptr;

    // ── 连接行 (恒显同形): [连接线段][L#·名][连接点 P#][拆开/重连] ──
    m_connRow->setVisible(true);
    m_refLeaderSeg->setVisible(true);
    m_lblConnSub->setVisible(true);
    {
        const QSignalBlocker lb(m_refLeaderSeg);
        const QSignalBlocker cb(m_refConnPoint);
        m_refLeaderSeg->setExcludeBlock(m_blockId);
        if (hasAtt && !att->angleOnly)
            m_refLeaderSeg->setPoint(att->toBlockId, att->toPointId);
        else
            m_refLeaderSeg->clearPoint();
        m_refConnPoint->setExcludeBlock(m_blockId);
        if (hasAtt && !att->angleOnly)
            m_refConnPoint->setPoint(att->toBlockId, att->toPointId);
        else
            m_refConnPoint->clearPoint();
    }
    m_refConnPoint->setToolTip(QString::fromUtf8(hasAtt
        ? "输入目标点 P 编号回车重定向到该点（角度反算无跳变）"
        : "输入目标点 P 编号回车建立连接"));

    // ── 拆开/重连 双面按钮 (位置维度, 2026-xx 用户拍板): 拆开 = 位置自由
    // (angleOnly, 角度仍跟随基准线); 重连 = 位置重新吸附回原宿主 + 重新焊接。
    // 与角度维度 (SegmentRefCard 基准点按钮) 独立 —— 双拆开 = 自由线。
    m_btnDetach->setVisible(true);
    m_btnDetach->setText(hasAtt && att->angleOnly
        ? QString::fromUtf8("重连") : QString::fromUtf8("拆开"));
    m_btnDetach->setEnabled(hasAtt);
    if (hasAtt && att->angleOnly) {
        m_btnDetach->setToolTip(QString::fromUtf8(
            "重新连接：位置重新吸附回原宿主点并重新焊接，角度基准保留"));
    } else {
        m_btnDetach->setToolTip(QString::fromUtf8(
            "拆开：解除位置吸附（角度仍跟随基准线，快拆与 D 键同语义）；"
            "配合基准点「拆开」可让位置与角度都自由（自由线）"));
    }

    // ── 终点连接行 (2026-xx 每端完整连接) ──
    refreshEndRow(block);
}

void SegmentConnectionCard::refreshEndRow(const cad::param::Block* block)
{
    const bool hasEnd = block && !block->endTargetPointId.isNull();
    const bool canReconnect = !hasEnd && !m_endMemBlock.isNull()
        && !m_endMemPoint.isNull();
    const bool hostAlive = canReconnect && m_doc
        && m_doc->findBlock(m_endMemBlock) != nullptr;

    // 连接线段 (PointRefEdit 回显; 可输入 L#/P# 重定向)。
    {
        const QSignalBlocker lb(m_refEndLeaderSeg);
        const QSignalBlocker cb(m_refEndPoint);
        m_refEndLeaderSeg->setExcludeBlock(m_blockId);
        if (hasEnd)
            m_refEndLeaderSeg->setPoint(block->endTargetBlockId,
                                        block->endTargetPointId);
        else
            m_refEndLeaderSeg->clearPoint();
        m_refEndPoint->setExcludeBlock(m_blockId);
        if (hasEnd)
            m_refEndPoint->setPoint(block->endTargetBlockId,
                                    block->endTargetPointId);
        else
            m_refEndPoint->clearPoint();
    }
    m_refEndPoint->setToolTip(QString::fromUtf8(hasEnd
        ? "输入目标点 P 编号回车重定向终点指向（角度反算无跳变）"
        : "输入目标点 P 编号回车建立终点连接"));

    // 偏移(°).
    {
        const QSignalBlocker ob(m_editEndOffset);
        m_editEndOffset->setText(hasEnd
            ? cad::geo::Units::formatDegTrimmed(block->endTargetOffset)
            : QString());
    }
    m_editEndOffset->setEnabled(hasEnd);

    // 拆开/重连 双面按钮: 有指向 = 拆开 (记忆目标); 无指向但有记忆且宿主存活
    // = 重连 (恢复); 其余 = 禁用「拆开」(与起点行同形)。
    m_btnEndDetach->setEnabled(hasEnd || (canReconnect && hostAlive));
    m_btnEndDetach->setText((hasEnd || !canReconnect || !hostAlive)
        ? QString::fromUtf8("拆开") : QString::fromUtf8("重连"));
    m_btnEndDetach->setToolTip(hasEnd
        ? QString::fromUtf8("拆开：清除终点连接（终点恢复自由，已发布的长度测量保留）")
        : (canReconnect && hostAlive
            ? QString::fromUtf8("重连：恢复到最近一次的目标点")
            : QString::fromUtf8("暂无终点连接：输入目标点 P# 建立")));
}

} // namespace cad::ui
