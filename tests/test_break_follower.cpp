#include "test_break_helpers.h"

class TestBreakFollower : public QObject
{
    Q_OBJECT

private slots:
    void breakFollowerAtOriginalEnd();
    void curveBreakFollowerKeepsDirection();
    void breakFollowerAtBreakpoint();
    void breakComponentAtBreakpoint();
    void polarEndpointAnchorCycleResolves();
    void hoverReportsSegment();
};

void TestBreakFollower::breakFollowerAtOriginalEnd()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0);
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);

    Block b;
    b.transform.origin = Vec2(200.0, 0.0);
    ParamPoint b1;
    b1.constraint = PointConstraint::Free;
    b1.freePos = Vec2::zero();
    QUuid b1Id = b1.id;
    ParamPoint b2;
    b2.constraint = PointConstraint::Polar;
    b2.refPointId = b1Id;
    b2.distance = 80.0;
    b2.angle = 0.0;
    QUuid b2Id = b2.id;
    b.addPoint(std::move(b1));
    b.addPoint(std::move(b2));
    Segment bs;
    bs.startPointId = b1Id;
    bs.endPointId = b2Id;
    b.addSegment(std::move(bs));
    QUuid bId = doc.addBlock(std::move(b));
    doc.resolveAll();

    Attachment att;
    att.fromBlockId = bId;
    att.fromPointId = b1Id;
    att.toBlockId = blockId;
    att.toPointId = endId;
    att.toSegmentId = segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const auto* blk = doc.findBlock(bId);
    const Vec2 before = blk->transform.toWorld(blk->findPoint(b1Id)->resolvedPos);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto* blk2 = doc.findBlock(bId);
    QVERIFY(blk2);
    const Vec2 after = blk2->transform.toWorld(blk2->findPoint(b1Id)->resolvedPos);
    QVERIFY2(after.distanceTo(before) < 0.01,
             qPrintable(QStringLiteral("end follower jumped %1 mm").arg(after.distanceTo(before))));

    // Connection survives, re-pointed to the BACK block's end.
    QUuid backBlockId;
    for (const auto& bb : doc.blocks())
        if (bb.id != blockId) backBlockId = bb.id;
    QVERIFY(!backBlockId.isNull());
    bool attAlive = false;
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == bId) {
            attAlive = true;
            QCOMPARE(a.toBlockId, backBlockId);
        }
    QVERIFY(attAlive);

    // Undo/redo round-trip keeps the connection and position.
    cmd.undo();
    bool attBack = false;
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == bId) attBack = true;
    QVERIFY(attBack);
    cmd.redo();
    bool attAlive2 = false;
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == bId) attAlive2 = true;
    QVERIFY(attAlive2);
}

