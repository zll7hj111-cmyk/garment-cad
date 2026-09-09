/// @file test_circle_detach.cpp
/// D14 acceptance: 解除圆约束 (`FitKind::Circle` → `None`)
/// (docs/design/CIRCLE_TOOL_DESIGN.md §4.4 / §16 / §17, 测试计划 6d).
///
/// 判据:
///   ① 解除瞬间**形状冻结**: 逐点采样前后一致 (差异 < 1e-9 mm);
///   ② 锚点/端点全部转 `Free` 且 `freePos == resolvedPos`; 圆心点不动不删;
///   ③ 拟合切向与 `autoTangent` 保持原值 —— 这正是形状不跳变的原因
///      (设计稿原文的 `autoTangent = true` 会让曲线在解除瞬间重解切向跳变);
///   ④ 解除后拖锚点 = 改形 (曲线经过被拖位置), 即普通曲线可自由编辑;
///   ⑤ undo 逐字段还原 (fitKind / 约束 / 距离 / 角度 / 圆心引用 / 形状)。
///
/// Run: test_circle_detach

#include <QtTest>
#include <QUndoStack>
#include <QUuid>
#include <cmath>
#include <vector>

#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/ParamDocument.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "document/commands/CircleCommands.h"
#include "tools/CircleFactory.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

struct CircleIds {
    QUuid blockId;
    QUuid segId;
    QUuid centerId;
    QUuid startId;
    QUuid endId;
};

CircleIds makeCircle(ParamDocument& doc, double radiusMm,
                     double startAngleDeg = 0.0,
                     const Vec2& center = Vec2(0.0, 0.0))
{
    CircleIds ids;
    cad::tools::CircleFactory factory(&doc, doc.undoStack());
    ids.blockId = factory.createCircle(center, radiusMm, startAngleDeg);
    if (Block* b = doc.findBlock(ids.blockId); b && !b->segments.empty()) {
        ids.segId = b->segments.front().id;
        ids.startId = b->segments.front().startPointId;
        ids.endId = b->segments.front().endPointId;
        if (const ParamPoint* sp = b->findPoint(ids.startId))
            ids.centerId = sp->refPointId;
    }
    doc.resolveAll();
    return ids;
}

/// 采样段的全部 Bézier 跨 (每跨 kSamplesPerSpan 个参数点)。
constexpr int kSamplesPerSpan = 16;

std::vector<Vec2> sampleCurve(const Block& b, const QUuid& segId)
{
    std::vector<Vec2> out;
    const CurveSpanEntry* entry = b.curveSpanEntry(segId);
    if (!entry || entry->spans.empty()) return out;
    const int n = static_cast<int>(entry->spans.size());
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k <= kSamplesPerSpan; ++k) {
            const double t = double(i) + double(k) / double(kSamplesPerSpan);
            out.push_back(cad::geo::evalCurve(entry->spans, t));
        }
    }
    return out;
}

double maxDelta(const std::vector<Vec2>& a, const std::vector<Vec2>& b)
{
    if (a.size() != b.size() || a.empty()) return 1e9;
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, a[i].distanceTo(b[i]));
    return worst;
}

/// 采样点到给定点的最近距离 (判「曲线是否经过某处」)。
double minDistanceTo(const std::vector<Vec2>& pts, const Vec2& target)
{
    double best = 1e9;
    for (const Vec2& p : pts)
        best = std::min(best, p.distanceTo(target));
    return best;
}

} // namespace

class TestCircleDetach : public QObject
{
    Q_OBJECT

private slots:
    void detachFreezesShapeAndConstraints();
    void detachKeepsTangentsAndMakesAnchorsDraggable();
    void undoRestoresCircleExactly();
    void detachOnPlainCurveIsNoOp();
};

void TestCircleDetach::detachFreezesShapeAndConstraints()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 30.0, 25.0);
    Block* b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(seg);
    QCOMPARE(seg->fitKind, FitKind::Circle);

    const std::vector<Vec2> before = sampleCurve(*b, ids.segId);
    QCOMPARE(before.size(), std::size_t(4 * (kSamplesPerSpan + 1)));   // 整圆 = 4 跨

    doc.undoStack()->push(new cad::cmd::DetachCircleCommand(&doc, ids.blockId, ids.segId));

    b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    seg = b->findSegment(ids.segId);
    QVERIFY(seg);
    QCOMPARE(seg->fitKind, FitKind::None);
    QCOMPARE(seg->type, SegmentType::Bezier);      // 仍是曲线 (锚点一个不少)
    QCOMPARE(seg->passPointIds.size(), std::size_t(3));

    // 端点 + 全部分段锚 → Free, 且 freePos 就是冻结时的解算位置。
    for (const QUuid& pid : {ids.startId, ids.endId}) {
        const ParamPoint* pt = b->findPoint(pid);
        QVERIFY(pt);
        QCOMPARE(pt->constraint, PointConstraint::Free);
        QVERIFY2(pt->freePos.distanceTo(pt->resolvedPos) < 1e-9,
                 qPrintable(QString::number(pt->freePos.distanceTo(pt->resolvedPos))));
        QVERIFY(pt->refPointId.isNull());          // Polar 权威已清
        QVERIFY(pt->distanceFormula.isEmpty());
    }
    for (const QUuid& pid : seg->passPointIds) {
        const ParamPoint* pt = b->findPoint(pid);
        QVERIFY(pt);
        QCOMPARE(pt->constraint, PointConstraint::Free);
        QVERIFY(pt->freePos.distanceTo(pt->resolvedPos) < 1e-9);
    }

    // 圆心点保留 (不删) 且位置不变 —— 块内可能有其它段引用它。
    const ParamPoint* center = b->findPoint(ids.centerId);
    QVERIFY(center);
    QCOMPARE(center->constraint, PointConstraint::Free);
    QVERIFY(center->resolvedPos.distanceTo(Vec2(0.0, 0.0)) < 1e-9);

    // ① 形状冻结。
    const std::vector<Vec2> after = sampleCurve(*b, ids.segId);
    QCOMPARE(after.size(), before.size());
    const double delta = maxDelta(before, after);
    QVERIFY2(delta < 1e-9, qPrintable(QStringLiteral("shape delta = %1 mm").arg(delta)));
}

