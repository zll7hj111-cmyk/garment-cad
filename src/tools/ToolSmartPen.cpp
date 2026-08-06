#include "ToolSmartPen.h"
#include "ToolManager.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QSet>
#include <QFontMetricsF>
#include <QtMath>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

#include "canvas/BlockItem.h"
#include "canvas/CanvasScene.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/MeasureVariable.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "LinePropertyDialog.h"
#include "QuickAuxDialog.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/DocumentCommands.h"

namespace cad::tools {

namespace {

/// Toast text when a freshly established attachment crosses layers (合法方向:
/// aux follower → working leader): "已建立跨层连接（测量层→操作层1）" with
/// the real layer names. Empty for same-layer connections.
QString crossLayerToast(const cad::param::ParamDocument* doc,
                        int fromLayer, int toLayer)
{
    if (!doc || fromLayer < 0 || toLayer < 0) return QString();
    if (doc->isAuxLayer(fromLayer) == doc->isAuxLayer(toLayer)) return QString();
    const auto& layers = doc->layers();
    auto name = [&layers](int idx) {
        return (idx >= 0 && idx < static_cast<int>(layers.size()))
            ? layers[static_cast<size_t>(idx)].name : QStringLiteral("?");
    };
    return QString::fromUtf8("\xe5\xb7\xb2\xe5\xbb\xba\xe7\xab\x8b"
                             "\xe8\xb7\xa8\xe5\xb1\x82\xe8\xbf\x9e\xe6\x8e\xa5"
                             "\xef\xbc\x88%1\u2192%2\xef\xbc\x89")  // 已建立跨层连接（%1→%2）
        .arg(name(fromLayer), name(toLayer));
}

/// True when the attachment with @p attId is actually present in the document
/// (commands may reject the edge — only toast for genuinely established ones).
bool attachmentEstablished(const cad::param::ParamDocument* doc, const QUuid& attId)
{
    if (!doc || attId.isNull()) return false;
    for (const auto& a : doc->attachments())
        if (a.id == attId) return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// HudItem
// ---------------------------------------------------------------------------

HudItem::HudItem(QGraphicsItem* parent)
    : QGraphicsItem(parent)
{
    setZValue(200.0);
}

void HudItem::setText(const QString& text)
{
    prepareGeometryChange();
    m_text = text;
    QFont f(QStringLiteral("Segoe UI"), 9);
    QFontMetricsF fm(f);
    m_rect = fm.boundingRect(m_text).adjusted(-4.0, -2.0, 4.0, 2.0);
}

void HudItem::moveToPoint(const cad::geo::Vec2& userPos, const QGraphicsView* view)
{
    setPos(cad::geo::Coord::toScene(userPos));  // user → scene
    double zoom = (view != nullptr) ? view->transform().m11() : 1.0;
    if (std::abs(zoom) < 1e-9) zoom = 1.0;
    setTransform(QTransform().scale(1.0 / zoom, 1.0 / zoom));
}

QRectF HudItem::boundingRect() const
{
    return m_rect.adjusted(-1.0, -1.0, 1.0, 1.0);
}

void HudItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if (m_text.isEmpty()) return;

    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    // Query style from scene if available.
    QColor bg(255, 255, 255, 215);
    QColor fg(30, 30, 30);
    if (auto* cs = qobject_cast<CanvasScene*>(scene())) {
        bg = cs->style()->hudBackground;
        fg = cs->style()->hudText;
    }

    painter->setPen(QPen(QColor(180, 180, 180), 0.5));
    painter->setBrush(bg);
    painter->drawRect(m_rect);

    painter->setPen(fg);
    painter->setFont(QFont(QStringLiteral("Segoe UI"), 9));
    painter->drawText(m_rect, Qt::AlignCenter, m_text);
}

// ---------------------------------------------------------------------------
// ToolSmartPen
// ---------------------------------------------------------------------------

void ToolSmartPen::activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    m_scene    = &scene;
    m_paramDoc = paramDoc;
    m_state    = State::Idle;
    m_angleSnap = false;
    m_startSnap.reset();
    m_currentSnap.reset();
}

bool ToolSmartPen::isBlankSpace(const QPointF& userPos) const
{
    if (!m_scene) return false;
    const QList<QGraphicsItem*> hits = m_scene->items(userPos);
    for (QGraphicsItem* item : hits) {
        // Curve children belong to their block — walk up to the BlockItem.
        if (BlockItem::containingItem(item) != nullptr)
            return false;
    }
    return true;
}

void ToolSmartPen::deactivate()
{
    if (m_auxDialog)
        m_auxDialog->close();  // WA_DeleteOnClose; finished(Rejected) → no-op
    cancelLine();
    m_scene    = nullptr;
    m_paramDoc = nullptr;
}

void ToolSmartPen::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene) return;

    // While the non-modal quick-aux dialog is open, canvas clicks are
    // ignored — the stroke waits for the dialog to be answered.
    if (m_auxDialog) return;

    if (event->button() == Qt::RightButton) {
        if (m_state == State::Drawing) {
            cancelLine();
        } else if (m_state == State::Idle && isBlankSpace(event->scenePos())) {
            // 空白右键 = 切到选择工具 (无状态时; 实体右键留给未来上下文菜单).
            requestToolSwitch(ToolType::Select);
        }
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    // event->scenePos() is already in user coords (Y-up) thanks to CanvasView conversion
    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 clickPos(sp.x(), sp.y());

    if (m_state == State::Idle) {
        // Line-body quick aux point: the X marker (m_segSnap) is live while
        // the cursor hovers a non-candidate segment body — a click opens the
        // QuickAuxDialog, creates the auxiliary point on the host segment and
        // continues drawing FROM it (start scenario).
        if (m_segSnap) {
            openAuxDialog(*m_segSnap, /*forStart=*/true);
            return;
        }

        // --- Set start point ---
        // Try snapping to existing point
        double zoom = 1.0;
        if (!m_scene->views().isEmpty())
            zoom = m_scene->views().first()->transform().m11();

        auto snap = m_snapEngine.findSnap(clickPos, m_paramDoc, zoom);
        if (snap) {
            // Curve points are valid snap targets too — start a line from them.
            setupSnappedStart(*snap);
        } else {
            m_startPoint = clickPos;
            m_startSnap.reset();
            m_refDirDeg = 0.0;
        }
        beginStroke(event->modifiers());
    }
    else if (m_state == State::Drawing) {
        // --- Set end point and commit ---
        cad::geo::Vec2 end = applyAngleSnap(clickPos);

        // Line-body quick aux point as the stroke's END (attached/bridge):
        // the X marker sits on a non-candidate segment, so this click creates
        // the auxiliary point and pins the line's end to it.
        if (m_segSnap) {
            openAuxDialog(*m_segSnap, /*forStart=*/false);
            return;
        }

        // Check for end-point snap
        double zoom = 1.0;
        if (!m_scene->views().isEmpty())
            zoom = m_scene->views().first()->transform().m11();
        auto endSnap = m_snapEngine.findSnap(clickPos, m_paramDoc, zoom);
        if (endSnap) {
            end = endSnap->worldPos;
            // 用户拍板: 起点自由 + 终点吸附 = 翻转新线 —— 吸附点成为新线
            // 起点, 原起点位置成为自由终点 (终点线变起点, 解开后语义不变:
            // 线段几何/长度/角度完全一致, 仅端点身份互换). 这样终点吸附
            // 也能走统一的“起点连接”路径创建连接, 而非被忽略成自由线.
            if (!m_startSnap) {
                const cad::geo::Vec2 origStart = m_startPoint;
                m_startPoint = endSnap->worldPos;
                m_startSnap = endSnap;
                m_refDirDeg = 0.0;
                commitLine(origStart, std::nullopt);
                return;
            }
        } else {
            // Click priority: point snap > leader-candidate switch > free end.
            // A click on a candidate's body switches the construction-angle
            // reference instead of placing the endpoint.
            if (m_startSnap) {
                const int idx = leaderCandidateAt(clickPos, zoom);
                if (idx >= 0) {
                    setLeaderIndex(idx);
                    return;
                }
            }
        }

        commitLine(end, endSnap);
    }
}

