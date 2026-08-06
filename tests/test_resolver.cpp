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
#include "parametric/MeasureVariable.h"
#include "geometry/Vec2.h"
#include "geometry/Angle.h"

using namespace cad::param;
using cad::geo::Vec2;

class TestResolver : public QObject
{
    Q_OBJECT

private slots:
    void freePointsResolve();
    void polarConstraintResolve();
    void polarWithFormula();
    void midpointResolve();
    void attachmentSnapsBlocks();
    void attachmentAtLeaderStartContinuesStraight();
    void explicitLeaderSegmentDisambiguates();
    void attachmentFollowerAngleIsCCWFromExitDir();
    void checkAttachmentRejectsDuplicateFollower();
    void checkAttachmentRejectsCycle();
    void checkAttachmentAcceptsChain();
    void danglingAttachmentIsReported();
    void danglingPointIsReported();
    void healthyForestHasNoDiagnostics();
    void bridgePinsFollowBothHosts();
    void bridgePinsAcceptedByGraph();
    void interpolatedPercentAndConstantAdd();
    void measureLineFollowsHostsWhenFollowing();
    void connPointRetargetReplacesAttachment();
    void endTargetRotationDoesNotOffsetFollowers();
    void curveAnchorResolvesOnChord();
    void curveAnchorFollowsEndpoints();
    void curveAnchorKeepsFullOffsetNearEndpoint();
    void arcLengthZeroMeansReverse180();
    void arcLengthFullTurnSameAsAngleZero();
    void arcLengthQuarterTurnMatchesAngle270();
};

void TestResolver::freePointsResolve()
{
    // A block with two free points should resolve to their freePos.
    Block block;
    block.name = "TestBlock";

    ParamPoint p1;
    p1.name = "A";
    p1.constraint = PointConstraint::Free;
    p1.freePos = {0.0, 0.0};
    QUuid id1 = block.addPoint(p1);

    ParamPoint p2;
    p2.name = "B";
    p2.constraint = PointConstraint::Free;
    p2.freePos = {100.0, 50.0};
    QUuid id2 = block.addPoint(p2);

    std::vector<Block> blocks;
    blocks.push_back(std::move(block));

    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments);

    const Block& resolved = blocks[0];
    const ParamPoint* rp1 = resolved.findPoint(id1);
    const ParamPoint* rp2 = resolved.findPoint(id2);

    QVERIFY(rp1 && rp1->resolved);
    QVERIFY(rp2 && rp2->resolved);
    QCOMPARE(rp1->resolvedPos.x, 0.0);
    QCOMPARE(rp1->resolvedPos.y, 0.0);
    QCOMPARE(rp2->resolvedPos.x, 100.0);
    QCOMPARE(rp2->resolvedPos.y, 50.0);
}

void TestResolver::polarConstraintResolve()
{
    // Point B is polar from A: distance=100mm, angle=0 degrees → B = (100, 0)
    Block block;
    block.name = "PolarBlock";

    ParamPoint pA;
    pA.name = "A";
    pA.constraint = PointConstraint::Free;
    pA.freePos = {0.0, 0.0};
    QUuid idA = block.addPoint(pA);

    ParamPoint pB;
    pB.name = "B";
    pB.constraint = PointConstraint::Polar;
    pB.refPointId = idA;
    pB.distance = 100.0;  // mm
    pB.angle = 0.0;       // degrees
    QUuid idB = block.addPoint(pB);

    std::vector<Block> blocks;
    blocks.push_back(std::move(block));

    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments);

    const ParamPoint* rpB = blocks[0].findPoint(idB);
    QVERIFY(rpB && rpB->resolved);
    QVERIFY(std::abs(rpB->resolvedPos.x - 100.0) < 1e-9);
    QVERIFY(std::abs(rpB->resolvedPos.y - 0.0) < 1e-9);
}

