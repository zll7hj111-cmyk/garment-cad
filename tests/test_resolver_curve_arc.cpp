#include <QtTest>
#include <cmath>
#include <vector>

#include "parametric/Resolver.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "geometry/Vec2.h"
#include "geometry/Angle.h"

using namespace cad::param;
using cad::geo::Vec2;

class TestResolverCurveArc : public QObject
{
    Q_OBJECT

private slots:
    void curveAnchorResolvesOnChord();
    void rigidDragKeepsCurveCacheFrozen();
    void curveAnchorFollowsEndpoints();
    void curveAnchorKeepsFullOffsetNearEndpoint();
    void arcLengthZeroMeansFoldBack0();
    void arcLengthHalfCircleMeansStraight180();
    void arcLengthQuarterTurnMatchesAngle90();
};

void TestResolverCurveArc::curveAnchorResolvesOnChord()
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

// 曲线缓存惰性重建回归 (用户报告 2026-09: 跟随对象拖动时曲线抖来抖去 +
// 无谓开销): 纯刚体拖动 (transform 变化) 不得重建曲线缓存 (无 C2/flatten
// 重算, epoch/span 冻结); 只有局部几何真正变化时才重建.
void TestResolverCurveArc::rigidDragKeepsCurveCacheFrozen()
{
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
    seg.type = cad::param::SegmentType::Bezier;  // 曲线段 → 帧级 span 缓存
    QUuid segId = block.addSegment(seg);

    ParamPoint anchor;
    anchor.constraint = PointConstraint::CurveAnchor;
    anchor.hostSegmentId = segId;
    anchor.interpPercent = 0.5;
    anchor.interpOffsetDist = 20.0;
    QUuid anchorId = block.addPoint(anchor);
    // isCurve() = type==Bezier && passPointIds 非空 — 必须写进块内的 segment.
    block.findSegment(segId)->passPointIds.push_back(anchorId);

    std::vector<Block> blocks;
    blocks.push_back(std::move(block));
    std::vector<Attachment> attachments;
    Resolver::resolveAll(blocks, attachments);
    Block& b = blocks[0];

    const quint64 epoch0 = b.geometryEpoch();
    const quint64 builds0 = b.curveCacheBuilds;
    const auto* entry0 = b.curveSpanEntry(segId);
    QVERIFY(entry0 && !entry0->spans.empty());
    const auto flat0 = entry0->flatLocal;

    // 纯刚体拖动 (平移 + 旋转 — 与 ConnectGesture::move 每帧同路径):
    // 局部几何零变化 → epoch 不变、曲线缓存零重建、span 逐字节相同.
    b.transform.origin = {55.0, -30.0};
    b.transform.rotation = 0.7;
    Resolver::resolveAll(blocks, attachments);
    QCOMPARE(b.geometryEpoch(), epoch0);      // 无局部点移动
    QCOMPARE(b.curveCacheBuilds, builds0);  // 没有 C2 重解 / 重 flatten
    const auto* entryA = b.curveSpanEntry(segId);
    QVERIFY(entryA && entryA->flatLocal == flat0);

    // 局部变化 (锚点 percent 移动) → epoch 变化 + 缓存重建 + span 更新.
    if (auto* pa = b.findPoint(anchorId)) pa->interpPercent = 0.8;
    Resolver::resolveAll(blocks, attachments);
    QVERIFY(b.geometryEpoch() != epoch0);
    QCOMPARE(b.curveCacheBuilds, builds0 + 1);
    const auto* entryB = b.curveSpanEntry(segId);
    QVERIFY(entryB && entryB->flatLocal != flat0);
}

void TestResolverCurveArc::curveAnchorFollowsEndpoints()
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

void TestResolverCurveArc::curveAnchorKeepsFullOffsetNearEndpoint()
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

void TestResolverCurveArc::arcLengthZeroMeansFoldBack0()
{
    // Arc-length mode measures from the CLOSED position (弧长 0 = 角度 0° =
    // 两线折叠重叠, 闭合基准, 用户拍板 2026-08 定稿), sweeping so that
    // πr = 180° = straight continuation. Leader A(0,0)→B(100,0) (exit dir
    // 0°); follower C(0,0)→D(50,0) snapped at B with arcLength = 0 →
    // follower folds back onto the leader (0°): D lands at (50, 0).
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
    att.arcLength = 0.0;   // 弧长 0 → 角度 0° (fold back onto the leader)

    std::vector<Block> blocks;
    blocks.push_back(std::move(leader));
    blocks.push_back(std::move(follower));
    std::vector<Attachment> attachments{att};
    Resolver::resolveAll(blocks, attachments);

    const Block& rf = blocks[1];
    Vec2 dWorld = rf.worldPos(idD);
    QVERIFY(std::abs(dWorld.x - 50.0) < 1e-6);   // 0°: folded back along the leader
    QVERIFY(std::abs(dWorld.y) < 1e-6);
    QVERIFY(std::abs(cad::geo::normalizeRad(rf.transform.rotation - M_PI)) < 1e-9);
}

void TestResolverCurveArc::arcLengthHalfCircleMeansStraight180()
{
    // arcLength = π·radius (a full half-circle worth of arc) → 角度 180°:
    // the follower continues straight along the leader (弧长 = πr = 180° =
    // 延伸直行, 闭合基准). D lands at (150, 0).
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
    att.arcLength = M_PI * 50.0;  // radius = follower length = 50mm; πr → 180°

    std::vector<Block> blocks;
    blocks.push_back(std::move(leader));
    blocks.push_back(std::move(follower));
    std::vector<Attachment> attachments{att};
    Resolver::resolveAll(blocks, attachments);

    const Block& rf = blocks[1];
    Vec2 dWorld = rf.worldPos(idD);
    QVERIFY(std::abs(dWorld.x - 150.0) < 1e-6);   // 180°: straight continuation
    QVERIFY(std::abs(dWorld.y) < 1e-6);
    QVERIFY(std::abs(rf.transform.rotation) < 1e-9);
}

void TestResolverCurveArc::arcLengthQuarterTurnMatchesAngle90()
{
    // arcLength = π/2·radius → 弧长角 90° → 角度 90°（闭合基准, 0° = 折叠,
    // 90° = 垂直）: follower points +Y.
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
    QVERIFY(std::abs(dWorld.x - 100.0) < 1e-6);   // 90°: up from B
    QVERIFY(std::abs(dWorld.y - 50.0) < 1e-6);
    QVERIFY(std::abs(rf.transform.rotation - 0.5 * M_PI) < 1e-9);
}

QTEST_GUILESS_MAIN(TestResolverCurveArc)
#include "test_resolver_curve_arc.moc"
