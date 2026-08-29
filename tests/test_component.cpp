#include <QtTest>
#include <QList>
#include <QUuid>
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QUndoStack>

#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Component.h"
#include "parametric/Attachment.h"
#include "document/commands/ComponentCommands.h"
#include "document/DocumentSerializer.h"
#include "geometry/Vec2.h"

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "tools/ToolManager.h"
#include "tools/ToolSelect.h"
#include "TestHelpers.h"

using namespace cad::param;

namespace {

/// A simple single-segment block: two Free points (start = local origin).
Block makeLine(const cad::geo::Vec2& origin, const cad::geo::Vec2& endLocal)
{
    Block b;
    b.transform.origin = origin;
    b.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = cad::geo::Vec2::zero();
    const QUuid p1Id = p1.id;
    b.addPoint(std::move(p1));

    ParamPoint p2;
    p2.constraint = PointConstraint::Free;
    p2.freePos = endLocal;
    const QUuid p2Id = p2.id;
    b.addPoint(std::move(p2));

    Segment s;
    s.startPointId = p1Id;
    s.endPointId = p2Id;
    b.addSegment(std::move(s));
    return b;
}

} // namespace

class TestComponent : public QObject
{
    Q_OBJECT

private slots:
    void createComponent();
    void wholeMovePreservesInternal();
    void componentLevelConnectFollowsLeader();
    void componentOneExternalLinkOnly();
    void makeUndoDissolves();
    void dissolveKeepsSegments();
    void deleteRemovesMembers();
    void serializeRoundTrip();
    void boundingBoxValid();
    void junctionComponentConnectGesture();
    void componentConnectOverlapSwitchTarget();
    void settleNoOpLeavesTransformsUntouched();
    void dragComponentLeaderCurveFollowStable();
};

