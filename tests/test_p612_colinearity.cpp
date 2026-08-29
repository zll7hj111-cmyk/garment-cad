/// @file test_p612_colinearity.cpp
/// 借用点交点（interAimPointId）的射线共线性验证。
///
/// P612 语义：交点的射线方向不是数值角，而是「起点 → 借用点」的方向；参数
/// 变化时起点与借用点都会移动，交点必须始终落在新的「起点→借用点」直线上
/// （用户报告肩褶高 8 → 15/20 时"感觉不在一条直线上"）。
///
/// P2-2：本测试原先加载 `build/out/Debug/1.gcad`（构建目录里的一份用户存档
/// 副本）—— 文件不在就 QSKIP，等于常年空跑，而且赌的是仓库之外的文件。现
/// 改为合成文档：起点与借用点都由参数 H 驱动，在 3 个参数值下验证共线性，
/// 并额外断言"交点确实在动"（否则共线性断言会在静止文档上空过）。

#include <QtTest>

#include <cmath>

#include "parametric/ParamDocument.h"
#include "geometry/Vec2.h"

using cad::param::Block;
using cad::param::ParamDocument;
using cad::param::ParamPoint;
using cad::param::PointConstraint;
using cad::param::Segment;
using cad::geo::Vec2;

namespace {

/// Synthetic borrow-point document:
///   Block R: anchor (0,0), ray origin O = Polar(H, 30°) and borrow point
///            P = Polar(2H, 20°) — both driven by parameter H (cm domain).
///   Block T: target segment x = 200mm, spanning y ∈ [-200, 300]; it carries
///            the intersection point with refPointA = O and
///            interAimPointId = P (borrow-point mode: direction = O → P).
struct BorrowDoc {
    ParamDocument doc;
    QUuid originId;
    QUuid aimId;
    QUuid interId;
};

/// Fills @p fx in place: the struct owns a QObject, so it can be neither
/// copied nor returned by value (MSVC rejects NRVO for it with C2280).
void buildBorrowDoc(BorrowDoc& fx)
{
    ParamDocument& doc = fx.doc;

    // ── R: origin + borrow point, both driven by H ─────────────────────────
    Block r;
    ParamPoint anchor;
    anchor.constraint = PointConstraint::Free;
    anchor.freePos = Vec2::zero();
    const QUuid anchorId = anchor.id;
    ParamPoint o;
    o.constraint = PointConstraint::Polar;
    o.refPointId = anchorId;
    o.distance = 0.0;
    o.distanceFormula = QStringLiteral("H");
    o.angle = 30.0;
    const QUuid oId = o.id;
    ParamPoint p;
    p.constraint = PointConstraint::Polar;
    p.refPointId = anchorId;
    p.distance = 0.0;
    p.distanceFormula = QStringLiteral("2*H");
    p.angle = 20.0;
    const QUuid pId = p.id;
    r.addPoint(anchor);
    r.addPoint(o);
    r.addPoint(p);
    Segment rs;
    rs.startPointId = anchorId;
    rs.endPointId = oId;
    r.addSegment(rs);
    doc.addBlock(std::move(r));

    // ── T: target segment + the borrow-point intersection ──────────────────
    Block t;
    ParamPoint t1;
    t1.constraint = PointConstraint::Free;
    t1.freePos = Vec2(200.0, -200.0);
    const QUuid t1Id = t1.id;
    ParamPoint t2;
    t2.constraint = PointConstraint::Free;
    t2.freePos = Vec2(200.0, 300.0);
    const QUuid t2Id = t2.id;
    t.addPoint(t1);
    t.addPoint(t2);
    Segment ts;
    ts.startPointId = t1Id;
    ts.endPointId = t2Id;
    const QUuid tsId = ts.id;
    t.addSegment(ts);

    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.serial = doc.newPointSerial();
    ix.isAuxiliary = true;
    ix.visible = true;
    ix.refPointA = oId;
    ix.interAimPointId = pId;      // ← borrow-point mode (direction = O → P)
    ix.hostSegmentId = tsId;
    const QUuid ixId = ix.id;
    t.addPoint(ix);
    t.findSegment(tsId)->auxPointIds.push_back(ixId);
    doc.addBlock(std::move(t));

    fx.originId = oId;
    fx.aimId = pId;
    fx.interId = ixId;

    doc.setParameter(QStringLiteral("H"), 8.0);
    doc.resolveAll();
}

Vec2 worldPosOf(const ParamDocument& doc, const QUuid& id)
{
    for (const auto& b : doc.blocks())
        if (b.findPoint(id))
            return b.worldPos(id);
    return Vec2::zero();
}

} // namespace

class TestP612Colinearity : public QObject
{
    Q_OBJECT
private slots:
    void colinearityAtDartHeights();
};

void TestP612Colinearity::colinearityAtDartHeights()
{
    BorrowDoc fx;
    buildBorrowDoc(fx);
    ParamDocument& doc = fx.doc;

    // The fixture must really be a borrow-point intersection, otherwise this
    // regression would silently test a plain numeric-angle ray.
    const Block* host = nullptr;
    for (const auto& b : doc.blocks())
        if (b.findPoint(fx.interId)) { host = &b; break; }
    QVERIFY(host);
    const ParamPoint* ip = host->findPoint(fx.interId);
    QVERIFY(ip);
    QCOMPARE(ip->constraint, PointConstraint::Intersection);
    QVERIFY(!ip->interAimPointId.isNull());
    QCOMPARE(ip->interAimPointId, fx.aimId);
    QVERIFY2(ip->resolved, "baseline: intersection must resolve");
    QVERIFY(doc.diagnostics().empty());

    Vec2 firstHit = Vec2::zero();
    bool first = true;
    for (double h : {8.0, 15.0, 20.0}) {
        doc.setParameter(QStringLiteral("H"), h);
        doc.resolveAll();

        const Vec2 o = worldPosOf(doc, fx.originId);
        const Vec2 a = worldPosOf(doc, fx.aimId);
        const Vec2 i = worldPosOf(doc, fx.interId);
        const Vec2 oa = a - o;
        const double len = oa.length();
        // 点到直线距离 = |(i-o) × oa| / |oa|
        const double dist = (len > 1e-9) ? std::abs((i - o).cross(oa)) / len : -1.0;
        qInfo() << "H" << h << "origin(" << o.x << "," << o.y << ")"
                << "borrow(" << a.x << "," << a.y << ")"
                << "inter(" << i.x << "," << i.y << ")"
                << "off-line" << dist << "mm";
        QVERIFY2(dist >= 0.0 && dist < 0.5,
                 qPrintable(QStringLiteral("H=%1 时交点偏离起点→借用点直线 %2 mm")
                                .arg(h).arg(dist)));

        if (first) { firstHit = i; first = false; }
    }

    // 防空过：交点必须真的随参数移动，否则上面的共线性断言在静止文档上也能过。
    const Vec2 lastHit = worldPosOf(doc, fx.interId);
    QVERIFY2(lastHit.distanceTo(firstHit) > 5.0,
             qPrintable(QStringLiteral("交点未随 H 移动 (位移 %1 mm)")
                            .arg(lastHit.distanceTo(firstHit))));
}

QTEST_GUILESS_MAIN(TestP612Colinearity)
#include "test_p612_colinearity.moc"
