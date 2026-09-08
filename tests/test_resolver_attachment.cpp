#include <QtTest>
#include <cmath>
#include <vector>

#include "parametric/Resolver.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/ParamDocument.h"
#include "parametric/MeasureVariable.h"
#include "geometry/Vec2.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

/// Minimal attachment referring only the two blocks (point ids left null).
Attachment makeAtt(const QUuid& from, const QUuid& to)
{
    Attachment att;
    att.fromBlockId = from;
    att.toBlockId = to;
    return att;
}

double normDeg180(double deg)
{
    while (deg >  180.0) deg -= 360.0;
    while (deg <= -180.0) deg += 360.0;
    return deg;
}

} // namespace

class TestResolverAttachment : public QObject
{
    Q_OBJECT

private slots:
    void attachmentSnapsBlocks();
    void attachmentAtLeaderStartContinuesStraight();
    void explicitLeaderSegmentDisambiguates();
    void attachmentFollowerAngleIsCCWFromExitDir();
    void checkAttachmentRejectsDuplicateFollower();
    void checkAttachmentRejectsCycle();
    void checkAttachmentAcceptsChain();
    void connPointRetargetReplacesAttachment();
};

void TestResolverAttachment::attachmentSnapsBlocks()
{
    // Block1 (leader): A(0,0) → B(100,0), segment direction = 0 rad
    // Block2 (follower): C(0,0) → D(50,0), segment direction = 0 rad
    // Attachment: from=Block2 point C, to=Block1 point B, followerAngle=0
    // Expected: Block2's C snaps to Block1's B world position (100,0)
    //           Block2 rotation = 0 (same direction)
    //           Block2 origin = (100,0) - rotated(C_local) = (100,0) - (0,0) = (100,0)

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

    QUuid block1Id = block1.id;
    QUuid block2Id = block2.id;

    Attachment att;
    att.fromBlockId = block2Id;
    att.fromPointId = idC;
    att.toBlockId = block1Id;
    att.toPointId = idB;
    att.followerAngle = 0.0;

    std::vector<Block> blocks;
    blocks.push_back(std::move(block1));
    blocks.push_back(std::move(block2));

    std::vector<Attachment> attachments;
    attachments.push_back(att);

    Resolver::resolveAll(blocks, attachments);

    // Block2 (index 1) should have its origin moved so that point C
    // coincides with Block1's point B at world (100, 0).
    const Block& follower = blocks[1];
    Vec2 cWorld = follower.worldPos(idC);
    QVERIFY(std::abs(cWorld.x - 100.0) < 1e-6);
    QVERIFY(std::abs(cWorld.y - 0.0) < 1e-6);
}

void TestResolverAttachment::attachmentAtLeaderStartContinuesStraight()
{
    // Regression for the construction-angle reference fix: snapping at the
    // leader's START point with followerAngle == 0 must extend the leader's
    // line straight (follower points away from the leader), not fold back
    // over it. The reference is the leader's exit direction at the snapped
    // point (Block::exitDirectionAtPoint), which is flipped at the start.

    // Block1 (leader): A(0,0) -> B(100,0). Exit direction at A = 180 deg.
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

    // Block2 (follower): C(0,0) -> D(50,0), local direction = 0 rad.
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

    QUuid block1Id = block1.id;
    QUuid block2Id = block2.id;

    // Snap the follower's C onto the leader's START point A, followerAngle =
    // 180 (闭合基准 2026-08: 180° = 沿 leader 直行延续)。
    Attachment att;
    att.fromBlockId = block2Id;
    att.fromPointId = idC;
    att.toBlockId = block1Id;
    att.toPointId = idA;
    att.followerAngle = 180.0;

    std::vector<Block> blocks;
    blocks.push_back(std::move(block1));
    blocks.push_back(std::move(block2));

    std::vector<Attachment> attachments;
    attachments.push_back(att);

    Resolver::resolveAll(blocks, attachments);

    const Block& follower = blocks[1];
    // C coincides with A at world (0,0).
    Vec2 cWorld = follower.worldPos(idC);
    QVERIFY(std::abs(cWorld.x) < 1e-6);
    QVERIFY(std::abs(cWorld.y) < 1e-6);
    // D extends the leader line straight past A: world (-50, 0).
    // Under the OLD (buggy) reference this resolved to (+50, 0), folding
    // the follower back over the leader.
    Vec2 dWorld = follower.worldPos(idD);
    QVERIFY(std::abs(dWorld.x + 50.0) < 1e-6);
    QVERIFY(std::abs(dWorld.y) < 1e-6);
}