// 回归 (用户报告 2026-09: "曲线跟随拖动的时候晃动"):
// 组件内曲线块的 CurveAnchor (曲线点) 跟随一个外部目标点; 拖动组件的外部
// leader 时, 组件沉降每帧把成员块 (连同曲线) 再挪一次 — 而曲线点跟随后处理
// 在组件沉降循环之前执行 → 帧末锚点世界位置偏离目标一个"本帧组件位移",
// 鼠标停顿帧位移为 0 → 锚点每帧在"偏离/回正"间跳变 = 可见晃动.
// 不变式: 每帧 resolveForDrag 结束后锚点世界位置必须精确落在目标 + offset.
void TestComponent::dragComponentLeaderCurveFollowStable()
{
    ParamDocument doc;

    // A = 曲线块: Polar 端点 + Bezier 段, CurveAnchor 作为 pass point (0.5/20).
    Block a;
    a.transform.origin = cad::geo::Vec2(0, 0);
    a.transform.rotation = 0.0;
    ParamPoint a1; a1.constraint = PointConstraint::Free; a1.freePos = cad::geo::Vec2::zero();
    const QUuid a1Id = a1.id;
    ParamPoint a2; a2.constraint = PointConstraint::Polar; a2.refPointId = a1Id;
    a2.distance = 100.0; a2.angle = 0.0;
    const QUuid a2Id = a2.id;
    a.addPoint(std::move(a1));
    a.addPoint(std::move(a2));
    Segment aSeg; aSeg.startPointId = a1Id; aSeg.endPointId = a2Id;
    aSeg.type = SegmentType::Bezier;
    ParamPoint pp; pp.constraint = PointConstraint::CurveAnchor;
    pp.hostSegmentId = aSeg.id; pp.interpPercent = 0.5; pp.interpOffsetDist = 20.0;
    pp.autoTangent = true;
    const QUuid ppId = pp.id;
    a.addPoint(std::move(pp));
    aSeg.passPointIds = {ppId};
    const QUuid aSegId = aSeg.id;
    a.addSegment(std::move(aSeg));
    const QUuid aId = doc.addBlock(std::move(a));

    const QUuid bId = doc.addBlock(makeLine(cad::geo::Vec2(100, 0), cad::geo::Vec2(80, 0)));
    const QUuid xId = doc.addBlock(makeLine(cad::geo::Vec2(100, -80), cad::geo::Vec2(60, 0)));
    const QUuid yId = doc.addBlock(makeLine(cad::geo::Vec2(300, -100), cad::geo::Vec2(40, 0)));
    // 真实拖动发生在工作层 (默认 Block::layer=0=辅助层, 见 test_commands.cpp:488):
    // 全上工作层后拖动求解才走 working 相位, post-pass 才会处理曲线锚点.
    for (const auto& b : doc.blocks())
        if (auto* mb = doc.blockById(b.id)) mb->layer = cad::test::layerIdAt(doc, 1);
    doc.resolveAll();

    // 内部连接 B→A (延伸): 组件打包的内部参数化关系.
    Attachment internal;
    internal.fromBlockId = bId;
    internal.fromPointId = doc.findBlock(bId)->segments.front().startPointId;
    internal.toBlockId = aId;
    internal.toPointId = a2Id;
    internal.toSegmentId = aSegId;
    internal.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(internal));

    // 组建 + 组件级连接 → X (暴露端点 = B.end).
    cad::cmd::MakeComponentCommand(&doc, {aId, bId}, QStringLiteral("C")).redo();
    const QUuid compId = doc.components().front().id;
    const QUuid bEnd = doc.findBlock(bId)->segments.front().endPointId;
    const QUuid xEnd = doc.findBlock(xId)->segments.front().endPointId;
    Attachment compAtt;
    compAtt.fromComponentId = compId;
    compAtt.fromPointId = bEnd;
    compAtt.toBlockId = xId;
    compAtt.toPointId = xEnd;
    compAtt.toSegmentId = doc.findBlock(xId)->segments.front().id;
    compAtt.followerAngle = 0.0;
    QVERIFY(doc.addAttachment(compAtt));
    doc.resolveAll();

    // 曲线点跟随 → Y 的起点 (外部静态目标, 与组件驱动 X 不同源).
    const QUuid yStart = doc.findBlock(yId)->segments.front().startPointId;
    auto* blkA = doc.blockById(aId);
    auto* anchor = blkA->findPoint(ppId);
    QVERIFY(anchor);
    anchor->followBlockId = yId;
    anchor->followPointId = yStart;
    anchor->followOffset = cad::geo::Vec2::zero();
    // 激活跟随: 一次拖动求解让 post-pass 把锚点移上目标.
    doc.invalidateLayer(cad::test::layerIdAt(doc, 1));
    doc.resolveForDrag({yId});
    QVERIFY2(doc.findBlock(aId)->worldPos(ppId).distanceTo(
        doc.findBlock(yId)->worldPos(yStart)) < 1e-6,
             qPrintable(QStringLiteral("activation: anchor=(%1,%2) target=(%3,%4) dist=%5 layer=%6")
                            .arg(doc.findBlock(aId)->worldPos(ppId).x)
                            .arg(doc.findBlock(aId)->worldPos(ppId).y)
                            .arg(doc.findBlock(yId)->worldPos(yStart).x)
                            .arg(doc.findBlock(yId)->worldPos(yStart).y)
                            .arg(doc.findBlock(aId)->worldPos(ppId).distanceTo(
                                doc.findBlock(yId)->worldPos(yStart)))
                            .arg(doc.findBlock(aId)->layer.toString())));

    // 逐帧拖动组件 leader X: 每帧结束锚点必须仍精确落在目标上, 且组件跟随.
    for (int i = 1; i <= 5; ++i) {
        doc.findBlock(xId)->transform.origin += cad::geo::Vec2(5.0, 3.0);
        doc.invalidateLayer(cad::test::layerIdAt(doc, 1));
        doc.resolveForDrag({xId});

        // 组件在拖动帧内跟随 (暴露端点落 X.end) — 收窄求解不跳过本组件.
        QVERIFY2(doc.findBlock(bId)->worldPos(bEnd).distanceTo(
            doc.findBlock(xId)->worldPos(xEnd)) < 1e-6,
                 qPrintable(QStringLiteral("frame %1: bEnd off xEnd by %2 (bEnd=(%3,%4) xEnd=(%5,%6))")
                                .arg(i)
                                .arg(doc.findBlock(bId)->worldPos(bEnd).distanceTo(
                                    doc.findBlock(xId)->worldPos(xEnd)))
                                .arg(doc.findBlock(bId)->worldPos(bEnd).x)
                                .arg(doc.findBlock(bId)->worldPos(bEnd).y)
                                .arg(doc.findBlock(xId)->worldPos(xEnd).x)
                                .arg(doc.findBlock(xId)->worldPos(xEnd).y)));
        // 帧末: 曲线点不得被组件沉降拖离目标 (修复前偏离 = 本帧组件位移).
        QVERIFY2(doc.findBlock(aId)->worldPos(ppId).distanceTo(
            doc.findBlock(yId)->worldPos(yStart)) < 1e-6,
                 qPrintable(QStringLiteral("frame %1: anchor off target by %2")
                                .arg(i)
                                .arg(doc.findBlock(aId)->worldPos(ppId).distanceTo(
                                    doc.findBlock(yId)->worldPos(yStart)))));
    }

    // 鼠标停顿帧: 无位移再求解一次, 锚点仍然在目标上.
    doc.invalidateLayer(cad::test::layerIdAt(doc, 1));
    doc.resolveForDrag({xId});
    QVERIFY(doc.findBlock(aId)->worldPos(ppId).distanceTo(
        doc.findBlock(yId)->worldPos(yStart)) < 1e-6);
}

