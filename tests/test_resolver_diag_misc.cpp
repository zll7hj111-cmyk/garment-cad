#include <QtTest>
#include <QHash>
#include <QString>
#include <cmath>
#include <vector>

#include "parametric/Resolver.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/ParamDocument.h"
#include "geometry/Vec2.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

Attachment makeAtt(const QUuid& from, const QUuid& to)
{
    Attachment att;
    att.fromBlockId = from;
    att.toBlockId = to;
    return att;
}

struct AimBlock {
    Block block;
    QUuid idOrigin;
    QUuid idEnd;
};

AimBlock makeAimBlock(const Vec2& origin, double len)
{
    AimBlock ab;
    ab.block.transform.origin = origin;
    ab.block.transform.rotation = 0.0;

    ParamPoint o; o.constraint = PointConstraint::Free; o.freePos = {0.0, 0.0};
    ab.idOrigin = ab.block.addPoint(o);
    ParamPoint e; e.constraint = PointConstraint::Free; e.freePos = {len, 0.0};
    ab.idEnd = ab.block.addPoint(e);

    Segment seg; seg.startPointId = ab.idOrigin; seg.endPointId = ab.idEnd;
    ab.block.addSegment(std::move(seg));
    return ab;
}

} // namespace

class TestResolverDiagMisc : public QObject
{
    Q_OBJECT

private slots:
    void danglingAttachmentIsReported();
    void danglingPointIsReported();
    void healthyForestHasNoDiagnostics();
    void bridgePinsFollowBothHosts();
    void bridgePinsAcceptedByGraph();
    void measureLineFollowsHostsWhenFollowing();
    void endTargetRotationDoesNotOffsetFollowers();
    void aimRingExhaustingBudgetReportsNotConverged();
    void oneWayAimReportsNoDiagnostics();
    void structureEpochTracksBlockMutations();
    void blockPointerMayLeaveStorageAfterGrowth();
};

void TestResolverDiagMisc::danglingAttachmentIsReported()
{
    // The attachment references a from-block that does not exist.
    Block block;
    block.name = "Leader";
    ParamPoint pA;
    pA.name = "A";
    pA.constraint = PointConstraint::Free;
    pA.freePos = {0.0, 0.0};
    QUuid idA = block.addPoint(pA);

    std::vector<Block> blocks;
    blocks.push_back(std::move(block));

    Attachment att;
    att.fromBlockId = QUuid::createUuid();  // no such block
    att.fromPointId = QUuid::createUuid();
    att.toBlockId = blocks[0].id;
    att.toPointId = idA;

    std::vector<Attachment> attachments;
    attachments.push_back(att);

    std::vector<ResolveDiagnostic> diags;
    Resolver::resolveAll(blocks, attachments, {}, {}, &diags);

    QCOMPARE(static_cast<int>(diags.size()), 1);
    QCOMPARE(static_cast<int>(diags[0].kind),
             static_cast<int>(ResolveDiagnostic::Kind::DanglingBlock));
    QCOMPARE(diags[0].attachmentId, att.id);
}

void TestResolverDiagMisc::danglingPointIsReported()
{
    // Both blocks exist, but the attachment snaps to a missing point.
    Block block1;
    block1.name = "Leader";
    ParamPoint pA;
    pA.name = "A";
    pA.constraint = PointConstraint::Free;
    pA.freePos = {0.0, 0.0};
    block1.addPoint(pA);

    Block block2;
    block2.name = "Follower";
    ParamPoint pC;
    pC.name = "C";
    pC.constraint = PointConstraint::Free;
    pC.freePos = {0.0, 0.0};
    QUuid idC = block2.addPoint(pC);

    const QUuid block1Id = block1.id;
    const QUuid block2Id = block2.id;

    std::vector<Block> blocks;
    blocks.push_back(std::move(block1));
    blocks.push_back(std::move(block2));

    Attachment att;
    att.fromBlockId = block2Id;
    att.fromPointId = idC;
    att.toBlockId = block1Id;
    att.toPointId = QUuid::createUuid();  // no such point on the leader

    std::vector<Attachment> attachments;
    attachments.push_back(att);

    std::vector<ResolveDiagnostic> diags;
    Resolver::resolveAll(blocks, attachments, {}, {}, &diags);

    QCOMPARE(static_cast<int>(diags.size()), 1);
    QCOMPARE(static_cast<int>(diags[0].kind),
             static_cast<int>(ResolveDiagnostic::Kind::DanglingPoint));
    QCOMPARE(diags[0].attachmentId, att.id);
}