void TestResolverAttachment::explicitLeaderSegmentDisambiguates()
{
    // Two leader segments share point B. With toSegmentId unset the scan
    // picks the FIRST segment (legacy behaviour); setting toSegmentId to the
    // second segment must pin the construction-angle reference to it.

    // Leader: s1 = A(0,0)->B(100,0), s2 = B(100,0)->C(100,100).
    Block leader;
    leader.name = "Leader";
    ParamPoint pA;
    pA.constraint = PointConstraint::Free;
    pA.freePos = {0.0, 0.0};
    QUuid idA = leader.addPoint(pA);
    ParamPoint pB;
    pB.constraint = PointConstraint::Free;
    pB.freePos = {100.0, 0.0};
    QUuid idB = leader.addPoint(pB);
    ParamPoint pC;
    pC.constraint = PointConstraint::Free;
    pC.freePos = {100.0, 100.0};
    QUuid idC = leader.addPoint(pC);

    Segment s1;
    s1.startPointId = idA;
    s1.endPointId = idB;
    leader.addSegment(s1);
    Segment s2;
    s2.startPointId = idB;
    s2.endPointId = idC;
    QUuid s2Id = leader.addSegment(s2);

    // Follower: E(0,0)->F(50,0), local direction 0 rad; E snaps onto B.
    Block followerBlock;
    followerBlock.name = "Follower";
    ParamPoint pE;
    pE.constraint = PointConstraint::Free;
    pE.freePos = {0.0, 0.0};
    QUuid idE = followerBlock.addPoint(pE);
    ParamPoint pF;
    pF.constraint = PointConstraint::Free;
    pF.freePos = {50.0, 0.0};
    QUuid idF = followerBlock.addPoint(pF);
    Segment fs;
    fs.startPointId = idE;
    fs.endPointId = idF;
    followerBlock.addSegment(fs);

    Attachment att;
    att.fromBlockId = followerBlock.id;
    att.fromPointId = idE;
    att.toBlockId = leader.id;
    att.toPointId = idB;
    att.followerAngle = 180.0;   // 闭合基准: 180° = 直行延续

    // Case 1: legacy (null toSegmentId) — scan finds s1, whose exit direction
    // at its END point B is 0 deg -> F lands at (150, 0).
    {
        std::vector<Block> blocks{leader, followerBlock};
        std::vector<Attachment> atts{att};
        Resolver::resolveAll(blocks, atts);
        Vec2 fWorld = blocks[1].worldPos(idF);
        QVERIFY(std::abs(fWorld.x - 150.0) < 1e-6);
        QVERIFY(std::abs(fWorld.y) < 1e-6);
    }

    // Case 2: explicit s2 — leader benchmark is always startPointId->endPointId
    // (B->C upward, 90 deg): straight ahead means upward, F at (100, +50).
    {
        att.toSegmentId = s2Id;
        std::vector<Block> blocks{leader, followerBlock};
        std::vector<Attachment> atts{att};
        Resolver::resolveAll(blocks, atts);
        Vec2 fWorld = blocks[1].worldPos(idF);
        QVERIFY(std::abs(fWorld.x - 100.0) < 1e-6);
        QVERIFY(std::abs(fWorld.y - 50.0) < 1e-6);
    }
}

void TestResolverAttachment::attachmentFollowerAngleIsCCWFromExitDir()
{
    // followerAngle is measured CCW from the leader's exit direction and is
    // owned by the follower. Snapping at the leader's END point B (exit
    // direction = 0 deg) with followerAngle = 90 puts the follower at +90 deg.

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

    QUuid block1Id = block1.id;
    QUuid block2Id = block2.id;

    Attachment att;
    att.fromBlockId = block2Id;
    att.fromPointId = idC;
    att.toBlockId = block1Id;
    att.toPointId = idB;
    att.followerAngle = 90.0;  // CCW from the leader's exit direction

    std::vector<Block> blocks;
    blocks.push_back(std::move(block1));
    blocks.push_back(std::move(block2));

    std::vector<Attachment> attachments;
    attachments.push_back(att);

    Resolver::resolveAll(blocks, attachments);

    const Block& follower = blocks[1];
    // C coincides with B at world (100, 0).
    Vec2 cWorld = follower.worldPos(idC);
    QVERIFY(std::abs(cWorld.x - 100.0) < 1e-6);
    QVERIFY(std::abs(cWorld.y) < 1e-6);
    // D is rotated +90 deg (CCW): world (100, 50).
    Vec2 dWorld = follower.worldPos(idD);
    QVERIFY(std::abs(dWorld.x - 100.0) < 1e-6);
    QVERIFY(std::abs(dWorld.y - 50.0) < 1e-6);
}

