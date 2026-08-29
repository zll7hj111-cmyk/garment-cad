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
    // M5 (TOOL_SYSTEM_AUDIT): 原提示完全没提 W —— 省道线模式在界面上不可
    // 发现 (切过去后下一次点击的语义完全不同: 起点必须吸附到已有点, 否则
    // 直接被拒)。提示带上当前模式 + W 键, 且模式切换时会被实时覆盖。
    d.hintText = modeIndicatorFor(Mode::Line, State::Idle, 0)
                     .hint(reinterpret_cast<const char*>(u8"智能笔"));
    d.factory = [] { return std::make_unique<ToolSmartPen>(); };
    return d;
}

void ToolSmartPen::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)scene;
    (void)paramDoc;
    // 这里刻意直接赋值而不是 setState(): 上面 m_mode 还没复位、下面的
    // m_leaderPicker 也还是旧的, 此时刷提示会拿半成品状态去算文案。
    // 统一在末尾 (模式已复位 + 协作对象已重建) 刷一次。
    m_state    = State::Idle;
    m_angleSnap = false;
    // P2/L5 常驻实例: 每次进入回到直线模式 (旧"销毁重建"即此语义)。
    m_mode     = Mode::Line;
    m_startSnap.reset();
    m_currentSnap.reset();
    // (Re)create the extracted collaborators with the current context.
    delete m_lineFactory;
    delete m_leaderPicker;
    m_lineFactory = new LineFactory(m_paramDoc, m_undoStack, m_scene);
    m_leaderPicker = new LeaderCandidatePicker(m_scene, m_paramDoc);
    // 常驻实例下每次进入都要把状态栏刷回直线模式 —— 和画布反馈同理, 否则
    // 上次会话停在省道线而状态栏还写着直线。
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
    if (m_dartDialog)
        m_dartDialog->close();  // finished(Rejected) → keeps Drawing state
    clearDialogBlockedCursor();   // M10: 工具切走恢复画布光标
    cancelLine();
    delete m_lineFactory;
    m_lineFactory = nullptr;
    delete m_leaderPicker;
    m_leaderPicker = nullptr;
}

