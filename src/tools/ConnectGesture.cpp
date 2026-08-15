#include "ConnectGesture.h"

#include <cmath>

#include <QKeyEvent>
#include "ElaLineEdit.h"
#include <QGraphicsView>
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
#include "tools/AngleHud.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockCommands.h"

namespace cad::tools {

namespace {

/// Connection snap reach (user units, same for source grab and target drop).
constexpr double kConnectSnapRadius = 7.5;
/// Two candidate targets closer than this are "the same spot" (overlap).
constexpr double kOverlapEps = 0.5;

/// Format an angle in degrees for display: integers render without a trailing
/// ".0" (e.g. 22 -> "22", 22.5 -> "22.5").
QString formatDeg(double deg)
{
    QString s = QString::number(deg, 'f', 1);
    if (s.endsWith(QLatin1String(".0")))
        s.chop(2);
    return s;
}

// ── 角度显示约定（2026-08 v3 定稿，用户拍板）────────────────────────────
// 与 ToolRotate.cpp 同步：存储域 α ∈ [0, 360°)；跟随角度显示 = 带符号折角
// [−180°, +180°]（折叠 0 / 垂直 ±90 / 开平 ±180，符号 = 折向）。
double signedFoldDeg(double alphaDeg)
{
    double a = std::fmod(alphaDeg, 360.0);
    if (a < 0.0) a += 360.0;
    return a > 180.0 ? a - 360.0 : a;
}
double alphaFromSignedFold(double foldDeg)
{
    double a = std::fmod(foldDeg, 360.0);
    if (a < 0.0) a += 360.0;
    return a;
}

/// Toast text when a freshly established attachment crosses layers:
/// "已建立跨层连接（测量层→操作层1）" (real layer names). Empty when
/// same-layer or blocks are gone.
QString crossLayerToast(cad::param::ParamDocument* doc,
                        const cad::param::Block& from,
                        const cad::param::Block& to)
{
    if (!doc) return QString();
    if (doc->isAuxBlock(from) == doc->isAuxBlock(to)) return QString();
    auto name = [doc](const QUuid& layerId) {
        const auto* l = doc->layerById(layerId);
        return l ? l->name : QStringLiteral("?");
    };
    return QString::fromUtf8("\xe5\xb7\xb2\xe5\xbb\xba\xe7\xab\x8b"
                             "\xe8\xb7\xa8\xe5\xb1\x82\xe8\xbf\x9e\xe6\x8e\xa5"
                             "\xef\xbc\x88%1\u2192%2\xef\xbc\x89")  // 已建立跨层连接（%1→%2）
        .arg(name(from.layer), name(to.layer));
}

} // namespace

ConnectGesture::ConnectGesture(CanvasScene* scene, cad::param::ParamDocument* doc,
                               QUndoStack* undoStack,
                               const std::function<void(SelectState)>& setState,
                               const std::function<void(const QString&)>& showToast,
                               const std::function<void()>& clearSelectionAndIdle,
                               const std::function<bool()>& selectionEmpty)
    : m_scene(scene)
    , m_paramDoc(doc)
    , m_undoStack(undoStack)
    , m_setState(setState)
    , m_showToast(showToast)
    , m_clearSelectionAndIdle(clearSelectionAndIdle)
    , m_selectionEmpty(selectionEmpty)
{
}

ConnectGesture::~ConnectGesture()
{
    // The HUD is parented to the viewport (which outlives this gesture); the
    // QPointer turns null if the viewport was already torn down, so deleting
    // it here is always safe. Without this the HUD would accumulate on the
    // viewport across tool activations (ToolSelect rebuilds the gesture).
    delete m_angleHud;
}

void ConnectGesture::beginConnect(const QUuid& fromBlockId, const QUuid& fromPointId,
                                  const Vec2& pos)
{
    (void)pos;  // grab offset derives from the point's world position
    auto* blk = m_paramDoc->findBlock(fromBlockId);
    if (!blk || blk->isBridge) return;   // bridges are pinned at both ends

    m_connectFromBlock = fromBlockId;
    m_connectFromPoint = fromPointId;
    m_connectTarget.reset();
    m_connectOldAtt.reset();

    // Grab geometry: the block translates so its from-point tracks the cursor.
    m_connectOrigOrigin   = blk->transform.origin;
    m_connectOrigRotation = blk->transform.rotation;
    m_connectGrabOffset   = blk->transform.origin - blk->worldPos(fromPointId);

    // 快拆 (quick-detach): if this block currently follows a leader, release it
    // immediately so it can move freely. 组内连接同样可拆 (组零限制). The
    // removal is re-wrapped into the final undo macro at commit time
    // (restore-then-replay pattern).
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId == fromBlockId && !att.isPin && !att.isLocked) {
            m_connectOldAtt = att;
            m_paramDoc->removeAttachment(att.id);
            break;
        }
    }

    setState(SelectState::Connecting);
    updateConnectHalo();
}