void TestComponent::createComponent()
{
    ParamDocument doc;
    const QUuid b1 = doc.addBlock(makeLine(cad::geo::Vec2(0, 0), cad::geo::Vec2(50, 0)));
    const QUuid b2 = doc.addBlock(makeLine(cad::geo::Vec2(60, 10), cad::geo::Vec2(0, 30)));
    const QUuid b3 = doc.addBlock(makeLine(cad::geo::Vec2(100, -20), cad::geo::Vec2(40, 40)));

    cad::cmd::MakeComponentCommand cmd(&doc, {b1, b2, b3}, QStringLiteral("组件A"));
    cmd.redo();

    QCOMPARE(static_cast<int>(doc.components().size()), 1);
    const Component& c = doc.components().front();
    QCOMPARE(c.name, QStringLiteral("组件A"));
    QCOMPARE(static_cast<int>(c.memberBlockIds.size()), 3);
    QVERIFY(c.isMember(b1) && c.isMember(b2) && c.isMember(b3));
    // 自动暴露: 无外部连接前不记录暴露端点.
    QVERIFY(c.exposedPointId.isNull());
    QVERIFY(doc.componentOfBlock(b1) != nullptr);
    QVERIFY(doc.componentOfBlock(b2) != nullptr);
    QVERIFY(doc.componentOfBlock(b3) != nullptr);
}

// 整体拖动: 所有成员同一平移 → 内部 attachment 相对几何保持 (B.start 仍吸
// 在 A.end) — 组件只是打包, 内部参数化关系继续生效.
void TestComponent::wholeMovePreservesInternal()
{
    ParamDocument doc;
    const QUuid a = doc.addBlock(makeLine(cad::geo::Vec2(0, 0), cad::geo::Vec2(100, 0)));
    const QUuid b = doc.addBlock(makeLine(cad::geo::Vec2(100, 0), cad::geo::Vec2(80, 0)));
    doc.resolveAll();

    // 内部连接: B.start 吸 A.end, 180° = 沿 A 延伸直行 (B.end → (180,0)).
    Attachment att;
    att.fromBlockId = b;
    att.fromPointId = doc.findBlock(b)->segments.front().startPointId;
    att.toBlockId   = a;
    att.toPointId   = doc.findBlock(a)->segments.front().endPointId;
    att.toSegmentId = doc.findBlock(a)->segments.front().id;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();
    QVERIFY(doc.findBlock(b)->worldPos(doc.findBlock(b)->segments.front().endPointId)
                .distanceTo(cad::geo::Vec2(180.0, 0.0)) < 1e-6);

    // 组建 (不冻结内部).
    cad::cmd::MakeComponentCommand(&doc, {a, b}, QStringLiteral("C")).redo();
    const auto startB = doc.findBlock(b)->segments.front().startPointId;
    const auto endA   = doc.findBlock(a)->segments.front().endPointId;
    const cad::geo::Vec2 aEndBefore = doc.findBlock(a)->worldPos(endA);
    const cad::geo::Vec2 bStartBefore = doc.findBlock(b)->worldPos(startB);
    QVERIFY(aEndBefore.distanceTo(bStartBefore) < 1e-6);

    // 整体平移 (模拟拖组件): 所有成员同 delta.
    const cad::geo::Vec2 delta(12.0, -7.0);
    for (const auto& blk : doc.blocks())
        doc.findBlock(blk.id)->transform.origin = blk.transform.origin + delta;
    doc.resolveAll();

    // 内部 attachment 依然满足 (相对几何保持), 且整体平移了 delta.
    QVERIFY(doc.findBlock(a)->worldPos(endA).distanceTo(aEndBefore + delta) < 1e-9);
    QVERIFY(doc.findBlock(b)->worldPos(startB).distanceTo(bStartBefore + delta) < 1e-9);
    QVERIFY(doc.findBlock(a)->worldPos(endA).distanceTo(doc.findBlock(b)->worldPos(startB)) < 1e-9);
}

