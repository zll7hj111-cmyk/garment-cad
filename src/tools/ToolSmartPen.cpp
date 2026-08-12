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
#include "QuickAuxDialog.h"
#include "LeaderCandidatePicker.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/DocumentCommands.h"

namespace cad::tools {

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
    // (Re)create the extracted collaborators with the current context.
    delete m_lineFactory;
    delete m_leaderPicker;
    m_lineFactory = new LineFactory(m_paramDoc, m_undoStack, m_scene);
    m_leaderPicker = new LeaderCandidatePicker(m_scene, m_paramDoc);
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
    delete m_lineFactory;
    m_lineFactory = nullptr;
    delete m_leaderPicker;
    m_leaderPicker = nullptr;
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
            m_leaderPicker->setRefDirDeg(0.0);
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
                m_leaderPicker->setRefDirDeg(0.0);
                commitLine(origStart, std::nullopt);
                return;
            }
        } else {
            // Click priority: point snap > leader-candidate switch > free end.
            // A click on a candidate's body switches the construction-angle
            // reference instead of placing the endpoint.
            if (m_startSnap) {
                const int idx = m_leaderPicker->candidateAt(clickPos, zoom);
                if (idx >= 0) {
                    m_leaderPicker->setIndex(idx);
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
    // 闭合基准（用户拍板 2026-08）：0° = 折叠重叠、180° = 直行延续，
    // 与起点/终点吸附无关。
    double refDirDeg = 0.0;
    if (const auto* lb = m_paramDoc->findBlock(snap.blockId))
        refDirDeg = (lb->transform.rotation
                     + lb->exitDirectionAtPoint(snap.pointId)) * 180.0 / M_PI;
    // Multiple segments may meet here (coincident points stack across
    // blocks). Collect them as leader candidates and auto-pick the
    // first; the user can click a candidate's body or press W to
    // switch while the rubber band is live.
    m_leaderPicker->collect(snap);
    m_leaderPicker->setRefDirDeg(refDirDeg);
    if (!m_leaderPicker->candidates().empty())
        m_leaderPicker->setIndex(0);
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
        if (m_state == State::Drawing
            && m_leaderPicker->candidates().size() > 1) {
            m_leaderPicker->setIndex((m_leaderPicker->index() + 1)
                % static_cast<int>(m_leaderPicker->candidates().size()));
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

    if (m_startSnap && endSnap) {
        // Both ends pinned to existing points → bridge line (桥接线).
        m_lineFactory->createBridgeLine(*m_startSnap, *endSnap,
                                        m_leaderPicker->index(),
                                        m_leaderPicker->candidates());
    } else if (m_startSnap) {
        m_lineFactory->createAttachedLine(*m_startSnap, end,
                                          m_leaderPicker->index(),
                                          m_leaderPicker->candidates());
    } else {
        m_lineFactory->createFreeLine(m_startPoint, end);
    }

    m_leaderPicker->clear();  // after createAttachedLine — it reads the index
    clearPreview();
    m_state = State::Idle;
    m_startSnap.reset();
    m_currentSnap.reset();

    // addBlock/addAttachment already triggered resolveAll + refreshAllBlockItems
    // via ParamDocument signals — no manual re-resolve needed.

    // Notify the host: the status-bar edit strip (SegmentEditBar) replaces the
    // old creation dialog. blockId/segmentId = the last created block/segment.
    if (m_scene && m_paramDoc && !m_paramDoc->blocks().empty()) {
        const auto& lastBlock = m_paramDoc->blocks().back();
        if (!lastBlock.segments.empty())
            m_scene->notifyLineCreated(lastBlock.id, lastBlock.segments.back().id);
    }
}

void ToolSmartPen::cancelLine()
{
    m_leaderPicker->clear();
    clearPreview();
    m_state = State::Idle;
    m_startSnap.reset();
    m_currentSnap.reset();
    // Aborted stroke: withdraw the status-bar preview readout.
    if (m_scene) m_scene->notifyLinePreview(0.0, 0.0);
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
    // refDirDeg == 0 for a free line, so this degenerates to the world angle.
    const double refDirDeg = m_leaderPicker->refDirDeg();
    const double rawWorldDeg = std::atan2(delta.y, delta.x) * 180.0 / M_PI;
    const double relDeg      = rawWorldDeg - refDirDeg;

    // 显示约定（2026-08 v3 定稿，与旋转工具 HUD 一致）：
    // 附着 leader 时 = 带符号折角 [−180°, +180°]（折叠 0 / 垂直 ±90 /
    // 直行 ±180：闭合基线 α = 180° − 相对角，符号 = 折向，与
    // setupSnappedStart 注释一致）；
    // 自由起点 = 水平基准绝对角（0~360° 逆时针为正，行业默认）。
    auto displayOf = [&](double rel) {
        if (m_startSnap.has_value()) {
            double alpha = std::fmod(180.0 - rel, 360.0);
            if (alpha < 0.0) alpha += 360.0;
            return alpha > 180.0 ? alpha - 360.0 : alpha;
        }
        double d = std::fmod(rel, 360.0);
        if (d < 0.0) d += 360.0;
        return d;
    };

    if (!m_angleSnap) {
        m_snapAngleDeg = displayOf(relDeg);
        return raw;
    }

    const double snappedRel = std::round(relDeg / 45.0) * 45.0;
    m_snapAngleDeg          = displayOf(snappedRel);
    const double rad        = (snappedRel + refDirDeg) * M_PI / 180.0;
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

    // Live readout for the status-bar preview (创建中只读读数).
    if (m_scene)
        m_scene->notifyLinePreview(cad::geo::Units::mmToCm(lenMm), m_snapAngleDeg);

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
        for (const auto& cand : m_leaderPicker->candidates())
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
    // NOTE: no WA_DeleteOnClose — the dialog schedules its own deleteLater()
    // on close (ElaAppBar's default close path would destroy it mid-call).
    // NON-modal on purpose: the user must be able to switch to the variable
    // panel and copy a formula while this dialog stays open.
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
        // 起点自由 + 终点辅助点 = 翻转 (与 mousePress 终点吸附同一规则):
        // 辅助点成为新线起点, 原起点位置成为自由终点, 然后走统一的起点
        // 连接路径 —— 否则 commitLine 会退化成 createFreeLine, 连接根本
        // 不建立 (无 Attachment → 拖动保护失效, 用户双击面板手动连才生效).
        if (!m_startSnap) {
            const cad::geo::Vec2 origStart = m_startPoint;
            m_startPoint = snap->worldPos;
            m_startSnap = snap;
            m_leaderPicker->setRefDirDeg(0.0);
            commitLine(origStart, std::nullopt);
            return;
        }
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

} // namespace cad::tools