void ConnectGesture::move(const Vec2& pos)
{
    if (m_state == SelectState::ConfirmTarget) {
        updateConfirmHighlight(pos);
        return;
    }
    if (m_state != SelectState::Connecting) return;
    if (!m_paramDoc) return;
    auto* blk = m_paramDoc->findBlock(m_connectFromBlock);
    if (!blk) return;

    double zoom = 1.0;
    if (m_scene && !m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    // Generous radius while connecting: dropping onto a target must feel easy.
    // Exclude the dragged block's OWN points — otherwise a nearby point of
    // the dragged line (e.g. its other endpoint) would shadow the real
    // target: findSnap returns the NEAREST point, and the old code reset the
    // snap AFTER the search, silently dropping the actual target.
    auto snap = m_snapEngine.findSnap(pos, m_paramDoc, zoom, kConnectSnapRadius,
                                      {}, &m_connectFromBlock);
    if (snap.has_value() && snap->blockId == m_connectFromBlock)
        snap.reset();                       // never snap to self

    // Only promise a connection (magnet + ring) when releasing would actually
    // attach — e.g. a descendant's point would close a cycle and is refused.
    bool willConnect = false;
    if (snap.has_value()) {
        cad::param::Attachment cand;
        cand.fromBlockId = m_connectFromBlock;
        cand.fromPointId = m_connectFromPoint;
        cand.toBlockId   = snap->blockId;
        cand.toPointId   = snap->pointId;
        willConnect = cad::param::checkAttachment(m_paramDoc->attachments(), cand)
                      == cad::param::AttachmentIssue::Ok;
    }
    m_connectTarget = willConnect ? snap : std::nullopt;

    // The block physically follows the cursor: from-point lands on the cursor
    // (or exactly on the snap target). Children cascade via resolveForDrag —
    // only the dragged block's subgraph moves (the old attachment, if any, is
    // already out of the document during the gesture).
    const Vec2 anchor = m_connectTarget.has_value() ? m_connectTarget->worldPos : pos;
    blk->transform.origin = anchor + m_connectGrabOffset;
    m_paramDoc->invalidateLayer(blk->layer);  // per-frame: freeze the other group
    m_paramDoc->resolveForDrag({m_connectFromBlock});
    // resolveForDrag() emits resolved → scene syncs positions automatically;
    // no explicit full refresh needed here (was the main per-frame cost).
    updateConnectMarker();
    updateConnectHalo();
}

void ConnectGesture::release(const Vec2& pos)
{
    (void)pos;
    removeConnectMarker();
    removeConnectHalo();
    bool connected = false;

    if (m_connectTarget.has_value() && m_paramDoc) {
        // Overlapping-target disambiguation: gather every candidate within the
        // snap radius that would actually attach, then check whether several
        // of them sit on the SAME spot (e.g. endpoints of different blocks
        // stacked at one position). Ambiguous → ConfirmTarget: the user clicks
        // the intended leader segment (its endpoint on the connection spot is
        // the anchor, its id becomes toSegmentId).
        double zoom = 1.0;
        if (m_scene && !m_scene->views().isEmpty())
            zoom = m_scene->views().first()->transform().m11();
        const auto allCands = m_snapEngine.findSnapCandidates(
            pos, m_paramDoc, zoom, kConnectSnapRadius, {}, &m_connectFromBlock);
        std::vector<SnapResult> pool;
        for (const auto& c : allCands) {
            if (c.blockId == m_connectFromBlock) continue;
            cad::param::Attachment cand;
            cand.fromBlockId = m_connectFromBlock;
            cand.fromPointId = m_connectFromPoint;
            cand.toBlockId   = c.blockId;
            cand.toPointId   = c.pointId;
            if (cad::param::checkAttachment(m_paramDoc->attachments(), cand)
                    == cad::param::AttachmentIssue::Ok)
                pool.push_back(c);
        }

        if (!pool.empty()) {
            const Vec2 refPos = pool.front().worldPos;
            // Overlap set = candidates at the same spot as the nearest one.
            std::vector<SnapResult> overlap;
            for (const auto& c : pool)
                if (c.worldPos.distanceTo(refPos) < kOverlapEps)
                    overlap.push_back(c);

            if (overlap.size() > 1) {
                // Multiple points stacked here — ask the user to confirm the
                // leader by clicking one of the candidate segments.
                m_confirmCandidates = collectConfirmCandidates(refPos);
                if (!m_confirmCandidates.empty()) {
                    setState(SelectState::ConfirmTarget);
                    if (m_scene)
                        m_scene->showToast(QString::fromUtf8(
                            "连接位置存在多个重叠点：点选基准线段确认连接"));  // 连接位置存在多个重叠点：点选基准线段确认连接
                    return;
                }
            }
            // Single unambiguous target: connect directly.
            const SnapResult& target = overlap.empty() ? pool.front() : overlap.front();
            connected = attachToTarget(target.blockId, target.pointId, QUuid());
        }
    }

    if (!connected)
        commitConnectMove();   // released away from any target: plain move

    if (m_state != SelectState::AngleInput) {
        m_connectFromBlock = QUuid();
        m_connectFromPoint = QUuid();
        m_connectTarget.reset();
        m_connectOldAtt.reset();
    }
}

void ConnectGesture::pressConfirmTarget(const Vec2& pos)
{
    if (!m_paramDoc || !m_scene) { cancel(); return; }
    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    const auto segSnap = m_snapEngine.findSegmentSnap(
        pos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        for (const auto& cand : m_confirmCandidates) {
            if (cand.blockId == segSnap->blockId && cand.segId == segSnap->segmentId) {
                removeConfirmHighlight();
                m_confirmCandidates.clear();
                if (attachToTarget(cand.blockId, cand.pointId, cand.segId))
                    return;   // → AngleInput
                break;        // rejected (cycle etc.) → cancel below
            }
        }
    }
    // Blank or non-candidate click: abort the whole gesture.
    removeConfirmHighlight();
    m_confirmCandidates.clear();
    cancel();
}

void ConnectGesture::cancel()
{
    // Abort the in-progress connect drag: restore the exact pre-drag state
    // (transform + old attachment) as if the gesture never started.
    if (m_paramDoc) {
        if (auto* blk = m_paramDoc->findBlock(m_connectFromBlock)) {
            blk->transform.origin   = m_connectOrigOrigin;
            blk->transform.rotation = m_connectOrigRotation;
        }
        if (m_connectOldAtt)
            m_paramDoc->addAttachmentRaw(*m_connectOldAtt);  // verbatim (keep snapshot isLocked)
        m_paramDoc->resolveAll();
    }
    if (m_scene) m_scene->refreshAllBlockItems();

    removeConnectMarker();
    removeConnectHalo();
    removeConfirmHighlight();
    m_confirmCandidates.clear();
    m_connectFromBlock = QUuid();
    m_connectFromPoint = QUuid();
    m_connectTarget.reset();
    m_connectOldAtt.reset();
    if (m_angleHud) hideAngleHud();
    setState(SelectState::Confirmed);
}

bool ConnectGesture::keyPress(QKeyEvent* event)
{
    if (m_state == SelectState::AngleInput) {
        // Fallback path: the key reached the view instead of the HUD widget.
        if (event->key() == Qt::Key_Escape) {
            cancelAngle();
            return true;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            commitAngle();
            return true;
        }
        return false;  // HUD owns all other keys
    }
    if (event->key() == Qt::Key_Escape) {
        cancel();
        return true;
    }
    return false;
}

bool ConnectGesture::attachToTarget(const QUuid& toBlockId, const QUuid& toPointId,
                                    const QUuid& toSegmentId)
{
    if (!m_paramDoc) return false;
    auto* fromBlk = m_paramDoc->findBlock(m_connectFromBlock);
    auto* toBlk   = m_paramDoc->findBlock(toBlockId);
    if (!fromBlk || !toBlk) return false;

    cad::param::Attachment att;
    att.fromBlockId = m_connectFromBlock;
    att.fromPointId = m_connectFromPoint;
    att.toBlockId   = toBlockId;
    att.toPointId   = toPointId;
    att.toSegmentId = !toSegmentId.isNull()
        ? toSegmentId : toBlk->exitSegmentAtPoint(toPointId);

    // Orientation-preserving follower angle: the Resolver drives
    //   rotation = refWorld + angle·π/180 − localDir
    // so choosing angle = (rotation + localDir − refWorld)·180/π keeps
    // the block's CURRENT world direction — zero visual jump on attach.
    const double refWorld = toBlk->transform.rotation
        + toBlk->exitDirectionAtPoint(toPointId, att.toSegmentId);
    const double localDir = fromBlk->directionAtPoint(m_connectFromPoint);
    const double angleDeg = cad::param::backSolveFollowerAngle(
        fromBlk->transform.rotation, localDir, refWorld);
    att.followerAngle = angleDeg;

    if (cad::param::checkAttachment(m_paramDoc->attachments(), att)
            != cad::param::AttachmentIssue::Ok)
        return false;

    // NOTE: 组对连接零限制 —— 无主连接预算, 组员自由建立连接.
    if (!m_paramDoc->addAttachment(att)) return false;
    // Added directly for live angle preview; finalizeConnection()
    // re-wraps everything into a single undoable macro.
    m_editingAttachmentId = att.id;
    m_initialAngle = angleDeg;
    m_paramDoc->resolveAll();
    if (m_scene) {
        m_scene->refreshAllBlockItems();
        // Cross-layer feedback: toast at the gesture's success point (closest
        // to the user action; never fires on undo/redo replay).
        if (const QString toast = crossLayerToast(m_paramDoc, *fromBlk, *toBlk);
            !toast.isEmpty())
            m_showToast(toast);
        const Vec2 anchor = m_connectTarget.has_value()
            ? m_connectTarget->worldPos : toBlk->worldPos(toPointId);
        showAngleHud(anchor);
    }
    setState(SelectState::AngleInput);
    return true;
}

std::vector<ConfirmCandidate> ConnectGesture::collectConfirmCandidates(
    const Vec2& connWorldPos) const
{
    std::vector<ConfirmCandidate> out;
    if (!m_paramDoc) return out;
    for (const auto& block : m_paramDoc->blocks()) {
        if (block.id == m_connectFromBlock) continue;
        for (const auto& seg : block.segments) {
            const auto* sp = block.findPoint(seg.startPointId);
            const auto* ep = block.findPoint(seg.endPointId);
            const Vec2 local = block.transform.toLocal(connWorldPos);
            if (sp && sp->resolved
                && sp->resolvedPos.distanceTo(local) < kOverlapEps)
                out.push_back({block.id, seg.id, sp->id});
            if (ep && ep->resolved
                && ep->resolvedPos.distanceTo(local) < kOverlapEps)
                out.push_back({block.id, seg.id, ep->id});
        }
    }
    return out;
}

void ConnectGesture::updateConfirmHighlight(const Vec2& pos)
{
    if (!m_paramDoc || !m_scene) return;
    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    QUuid hitBlock, hitSeg;
    const auto segSnap = m_snapEngine.findSegmentSnap(
        pos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        for (const auto& cand : m_confirmCandidates) {
            if (cand.blockId == segSnap->blockId && cand.segId == segSnap->segmentId) {
                hitBlock = cand.blockId;
                hitSeg = cand.segId;
                break;
            }
        }
    }

    if (hitSeg.isNull()) {
        if (m_confirmHighlight) m_confirmHighlight->setVisible(false);
        return;
    }
    const auto* blk = m_paramDoc->findBlock(hitBlock);
    const auto* seg = blk ? blk->findSegment(hitSeg) : nullptr;
    const auto* sp = seg ? blk->findPoint(seg->startPointId) : nullptr;
    const auto* ep = seg ? blk->findPoint(seg->endPointId) : nullptr;
    if (!sp || !ep || !sp->resolved || !ep->resolved) return;

    if (!m_confirmHighlight) {
        m_confirmHighlight = new QGraphicsPathItem();
        QPen pen(QColor(0xF39C12), 3.0);
        pen.setCosmetic(true);
        m_confirmHighlight->setPen(pen);
        m_confirmHighlight->setBrush(Qt::NoBrush);
        m_confirmHighlight->setZValue(101.0);
        m_scene->addItem(m_confirmHighlight);
    }
    QPainterPath path;
    path.moveTo(cad::geo::Coord::toScene(blk->worldPos(sp->id)));
    path.lineTo(cad::geo::Coord::toScene(blk->worldPos(ep->id)));
    m_confirmHighlight->setPath(path);
    m_confirmHighlight->setVisible(true);
}

void ConnectGesture::removeConfirmHighlight()
{
    if (m_confirmHighlight) {
        m_confirmHighlight->setVisible(false);
        delete m_confirmHighlight;
        m_confirmHighlight = nullptr;
    }
}

void ConnectGesture::commitConnectMove()
{
    auto* blk = m_paramDoc ? m_paramDoc->findBlock(m_connectFromBlock) : nullptr;
    if (!blk) {
        setState(m_selectionEmpty() ? SelectState::Idle : SelectState::Confirmed);
        return;
    }

    const Vec2 delta = blk->transform.origin - m_connectOrigOrigin;

    // Restore the pre-drag state, then replay through the undo stack so the
    // whole gesture (quick-detach + move) is one undo step.
    blk->transform.origin   = m_connectOrigOrigin;
    blk->transform.rotation = m_connectOrigRotation;
    if (m_connectOldAtt)
        m_paramDoc->addAttachmentRaw(*m_connectOldAtt);  // verbatim (keep snapshot isLocked)

    if (m_undoStack && (m_connectOldAtt || delta.lengthSquared() > 1e-10)) {
        m_undoStack->beginMacro(QStringLiteral(
            "\xe6\x8b\x86\xe5\xbc\x80\xe5\xb9\xb6\xe7\xa7\xbb\xe5\x8a\xa8"));  // 拆开并移动
        if (m_connectOldAtt)
            m_undoStack->push(new cad::cmd::RemoveAttachmentCommand(
                m_paramDoc, m_connectOldAtt->id));
        if (delta.lengthSquared() > 1e-10)
            m_undoStack->push(new cad::cmd::MoveBlockCommand(
                m_paramDoc, {m_connectFromBlock}, delta));
        m_undoStack->endMacro();
    }

    m_scene->refreshAllBlockItems();
    setState(m_selectionEmpty() ? SelectState::Idle : SelectState::Confirmed);
}

// ── Angle HUD ──

void ConnectGesture::showAngleHud(const Vec2& anchorUser)
{
    if (!m_scene || m_scene->views().isEmpty()) return;
    QGraphicsView* view = m_scene->views().first();
    QWidget* viewport = view->viewport();

    if (!m_angleHud) {
        m_angleHud = new AngleHud(viewport);
        m_angleHud->onTextChanged = [this](const QString& t) { onAngleTextChanged(t); };
        m_angleHud->onCommit      = [this] { commitAngle(); };
        m_angleHud->onCancel      = [this] { cancelAngle(); };
        m_angleHud->onModeChanged = [this](cad::param::RotationMode m) { onAngleModeChanged(m); };
    } else {
        m_angleHud->setParent(viewport);
    }

    // Start in angle mode (fresh connection always begins as angle).
    m_angleMode = cad::param::RotationMode::Angle;
    m_angleHud->setMode(m_angleMode);

    // Position near the connection point (user → scene → viewport pixels).
    const QPointF scenePt = cad::geo::Coord::toScene(anchorUser.x, anchorUser.y);
    const QPoint vpPt = view->mapFromScene(scenePt);
    m_angleHud->move(vpPt + QPoint(16, 16));
    m_angleHud->adjustSize();

    m_angleValid = true;
    m_angleHud->setValid(true);
    m_angleHud->edit()->blockSignals(true);
    // Pre-fill with the current angle as 带符号折角 (v3 定稿，与旋转 HUD 一致)。
    m_angleHud->edit()->setText(formatDeg(signedFoldDeg(m_initialAngle)));
    m_angleHud->edit()->blockSignals(false);
    m_angleHud->show();
    m_angleHud->edit()->setFocus();
    m_angleHud->edit()->selectAll();  // typing immediately replaces the value
}

void ConnectGesture::hideAngleHud()
{
    if (m_angleHud) {
        m_angleHud->hide();
        if (m_scene && !m_scene->views().isEmpty())
            m_scene->views().first()->setFocus();
    }
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
        bool isNumber = false;
        const double numVal = t.toDouble(&isNumber);
        if (isNumber) {
            if (m_angleMode == cad::param::RotationMode::ArcLength) {
                // 输入 = 带符号折角弧长（v3 定稿）→ 存储 α ∈ [0, 360°) 弧长。
                const cad::param::Block* blk = m_paramDoc->findBlock(att->fromBlockId);
                const double radius = blk ? blk->segmentLengthAtPoint(att->fromPointId) : 0.0;
                const double foldDeg = (radius > 1e-9)
                    ? numVal / (M_PI / 180.0 * radius * 0.1) : 0.0;
                const double alphaDeg = alphaFromSignedFold(foldDeg);
                att->rotationMode = cad::param::RotationMode::ArcLength;
                att->arcLength = alphaDeg * M_PI / 180.0 * radius;
                att->arcLengthFormula.clear();
            } else {
                // 输入 = 带符号折角 → 存储 α（v3 定稿）。
                att->rotationMode = cad::param::RotationMode::Angle;
                att->followerAngle = alphaFromSignedFold(numVal);
                att->followerAngleFormula.clear();
            }
            m_angleValid = true;
        } else {
            auto r = cad::param::ConditionEngine::evaluate(
                t, m_paramDoc->parameters(), {});
            if (r.ok) {
                if (m_angleMode == cad::param::RotationMode::ArcLength) {
                    att->rotationMode = cad::param::RotationMode::ArcLength;
                    att->arcLength = geo::Units::cmToMm(r.value);
                    att->arcLengthFormula = t;
                } else {
                    att->rotationMode = cad::param::RotationMode::Angle;
                    att->followerAngle = r.value;
                    att->followerAngleFormula = t;
                }
                m_angleValid = true;
            } else {
                m_angleValid = false;             // keep last valid geometry
            }
        }
    }

    if (m_angleHud) m_angleHud->setValid(m_angleValid);
    if (m_angleValid) {
        // Per-frame preview: only the connected block's layer group moves.
        if (const auto* fb = m_paramDoc->findBlock(att->fromBlockId))
            m_paramDoc->invalidateLayer(fb->layer);
        m_paramDoc->resolveAll();                 // live rotation preview
        m_scene->refreshAllBlockItems();
    }
}