void TestResolverDiagMisc::healthyForestHasNoDiagnostics()
{
    // A well-formed two-block attachment resolves cleanly: the iteration
    // settles and no diagnostic is produced.
    Block block1;
    block1.name = "Leader";
    ParamPoint pA;
    pA.name = "A";
    pA.constraint = PointConstraint::Free;
    pA.freePos = {0.0, 0.0};
    QUuid idA = block1.addPoint(pA);

    ParamPoint pB;
    pB.name = "B";
    pB.constraint = PointConstraint::Free;
    pB.freePos = {100.0, 0.0};
    QUuid idB = block1.addPoint(pB);

    Segment seg1;
    seg1.startPointId = idA;
    seg1.endPointId = idB;
    block1.addSegment(seg1);

    Block block2;
    block2.name = "Follower";
    ParamPoint pC;
    pC.name = "C";
    pC.constraint = PointConstraint::Free;
    pC.freePos = {0.0, 0.0};
    QUuid idC = block2.addPoint(pC);

    ParamPoint pD;
    pD.name = "D";
    pD.constraint = PointConstraint::Free;
    pD.freePos = {50.0, 0.0};
    QUuid idD = block2.addPoint(pD);

    Segment seg2;
    seg2.startPointId = idC;
    seg2.endPointId = idD;
    block2.addSegment(seg2);

    const QUuid block1Id = block1.id;
    const QUuid block2Id = block2.id;

    Attachment att;
    att.fromBlockId = block2Id;
    att.fromPointId = idC;
    att.toBlockId = block1Id;
    att.toPointId = idB;
    att.followerAngle = 30.0;

    std::vector<Block> blocks;
    blocks.push_back(std::move(block1));
    blocks.push_back(std::move(block2));

    std::vector<Attachment> attachments;
    attachments.push_back(att);

    std::vector<ResolveDiagnostic> diags;
    Resolver::resolveAll(blocks, attachments, {}, {}, &diags);

    QVERIFY(diags.empty());
}

void TestResolverDiagMisc::bridgePinsFollowBothHosts()
{
    // Two independent host blocks and a bridge whose two endpoints are pinned
    // to one point on each host. The bridge endpoints must land exactly on
    // their host points (passive length), and no NotConverged is reported
    // (pins must be skipped by the rigid-transform loop).

    // Host 1: single free point P at world (0,0).
    Block host1;
    host1.name = "Host1";
    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = {0.0, 0.0};
    QUuid idP1 = host1.addPoint(p1);
    host1.transform.origin = {10.0, 20.0};  // P world = (10,20)

    // Host 2: single free point Q at world (0,0) + origin (200, 60) => (200,60).
    Block host2;
    host2.name = "Host2";
    ParamPoint p2;
    p2.constraint = PointConstraint::Free;
    p2.freePos = {0.0, 0.0};
    QUuid idP2 = host2.addPoint(p2);
    host2.transform.origin = {200.0, 60.0};  // Q world = (200,60)

    // Bridge: start (Free, local 0,0) + end (Polar initial guess). Both pins
    // will overwrite the resolved positions.
    Block bridge;
    bridge.name = "Bridge";
    bridge.isBridge = true;
    bridge.transform.origin = {10.0, 20.0};
    ParamPoint bs;
    bs.constraint = PointConstraint::Free;
    bs.freePos = {0.0, 0.0};
    QUuid idBs = bridge.addPoint(bs);
    ParamPoint be;
    be.constraint = PointConstraint::Polar;
    be.refPointId = idBs;
    be.distance = 1.0;   // deliberately wrong; resolver overrides
    be.angle = 0.0;
    QUuid idBe = bridge.addPoint(be);
    Segment bseg;
    bseg.startPointId = idBs;
    bseg.endPointId = idBe;
    bridge.addSegment(bseg);

    const QUuid host1Id = host1.id;
    const QUuid host2Id = host2.id;
    const QUuid bridgeId = bridge.id;

    Attachment pinStart;
    pinStart.isPin = true;
    pinStart.fromBlockId = bridgeId;
    pinStart.fromPointId = idBs;
    pinStart.toBlockId = host1Id;
    pinStart.toPointId = idP1;

    Attachment pinEnd;
    pinEnd.isPin = true;
    pinEnd.fromBlockId = bridgeId;
    pinEnd.fromPointId = idBe;
    pinEnd.toBlockId = host2Id;
    pinEnd.toPointId = idP2;

    std::vector<Block> blocks;
    blocks.push_back(std::move(host1));
    blocks.push_back(std::move(host2));
    blocks.push_back(std::move(bridge));

    std::vector<Attachment> attachments{pinStart, pinEnd};

    std::vector<ResolveDiagnostic> diags;
    Resolver::resolveAll(blocks, attachments, {}, {}, &diags);
    QVERIFY(diags.empty());

    const Block& rb = blocks[2];
    Vec2 startWorld = rb.worldPos(idBs);
    Vec2 endWorld   = rb.worldPos(idBe);
    QVERIFY(std::abs(startWorld.x - 10.0) < 1e-6);
    QVERIFY(std::abs(startWorld.y - 20.0) < 1e-6);
    QVERIFY(std::abs(endWorld.x - 200.0) < 1e-6);
    QVERIFY(std::abs(endWorld.y - 60.0) < 1e-6);
}

