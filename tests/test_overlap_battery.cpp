#include <QTest>
#include <QApplication>
#include <QGraphicsView>
#include "canvas/CanvasScene.h"
#include "canvas/OverlapBatteryHud.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "geometry/Units.h"
#include "tools/ConnectOverlapResolver.h"
#include "tools/OverlapDisambiguationController.h"

using namespace cad::geo;
using namespace cad::param;
using namespace cad::tools;
using namespace cad::canvas;

class TestOverlapBattery : public QObject
{
    Q_OBJECT

private slots:
    void testBatteryHudGeometryAndHit();
    void testOverlapDisambiguationControllerPoints();
    void testOverlapDisambiguationControllerSegments();
    void testConnectOverlapResolverLandingPads();
};

void TestOverlapBattery::testBatteryHudGeometryAndHit()
{
    OverlapBatteryHud hud;
    QCOMPARE(hud.displayMode(), OverlapBatteryHud::DisplayMode::Hidden);
    QCOMPARE(hud.candidateCount(), 0);
    QVERIFY(hud.boundingRect().isEmpty());

    // Prepare 2 candidates
    std::vector<BatteryCandidate> cands;
    {
        BatteryCandidate c1;
        c1.kind = BatteryCandidate::Kind::Point;
        c1.blockId = QUuid::createUuid();
        c1.pointId = QUuid::createUuid();
        c1.title = QStringLiteral("P1");
        c1.name = QStringLiteral("测试放置点");
        c1.blockName = QStringLiteral("前衣片");
        c1.roleText = QStringLiteral("放置点");
        c1.isPlaced = true;
        cands.push_back(c1);

        BatteryCandidate c2;
        c2.kind = BatteryCandidate::Kind::Point;
        c2.blockId = QUuid::createUuid();
        c2.pointId = QUuid::createUuid();
        c2.title = QStringLiteral("P2");
        c2.name = QStringLiteral("侧缝端点");
        c2.blockName = QStringLiteral("后衣片");
        c2.roleText = QStringLiteral("端点");
        cands.push_back(c2);
    }

    hud.setCandidates(cands);
    QCOMPARE(hud.candidateCount(), 2);

    hud.updatePosition(Vec2{50.0, 50.0}, nullptr);
    const QPointF originScene = Coord::toScene(50.0, 50.0);
    QCOMPARE(hud.pos(), originScene);

    // 1. Badge mode
    hud.setDisplayMode(OverlapBatteryHud::DisplayMode::Badge);
    QCOMPARE(hud.displayMode(), OverlapBatteryHud::DisplayMode::Badge);
    QVERIFY(!hud.boundingRect().isEmpty());

    // Hit test badge: badge is located at (10, -10, 42, 20) relative to origin
    const QPointF badgeCenter = originScene + QPointF(25.0, 0.0);
    QVERIFY(hud.hitBadgeAtScene(badgeCenter, 1.0));
    QVERIFY(!hud.hitBadgeAtScene(originScene + QPointF(200.0, 200.0), 1.0));

    // 2. Expanded mode
    hud.setDisplayMode(OverlapBatteryHud::DisplayMode::Expanded);
    QCOMPARE(hud.displayMode(), OverlapBatteryHud::DisplayMode::Expanded);

    // Check hit on chip 0 vs chip 1
    const QPointF port0 = hud.candidatePortScenePos(0, 1.0);
    const QPointF port1 = hud.candidatePortScenePos(1, 1.0);
    QVERIFY(port0 != port1);

    // Click inside chip 0
    const QPointF chip0Pt = port0 + QPointF(50.0, 0.0);
    QCOMPARE(hud.hitCandidateAtScene(chip0Pt, 1.0), 0);

    // Hit test via world coordinate conversion
    const Vec2 chip0World = Coord::toUser(chip0Pt);
    QCOMPARE(hud.hitCandidateAtWorld(chip0World, 1.0), 0);

    hud.setSelectedIndex(1);
    QCOMPARE(hud.selectedIndex(), 1);

    // Click inside chip 1
    const QPointF chip1Pt = port1 + QPointF(50.0, 0.0);
    QCOMPARE(hud.hitCandidateAtScene(chip1Pt, 1.0), 1);
    const Vec2 chip1World = Coord::toUser(chip1Pt);
    QCOMPARE(hud.hitCandidateAtWorld(chip1World, 1.0), 1);

    // Outside click
    QCOMPARE(hud.hitCandidateAtScene(originScene + QPointF(-100.0, -100.0), 1.0), -1);

    // 3. Connect mode
    hud.setConnectMode(true);
    QVERIFY(hud.isConnectMode());
    // In connect mode, port ring left margin is expanded for snapping
    QCOMPARE(hud.hitCandidateAtScene(port0 - QPointF(5.0, 0.0), 1.0), 0);
}

