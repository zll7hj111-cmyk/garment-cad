#include "ConnectGesture.h"

#include <cmath>
#include <utility>

#include <QKeyEvent>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QPen>
#include <QColor>
#include <QUndoStack>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "parametric/FollowerAngle.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "ui/LayerFeedback.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/ComponentCommands.h"
#include "ui/Theme.h"

namespace cad::tools {

void ConnectGesture::commitConnectMove()
{
    // A failed/plain-move end of the gesture also clears any source selection
    // highlight (the connection did not complete).
    removeConfirmHighlight();
    removeSourcePortMarker();
    m_confirmCandidates.clear();
    m_selectedSourceCandidate.reset();
    m_componentSwitchCandidates.clear();

    auto* blk = m_paramDoc ? m_paramDoc->findBlock(m_connectFromBlock) : nullptr;
    if (!blk) {
        setState(m_selectionEmpty() ? SelectState::Idle : SelectState::Confirmed);
        return;
    }

    const Vec2 delta = blk->transform.origin - m_connectOrigOrigin;

    // 滑轨模式 (抽屉式滑动): 拖动期间已每帧回写自由轴坐标 — 记录拖后值,
    // 提交宏里用 SetSlideOffsetsCommand 把 old→new 与 MoveBlockCommand 一起
    // 入栈 (undo 整体回到拖前滑轨位置)。
    double curSlideAlong = 0.0, curSlidePerp = 0.0;
    bool slideAlive = false;
    if (!m_connectSlideAttId.isNull()) {
        if (const auto* a = m_paramDoc->attachmentsView().byId(m_connectSlideAttId)) {
            curSlideAlong = a->slideAlongMm;
            curSlidePerp = a->slidePerpMm;
            slideAlive = true;
        }
    }
    const bool offsetsChanged = slideAlive &&
        (std::abs(curSlideAlong - m_connectOldSlideAlong) > 1e-9 ||
         std::abs(curSlidePerp - m_connectOldSlidePerp) > 1e-9);

    // Restore the pre-drag state, then replay through the undo stack so the
    // whole gesture (move / slide) is one undo step.
    blk->transform.origin   = m_connectOrigOrigin;
    blk->transform.rotation = m_connectOrigRotation;

    if (m_undoStack && (offsetsChanged || delta.lengthSquared() > 1e-10)) {
        m_undoStack->beginMacro(m_connectSlideAttId.isNull()
            ? QStringLiteral("\xe7\xa7\xbb\xe5\x8a\xa8")  // 移动
            : QStringLiteral("\xe6\xbb\x91\xe5\x8a\xa8\xe5\xb9\xb6\xe7\xa7\xbb\xe5\x8a\xa8"));  // 滑动并移动
        if (offsetsChanged)
            m_undoStack->push(new cad::cmd::SetSlideOffsetsCommand(
                m_paramDoc, m_connectSlideAttId,
                m_connectOldSlideAlong, m_connectOldSlidePerp,
                curSlideAlong, curSlidePerp));
        if (delta.lengthSquared() > 1e-10) {
            QList<QUuid> moveBlocks{m_connectFromBlock};
            m_undoStack->push(new cad::cmd::MoveBlockCommand(
                m_paramDoc, moveBlocks, delta));
        }
        m_undoStack->endMacro();
    }

    m_scene->refreshAllBlockItems();
    setState(m_selectionEmpty() ? SelectState::Idle : SelectState::Confirmed);
}

// ── 连接角度会话 (二期: 输入面 = 上下文属性条) ──
// 旧浮动 AngleHud (viewport overlay) 整体退场: 条带显示跟随线段的名称/长度,
// 角度框可编辑, 击键经 ToolHost 回传 onAngleTextChanged 实时预览, Enter/Esc
// 回传 commitAngle/cancelAngle 收尾。tools 层不再持有任何 QWidget。

void ConnectGesture::beginAngleSession(const QUuid& attachmentId, double initialAngle)
{
    if (!m_beginAngleSession || !m_paramDoc) return;

    // 会话开始即复位输入状态 (旧 showAngleHud 同款): 上一会话残留的
    // 无效标记/弧长模式不得带入 —— commitAngle 靠 m_angleValid 放行,
    // onAngleTextChanged 靠 m_angleMode 分流角度/弧长, 残留会让新会话
    // Enter 失效或把数值写进错误的存储域。
    m_angleValid = true;
    m_angleMode = cad::param::RotationMode::Angle;

    // 跟随线段 = 源端点所在线段 (exitSegmentAtPoint: 该点为端点的第一段)。
    // 组件级连接同样成立: m_connectFromBlock = 被抓住的成员块。
    QUuid segId;
    if (const auto* blk = m_paramDoc->findBlock(m_connectFromBlock))
        segId = blk->exitSegmentAtPoint(m_connectFromPoint);
    if (segId.isNull()) {
        // 端点不属于任何线段 (理论不可达): 会话无显示目标, 直接结束。
        m_beginAngleSession(QUuid(), QUuid(), QUuid(), 0.0);
        return;
    }
    m_beginAngleSession(m_connectFromBlock, segId, attachmentId, initialAngle);
}

void ConnectGesture::endAngleSession()
{
    if (m_beginAngleSession)
        m_beginAngleSession(QUuid(), QUuid(), QUuid(), 0.0);
}

void ConnectGesture::onAngleTextChanged(const QString& text)
{
    if (!m_paramDoc) return;

    // Locate the attachment being tuned.
    cad::param::Attachment* att = m_paramDoc->findAttachment(m_editingAttachmentId);
    if (!att) return;

    const QString t = text.trimmed();
    if (t.isEmpty()) {
        // Empty input = keep the orientation-preserving initial angle.
        att->rotationMode = cad::param::RotationMode::Angle;
        att->followerAngle = m_initialAngle;
        att->followerAngleFormula.clear();
        m_angleMode = cad::param::RotationMode::Angle;
        m_angleValid = true;
    } else {
        const auto parsed = cad::geo::parseNumberOrFormula(t);
        if (parsed.isNumber) {
            const double numVal = parsed.value;
            if (m_angleMode == cad::param::RotationMode::ArcLength) {
                // 输入 = 带符号折角弧长（v3 定稿）→ 存储 α ∈ [0, 360°) 弧长。
                const cad::param::Block* blk = m_paramDoc->findBlock(att->fromBlockId);
                const double radius = blk ? blk->segmentLengthAtPoint(att->fromPointId) : 0.0;
                const double foldDeg = (radius > 1e-9)
                    ? cad::geo::arcMmToDeg(cad::geo::Units::cmToMm(numVal), radius) : 0.0;
                const double alphaDeg = cad::geo::normalizeDeg360(foldDeg);
                att->rotationMode = cad::param::RotationMode::ArcLength;
                att->arcLength = cad::geo::degToArcMm(alphaDeg, radius);
                att->arcLengthFormula.clear();
            } else {
                // 输入 = 带符号折角 → 存储 α（v3 定稿）。
                att->rotationMode = cad::param::RotationMode::Angle;
                att->followerAngle = cad::geo::normalizeDeg360(numVal);
                att->followerAngleFormula.clear();
            }
            m_angleValid = true;
        } else {
            auto r = cad::param::ConditionEngine::evaluate(
                parsed.formula, m_paramDoc->parameters(), {});
            if (r.ok) {
                if (m_angleMode == cad::param::RotationMode::ArcLength) {
                    att->rotationMode = cad::param::RotationMode::ArcLength;
                    att->arcLength = geo::Units::cmToMm(r.value);
                    att->arcLengthFormula = parsed.formula;
                } else {
                    att->rotationMode = cad::param::RotationMode::Angle;
                    att->followerAngle = r.value;
                    att->followerAngleFormula = parsed.formula;
                }
                m_angleValid = true;
            } else {
                m_angleValid = false;             // keep last valid geometry
            }
        }
    }

    if (m_angleValidity) m_angleValidity(m_angleValid);
    if (m_angleValid) {
        // Per-frame preview (角度 HUD 击键): resolve ONLY the connected
        // subgraph and sync cheaply — the old resolveAll() +
        // refreshAllBlockItems() re-resolved the whole document and rebuilt
        // every block item on EVERY keystroke.
        QList<QUuid> seeds;
        if (!att->fromComponentId.isNull()) {
            // 组件级连接: 整个组件是 follower — 借暴露端点成员作种子
            // (resolveForDrag 的 componentClosure 展开到全组件).
            if (const auto* comp = m_paramDoc->componentsView().byId(att->fromComponentId)) {
                const QUuid mb = m_paramDoc->componentsView().memberOwningPoint(*comp, att->fromPointId);
                if (!mb.isNull()) seeds.push_back(mb);
            }
        } else if (!att->fromBlockId.isNull()) {
            seeds.push_back(att->fromBlockId);
        }
        for (const QUuid& s : seeds)
            if (const auto* fb = m_paramDoc->findBlock(s))
                m_paramDoc->invalidateLayer(fb->layer);
        if (!seeds.isEmpty())
            m_paramDoc->resolveForDrag(seeds);
        if (m_scene) m_scene->syncBlockPositions();
    }
}

void ConnectGesture::onAngleModeChanged(cad::param::RotationMode mode)
{
    if (!m_paramDoc) return;
    cad::param::Attachment* att = m_paramDoc->findAttachment(m_editingAttachmentId);
    if (!att) return;

    // Geometry-preserving switch: compute current effective angle, then
    // convert to the new mode's value. 2026-12: 公式驱动不再拒绝切换 ——
    // 公式跨域换算保留变量链接 (半径烘焙为常数, FollowerAngle.h); 旧语义
    // "公式驱动拒绝 °/⌒"已由用户拍板废除。
    const cad::param::Block* blk = m_paramDoc->findBlock(att->fromBlockId);
    const double radius = blk ? blk->segmentLengthAtPoint(att->fromPointId) : 0.0;
    const auto res = cad::param::followerModeSwitchValues(
        *att, radius, mode, m_paramDoc->parameters(), {});

    if (mode == cad::param::RotationMode::ArcLength) {
        att->rotationMode = cad::param::RotationMode::ArcLength;
        att->arcLength = res.arcMm;
        att->arcLengthFormula = res.arcFormula;
    } else {
        att->rotationMode = cad::param::RotationMode::Angle;
        att->followerAngle = res.angle;
        att->followerAngleFormula = res.angleFormula;
    }
    m_angleMode = mode;
    m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();

    // 模式切换后条带角度框回显换算值由条带自身的 resolved 处理 (refreshFields
    // 焦点保护: 用户点击 °/⌒ 时角度框已失焦, 回填生效)。旧 HUD 的直写文本
    // 已随 AngleHud 退场。
}

void ConnectGesture::commitAngle()
{
    if (!m_angleValid) return;   // ignore Enter on an invalid formula
    finalizeConnection();
}

void ConnectGesture::cancelAngle()
{
    // Keep the connection but revert to the orientation-preserving angle that
    // was computed at attach time (the block keeps its dragged orientation).
    if (m_paramDoc) {
        if (auto* a = m_paramDoc->findAttachment(m_editingAttachmentId)) {
            a->rotationMode = cad::param::RotationMode::Angle;
            a->followerAngle = m_initialAngle;
            a->followerAngleFormula.clear();
            a->arcLength = 0.0;
            a->arcLengthFormula.clear();
        }
        m_paramDoc->resolveAll();
    }
    finalizeConnection();
}

void ConnectGesture::finalizeConnection()
{
    endAngleSession();
    // Connection committed: remove the persistent source-selection highlight.
    removeConfirmHighlight();
    removeSourcePortMarker();
    m_confirmCandidates.clear();
    m_selectedSourceCandidate.reset();
    m_componentSwitchCandidates.clear();

    // Snapshot the tuned attachment, then restore the COMPLETE pre-drag state
    // (transform + old attachment) and replay the whole gesture through the
    // undo stack: "quick-detach + connect + angle" becomes one undo step.
    // 仅角度重挂 (m_reattachActive): 原附件换代 — 新态 (含 HUD 角度调整) 与
    // 旧仅角度态都固化进 ReconnectAttachmentCommand, 单次 undo 即回到拖前
    // 仅角度态 (位置/角度原样), 与普通连接的宏撤销体验一致。
    if (m_paramDoc && m_undoStack) {
        cad::param::Attachment snapshot;
        bool found = false;
        for (const auto& a : m_paramDoc->attachments()) {
            if (a.id == m_editingAttachmentId) { snapshot = a; found = true; break; }
        }
        if (found) {
            if (m_reattachActive) {
                m_undoStack->push(new cad::cmd::ReconnectAttachmentCommand(
                    m_paramDoc, m_editingAttachmentId, snapshot,
                    m_reattachOldAtt, m_connectOrigOrigin, m_connectOrigRotation));
            } else {
                m_paramDoc->removeAttachment(m_editingAttachmentId);
                if (!snapshot.fromComponentId.isNull()) {
                    // 组件级连接: 恢复所有成员拖前 origin (宏 redo 再整体落位).
                    for (auto it = m_connectOrigOrigins.cbegin(); it != m_connectOrigOrigins.cend(); ++it) {
                        if (auto* mb = m_paramDoc->findBlock(it.key()))
                            mb->transform.origin = it.value();
                    }
                } else if (auto* blk = m_paramDoc->findBlock(snapshot.fromBlockId)) {
                    blk->transform.origin   = m_connectOrigOrigin;
                    blk->transform.rotation = m_connectOrigRotation;
                }

                m_undoStack->beginMacro(QStringLiteral(
                    "\xe5\xbb\xba\xe7\xab\x8b\xe8\xbf\x9e\xe6\x8e\xa5"));  // 建立连接
                // 暴露端点 (自动暴露) 由引擎在 addAttachment/addAttachmentRaw/
                // removeAttachment 统一维护 — 连接宏只需 AddAttachmentCommand.
                m_undoStack->push(new cad::cmd::AddAttachmentCommand(m_paramDoc, snapshot));
                m_undoStack->endMacro();
            }
        }
    }

    m_editingAttachmentId = QUuid();
    m_connectFromBlock = QUuid();
    m_connectFromPoint = QUuid();
    m_connectTarget.reset();
    if (m_scene) m_scene->refreshAllBlockItems();
    m_clearSelectionAndIdle();
    // The owner tool is now Idle — sync the gesture's own state too, or
    // active() would stay true (AngleInput) and swallow every later press
    // in ToolSelect::mousePress's connect branch (regression: 连接完成后
    // 线段/画布无法再选中或移动).
    setState(SelectState::Idle);
}
} // namespace cad::tools