void TestResolverDiagMisc::bridgePinsAcceptedByGraph()
{
    // The forest check must accept exactly two pins on the same follower
    // (bridge), reject a third, and reject mixing a pin with a regular
    // attachment on the same follower.
    const QUuid bridgeId = QUuid::createUuid();
    const QUuid hostA = QUuid::createUuid();
    const QUuid hostB = QUuid::createUuid();
    const QUuid hostC = QUuid::createUuid();

    auto pin = [&](const QUuid& to) {
        Attachment a;
        a.isPin = true;
        a.fromBlockId = bridgeId;
        a.toBlockId = to;
        return a;
    };

    std::vector<Attachment> existing;
    // First pin: OK on an empty graph.
    QCOMPARE(static_cast<int>(checkAttachment(existing, pin(hostA))),
             static_cast<int>(AttachmentIssue::Ok));
    existing.push_back(pin(hostA));
    // Second pin: OK (bridge endpoints).
    QCOMPARE(static_cast<int>(checkAttachment(existing, pin(hostB))),
             static_cast<int>(AttachmentIssue::Ok));
    existing.push_back(pin(hostB));
    // Third pin: rejected.
    QCOMPARE(static_cast<int>(checkAttachment(existing, pin(hostC))),
             static_cast<int>(AttachmentIssue::DuplicateFollower));
    // A regular attachment mixing with existing pins: rejected.
    QCOMPARE(static_cast<int>(checkAttachment(existing, makeAtt(bridgeId, hostC))),
             static_cast<int>(AttachmentIssue::DuplicateFollower));
}