// ---------------------------------------------------------------------------
// 曲线断点上的跟随线：打断后世界朝向必须零跳变（角度补偿 refDeltaRad）。
// ---------------------------------------------------------------------------
void TestBreakFollower::curveBreakFollowerKeepsDirection()
{
    ParamDocument doc;
    auto [blockId, segId, pp1Id, pp2Id] = makeCurve(doc);
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);

    Block b;
    b.transform.origin = Vec2::zero();
    ParamPoint b1;
    b1.constraint = PointConstraint::Free;
    b1.freePos = Vec2::zero();
    QUuid b1Id = b1.id;
    ParamPoint b2;
    b2.constraint = PointConstraint::Polar;
    b2.refPointId = b1Id;
    b2.distance = 80.0;
    b2.angle = 20.0;
    QUuid b2Id = b2.id;
    b.addPoint(std::move(b1));
    b.addPoint(std::move(b2));
    Segment bs;
    bs.startPointId = b1Id;
    bs.endPointId = b2Id;
    b.addSegment(std::move(bs));
    QUuid bId = doc.addBlock(std::move(b));
    doc.resolveAll();

    Attachment att;
    att.fromBlockId = bId;
    att.fromPointId = b1Id;
    att.toBlockId = blockId;
    att.toPointId = auxId;
    att.toSegmentId = segId;
    att.followerAngle = 55.0;  // some non-trivial relative angle
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const auto* blk = doc.findBlock(bId);
    const Vec2 p0before = blk->transform.toWorld(blk->findPoint(b1Id)->resolvedPos);
    const Vec2 p1before = blk->transform.toWorld(blk->findPoint(b2Id)->resolvedPos);
    const double dirBefore = std::atan2(p1before.y - p0before.y, p1before.x - p0before.x);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto* blk2 = doc.findBlock(bId);
    QVERIFY(blk2);
    const Vec2 p0after = blk2->transform.toWorld(blk2->findPoint(b1Id)->resolvedPos);
    const Vec2 p1after = blk2->transform.toWorld(blk2->findPoint(b2Id)->resolvedPos);
    const double dirAfter = std::atan2(p1after.y - p0after.y, p1after.x - p0after.x);
    double dAng = std::abs(dirAfter - dirBefore);
    dAng = std::fmod(dAng, 2.0 * M_PI);
    if (dAng > M_PI) dAng = 2.0 * M_PI - dAng;
    const double dAngDeg = dAng * 180.0 / M_PI;
    qInfo().noquote() << QStringLiteral("follower dir before=%1 after=%2 deltaDeg=%3")
        .arg(dirBefore * 180.0 / M_PI, 0, 'f', 3)
        .arg(dirAfter * 180.0 / M_PI, 0, 'f', 3).arg(dAngDeg, 0, 'f', 4);

    QVERIFY2(dAngDeg < 0.01,
             qPrintable(QStringLiteral("curve follower rotated %1 deg on break").arg(dAngDeg)));
    QVERIFY2(p0after.distanceTo(p0before) < 0.01,
             qPrintable(QStringLiteral("curve follower jumped %1 mm")
                        .arg(p0after.distanceTo(p0before))));
    bool attAlive = false;
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == bId) attAlive = true;
    QVERIFY(attAlive);
}

// ---------------------------------------------------------------------------
// REPRO 2a (端点连接打断位置跳变): a follower line attached at the break
// point must keep its position AND its connection after the break.
// ---------------------------------------------------------------------------
void TestBreakFollower::breakFollowerAtBreakpoint()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0);
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);

    Block b;
    b.transform.origin = Vec2(100.0, 0.0);
    ParamPoint b1;
    b1.constraint = PointConstraint::Free;
    b1.freePos = Vec2::zero();
    QUuid b1Id = b1.id;
    ParamPoint b2;
    b2.constraint = PointConstraint::Polar;
    b2.refPointId = b1Id;
    b2.distance = 80.0;
    b2.angle = 0.0;
    QUuid b2Id = b2.id;
    b.addPoint(std::move(b1));
    b.addPoint(std::move(b2));
    Segment bs;
    bs.startPointId = b1Id;
    bs.endPointId = b2Id;
    QUuid bSegId = bs.id;
    b.addSegment(std::move(bs));
    QUuid bId = doc.addBlock(std::move(b));
    doc.resolveAll();

    Attachment att;
    att.fromBlockId = bId;
    att.fromPointId = b1Id;
    att.toBlockId = blockId;
    att.toPointId = auxId;
    att.toSegmentId = segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const auto* blk = doc.findBlock(bId);
    const Vec2 before = blk->transform.toWorld(blk->findPoint(b1Id)->resolvedPos);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto* blk2 = doc.findBlock(bId);
    QVERIFY(blk2);
    const Vec2 after = blk2->transform.toWorld(blk2->findPoint(b1Id)->resolvedPos);
    qInfo().noquote() << QStringLiteral("follower before=(%1,%2) after=(%3,%4)")
        .arg(before.x, 0, 'f', 3).arg(before.y, 0, 'f', 3)
        .arg(after.x, 0, 'f', 3).arg(after.y, 0, 'f', 3);

    bool attAlive = false;
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == bId) attAlive = true;
    qInfo() << "follower attachment alive:" << attAlive;

    QVERIFY2(after.distanceTo(before) < 0.01,
             qPrintable(QStringLiteral("follower jumped %1 mm").arg(after.distanceTo(before))));
    QVERIFY(attAlive);  // the connection must survive the break
}

