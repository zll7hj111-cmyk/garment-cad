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
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QtMath>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

#include "canvas/BlockItem.h"
#include "HitTester.h"
#include "canvas/CanvasScene.h"
#include "canvas/HudItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/MeasureVariable.h"
#include "parametric/Serial.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "ui/QuickAuxDialog.h"
#include "LeaderCandidatePicker.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/DocumentCommands.h"
#include "geometry/Epsilon.h"

namespace cad::tools {

// ---------------------------------------------------------------------------
// ToolSmartPen
// ---------------------------------------------------------------------------

ToolDescriptor ToolSmartPen::describe()
{
    ToolDescriptor d;
    d.id = ToolType::SmartPen;
    d.displayName = QString::fromUtf8("智能笔(&L)");
    d.iconName = QStringLiteral("pen");
    d.shortcut = QKeySequence(Qt::Key_L);
    d.hintText = modeIndicatorFor(State::Idle, 0)
                     .hint(reinterpret_cast<const char*>(u8"智能笔"));
    d.factory = [] { return std::make_unique<ToolSmartPen>(); };
    return d;
}

void ToolSmartPen::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)scene;
    (void)paramDoc;
    m_state    = State::Idle;
    m_angleSnap = false;
    m_startSnap.reset();
    m_currentSnap.reset();
    // (Re)create the extracted collaborators with the current context.
    delete m_lineFactory;
    delete m_leaderPicker;
    delete m_strokeInput;
    m_lineFactory = new LineFactory(m_paramDoc, m_undoStack, m_scene);
    m_leaderPicker = new LeaderCandidatePicker(m_scene, m_paramDoc);
    m_strokeInput = new SmartPenStrokeInput(
        m_paramDoc,
        [this](const QString& msg) {
            if (m_scene) m_scene->showToast(msg);
        });
    refreshModeIndicator();
}

bool ToolSmartPen::isBlankSpace(const QPointF& userPos) const
{
    if (!m_scene) return false;
    // 统一命中 (P1/M7+L2): 不分层 —— 实体处右键保留给未来的上下文菜单。
    return isBlankSpaceAtScene(*m_scene, userPos);
}

void ToolSmartPen::onDeactivate()
{
    if (m_auxDialog)
        m_auxDialog->close();  // WA_DeleteOnClose; finished(Rejected) → no-op
    clearDialogBlockedCursor();   // M10: 工具切走恢复画布光标
    cancelLine();
    delete m_lineFactory;
    m_lineFactory = nullptr;
    delete m_leaderPicker;
    m_leaderPicker = nullptr;
    delete m_strokeInput;
    m_strokeInput = nullptr;
}