void ConnectGesture::onAngleModeChanged(cad::param::RotationMode mode)
{
    if (!m_paramDoc) return;
    cad::param::Attachment* att = m_paramDoc->findAttachment(m_editingAttachmentId);
    if (!att) return;

    // 公式驱动（角度/弧长表达式）：模式切换只是显示单位变化，绝不换算
    // 烘焙公式——表达式必须原样保留（用户要求）。公式存在时拒绝切换。
    const bool hasFormula =
        (att->rotationMode == cad::param::RotationMode::ArcLength)
            ? !att->arcLengthFormula.isEmpty()
            : !att->followerAngleFormula.isEmpty();
    if (hasFormula && mode != att->rotationMode) {
        if (m_angleHud) m_angleHud->setMode(att->rotationMode);   // 弹回
        return;
    }

    // Geometry-preserving switch: compute current effective angle, then
    // convert to the new mode's value.
    double curDeg = att->followerAngle;
    if (att->rotationMode == cad::param::RotationMode::ArcLength) {
        // Current is arc length → derive angle. 弧长 = 线夹角恒等映射
        // （2026-08 定稿）：弧长 0 = 0° 折叠、πr = 180° 开平，与 Resolver
        // 一致，不再反转。
        const cad::param::Block* blk = m_paramDoc->findBlock(att->fromBlockId);
        double radius = blk ? blk->segmentLengthAtPoint(att->fromPointId) : 0.0;
        double arcMm = att->arcLength;
        if (!att->arcLengthFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                att->arcLengthFormula, m_paramDoc->parameters(), {});
            if (r.ok) arcMm = geo::Units::cmToMm(r.value);
        }
        curDeg = (radius > 1e-9) ? (arcMm / radius) * 180.0 / M_PI : 0.0;
        curDeg = std::fmod(curDeg, 360.0);
        if (curDeg < 0.0) curDeg += 360.0;
    } else if (!att->followerAngleFormula.isEmpty()) {
        auto r = cad::param::ConditionEngine::evaluate(
            att->followerAngleFormula, m_paramDoc->parameters(), {});
        if (r.ok) curDeg = r.value;
    }

    if (mode == cad::param::RotationMode::ArcLength) {
        const cad::param::Block* blk = m_paramDoc->findBlock(att->fromBlockId);
        double radius = blk ? blk->segmentLengthAtPoint(att->fromPointId) : 0.0;
        att->rotationMode = cad::param::RotationMode::ArcLength;
        // 弧长 = 线夹角恒等映射（2026-08 定稿）：弧长角 = 显示角，不再反转。
        att->arcLength = std::fmod(curDeg, 360.0) * M_PI / 180.0 * radius;
        att->arcLengthFormula.clear();
    } else {
        att->rotationMode = cad::param::RotationMode::Angle;
        att->followerAngle = curDeg;
        att->followerAngleFormula.clear();
    }
    m_angleMode = mode;
    m_paramDoc->resolveAll();
    if (m_scene) m_scene->refreshAllBlockItems();

    // Refresh HUD text to show the converted value (带符号折角，v3 定稿)。
    if (m_angleHud) {
        m_angleHud->edit()->blockSignals(true);
        if (mode == cad::param::RotationMode::ArcLength) {
            const cad::param::Block* blk = m_paramDoc->findBlock(att->fromBlockId);
            const double radius = blk ? blk->segmentLengthAtPoint(att->fromPointId) : 0.0;
            const double alphaDeg = (radius > 1e-9)
                ? (att->arcLength / radius) * 180.0 / M_PI : 0.0;
            const double foldDeg = signedFoldDeg(alphaDeg);
            m_angleHud->edit()->setText(formatDeg(foldDeg * M_PI / 180.0 * radius * 0.1));
        } else {
            m_angleHud->edit()->setText(formatDeg(signedFoldDeg(att->followerAngle)));
        }
        m_angleHud->edit()->blockSignals(false);
    }
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
    hideAngleHud();

    // Snapshot the tuned attachment, then restore the COMPLETE pre-drag state
    // (transform + old attachment) and replay the whole gesture through the
    // undo stack: "quick-detach + connect + angle" becomes one undo step.
    if (m_paramDoc && m_undoStack) {
        cad::param::Attachment snapshot;
        bool found = false;
        for (const auto& a : m_paramDoc->attachments()) {
            if (a.id == m_editingAttachmentId) { snapshot = a; found = true; break; }
        }
        if (found) {
            m_paramDoc->removeAttachment(m_editingAttachmentId);
            if (auto* blk = m_paramDoc->findBlock(snapshot.fromBlockId)) {
                blk->transform.origin   = m_connectOrigOrigin;
                blk->transform.rotation = m_connectOrigRotation;
            }
            if (m_connectOldAtt)
                m_paramDoc->addAttachmentRaw(*m_connectOldAtt);  // verbatim (keep snapshot isLocked)

            m_undoStack->beginMacro(QStringLiteral(
                "\xe5\xbb\xba\xe7\xab\x8b\xe8\xbf\x9e\xe6\x8e\xa5"));  // 建立连接
            if (m_connectOldAtt)
                m_undoStack->push(new cad::cmd::RemoveAttachmentCommand(
                    m_paramDoc, m_connectOldAtt->id));
            m_undoStack->push(new cad::cmd::AddAttachmentCommand(m_paramDoc, snapshot));
            m_undoStack->endMacro();
        }
    }

    m_editingAttachmentId = QUuid();
    m_connectFromBlock = QUuid();
    m_connectFromPoint = QUuid();
    m_connectTarget.reset();
    m_connectOldAtt.reset();
    if (m_scene) m_scene->refreshAllBlockItems();
    m_clearSelectionAndIdle();
    // The owner tool is now Idle — sync the gesture's own state too, or
    // active() would stay true (AngleInput) and swallow every later press
    // in ToolSelect::mousePress's connect branch (regression: 连接完成后
    // 线段/画布无法再选中或移动).
    setState(SelectState::Idle);
}