void TestResolverDiagMisc::measureLineFollowsHostsWhenFollowing()
{
    // 桥接线（自由线 + 测量变量）开启“起点跟随 + 终点指向”后必须随宿主点联动：
    //   起点 Attachment（位置）+ endTarget（方向）+ 长度 = M_xxx（距离）
    //   ⇒ 两端精确落在宿主点上，无论宿主搬到哪里。

    // Host A: segment (0,0)-(100,0), host point = its end at world (100, 0).
    Block hostA;
    ParamPoint a0; a0.constraint = PointConstraint::Free; a0.freePos = {0.0, 0.0};
    ParamPoint a1; a1.constraint = PointConstraint::Free; a1.freePos = {100.0, 0.0};
    QUuid idA0 = hostA.addPoint(a0);
    QUuid idA1 = hostA.addPoint(a1);
    Segment segA; segA.startPointId = idA0; segA.endPointId = idA1;
    QUuid segAId = segA.id;
    hostA.addSegment(std::move(segA));

    // Host B: origin (400,300), host point = its start at world (400, 300).
    Block hostB;
    ParamPoint b0; b0.constraint = PointConstraint::Free; b0.freePos = {0.0, 0.0};
    ParamPoint b1; b1.constraint = PointConstraint::Free; b1.freePos = {100.0, 0.0};
    QUuid idB0 = hostB.addPoint(b0);
    hostB.addPoint(b1);
    Segment segB; segB.startPointId = idB0; segB.endPointId = hostB.points.back().id;
    hostB.addSegment(std::move(segB));
    hostB.transform.origin = {400.0, 300.0};

    // Measure line: start Free(0,0), end Polar with distanceFormula = "M_test"
    // (cm domain, like a MeasureVariable refName). Transform is a deliberately
    // stale creation-time snapshot — following must overwrite it.
    Block line;
    line.transform.origin = {999.0, -999.0};
    line.transform.rotation = 0.7;
    ParamPoint ls; ls.constraint = PointConstraint::Free; ls.freePos = {0.0, 0.0};
    QUuid idLs = line.addPoint(ls);
    ParamPoint le; le.constraint = PointConstraint::Polar;
    le.refPointId = idLs; le.distance = 1.0; le.distanceFormula = "M_test"; le.angle = 0.0;
    QUuid idLe = line.addPoint(le);
    Segment segL; segL.startPointId = idLs; segL.endPointId = idLe;
    line.addSegment(std::move(segL));

    // 起点跟随: follower attachment start → host A end point.
    Attachment att;
    att.fromBlockId = line.id;
    att.fromPointId = idLs;
    att.toBlockId = hostA.id;
    att.toPointId = idA1;
    att.toSegmentId = segAId;
    att.followerAngle = 0.0;

    // 终点指向: aim the segment end at host B's point.
    line.endTargetBlockId = hostB.id;
    line.endTargetPointId = idB0;

    std::vector<Block> blocks;
    blocks.push_back(std::move(hostA));
    blocks.push_back(std::move(hostB));
    blocks.push_back(std::move(line));
    std::vector<Attachment> attachments{att};

    // M_test = |(100,0)-(400,300)| = √(300²+300²) mm, published in cm.
    QHash<QString, double> params;
    params.insert("M_test", std::sqrt(300.0 * 300.0 + 300.0 * 300.0) / 10.0);
    Resolver::resolveAll(blocks, attachments, params);

    {
        const Block& rl = blocks[2];
        Vec2 startWorld = rl.worldPos(idLs);
        Vec2 endWorld   = rl.worldPos(idLe);
        QVERIFY(std::abs(startWorld.x - 100.0) < 1e-6);
        QVERIFY(std::abs(startWorld.y -   0.0) < 1e-6);
        QVERIFY(std::abs(endWorld.x - 400.0) < 1e-6);
        QVERIFY(std::abs(endWorld.y - 300.0) < 1e-6);
    }

    // Move host B (as if a formula variable changed) and refresh the measure
    // value — the line must swing and stretch onto the new host position.
    blocks[1].transform.origin = {600.0, 100.0};
    params.insert("M_test", std::sqrt(500.0 * 500.0 + 100.0 * 100.0) / 10.0);
    Resolver::resolveAll(blocks, attachments, params);

    {
        const Block& rl = blocks[2];
        Vec2 startWorld = rl.worldPos(idLs);
        Vec2 endWorld   = rl.worldPos(idLe);
        QVERIFY(std::abs(startWorld.x - 100.0) < 1e-6);
        QVERIFY(std::abs(startWorld.y -   0.0) < 1e-6);
        QVERIFY(std::abs(endWorld.x - 600.0) < 1e-6);
        QVERIFY(std::abs(endWorld.y - 100.0) < 1e-6);
    }
}