void ToolSmartPen::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene) return;

    // While a non-modal helper dialog (quick-aux) is open, canvas
    // clicks are ignored — the stroke waits for the dialog to be answered.
    // M10: 兜底维持 Forbidden 光标 (打开时已设, 防被 hover 路径覆写)。
    if (m_auxDialog) {
        showDialogBlockedFeedback();
        return;
    }

    if (event->button() == Qt::RightButton) {
        if (m_state == State::Drawing || m_state == State::ConfirmEnd) {
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

    if (m_state == State::ConfirmEnd) {
        handleConfirmEndPress(clickPos);
        return;
    }

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
        double zoom = m_scene->safeZoom();

        auto snap = m_snapEngine.findSnap(clickPos, m_paramDoc, zoom);
        if (snap) {
            // Curve points are valid snap targets too — start a line from them.
            setupSnappedStart(*snap);
            // Stacked-point bookkeeping: every candidate at the same spot is a
            // switchable start target while the rubber band is live (点线切换).
            m_startPool = overlapPool(clickPos, *snap);
        } else {
            m_startPoint = clickPos;
            m_startSnap.reset();
            m_leaderPicker->setRefDirDeg(0.0);
            m_startPool.clear();
        }
        startStroke(event->modifiers());
    }
    else if (m_state == State::Drawing) {
        // --- Set end point and commit ---
        // 预输入约束优先：长度/角度已由状态栏给定，终点按约束计算，
        // 不再借用线段身辅助点或终点吸附（预输入 = 几何已确定）。
        if (m_strokeInput->hasConstraint()) {
            const cad::geo::Vec2 end = applyPreInputConstraints(clickPos);
            if (m_startPoint.distanceSquaredTo(end) < cad::geo::kGeomEpsUltra) {
                cancelLine();
                return;
            }
            commitLine(end, std::nullopt);
            return;
        }

        cad::geo::Vec2 end = applyAngleSnap(clickPos);

        // Line-body quick aux point as the stroke's END (attached/bridge):
        // the X marker sits on a non-candidate segment, so this click creates
        // the auxiliary point and pins the line's end to it.
        if (m_segSnap) {
            openAuxDialog(*m_segSnap, /*forStart=*/false);
            return;
        }

        // Check for end-point snap
        double zoom = m_scene->safeZoom();
        auto endSnap = m_snapEngine.findSnap(clickPos, m_paramDoc, zoom);
        if (endSnap) {
            // Stacked candidates at the end spot (several layers' points on
            // the same place) → confirm state: the default is the ACTIVE
            // layer's point (先选活动层); clicking a candidate segment
            // commits with that point, blank click accepts the default,
            // Esc cancels the whole stroke.
            const std::vector<SnapResult> pool = overlapPool(clickPos, *endSnap);
            if (pool.size() >= 2) {
                enterEndConfirm(*endSnap, pool);
                return;
            }
            commitEndSnap(*endSnap);
            return;
        }
        // Click priority: point snap > leader-candidate switch > free end.
        // A click on a candidate's body either switches the START POINT (when
        // the candidate's point is another stacked candidate at the start
        // spot — a legal attachment target), or — for the SAME point — the
        // construction-angle reference. A different point that is NOT a legal
        // target (e.g. a grayed aux segment while a working layer is active)
        // is ignored: the attachment target follows the leader candidate, so
        // switching there would silently reject the attachment and degrade
        // the line into a free line.
        if (m_startSnap) {
            const int idx = m_leaderPicker->candidateAt(clickPos, zoom);
            if (idx >= 0) {
                const LeaderCandidate& cand =
                    m_leaderPicker->candidates()[static_cast<size_t>(idx)];
                if (trySwitchStartPoint(cand, idx)) return;
                if (cand.pointId == m_startSnap->pointId)
                    m_leaderPicker->setIndex(idx);
                return;
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
        refDirDeg = cad::geo::radToDeg(lb->transform.rotation
                     + lb->exitDirectionAtPoint(snap.pointId));
    // Multiple segments may meet here (coincident points stack across
    // blocks). Collect them as leader candidates and auto-pick the
    // candidate ON THE SNAPPED POINT — the attachment target follows the
    // leader candidate (LineFactory::createAttachedLine), so it must agree
    // with what findSnap picked (which resolves exact cross-layer stacks to
    // the ACTIVE layer). Without this, the ranking (creation order) could
    // override the snap and attach to another layer's coincident point —
    // the "总是捕捉到别的图层" trap. The user can still click a candidate's
    // body or press W to switch while the rubber band is live.
    m_leaderPicker->collect(snap);
    m_leaderPicker->setRefDirDeg(refDirDeg);
    if (!m_leaderPicker->candidates().empty()) {
        int autoIdx = 0;
        for (int i = 0; i < static_cast<int>(m_leaderPicker->candidates().size()); ++i) {
            const auto& cand = m_leaderPicker->candidates()[static_cast<size_t>(i)];
            if (cand.blockId == snap.blockId && cand.pointId == snap.pointId) {
                autoIdx = i;
                break;
            }
        }
        m_leaderPicker->setIndex(autoIdx);
    }
}

void ToolSmartPen::startStroke(Qt::KeyboardModifiers mods)
{
    captureStrokeInput();

    // 预输入完整（长度+角度）：一次点击即可成线，无需第二击。
    if (m_strokeInput->hasConstraint()) {
        // 仅当长度与角度都有效时才单击成线; 只长度或只角度仍需第二击
        // (方向/距离由第二击确定)。
        if (m_strokeInput->hasLength() && m_strokeInput->hasAngle()) {
            commitLine(fixedPreInputEnd(), std::nullopt);
            return;
        }
    }

    beginStroke(mods);
}

void ToolSmartPen::captureStrokeInput()
{
    m_strokeInput->capture(m_preInput);
}

cad::geo::Vec2 ToolSmartPen::applyPreInputConstraints(const cad::geo::Vec2& cursor)
{
    const cad::geo::Vec2 end = m_strokeInput->applyToCursor(
        cursor, m_startPoint,
        applyAngleSnap(cursor),
        [this]() -> double {
            const bool snapped = m_startSnap.has_value();
            const double refDir = m_leaderPicker ? m_leaderPicker->refDirDeg() : 0.0;
            return m_strokeInput->toWorldAngleDeg(
                m_strokeInput->displayAngleDeg(), snapped, refDir);
        });
    // HUD 显示角回写 (原 applyPreInputConstraints 的角度分支赋值 m_snapAngleDeg)。
    m_snapAngleDeg = m_strokeInput->currentDisplayAngle();
    return end;
}

cad::geo::Vec2 ToolSmartPen::fixedPreInputEnd() const
{
    return m_strokeInput->fixedEnd(m_startPoint);
}

LineBuildOptions ToolSmartPen::strokeBuildOptions() const
{
    return m_strokeInput->buildOptions(m_strokeInput->raw().name);
}

void ToolSmartPen::consumePreInput()
{
    m_strokeInput->consume(m_preInput);
}

void ToolSmartPen::beginStroke(Qt::KeyboardModifiers mods)
{
    setState(State::Drawing);
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
    m_managed.own(m_previewLine, &m_previewLine);

    // 起点标记（橡皮筋浏览点）：直径 1px（半径 0.5），保持可见又不遮挡画布。
    constexpr double r = 0.5;
    m_startMarker = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
    m_startMarker->setPos(cad::geo::Coord::toScene(m_startPoint));
    m_startMarker->setPen(Qt::NoPen);
    m_startMarker->setBrush(m_startSnap ? st->snapPointColor : st->previewLineColor);
    m_startMarker->setZValue(101.0);
    m_scene->addItem(m_startMarker);
    m_managed.own(m_startMarker, &m_startMarker);

    // Snap indicator (small square, hidden by default)
    constexpr double sr = 5.0;
    m_snapIndicator = new QGraphicsRectItem(-sr, -sr, sr * 2.0, sr * 2.0);
    m_snapIndicator->setPen(QPen(st->snapIndicatorColor, 1.5, Qt::SolidLine));
    m_snapIndicator->setBrush(Qt::NoBrush);
    m_snapIndicator->setZValue(102.0);
    m_snapIndicator->setVisible(false);
    m_scene->addItem(m_snapIndicator);
    m_managed.own(m_snapIndicator, &m_snapIndicator);

    m_hud = new HudItem();
    m_scene->addItem(m_hud);
    m_managed.own(m_hud, &m_hud);

    m_angleSnap = mods & Qt::ShiftModifier;
}

void ToolSmartPen::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene) return;

    // Freeze hover feedback while a non-modal helper dialog is open.
    if (m_auxDialog) {
        showDialogBlockedFeedback();   // M10: 维持 Forbidden + 提示 (toast 同值短路)
        return;
    }

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 cursorPos(sp.x(), sp.y());

    if (m_state == State::ConfirmEnd) {
        // 落点确认: rubber band locked on the default pick; hovering a
        // confirmable candidate segment teal-highlights it (no X marker or
        // stray snap rings in this state).
        if (m_endAutoPick)
            updatePreview(m_endAutoPick->worldPos);
        updateEndConfirmHighlight(cursorPos);
        return;
    }

    if (m_state != State::Drawing) {
        // Idle: keep the segment-body X marker (quick aux point) in sync with
        // the cursor. This is the ONLY place it is refreshed — without it the
        // marker never appears and line-body aux points cannot be created.
        updateSegMarker(cursorPos);
        // 上下文属性条: 智能笔**只上报悬停, 从不锁定** —— 这里的点击是
        // 落点/画线, 没有"选中线段"这回事 (用户拍板)。画线中条带显示的是
        // 正在画的那条线 (MainWindow 经 showStrokePreview 驱动), 所以只有
        // Idle 态才需要上报悬停。
        if (m_scene && m_paramDoc) {
            const QPointF hoverPt = cad::geo::Coord::toScene(cursorPos.x, cursorPos.y);
            const auto hoverHits = blockHitsAtScene(*m_scene, *m_paramDoc, hoverPt);
            if (hoverHits.empty())
                reportHoverTarget(QUuid(), QUuid());
            else
                reportHoverTarget(hoverHits.front().blockId, hoverHits.front().segmentId);
        }
        return;
    }

    m_angleSnap = event->modifiers() & Qt::ShiftModifier;

    const cad::geo::Vec2 effectiveEnd =
        m_strokeInput->hasConstraint()
            ? applyPreInputConstraints(cursorPos)
            : applyAngleSnap(cursorPos);

    updatePreview(effectiveEnd);
    updateSnapIndicator(cursorPos);
    // Reuse the point-snap result computed above: a repeated findSnap() full
    // scan here costs one extra pass over every block's points per frame.
    updateSegMarker(cursorPos, &m_currentSnap);  // non-candidate segments: X = end aux point
}

void ToolSmartPen::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    (void)event;
}