void ToolSmartPen::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene) return;

    // While a non-modal helper dialog (quick-aux / dart) is open, canvas
    // clicks are ignored — the stroke waits for the dialog to be answered.
    // M10: 兜底维持 Forbidden 光标 (打开时已设, 防被 hover 路径覆写)。
    if (m_auxDialog || m_dartDialog) {
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
        if (m_mode == Mode::Dart) {
            // 省道线起点 A：必须吸附到已有点（自由起点禁止进入省道模式；
            // 系统设计点不孤立，均挂线/端点）。线段身点击不创建辅助点。
            double zoom = m_scene->currentZoom();
            auto snap = m_snapEngine.findSnap(clickPos, m_paramDoc, zoom);
            if (!snap) {
                if (m_scene)
                    m_scene->showToast(QStringLiteral("省道线起点必须吸附到已有点"));
                return;
            }
            setupSnappedStart(*snap);
            m_startPool.clear();
            beginStroke(event->modifiers());  // rubber-band preview toward B
            return;
        }

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
        double zoom = m_scene->currentZoom();

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
        if (m_mode == Mode::Dart) {
            // 省道线第二击：选择偏移点 B（线段上的点）。预览线已显示
            // A→光标；B 确定后弹窗填 偏移 d / 角度 β（默认 90）/ 名称。
            double zoom = m_scene->currentZoom();
            auto bSnap = m_snapEngine.findSnap(clickPos, m_paramDoc, zoom);
            if (!bSnap) {
                if (m_scene)
                    m_scene->showToast(QStringLiteral("省道线偏移点必须选择线段上的点"));
                return;  // stay in Drawing — pick another B
            }
            openDartDialog(*bSnap);
            return;
        }

        // --- Set end point and commit ---
        // 预输入约束优先：长度/角度已由状态栏给定，终点按约束计算，
        // 不再借用线段身辅助点或终点吸附（预输入 = 几何已确定）。
        if (m_strokeInput.hasLength || m_strokeInput.hasAngle) {
            const cad::geo::Vec2 end = applyPreInputConstraints(clickPos);
            if (m_startPoint.distanceSquaredTo(end) < 1e-10) {
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
        double zoom = m_scene->currentZoom();
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
        refDirDeg = (lb->transform.rotation
                     + lb->exitDirectionAtPoint(snap.pointId)) * 180.0 / M_PI;
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
    if (m_strokeInput.hasLength && m_strokeInput.hasAngle) {
        commitLine(fixedPreInputEnd(), std::nullopt);
        return;
    }

    beginStroke(mods);
}

void ToolSmartPen::captureStrokeInput()
{
    m_strokeInput = StrokeInput{};
    m_strokeInput.raw = m_preInput;

    const QString lenText = m_preInput.lengthCm.trimmed();
    if (!lenText.isEmpty()) {
        const auto parsed = cad::geo::parseNumberOrFormula(lenText);
        if (parsed.isNumber) {
            m_strokeInput.hasLength = true;
            m_strokeInput.lengthMm = cad::geo::Units::cmToMm(parsed.value);
        } else if (m_paramDoc) {
            if (cad::param::ConditionEngine::evaluateLengthMm(
                    parsed.formula, m_paramDoc->parameters(), m_paramDoc->conditions(),
                    m_strokeInput.lengthMm)) {
                m_strokeInput.hasLength = true;
                m_strokeInput.lengthFormula = parsed.formula;
            } else if (m_scene) {
                m_scene->showToast(QString::fromUtf8("预输入长度无法计算，已忽略"));
            }
        }
    }

    const QString angText = m_preInput.angleDeg.trimmed();
    if (!angText.isEmpty()) {
        const auto parsed = cad::geo::parseNumberOrFormula(angText);
        if (parsed.isNumber) {
            m_strokeInput.hasAngle = true;
            m_strokeInput.displayAngleDeg = parsed.value;
        } else if (m_paramDoc) {
            auto r = cad::param::ConditionEngine::evaluate(
                parsed.formula, m_paramDoc->parameters(), m_paramDoc->conditions());
            if (r.ok) {
                m_strokeInput.hasAngle = true;
                m_strokeInput.displayAngleDeg = r.value;
                m_strokeInput.angleFormula = parsed.formula;
            } else if (m_scene) {
                m_scene->showToast(QString::fromUtf8("预输入角度无法计算，已忽略"));
            }
        }
    }
}

cad::geo::Vec2 ToolSmartPen::applyPreInputConstraints(const cad::geo::Vec2& cursor) const
{
    if (m_strokeInput.hasAngle) {
        // 角度已定：光标沿固定方向射线投影，第二击只决定长度。
        const double worldDeg = toWorldAngleDeg(m_strokeInput.displayAngleDeg);
        const double rad = worldDeg * M_PI / 180.0;
        const cad::geo::Vec2 dir(std::cos(rad), std::sin(rad));
        double t = (cursor - m_startPoint).dot(dir);
        if (t < 0.0) t = 0.0;
        m_snapAngleDeg = m_strokeInput.displayAngleDeg;
        return m_startPoint + dir * t;
    }

    // 长度已定：沿用（可 Shift 吸附的）光标方向，距离固定。
    cad::geo::Vec2 end = applyAngleSnap(cursor);
    const cad::geo::Vec2 delta = end - m_startPoint;
    const double dist = delta.length();
    if (dist > 1e-12)
        end = m_startPoint + delta * (m_strokeInput.lengthMm / dist);
    return end;
}

cad::geo::Vec2 ToolSmartPen::fixedPreInputEnd() const
{
    const double worldDeg = toWorldAngleDeg(m_strokeInput.displayAngleDeg);
    const double rad = worldDeg * M_PI / 180.0;
    return m_startPoint
        + cad::geo::Vec2(std::cos(rad), std::sin(rad)) * m_strokeInput.lengthMm;
}

double ToolSmartPen::toWorldAngleDeg(double displayDeg) const
{
    // 附着起点：显示角 = 跟随折角（闭合基准 α = 180° − 相对角）；
    // 自由起点：显示角 = 绝对世界角。
    if (m_startSnap)
        return m_leaderPicker->refDirDeg() + (180.0 - displayDeg);
    return displayDeg;
}

LineBuildOptions ToolSmartPen::strokeBuildOptions() const
{
    LineBuildOptions opts;
    opts.name = m_strokeInput.raw.name.trimmed();
    if (m_strokeInput.hasLength) {
        opts.hasLength = true;
        opts.lengthMm = m_strokeInput.lengthMm;
        opts.lengthFormula = m_strokeInput.lengthFormula;
    }
    if (m_strokeInput.hasAngle) {
        opts.hasAngle = true;
        opts.displayAngleDeg = m_strokeInput.displayAngleDeg;
        opts.angleFormula = m_strokeInput.angleFormula;
    }
    return opts;
}

void ToolSmartPen::consumePreInput()
{
    // 一次性预输入：只清空本次真正生效的字段（内容被使用后就清空）。
    // 仅当状态栏文本仍等于快照时才清——若用户在画线过程中又输入了新值，
    // 新值保留给下一条线。名称随任何成功创建的线生效。
    auto consume = [](QString& current, const QString& used, bool usedThisTime) {
        if (usedThisTime && !used.isEmpty() && current == used)
            current.clear();
    };
    consume(m_preInput.name, m_strokeInput.raw.name, true);
    consume(m_preInput.lengthCm, m_strokeInput.raw.lengthCm,
            m_strokeInput.hasLength);
    consume(m_preInput.angleDeg, m_strokeInput.raw.angleDeg,
            m_strokeInput.hasAngle);
    m_strokeInput = StrokeInput{};
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
    if (m_auxDialog || m_dartDialog) {
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
        // Dart mode keeps the marker hidden (segment-body clicks are reserved
        // for line mode; the dart flow only snaps to existing points).
        if (m_mode == Mode::Line)
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
        (m_strokeInput.hasLength || m_strokeInput.hasAngle)
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
    if (m_auxDialog || m_dartDialog) return;

    if (event->key() == Qt::Key_Shift) {
        m_angleSnap = true;
    }
    else if (event->key() == Qt::Key_W) {
        if (m_state == State::Idle) {
            // 工具模式切换统一用 W 键：空闲时循环 直线 ↔ 省道线。
            // (W instead of Tab: Tab is a focus-navigation key and would move focus.)
            cycleMode();
            event->accept();
        } else if (m_state == State::Drawing
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
    if (m_startPoint.distanceSquaredTo(end) < 1e-10) {
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

// ---------------------------------------------------------------------------
// 省道线模式 (dart line, 用户拍板 2026-08)
// ---------------------------------------------------------------------------

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
    return modeIndicatorFor(m_mode, m_state, leaderCount);
}

ModeIndicator ToolSmartPen::modeIndicatorFor(Mode mode, State state, int leaderCount)
{
    ModeIndicator mi;
    mi.modeName = (mode == Mode::Dart) ? QString::fromUtf8("省道线")
                                       : QString::fromUtf8("直线");

    if (mode == Mode::Dart) {
        mi.detail  = QString::fromUtf8("点起点A(须吸附已有点) | 点线段上偏移点B | 填偏移d/角度β | 右键/Esc取消");
        mi.wAction = QString::fromUtf8("W 切直线");
        mi.toast   = QString::fromUtf8("省道线模式：点起点A → 点偏移点B → 填偏移 d 与角度");
    } else {
        mi.detail  = QString::fromUtf8("点设起点 | 再点设终点 | Shift约束45° | 右键/Esc取消 | 空白右键→选择");
        mi.wAction = QString::fromUtf8("W 切省道线");
        mi.toast   = QString::fromUtf8("智能笔：直线模式");
        // 直线是默认态 → 画布角标不显示; 切到省道线才挂上。省道线的起点
        // 必须吸附已有点, 否则直接被拒 —— 这个"下一次点击的语义"值得常驻
        // 提示, 光靠 1.4 秒的 toast 不够。
        mi.isDefault = true;
    }

    // 画线中/确认终点态: 把步骤描述换成"当前该做的事" —— Idle 版写的是
    // "点设起点", 起笔后还挂着这句就是误导。
    if (state == State::Drawing)
        mi.detail = QString::fromUtf8("移动预览长度与角度 | 再点设终点 | Shift约束45° | 右键/Esc取消");
    else if (state == State::ConfirmEnd)
        mi.detail = QString::fromUtf8("点击或回车确认终点 | 右键/Esc取消");

    // W 的语义随状态变化: 只有 Idle 才切模式, Drawing 且有多个 leader 候选
    // 时 W = 循环候选。只跟 mode 走会给出「W 切省道线」这种此刻根本不成立
    // 的指引 (旧实现正是如此 —— 画线中按 W 会静默循环候选, 提示却说切模式)。
    if (state != State::Idle) {
        mi.wAction = (state == State::Drawing && leaderCount > 1)
                         ? QString::fromUtf8("W 循环 leader 候选")
                         : QString();   // 单候选/确认终点态 W 无效 → 别提它
    }
    return mi;
}

void ToolSmartPen::cycleMode()
{
    m_mode = (m_mode == Mode::Line) ? Mode::Dart : Mode::Line;
    // 单一出口: toast 讲"刚切成什么了" (1.4s 后消失), 状态栏常驻讲
    // "现在是哪个模式、W 会切到哪"。
    announceModeChange();
}

void ToolSmartPen::showDialogBlockedFeedback()
{
    // M10: 弹窗打开期间画布给明确反馈 —— 用户查完公式回来点画布不再像
    // "工具卡死" (旧实现所有输入被静默吞掉, 光标不变、无提示)。
    if (m_scene && !m_scene->views().isEmpty())
        m_scene->views().first()->viewport()->setCursor(Qt::ForbiddenCursor);
    const QString toast = m_auxDialog
        ? QString::fromUtf8("请先完成「快速辅助点」设置")
        : QString::fromUtf8("请先完成「省道线」设置");
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

void ToolSmartPen::openDartDialog(const SnapResult& bSnap)
{
    if (!m_startSnap || !m_paramDoc || !m_scene || m_dartDialog) return;

    QWidget* parent = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
    auto* dlg = new QDialog(parent);
    dlg->setWindowTitle(QStringLiteral("\u7701\u9053\u7ebf"));
    // NON-modal on purpose: the user must be able to switch to the
    // variable/formula panel and copy a name/expression while this dialog
    // stays open.
    dlg->setModal(false);
    auto* form = new QFormLayout(dlg);
    auto* nameEdit = new QLineEdit(dlg);
    nameEdit->setPlaceholderText(QStringLiteral("\u7ebf\u6bb5\u540d\u79f0\uff08\u53ef\u9009\uff09"));
    nameEdit->setText(m_preInput.name.trimmed());
    auto* offEdit = new QLineEdit(dlg);
    offEdit->setPlaceholderText(QStringLiteral("\u504f\u79fb\u8ddd\u79bb d\uff08\u6570\u5b57=mm\uff1b\u516c\u5f0f=cm \u57df\uff09"));
    auto* angEdit = new QLineEdit(dlg);
    angEdit->setText(QStringLiteral("90"));
    form->addRow(QStringLiteral("\u540d\u79f0"), nameEdit);
    form->addRow(QStringLiteral("\u504f\u79fb d"), offEdit);
    form->addRow(QStringLiteral("\u89d2\u5ea6 \u03b2"), angEdit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    form->addRow(buttons);

    // Validate + commit on OK, but keep the dialog open when the input is
    // invalid so the user can fix it (or go copy a variable name).
    QObject::connect(buttons, &QDialogButtonBox::accepted, dlg,
        [this, dlg, bSnap, nameEdit, offEdit, angEdit]() {
            if (!m_startSnap || !m_paramDoc || !m_lineFactory) return;

            // Offset d: a pure number is mm; anything else is a cm-domain formula.
            const QString offText = offEdit->text().trimmed();
            if (offText.isEmpty()) {
                if (m_scene)
                    m_scene->showToast(QStringLiteral("\u504f\u79fb\u8ddd\u79bb d \u4e0d\u80fd\u4e3a\u7a7a"));
                return;
            }
            QString offsetFormula;
            const auto parsedOffset = cad::geo::parseNumberOrFormula(offText);
            double offsetMm = parsedOffset.value;
            if (!parsedOffset.isNumber) {
                if (!cad::param::ConditionEngine::evaluateLengthMm(
                        parsedOffset.formula, m_paramDoc->parameters(), m_paramDoc->conditions(),
                        offsetMm)) {
                    if (m_scene)
                        m_scene->showToast(QStringLiteral("\u504f\u79fb\u8ddd\u79bb\u516c\u5f0f\u65e0\u6cd5\u8ba1\u7b97"));
                    return;
                }
                offsetFormula = parsedOffset.formula;
            }

            // Angle β relative to the reference segment (default 90°).
            QString angleFormula;
            double betaDeg = 90.0;
            const QString angText = angEdit->text().trimmed();
            if (!angText.isEmpty()) {
                const auto parsedAng = cad::geo::parseNumberOrFormula(angText);
                betaDeg = parsedAng.value;
                if (!parsedAng.isNumber) {
                    auto r = cad::param::ConditionEngine::evaluate(
                        parsedAng.formula, m_paramDoc->parameters(), m_paramDoc->conditions());
                    if (!r.ok) {
                        if (m_scene)
                            m_scene->showToast(QStringLiteral("\u89d2\u5ea6\u516c\u5f0f\u65e0\u6cd5\u8ba1\u7b97"));
                        return;
                    }
                    betaDeg = r.value;
                    angleFormula = parsedAng.formula;
                }
            }

            commitDartLine(m_startSnap->blockId, m_startSnap->pointId,
                           bSnap.blockId, bSnap.pointId,
                           offsetMm, betaDeg, nameEdit->text().trimmed(),
                           offsetFormula, angleFormula);
            dlg->accept();
        });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    m_dartDialog = dlg;
    showDialogBlockedFeedback();
    QObject::connect(dlg, &QDialog::finished, dlg, [this, dlg](int result) {
        if (m_dartDialog == dlg) {
            m_dartDialog = nullptr;
            clearDialogBlockedCursor();   // M10: 关闭即恢复画布光标
        }
        dlg->deleteLater();
        // Rejected / closed: nothing committed — Drawing keeps its rubber
        // band so the user may pick another offset point B.
        (void)result;
    });

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void ToolSmartPen::commitDartLine(const QUuid& aBlockId, const QUuid& aPointId,
                                  const QUuid& bBlockId, const QUuid& bPointId,
                                  double offsetMm, double angleDeg,
                                  const QString& name,
                                  const QString& offsetFormula,
                                  const QString& angleFormula)
{
    if (!m_lineFactory || !m_paramDoc || !m_startSnap) return;

    const SnapResult aSnap{m_startSnap->worldPos, aBlockId, aPointId};
    const SnapResult bSnap{cad::geo::Vec2(), bBlockId, bPointId};
    // The pre-input strip does not drive dart geometry (calculated from
    // A/B/d/β); only the dialog-provided name is forwarded.
    LineBuildOptions opts;
    opts.name = name;
    m_lineFactory->createDartLine(aSnap, bSnap, offsetMm, angleDeg,
                                  opts, offsetFormula, angleFormula);

    consumePreInput();
    m_leaderPicker->clear();
    clearPreview();
    setState(State::Idle);
    m_startSnap.reset();
    m_currentSnap.reset();
    resetStrokeTargets();

    if (m_scene && m_paramDoc && !m_paramDoc->blocks().empty()) {
        const auto& lastBlock = m_paramDoc->blocks().back();
        if (!lastBlock.segments.empty())
            m_scene->notifyLineCreated(lastBlock.id, lastBlock.segments.back().id);
    }
}

// ---------------------------------------------------------------------------
// 落点确认 (stacked-point disambiguation)
// ---------------------------------------------------------------------------

std::vector<SnapResult> ToolSmartPen::overlapPool(
    const cad::geo::Vec2& spot, const SnapResult& snap) const
{
    std::vector<SnapResult> out;
    if (!m_paramDoc) return out;
    double zoom = m_scene ? m_scene->currentZoom() : 1.0;
    // findSnapCandidates applies the same layer policy as findSnap
    // (layerSnappable) — only LEGAL attachment targets ever enter the pool,
    // so a switch can never propose a rejected cross-layer attachment.
    const auto cands = m_snapEngine.findSnapCandidates(spot, m_paramDoc, zoom);
    for (const auto& c : cands)
        if (c.worldPos.distanceTo(snap.worldPos) <= kSnapOverlapEps)
            out.push_back(c);
    return out;
}

std::optional<SnapResult> ToolSmartPen::preferActiveLayer(
    const std::vector<SnapResult>& pool,
    const std::optional<SnapResult>& fallback) const
{
    if (!m_paramDoc) return fallback;
    const QUuid active = m_paramDoc->activeLayer();
    for (const auto& c : pool) {   // nearest first
        const auto* blk = m_paramDoc->findBlock(c.blockId);
        if (blk && blk->layer == active) return c;
    }
    return fallback;
}

void ToolSmartPen::enterEndConfirm(const SnapResult& snap,
                                   const std::vector<SnapResult>& pool)
{
    // 先选活动层: the default end target is the active layer's stacked
    // point (fallback = the raw nearest pick; findSnap already resolves
    // exact coincidences to the active layer too).
    m_endAutoPick = preferActiveLayer(pool, snap);

    // Confirmable segments: every segment incident (endpoint or interpolated
    // host) to any pool point — same collection rule as the leader picker.
    m_endCands.clear();
    QSet<QUuid> seen;
    for (const auto& p : pool) {
        const auto* blk = m_paramDoc ? m_paramDoc->findBlock(p.blockId) : nullptr;
        if (!blk) continue;
        for (const auto& seg : blk->segments) {
            const bool isEndpoint = (seg.startPointId == p.pointId
                                     || seg.endPointId == p.pointId);
            bool isHost = false;
            if (const auto* pt = blk->findPoint(p.pointId))
                isHost = (pt->constraint == cad::param::PointConstraint::Interpolated
                          && pt->hostSegmentId == seg.id);
            if (!isEndpoint && !isHost) continue;
            if (seen.contains(seg.id)) continue;
            seen.insert(seg.id);
            m_endCands.push_back({p.blockId, seg.id, p.pointId, p});
        }
    }

    setState(State::ConfirmEnd);
    if (m_scene)
        m_scene->showToast(QString::fromUtf8(
            "落点存在多个重叠点：点选线段切换落点，空白点击接受默认，Esc 取消"));
    if (m_endAutoPick)
        updatePreview(m_endAutoPick->worldPos);   // lock the rubber band
}

void ToolSmartPen::handleConfirmEndPress(const cad::geo::Vec2& clickPos)
{
    if (!m_endAutoPick) { cancelLine(); return; }

    std::optional<SnapResult> confirmed;
    double zoom = m_scene ? m_scene->currentZoom() : 1.0;
    const auto segSnap = m_snapEngine.findSegmentSnap(
        clickPos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        for (const auto& cand : m_endCands) {
            if (cand.blockId == segSnap->blockId
                && cand.segId == segSnap->segmentId) {
                confirmed = cand.snap;
                break;
            }
        }
    }
    // Candidate segment → that point; blank click = accept the default
    // (active-layer) pick. Esc cancels the whole stroke (see keyPress).
    commitEndSnap(confirmed.value_or(*m_endAutoPick));
}

void ToolSmartPen::commitEndSnap(const SnapResult& endSnap)
{
    if (!m_startSnap) {
        // 用户拍板: 起点自由 + 终点吸附 = 翻转新线 —— 吸附点成为新线
        // 起点, 原起点位置成为自由终点 (终点线变起点, 解开后语义不变:
        // 线段几何/长度/角度完全一致, 仅端点身份互换). 这样终点吸附
        // 也能走统一的“起点连接”路径创建连接, 而非被忽略成自由线.
        const cad::geo::Vec2 origStart = m_startPoint;
        m_startPoint = endSnap.worldPos;
        m_startSnap  = endSnap;
        m_leaderPicker->setRefDirDeg(0.0);
        commitLine(origStart, std::nullopt);
        return;
    }
    commitLine(endSnap.worldPos, endSnap);
}

bool ToolSmartPen::trySwitchStartPoint(const LeaderCandidate& cand, int candIndex)
{
    if (!m_startSnap) return false;
    if (cand.pointId == m_startSnap->pointId) return false;
    // Only pool members (legal, snappable targets) may become the start
    // point — a grayed-layer segment in the leader list stays a pure angle
    // reference and cannot hijack the attachment.
    for (const auto& p : m_startPool) {
        if (p.blockId == cand.blockId && p.pointId == cand.pointId) {
            m_startPoint = p.worldPos;
            m_startSnap  = p;
            // Keep the clicked segment as the construction-angle reference.
            m_leaderPicker->setIndex(candIndex);
            return true;
        }
    }
    return false;
}

void ToolSmartPen::updateEndConfirmHighlight(const cad::geo::Vec2& worldPos)
{
    if (!m_paramDoc || !m_scene) return;
    double zoom = m_scene->currentZoom();

    QUuid hitBlock, hitSeg;
    const auto segSnap = m_snapEngine.findSegmentSnap(
        worldPos, m_paramDoc, zoom, m_scene->style()->hoverRadiusPx());
    if (segSnap) {
        for (const auto& cand : m_endCands) {
            if (cand.blockId == segSnap->blockId
                && cand.segId == segSnap->segmentId) {
                hitBlock = cand.blockId;
                hitSeg = cand.segId;
                break;
            }
        }
    }
    if (hitBlock == m_endHighlightBlockId && hitSeg == m_endHighlightSegId)
        return;
    clearEndConfirmHighlight();
    m_endHighlightBlockId = hitBlock;
    m_endHighlightSegId = hitSeg;
    if (!hitBlock.isNull())
        if (auto* item = m_scene->findBlockItem(hitBlock))
            item->setLeaderHighlight(hitSeg);
}

void ToolSmartPen::clearEndConfirmHighlight()
{
    if (m_scene && !m_endHighlightBlockId.isNull())
        if (auto* item = m_scene->findBlockItem(m_endHighlightBlockId))
            item->setLeaderHighlight(QUuid());
    m_endHighlightBlockId = QUuid();
    m_endHighlightSegId = QUuid();
}

void ToolSmartPen::resetStrokeTargets()
{
    m_startPool.clear();
    m_endCands.clear();
    m_endAutoPick.reset();
    clearEndConfirmHighlight();
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

    double zoom = m_scene ? m_scene->currentZoom() : 1.0;

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

    double zoom = m_scene->currentZoom();

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

void ToolSmartPen::openAuxDialog(const SegmentSnapResult& segSnap, bool forStart)
{
    if (!m_paramDoc || m_auxDialog) return;
    auto* block = m_paramDoc->findBlock(segSnap.blockId);
    auto* seg = block ? block->findSegment(segSnap.segmentId) : nullptr;
    if (!block || !seg) return;

    // 端点延长线 D7 (EXTEND_LINE_DESIGN.md): 尾巴上不允许建立辅助点 —— 辅助点
    // 按本体定义, 落在尾巴上会与"比例点不随延长漂移"冲突。吸附/连接/测量仍可用。
    if (!block->segmentSnapWithinBase(segSnap.segmentId, segSnap.t)) {
        if (m_scene)
            m_scene->showToast(QString::fromUtf8(
                "请在本体范围内建立辅助点（延长尾巴上不支持建点）"));
        return;
    }

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
    auto* dlg = new cad::ui::QuickAuxDialog(pt, block->findPoint(seg->startPointId),
                                   block->findPoint(seg->endPointId), parentWidget);
    // NOTE: no WA_DeleteOnClose — the dialog schedules its own deleteLater()
    // on close (ElaAppBar's default close path would destroy it mid-call).
    // NON-modal on purpose: the user must be able to switch to the variable
    // panel and copy a formula while this dialog stays open.
    m_auxDialog = dlg;
    m_auxDialogForStart = forStart;
    m_auxDialogSegSnap = segSnap;
    showDialogBlockedFeedback();

    QObject::connect(dlg, &QDialog::finished, dlg, [this](int result) {
        auto* dlg = m_auxDialog.data();
        m_auxDialog = nullptr;
        clearDialogBlockedCursor();   // M10: 关闭即恢复画布光标
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
        startStroke(Qt::NoModifier);
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







