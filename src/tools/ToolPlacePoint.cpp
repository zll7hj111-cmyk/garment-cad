#include "tools/ToolPlacePoint.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QKeyEvent>
#include <QPen>
#include <QBrush>
#include <QUndoStack>
#include <cmath>
#include "geometry/Angle.h"

#include "canvas/CanvasScene.h"
#include "geometry/Units.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Serial.h"
#include "document/commands/EndpointCommands.h"
#include "ui/Theme.h"
#include "geometry/Epsilon.h"

namespace cad::tools {

ToolDescriptor ToolPlacePoint::describe()
{
    ToolDescriptor d;
    d.id = ToolType::PlacePoint;
    d.displayName = QString::fromUtf8("放置点(&P)");
    d.iconName = QStringLiteral("crosshair");
    d.shortcut = QKeySequence(Qt::Key_P);
    d.hintText = QString::fromUtf8("放置点: 点击选择基准端点或线段");
    d.factory = [] { return std::make_unique<ToolPlacePoint>(); };
    return d;
}

void ToolPlacePoint::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)scene;
    (void)paramDoc;
    resetState();
    reportPlacePointSession(true, QString());
}

void ToolPlacePoint::onDeactivate()
{
    clearPreview();
    m_managed.clear();
    reportPlacePointSession(false, QString());
}

void ToolPlacePoint::resetState()
{
    clearPreview();
    m_state = State::PickRef;
    m_blockId = QUuid();
    m_hostSegmentId = QUuid();
    m_refPointId = QUuid();
    m_refPointPos = cad::geo::Vec2();
    m_baseDir = cad::geo::Vec2(1.0, 0.0);
    m_alongDistance = 0.0;
    m_tRatio = 0.0;
    m_currentAngle = 0.0;
    m_currentDist = 0.0;
    m_distLocked = false;
    m_lockedDist = 0.0;
    m_angleLocked = false;
    m_lockedAngle = 0.0;
    reportHintOverride(QString::fromUtf8("放置点: 点击选择基准端点或线段"));
    reportPlacePointSession(true, QString());
}

void ToolPlacePoint::clearPreview()
{
    m_managed.clear();
    m_previewPath = nullptr;
    m_previewDiamond = nullptr;
}

void ToolPlacePoint::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    if (event->button() == Qt::RightButton) {
        if (m_state == State::SetOffset) {
            resetState();
        } else {
            if (m_host) m_host->requestToolSwitch(ToolType::Select);
        }
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 clickPos(sp.x(), sp.y());
    double zoom = m_scene->safeZoom();

    if (m_state == State::PickRef) {
        handlePickRefPress(clickPos, zoom);
    } else if (m_state == State::SetOffset) {
        const bool shiftPressed = (event->modifiers() & Qt::ShiftModifier) != 0;
        handleSetOffsetPress(clickPos, shiftPressed);
    }
}

void ToolPlacePoint::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 cursorWorld(sp.x(), sp.y());
    const bool shiftPressed = (event->modifiers() & Qt::ShiftModifier) != 0;

    if (m_state == State::PickRef) {
        double zoom = m_scene->safeZoom();
        auto snap = m_snap.findSnap(cursorWorld, m_paramDoc, zoom, -1.0, {}, nullptr, true);
        if (snap) {
            reportHintOverride(QString::fromUtf8("放置点: 点击选择基准点 %1").arg(snap->pointName));
        } else {
            auto segSnap = m_snap.findSegmentSnap(cursorWorld, m_paramDoc, zoom, -1.0, nullptr, true);
            if (segSnap) {
                reportHintOverride(QString::fromUtf8("放置点: 点击选择基准线段"));
            } else {
                reportHintOverride(QString::fromUtf8("放置点: 点击选择基准端点或线段"));
            }
        }
    } else if (m_state == State::SetOffset) {
        updatePreview(cursorWorld, shiftPressed);
    }
}

void ToolPlacePoint::mouseRelease(QGraphicsSceneMouseEvent* /*event*/)
{
}

void ToolPlacePoint::keyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        reportPlacePointFocusNextField();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_state == State::SetOffset) {
            commitPlacedPoint();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_state == State::SetOffset) {
            resetState();
        } else {
            if (m_host) m_host->requestToolSwitch(ToolType::Select);
        }
        event->accept();
        return;
    }
}

void ToolPlacePoint::placePointDistInput(double distCm, bool locked)
{
    m_distLocked = locked;
    m_lockedDist = cad::geo::Units::cmToMm(distCm);
    if (m_state == State::SetOffset) {
        updatePreview(m_lastRawCursor, false);
    }
}

void ToolPlacePoint::placePointAngleInput(double angleDeg, bool locked)
{
    m_angleLocked = locked;
    m_lockedAngle = angleDeg;
    if (m_state == State::SetOffset) {
        updatePreview(m_lastRawCursor, false);
    }
}

void ToolPlacePoint::placePointCommitted()
{
    if (m_state == State::SetOffset) {
        commitPlacedPoint();
    }
}