void TestResolver::polarWithFormula()
{
    // Point B uses a distance formula "hip/4+2" with hip=92 (cm)
    // Result: 92/4+2 = 25 cm → 250 mm
    Block block;
    block.name = "FormulaBlock";

    ParamPoint pA;
    pA.name = "A";
    pA.constraint = PointConstraint::Free;
    pA.freePos = {0.0, 0.0};
    QUuid idA = block.addPoint(pA);

    ParamPoint pB;
    pB.name = "B";
    pB.constraint = PointConstraint::Polar;
    pB.refPointId = idA;
    pB.distance = 0.0;  // overridden by formula
    pB.distanceFormula = "hip/4+2";
    pB.angle = 0.0;
    QUuid idB = block.addPoint(pB);

    QHash<QString, double> params;
    params["hip"] = 92.0;  // cm

    std::vector<Block> blocks;
    blocks.push_back(std::move(block));

    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments, params);

    const ParamPoint* rpB = blocks[0].findPoint(idB);
    QVERIFY(rpB && rpB->resolved);
    // 25 cm = 250 mm along angle 0 → x=250, y=0
    QVERIFY(std::abs(rpB->resolvedPos.x - 250.0) < 1e-9);
    QVERIFY(std::abs(rpB->resolvedPos.y - 0.0) < 1e-9);
}

void TestResolver::midpointResolve()
{
    // Point C is midpoint of A(0,0) and B(100, 200) → C = (50, 100)
    Block block;
    block.name = "MidBlock";

    ParamPoint pA;
    pA.name = "A";
    pA.constraint = PointConstraint::Free;
    pA.freePos = {0.0, 0.0};
    QUuid idA = block.addPoint(pA);

    ParamPoint pB;
    pB.name = "B";
    pB.constraint = PointConstraint::Free;
    pB.freePos = {100.0, 200.0};
    QUuid idB = block.addPoint(pB);

    ParamPoint pC;
    pC.name = "C";
    pC.constraint = PointConstraint::Midpoint;
    pC.refPointA = idA;
    pC.refPointB = idB;
    QUuid idC = block.addPoint(pC);

    std::vector<Block> blocks;
    blocks.push_back(std::move(block));

    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments);

    const ParamPoint* rpC = blocks[0].findPoint(idC);
    QVERIFY(rpC && rpC->resolved);
    QCOMPARE(rpC->resolvedPos.x, 50.0);
    QCOMPARE(rpC->resolvedPos.y, 100.0);
}

void TestResolver::attachmentSnapsBlocks()
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

void TestResolver::attachmentAtLeaderStartContinuesStraight()
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

    // Snap the follower's C onto the leader's START point A, followerAngle = 0.
    Attachment att;
    att.fromBlockId = block2Id;
    att.fromPointId = idC;
    att.toBlockId = block1Id;
    att.toPointId = idA;
    att.followerAngle = 0.0;

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

void TestResolver::explicitLeaderSegmentDisambiguates()
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
    att.followerAngle = 0.0;

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

    // Case 2: explicit s2 — B is s2's START point, so the exit direction is
    // flipped (C->B extended): straight ahead means downward, F at (100, -50).
    {
        att.toSegmentId = s2Id;
        std::vector<Block> blocks{leader, followerBlock};
        std::vector<Attachment> atts{att};
        Resolver::resolveAll(blocks, atts);
        Vec2 fWorld = blocks[1].worldPos(idF);
        QVERIFY(std::abs(fWorld.x - 100.0) < 1e-6);
        QVERIFY(std::abs(fWorld.y + 50.0) < 1e-6);
    }
}

void TestResolver::attachmentFollowerAngleIsCCWFromExitDir()
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

namespace {

/// Minimal attachment referring only the two blocks (point ids left null).
Attachment makeAtt(const QUuid& from, const QUuid& to)
{
    Attachment att;
    att.fromBlockId = from;
    att.toBlockId = to;
    return att;
}

} // namespace

void TestResolver::checkAttachmentRejectsDuplicateFollower()
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

