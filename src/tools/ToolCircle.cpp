#include "tools/ToolCircle.h"

#include <QBrush>
#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QPainterPath>
#include <QPen>
#include <QUuid>

#include <cmath>

#include "canvas/CanvasScene.h"
#include "geometry/Epsilon.h"
#include "geometry/Units.h"
#include "parametric/ParamDocument.h"
#include "tools/CircleFactory.h"
#include "ui/Theme.h"

namespace cad::tools {

namespace {
/// A bare click (press + release without dragging) must not spawn a
/// degenerate circle; anything below this radius is treated as a miss.
constexpr double kMinCircleRadiusMm = 0.5;
} // namespace

ToolDescriptor ToolCircle::describe()
{
    ToolDescriptor d;
    d.id = ToolType::Circle;
    d.displayName = QString::fromUtf8("画圆(&O)");
    d.iconName = QStringLiteral("circle");
    d.shortcut = QKeySequence(Qt::Key_O);
    // Static default text and the runtime override share one source (default
    // state = waiting for the center), so the two can never drift apart
    // (test_mode_indicator / test_tool_hints lock this).
    d.hintText = modeIndicatorFor(CircleMode::CenterRadius, false)
                     .hint(reinterpret_cast<const char*>(u8"画圆"));
    d.factory = [] { return std::make_unique<ToolCircle>(); };
    return d;
}

ModeIndicator ToolCircle::modeIndicatorFor(CircleMode mode, bool gestureActive)
{
    ModeIndicator mi;
    if (mode == CircleMode::TwoPointDiameter) {
        mi.modeName  = QString::fromUtf8("直径");
        mi.detail    = gestureActive
            ? QString::fromUtf8("点选第二点定直径")
            : QString::fromUtf8("点选两点定直径");
        mi.wAction   = gestureActive
            ? QString::fromUtf8("Esc 取消")
            : QString::fromUtf8("W 切圆心");
        mi.toast     = QString::fromUtf8("已切换：两点直径");
        mi.isDefault = false;
        return mi;
    }

    // 圆心+半径模式（默认态）。
    if (gestureActive) {
        mi.modeName = QString::fromUtf8("拖半径");
        mi.detail   = QString::fromUtf8("拖动确定半径，松开完成");
        mi.wAction  = QString::fromUtf8("Esc 取消");
        mi.toast    = QString::fromUtf8("画圆：拖动半径，松开成圆");
        mi.isDefault = false;
    } else {
        mi.modeName = QString::fromUtf8("圆心");
        mi.detail   = QString::fromUtf8("点选圆心，拖出半径");
        mi.wAction  = QString::fromUtf8("W 切直径");
        mi.toast    = QString::fromUtf8("已切换：圆心+半径");
        mi.isDefault = true;
    }
    return mi;
}

ModeIndicator ToolCircle::modeIndicator() const
{
    return modeIndicatorFor(m_mode, m_session != Session::Idle);
}

void ToolCircle::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)scene;
    (void)paramDoc;
    resetToIdle();
    refreshModeIndicator();
}

void ToolCircle::onDeactivate()
{
    resetToIdle();
    m_managed.clear();
}

void ToolCircle::resetToIdle()
{
    m_session = Session::Idle;
    m_radiusMm = 0.0;
    m_radiusLocked = false;
    m_lockedRadiusMm = 0.0;
    clearPreview();
    reportHintOverride(QString());
    if (m_sessionReported) {
        m_sessionReported = false;
        reportCircleSession(false);
    }
}

void ToolCircle::commitCurrentCircle()
{
    const cad::geo::Vec2 center = m_center;
    const double radiusMm = effectiveRadiusMm();
    resetToIdle();
    refreshModeIndicator();
    commitCircle(center, radiusMm);
}

void ToolCircle::commitCircle(const cad::geo::Vec2& center, double radiusMm)
{
    if (radiusMm < kMinCircleRadiusMm) return;   // 退化半径：静默丢弃
    if (!m_paramDoc) return;

    CircleFactory factory(m_paramDoc, m_undoStack);
    const QUuid blockId = factory.createCircle(center, radiusMm);
    if (blockId.isNull() || !m_scene) return;

    // 落圆后把上下文焦点交给宿主 (一期补充②)：与智能笔复用同一条通道
    // (CanvasScene::lineCreated → MainWindow::onLineCreated → pinCreatedLine)，
    // 条带按 seg->fitKind 自动路由到圆专属条带，Esc 语义与线段创建锁定一致。
    const auto* blk = m_paramDoc->findBlock(blockId);
    if (blk && !blk->segments.empty())
        m_scene->notifyLineCreated(blockId, blk->segments.front().id);
}