void TestOverlapBattery::testOverlapDisambiguationControllerPoints()
{
    ParamDocument doc;
    CanvasScene scene(&doc);

    // Create Block 1 with endpoint at (100, 100)
    Block b1;
    b1.name = QStringLiteral("前片");
    b1.transform.origin = Vec2{0.0, 0.0};
    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2{100.0, 100.0};
    p1.resolvedPos = Vec2{100.0, 100.0};
    p1.resolved = true;
    p1.serial = QStringLiteral("P1");
    p1.name = QStringLiteral("前片顶");
    b1.points.push_back(p1);

    // Add a placed point on Block 1 at the same (100, 100)
    ParamPoint pPlaced;
    pPlaced.constraint = PointConstraint::Free;
    pPlaced.freePos = Vec2{100.0, 100.0};
    pPlaced.resolvedPos = Vec2{100.0, 100.0};
    pPlaced.resolved = true;
    pPlaced.serial = QStringLiteral("P2");
    pPlaced.name = QStringLiteral("前片偏置点");
    pPlaced.isPlaced = true;
    b1.points.push_back(pPlaced);
    b1.rebuildPointIndex();
    doc.addBlock(std::move(b1));

    // Create Block 2 with endpoint also at (100, 100)
    Block b2;
    b2.name = QStringLiteral("后片");
    b2.transform.origin = Vec2{0.0, 0.0};
    ParamPoint p3;
    p3.constraint = PointConstraint::Free;
    p3.freePos = Vec2{100.0, 100.0};
    p3.resolvedPos = Vec2{100.0, 100.0};
    p3.resolved = true;
    p3.serial = QStringLiteral("P3");
    p3.name = QStringLiteral("后片顶");
    b2.points.push_back(p3);
    b2.rebuildPointIndex();
    doc.addBlock(std::move(b2));

    QUuid pickedBlock;
    QUuid pickedPoint;
    QUuid pickedSegment;

    OverlapDisambiguationController ctl(
        &scene, &doc,
        [&](const QUuid& bid, const QUuid& sid) {
            pickedBlock = bid;
            pickedSegment = sid;
        },
        []() {},
        [&](const QUuid& bid, const QUuid& pid) {
            pickedBlock = bid;
            pickedPoint = pid;
        });

    // Collect overlapping points at (100, 100)
    const auto ptCands = ctl.collectPoints(Vec2{100.0, 100.0}, 1.0);
    QCOMPARE(ptCands.size(), 3);

    // Verify properties of collected points
    bool foundPlaced = false;
    for (const auto& c : ptCands) {
        QCOMPARE(c.kind, OverlapDisambiguationController::Candidate::Kind::Point);
        if (c.isPlaced) {
            foundPlaced = true;
            QCOMPARE(c.roleText, QString::fromUtf8("放置点"));
        }
    }
    QVERIFY(foundPlaced);

    // Display Battery HUD in Badge mode
    ctl.showBattery(Vec2{100.0, 100.0}, ptCands, OverlapBatteryHud::DisplayMode::Badge);
    QVERIFY(ctl.hasBattery());
    QCOMPARE(ctl.batteryMode(), OverlapBatteryHud::DisplayMode::Badge);
    QCOMPARE(ctl.batteryCandidateCount(), 3);

    // Expand battery HUD
    ctl.setBatteryMode(OverlapBatteryHud::DisplayMode::Expanded);
    QCOMPARE(ctl.batteryMode(), OverlapBatteryHud::DisplayMode::Expanded);

    // Apply pick point candidate 1
    ctl.activate(ptCands, ptCands[1].blockId, Vec2{100.0, 100.0});
    ctl.applyPick(1);
    QCOMPARE(pickedBlock, ptCands[1].blockId);
    QCOMPARE(pickedPoint, ptCands[1].pointId);

    ctl.hideBattery();
    QVERIFY(!ctl.hasBattery());
}