void TestResolverDiagMisc::endTargetRotationDoesNotOffsetFollowers()
{
    // Host H: provides the aim target point T at world (300, 100).
    Block host;
    ParamPoint t; t.constraint = PointConstraint::Free; t.freePos = {300.0, 100.0};
    QUuid idT = host.addPoint(t);

    // Block A: anchor at the local origin, a NON-origin point PA, and a segment
    // whose end aims at T. A carries the endTarget.
    Block a;
    a.transform.origin = {0.0, 0.0};
    a.transform.rotation = 0.0;
    ParamPoint o; o.constraint = PointConstraint::Free; o.freePos = {0.0, 0.0};
    QUuid idO = a.addPoint(o);
    ParamPoint e; e.constraint = PointConstraint::Free; e.freePos = {40.0, 0.0};
    QUuid idE = a.addPoint(e);
    ParamPoint pa; pa.constraint = PointConstraint::Free; pa.freePos = {50.0, 30.0};
    QUuid idPA = a.addPoint(pa);
    Segment segA; segA.startPointId = idO; segA.endPointId = idE;
    a.addSegment(std::move(segA));
    a.endTargetBlockId = host.id;
    a.endTargetPointId = idT;

    // Block B: follower whose start PB attaches to PA (NOT A's local origin).
    Block b;
    ParamPoint pb; pb.constraint = PointConstraint::Free; pb.freePos = {0.0, 0.0};
    QUuid idPB = b.addPoint(pb);
    ParamPoint q; q.constraint = PointConstraint::Free; q.freePos = {20.0, 0.0};
    QUuid idQ = b.addPoint(q);
    Segment segB; segB.startPointId = idPB; segB.endPointId = idQ;
    b.addSegment(std::move(segB));

    Attachment att;
    att.fromBlockId = b.id;
    att.fromPointId = idPB;
    att.toBlockId = a.id;
    att.toPointId = idPA;
    att.followerAngle = 0.0;

    std::vector<Block> blocks;
    blocks.push_back(std::move(host));
    blocks.push_back(std::move(a));
    blocks.push_back(std::move(b));
    std::vector<Attachment> attachments{att};

    Resolver::resolveAll(blocks, attachments);

    const Block& ra = blocks[1];
    const Block& rb = blocks[2];
    Vec2 wPA = ra.worldPos(idPA);
    Vec2 wPB = rb.worldPos(idPB);

    // Sanity: the aim actually rotated A, so PA left its creation-time world
    // position (50, 30) — the test genuinely exercises the re-settle path.
    QVERIFY(std::abs(wPA.x - 50.0) > 1e-3 || std::abs(wPA.y - 30.0) > 1e-3);

    // B's start must coincide with A's PA after the aim rotation moved PA.
    QVERIFY(std::abs(wPA.x - wPB.x) < 1e-6);
    QVERIFY(std::abs(wPA.y - wPB.y) < 1e-6);
}

// Two blocks aiming at EACH OTHER's endpoint form a ring: every Step-7 pass
// re-aims both blocks, and the iteration only creeps toward its fixed point
// (~0.15°/round left after 3 rounds for the offsets used here). The
// kMaxSettleRounds budget therefore runs out while the poses are STILL MOVING —
// which must be reported as NotConverged rather than silently shipping a pose
// that is one rotation short of the fixed point (P1-4: 未收敛可观测化).
void TestResolverDiagMisc::aimRingExhaustingBudgetReportsNotConverged()
{
    AimBlock a = makeAimBlock({0.0, 0.0}, 40.0);
    AimBlock b = makeAimBlock({100.0, 0.0}, 40.0);

    // Mutual aims with asymmetric offsets so the ring never lands exactly on a
    // fixed point within the budget.
    a.block.endTargetBlockId = b.block.id;
    a.block.endTargetPointId = b.idEnd;
    a.block.endTargetOffset = 30.0;
    b.block.endTargetBlockId = a.block.id;
    b.block.endTargetPointId = a.idEnd;
    b.block.endTargetOffset = 20.0;

    std::vector<Block> blocks;
    blocks.push_back(std::move(a.block));
    blocks.push_back(std::move(b.block));
    std::vector<Attachment> attachments;

    std::vector<ResolveDiagnostic> diags;
    Resolver::resolveAll(blocks, attachments, {}, {}, &diags);

    // Sanity: the ring really rotated both blocks (the budget was consumed by
    // movement, not by a no-op loop).
    QVERIFY(std::abs(blocks[0].transform.rotation) > 1e-6);
    QVERIFY(std::abs(blocks[1].transform.rotation) > 1e-6);

    // Budget exhausted → non-convergence is observable, and reported ONCE (the
    // dedup in report() keeps repeated reports out of the badge count).
    int notConverged = 0;
    for (const auto& d : diags)
        if (d.kind == ResolveDiagnostic::Kind::NotConverged) ++notConverged;
    QVERIFY(notConverged == 1);
}

// Control: the same two blocks with a ONE-WAY aim (A aims at B, B is static)
// reach their fixed point in the first round, so a healthy document must stay
// free of diagnostics — the new exhaustion report must not cry wolf.
void TestResolverDiagMisc::oneWayAimReportsNoDiagnostics()
{
    AimBlock a = makeAimBlock({0.0, 0.0}, 40.0);
    AimBlock b = makeAimBlock({100.0, 0.0}, 40.0);
    a.block.endTargetBlockId = b.block.id;
    a.block.endTargetPointId = b.idEnd;
    a.block.endTargetOffset = 30.0;

    std::vector<Block> blocks;
    blocks.push_back(std::move(a.block));
    blocks.push_back(std::move(b.block));
    std::vector<Attachment> attachments;

    std::vector<ResolveDiagnostic> diags;
    Resolver::resolveAll(blocks, attachments, {}, {}, &diags);
    QVERIFY(diags.empty());
}