void ToolPlacePoint::handlePickRefPress(const cad::geo::Vec2& clickPos, double zoom)
{
    // 1. Try point snap first (allow points in any layer as reference)
    auto snap = m_snap.findSnap(clickPos, m_paramDoc, zoom, -1.0, {}, nullptr, true);
    if (snap) {
        m_refPointId = snap->pointId;
        m_blockId = snap->blockId;
        m_refPointPos = snap->worldPos;
        m_alongDistance = 0.0;
        m_tRatio = 0.0;

        auto* blk = m_paramDoc->findBlock(m_blockId);
        if (!blk) return;

        QString segLabel;
        for (const auto& seg : blk->segments) {
            if (seg.startPointId == m_refPointId || seg.endPointId == m_refPointId ||
                std::find(seg.auxPointIds.begin(), seg.auxPointIds.end(), m_refPointId) != seg.auxPointIds.end()) {
                m_hostSegmentId = seg.id;
                segLabel = cad::param::Serial::tag(seg.serial);
                if (!seg.name.isEmpty()) segLabel += QStringLiteral("·") + seg.name;

                const auto* sp = blk->findPoint(seg.startPointId);
                const auto* ep = blk->findPoint(seg.endPointId);
                if (sp && ep && sp->resolved && ep->resolved) {
                    const cad::geo::Vec2 spWorld = blk->transform.toWorld(sp->resolvedPos);
                    const cad::geo::Vec2 epWorld = blk->transform.toWorld(ep->resolvedPos);
                    cad::geo::Vec2 v = epWorld - spWorld;
                    if (v.length() > cad::geo::kGeomEps) {
                        m_baseDir = v.normalized();
                    }
                }
                break;
            }
        }
        m_state = State::SetOffset;
        reportHintOverride(QString::fromUtf8("放置点: 移动光标或在底栏输入指定偏置距离与角度，点击确认 (Tab切换，Esc取消)"));
        reportPlacePointSession(true, segLabel);
        return;
    }

    // 2. Try segment snap (click on segment body)
    auto segSnap = m_snap.findSegmentSnap(clickPos, m_paramDoc, zoom, -1.0, nullptr, true);
    if (segSnap) {
        m_blockId = segSnap->blockId;
        m_hostSegmentId = segSnap->segmentId;
        m_refPointPos = segSnap->worldPos;
        m_tRatio = segSnap->t;
        m_alongDistance = 0.0;

        auto* blk = m_paramDoc->findBlock(m_blockId);
        if (!blk) return;
        auto* seg = blk->findSegment(m_hostSegmentId);
        if (!seg) return;

        QString segLabel = cad::param::Serial::tag(seg->serial);
        if (!seg->name.isEmpty()) segLabel += QStringLiteral("·") + seg->name;

        if (seg->isCurve()) {
            m_baseDir = segSnap->tangent.normalized();
            m_refPointId = QUuid();
        } else {
            const auto* sp = blk->findPoint(seg->startPointId);
            const auto* ep = blk->findPoint(seg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                const cad::geo::Vec2 spWorld = blk->transform.toWorld(sp->resolvedPos);
                const cad::geo::Vec2 epWorld = blk->transform.toWorld(ep->resolvedPos);
                cad::geo::Vec2 v = epWorld - spWorld;
                if (v.length() > cad::geo::kGeomEps) {
                    m_baseDir = v.normalized();
                }
                m_refPointId = sp->id;
            }
        }
        m_state = State::SetOffset;
        reportHintOverride(QString::fromUtf8("放置点: 移动光标或在底栏输入指定偏置距离与角度，点击确认 (Tab切换，Esc取消)"));
        reportPlacePointSession(true, segLabel);
        return;
    }

    reportHintOverride(QString::fromUtf8("未选中基准: 请点击画布上的端点或线段作为基准"));
}

