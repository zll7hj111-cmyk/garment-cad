#include "SegmentConnectionCard.h"

#include <algorithm>
#include <cmath>

#include "ElaCheckBox.h"
#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include <QSignalBlocker>
#include <QComboBox>
#include <QVBoxLayout>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "parametric/FollowerAngle.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "canvas/CanvasScene.h"
#include "PointRefEdit.h"
#include "tools/LayerFeedback.h"
#include "document/commands/AttachmentCommands.h"
#include "ui/Theme.h"

namespace cad::tools {

// ── 角度显示约定（2026-08 v3 定稿，用户拍板）────────────────────────────
// 存储域 α ∈ [0, 360°)；连接线显示 = 带符号折角 [−180°, +180°]；自由线
// 绝对角度 = 0~360° 逆时针为正（行业默认，AutoCAD 同）。统一实现收口在
// geometry/Angle.h（normalizeDeg360 / normalizeDeg180）与 geometry/Units.h
// （formatDegValue / formatDegTrimmed）；跨层连接 toast/badge 文案统一在
// tools/LayerFeedback.h。此处不再本地复制。

SegmentConnectionCard::SegmentConnectionCard(cad::param::ParamDocument* doc,
                                             CanvasScene* scene, QWidget* parent)
    : ElaScrollPageArea(parent)
    , m_doc(doc)
    , m_scene(scene)
{
    // ElaScrollPageArea's constructor hard-codes setFixedHeight(75); lift it
    // so the card sizes itself from its content layout.
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    auto* angleLayout = new QVBoxLayout(this);
    angleLayout->setContentsMargins(10, 8, 10, 10);
    angleLayout->setSpacing(6);

    m_titleLabel = new ElaText(QString::fromUtf8("绝对角度 · 连接"), 13, this);
    m_titleLabel->setStyleSheet("font-weight:600;");
    angleLayout->addWidget(m_titleLabel);

    // ── 各行构建 (SegmentConnectionCardBuild.cpp) ──
    buildModeRow(angleLayout);
    buildConnRow(angleLayout);
    QHBoxLayout* angleRow = buildAngleRow();
    buildDartRow();
    buildSlideRow();
    buildAngleRefRow();
    buildAimRow();

    // ── 连接控制组 (用户拍板 2026-09 分组) ──
    // 位置吸附 / 拖动保护 并排一行, 滑轨紧随其后, 再是角度编辑行与省道行。
    // 布局顺序: 连接行 → 控制组 → 滑轨 → 角度基准 → 指向 → 角度编辑 → 省道
    // — 先看连接状态 (吸附? 保护? 滑轨?), 再调角度。
    angleLayout->addLayout(buildConnControls());
    angleLayout->addWidget(m_slideRow);
    angleLayout->addWidget(m_angleRefRow);
    angleLayout->addWidget(m_aimRow);
    angleLayout->addLayout(angleRow);
    angleLayout->addWidget(m_dartRow);

    connectSignals();
}

void SegmentConnectionCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    // 宿主记忆只属于被编辑的这条线: 换目标按当前连接重新播种 (断开则为空),
    // 不从上一条线带过来.
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
    populateAngleField();
    refreshCard();
    // 模式切换缓存跟随最新连接状态 (detach 时不重置 —— 需先快照再删)。
    if (const cad::param::Attachment* att = findFollowerAttachment())
        m_modeCache = *att;
}

