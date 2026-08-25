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


// ── 角度/弧长编辑：应用、脏标记、模式切换、绝对角度 hint (2026-08 拆分) ──

void SegmentConnectionCard::applyAngle()
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    QString text = m_editAngle->text().trimmed();
    if (text.isEmpty()) return;

    // Evaluate: number or formula
    bool isNumber = false;
    double targetDeg = text.toDouble(&isNumber);
    if (!isNumber) {
        auto r = cad::param::ConditionEngine::evaluate(
            text, m_doc->parameters(), {});
        if (!r.ok) return;
        targetDeg = r.value;
    }

    // Check if this block is a follower
    bool isFollower = false;
    bool isIndependentAngle = false;
    QUuid attId;
    for (const auto& att : m_doc->attachments()) {
        if (att.fromBlockId == m_blockId) {
            isFollower = true;
            isIndependentAngle = att.angleIndependent;
            attId = att.id;
            break;
        }
    }

    if (isFollower && !isIndependentAngle) {
        // The angle field edits the FOLLOWER ANGLE or ARC LENGTH directly.
        // 输入 = 显示域（带符号折角，v3 定稿）→ 存储域 α ∈ [0, 360°)；
        // 公式输入原样存储（公式域 = 存储域，全角域不受限）。
        if (auto* att = m_doc->findAttachment(attId)) {
            if (att->rotationMode == cad::param::RotationMode::ArcLength) {
                const double radius = block->segmentLengthAtPoint(att->fromPointId);
                const double foldDeg = (radius > 1e-9)
                    ? targetDeg / (M_PI / 180.0 * radius * 0.1) : 0.0;
                const double alphaDeg = cad::geo::normalizeDeg360(foldDeg);
                att->arcLength = alphaDeg * M_PI / 180.0 * radius;
                att->arcLengthFormula = isNumber ? QString() : text;
            } else {
                att->followerAngle = cad::geo::normalizeDeg360(targetDeg);
                att->followerAngleFormula = isNumber ? QString() : text;
            }
        }
    } else {
        // Free block: set endpoint's Polar angle directly.
        cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
        if (!ep) return;

        if (ep->constraint != cad::param::PointConstraint::Polar) {
            const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
            if (!sp || !sp->resolved || !ep->resolved) return;
            double dist = sp->resolvedPos.distanceTo(ep->resolvedPos);
            ep->constraint = cad::param::PointConstraint::Polar;
            ep->refPointId = seg->startPointId;
            ep->distance = dist;
        }

        // 自由线显示 = 绝对角度（0~360°，逆时针为正，2026-08 v3 定稿）；
        // Polar 存储角 = 世界角 − 块旋转，故 localDeg = 显示角 − rotDeg。
        const double rotDeg = block->transform.rotation * 180.0 / M_PI;
        const double localDeg = targetDeg - rotDeg;
        ep->angle = localDeg;
        ep->angleFormula.clear();

        if (!isNumber) {
            ep->angleFormula = (std::abs(rotDeg) > 1e-9)
                ? QStringLiteral("(%1)-%2").arg(text).arg(rotDeg, 0, 'g', 12)
                : text;
        }
    }

    // Clear dirty indicator (do NOT overwrite the user's input text)
    m_editAngle->setStyleSheet(QString());
    m_lblFxAngle->setVisible(!isNumber && !text.isEmpty());
    // 绝对角度/跟随值等读数依赖刚写入的角度值 —— 必须就地刷新, 否则
    // "= 绝对角度 xx°" 停留在旧值 (用户报告 2026-08: 不是实时刷新的)。
    // refreshCard() 不动 m_editAngle (保留用户输入), 只刷标签/行状态.
    refreshCard();
    emit changed(ChangeKind::AngleApplied);
}

void SegmentConnectionCard::onAngleDirty()
{
    m_editAngle->setStyleSheet(QString());

    QString text = m_editAngle->text().trimmed();
    bool isNumber = false;
    text.toDouble(&isNumber);
    m_lblFxAngle->setVisible(!isNumber && !text.isEmpty());

    emit angleEdited();
}