void TestResolverAttachment::checkAttachmentRejectsDuplicateFollower()
{
    // B already follows A; B cannot become the follower of a second leader.
    const QUuid A = QUuid::createUuid();
    const QUuid B = QUuid::createUuid();
    const QUuid C = QUuid::createUuid();

    std::vector<Attachment> existing;
    existing.push_back(makeAtt(B, A));

    QCOMPARE(static_cast<int>(checkAttachment(existing, makeAtt(B, C))),
             static_cast<int>(AttachmentIssue::DuplicateFollower));
}

void TestResolverAttachment::checkAttachmentRejectsCycle()
{
    const QUuid A = QUuid::createUuid();
    const QUuid B = QUuid::createUuid();
    const QUuid C = QUuid::createUuid();

    // Self-attachment is a trivial cycle.
    QCOMPARE(static_cast<int>(checkAttachment({}, makeAtt(A, A))),
             static_cast<int>(AttachmentIssue::Cycle));

    // B follows A; A following B back would close a 2-cycle.
    std::vector<Attachment> pair;
    pair.push_back(makeAtt(B, A));
    QCOMPARE(static_cast<int>(checkAttachment(pair, makeAtt(A, B))),
             static_cast<int>(AttachmentIssue::Cycle));

    // Chain C -> B -> A; A following C would close a 3-cycle.
    std::vector<Attachment> chain;
    chain.push_back(makeAtt(B, A));
    chain.push_back(makeAtt(C, B));
    QCOMPARE(static_cast<int>(checkAttachment(chain, makeAtt(A, C))),
             static_cast<int>(AttachmentIssue::Cycle));
}

void TestResolverAttachment::checkAttachmentAcceptsChain()
{
    const QUuid A = QUuid::createUuid();
    const QUuid B = QUuid::createUuid();
    const QUuid C = QUuid::createUuid();

    // B follows A; extending the chain (C -> B) or attaching a second
    // follower to the same leader (C -> A) are both legal forest shapes.
    std::vector<Attachment> existing;
    existing.push_back(makeAtt(B, A));

    QCOMPARE(static_cast<int>(checkAttachment(existing, makeAtt(C, B))),
             static_cast<int>(AttachmentIssue::Ok));
    QCOMPARE(static_cast<int>(checkAttachment(existing, makeAtt(C, A))),
             static_cast<int>(AttachmentIssue::Ok));
}