// 组件级连接: 组件整体作为 follower, 借用暴露端点连接 + 端点线段方向.
// 内部 attachment 保持; 外部线移动 → 整组跟随; 暴露端点首个连接时自动记录.
void TestComponent::componentLevelConnectFollowsLeader()
{
    ParamDocument doc;
    const QUuid a = doc.addBlock(makeLine(cad::geo::Vec2(0, 0), cad::geo::Vec2(100, 0)));
    const QUuid b = doc.addBlock(makeLine(cad::geo::Vec2(100, 0), cad::geo::Vec2(80, 0)));
    const QUuid x = doc.addBlock(makeLine(cad::geo::Vec2(100, -80), cad::geo::Vec2(60, 0)));
    doc.resolveAll();

    // 内部连接 B→A (延伸).
    Attachment internal;
    internal.fromBlockId = b;
    internal.fromPointId = doc.findBlock(b)->segments.front().startPointId;
    internal.toBlockId   = a;
    internal.toPointId   = doc.findBlock(a)->segments.front().endPointId;
    internal.toSegmentId = doc.findBlock(a)->segments.front().id;
    internal.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(internal));

    // 组建.
    cad::cmd::MakeComponentCommand(&doc, {a, b}, QStringLiteral("C")).redo();
    const QUuid compId = doc.components().front().id;

    // 组件级连接: 组件 → X, 暴露端点 = B.end.
    const QUuid bEnd = doc.findBlock(b)->segments.front().endPointId;
    const QUuid xEnd = doc.findBlock(x)->segments.front().endPointId;
    Attachment compAtt;
    compAtt.fromComponentId = compId;
    compAtt.fromPointId = bEnd;
    compAtt.toBlockId = x;
    compAtt.toPointId = xEnd;
    compAtt.toSegmentId = doc.findBlock(x)->segments.front().id;
    compAtt.followerAngle = 0.0;  // 闭合基准 0°: 组件折叠到 X 方向
    QVERIFY(doc.addAttachment(compAtt));
    doc.resolveAll();

    // 组件级 attachment 建立 + 暴露端点自动记录 + 内部 B→A 保留.
    QCOMPARE(static_cast<int>(doc.attachments().size()), 2);
    QCOMPARE(doc.components().front().exposedPointId, bEnd);
    QVERIFY(doc.components().front().exposedSegmentId == doc.findBlock(b)->segments.front().id);

    // 暴露端点 (B.end) 落到 X.end; 内部 B.start == A.end 保持.
    const cad::geo::Vec2 xEndWorld = doc.findBlock(x)->worldPos(xEnd);
    QVERIFY(doc.findBlock(b)->worldPos(bEnd).distanceTo(xEndWorld) < 1e-6);
    const auto bStart = doc.findBlock(b)->segments.front().startPointId;
    const auto aEnd   = doc.findBlock(a)->segments.front().endPointId;
    QVERIFY(doc.findBlock(b)->worldPos(bStart).distanceTo(doc.findBlock(a)->worldPos(aEnd)) < 1e-6);

    // X 移动 → 组件整体跟随 (暴露端点仍落 X.end), 内部保持.
    const cad::geo::Vec2 xMove(50.0, 30.0);
    doc.findBlock(x)->transform.origin = doc.findBlock(x)->transform.origin + xMove;
    doc.resolveAll();
    QVERIFY(doc.findBlock(b)->worldPos(bEnd).distanceTo(doc.findBlock(x)->worldPos(xEnd)) < 1e-6);
    QVERIFY(doc.findBlock(b)->worldPos(bStart).distanceTo(doc.findBlock(a)->worldPos(aEnd)) < 1e-6);
}

// 森林不变式 (组件维度): 一个组件至多一条外部跟随线.
void TestComponent::componentOneExternalLinkOnly()
{
    ParamDocument doc;
    const QUuid a = doc.addBlock(makeLine(cad::geo::Vec2(0, 0), cad::geo::Vec2(100, 0)));
    const QUuid b = doc.addBlock(makeLine(cad::geo::Vec2(100, 0), cad::geo::Vec2(80, 0)));
    const QUuid x = doc.addBlock(makeLine(cad::geo::Vec2(100, -80), cad::geo::Vec2(60, 0)));
    const QUuid y = doc.addBlock(makeLine(cad::geo::Vec2(300, -80), cad::geo::Vec2(60, 0)));
    doc.resolveAll();
    cad::cmd::MakeComponentCommand(&doc, {a, b}, QStringLiteral("C")).redo();
    const QUuid compId = doc.components().front().id;

    const auto bEnd = doc.findBlock(b)->segments.front().endPointId;
    auto mk = [&](const QUuid& leader) {
        Attachment att;
        att.fromComponentId = compId;
        att.fromPointId = bEnd;
        att.toBlockId = leader;
        att.toPointId = doc.findBlock(leader)->segments.front().endPointId;
        att.toSegmentId = doc.findBlock(leader)->segments.front().id;
        return att;
    };
    QVERIFY(doc.addAttachment(mk(x)));
    QVERIFY(!doc.addAttachment(mk(y)));  // 第二条拒绝
    QCOMPARE(static_cast<int>(doc.attachments().size()), 1);
}

void TestComponent::makeUndoDissolves()
{
    ParamDocument doc;
    const QUuid b1 = doc.addBlock(makeLine(cad::geo::Vec2(0, 0), cad::geo::Vec2(50, 0)));
    const QUuid b2 = doc.addBlock(makeLine(cad::geo::Vec2(60, 10), cad::geo::Vec2(0, 30)));
    cad::cmd::MakeComponentCommand cmd(&doc, {b1, b2}, QStringLiteral("C"));
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.components().size()), 1);

    cmd.undo();
    QVERIFY(doc.components().empty());
    QVERIFY(doc.findBlock(b1) != nullptr);
    QVERIFY(doc.findBlock(b2) != nullptr);
    QVERIFY(doc.componentOfBlock(b1) == nullptr);
}

