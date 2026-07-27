#include <QtTest>
#include <QHash>
#include <QString>
#include <cmath>
#include <vector>

#include "parametric/Resolver.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "geometry/Vec2.h"

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
    // Attachment: from=Block2 point C, to=Block1 point B, angleOffset=0
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
    att.angleOffset = 0.0;

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

QTEST_GUILESS_MAIN(TestResolver)
#include "test_resolver.moc"