// Replicates LinePropertyDialog::onConnPointResolved — editing the 指向点
// (connection point) in the connected state RETARGETS the existing follower
// attachment IN PLACE to the new leader point (the dialog mutates the stored
// attachment, it does not add a new one), back-solving the follower angle
// so the world direction is preserved. After resolve the line's start must sit
// on the NEW host point.
void TestResolverAttachment::connPointRetargetReplacesAttachment()
{
    ParamDocument doc;

    // Host A: segment (0,0)-(100,0); two candidate leader points (end & start).
    Block hostA;
    ParamPoint a0; a0.constraint = PointConstraint::Free; a0.freePos = {0.0, 0.0};
    ParamPoint a1; a1.constraint = PointConstraint::Free; a1.freePos = {100.0, 0.0};
    QUuid idA0 = hostA.addPoint(a0);
    QUuid idA1 = hostA.addPoint(a1);
    Segment segA; segA.startPointId = idA0; segA.endPointId = idA1;
    QUuid segAId = segA.id;
    hostA.addSegment(std::move(segA));
    QUuid hostAId = hostA.id;
    doc.addBlock(std::move(hostA));

    // Host B at world (400,300).
    Block hostB;
    ParamPoint b0; b0.constraint = PointConstraint::Free; b0.freePos = {0.0, 0.0};
    ParamPoint b1; b1.constraint = PointConstraint::Free; b1.freePos = {100.0, 0.0};
    QUuid idB0 = hostB.addPoint(b0);
    QUuid idB1 = hostB.addPoint(b1);
    Segment segB; segB.startPointId = idB0; segB.endPointId = idB1;
    hostB.addSegment(std::move(segB));
    hostB.transform.origin = {400.0, 300.0};
    QUuid hostBId = hostB.id;
    doc.addBlock(std::move(hostB));

    // Measure line (SmartPen style): start Free(0,0), end Polar length=M_test,
    // start follows host A end point, end aims at host B start point.
    Block line;
    line.transform.origin = {100.0, 0.0};
    line.transform.rotation = std::atan2(300.0, 300.0);
    ParamPoint ls; ls.constraint = PointConstraint::Free; ls.freePos = {0.0, 0.0};
    QUuid idLs = line.addPoint(ls);
    ParamPoint le; le.constraint = PointConstraint::Polar;
    le.refPointId = idLs; le.distance = 1.0; le.distanceFormula = "M_test"; le.angle = 0.0;
    QUuid idLe = line.addPoint(le);
    Segment segL; segL.startPointId = idLs; segL.endPointId = idLe;
    segL.lengthFormula = "M_test";
    QUuid segLId = segL.id;
    line.addSegment(std::move(segL));
    line.endTargetBlockId = hostBId;
    line.endTargetPointId = idB0;
    QUuid lineId = line.id;
    doc.addBlock(std::move(line));

    // Start-follow attachment to host A END point (idA1), like SmartPen default.
    Attachment att;
    att.fromBlockId = lineId;
    att.fromPointId = idLs;
    att.toBlockId = hostAId;
    att.toPointId = idA1;
    att.toSegmentId = segAId;
    att.followerAngle = 0.0;
    QVERIFY(doc.addAttachment(att));

    MeasureVariable mv;
    mv.blockA = hostAId; mv.pointA = idA1;
    mv.blockB = hostBId; mv.pointB = idB0;
    mv.value = std::sqrt(300.0 * 300.0 + 300.0 * 300.0);
    mv.refName = "M_test";
    doc.addMeasure(std::move(mv));

    doc.resolveAll();

    // Baseline: start on host A end (100,0), end on host B (400,300).
    {
        const Block* rl = doc.findBlock(lineId);
        Vec2 s = rl->worldPos(idLs);
        Vec2 e = rl->worldPos(idLe);
        QVERIFY(std::abs(s.x - 100.0) < 1e-6);
        QVERIFY(std::abs(s.y -   0.0) < 1e-6);
        QVERIFY(std::abs(e.x - 400.0) < 1e-6);
        QVERIFY(std::abs(e.y - 300.0) < 1e-6);
    }

    // --- Replicate onConnPointResolved: retarget 指向点 to host A START (idA0) ---
    // The real dialog mutates the existing follower attachment IN PLACE (it does
    // NOT addAttachment), then back-solves the follower angle.
    Block* lineBlk = doc.findBlock(lineId);
    Segment* seg = lineBlk->findSegment(segLId);
    const Block* leader = doc.findBlock(hostAId);

    // Locate the mutable follower attachment (same as the dialog).
    Attachment* att2 = nullptr;
    for (auto& a : const_cast<std::vector<Attachment>&>(doc.attachments()))
        if (!a.isPin && a.fromBlockId == lineId) { att2 = &a; break; }
    QVERIFY(att2 != nullptr);

    att2->toBlockId = hostAId;
    att2->toPointId = idA0;   // NEW connection point
    att2->toSegmentId = leader->exitSegmentAtPoint(idA0);
    const double refWorld = leader->transform.rotation
        + leader->exitDirectionAtPoint(idA0, att2->toSegmentId);
    const double localDir = lineBlk->directionAtPoint(seg->startPointId);
    att2->followerAngle = normDeg180(
        (lineBlk->transform.rotation + localDir - refWorld) * 180.0 / M_PI);
    att2->followerAngleFormula.clear();

    doc.resolveAll();

    // After a successful re-target the start must sit on the NEW host point (0,0).
    {
        const Block* rl = doc.findBlock(lineId);
        Vec2 s = rl->worldPos(idLs);
        QVERIFY(std::abs(s.x - 0.0) < 1e-6);
        QVERIFY(std::abs(s.y - 0.0) < 1e-6);
    }
}

QTEST_GUILESS_MAIN(TestResolverAttachment)
#include "test_resolver_attachment.moc"