void ToolSmartPen::setupSnappedStart(const SnapResult& snap)
{
    m_startPoint = snap.worldPos;
    m_startSnap  = snap;
    // Cache the leader segment's world "exit" direction so the HUD and
    // Shift snap can work in construction-angle (included angle) space.
    // The exit direction makes 0° mean "continue straight along the
    // leader" regardless of which leader endpoint is snapped.
    m_refDirDeg = 0.0;
    if (const auto* lb = m_paramDoc->findBlock(snap.blockId))
        m_refDirDeg = (lb->transform.rotation
                       + lb->exitDirectionAtPoint(snap.pointId)) * 180.0 / M_PI;
    // Multiple segments may meet here (coincident points stack across
    // blocks). Collect them as leader candidates and auto-pick the
    // first; the user can click a candidate's body or press Tab to
    // switch while the rubber band is live.
    collectLeaderCandidates(snap);
    if (!m_leaderCandidates.empty())
        setLeaderIndex(0);
}

void ToolSmartPen::beginStroke(Qt::KeyboardModifiers mods)
{
    m_state = State::Drawing;
    hideSegMarker();  // the X marker belongs to Idle hover — clear it once the stroke starts

    // Create preview items
    m_previewLine = new QGraphicsLineItem();
    const CanvasStyle* st = m_scene->style();
    QPen pen(st->previewLineColor, 1.5);
    pen.setCosmetic(true);
    pen.setStyle(Qt::DashLine);
    m_previewLine->setPen(pen);
    m_previewLine->setZValue(100.0);
    m_scene->addItem(m_previewLine);

    // 起点标记（橡皮筋浏览点）：直径 1px（半径 0.5），保持可见又不遮挡画布。
    constexpr double r = 0.5;
    m_startMarker = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
    m_startMarker->setPos(cad::geo::Coord::toScene(m_startPoint));
    m_startMarker->setPen(Qt::NoPen);
    m_startMarker->setBrush(m_startSnap ? st->snapPointColor : st->previewLineColor);
    m_startMarker->setZValue(101.0);
    m_scene->addItem(m_startMarker);

    // Snap indicator (small square, hidden by default)
    constexpr double sr = 5.0;
    m_snapIndicator = new QGraphicsRectItem(-sr, -sr, sr * 2.0, sr * 2.0);
    m_snapIndicator->setPen(QPen(st->snapIndicatorColor, 1.5, Qt::SolidLine));
    m_snapIndicator->setBrush(Qt::NoBrush);
    m_snapIndicator->setZValue(102.0);
    m_snapIndicator->setVisible(false);
    m_scene->addItem(m_snapIndicator);

    m_hud = new HudItem();
    m_scene->addItem(m_hud);

    m_angleSnap = mods & Qt::ShiftModifier;
}

