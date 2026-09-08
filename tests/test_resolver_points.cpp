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

class TestResolverPoints : public QObject
{
    Q_OBJECT

private slots:
    void freePointsResolve();
    void polarConstraintResolve();
    void polarWithFormula();
    void orthoOffsetConstraintResolve();
    void orthoOffsetWithFormula();
    void midpointResolve();
    void interpolatedPercentAndConstantAdd();
};

void TestResolverPoints::freePointsResolve()
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

void TestResolverPoints::polarConstraintResolve()
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

void TestResolverPoints::polarWithFormula()
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

void TestResolverPoints::orthoOffsetConstraintResolve()
{
    Block block;
    block.name = "OrthoBlock";

    ParamPoint pA;
    pA.name = "A";
    pA.constraint = PointConstraint::Free;
    pA.freePos = {0.0, 0.0};
    QUuid idA = block.addPoint(pA);

    // B: main axis 0 deg, main length 100mm, offset +50mm (left/+90 -> +Y)
    ParamPoint pB;
    pB.name = "B";
    pB.constraint = PointConstraint::OrthoOffset;
    pB.refPointId = idA;
    pB.distance = 100.0;
    pB.angle = 0.0;
    pB.orthoOffsetDist = 50.0;
    QUuid idB = block.addPoint(pB);

    // C: main axis 0 deg, main length 100mm, offset -30mm (right/-90 -> -Y)
    ParamPoint pC;
    pC.name = "C";
    pC.constraint = PointConstraint::OrthoOffset;
    pC.refPointId = idA;
    pC.distance = 100.0;
    pC.angle = 0.0;
    pC.orthoOffsetDist = -30.0;
    QUuid idC = block.addPoint(pC);

    // D: main axis 90 deg (+Y), main length 100mm, offset +40mm (left/+90 from +Y -> -X)
    ParamPoint pD;
    pD.name = "D";
    pD.constraint = PointConstraint::OrthoOffset;
    pD.refPointId = idA;
    pD.distance = 100.0;
    pD.angle = 90.0;
    pD.orthoOffsetDist = 40.0;
    QUuid idD = block.addPoint(pD);

    std::vector<Block> blocks;
    blocks.push_back(std::move(block));

    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments);

    const Block& resolved = blocks[0];
    const ParamPoint* rpB = resolved.findPoint(idB);
    const ParamPoint* rpC = resolved.findPoint(idC);
    const ParamPoint* rpD = resolved.findPoint(idD);

    QVERIFY(rpB && rpB->resolved);
    QVERIFY(rpC && rpC->resolved);
    QVERIFY(rpD && rpD->resolved);

    // 屏幕空间 (Y向下):
    // B: 主轴 0° (向右), 偏置 +50mm (视线向左 -> Y- 屏幕上方) -> (100, -50)
    // C: 主轴 0° (向右), 偏置 -30mm (视线向右 -> Y+ 屏幕下方) -> (100, +30)
    // D: 主轴 90° (向下), 偏置 +40mm (视线向左 -> X+ 屏幕右侧) -> (40, 100)
    QVERIFY(std::abs(rpB->resolvedPos.x - 100.0) < 1e-6);
    QVERIFY(std::abs(rpB->resolvedPos.y - (-50.0)) < 1e-6);

    QVERIFY(std::abs(rpC->resolvedPos.x - 100.0) < 1e-6);
    QVERIFY(std::abs(rpC->resolvedPos.y - 30.0) < 1e-6);

    QVERIFY(std::abs(rpD->resolvedPos.x - 40.0) < 1e-6);
    QVERIFY(std::abs(rpD->resolvedPos.y - 100.0) < 1e-6);
}

void TestResolverPoints::orthoOffsetWithFormula()
{
    Block block;
    block.name = "OrthoFormulaBlock";

    ParamPoint pA;
    pA.name = "A";
    pA.constraint = PointConstraint::Free;
    pA.freePos = {10.0, 20.0};
    QUuid idA = block.addPoint(pA);

    // B: formula driven
    ParamPoint pB;
    pB.name = "B";
    pB.constraint = PointConstraint::OrthoOffset;
    pB.refPointId = idA;
    pB.distanceFormula = "shoulder_w / 2"; // 40 / 2 = 20cm -> 200mm
    pB.angle = 0.0;
    pB.orthoOffsetDistFormula = "-drop";    // -4cm -> -40mm (right -> -Y)
    QUuid idB = block.addPoint(pB);

    QHash<QString, double> params;
    params["shoulder_w"] = 40.0;
    params["drop"] = 4.0;

    std::vector<Block> blocks;
    blocks.push_back(std::move(block));

    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments, params);

    const ParamPoint* rpB = blocks[0].findPoint(idB);
    QVERIFY(rpB && rpB->resolved);
    // 屏幕空间 (Y向下): x = 10 + 200 = 210, 负偏置(右偏) -> 屏幕下方 Y+: y = 20 + 40 = 60
    QVERIFY(std::abs(rpB->resolvedPos.x - 210.0) < 1e-6);
    QVERIFY(std::abs(rpB->resolvedPos.y - 60.0) < 1e-6);
}

void TestResolverPoints::midpointResolve()
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

void TestResolverPoints::interpolatedPercentAndConstantAdd()
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

QTEST_GUILESS_MAIN(TestResolverPoints)
#include "test_resolver_points.moc"