void ToolSmartPen::keyPress(QKeyEvent* event)
{
    // The open non-modal helper dialog owns keyboard interaction (its Esc/Enter
    // must not cancel or disturb the pending stroke).
    if (m_auxDialog) return;

    if (event->key() == Qt::Key_Shift) {
        m_angleSnap = true;
    }
    else if (event->key() == Qt::Key_W) {
        if (m_state == State::Drawing
            && m_leaderPicker->candidates().size() > 1) {
            // 画线中：W 循环 leader 候选（构造角参考/吸附目标的切换）。
            m_leaderPicker->setIndex((m_leaderPicker->index() + 1)
                % static_cast<int>(m_leaderPicker->candidates().size()));
            event->accept();
        }
    }
    else if (event->key() == Qt::Key_Escape) {
        if (m_state == State::Drawing || m_state == State::ConfirmEnd)
            cancelLine();
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
    if (m_startPoint.distanceSquaredTo(end) < cad::geo::kGeomEpsUltra) {
        cancelLine();
        return;
    }

    const LineBuildOptions opts = strokeBuildOptions();

    if (m_startSnap && endSnap) {
        // Both ends pinned to existing points → bridge line (桥接线).
        // 预输入长度/角度对桥接线被动几何无意义，仅名称生效。
        m_lineFactory->createBridgeLine(*m_startSnap, *endSnap,
                                        m_leaderPicker->index(),
                                        m_leaderPicker->candidates(), opts);
    } else if (m_startSnap) {
        m_lineFactory->createAttachedLine(*m_startSnap, end,
                                          m_leaderPicker->index(),
                                          m_leaderPicker->candidates(), opts);
    } else {
        m_lineFactory->createFreeLine(m_startPoint, end, opts);
    }

    consumePreInput();  // 预输入一次性：内容已使用，立即清空。

    m_leaderPicker->clear();  // after createAttachedLine — it reads the index
    clearPreview();
    setState(State::Idle);
    m_startSnap.reset();
    m_currentSnap.reset();
    resetStrokeTargets();

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
    setState(State::Idle);
    m_startSnap.reset();
    m_currentSnap.reset();
    resetStrokeTargets();
    // Aborted stroke: withdraw the status-bar preview readout.
    if (m_scene) m_scene->notifyLinePreview(0.0, 0.0);
}

void ToolSmartPen::setState(State s)
{
    m_state = s;
    // 状态决定"此刻按 W 会发生什么" (Drawing 且多候选时 W = 循环候选),
    // 所以每次迁移都要刷提示 —— 散在 5 个赋值点各记一次迟早漏一处。
    refreshModeIndicator();
}

ModeIndicator ToolSmartPen::modeIndicator() const
{
    const int leaderCount = m_leaderPicker
        ? static_cast<int>(m_leaderPicker->candidates().size()) : 0;
    return modeIndicatorFor(m_state, leaderCount);
}

ModeIndicator ToolSmartPen::modeIndicatorFor(State state, int leaderCount)
{
    ModeIndicator mi;
    mi.modeName = QString::fromUtf8("直线");
    mi.detail  = QString::fromUtf8("点设起点 | 再点设终点 | Shift约束45° | 右键/Esc取消 | 空白右键→选择");
    mi.isDefault = true;

    // 画线中/确认终点态: 把步骤描述换成"当前该做的事"
    if (state == State::Drawing)
        mi.detail = QString::fromUtf8("移动预览长度与角度 | 再点设终点 | Shift约束45° | 右键/Esc取消");
    else if (state == State::ConfirmEnd)
        mi.detail = QString::fromUtf8("点击或回车确认终点 | 右键/Esc取消");

    // W 的语义: Drawing 且有多个 leader 候选时 W = 循环候选
    if (state == State::Drawing && leaderCount > 1) {
        mi.wAction = QString::fromUtf8("W 循环 leader 候选");
    }
    return mi;
}

void ToolSmartPen::showDialogBlockedFeedback()
{
    // M10: 弹窗打开期间画布给明确反馈 —— 用户查完公式回来点画布不再像
    // "工具卡死" (旧实现所有输入被静默吞掉, 光标不变、无提示)。
    if (m_scene && !m_scene->views().isEmpty())
        m_scene->views().first()->viewport()->setCursor(Qt::ForbiddenCursor);
    const QString toast = QString::fromUtf8("请先完成「快速辅助点」设置");
    // 同值守卫: 文案不变就不重复 toast —— showToast 无内置短路, mouseMove
    // 每帧早退调用会重定位+重启 1400ms 定时器造成刷屏。
    if (m_scene && toast != m_lastDialogToast) {
        m_lastDialogToast = toast;
        m_scene->showToast(toast);
    }
}

void ToolSmartPen::clearDialogBlockedCursor()
{
    m_lastDialogToast.clear();   // 下次打开同类弹窗重新 toast
    if (m_scene && !m_scene->views().isEmpty())
        m_scene->views().first()->viewport()->unsetCursor();
}

void ToolSmartPen::clearPreview()
{
    if (!m_scene) return;

    // 会话中释放-重建型: release 单项 (释放 + 影子置空 + 撤销登记, P1/L1)。
    m_managed.release(m_previewLine);
    m_managed.release(m_startMarker);
    m_managed.release(m_snapIndicator);
    m_managed.release(m_segMarker);
    m_managed.release(m_hud);
    m_segSnap.reset();
}

cad::geo::Vec2 ToolSmartPen::applyAngleSnap(const cad::geo::Vec2& raw) const
{
    const cad::geo::Vec2 delta = raw - m_startPoint;
    const double dist = delta.length();
    if (dist < cad::geo::kGeomEpsTight) return raw;

    // Work in construction-angle space: angle relative to the leader segment.
    // refDirDeg == 0 for a free line, so this degenerates to the world angle.
    const double refDirDeg = m_leaderPicker->refDirDeg();
    const double rawWorldDeg = cad::geo::radToDeg(std::atan2(delta.y, delta.x));
    const double relDeg      = rawWorldDeg - refDirDeg;

    // 显示约定（2026-08 v3 定稿，与旋转工具 HUD 一致）：
    // 附着 leader 时 = 带符号折角 [−180°, +180°]（折叠 0 / 垂直 ±90 /
    // 直行 ±180：闭合基线 α = 180° − 相对角，符号 = 折向，与
    // setupSnappedStart 注释一致）；
    // 自由起点 = 水平基准绝对角（0~360° 逆时针为正，行业默认）。
    auto displayOf = [&](double rel) {
        if (m_startSnap.has_value())
            return cad::geo::normalizeDeg180(180.0 - rel);
        return cad::geo::normalizeDeg360(rel);
    };

    if (!m_angleSnap) {
        m_snapAngleDeg = displayOf(relDeg);
        return raw;
    }

    const double snappedRel = std::round(relDeg / 45.0) * 45.0;
    m_snapAngleDeg          = displayOf(snappedRel);
    const double rad        = cad::geo::degToRad(snappedRel + refDirDeg);
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
    QString text = cad::geo::Units::formatLength(lenMm);
    // Show the follower angle: always when attached to a leader segment,
    // otherwise only while Shift angle-snap is active.
    if (m_startSnap || m_angleSnap) {
        text += QStringLiteral("  %1°").arg(cad::geo::Units::formatDegValue(m_snapAngleDeg));
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
    m_hud->moveToPoint(effectiveEnd, view, HudItem::kCursorOffset);
}

void ToolSmartPen::updateSnapIndicator(const cad::geo::Vec2& worldPos)
{
    if (!m_snapIndicator) return;

    double zoom = m_scene->safeZoom();

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

void ToolSmartPen::updateSegMarker(const cad::geo::Vec2& worldPos,
                                     const std::optional<SnapResult>* knownPointSnap)
{
    m_segSnap.reset();
    if (!m_scene || !m_paramDoc) { hideSegMarker(); return; }

    double zoom = m_scene->safeZoom();

    // Point snap wins: no X while the cursor would snap to an endpoint.
    // When the caller already ran findSnap() this frame (Drawing state runs
    // updateSnapIndicator first), pass the result instead of re-scanning.
    const bool pointSnapped = knownPointSnap
        ? knownPointSnap->has_value()
        : m_snapEngine.findSnap(worldPos, m_paramDoc, zoom).has_value();
    if (pointSnapped) {
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
        m_managed.own(m_segMarker, &m_segMarker);
    }
    m_segMarker->setPos(cad::geo::Coord::toScene(m_segSnap->worldPos));
    m_segMarker->setVisible(true);
}

void ToolSmartPen::hideSegMarker()
{
    if (m_segMarker)
        m_segMarker->setVisible(false);
}

} // namespace cad::tools