void ToolSmartPen::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene) return;

    // Freeze hover feedback while the quick-aux dialog is open.
    if (m_auxDialog) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 cursorPos(sp.x(), sp.y());

    if (m_state != State::Drawing) {
        // Idle: keep the segment-body X marker (quick aux point) in sync with
        // the cursor. This is the ONLY place it is refreshed — without it the
        // marker never appears and line-body aux points cannot be created.
        updateSegMarker(cursorPos);
        return;
    }

    m_angleSnap = event->modifiers() & Qt::ShiftModifier;

    const cad::geo::Vec2 effectiveEnd = applyAngleSnap(cursorPos);

    updatePreview(effectiveEnd);
    updateSnapIndicator(cursorPos);
    updateSegMarker(cursorPos);  // non-candidate segments: X = end aux point
}

void ToolSmartPen::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    (void)event;
}

void ToolSmartPen::keyPress(QKeyEvent* event)
{
    // The open quick-aux dialog owns keyboard interaction (its Esc/Enter
    // must not cancel or disturb the pending stroke).
    if (m_auxDialog) return;

    if (event->key() == Qt::Key_Shift) {
        m_angleSnap = true;
    }
    else if (event->key() == Qt::Key_W) {
        // Cycle through leader candidates while the rubber band is live.
        // (W instead of Tab: Tab is a focus-navigation key and would move focus.)
        if (m_state == State::Drawing && m_leaderCandidates.size() > 1) {
            setLeaderIndex((m_leaderIndex + 1)
                           % static_cast<int>(m_leaderCandidates.size()));
            event->accept();
        }
    }
    else if (event->key() == Qt::Key_Escape) {
        if (m_state == State::Drawing) cancelLine();
    }
}