void TestCircleDetach::detachKeepsTangentsAndMakesAnchorsDraggable()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 40.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    const QUuid anchorId = seg->passPointIds.front();
    const ParamPoint* anchorBefore = b->findPoint(anchorId);
    QVERIFY(anchorBefore);
    // 圆拟合已把拟合切向写进点 (autoTangent = false) —— 形状冻结的前提。
    QVERIFY(!anchorBefore->autoTangent);
    const Vec2 tangentOut = anchorBefore->tangentOut;
    QVERIFY(tangentOut.length() > 0.0);

    doc.undoStack()->push(new cad::cmd::DetachCircleCommand(&doc, ids.blockId, ids.segId));

    b = doc.findBlock(ids.blockId);
    seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);
    const ParamPoint* anchor = b->findPoint(anchorId);
    QVERIFY(anchor);
    QVERIFY(!anchor->autoTangent);                       // ③ 不回到 Hobby 自动切向
    QVERIFY(anchor->tangentOut.distanceTo(tangentOut) < 1e-12);

    // ④ 自由拖动: 改 freePos → 曲线跟着走, 并经过新位置。
    const Vec2 frozenPos = anchor->resolvedPos;
    const std::vector<Vec2> before = sampleCurve(*b, ids.segId);
    const Vec2 target = frozenPos + Vec2(6.0, 0.0);
    b->findPoint(anchorId)->freePos = target;
    doc.touchAndResolve(ids.blockId);

    b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    const ParamPoint* moved = b->findPoint(anchorId);
    QVERIFY(moved);
    QVERIFY2(moved->resolvedPos.distanceTo(target) < 1e-9,
             qPrintable(QString::number(moved->resolvedPos.distanceTo(target))));

    const std::vector<Vec2> after = sampleCurve(*b, ids.segId);
    QVERIFY(maxDelta(before, after) > 1.0);              // 形状真的变了
    QVERIFY2(minDistanceTo(after, target) < 1e-6,
             qPrintable(QString::number(minDistanceTo(after, target))));
}

void TestCircleDetach::undoRestoresCircleExactly()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 25.0, 90.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    const ParamPoint startBefore = *b->findPoint(ids.startId);
    const ParamPoint endBefore   = *b->findPoint(ids.endId);
    const std::vector<Vec2> before = sampleCurve(*b, ids.segId);

    doc.undoStack()->push(new cad::cmd::DetachCircleCommand(&doc, ids.blockId, ids.segId));
    QCOMPARE(doc.findBlock(ids.blockId)->findSegment(ids.segId)->fitKind, FitKind::None);

    doc.undoStack()->undo();
    b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    seg = b->findSegment(ids.segId);
    QVERIFY(seg);
    QCOMPARE(seg->fitKind, FitKind::Circle);

    const ParamPoint* sp = b->findPoint(ids.startId);
    const ParamPoint* ep = b->findPoint(ids.endId);
    QVERIFY(sp && ep);
    QCOMPARE(sp->constraint, PointConstraint::Polar);
    QCOMPARE(sp->refPointId, ids.centerId);
    QVERIFY(std::abs(sp->distance - startBefore.distance) < 1e-12);
    QVERIFY(std::abs(sp->angle - startBefore.angle) < 1e-12);
    QVERIFY(std::abs(ep->angle - endBefore.angle) < 1e-12);   // 整圆 90+360 不归一化
    QVERIFY(std::abs(ep->angle - 450.0) < 1e-12);
    for (const QUuid& pid : seg->passPointIds)
        QCOMPARE(b->findPoint(pid)->constraint, PointConstraint::CurveAnchor);

    const double delta = maxDelta(before, sampleCurve(*b, ids.segId));
    QVERIFY2(delta < 1e-9, qPrintable(QStringLiteral("undo shape delta = %1 mm").arg(delta)));

    // redo 仍可再解除一次。
    doc.undoStack()->redo();
    QCOMPARE(doc.findBlock(ids.blockId)->findSegment(ids.segId)->fitKind, FitKind::None);
}

void TestCircleDetach::detachOnPlainCurveIsNoOp()
{
    ParamDocument doc;
    const CircleIds ids = makeCircle(doc, 20.0);
    Block* b = doc.findBlock(ids.blockId);
    Segment* seg = b->findSegment(ids.segId);
    QVERIFY(b && seg);

    // 先解除一次, 再对同一段重复执行: 第二次必须是安全的 no-op。
    doc.undoStack()->push(new cad::cmd::DetachCircleCommand(&doc, ids.blockId, ids.segId));
    const std::vector<Vec2> after = sampleCurve(*doc.findBlock(ids.blockId), ids.segId);
    doc.undoStack()->push(new cad::cmd::DetachCircleCommand(&doc, ids.blockId, ids.segId));

    b = doc.findBlock(ids.blockId);
    QVERIFY(b);
    QCOMPARE(b->findSegment(ids.segId)->fitKind, FitKind::None);
    QVERIFY(maxDelta(after, sampleCurve(*b, ids.segId)) < 1e-9);
}

QTEST_GUILESS_MAIN(TestCircleDetach)
#include "test_circle_detach.moc"