// ── Visual helpers ──

void ConnectGesture::updateConnectMarker()
{
    if (!m_scene) return;
    if (!m_connectTarget.has_value()) {
        removeConnectMarker();
        return;
    }

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    if (zoom < 1e-9) zoom = 1.0;
    // The ring is the SAME size as the snap radius — the magnet's reach made
    // visible: releasing anywhere inside this ring connects to this point.
    const double r = kConnectSnapRadius / zoom;

    if (!m_connectMarker) {
        m_connectMarker = new QGraphicsEllipseItem();
        QPen pen(QColor(38, 166, 154));          // teal: "release = connect"
        pen.setWidthF(2.0);
        pen.setCosmetic(true);
        m_connectMarker->setPen(pen);
        m_connectMarker->setBrush(QColor(38, 166, 154, 50));
        m_connectMarker->setZValue(9999);
        m_scene->addItem(m_connectMarker);
    }
    const QPointF c = cad::geo::Coord::toScene(m_connectTarget->worldPos.x,
                                               m_connectTarget->worldPos.y);
    m_connectMarker->setRect(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);
    m_connectMarker->show();
}

void ConnectGesture::removeConnectMarker()
{
    if (m_connectMarker) {
        if (m_scene) m_scene->removeItem(m_connectMarker);
        delete m_connectMarker;
        m_connectMarker = nullptr;
    }
}