void TestComponent::dissolveKeepsSegments()
{
    ParamDocument doc;
    const QUuid b1 = doc.addBlock(makeLine(cad::geo::Vec2(0, 0), cad::geo::Vec2(50, 0)));
    const QUuid b2 = doc.addBlock(makeLine(cad::geo::Vec2(60, 10), cad::geo::Vec2(0, 30)));
    cad::cmd::MakeComponentCommand(&doc, {b1, b2}, QStringLiteral("C")).redo();
    const QUuid compId = doc.components().front().id;

    cad::cmd::DissolveComponentCommand dis(&doc, compId);
    dis.redo();
    QVERIFY(doc.components().empty());
    QVERIFY(doc.findBlock(b1) != nullptr);
    QVERIFY(doc.findBlock(b2) != nullptr);

    dis.undo();
    QCOMPARE(static_cast<int>(doc.components().size()), 1);
    QVERIFY(doc.findComponent(compId) != nullptr);
}

void TestComponent::deleteRemovesMembers()
{
    ParamDocument doc;
    const QUuid b1 = doc.addBlock(makeLine(cad::geo::Vec2(0, 0), cad::geo::Vec2(50, 0)));
    const QUuid b2 = doc.addBlock(makeLine(cad::geo::Vec2(60, 10), cad::geo::Vec2(0, 30)));
    cad::cmd::MakeComponentCommand(&doc, {b1, b2}, QStringLiteral("C")).redo();
    const QUuid compId = doc.components().front().id;

    cad::cmd::DeleteComponentCommand del(&doc, compId);
    del.redo();
    QVERIFY(doc.components().empty());
    QVERIFY(doc.findBlock(b1) == nullptr);
    QVERIFY(doc.findBlock(b2) == nullptr);

    del.undo();
    QCOMPARE(static_cast<int>(doc.components().size()), 1);
    QVERIFY(doc.findBlock(b1) != nullptr);
    QVERIFY(doc.findBlock(b2) != nullptr);
}

void TestComponent::serializeRoundTrip()
{
    ParamDocument doc;
    const QUuid b1 = doc.addBlock(makeLine(cad::geo::Vec2(0, 0), cad::geo::Vec2(50, 0)));
    const QUuid b2 = doc.addBlock(makeLine(cad::geo::Vec2(60, 10), cad::geo::Vec2(0, 30)));
    const QUuid b3 = doc.addBlock(makeLine(cad::geo::Vec2(100, -20), cad::geo::Vec2(40, 40)));
    cad::cmd::MakeComponentCommand(&doc, {b1, b2, b3}, QStringLiteral("袖片")).redo();
    Component* src = doc.findComponent(doc.components().front().id);
    src->showBoundingBox = false;
    src->defaultAngleDeg = 42.0;

    // 组件级连接也要 round-trip (fromComponentId). Leader 必须是组件外的线.
    const QUuid x = doc.addBlock(makeLine(cad::geo::Vec2(200, 0), cad::geo::Vec2(40, 0)));
    const QUuid b3End = doc.findBlock(b3)->segments.front().endPointId;
    Attachment compAtt;
    compAtt.fromComponentId = src->id;
    compAtt.fromPointId = b3End;
    compAtt.toBlockId = x;
    compAtt.toPointId = doc.findBlock(x)->segments.front().endPointId;
    compAtt.toSegmentId = doc.findBlock(x)->segments.front().id;
    QVERIFY(doc.addAttachment(compAtt));
    src->exposedPointId = b3End;
    src->exposedSegmentId = doc.findBlock(b3)->segments.front().id;

    ParamDocument dst;
    DocumentSerializer::deserialize(dst, DocumentSerializer::serialize(doc));

    QCOMPARE(static_cast<int>(dst.components().size()), 1);
    const Component* c = dst.findComponent(dst.components().front().id);
    QVERIFY(c != nullptr);
    QCOMPARE(c->name, QStringLiteral("袖片"));
    QCOMPARE(static_cast<int>(c->memberBlockIds.size()), 3);
    QCOMPARE(c->showBoundingBox, false);
    QCOMPARE(c->defaultAngleDeg, 42.0);
    QCOMPARE(c->exposedPointId, b3End);
    QVERIFY(dst.componentOfBlock(c->memberBlockIds.front()) != nullptr);
    // 组件级 attachment round-trip.
    bool foundCompAtt = false;
    for (const auto& a : dst.attachments())
        if (a.fromComponentId == c->id) { foundCompAtt = true; break; }
    QVERIFY(foundCompAtt);
}

void TestComponent::boundingBoxValid()
{
    ParamDocument doc;
    doc.addBlock(makeLine(cad::geo::Vec2(0, 0), cad::geo::Vec2(50, 0)));
    doc.addBlock(makeLine(cad::geo::Vec2(60, 10), cad::geo::Vec2(0, 30)));
    doc.addBlock(makeLine(cad::geo::Vec2(100, -20), cad::geo::Vec2(40, 40)));
    cad::cmd::MakeComponentCommand(&doc,
        QList<QUuid>{doc.blocks()[0].id, doc.blocks()[1].id, doc.blocks()[2].id},
        QStringLiteral("C")).redo();
    doc.resolveAll();

    const BBox box = doc.boundingBoxOf(doc.components().front().id);
    QVERIFY(box.valid);
    QVERIFY(box.width() > 0.0);
    QVERIFY(box.height() > 0.0);
}