void TestResolver::checkAttachmentRejectsCycle()
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

void TestResolver::checkAttachmentAcceptsChain()
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

void TestResolver::danglingAttachmentIsReported()
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

void TestResolver::danglingPointIsReported()
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

void TestResolver::healthyForestHasNoDiagnostics()
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

void TestResolver::bridgePinsFollowBothHosts()
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

void TestResolver::bridgePinsAcceptedByGraph()
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

void TestResolver::interpolatedPercentAndConstantAdd()
{
    // Percent and constant must ADD (相加), never act as two separate point
    // definitions:  position = measureEnd + dir * (segLength*percent + constant).
    // Reference example: 100cm segment, percent 0.5, constant 7cm
    //   → 100*0.5 + 7 = 57cm from the start.
    Block block;

    ParamPoint a;
    a.constraint = PointConstraint::Free;
    a.freePos = {0.0, 0.0};
    QUuid aId = block.addPoint(a);

    ParamPoint b;
    b.constraint = PointConstraint::Free;
    b.freePos = {1000.0, 0.0};  // 100cm stored as mm
    QUuid bId = block.addPoint(b);

    Segment seg;
    seg.startPointId = aId;
    seg.endPointId = bId;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    // Aux 1: percent 0.5 + constant 7cm → 57cm from start.
    ParamPoint aux1;
    aux1.constraint = PointConstraint::Interpolated;
    aux1.hostSegmentId = segId;
    aux1.isAuxiliary = true;
    aux1.interpPercent = 0.5;
    aux1.interpConstant = 70.0;  // 7cm in mm
    QUuid aux1Id = block.addPoint(aux1);

    // Aux 2: percent 0 + constant 8cm → plain absolute position at 8cm.
    ParamPoint aux2;
    aux2.constraint = PointConstraint::Interpolated;
    aux2.hostSegmentId = segId;
    aux2.isAuxiliary = true;
    aux2.interpPercent = 0.0;
    aux2.interpConstant = 80.0;  // 8cm in mm
    QUuid aux2Id = block.addPoint(aux2);

    // Aux 3: same values measured from the END → 57cm from end = 43cm from start.
    ParamPoint aux3;
    aux3.constraint = PointConstraint::Interpolated;
    aux3.hostSegmentId = segId;
    aux3.isAuxiliary = true;
    aux3.interpPercent = 0.5;
    aux3.interpConstant = 70.0;
    aux3.interpFromEnd = true;
    QUuid aux3Id = block.addPoint(aux3);

    block.resolve();

    const ParamPoint* r1 = block.findPoint(aux1Id);
    QVERIFY(r1 && r1->resolved);
    QCOMPARE(r1->resolvedPos.x, 570.0);  // 100*0.5 + 7 = 57cm
    QCOMPARE(r1->resolvedPos.y, 0.0);

    const ParamPoint* r2 = block.findPoint(aux2Id);
    QVERIFY(r2 && r2->resolved);
    QCOMPARE(r2->resolvedPos.x, 80.0);   // percent 0 → the constant alone positions the point
    QCOMPARE(r2->resolvedPos.y, 0.0);

    const ParamPoint* r3 = block.findPoint(aux3Id);
    QVERIFY(r3 && r3->resolved);
    QCOMPARE(r3->resolvedPos.x, 430.0);  // 1000 - 570
    QCOMPARE(r3->resolvedPos.y, 0.0);
}

void TestResolver::measureLineFollowsHostsWhenFollowing()
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

namespace {
double normDeg180(double deg)
{
    while (deg >  180.0) deg -= 360.0;
    while (deg <= -180.0) deg += 360.0;
    return deg;
}
} // namespace

// Replicates LinePropertyDialog::onConnPointResolved — editing the 指向点
// (connection point) in the connected state RETARGETS the existing follower
// attachment IN PLACE to the new leader point (the dialog mutates the stored
// attachment, it does not add a new one), back-solving the follower angle
// so the world direction is preserved. After resolve the line's start must sit
// on the NEW host point.
void TestResolver::connPointRetargetReplacesAttachment()
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