void ToolSmartPen::keyRelease(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Shift) {
        m_angleSnap = false;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ToolSmartPen::commitLine(const cad::geo::Vec2& end,
                              const std::optional<SnapResult>& endSnap)
{
    if (m_startPoint.distanceSquaredTo(end) < 1e-10) {
        cancelLine();
        return;
    }

    QUuid blockId;
    QUuid segmentId;

    if (m_startSnap && endSnap) {
        // Both ends pinned to existing points → bridge line (桥接线).
        createBridgeLine(*m_startSnap, *endSnap);
    } else if (m_startSnap) {
        createAttachedLine(*m_startSnap, end);
        // Retrieve the last added block/segment IDs
        // (createAttachedLine stores them internally via paramDoc)
    } else {
        createFreeLine(m_startPoint, end);
    }

    clearLeaderState();  // after createAttachedLine — it reads m_leaderIndex
    clearPreview();
    m_state = State::Idle;
    m_startSnap.reset();
    m_currentSnap.reset();

    // addBlock/addAttachment already triggered resolveAll + refreshAllBlockItems
    // via ParamDocument signals — no manual re-resolve needed.

    // Show property dialog for the last created block/segment
    if (m_paramDoc && !m_paramDoc->blocks().empty()) {
        auto& lastBlock = m_paramDoc->blocks().back();
        blockId = lastBlock.id;
        if (!lastBlock.segments.empty()) {
            segmentId = lastBlock.segments.back().id;

            // Find the parent widget for the dialog
            QWidget* parentWidget = m_scene->views().isEmpty()
                ? nullptr : m_scene->views().first();

            // Modeless: user can still interact with panels while editing.
            // isCreation=true: the smart pen just drew this line — 撤销全部
            // in the dialog then means 取消线段创建 (delete the line).
            auto* dlg = new LinePropertyDialog(blockId, segmentId, m_paramDoc,
                                               m_scene, parentWidget,
                                               /*isCreation=*/true);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        }
    }
}

void ToolSmartPen::createFreeLine(const cad::geo::Vec2& start, const cad::geo::Vec2& end)
{
    if (!m_paramDoc) return;

    cad::param::Block block;
    block.layer = m_paramDoc->activeLayer();
    // Segment names default to empty; the user assigns them via the property
    // dialog or the group panel.

    // Block origin at start point (local (0,0) = start)
    block.transform.origin = start;
    block.transform.rotation = 0.0;

    // Start point: Free at local (0,0)
    cad::param::ParamPoint ptStart;
    ptStart.constraint = cad::param::PointConstraint::Free;
    ptStart.freePos = cad::geo::Vec2::zero();
    QUuid startId = ptStart.id;

    // End point: Polar relative to start
    cad::geo::Vec2 delta = end - start;
    double dist = delta.length();
    double angleDeg = std::atan2(delta.y, delta.x) * 180.0 / M_PI;

    cad::param::ParamPoint ptEnd;
    ptEnd.constraint = cad::param::PointConstraint::Polar;
    ptEnd.refPointId = startId;
    ptEnd.distance = dist;
    ptEnd.angle = angleDeg;
    QUuid endId = ptEnd.id;

    block.addPoint(std::move(ptStart));
    block.addPoint(std::move(ptEnd));

    // Segment connecting them
    cad::param::Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    block.addSegment(std::move(seg));

    if (m_undoStack) {
        cad::param::Attachment dummy;
        m_undoStack->push(new cad::cmd::DrawLineCommand(
            m_paramDoc, std::move(block), dummy, false));
    } else {
        m_paramDoc->addBlock(std::move(block));
    }
}

void ToolSmartPen::createAttachedLine(const SnapResult& snapStart, const cad::geo::Vec2& end)
{
    if (!m_paramDoc) return;

    // Attachment target: the user-selected leader candidate (click/Tab during
    // rubber band) wins; without candidates fall back to the raw snap result
    // + auto-picked exit segment. Resolved first because the block origin must
    // sit exactly on the point we actually attach to (coincident points from
    // different blocks may differ by sub-pixel amounts).
    QUuid toBlockId   = snapStart.blockId;
    QUuid toPointId   = snapStart.pointId;
    QUuid toSegmentId;
    if (m_leaderIndex >= 0
        && m_leaderIndex < static_cast<int>(m_leaderCandidates.size())) {
        const LeaderCandidate& cand = m_leaderCandidates[m_leaderIndex];
        toBlockId   = cand.blockId;
        toPointId   = cand.pointId;
        toSegmentId = cand.segmentId;
    }

    cad::geo::Vec2 startWorld = snapStart.worldPos;
    double refWorldRad = 0.0;
    if (const auto* targetBlock = m_paramDoc->findBlock(toBlockId)) {
        startWorld = targetBlock->worldPos(toPointId);
        if (toSegmentId.isNull())
            toSegmentId = targetBlock->exitSegmentAtPoint(toPointId);
        // Reference world direction = target block rotation + local exit
        // direction. exitDirectionAtPoint handles snapping to either endpoint
        // of the leader segment, orienting the reference so 0° always means
        // "keep going straight". The leader segment is recorded explicitly
        // (toSegmentId) so the reference never silently switches when the
        // point gains more segments.
        refWorldRad = targetBlock->transform.rotation
                    + targetBlock->exitDirectionAtPoint(toPointId, toSegmentId);
    }

    cad::param::Block block;
    block.layer = m_paramDoc->activeLayer();
    // Segment names default to empty (see createFreeLine).

    // Block origin at the attached point
    block.transform.origin = startWorld;
    block.transform.rotation = 0.0;

    // Start point: Free at local (0,0) — coincides with snapped point
    cad::param::ParamPoint ptStart;
    ptStart.constraint = cad::param::PointConstraint::Free;
    ptStart.freePos = cad::geo::Vec2::zero();
    QUuid startId = ptStart.id;

    // End point: Polar relative to start
    cad::geo::Vec2 delta = end - startWorld;
    double dist = delta.length();
    double angleDeg = std::atan2(delta.y, delta.x) * 180.0 / M_PI;

    cad::param::ParamPoint ptEnd;
    ptEnd.constraint = cad::param::PointConstraint::Polar;
    ptEnd.refPointId = startId;
    ptEnd.distance = dist;
    ptEnd.angle = angleDeg;
    QUuid endId = ptEnd.id;

    block.addPoint(std::move(ptStart));
    block.addPoint(std::move(ptEnd));

    cad::param::Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    block.addSegment(std::move(seg));

    QUuid newBlockId = block.id;

    cad::param::Attachment att;
    att.fromBlockId = newBlockId;
    att.fromPointId = startId;
    att.toBlockId   = toBlockId;
    att.toPointId   = toPointId;
    att.toSegmentId = toSegmentId;

    // Follower angle = new line's world angle - leader segment world direction
    att.followerAngle = angleDeg - refWorldRad * 180.0 / M_PI;

    // Cross-layer toast bookkeeping (captured BEFORE block is moved away).
    const int fromLayer = block.layer;
    const int toLayer = [&] {
        const auto* leaderBlk = m_paramDoc->findBlock(toBlockId);
        return leaderBlk ? leaderBlk->layer : -1;
    }();
    const QUuid attId = att.id;

    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::DrawLineCommand(
            m_paramDoc, std::move(block), att, true));
    } else {
        m_paramDoc->addBlock(std::move(block));
        m_paramDoc->addAttachment(std::move(att));
    }

    // Toast at the creation site (closest to the user gesture); the id check
    // guards against a rejected edge, and undo/redo replays never re-toast.
    if (m_scene && attachmentEstablished(m_paramDoc, attId)) {
        if (const QString toast = crossLayerToast(m_paramDoc, fromLayer, toLayer);
            !toast.isEmpty())
            m_scene->showToast(toast);
    }
}