void ToolPlacePoint::updatePreview(const cad::geo::Vec2& cursorWorld, bool shiftPressed)
{
    m_lastRawCursor = cursorWorld;

    const cad::geo::Vec2 v = cursorWorld - m_refPointPos;
    double dist = v.length();
    const double cursorAngleRad = std::atan2(v.y, v.x);
    const double baseAngleRad = std::atan2(m_baseDir.y, m_baseDir.x);
    double diffDeg = cad::geo::radToDeg(cursorAngleRad - baseAngleRad);

    while (diffDeg > 180.0) diffDeg -= 360.0;
    while (diffDeg <= -180.0) diffDeg += 360.0;

    if (m_angleLocked) {
        diffDeg = m_lockedAngle;
    } else if (shiftPressed) {
        diffDeg = std::round(diffDeg / 45.0) * 45.0;
    }

    if (m_distLocked) {
        dist = m_lockedDist;
    } else if (m_angleLocked) {
        const double rayRad = baseAngleRad + cad::geo::degToRad(diffDeg);
        const cad::geo::Vec2 rayDir(std::cos(rayRad), std::sin(rayRad));
        dist = std::max(0.0, v.dot(rayDir));
    }

    m_currentAngle = diffDeg;
    m_currentDist = dist;

    // Report real-time values to context strip
    reportPlacePointSessionUpdate(cad::geo::Units::mmToCm(m_currentDist), m_currentAngle, m_distLocked, m_angleLocked);

    // Compute preview point and corner point for orthogonal elbow line
    const double rad = cad::geo::degToRad(diffDeg);
    const cad::geo::Vec2 u = m_baseDir.normalized();
    const cad::geo::Vec2 n(-u.y, u.x);
    const double dParallel = dist * std::cos(rad);
    const double dPerp = dist * std::sin(rad);

    const cad::geo::Vec2 cornerWorld = m_refPointPos + u * dParallel;
    const cad::geo::Vec2 previewWorld = cornerWorld + n * dPerp;

    const QPointF startSc = cad::geo::Coord::toScene(m_refPointPos);
    const QPointF cornerSc = cad::geo::Coord::toScene(cornerWorld);
    const QPointF endSc = cad::geo::Coord::toScene(previewWorld);

    if (!m_previewPath) {
        m_previewPath = new QGraphicsPathItem();
        QPen pen(cad::ui::Theme::tokens().warning, 1.2, Qt::DashLine);
        pen.setCosmetic(true);
        m_previewPath->setPen(pen);
        m_previewPath->setBrush(Qt::NoBrush);
        m_previewPath->setZValue(100.0);
        m_scene->addItem(m_previewPath);
        m_managed.own(m_previewPath, &m_previewPath);
    }

    QPainterPath path;
    path.moveTo(startSc);
    path.lineTo(cornerSc);
    path.lineTo(endSc);

    const double lenParSc = std::hypot(cornerSc.x() - startSc.x(), cornerSc.y() - startSc.y());
    const double lenPerpSc = std::hypot(endSc.x() - cornerSc.x(), endSc.y() - cornerSc.y());
    if (lenParSc > 8.0 && lenPerpSc > 8.0) {
        const double s = std::min(6.0, std::min(lenParSc, lenPerpSc) * 0.25);
        const QPointF uBack = (startSc - cornerSc) / lenParSc;
        const QPointF uPerp = (endSc - cornerSc) / lenPerpSc;
        const QPointF k1 = cornerSc + uBack * s;
        const QPointF k2 = cornerSc + uBack * s + uPerp * s;
        const QPointF k3 = cornerSc + uPerp * s;
        path.moveTo(k1);
        path.lineTo(k2);
        path.lineTo(k3);
    }
    m_previewPath->setPath(path);

    if (!m_previewDiamond) {
        m_previewDiamond = new QGraphicsPolygonItem();
        m_previewDiamond->setPen(QPen(Qt::NoPen));
        m_previewDiamond->setBrush(QBrush(cad::ui::Theme::tokens().warning));
        m_previewDiamond->setZValue(101.0);
        m_scene->addItem(m_previewDiamond);
        m_managed.own(m_previewDiamond, &m_previewDiamond);
    }

    constexpr double r = 4.5;
    QPolygonF diamond;
    diamond << QPointF(endSc.x(), endSc.y() - r)
            << QPointF(endSc.x() + r, endSc.y())
            << QPointF(endSc.x(), endSc.y() + r)
            << QPointF(endSc.x() - r, endSc.y());
    m_previewDiamond->setPolygon(diamond);

    QString lockInfo;
    if (m_distLocked && m_angleLocked) lockInfo = QString::fromUtf8(" [距离+角度已锁]");
    else if (m_distLocked) lockInfo = QString::fromUtf8(" [距离已锁]");
    else if (m_angleLocked) lockInfo = QString::fromUtf8(" [角度已锁]");

    reportHintOverride(QString::fromUtf8("放置点: 偏置距离 %1 cm | 偏置角度 %2°%3 (Tab切换输入框，点击或Enter确认，Esc取消)")
                           .arg(cad::geo::Units::formatCm(dist))
                           .arg(cad::geo::Units::formatDegValue(diffDeg))
                           .arg(lockInfo));
}

void ToolPlacePoint::commitPlacedPoint()
{
    if (m_blockId.isNull() || m_hostSegmentId.isNull()) {
        resetState();
        return;
    }

    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::Interpolated;
    pt.hostSegmentId = m_hostSegmentId;
    pt.interpRefPointId = m_refPointId;
    pt.interpPercent = m_tRatio;
    pt.interpConstant = 0.0;
    pt.interpOffsetAngle = m_currentAngle;
    pt.interpOffsetDist = m_currentDist;
    pt.isAuxiliary = true;
    pt.isPlaced = true;
    pt.serial = m_paramDoc->newPointSerial();

    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::AddAuxPointCommand(
            m_paramDoc, m_blockId, m_hostSegmentId, pt));
    } else {
        auto* blk = m_paramDoc->findBlock(m_blockId);
        if (blk) {
            blk->addPoint(pt);
            if (auto* seg = blk->findSegment(m_hostSegmentId)) {
                seg->auxPointIds.push_back(pt.id);
            }
            m_paramDoc->resolveAll();
        }
    }

    resetState();
}

void ToolPlacePoint::handleSetOffsetPress(const cad::geo::Vec2& cursorWorld, bool shiftPressed)
{
    updatePreview(cursorWorld, shiftPressed);
    commitPlacedPoint();
}

} // namespace cad::tools