const cad::param::Attachment* SegmentConnectionCard::findFollowerAttachment() const
{
    if (!m_doc) return nullptr;
    for (const auto& att : m_doc->attachments()) {
        // Position pins (bridge lines) are not construction-angle followers.
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
                segPart += QStringLiteral("\u00b7") + lseg->name;
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
    // Restore the angle-editing row for normal lines (dart state hides it).
    m_editAngle->setVisible(true);
    m_lblAngleCaption->setVisible(true);
    m_lblFollowValue->setVisible(false);
    m_lblFxAngle->setVisible(false);
    m_dartRow->setVisible(false);
    m_modeRow->setVisible(true);

    refreshModeCombo(att);
    if (att)
        refreshConnectedState(att, block, seg);
    else
        refreshFreeState(block, seg);

    // 位置吸附 / 拖动保护 / 角度独立 checkboxes + 角度基准分离行:
    // 目标线存在时才有意义 (宿主标签读宿主点 serial)。
    if (block && seg) {
        refreshConnectionToggles(att);
        refreshAngleRefRow(att);
    }

    // 指向（终点指向）行。
    if (block)
        refreshAimRow(block);
}

void SegmentConnectionCard::populateAngleField()
{
    cad::param::Block* block = m_doc ? m_doc->findBlock(m_blockId) : nullptr;
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;

    // Bridge lines never edit the angle — the dialog mirrors the same for the
    // length editor; re-enable here so a re-target to a normal line recovers.
    m_editAngle->setEnabled(!(block && block->isBridge)
                            && !(block && !block->endTargetPointId.isNull()));

    const cad::param::Attachment* att = findFollowerAttachment();

    // 角度独立模式: 位置仍吸附, 但角度由本线自己控制 —— 编辑器显示/编辑
    // 绝对角度 (与自由线相同), 而不是 followerAngle。
    if (att && att->angleIndependent && block && seg) {
        const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
        const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
        if (ep && !ep->angleFormula.isEmpty()) {
            m_editAngle->setText(ep->angleFormula);
            m_lblFxAngle->setVisible(true);
        } else if (sp && ep && sp->resolved && ep->resolved) {
            cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
            cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
            double angleDeg = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
            angleDeg = std::fmod(angleDeg, 360.0);
            if (angleDeg < 0.0) angleDeg += 360.0;
            m_editAngle->setText(QString::number(angleDeg, 'f', 1));
            m_lblFxAngle->setVisible(false);
        }
        m_editAngle->setPlaceholderText(
            QString::fromUtf8("\u6570\u503c(\u00b0)\u6216\u516c\u5f0f"));  // 数值(°)或公式
        m_btnAngleMode->setVisible(true);  // 弧长/角度切换保留。
        return;
    }

    if (att) {
        // Follower: show the follower angle or arc length depending on mode.
        if (att->rotationMode == cad::param::RotationMode::ArcLength) {
            if (!att->arcLengthFormula.isEmpty()) {
                m_editAngle->setText(att->arcLengthFormula);
                m_lblFxAngle->setVisible(true);
            } else {
                // 显示 = 带符号折角弧长（v3 定稿，与旋转 HUD 一致）：
                // 折叠 0 / 开平 πr / 另一侧负值。编辑时按同一语义回写
                // （applyAngle 负责 折角 cm → α → 存储弧长）。
                const double radius = block ? block->segmentLengthAtPoint(att->fromPointId) : 0.0;
                const double alphaDeg = (radius > 1e-9)
                    ? (att->arcLength / radius) * 180.0 / M_PI : 0.0;
                const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
                m_editAngle->setText(QString::number(
                    foldDeg * M_PI / 180.0 * radius * 0.1, 'f', 2));
                m_lblFxAngle->setVisible(false);
            }
            m_editAngle->setPlaceholderText(
                QString::fromUtf8("\u6570\u503c(cm)\u6216\u516c\u5f0f"));  // 数值(cm)或公式
        } else {
            if (!att->followerAngleFormula.isEmpty()) {
                m_editAngle->setText(att->followerAngleFormula);
                m_lblFxAngle->setVisible(true);
            } else {
                // 显示 = 带符号折角 [−180°, +180°]（v3 定稿）：折叠 0 /
                // 垂直 ±90 / 开平 ±180，符号 = 折向。编辑时按同一语义回写
                // （applyAngle 负责 折角 → α → 存储）。
                m_editAngle->setText(cad::geo::Units::formatDegValue(
                    cad::geo::normalizeDeg180(att->followerAngle)));
                m_lblFxAngle->setVisible(false);
            }
            m_editAngle->setPlaceholderText(
                QString::fromUtf8("\u6570\u503c(\u00b0)\u6216\u516c\u5f0f"));  // 数值(°)或公式
        }
    } else if (block && seg) {
        // Free block: stored endpoint angle formula or numeric world angle.
        const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
        const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
        if (ep && !ep->angleFormula.isEmpty()) {
            m_editAngle->setText(ep->angleFormula);
            m_lblFxAngle->setVisible(true);
        } else if (sp && ep && sp->resolved && ep->resolved) {
            // 自由线绝对角度 = 世界角（0~360°，逆时针为正，2026-08 v3 定稿）。
            cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
            cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
            double angleDeg = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
            angleDeg = std::fmod(angleDeg, 360.0);
            if (angleDeg < 0.0) angleDeg += 360.0;
            m_editAngle->setText(QString::number(angleDeg, 'f', 1));
            m_lblFxAngle->setVisible(false);
        }
    }
}

void SegmentConnectionCard::setBridgeReadOnly(bool bridge)
{
    if (!bridge || !m_doc) return;
    const cad::param::Block* block = m_doc->findBlock(m_blockId);
    if (!block || !block->isBridge) return;
    const cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    // Show the measured world angle (read-only).
    const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
    const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
    if (sp && ep && sp->resolved && ep->resolved) {
        // 桥接线显示世界角（0~360°，逆时针为正，2026-08 v3 定稿）。
        const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
        const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
        double angleDeg = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
        angleDeg = std::fmod(angleDeg, 360.0);
        if (angleDeg < 0.0) angleDeg += 360.0;
        m_editAngle->setText(QString::number(angleDeg, 'f', 1));
    }

    m_editAngle->setEnabled(false);
    m_lblFxAngle->setVisible(false);
    m_lblWorldAngle->setVisible(false);
    m_lblFollowValue->setVisible(false);
    m_lblAngleCaption->setText(QString::fromUtf8("\u89d2\u5ea6(\u00b0):"));  // 角度(°):
    m_lblAngleCaption->setStyleSheet(QString());
    const QString tip = QString::fromUtf8(
        "\u6865\u63a5\u7ebf\uff1a\u957f\u5ea6\u4e0e\u89d2\u5ea6\u7531\u4e24\u7aef"
        "\u9489\u4f4f\u7684\u5bbf\u4e3b\u70b9\u51b3\u5b9a\uff0c\u4e0d\u53ef\u7f16\u8f91");  // 桥接线：长度与角度由两端钉住的宿主点决定，不可编辑
    m_editAngle->setToolTip(tip);
}

} // namespace cad::tools