void ToolSmartPen::createBridgeLine(const SnapResult& snapStart,
                                    const SnapResult& snapEnd)
{
    if (!m_paramDoc) return;

    // Resolve the actual host points (leader candidate wins for the start).
    QUuid startBlockId = snapStart.blockId;
    QUuid startPointId = snapStart.pointId;
    if (m_leaderIndex >= 0
        && m_leaderIndex < static_cast<int>(m_leaderCandidates.size())) {
        const LeaderCandidate& cand = m_leaderCandidates[m_leaderIndex];
        startBlockId = cand.blockId;
        startPointId = cand.pointId;
    }

    cad::geo::Vec2 startWorld = snapStart.worldPos;
    if (const auto* hb = m_paramDoc->findBlock(startBlockId))
        startWorld = hb->worldPos(startPointId);
    cad::geo::Vec2 endWorld = snapEnd.worldPos;
    if (const auto* hb = m_paramDoc->findBlock(snapEnd.blockId))
        endWorld = hb->worldPos(snapEnd.pointId);

    if (startWorld.distanceSquaredTo(endWorld) < 1e-10)
        return;  // Both points on the same spot — nothing to draw.

    // --- Measure variable: |P1 - P2| published as a formula parameter ---
    cad::param::MeasureVariable mv;
    mv.blockA = startBlockId;
    mv.pointA = startPointId;
    mv.blockB = snapEnd.blockId;
    mv.pointB = snapEnd.pointId;
    mv.value = startWorld.distanceTo(endWorld);
    // Reference names are uppercase by convention (CopyChip force-uppercases
    // them for display/editing); generate uppercase so the stored refName
    // matches what the user sees and types back into formula fields.
    mv.refName = QStringLiteral("M_") + cad::param::Serial::randomPrefix().toUpper();

    // --- Free line (new bridge model): length = measure var, angle = world ---
    cad::geo::Vec2 delta = endWorld - startWorld;
    const double lenMm = delta.length();
    const double worldAngleDeg = std::atan2(delta.y, delta.x) * 180.0 / M_PI;

    cad::param::Block block;
    block.layer = m_paramDoc->activeLayer();
    block.transform.origin = startWorld;
    block.transform.rotation = worldAngleDeg * M_PI / 180.0;
    // The measurement belongs to this bridge line: deleting the line deletes
    // the variable, and clicking the card highlights the line (not the hosts).
    mv.ownerBlockId = block.id;

    cad::param::ParamPoint ptStart;
    ptStart.constraint = cad::param::PointConstraint::Free;
    ptStart.freePos = cad::geo::Vec2::zero();
    QUuid startId = ptStart.id;

    // End point: Polar along local X (block rotation carries the world angle).
    // Length is driven by the measure variable formula.
    cad::param::ParamPoint ptEnd;
    ptEnd.constraint = cad::param::PointConstraint::Polar;
    ptEnd.refPointId = startId;
    ptEnd.distance = lenMm;
    ptEnd.distanceFormula = mv.refName;  // length = M_xxx
    ptEnd.angle = 0.0;
    QUuid endId = ptEnd.id;

    block.addPoint(std::move(ptStart));
    block.addPoint(std::move(ptEnd));

    cad::param::Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    seg.lengthFormula = mv.refName;
    block.addSegment(std::move(seg));

    // --- Default follow (构造线默认跟随): start follows host A, end aims at
    // host B. Both can be released later via the property dialog.
    block.endTargetBlockId = snapEnd.blockId;
    block.endTargetPointId = snapEnd.pointId;
    block.endTargetOffset = 0.0;

    std::optional<cad::param::Attachment> followAtt;
    if (const auto* leader = m_paramDoc->findBlock(startBlockId)) {
        cad::param::Attachment att;
        att.fromBlockId = block.id;
        att.fromPointId = startId;
        att.toBlockId   = startBlockId;
        att.toPointId   = startPointId;
        att.toSegmentId = leader->exitSegmentAtPoint(startPointId);
        // Back-solve the follower angle so the initial world direction is
        // preserved: rotation = refWorld + followerAngle.
        const double refWorldRad = leader->transform.rotation
            + leader->exitDirectionAtPoint(startPointId, att.toSegmentId);
        att.followerAngle = cad::geo::normalizeDeg180(worldAngleDeg
            - refWorldRad * 180.0 / M_PI);
        followAtt = std::move(att);
    }

    // Cross-layer toast bookkeeping (captured BEFORE block is moved away).
    const int fromLayer = block.layer;
    const int toLayer = followAtt
        ? ([&] { const auto* l = m_paramDoc->findBlock(followAtt->toBlockId);
                 return l ? l->layer : -1; })()
        : -1;
    const QUuid attId = followAtt ? followAtt->id : QUuid();

    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::DrawMeasureLineCommand(
            m_paramDoc, std::move(block), std::move(mv), std::move(followAtt)));
    } else {
        m_paramDoc->addMeasure(std::move(mv));
        m_paramDoc->addBlock(std::move(block));
        if (followAtt)
            m_paramDoc->addAttachment(std::move(*followAtt));
    }

    // Toast at the creation site; the id check covers the command rejecting
    // the follow attachment (DrawMeasureLineCommand::m_attAdded guard).
    if (m_scene && attachmentEstablished(m_paramDoc, attId)) {
        if (const QString toast = crossLayerToast(m_paramDoc, fromLayer, toLayer);
            !toast.isEmpty())
            m_scene->showToast(toast);
    }
}