// ---------------------------------------------------------------------------
// REPRO 2b (组件连接打断位置跳变): a component attached at the break point
// must keep its position AND its connection after the break.
// ---------------------------------------------------------------------------
void TestBreakFollower::breakComponentAtBreakpoint()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 200.0);
    QUuid auxId = addAuxPoint(doc, blockId, segId, 0.5);

    // Component member A: starts at (100, 0) → (150, 0).
    Block ma;
    ma.transform.origin = Vec2(100.0, 0.0);
    ParamPoint ma1;
    ma1.constraint = PointConstraint::Free;
    ma1.freePos = Vec2::zero();
    QUuid ma1Id = ma1.id;
    ParamPoint ma2;
    ma2.constraint = PointConstraint::Free;
    ma2.freePos = Vec2(50.0, 0.0);
    QUuid ma2Id = ma2.id;
    ma.addPoint(std::move(ma1));
    ma.addPoint(std::move(ma2));
    Segment mas;
    mas.startPointId = ma1Id;
    mas.endPointId = ma2Id;
    ma.addSegment(std::move(mas));
    QUuid maId = doc.addBlock(std::move(ma));

    Block mb;
    mb.transform.origin = Vec2(150.0, 0.0);
    ParamPoint mb1;
    mb1.constraint = PointConstraint::Free;
    mb1.freePos = Vec2::zero();
    QUuid mb1Id = mb1.id;
    ParamPoint mb2;
    mb2.constraint = PointConstraint::Free;
    mb2.freePos = Vec2(40.0, 30.0);
    QUuid mb2Id = mb2.id;
    mb.addPoint(std::move(mb1));
    mb.addPoint(std::move(mb2));
    Segment mbs;
    mbs.startPointId = mb1Id;
    mbs.endPointId = mb2Id;
    mb.addSegment(std::move(mbs));
    QUuid mbId = doc.addBlock(std::move(mb));
    doc.resolveAll();

    Component comp;
    comp.name = QStringLiteral("C");
    comp.memberBlockIds = {maId, mbId};
    QUuid compId = doc.addComponent(comp);

    Attachment att;
    att.fromComponentId = compId;
    att.fromPointId = ma1Id;  // exposed endpoint at the break point
    att.toBlockId = blockId;
    att.toPointId = auxId;
    att.toSegmentId = segId;
    att.followerAngle = 180.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const auto* blk = doc.findBlock(maId);
    const Vec2 before = blk->transform.toWorld(blk->findPoint(ma1Id)->resolvedPos);

    cad::cmd::BreakSegmentCommand cmd(&doc, blockId, segId, auxId);
    QVERIFY(cmd.isValid());
    cmd.redo();

    const auto* blk2 = doc.findBlock(maId);
    QVERIFY(blk2);
    const Vec2 after = blk2->transform.toWorld(blk2->findPoint(ma1Id)->resolvedPos);
    qInfo().noquote() << QStringLiteral("component member before=(%1,%2) after=(%3,%4)")
        .arg(before.x, 0, 'f', 3).arg(before.y, 0, 'f', 3)
        .arg(after.x, 0, 'f', 3).arg(after.y, 0, 'f', 3);

    bool attAlive = false;
    for (const auto& a : doc.attachments())
        if (a.fromComponentId == compId) attAlive = true;
    qInfo() << "component attachment alive:" << attAlive;

    QVERIFY2(after.distanceTo(before) < 0.01,
             qPrintable(QStringLiteral("component jumped %1 mm").arg(after.distanceTo(before))));
    QVERIFY(attAlive);  // the component connection must survive the break
}

