#include "ui/SegmentConnectionCard.h"

#include <algorithm>
#include <cmath>

#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include <QSignalBlocker>
#include <QComboBox>

#include "ui/PointRefEdit.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "ui/Theme.h"

namespace cad::ui {

// ── refresh 分块 (SegmentConnectionCardRefresh.cpp): 统一连接表单刷新 ──
// 2026-12-15 用户拍板: 不做面板状态区分 (同一张卡、同样行集、标题恒定),
// 自由线 = 连接字段为空; 无连接时 清除/拆开/滑轨 禁用仍显示。
// 2026-xx 用户再拍板: 「连接线段」「独立角度」复选框删除 —— 状态完全由
// 行内表达 (连接行 + 基准行 + 拆开/重连双面按钮)。
// 角度/引用线段/指向点 已抽出 (SegmentAngleCard / SegmentRefCard)。

void SegmentConnectionCard::refreshUnifiedState(const cad::param::Attachment* att,
                                                cad::param::Block* block,
                                                cad::param::Segment* seg)
{
    (void)block;
    (void)seg;
    const bool hasAtt = att != nullptr;

    // ── 连接行 (恒显同形): [连接线段][L#·名][连接点 P#][拆开/重连] ──
    m_connRow->setVisible(true);
    m_lblConnLabel->setText(QString::fromUtf8("连接线段"));
    m_lblLeaderRef->setVisible(true);
    m_lblConnSub->setVisible(true);
    {
        const QSignalBlocker lb(m_lblLeaderRef);
        const QSignalBlocker cb(m_refConnPoint);
        m_lblLeaderRef->clear();
        if (hasAtt && !att->angleOnly)
            m_lblLeaderRef->setText(leaderRefLabel(*att));
        m_refConnPoint->setExcludeBlock(m_blockId);
        if (hasAtt && !att->angleOnly)
            m_refConnPoint->setPoint(att->toBlockId, att->toPointId);
        else
            m_refConnPoint->clearPoint();
    }
    m_lblLeaderRef->setEnabled(hasAtt);
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
    m_lblLayerBadge->setVisible(false);
    m_lblAngleOnlyBadge->setVisible(false);

    // ── 滑轨行 (恒显; 无连接/拆开/独立角态禁用) ──
    m_slideRow->setVisible(true);
    {
        const QSignalBlocker cb(m_cmbSlideMode);
        const QSignalBlocker sb1(m_editSlideAlong);
        const QSignalBlocker sb2(m_editSlidePerp);
        const bool slideOk = hasAtt && !att->angleOnly && !att->angleIndependent;
        m_cmbSlideMode->setCurrentIndex(hasAtt ? static_cast<int>(att->slideMode) : 0);
        m_cmbSlideMode->setEnabled(slideOk);
        m_editSlideAlong->setEnabled(slideOk);
        m_editSlidePerp->setEnabled(slideOk);
        const bool hasSlide = hasAtt
            && att->slideMode != cad::param::SlideMode::None;
        // 数值或公式 (cm 域): 公式显示原文本; 数值去尾零回显 (用户 2026-12: 去掉 .00)。
        m_editSlideAlong->setText(hasSlide
            ? (!att->slideAlongFormula.isEmpty()
                   ? att->slideAlongFormula
                   : cad::geo::Units::formatCmTrimmed(att->slideAlongMm))
            : QString());
        m_editSlidePerp->setText(hasSlide
            ? (!att->slidePerpFormula.isEmpty()
                   ? att->slidePerpFormula
                   : cad::geo::Units::formatCmTrimmed(att->slidePerpMm))
            : QString());
        const QString slideTip = !hasAtt
            ? QString::fromUtf8("连接后可设：沿基准线单向滑动（角度跟随始终保留）。")
            : (!slideOk
                ? QString::fromUtf8("滑轨需要完整连接（位置吸附 + 角度跟随）：先「重连」恢复两个维度。")
                : QString::fromUtf8("滑轨模式（抽屉式单向滑动）：全连接 = 位置吸附 + 角度跟随（默认）；"
                                    "沿线滑动 = 仅沿基准线方向可滑；垂直拉出 = 仅垂直基准线可拉。"
                                    "进入滑轨后拖动跟随线只沿对应方向动，角度跟随始终保留。与「拆开」互斥。"));
        if (m_cmbSlideMode->toolTip() != slideTip)
            m_cmbSlideMode->setToolTip(slideTip);
    }
    m_lblSlideBadge->setVisible(false);

    // ── 影子偏转行 (§2.6): 连接存在且角度被基准驱动时恒显 (用户 2026-08-28
    //    拍板"常显可控") —— 数值 0 时归零按钮禁用; 角度独立态不展示 (影子
    //    不参与驱动) ──
    {
        const bool showShadow = hasAtt && !att->angleIndependent;
        m_shadowRow->setVisible(showShadow);
        if (showShadow) {
            const QSignalBlocker vb(m_lblShadowValue);
            m_lblShadowValue->setText(
                cad::geo::Units::formatDegTrimmed(att->baselineOffsetDeg));
            m_btnShadowReset->setEnabled(
                std::abs(att->baselineOffsetDeg) > 1e-9);
        }
    }
}

} // namespace cad::ui