void ToolSmartPen::cancelLine()
{
    clearLeaderState();
    clearPreview();
    m_state = State::Idle;
    m_startSnap.reset();
    m_currentSnap.reset();
}

void ToolSmartPen::clearPreview()
{
    if (!m_scene) return;

    if (m_previewLine)   { m_scene->removeItem(m_previewLine);   delete m_previewLine;   m_previewLine   = nullptr; }
    if (m_startMarker)   { m_scene->removeItem(m_startMarker);   delete m_startMarker;   m_startMarker   = nullptr; }
    if (m_snapIndicator) { m_scene->removeItem(m_snapIndicator); delete m_snapIndicator; m_snapIndicator = nullptr; }
    if (m_segMarker)     { m_scene->removeItem(m_segMarker);     delete m_segMarker;     m_segMarker     = nullptr; }
    if (m_hud)           { m_scene->removeItem(m_hud);           delete m_hud;           m_hud           = nullptr; }
    m_segSnap.reset();
}

cad::geo::Vec2 ToolSmartPen::applyAngleSnap(const cad::geo::Vec2& raw) const
{
    const cad::geo::Vec2 delta = raw - m_startPoint;
    const double dist = delta.length();
    if (dist < 1e-12) return raw;

    // Work in construction-angle space: angle relative to the leader segment.
    // m_refDirDeg == 0 for a free line, so this degenerates to the world angle.
    const double rawWorldDeg = std::atan2(delta.y, delta.x) * 180.0 / M_PI;
    const double relDeg      = rawWorldDeg - m_refDirDeg;

    if (!m_angleSnap) {
        m_snapAngleDeg = cad::geo::normalizeDeg180(relDeg);
        return raw;
    }

    const double snappedRel = std::round(relDeg / 45.0) * 45.0;
    m_snapAngleDeg          = cad::geo::normalizeDeg180(snappedRel);
    const double rad        = (snappedRel + m_refDirDeg) * M_PI / 180.0;
    return m_startPoint + cad::geo::Vec2(std::cos(rad) * dist, std::sin(rad) * dist);
}

void ToolSmartPen::updatePreview(const cad::geo::Vec2& effectiveEnd)
{
    if (m_previewLine) {
        QPointF p1 = cad::geo::Coord::toScene(m_startPoint);
        QPointF p2 = cad::geo::Coord::toScene(effectiveEnd);
        m_previewLine->setLine(p1.x(), p1.y(), p2.x(), p2.y());
    }

    if (!m_hud || !m_scene) return;

    const double lenMm = m_startPoint.distanceTo(effectiveEnd);
    QString text = cad::geo::Units::formatLength(lenMm, 1);
    // Show the follower angle: always when attached to a leader segment,
    // otherwise only while Shift angle-snap is active.
    if (m_startSnap || m_angleSnap) {
        text += QStringLiteral("  %1°").arg(m_snapAngleDeg, 0, 'f', 0);
    }
    if (m_currentSnap) {
        text += QStringLiteral("  → %1").arg(
            m_currentSnap->pointName.isEmpty()
                ? QStringLiteral("点")
                : m_currentSnap->pointName);
    }
    m_hud->setText(text);

    QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    m_hud->moveToPoint(effectiveEnd, view);
}