void SegmentConnectionCard::onModeToggle()
{
    const cad::param::Attachment* att = findFollowerAttachment();
    if (!att || !m_doc) return;

    // 用户可能在编辑后直接点模式切换按钮（未回车）：先把输入落盘，避免
    // 切换时输入丢失。输入是公式（非纯数值）时拒绝切换——公式必须原样
    // 保留，不参与换算（用户要求）；无效公式同样拒绝，避免输入被刷新。
    const QString text = m_editAngle->text().trimmed();
    if (!text.isEmpty()) {
        bool isNumber = false;
        text.toDouble(&isNumber);
        if (!isNumber) return;
        applyAngle();
    }

    auto* mutAtt = m_doc->findAttachment(att->id);
    if (!mutAtt) return;

    // 模型已存公式（角度/弧长表达式）：同样拒绝切换，绝不换算烘焙公式。
    const bool hasFormula =
        (mutAtt->rotationMode == cad::param::RotationMode::ArcLength)
            ? !mutAtt->arcLengthFormula.isEmpty()
            : !mutAtt->followerAngleFormula.isEmpty();
    if (hasFormula) return;

    // Geometry-preserving switch.
    cad::param::Block* blk = m_doc->findBlock(m_blockId);
    double radius = blk ? blk->segmentLengthAtPoint(mutAtt->fromPointId) : 0.0;
    double curDeg = mutAtt->followerAngle;
    if (mutAtt->rotationMode == cad::param::RotationMode::ArcLength) {
        double arcMm = mutAtt->arcLength;
        if (!mutAtt->arcLengthFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                mutAtt->arcLengthFormula, m_doc->parameters(), {});
            if (r.ok) arcMm = cad::geo::Units::cmToMm(r.value);
        }
        // 弧长 = 线夹角恒等映射（2026-08 定稿）：弧长 0 = 0° 折叠、
        // πr = 180° 开平，与 Resolver 一致，不再反转。归一化 [0, 360°)。
        curDeg = (radius > 1e-9) ? (arcMm / radius) * 180.0 / M_PI : 0.0;
        curDeg = std::fmod(curDeg, 360.0);
        if (curDeg < 0.0) curDeg += 360.0;
    } else if (!mutAtt->followerAngleFormula.isEmpty()) {
        auto r = cad::param::ConditionEngine::evaluate(
            mutAtt->followerAngleFormula, m_doc->parameters(), {});
        if (r.ok) curDeg = r.value;
    }

    if (mutAtt->rotationMode == cad::param::RotationMode::Angle) {
        mutAtt->rotationMode = cad::param::RotationMode::ArcLength;
        // 弧长 = 线夹角恒等映射（2026-08 定稿）：弧长角 = 显示角，不再反转。
        mutAtt->arcLength = std::fmod(curDeg, 360.0) * M_PI / 180.0 * radius;
        mutAtt->arcLengthFormula.clear();
    } else {
        mutAtt->rotationMode = cad::param::RotationMode::Angle;
        mutAtt->followerAngle = curDeg;
        mutAtt->followerAngleFormula.clear();
    }
    m_doc->resolveAll();

    // 完整刷新（caption/按钮图标/世界角提示/跟随值一并更新，不只输入框）：
    // 不完整刷新曾导致切到弧长后界面文字毫无变化（用户报告）。
    refreshCard();
    populateAngleField();
    emit changed(ChangeKind::ModeSwitched);
}