void TestResolverDiagMisc::structureEpochTracksBlockMutations()
{
    ParamDocument doc;

    Block b1;
    ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = {0.0, 0.0};
    const QUuid s1 = b1.addPoint(p1);
    ParamPoint e1; e1.constraint = PointConstraint::Free; e1.freePos = {100.0, 0.0};
    const QUuid e1id = b1.addPoint(e1);
    Segment seg1; seg1.startPointId = s1; seg1.endPointId = e1id;
    b1.addSegment(seg1);
    const QUuid id1 = doc.addBlock(std::move(b1));
    Q_UNUSED(id1);

    const quint64 afterAdd = doc.structureEpoch();
    QVERIFY(afterAdd > 0);

    // A resolve pass (and any property-only edit) must NOT touch the epoch:
    // the set of blocks is unchanged, so caches keyed on it stay valid.
    doc.resolveAll();
    Block* b = doc.findBlock(id1);
    QVERIFY(b);
    b->name = QStringLiteral("renamed");
    b->touchGeometry();
    QCOMPARE(doc.structureEpoch(), afterAdd);

    // A structural mutation must bump it (add ...).
    Block b2;
    ParamPoint p2; p2.constraint = PointConstraint::Free; p2.freePos = {0.0, 0.0};
    const QUuid s2 = b2.addPoint(p2);
    ParamPoint e2; e2.constraint = PointConstraint::Free; e2.freePos = {50.0, 0.0};
    const QUuid e2id = b2.addPoint(e2);
    Segment seg2; seg2.startPointId = s2; seg2.endPointId = e2id;
    b2.addSegment(seg2);
    const QUuid id2 = doc.addBlock(std::move(b2));
    QVERIFY(doc.structureEpoch() > afterAdd);

    // ... and remove.
    const quint64 afterAdd2 = doc.structureEpoch();
    doc.removeBlock(id2);
    QVERIFY(doc.structureEpoch() > afterAdd2);

    // clear() counts as a structural change too.
    const quint64 afterRemove = doc.structureEpoch();
    doc.clear();
    QVERIFY(doc.structureEpoch() > afterRemove);
}

// The debug range check detects the classic "held a Block* across addBlock()
// and the vector reallocated" bug. Release builds compile it to `true`, so the
// assertion is conditional.
void TestResolverDiagMisc::blockPointerMayLeaveStorageAfterGrowth()
{
    ParamDocument doc;

    Block first;
    ParamPoint p; p.constraint = PointConstraint::Free; p.freePos = {0.0, 0.0};
    const QUuid sp = first.addPoint(p);
    ParamPoint e; e.constraint = PointConstraint::Free; e.freePos = {10.0, 0.0};
    const QUuid ep = first.addPoint(e);
    Segment seg; seg.startPointId = sp; seg.endPointId = ep;
    first.addSegment(seg);
    const QUuid firstId = doc.addBlock(std::move(first));

    Block* held = doc.findBlock(firstId);
    QVERIFY(held);
    QVERIFY(doc.blockPointerInRange(held));

    // Grow the vector well past any reserved capacity so it must reallocate.
    for (int i = 0; i < 200; ++i) {
        Block filler;
        ParamPoint fp; fp.constraint = PointConstraint::Free; fp.freePos = {0.0, 0.0};
        const QUuid fs = filler.addPoint(fp);
        ParamPoint fe; fe.constraint = PointConstraint::Free; fe.freePos = {5.0, 0.0};
        const QUuid feid = filler.addPoint(fe);
        Segment fseg; fseg.startPointId = fs; fseg.endPointId = feid;
        filler.addSegment(fseg);
        doc.addBlock(std::move(filler));
    }

#ifndef NDEBUG
    // Either the stale pointer is out of the current storage, or the allocator
    // happened to hand the new buffer the same address (in which case it still
    // denotes the same block) — both observations are consistent,never neither.
    QVERIFY(!doc.blockPointerInRange(held)
            || held == doc.findBlock(firstId));
#endif

    // The safe pattern: re-fetch after any mutation.
    QVERIFY(doc.findBlock(firstId) != nullptr);
    QVERIFY(doc.blockPointerInRange(doc.findBlock(firstId)));
}

QTEST_GUILESS_MAIN(TestResolverDiagMisc)
#include "test_resolver_diag_misc.moc"