void ToolSmartPen::updateSnapIndicator(const cad::geo::Vec2& worldPos)
{
    if (!m_snapIndicator) return;

    double zoom = 1.0;
    if (m_scene && !m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    auto snap = m_snapEngine.findSnap(worldPos, m_paramDoc, zoom);
    m_currentSnap = snap;

    if (snap) {
        m_snapIndicator->setPos(cad::geo::Coord::toScene(snap->worldPos));
        m_snapIndicator->setVisible(true);
    } else {
        m_snapIndicator->setVisible(false);
    }
}

// ---------------------------------------------------------------------------
// Segment-body snap (线身 X 标记 / 快捷辅助点)
// ---------------------------------------------------------------------------

void ToolSmartPen::updateSegMarker(const cad::geo::Vec2& worldPos)
{
    m_segSnap.reset();
    if (!m_scene || !m_paramDoc) { hideSegMarker(); return; }

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();

    // Point snap wins: no X while the cursor would snap to an endpoint.
    if (m_snapEngine.findSnap(worldPos, m_paramDoc, zoom)) {
        hideSegMarker();
        return;
    }

    // Leader candidates are excluded while drawing: clicking their body
    // switches the construction-angle reference — and a line from the start
    // point to a point on an incident segment would be degenerate anyway.
    QSet<QUuid> exclude;
    if (m_state == State::Drawing) {
        for (const auto& cand : m_leaderCandidates)
            exclude.insert(cand.segmentId);
    }

    m_segSnap = m_snapEngine.findSegmentSnap(
        worldPos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx(),
        exclude.isEmpty() ? nullptr : &exclude);

    if (!m_segSnap) {
        hideSegMarker();
        return;
    }

    if (!m_segMarker) {
        // Fixed screen-size X (叉叉): the click-here-to-add-a-point cue. Green
        // like the auxiliary points it creates.
        constexpr double s = 4.0;
        QPainterPath cross;
        cross.moveTo(-s, -s); cross.lineTo(s, s);
        cross.moveTo(-s, s);  cross.lineTo(s, -s);
        m_segMarker = new QGraphicsPathItem(cross);
        QPen pen(m_scene->style()->auxMarkerColor, 1.6);
        pen.setCosmetic(true);
        m_segMarker->setPen(pen);
        m_segMarker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        m_segMarker->setZValue(102.0);
        m_scene->addItem(m_segMarker);
    }
    m_segMarker->setPos(cad::geo::Coord::toScene(m_segSnap->worldPos));
    m_segMarker->setVisible(true);
}

void ToolSmartPen::hideSegMarker()
{
    if (m_segMarker)
        m_segMarker->setVisible(false);
}


void ToolSmartPen::openAuxDialog(const SegmentSnapResult& segSnap, bool forStart)
{
    if (!m_paramDoc || m_auxDialog) return;
    auto* block = m_paramDoc->findBlock(segSnap.blockId);
    auto* seg = block ? block->findSegment(segSnap.segmentId) : nullptr;
    if (!block || !seg) return;

    hideSegMarker();

    // Prepared point: Interpolated defaults, percent = projection t so the
    // confirmed point lands exactly under the X marker. The name stays empty
    // — the serial is the identity (dual-track naming).
    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::Interpolated;
    pt.hostSegmentId = seg->id;
    pt.isAuxiliary = true;
    pt.visible = true;
    pt.showName = false;
    pt.interpPercent = segSnap.t;
    pt.serial = m_paramDoc->newPointSerial();

    QWidget* parentWidget = m_scene && !m_scene->views().isEmpty()
        ? m_scene->views().first() : nullptr;
    auto* dlg = new QuickAuxDialog(pt, block->findPoint(seg->startPointId),
                                   block->findPoint(seg->endPointId), parentWidget);
    // NON-modal on purpose: the user must be able to switch to the variable
    // panel and copy a formula while this dialog stays open.
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_auxDialog = dlg;
    m_auxDialogForStart = forStart;
    m_auxDialogSegSnap = segSnap;

    QObject::connect(dlg, &QDialog::finished, dlg, [this](int result) {
        auto* dlg = m_auxDialog.data();
        m_auxDialog = nullptr;
        if (result == QDialog::Accepted && dlg)
            onAuxDialogAccepted(dlg->point());
        // Rejected / closed: nothing created, stroke state untouched
        // (Idle stays Idle; Drawing keeps its rubber band).
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void ToolSmartPen::onAuxDialogAccepted(const cad::param::ParamPoint& pt)
{
    if (!m_paramDoc || !m_scene) return;

    auto snap = commitAuxPoint(pt, m_auxDialogSegSnap.blockId,
                               m_auxDialogSegSnap.segmentId);
    if (!snap) return;  // host segment vanished while the dialog was open

    if (m_auxDialogForStart) {
        if (m_state != State::Idle) return;  // safety: state drifted
        setupSnappedStart(*snap);
        beginStroke(Qt::NoModifier);
    } else {
        if (m_state != State::Drawing) return;  // safety: state drifted
        commitLine(snap->worldPos, snap);
    }
}

std::optional<SnapResult> ToolSmartPen::commitAuxPoint(
    const cad::param::ParamPoint& pt,
    const QUuid& blockId, const QUuid& segmentId)
{
    if (!m_paramDoc) return std::nullopt;
    auto* block = m_paramDoc->findBlock(blockId);
    auto* seg = block ? block->findSegment(segmentId) : nullptr;
    if (!block || !seg) return std::nullopt;

    // Own undo step (建点与建线分开撤销): the aux point belongs to the host
    // segment and survives deletion of the borrowing line.
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::AddAuxPointCommand(
            m_paramDoc, blockId, segmentId, pt));
    } else {
        block->addPoint(pt);
        seg->auxPointIds.push_back(pt.id);
        m_paramDoc->resolveAll();
    }

    // Synthesize a SnapResult on the resolved point so the normal
    // attached/bridge flow can pin to it.
    const auto* hb = m_paramDoc->findBlock(blockId);
    const auto* created = hb ? hb->findPoint(pt.id) : nullptr;
    if (!created || !created->resolved)
        return std::nullopt;
    return SnapResult{
        .worldPos  = hb->transform.toWorld(created->resolvedPos),
        .blockId   = blockId,
        .pointId   = pt.id,
        .pointName = created->name
    };
}

// ---------------------------------------------------------------------------
// Leader candidate selection
// ---------------------------------------------------------------------------

void ToolSmartPen::collectLeaderCandidates(const SnapResult& snap)
{
    clearLeaderState();
    if (!m_paramDoc || !m_scene) return;

    double zoom = 1.0;
    if (!m_scene->views().isEmpty())
        zoom = m_scene->views().first()->transform().m11();
    if (std::abs(zoom) < 1e-9) zoom = 1.0;
    // Same reach as the snap itself: what looks like one point may be several
    // coincident points from different blocks stacked on the same spot, and
    // every segment incident to any of them is a valid angle reference.
    const double tol = m_snapEngine.snapRadius / zoom;

    struct Ranked {
        LeaderCandidate cand;
        int rank;   ///< Lower = preferred for the auto-pick.
    };
    std::vector<Ranked> ranked;
    QSet<QUuid> seenSegments;

    for (const auto& block : m_paramDoc->blocks()) {
        for (const auto& pt : block.points) {
            if (!pt.resolved) continue;
            const cad::geo::Vec2 wp = block.transform.toWorld(pt.resolvedPos);
            if (wp.distanceTo(snap.worldPos) > tol) continue;

            for (const auto& seg : block.segments) {
                const bool isEndpoint = (seg.startPointId == pt.id
                                         || seg.endPointId == pt.id);
                const bool isHost =
                    (pt.constraint == cad::param::PointConstraint::Interpolated
                     && pt.hostSegmentId == seg.id);
                if (!isEndpoint && !isHost) continue;
                if (seenSegments.contains(seg.id)) continue;
                seenSegments.insert(seg.id);

                // Auto-pick order: endpoint segments before host segments,
                // then Outline < Internal < Auxiliary, then creation order
                // (stable sort keeps document iteration order within a rank).
                int rank = isEndpoint ? 0 : 100;
                switch (seg.role) {
                case cad::param::SegmentRole::Outline:   rank += 0; break;
                case cad::param::SegmentRole::Internal:  rank += 1; break;
                case cad::param::SegmentRole::Auxiliary: rank += 2; break;
                }
                ranked.push_back({{block.id, pt.id, seg.id}, rank});
            }
        }
    }

    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const Ranked& a, const Ranked& b) {
                         return a.rank < b.rank;
                     });

    m_leaderCandidates.reserve(ranked.size());
    for (const auto& r : ranked)
        m_leaderCandidates.push_back(r.cand);
}