// A block carrying an endpoint-aim constraint (endTarget) is rotated in Step 7
// AFTER the attachment forest has settled. Any of its points that are not at its
// local origin move with that rotation; a follower attached to such a point must
// re-snap, otherwise it is left offset by the aim rotation (reported as a
// connection misalignment of ~4mm for L54/P128->P127).
void TestResolver::endTargetRotationDoesNotOffsetFollowers()
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

void TestResolver::curveAnchorResolvesOnChord()
{
    // A CurveAnchor point sits on its host segment's CHORD at interpPercent,
    // displaced perpendicular by interpOffsetDist (positive = left of the
    // start→end direction).
    Block block;

    ParamPoint sp;
    sp.constraint = PointConstraint::Free;
    sp.freePos = {0.0, 0.0};
    QUuid spId = block.addPoint(sp);

    ParamPoint ep;
    ep.constraint = PointConstraint::Free;
    ep.freePos = {100.0, 0.0};
    QUuid epId = block.addPoint(ep);

    Segment seg;
    seg.startPointId = spId;
    seg.endPointId = epId;
    QUuid segId = block.addSegment(seg);

    ParamPoint anchor;
    anchor.constraint = PointConstraint::CurveAnchor;
    anchor.hostSegmentId = segId;
    anchor.interpPercent = 0.5;     // midpoint of the chord
    anchor.interpOffsetDist = 20.0; // 20mm to the left of start→end (+x → +y)
    QUuid anchorId = block.addPoint(anchor);

    std::vector<Block> blocks;
    blocks.push_back(std::move(block));
    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments);

    const Block& b = blocks[0];
    Vec2 pos = b.worldPos(anchorId);
    QCOMPARE(std::abs(pos.x - 50.0) < 1e-6, true);
    QCOMPARE(std::abs(pos.y - 20.0) < 1e-6, true);
}

void TestResolver::curveAnchorFollowsEndpoints()
{
    // Parametric follow: when the segment endpoints move, the anchor keeps its
    // chord fraction + offset, so it travels with the chord.
    Block block;

    ParamPoint sp;
    sp.constraint = PointConstraint::Free;
    sp.freePos = {0.0, 0.0};
    QUuid spId = block.addPoint(sp);

    ParamPoint ep;
    ep.constraint = PointConstraint::Free;
    ep.freePos = {100.0, 0.0};
    QUuid epId = block.addPoint(ep);

    Segment seg;
    seg.startPointId = spId;
    seg.endPointId = epId;
    QUuid segId = block.addSegment(seg);

    ParamPoint anchor;
    anchor.constraint = PointConstraint::CurveAnchor;
    anchor.hostSegmentId = segId;
    anchor.interpPercent = 0.5;
    anchor.interpOffsetDist = 20.0;
    QUuid anchorId = block.addPoint(anchor);

    std::vector<Block> blocks;
    blocks.push_back(std::move(block));
    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments);

    // Stretch the chord to 200mm: the anchor must stay at 50% + 20mm offset.
    Block& b = blocks[0];
    b.findPoint(epId)->freePos = {200.0, 0.0};
    Resolver::resolveAll(blocks, attachments);

    Vec2 pos = b.worldPos(anchorId);
    QCOMPARE(std::abs(pos.x - 100.0) < 1e-6, true);  // 50% of 200
    QCOMPARE(std::abs(pos.y - 20.0)  < 1e-6, true);  // offset preserved
}