void TestOverlapBattery::testOverlapDisambiguationControllerSegments()
{
    ParamDocument doc;
    CanvasScene scene(&doc);

    // Create 2 overlapping segments
    auto makeSegBlock = [&](const QString& blkName, const QString& segSerial) {
        Block blk;
        blk.name = blkName;
        ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2{0, 0}; p1.resolvedPos = p1.freePos; p1.resolved = true;
        ParamPoint p2; p2.constraint = PointConstraint::Free; p2.freePos = Vec2{100, 0}; p2.resolvedPos = p2.freePos; p2.resolved = true;
        blk.points = {p1, p2};
        blk.rebuildPointIndex();
        Segment seg;
        seg.startPointId = blk.points[0].id;
        seg.endPointId = blk.points[1].id;
        seg.serial = segSerial;
        blk.segments.push_back(seg);
        doc.addBlock(std::move(blk));
    };

    makeSegBlock(QStringLiteral("线段A"), QStringLiteral("L1"));
    makeSegBlock(QStringLiteral("线段B"), QStringLiteral("L2"));

    QUuid pickedBlock;
    QUuid pickedSeg;
    OverlapDisambiguationController ctl(
        &scene, &doc,
        [&](const QUuid& bid, const QUuid& sid) {
            pickedBlock = bid;
            pickedSeg = sid;
        },
        []() {});

    // Collect overlapping segments at (50, 0)
    const auto segCands = ctl.collect(Vec2{50.0, 0.0});
    QCOMPARE(segCands.size(), 2);
    QCOMPARE(segCands[0].kind, OverlapDisambiguationController::Candidate::Kind::Segment);

    // Activate W cycling
    ctl.activate(segCands, segCands[0].blockId, Vec2{50.0, 0.0});
    QCOMPARE(ctl.index(), 0);
    QCOMPARE(pickedBlock, segCands[0].blockId);
    QVERIFY(ctl.hasBattery());
    QCOMPARE(ctl.batteryMode(), OverlapBatteryHud::DisplayMode::Expanded);

    // Cycle to next
    ctl.cycle();
    QCOMPARE(ctl.index(), 1);
    QCOMPARE(pickedBlock, segCands[1].blockId);

    ctl.deactivate();
    QCOMPARE(ctl.index(), -1);
    QVERIFY(!ctl.hasBattery());
}

void TestOverlapBattery::testConnectOverlapResolverLandingPads()
{
    ParamDocument doc;
    CanvasScene scene(&doc);

    // Target block 1
    Block b1;
    b1.name = QStringLiteral("目标片1");
    ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2{50, 50}; p1.resolvedPos = p1.freePos; p1.resolved = true; p1.serial = QStringLiteral("P1");
    ParamPoint p2; p2.constraint = PointConstraint::Free; p2.freePos = Vec2{100, 50}; p2.resolvedPos = p2.freePos; p2.resolved = true; p2.serial = QStringLiteral("P2");
    b1.points = {p1, p2};
    b1.rebuildPointIndex();
    Segment s1; s1.startPointId = b1.points[0].id; s1.endPointId = b1.points[1].id; s1.serial = QStringLiteral("L1");
    b1.segments.push_back(s1);
    const QUuid b1Id = doc.addBlock(std::move(b1));

    // Target block 2 meeting at (50, 50)
    Block b2;
    b2.name = QStringLiteral("目标片2");
    ParamPoint p3; p3.constraint = PointConstraint::Free; p3.freePos = Vec2{50, 50}; p3.resolvedPos = p3.freePos; p3.resolved = true; p3.serial = QStringLiteral("P3");
    ParamPoint p4; p4.constraint = PointConstraint::Free; p4.freePos = Vec2{50, 100}; p4.resolvedPos = p4.freePos; p4.resolved = true; p4.serial = QStringLiteral("P4");
    b2.points = {p3, p4};
    b2.rebuildPointIndex();
    Segment s2; s2.startPointId = b2.points[0].id; s2.endPointId = b2.points[1].id; s2.serial = QStringLiteral("L2");
    b2.segments.push_back(s2);
    const QUuid b2Id = doc.addBlock(std::move(b2));

    const QUuid fromBlockId = QUuid::createUuid();
    ConnectOverlapResolver resolver(&scene, &doc);

    // Collect candidates at (50, 50)
    const auto candidates = resolver.collectConfirmCandidates(Vec2{50.0, 50.0}, fromBlockId);
    QCOMPARE(candidates.size(), 2);

    // Show battery landing pads
    resolver.showBatteryLandingPads(Vec2{50.0, 50.0}, candidates);
    QVERIFY(resolver.hasBatteryLandingPads());

    // Hit test candidates
    const QPointF port0 = resolver.batteryPortScenePos(0, 1.0);
    const QPointF port1 = resolver.batteryPortScenePos(1, 1.0);
    QVERIFY(port0 != port1);

    QCOMPARE(resolver.hitBatteryCandidateAt(port0, 1.0), 0);
    QCOMPARE(resolver.hitBatteryCandidateAt(port1, 1.0), 1);

    resolver.setBatteryHoveredIndex(0);

    // Test candidate highlight switching
    resolver.highlightCandidate(candidates[0].blockId, candidates[0].segId);
    resolver.highlightCandidate(candidates[1].blockId, candidates[1].segId);
    resolver.highlightCandidate(QUuid(), QUuid());

    // Hide pads
    resolver.hideBatteryLandingPads();
    QVERIFY(!resolver.hasBatteryLandingPads());
}

QTEST_MAIN(TestOverlapBattery)
#include "test_overlap_battery.moc"