void ToolCircle::toggleMode()
{
    m_mode = (m_mode == CircleMode::CenterRadius)
                 ? CircleMode::TwoPointDiameter
                 : CircleMode::CenterRadius;
    // 切模式即取消进行中的手势 (§5.2)：两类手势的锚点语义不同，跨模式续命
    // 会让"刚落的圆心"被当成"直径首点"。
    resetToIdle();
    announceModeChange();
}

void ToolCircle::clearPreview()
{
    m_managed.clear();
    m_previewCircle = nullptr;
    m_previewCenter = nullptr;
    m_previewGuide  = nullptr;
}

void ToolCircle::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    if (event->button() == Qt::RightButton) {
        if (m_session != Session::Idle) {
            resetToIdle();
            refreshModeIndicator();
        } else {
            requestToolSwitch(ToolType::Select);
        }
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    const QPointF sp = event->scenePos();
    cad::geo::Vec2 cursor(sp.x(), sp.y());
    // Snap onto an existing point when the cursor is close enough: a circle is
    // usually struck from a construction point. 圆心与直径端点同一条捕捉规则
    // (§5.3)；圆心仍留 Free 点，不建附着语义。
    if (const auto snap = m_snap.findSnap(cursor, m_paramDoc, m_scene->safeZoom()))
        cursor = snap->worldPos;

    if (m_mode == CircleMode::TwoPointDiameter) {
        if (m_session == Session::Idle) {
            m_anchor  = cursor;          // 首点 A：进入 Armed，等第二点。
            m_session = Session::Armed;
            updatePreview(cursor);
            refreshModeIndicator();
        } else {
            // 第二点 B = 直径另一端 → 在 press 处提交（release 不再处理）。
            const cad::geo::Vec2 a = m_anchor;
            resetToIdle();
            refreshModeIndicator();
            const double diameter = (cursor - a).length();
            commitCircle((a + cursor) * 0.5, diameter * 0.5);   // 半径 < 下限: 双击同点，忽略
        }
        return;
    }

    // 圆心+半径：press 落圆心，拖动预览，release 提交。
    // 会话中再次按下 = 落圆（点击-点击画法；拖动-松开画法照旧可用）。
    if (m_session == Session::Dragging) {
        if (effectiveRadiusMm() >= kMinCircleRadiusMm) commitCurrentCircle();
        return;
    }
    m_anchor       = cursor;
    m_lastCursor   = cursor;
    m_session      = Session::Dragging;
    m_radiusMm     = 0.0;
    m_radiusLocked = false;
    m_lockedRadiusMm = 0.0;
    reportCircleSession(true);   // 条带进绘制态 (§5.5)
    m_sessionReported = true;
    updatePreview(cursor);
    refreshModeIndicator();
}

void ToolCircle::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (m_session == Session::Idle || !m_scene) return;
    const QPointF sp = event->scenePos();
    m_lastCursor = cad::geo::Vec2(sp.x(), sp.y());
    if (m_radiusLocked) return;   // 半径已被条带锁定：光标不再决定半径
    updatePreview(m_lastCursor);
}

void ToolCircle::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    if (!m_paramDoc) return;
    if (event->button() != Qt::LeftButton) return;
    // 直径模式在第二次 press 提交（Armed），只有圆心模式的拖拽在 release 提交。
    if (m_session != Session::Dragging) return;

    // 拖动幅度够大 → 直接落圆；裸点击保留会话，等条带输入半径 (§5.5)。
    if (effectiveRadiusMm() < kMinCircleRadiusMm) return;

    commitCurrentCircle();
}

void ToolCircle::circleRadiusInput(double radiusCm, bool locked)
{
    if (m_mode != CircleMode::CenterRadius || m_session != Session::Dragging) return;
    m_radiusLocked   = locked;
    m_lockedRadiusMm = locked ? cad::geo::Units::cmToMm(std::max(radiusCm, 0.0)) : 0.0;
    updatePreview(m_lastCursor);
}

void ToolCircle::circleCommitted()
{
    if (m_session != Session::Dragging) return;
    if (effectiveRadiusMm() < kMinCircleRadiusMm) return;
    commitCurrentCircle();
}

void ToolCircle::circleCancelled()
{
    if (m_session == Session::Idle) return;
    resetToIdle();
    refreshModeIndicator();
}

void ToolCircle::keyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_W) {
        toggleMode();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_session == Session::Dragging && effectiveRadiusMm() >= kMinCircleRadiusMm) {
            commitCurrentCircle();
            event->accept();
        }
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_session != Session::Idle) {
            resetToIdle();
            refreshModeIndicator();
        } else {
            requestToolSwitch(ToolType::Select);
        }
        event->accept();
    }
}