void TestResolver::curveAnchorKeepsFullOffsetNearEndpoint()
{
    // ETCAD-like behaviour: the anchor's perpendicular offset is NOT tapered
    // near an endpoint — the curve keeps its full shape and smoothness is
    // guaranteed by the curve math (non-collapsing tangent), not by flattening
    // the anchor onto the chord. So an anchor 10% from the endpoint with a
    // 20mm offset resolves at the full offset.
    Block block;

    ParamPoint sp;
    sp.constraint = PointConstraint::Free;
    sp.freePos = {0.0, 0.0};
    QUuid spId = block.addPoint(sp);

    ParamPoint ep;
    ep.constraint = PointConstraint::Free;
    ep.freePos = {100.0, 0.0};
    QUuid epId = block.addPoint(ep);

    Segment seg;
    seg.startPointId = spId;
    seg.endPointId = epId;
    QUuid segId = block.addSegment(seg);

    ParamPoint anchor;
    anchor.constraint = PointConstraint::CurveAnchor;
    anchor.hostSegmentId = segId;
    anchor.interpPercent = 0.1;      // 10% from the start (near the endpoint)
    anchor.interpOffsetDist = 20.0;  // stored offset
    QUuid anchorId = block.addPoint(anchor);

    std::vector<Block> blocks;
    blocks.push_back(std::move(block));
    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments);

    const Block& b = blocks[0];
    Vec2 pos = b.worldPos(anchorId);
    QCOMPARE(std::abs(pos.x - 10.0) < 1e-6, true);   // 10% of 100
    QCOMPARE(std::abs(pos.y - 20.0) < 1e-6, true);   // full offset preserved (no taper)
}

void TestResolver::arcLengthZeroMeansReverse180()
{
    // Arc-length mode measures the arc from the leader's REVERSE direction
    // (弧长 0 = 角度 180°). Leader A(0,0)→B(100,0) (exit dir 0°); follower
    // C(0,0)→D(50,0) snapped at B with arcLength = 0 → follower points
    // 180° (back along the leader): D lands at (50, 0).
    Block leader;
    ParamPoint pA; pA.name="A"; pA.constraint=PointConstraint::Free; pA.freePos={0.0,0.0};
    QUuid idA = leader.addPoint(pA);
    ParamPoint pB; pB.name="B"; pB.constraint=PointConstraint::Free; pB.freePos={100.0,0.0};
    QUuid idB = leader.addPoint(pB);
    Segment segL; segL.startPointId=idA; segL.endPointId=idB;
    leader.addSegment(segL);

    Block follower;
    ParamPoint pC; pC.name="C"; pC.constraint=PointConstraint::Free; pC.freePos={0.0,0.0};
    QUuid idC = follower.addPoint(pC);
    ParamPoint pD; pD.name="D"; pD.constraint=PointConstraint::Free; pD.freePos={50.0,0.0};
    QUuid idD = follower.addPoint(pD);
    Segment segF; segF.startPointId=idC; segF.endPointId=idD;
    follower.addSegment(segF);

    Attachment att;
    att.fromBlockId=follower.id; att.fromPointId=idC;
    att.toBlockId=leader.id; att.toPointId=idB;
    att.rotationMode = RotationMode::ArcLength;
    att.arcLength = 0.0;   // 弧长 0 → 角度 180°

    std::vector<Block> blocks;
    blocks.push_back(std::move(leader));
    blocks.push_back(std::move(follower));
    std::vector<Attachment> attachments{att};
    Resolver::resolveAll(blocks, attachments);

    const Block& rf = blocks[1];
    Vec2 dWorld = rf.worldPos(idD);
    QVERIFY(std::abs(dWorld.x - 50.0) < 1e-6);   // 180°: back along the leader
    QVERIFY(std::abs(dWorld.y) < 1e-6);
    QVERIFY(std::abs(rf.transform.rotation - M_PI) < 1e-9);
}