void ToolSmartPen::setLeaderIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_leaderCandidates.size()))
        return;

    // Move the teal highlight to the new candidate's block item.
    if (m_scene && !m_highlightBlockId.isNull()) {
        if (auto* item = m_scene->findBlockItem(m_highlightBlockId))
            item->setLeaderHighlight(QUuid());
    }

    m_leaderIndex = index;
    const LeaderCandidate& cand = m_leaderCandidates[index];
    m_highlightBlockId = cand.blockId;
    if (m_scene) {
        if (auto* item = m_scene->findBlockItem(cand.blockId))
            item->setLeaderHighlight(cand.segmentId);
    }

    // Re-anchor construction-angle space (HUD / Shift snap / final followerAngle)
    // on the new leader's exit direction.
    if (m_paramDoc) {
        if (const auto* lb = m_paramDoc->findBlock(cand.blockId))
            m_refDirDeg = (lb->transform.rotation
                           + lb->exitDirectionAtPoint(cand.pointId, cand.segmentId))
                          * 180.0 / M_PI;
    }
}

void ToolSmartPen::clearLeaderState()
{
    if (m_scene && !m_highlightBlockId.isNull()) {
        if (auto* item = m_scene->findBlockItem(m_highlightBlockId))
            item->setLeaderHighlight(QUuid());
    }
    m_highlightBlockId = QUuid();
    m_leaderCandidates.clear();
    m_leaderIndex = -1;
}

int ToolSmartPen::leaderCandidateAt(const cad::geo::Vec2& worldPos,
                                    double zoom) const
{
    if (!m_paramDoc) return -1;
    if (std::abs(zoom) < 1e-9) zoom = 1.0;
    // Same pick tolerance as segment hover — the two gestures should feel
    // identical (hoverRadiusPx is the shared interaction token).
    const double tol = (m_scene ? m_scene->style()->hoverRadiusPx() : 8.0) / zoom;

    int best = -1;
    double bestDist = tol;
    for (int i = 0; i < static_cast<int>(m_leaderCandidates.size()); ++i) {
        const LeaderCandidate& cand = m_leaderCandidates[i];
        const auto* block = m_paramDoc->findBlock(cand.blockId);
        if (!block) continue;
        const auto* seg = block->findSegment(cand.segmentId);
        if (!seg) continue;
        const auto* a = block->findPoint(seg->startPointId);
        const auto* b = block->findPoint(seg->endPointId);
        if (!a || !b || !a->resolved || !b->resolved) continue;
        const cad::geo::Vec2 wa = block->transform.toWorld(a->resolvedPos);
        const cad::geo::Vec2 wb = block->transform.toWorld(b->resolvedPos);
        const double d = cad::geo::Vec2::distanceToSegment(worldPos, wa, wb);
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

} // namespace cad::tools