void ToolCircle::updatePreview(const cad::geo::Vec2& cursorWorld)
{
    // 圆心模式：anchor = 圆心，半径 = |光标 − 圆心|。
    // 直径模式：anchor = 首点 A，光标 = 另端 B → 圆心 = AB 中点，半径 = |AB|/2。
    cad::geo::Vec2 center = m_anchor;
    if (m_mode == CircleMode::TwoPointDiameter) {
        center     = (m_anchor + cursorWorld) * 0.5;
        m_radiusMm = (cursorWorld - m_anchor).length() * 0.5;
    } else if (m_radiusLocked) {
        // 条带已定值：半径由输入说了算，光标只影响不了它 (§5.5)。
        m_radiusMm = m_lockedRadiusMm;
    } else {
        m_radiusMm = (cursorWorld - m_anchor).length();
    }
    m_center = center;

    // Scene coordinates are the world coordinates with Y flipped (Units.h
    // Coord::toScene), so the radius stays isotropic and needs no scaling.
    const QPointF centerSc = cad::geo::Coord::toScene(center);

    if (!m_previewCircle) {
        m_previewCircle = new QGraphicsPathItem();
        QPen pen(cad::ui::Theme::tokens().warning, 1.2, Qt::DashLine);
        pen.setCosmetic(true);
        m_previewCircle->setPen(pen);
        m_previewCircle->setBrush(Qt::NoBrush);
        m_previewCircle->setZValue(100.0);
        m_scene->addItem(m_previewCircle);
        m_managed.own(m_previewCircle, &m_previewCircle);
    }
    QPainterPath path;
    if (m_radiusMm > cad::geo::kGeomEps)
        path.addEllipse(centerSc, m_radiusMm, m_radiusMm);
    m_previewCircle->setPath(path);

    // Center marker: a small fixed-size cross so the struck point is visible
    // even while the radius is still ~0.
    if (!m_previewCenter) {
        m_previewCenter = new QGraphicsPathItem();
        QPen pen(cad::ui::Theme::tokens().warning, 1.0);
        pen.setCosmetic(true);
        m_previewCenter->setPen(pen);
        m_previewCenter->setBrush(Qt::NoBrush);
        m_previewCenter->setZValue(101.0);
        m_scene->addItem(m_previewCenter);
        m_managed.own(m_previewCenter, &m_previewCenter);
    }
    constexpr double kCrossScene = 3.0;
    QPainterPath cross;
    cross.moveTo(centerSc.x() - kCrossScene, centerSc.y());
    cross.lineTo(centerSc.x() + kCrossScene, centerSc.y());
    cross.moveTo(centerSc.x(), centerSc.y() - kCrossScene);
    cross.lineTo(centerSc.x(), centerSc.y() + kCrossScene);
    m_previewCenter->setPath(cross);

    // 直径引导线 (§5.4)：A→B 实线，仅直径模式存在；圆心模式留空路径。
    if (!m_previewGuide) {
        m_previewGuide = new QGraphicsPathItem();
        QPen pen(cad::ui::Theme::tokens().text2, 1.0);
        pen.setCosmetic(true);
        m_previewGuide->setPen(pen);
        m_previewGuide->setBrush(Qt::NoBrush);
        m_previewGuide->setZValue(100.5);
        m_scene->addItem(m_previewGuide);
        m_managed.own(m_previewGuide, &m_previewGuide);
    }
    QPainterPath guide;
    if (m_mode == CircleMode::TwoPointDiameter) {
        guide.moveTo(cad::geo::Coord::toScene(m_anchor));
        guide.lineTo(cad::geo::Coord::toScene(cursorWorld));
    }
    m_previewGuide->setPath(guide);

    if (m_mode == CircleMode::TwoPointDiameter) {
        reportHintOverride(
            QString::fromUtf8("画圆[直径]: 直径 %1 cm，点选第二点完成 (Esc 取消)")
                .arg(cad::geo::Units::formatCm(2.0 * m_radiusMm)));
    } else if (m_session == Session::Dragging) {
        // 绘制会话: 半径既可拖出也可直接输入 (§5.5)。
        reportHintOverride(m_radiusLocked
            ? QString::fromUtf8("画圆: 半径 %1 cm 已锁定 (回车或再点落圆, Esc 取消)")
                  .arg(cad::geo::Units::formatCm(m_lockedRadiusMm))
            : QString::fromUtf8("画圆: 半径 %1 cm，拖动或输入半径 (回车或再点落圆)")
                  .arg(cad::geo::Units::formatCm(m_radiusMm)));
        reportCircleValues(cad::geo::Units::mmToCm(effectiveRadiusMm()), m_radiusLocked);
    } else {
        reportHintOverride(
            QString::fromUtf8("画圆: 半径 %1 cm，松开完成 (Esc 取消)")
                .arg(cad::geo::Units::formatCm(m_radiusMm)));
    }
}

} // namespace cad::tools