// 手势级: 抓组件成员端点 (该成员是内部 follower) 连外部线 → 组件级连接
// 建立 (fromComponentId), 内部 B→A 保留; undo 还原.
void TestComponent::junctionComponentConnectGesture()
{
    using cad::test::makeLine;
    using cad::test::layerIdAt;

    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    // A: (0,0)-(100,0). B: (100,0)-(180,0) (180° 延伸, B.end=(180,0)). X: 外部线.
    const auto a = makeLine(doc, 100.0);
    const auto b = makeLine(doc, 80.0, cad::geo::Vec2(100.0, 0.0));
    const auto x = makeLine(doc, 60.0, cad::geo::Vec2(100.0, -80.0));
    doc.resolveAll();

    // 内部连接 B→A (延伸直行).
    cad::param::Attachment att;
    att.fromBlockId = b.blockId;
    att.fromPointId = b.startId;
    att.toBlockId   = a.blockId;
    att.toPointId   = a.endId;
    att.toSegmentId = a.segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));

    // 组建 (内部 B→A 保留).
    cad::cmd::MakeComponentCommand(&doc, {a.blockId, b.blockId}, QStringLiteral("组")).redo();
    QCOMPARE(static_cast<int>(doc.attachments().size()), 1);
    const QUuid compId = doc.components().front().id;

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);
    // NOTE: ConnectGesture pushes its undo macro to m_paramDoc->undoStack()
    // (ToolSelect::activate), not the injected tool stack — undo via the doc.

    // 连接角度会话记录器 (二期): 手势经 ToolHost 上报会话开始/结束。
    QUuid sessBlock, sessSeg, sessAtt;
    bool sessStarted = false, sessEnded = false;
    connect(&tm, &cad::tools::ToolManager::connectAngleSessionChanged,
            &tm, [&](const QUuid& bid, const QUuid& sid, const QUuid& aid, double) {
                if (!aid.isNull()) { sessBlock = bid; sessSeg = sid; sessAtt = aid; sessStarted = true; }
                else sessEnded = true;
            });

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) 选中 B (点击线身).
    sendMouse(QEvent::MouseButtonPress, vp(140.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(140.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    // 前序事件已同步处理完成(工具状态机/文档变更直接生效); 无统一可观测条件, 暂留排空。
    QTest::qWait(20);

    // 2) 抓 B 的 END 端点 (B 是内部 follower — 组件级连接不占用线级名额) 拖到 X.end.
    sendMouse(QEvent::MouseButtonPress, vp(180.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(175.0, -20.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(170.0, -50.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(160.0, -80.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(160.0, -80.0), Qt::LeftButton, Qt::NoModifier);
    // 前序事件已同步处理完成(工具状态机/文档变更直接生效); 无统一可观测条件, 暂留排空。
    QTest::qWait(30);

    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);
    QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
    // 二期: 组件级连接同样进入连接角度会话 (跟随线段 = 借用的暴露成员块线段)。
    QVERIFY2(sessStarted, "组件级连接必须上报连接角度会话");
    QVERIFY(!sessAtt.isNull());
    QCOMPARE(sessBlock, b.blockId);
    QVERIFY(!sessSeg.isNull());
    // 2 条 attachment: 内部 B→A 保留 + 组件级连接 (组件 → X).
    QCOMPARE(static_cast<int>(doc.attachments().size()), 2);

    // 3) Esc → 提交 (保留连接)。
    ts->connectAngleCancelled();   // 条带 Esc → 工具 → 手势
    QTest::qWait(30);

    QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
    QCOMPARE(static_cast<int>(doc.attachments().size()), 2);
    QVERIFY2(sessEnded, "收尾后必须上报会话结束 (全 null)");

    // 内部 B→A 保留; 新增的是组件级连接 (fromComponentId = 组件).
    bool foundInternal = false, foundComp = false;
    for (const auto& aa : doc.attachments()) {
        if (aa.fromBlockId == b.blockId && aa.toBlockId == a.blockId) foundInternal = true;
        if (aa.fromComponentId == compId) foundComp = true;
    }
    QVERIFY(foundInternal);
    QVERIFY(foundComp);
    // 暴露端点自动记录 = 借用的端点 (B.end).
    QCOMPARE(doc.components().front().exposedPointId, b.endId);

    // 4) undo: 组件级连接删除, 内部 B→A 仍在, 暴露端点恢复空.
    doc.undoStack()->undo();
    QCOMPARE(static_cast<int>(doc.attachments().size()), 1);
    QCOMPARE(doc.attachments().front().fromBlockId, b.blockId);
    QCOMPARE(doc.attachments().front().toBlockId, a.blockId);
    QVERIFY(doc.components().front().exposedPointId.isNull());
}

// 组件级连接重叠切换 (用户要求 2026-09): 连接点有多个端点重叠时, 组件释放
// 直接连最近候选 (拖到哪个对象即 leader); 角度窗口内点击重叠线段的线身即可
// 切换跟随对象 (点选重叠点的线段确定跟随对象), 提交后整步 undo.
void TestComponent::componentConnectOverlapSwitchTarget()
{
    using cad::test::makeLine;
    using cad::test::layerIdAt;

    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    // A: (0,0)-(100,0). B: (100,0)-(180,0). X/Y 的起点都精确落在 (180,0):
    //   X 水平 (180,0)-(240,0); Y 旋转 90°, 竖直向上 (180,0)-(180,70).
    const auto a = makeLine(doc, 100.0);
    const auto b = makeLine(doc, 80.0, cad::geo::Vec2(100.0, 0.0));
    const auto x = makeLine(doc, 60.0, cad::geo::Vec2(180.0, 0.0));
    const auto y = makeLine(doc, 70.0, cad::geo::Vec2(180.0, 0.0));
    doc.findBlock(y.blockId)->transform.rotation = M_PI / 2.0;
    doc.resolveAll();
    QVERIFY(doc.findBlock(x.blockId)->worldPos(x.startId)
                .distanceTo(cad::geo::Vec2(180.0, 0.0)) < 1e-6);
    QVERIFY(doc.findBlock(y.blockId)->worldPos(y.startId)
                .distanceTo(cad::geo::Vec2(180.0, 0.0)) < 1e-6);

    // 内部连接 B→A (延伸直行): B.end 世界 = (180,0).
    cad::param::Attachment att;
    att.fromBlockId = b.blockId;
    att.fromPointId = b.startId;
    att.toBlockId   = a.blockId;
    att.toPointId   = a.endId;
    att.toSegmentId = a.segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    QVERIFY(doc.findBlock(b.blockId)->worldPos(b.endId)
                .distanceTo(cad::geo::Vec2(180.0, 0.0)) < 1e-6);

    // 组建 (内部 B→A 保留).
    cad::cmd::MakeComponentCommand(&doc, {a.blockId, b.blockId}, QStringLiteral("组")).redo();
    const QUuid compId = doc.components().front().id;

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos,
                         Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn | (mods & Qt::ControlModifier ? Qt::LeftButton : Qt::NoButton));
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    auto click = [&](double x, double y) {
        const QPoint p = vp(x, y);
        sendMouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoModifier);
    };
    // 在候选线段的线身中点上点击 (远离 HUD 浮窗): X → (210,0), Y → (180,35).
    auto clickBody = [&](const cad::test::LineSetup& l) {
        if (l.blockId == x.blockId) click(210.0, 0.0);
        else                        click(180.0, 35.0);
    };
    auto findCompAtt = [&]() -> const cad::param::Attachment* {
        for (const auto& aa : doc.attachments())
            if (!aa.fromComponentId.isNull()) return &aa;
        return nullptr;
    };

    // 1) 选中 B (点击线身).
    click(140.0, 0.0);
    // 前序事件已同步处理完成(工具状态机/文档变更直接生效); 无统一可观测条件, 暂留排空。
    QTest::qWait(20);

    // 2) 抓 B 的 END 端点拖到重叠点 (180,0) 释放 → 直接连最近候选.
    sendMouse(QEvent::MouseButtonPress, vp(180.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(200.0, -60.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(190.0, -30.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(185.0, -8.0), Qt::NoButton, Qt::NoModifier);
    // 最后一步 move 落在目标点上 (release 只认最后一次 move 留下的 snap).
    sendMouse(QEvent::MouseMove, vp(180.0, 0.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(180.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    // 前序事件已同步处理完成(工具状态机/文档变更直接生效); 无统一可观测条件, 暂留排空。
    QTest::qWait(30);

    auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool());
    QVERIFY(ts);
    QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
    QCOMPARE(static_cast<int>(doc.attachments().size()), 2);
    // 二期: 会话经 ToolHost 上报 (无浮动 HUD 可查, 会话即角度输入入口)。

    // 初始 leader = 最近候选 (X 或 Y, 顺序不承诺 — 读出来再切到另一个).
    const cad::param::Attachment* compAtt0 = findCompAtt();
    QVERIFY(compAtt0);
    const bool initialIsX = (compAtt0->toBlockId == x.blockId);
    QVERIFY(initialIsX || compAtt0->toBlockId == y.blockId);
    const auto& orig  = initialIsX ? x : y;
    const auto& other = initialIsX ? y : x;

    // 3) 点击另一条重叠线段的线身 → 跟随对象切换 (基准换掉, 角度窗口仍在).
    clickBody(other);
    QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
    {
        const auto* a1 = findCompAtt();
        QVERIFY(a1);
        QCOMPARE(a1->toBlockId, other.blockId);
        QCOMPARE(a1->toPointId, other.startId);
        QCOMPARE(a1->toSegmentId, other.segId);
        // 暴露端点保持 (移除重加后自动重新记录).
        QCOMPARE(doc.components().front().exposedPointId, b.endId);
        // 几何: B.end 仍精确落在重叠点 (180,0).
        QVERIFY(doc.findBlock(b.blockId)->worldPos(b.endId).distanceTo(
            doc.findBlock(other.blockId)->worldPos(other.startId)) < 1e-6);
    }

    // 4) 切换回原基准再切到 other (候选集随当前基准刷新).
    clickBody(orig);
    QCOMPARE(findCompAtt()->toBlockId, orig.blockId);
    clickBody(other);
    QCOMPARE(findCompAtt()->toBlockId, other.blockId);

    // 5) 点组件成员线身 (非候选) = 无操作, 连接保持.
    click(50.0, 0.0);
    QCOMPARE(ts->state(), cad::tools::SelectState::AngleInput);
    QCOMPARE(findCompAtt()->toBlockId, other.blockId);

    // 6) Esc → 提交 (带符号折角保持初始角): 连接保留、基准为 other.
    ts->connectAngleCancelled();   // 条带 Esc → 工具 → 手势
    QTest::qWait(30);
    QCOMPARE(ts->state(), cad::tools::SelectState::Idle);
    QCOMPARE(static_cast<int>(doc.attachments().size()), 2);
    const auto* aFinal = findCompAtt();
    QVERIFY(aFinal);
    QCOMPARE(aFinal->toBlockId, other.blockId);
    QCOMPARE(doc.components().front().exposedPointId, b.endId);

    // 7) undo: 组件级连接删除, 内部 B→A 仍在, 暴露端点恢复空.
    doc.undoStack()->undo();
    QCOMPARE(static_cast<int>(doc.attachments().size()), 1);
    QCOMPARE(doc.attachments().front().fromBlockId, b.blockId);
    QCOMPARE(doc.attachments().front().toBlockId, a.blockId);
    QVERIFY(doc.components().front().exposedPointId.isNull());
}

// 回归 (settleComponents 性能修复): 无变更的重复 resolveAll 不得改写任何成员
// 变换 — applyComponentTransform 需 epsilon 判等 (delta<1e-9 rad &&
// translate²<1e-12 mm²) 判定"实际未动"并跳过写入, settleComponents 返回 false
// 让 4 轮沉降循环首轮即断. 旧实现 compMoved 只要存在组件级连接即为 true,
// 每次 resolve 无条件跑满 4 轮全组件沉降 + 4 次森林重解.
void TestComponent::settleNoOpLeavesTransformsUntouched()
{
    ParamDocument doc;
    const QUuid a = doc.addBlock(makeLine(cad::geo::Vec2(0, 0), cad::geo::Vec2(100, 0)));
    const QUuid b = doc.addBlock(makeLine(cad::geo::Vec2(100, 0), cad::geo::Vec2(80, 0)));
    const QUuid x = doc.addBlock(makeLine(cad::geo::Vec2(100, -80), cad::geo::Vec2(60, 0)));
    doc.resolveAll();

    // 内部连接 B→A + 组件 + 组件级外部连接 (暴露端点 = B.end) — 复刻热路径形态.
    Attachment internal;
    internal.fromBlockId = b;
    internal.fromPointId = doc.findBlock(b)->segments.front().startPointId;
    internal.toBlockId   = a;
    internal.toPointId   = doc.findBlock(a)->segments.front().endPointId;
    internal.toSegmentId = doc.findBlock(a)->segments.front().id;
    internal.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(internal));
    cad::cmd::MakeComponentCommand(&doc, {a, b}, QStringLiteral("C")).redo();
    Attachment compAtt;
    compAtt.fromComponentId = doc.components().front().id;
    compAtt.fromPointId = doc.findBlock(b)->segments.front().endPointId;
    compAtt.toBlockId   = x;
    compAtt.toPointId   = doc.findBlock(x)->segments.front().endPointId;
    compAtt.toSegmentId = doc.findBlock(x)->segments.front().id;
    compAtt.followerAngle = 0.0;
    QVERIFY(doc.addAttachment(compAtt));
    doc.resolveAll();

    auto snap = [&](const QUuid& id) {
        const auto* blk = doc.findBlock(id);
        return std::make_tuple(blk->transform.origin.x, blk->transform.origin.y,
                               blk->transform.rotation);
    };

    // 无任何变更, 再次 resolveAll: 成员变换必须逐位不变.
    const auto a1 = snap(a), b1 = snap(b), x1 = snap(x);
    doc.resolveAll();
    QCOMPARE(snap(a), a1);
    QCOMPARE(snap(b), b1);
    QCOMPARE(snap(x), x1);

    // leader 移动后收敛, 再次无变更 resolveAll 同样逐位不变.
    doc.findBlock(x)->transform.origin =
        doc.findBlock(x)->transform.origin + cad::geo::Vec2(50.0, 30.0);
    doc.resolveAll();
    const auto a2 = snap(a), b2 = snap(b);
    doc.resolveAll();
    QCOMPARE(snap(a), a2);
    QCOMPARE(snap(b), b2);
}

QTEST_MAIN(TestComponent)
#include "test_component.moc"
