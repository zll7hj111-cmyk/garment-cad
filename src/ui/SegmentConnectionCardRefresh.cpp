#include "ui/SegmentConnectionCard.h"

#include <algorithm>
#include <cmath>

#include "ElaCheckBox.h"
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
// 自由线 = 连接字段为空; 无连接时 清除/拆开/独立角度/滑轨 禁用仍显示。
// 角度/引用线段/指向点 已抽出 (SegmentAngleCard / SegmentRefCard)。

void SegmentConnectionCard::refreshUnifiedState(const cad::param::Attachment* att,
                                                cad::param::Block* block,
                                                cad::param::Segment* seg)
{
    (void)block;
    (void)seg;
    const bool hasAtt = att != nullptr;

    // ── 连接行 (恒显同形): [连接线段][L#·名][连接点 P#][清除][拆开] ──
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
        : "输入目标点 P 编号回车建立连接；断开后记忆最近宿主，重新勾选「连接线段」可恢复"));
    m_btnClearConn->setVisible(true);
    m_btnClearConn->setEnabled(hasAtt);
    // 拆开: 无连接/仅角度态禁用 (仍显示, 表单同形)。
    m_btnAngleOnly->setVisible(true);
    m_btnAngleOnly->setEnabled(hasAtt && !att->angleOnly);
    m_lblLayerBadge->setVisible(false);
    m_lblAngleOnlyBadge->setVisible(false);

    // ── 滑轨行 (恒显; 无连接/拆开态禁用) ──
    m_slideRow->setVisible(true);
    {
        const QSignalBlocker cb(m_cmbSlideMode);
        const QSignalBlocker sb1(m_editSlideAlong);
        const QSignalBlocker sb2(m_editSlidePerp);
        const bool slideOk = hasAtt && !att->angleOnly;
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
            : (att->angleOnly
                ? QString::fromUtf8("拆开状态（仅角度）与滑轨互斥：先勾选「连接线段」恢复全连接后可用。")
                : QString::fromUtf8("滑轨模式（抽屉式单向滑动）：全连接 = 位置吸附 + 角度跟随（默认）；"
                                    "沿线滑动 = 仅沿基准线方向可滑；垂直拉出 = 仅垂直基准线可拉。"
                                    "进入滑轨后拖动跟随线只沿对应方向动，角度跟随始终保留。与「拆开（保留角度）」互斥。"));
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

void SegmentConnectionCard::refreshConnectionToggles(const cad::param::Attachment* att)
{
    const QUuid hostBlock = att ? att->toBlockId : m_refConnPoint->resolvedBlockId();
    const QUuid hostPoint = att ? att->toPointId : m_refConnPoint->resolvedPointId();
    const auto* hostBlk = !hostBlock.isNull() ? m_doc->findBlock(hostBlock) : nullptr;
    const auto* hostPt = hostBlk ? hostBlk->findPoint(hostPoint) : nullptr;

    const QSignalBlocker b(m_chkFollowHost);
    m_chkFollowHost->setText(hostPt
        ? QString::fromUtf8("连接线段 %1")
            .arg(cad::param::Serial::tag(hostPt->serial))
        : (att ? QString::fromUtf8("连接线段（已删除）")
               : QString::fromUtf8("连接线段")));
    if (att && att->angleOnly)
        m_chkFollowHost->setText(m_chkFollowHost->text()
            + QString::fromUtf8("（仅角度）"));
    m_chkFollowHost->setChecked(att != nullptr);
    m_chkFollowHost->setEnabled(true);
    // 语义 (用户拍板 2026-08 复旧 + 2026-12-15 改名): 勾选 = 连接 (位置+角度跟随,
    // 默认焊接); 取消 = 彻底断开。断开时快照完整配置, 重新勾选原样恢复。
    m_chkFollowHost->setToolTip(QString::fromUtf8(
        "勾选后起点吸附到宿主点并建立角度跟随"
        "（默认焊接：拖任一端整对移动不拆，拆散走 D 键快拆）；"
        "取消勾选 = 彻底断开（位置吸附与角度跟随一起解除）；"
        "断开时快照完整连接配置（角度公式/引用线段/滑轨/焊接），"
        "再次勾选原样恢复（角度反算、无跳变）；"
        "仅角度状态由拖拆 / D 键快拆引入。"));
    // 复选框 = 唯一连接开关 (恒显)。
    m_chkFollowHost->setVisible(true);

    // 连接保护 (拖动保护, 语义 2026-08 复旧): checked = 焊接 (isLocked)。
    // 仅角度态勾选 = 恢复完整连接 + 重新焊接; 滑轨态禁用。
    const QSignalBlocker lb(m_chkLockConn);
    m_chkLockConn->setChecked(att != nullptr && att->isLocked);
    const bool lockEnabled = att != nullptr
        && att->slideMode == cad::param::SlideMode::None;
    m_chkLockConn->setEnabled(lockEnabled);
    m_chkLockConn->setVisible(false);  // 折叠进「连接线段」开关语义 (不再单独展示).

    // 独立角度: 只有完整连接 (非仅角度/非滑轨) 时可用。
    const QSignalBlocker ab(m_chkAngleIndependent);
    m_chkAngleIndependent->setChecked(att && att->angleIndependent);
    const bool aiEnabled = att && !att->angleOnly
        && att->slideMode == cad::param::SlideMode::None;
    m_chkAngleIndependent->setEnabled(aiEnabled);
    m_chkAngleIndependent->setVisible(true);  // 统一表单恒显 (无连接时禁用).
    if (!aiEnabled) {
        const QString tip = att
            ? QString::fromUtf8(
                "当前不是普通全连接（仅角度/滑轨态与角度独立互斥），请先恢复全连接。")
            : QString::fromUtf8("当前是自由线，无连接可设。");
        if (m_chkAngleIndependent->toolTip() != tip)
            m_chkAngleIndependent->setToolTip(tip);
    } else {
        m_chkAngleIndependent->setToolTip(QString::fromUtf8(
            "勾选后：位置仍吸附在基准点，但本线角度不再跟随基准线，"
            "可用旋转/角度公式自由控制。取消勾选恢复角度跟随（自动反算当前角度，无跳变）。"));
    }
}

} // namespace cad::ui