void SegmentConnectionCard::onModeChanged(int index)
{
    if (!m_doc) return;
    const cad::param::Attachment* att = findFollowerAttachment();

    switch (index) {
    case 0: // 跟随: 缓存恢复 —— 独立线段期间快照的完整连接配置原样回来.
        if (!att && m_modeCache && m_modeCache->fromBlockId == m_blockId) {
            // 验证缓存宿主仍有效, 否则丢弃缓存 (防每次切换反复尝试).
            const auto* leader = m_doc->findBlock(m_modeCache->toBlockId);
            const auto* pt = leader ? leader->findPoint(m_modeCache->toPointId)
                                    : nullptr;
            if (!leader || !pt) {
                m_modeCache.reset();
                break;
            }
            cad::param::Attachment candidate = *m_modeCache;
            // 线段在宿主点可能已变化 → 按当前宿主点重新解析所在线段.
            const QUuid segId = leader->exitSegmentAtPoint(m_modeCache->toPointId);
            if (segId.isNull()) break;
            candidate.toSegmentId = segId;
            // 森林/值环校验 (addAttachmentRaw 不校验, 恢复前预检).
            std::vector<cad::param::Attachment> others;
            for (const auto& a : m_doc->attachments())
                others.push_back(a);
            if (cad::param::checkAttachment(others, candidate)
                    != cad::param::AttachmentIssue::Ok) {
                m_modeCache.reset();
                break;
            }
            if (auto* stack = m_doc->undoStack())
                stack->push(new cad::cmd::AddAttachmentCommand(m_doc, candidate));
            else
                m_doc->addAttachment(candidate);
            // 同步缓存为恢复后的最新连接.
            if (const auto* now = findFollowerAttachment())
                m_modeCache = *now;
            refresh();
            emit changed(ChangeKind::Connected);
            return;
        }
        break;
    case 1: // 独立线段: 快照连接配置后拆除 (切回跟随可恢复).
        if (att) detachWithCache();
        break;
    default:
        break;
    }

    refresh();
    emit changed(ChangeKind::ConnectionModeChanged);
}

void SegmentConnectionCard::updateWorldAngleLabel(const cad::param::Attachment& att)
{
    if (!m_doc) { m_lblWorldAngle->setVisible(false); return; }
    const cad::param::Block* leader = m_doc->findBlock(att.toBlockId);
    if (!leader) { m_lblWorldAngle->setVisible(false); return; }

    const double refWorldDeg = (leader->transform.rotation
        + leader->exitDirectionAtPoint(att.toPointId, att.toSegmentId))
        * 180.0 / M_PI;

    double constDeg;
    if (att.rotationMode == cad::param::RotationMode::ArcLength) {
        double arcMm = att.arcLength;
        if (!att.arcLengthFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                att.arcLengthFormula, m_doc->parameters(), {});
            if (r.ok) arcMm = cad::geo::Units::cmToMm(r.value);
        }
        const cad::param::Block* block = m_doc->findBlock(m_blockId);
        const double radius = block ? block->segmentLengthAtPoint(att.fromPointId) : 0.0;
        constDeg = (radius > 1e-9) ? (arcMm / radius) * 180.0 / M_PI : 0.0;
        constDeg = std::fmod(constDeg, 360.0);
        if (constDeg < 0.0) constDeg += 360.0;
    } else {
        constDeg = att.followerAngle;
        if (!att.followerAngleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                att.followerAngleFormula, m_doc->parameters(), {});
            if (r.ok) constDeg = r.value;
        }
    }

    const double absDeg = cad::geo::normalizeDeg360(refWorldDeg + 180.0 - constDeg);
    const QString text = QString::fromUtf8("= 绝对角度 %1°")
                             .arg(cad::geo::Units::formatDegValue(absDeg));
    if (m_lblWorldAngle->text() != text)
        m_lblWorldAngle->setText(text);
    m_lblWorldAngle->setVisible(true);
}

void SegmentConnectionCard::onDocResolved()
{
    // Live path: only the geometry-dependent readouts, never the editor input
    // (populateAngleField would clobber in-progress typing).
    const cad::param::Attachment* att = findFollowerAttachment();
    if (att) {
        updateWorldAngleLabel(*att);
        return;
    }
    // 省道线 (用户拍板 2026-08): 每帧 resolved 也刷新反算角度, 让基准线段
    // 被拖动/旋转时读数实时跟. 只动标签文本 (同值短路).
    if (m_doc) {
        if (const auto* block = m_doc->findBlock(m_blockId);
            block && block->isDart()) {
            const QString text = dartFoldAngleText(*block);
            if (!text.isEmpty() && m_dartFoldLabel->text() != text)
                m_dartFoldLabel->setText(text);
        }
    }
}

} // namespace cad::tools