// ---------------------------------------------------------------------------
// 3.gcad 循环复现（引擎级修复回归）：线段端点（Polar）锚在本线段交点辅助点
// 上 = 冷启动死锁（端点无缓存位姿 → 线段退化 → 交点永不解析 → 端点永不解析）。
// 修复：退化线段启动种子（极角公式暂时锚段起点），不动点随后收敛到真锚。
// 不动点解析值：P175=(7.5,0)，P176=(22.5,0)（= P175 + 15mm @0°，两轮收敛）。
// ---------------------------------------------------------------------------
void TestBreakFollower::polarEndpointAnchorCycleResolves()
{
    ParamDocument doc;
    auto [blockId, startId, endId, segId] = makeLine(doc, 15.0);

    // 交点射线锚点块（起点位于 (7.5, -50)，射线 90° 竖直向上）。
    auto [bId, bStartId, bEndId, bSegId] = makeLine(doc, 50.0);
    {
        auto* bBlk = doc.findBlock(bId);
        bBlk->transform.origin = Vec2(7.5, -50.0);
        doc.resolveAll();
    }

    // P175: 交点（host=线段本身, 锚点=另一块起点, 相对宿主角 90°）。
    QUuid auxId;
    {
        auto* blk = doc.findBlock(blockId);
        auto* seg = blk->findSegment(segId);
        ParamPoint pt;
        pt.constraint = PointConstraint::Intersection;
        pt.hostSegmentId = segId;
        pt.refPointA = bStartId;
        pt.interAngle = 90.0;
        pt.isAuxiliary = true;
        auxId = pt.id;
        blk->addPoint(std::move(pt));
        seg->auxPointIds.push_back(auxId);
    }

    // 复现 3.gcad：把线段端点锚到同段交点（循环依赖）。
    {
        auto* blk = doc.findBlock(blockId);
        auto* seg = blk->findSegment(segId);
        auto* ep = blk->findPoint(seg->endPointId);
        ep->refPointId = auxId;
    }

    // 冷启动（全新文档 = 无缓存位姿）——修复前此调用后端点永久 unresolved。
    doc.resolveAll();

    const auto* blk = doc.findBlock(blockId);
    const auto* seg = blk->findSegment(segId);
    const auto* sp = blk->findPoint(seg->startPointId);
    const auto* ep = blk->findPoint(seg->endPointId);
    const auto* aux = blk->findPoint(auxId);
    qInfo().noquote() << QStringLiteral("aux=(%1,%2) ep=(%3,%4)")
        .arg(aux->resolvedPos.x, 0, 'f', 3).arg(aux->resolvedPos.y, 0, 'f', 3)
        .arg(ep->resolvedPos.x, 0, 'f', 3).arg(ep->resolvedPos.y, 0, 'f', 3);
    QVERIFY2(aux->resolved, "intersection anchored to own segment must resolve");
    QVERIFY2(ep->resolved, "polar endpoint anchored to own-segment aux must resolve");
    QVERIFY2(aux->resolvedPos.distanceTo(Vec2(7.5, 0.0)) < 0.01,
             "aux must converge to the fixed point");
    QVERIFY2(ep->resolvedPos.distanceTo(Vec2(22.5, 0.0)) < 0.01,
             "endpoint must converge to the fixed point");
    (void)sp;
}

// ── 三期: 只读悬停上报 (扫过即看) — 打断工具悬停线段 → 上报线段;
//    悬停可断点 → 上报断点所在线段; 移出 → 空。 ──

void TestBreakFollower::hoverReportsSegment()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const LineSetup line = makeLine(doc, 100.0);
    doc.resolveAll();

    cad::test::RecordingToolHost host;
    cad::tools::ToolBreak tool;
    cad::tools::ToolContext ctx;
    ctx.scene = &scene;
    ctx.paramDoc = &doc;
    ctx.host = &host;
    tool.activate(ctx);

    // 线身中点 → 线段吸附 → 上报。
    cad::test::sendToolMouseMove(tool, QPointF(50.0, 0.0));
    QCOMPARE(host.hoverBlock, line.blockId);
    QCOMPARE(host.hoverSeg, line.segId);

    // 移出 → 上报空 (条带收起)。
    cad::test::sendToolMouseMove(tool, QPointF(300.0, 300.0));
    QVERIFY(host.hoverBlock.isNull());
    QVERIFY(host.hoverSeg.isNull());
    tool.deactivate();
}


QTEST_MAIN(TestBreakFollower)
#include "test_break_follower.moc"