// ── Source-point halo (连接源点光环) ──
// The dashed ring around the dragged point IS the connect reach: its radius
// equals the snap radius, so the moment its edge touches any target point
// the snap fires (and the target ring appears on that point). 所见即所判.

void ConnectGesture::updateConnectHalo()
{
    if (!m_scene || !m_paramDoc) return;
    auto* blk = m_paramDoc->findBlock(m_connectFromBlock);
    if (!blk) return;

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    if (zoom < 1e-9) zoom = 1.0;
    const double r = kConnectSnapRadius / zoom;  // halo == connect reach

    if (!m_connectHalo) {
        m_connectHalo = new QGraphicsEllipseItem();
        QPen pen(QColor(38, 166, 154));
        pen.setWidthF(1.5);
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_connectHalo->setPen(pen);
        m_connectHalo->setBrush(QColor(38, 166, 154, 20));  // faint fill (~8%)
        m_connectHalo->setZValue(9998);                     // under the snap ring
        m_scene->addItem(m_connectHalo);
    }

    const Vec2 src = blk->worldPos(m_connectFromPoint);
    const QPointF c = cad::geo::Coord::toScene(src.x, src.y);
    m_connectHalo->setRect(c.x() - r, c.y() - r, 2.0 * r, 2.0 * r);
    m_connectHalo->show();
}

void ConnectGesture::removeConnectHalo()
{
    if (m_connectHalo) {
        if (m_scene) m_scene->removeItem(m_connectHalo);
        delete m_connectHalo;
        m_connectHalo = nullptr;
    }
}

std::optional<SnapResult> ConnectGesture::hitPoint(const Vec2& worldPos) const
{
    if (!m_paramDoc) return std::nullopt;
    double zoom = 1.0;
    if (m_scene && !m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    // Same generous radius as the connect snap: grabbing a point to start
    // a connection must feel as easy as dropping onto a target.
    return m_snapEngine.findSnap(worldPos, m_paramDoc, zoom, kConnectSnapRadius);
}

} // namespace cad::tools
