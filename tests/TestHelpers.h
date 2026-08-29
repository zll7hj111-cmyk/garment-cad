#pragma once

/// @file TestHelpers.h
/// Shared test utilities for parametric CAD unit tests.
/// Header-only: no CMake source-list changes needed.

#include <QtTest>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QWidget>

#include <QUuid>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Attachment.h"
#include "geometry/Vec2.h"
#include "tools/Tool.h"

namespace cad::test {

using cad::param::ParamDocument;
using cad::param::Block;
using cad::param::ParamPoint;
using cad::param::Segment;
using cad::param::Attachment;
using cad::param::PointConstraint;
using cad::geo::Vec2;

/// Handles of a simple 2-point / 1-segment line block.
struct LineSetup {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

/// Create a horizontal line block: Free start at local (0,0), Polar end.
/// @param doc              Target document (block is added immediately).
/// @param lenMm            Segment length in mm.
/// @param origin           Block transform origin (default: zero).
/// @param pointDistFormula Optional distance formula on the END POINT (cm domain).
/// @param segLenFormula    Optional length formula on the SEGMENT (cm domain).
inline LineSetup makeLine(ParamDocument& doc, double lenMm,
                          Vec2 origin = Vec2::zero(),
                          const QString& pointDistFormula = {},
                          const QString& segLenFormula = {})
{
    Block block;
    block.transform.origin = origin;
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = lenMm;
    p2.angle = 0.0;
    p2.distanceFormula = pointDistFormula;
    QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    seg.lengthFormula = segLenFormula;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId, segId};
}

/// Create a bridge block pinned between two host points.
inline LineSetup makeBridge(ParamDocument& doc,
                            const QUuid& hostA, const QUuid& hostAPoint,
                            const QUuid& hostB, const QUuid& hostBPoint)
{
    const Vec2 startWorld = doc.findBlock(hostA)->worldPos(hostAPoint);
    const Vec2 endWorld   = doc.findBlock(hostB)->worldPos(hostBPoint);

    Block block;
    block.isBridge = true;
    block.transform.origin = startWorld;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;

    const Vec2 delta = endWorld - startWorld;
    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = delta.length();
    p2.angle = std::atan2(delta.y, delta.x) * 180.0 / M_PI;
    QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    doc.addBlock(std::move(block));

    Attachment pinStart;
    pinStart.isPin = true;
    pinStart.fromBlockId = blockId;
    pinStart.fromPointId = startId;
    pinStart.toBlockId = hostA;
    pinStart.toPointId = hostAPoint;

    Attachment pinEnd;
    pinEnd.isPin = true;
    pinEnd.fromBlockId = blockId;
    pinEnd.fromPointId = endId;
    pinEnd.toBlockId = hostB;
    pinEnd.toPointId = hostBPoint;

    doc.addAttachment(std::move(pinStart));
    doc.addAttachment(std::move(pinEnd));
    return {blockId, startId, endId, segId};
}

/// Resolved length (mm) of the first segment of a block in the document.
/// Returns -1.0 if the block/segment/points are missing or unresolved.
inline double segmentLength(const ParamDocument& doc, const QUuid& blockId)
{
    const Block* b = doc.findBlock(blockId);
    if (!b || b->segments.empty()) return -1.0;
    const Segment& seg = b->segments.front();
    const ParamPoint* sp = b->findPoint(seg.startPointId);
    const ParamPoint* ep = b->findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return -1.0;
    return sp->resolvedPos.distanceTo(ep->resolvedPos);
}

/// Stable id of the display layer at @p row (test convenience: layers are
/// id-referenced in the model, but tests think in display rows).
inline QUuid layerIdAt(const ParamDocument& doc, int row)
{
    const auto& ls = doc.layers();
    return (row >= 0 && row < static_cast<int>(ls.size()))
        ? ls[static_cast<size_t>(row)].id : QUuid();
}

// ── Conditional waits (P2-3) ───────────────────────────────────────────────
// GUI tests used to sleep a FIXED number of milliseconds (QTest::qWait(30))
// and then assert on the result. That is a race: on a loaded machine the
// widget has not finished painting when the assertion runs, so a perfectly
// healthy feature fails — the "GUI 时序抖动" reds that made ctest unreadable
// (a different set of tests flaked on every run). These helpers wait for a
// CONDITION with a generous deadline instead: fast when the machine is idle,
// stable when it is busy.

/// Pump the event loop until @p pred returns true. Returns false on timeout.
/// @p timeoutMs is a safety net, not an expected duration.
template <typename Predicate>
inline bool waitUntil(Predicate pred, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (pred()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QTest::qSleep(1);
    }
    return pred();      // last chance: the condition may have just flipped
}

/// Drain the event loop for a bounded number of rounds.
///
/// Use ONLY for the "assert that NOTHING happened" case — e.g. editing line B
/// must not disturb an attachment on line A. There is no state to wait FOR, so
/// waitUntil() has no predicate to take; settle() simply gives the app every
/// chance to react before the assertion, without betting on a wall-clock
/// duration. For "assert that something DID happen" always use waitUntil():
/// settle() cannot make a slow machine catch up, it only stops a fast one from
/// over-sleeping.
inline void settle(int rounds = 5, int perRoundMs = 5)
{
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents(QEventLoop::AllEvents, perRoundMs);
    }
}

/// Grab a widget AFTER it has actually painted: poll until two consecutive
/// grabs are byte-identical (bounded by @p timeoutMs). Replaces
/// `QTest::qWait(30); view.grab()`, which produced half-painted or blank
/// grabs under load and broke every pixel-count assertion downstream.
inline QImage grabStable(QWidget& widget, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    QImage prev = widget.grab().toImage();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QTest::qSleep(1);
        const QImage cur = widget.grab().toImage();
        if (cur == prev) return cur;
        prev = cur;
    }
    return prev;        // timeout: return the last frame rather than failing
}

/// 记录型 ToolHost 桩 (三期只读悬停测试): 记录工具最后一次上报的悬停/锁定目标。
/// 直驱工具 (activate 注入 ToolContext.host) 时用于断言"扫过即看"的悬停上报。
struct RecordingToolHost : cad::tools::ToolHost {
    QUuid hoverBlock;
    QUuid hoverSeg;
    QUuid pinnedBlock;
    QUuid pinnedSeg;
    int hoverCount = 0;
    int pinnedCount = 0;

    void requestToolSwitch(cad::tools::ToolType) override {}
    void setHoverTarget(const QUuid& blockId, const QUuid& segmentId) override
    {
        hoverBlock = blockId;
        hoverSeg = segmentId;
        ++hoverCount;
    }
    void setPinnedTarget(const QUuid& blockId, const QUuid& segmentId) override
    {
        pinnedBlock = blockId;
        pinnedSeg = segmentId;
        ++pinnedCount;
    }
};

/// 向工具投递一次画布鼠标移动 (scene 坐标, mm)。悬停上报由工具在 mouseMove
/// 内同步调用, 返回后断言 RecordingToolHost 即可。
inline void sendToolMouseMove(cad::tools::Tool& tool, const QPointF& scenePos)
{
    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(scenePos);
    move.setButton(Qt::NoButton);
    move.setButtons(Qt::NoButton);
    tool.mouseMove(&move);
}

} // namespace cad::test