void TestResolver::arcLengthFullTurnSameAsAngleZero()
{
    // arcLength = π·radius (a full half-circle worth of arc) → 角度 0°:
    // follower continues straight along the leader (same as followerAngle=0).
    Block leader;
    ParamPoint pA; pA.name="A"; pA.constraint=PointConstraint::Free; pA.freePos={0.0,0.0};
    QUuid idA = leader.addPoint(pA);
    ParamPoint pB; pB.name="B"; pB.constraint=PointConstraint::Free; pB.freePos={100.0,0.0};
    QUuid idB = leader.addPoint(pB);
    Segment segL; segL.startPointId=idA; segL.endPointId=idB;
    leader.addSegment(segL);

    Block follower;
    ParamPoint pC; pC.name="C"; pC.constraint=PointConstraint::Free; pC.freePos={0.0,0.0};
    QUuid idC = follower.addPoint(pC);
    ParamPoint pD; pD.name="D"; pD.constraint=PointConstraint::Free; pD.freePos={50.0,0.0};
    QUuid idD = follower.addPoint(pD);
    Segment segF; segF.startPointId=idC; segF.endPointId=idD;
    follower.addSegment(segF);

    Attachment att;
    att.fromBlockId=follower.id; att.fromPointId=idC;
    att.toBlockId=leader.id; att.toPointId=idB;
    att.rotationMode = RotationMode::ArcLength;
    att.arcLength = M_PI * 50.0;  // radius = follower length = 50mm; πr → 0°

    std::vector<Block> blocks;
    blocks.push_back(std::move(leader));
    blocks.push_back(std::move(follower));
    std::vector<Attachment> attachments{att};
    Resolver::resolveAll(blocks, attachments);

    const Block& rf = blocks[1];
    Vec2 dWorld = rf.worldPos(idD);
    QVERIFY(std::abs(dWorld.x - 150.0) < 1e-6);   // 0°: straight continuation
    QVERIFY(std::abs(dWorld.y) < 1e-6);
    // rotation = 2π numerically (angle π + arc/radius = π + π); equivalent to 0.
    QVERIFY(std::abs(cad::geo::normalizeRad(rf.transform.rotation)) < 1e-9);
}

void TestResolver::arcLengthQuarterTurnMatchesAngle270()
{
    // arcLength = π/2·radius → 角度 180°+90° = 270°: follower points −Y.
    Block leader;
    ParamPoint pA; pA.name="A"; pA.constraint=PointConstraint::Free; pA.freePos={0.0,0.0};
    QUuid idA = leader.addPoint(pA);
    ParamPoint pB; pB.name="B"; pB.constraint=PointConstraint::Free; pB.freePos={100.0,0.0};
    QUuid idB = leader.addPoint(pB);
    Segment segL; segL.startPointId=idA; segL.endPointId=idB;
    leader.addSegment(segL);

    Block follower;
    ParamPoint pC; pC.name="C"; pC.constraint=PointConstraint::Free; pC.freePos={0.0,0.0};
    QUuid idC = follower.addPoint(pC);
    ParamPoint pD; pD.name="D"; pD.constraint=PointConstraint::Free; pD.freePos={50.0,0.0};
    QUuid idD = follower.addPoint(pD);
    Segment segF; segF.startPointId=idC; segF.endPointId=idD;
    follower.addSegment(segF);

    Attachment att;
    att.fromBlockId=follower.id; att.fromPointId=idC;
    att.toBlockId=leader.id; att.toPointId=idB;
    att.rotationMode = RotationMode::ArcLength;
    att.arcLength = M_PI / 2.0 * 50.0;  // 90° of arc on radius 50mm

    std::vector<Block> blocks;
    blocks.push_back(std::move(leader));
    blocks.push_back(std::move(follower));
    std::vector<Attachment> attachments{att};
    Resolver::resolveAll(blocks, attachments);

    const Block& rf = blocks[1];
    Vec2 dWorld = rf.worldPos(idD);
    QVERIFY(std::abs(dWorld.x - 100.0) < 1e-6);   // 270°: down from B
    QVERIFY(std::abs(dWorld.y + 50.0) < 1e-6);
    QVERIFY(std::abs(rf.transform.rotation - 1.5 * M_PI) < 1e-9);
}

QTEST_GUILESS_MAIN(TestResolver)
#include "test_resolver.moc"