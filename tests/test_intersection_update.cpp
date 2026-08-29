/// @file test_intersection_update.cpp
/// 跨图层交点随参数变化联动更新（Phase 2.5 / Phase 3 / Phase 4）。
///
/// 场景（用户 2026-08 报告的拓扑，本测试用合成档复现，见 CrossLayerDoc）：
/// 辅助层上的射线-线段交点（目标线段在辅助层块上），射线起点位于工作层
/// （距离公式引用参数 L）。修改变量 → 工作层起点移动 → 交点位置必须同步
/// 更新。本测试量化验证：
///   ① 起点确实移动；
///   ② 交点移动且仍位于（新起点 → 新射线方向）与目标线段的交点；
///   ③ 引用交点的插值辅助点（距交点 15mm 沿线段）同步跟随。
///
/// P2-2：这些用例原先加载用户的存档 E:/3.gcad —— 那是一份真实的制图纸样，
/// 用户一改就红（"交点偏移 29.17mm / 缺变量 后长补正"），等于把回归基线押
/// 在仓库之外的文件上。现全部改为合成文档：断言钉在引擎行为上，磁盘上没有
/// 任何用户文档也不影响本测试。
///
#include <QtTest>
#include <QPainter>
#include <QUndoStack>

#include <cmath>

#include "parametric/ParamDocument.h"
#include "geometry/Vec2.h"
#include "canvas/CanvasScene.h"
#include "document/commands/BlockCommands.h"

namespace {

struct SnapPos {
    cad::geo::Vec2 origin;
    cad::geo::Vec2 inter;
    cad::geo::Vec2 interp;
};

/// Synthetic cross-layer document (P2-2).
///
/// These cases used to load `e:/3.gcad` — a REAL user save file, i.e. the
/// assertions were pinned to whatever the user happened to have on disk, and
/// they went red the moment that file was edited ("交点偏移 29.17mm / 缺变量
/// 后长补正"). The structure below reproduces the reported topology in ~40
/// lines, so the regression is pinned to the ENGINE behaviour instead:
///
///   W (working):  anchor A(0,-100) + Polar end O = A + L·10mm along +x.
///                 O is the ray ORIGIN and moves with parameter L (cm).
///   T:            target segment (0,0)-(300,0); carries the intersection
///                 (origin = O, 90° relative to the segment) and an
///                 interpolated point 15mm along the segment from it.
///   A cross-layer attachment (aux follower -> working leader) snapped to W's
///   ANCHOR keeps the target block coupled to W, so moving O moves the ray but
///   NOT the target segment — exactly the situation Phase 2.5 / Phase 4 exist
///   for. Without it the intersection would be a same-layer cross-block case.
///
/// @param originOnAux false = target/intersection on the AUX block and the
///        origin on the WORKING block (Phase 2.5, the original 3.gcad report).
///        true  = mirrored: origin on the AUX block, target/intersection on the
///        WORKING block (Phase 4, 工作侧跨层交点重解).
struct CrossLayerDoc {
    cad::param::ParamDocument doc;
    QUuid workLayer;
    QUuid auxLayer;
    QUuid leaderBlock;   ///< working block the aux block follows
    QUuid originBlock;   ///< block owning the ray origin
    QUuid targetBlock;   ///< block owning the target segment
    QUuid anchorId;      ///< attachment snap point on the leader
    QUuid originId;      ///< ray origin point
    QUuid targetSegId;
    QUuid interId;
    QUuid interpId;
    /// The one-way cross-layer attachment was accepted (checked by the caller:
    /// QVERIFY must not live in a non-void helper).
    bool attachmentAdded = false;
};

/// Build the fixture and resolve it once. Parameter "L" (cm) drives the ray
/// origin: L=8 -> origin at (80, -100).
///
/// Fills @p fx in place: the struct owns a QObject, so it is neither copyable
/// nor movable and cannot be returned by value (MSVC rejects NRVO for it with
/// C2280 even though the copy would be elided).
void buildCrossLayerDoc(CrossLayerDoc& fx, bool originOnAux)
{
    using cad::param::Attachment;
    using cad::param::Block;
    using cad::param::LayerType;
    using cad::param::ParamPoint;
    using cad::param::PointConstraint;
    using cad::param::Segment;

    cad::param::ParamDocument& doc = fx.doc;
    for (const auto& l : doc.layers()) {
        if (l.type == LayerType::Auxiliary) fx.auxLayer = l.id;
        else if (fx.workLayer.isNull()) fx.workLayer = l.id;
    }
    Q_ASSERT(!fx.workLayer.isNull() && !fx.auxLayer.isNull());

    // ── W: anchor (0,-100) + Polar end driven by "L" ──────────────────────
    Block w;
    w.layer = fx.workLayer;
    ParamPoint wAnchor;
    wAnchor.constraint = PointConstraint::Free;
    wAnchor.freePos = cad::geo::Vec2(0.0, -100.0);
    const QUuid wAnchorId = wAnchor.id;
    ParamPoint wEnd;
    wEnd.constraint = PointConstraint::Polar;
    wEnd.refPointId = wAnchorId;
    wEnd.distance = 0.0;
    wEnd.distanceFormula = QStringLiteral("L");
    wEnd.angle = 0.0;
    const QUuid wEndId = wEnd.id;
    w.addPoint(wAnchor);
    w.addPoint(wEnd);
    Segment wSeg;
    wSeg.startPointId = wAnchorId;
    wSeg.endPointId = wEndId;
    w.addSegment(wSeg);
    fx.leaderBlock = w.id;
    fx.anchorId = wAnchorId;
    doc.addBlock(std::move(w));

    // ── T: target segment (0,0)-(300,0) ──────────────────────────────────
    Block t;
    t.layer = originOnAux ? fx.workLayer : fx.auxLayer;
    ParamPoint t1;
    t1.constraint = PointConstraint::Free;
    t1.freePos = cad::geo::Vec2(0.0, 0.0);
    const QUuid t1Id = t1.id;
    ParamPoint t2;
    t2.constraint = PointConstraint::Free;
    t2.freePos = cad::geo::Vec2(300.0, 0.0);
    const QUuid t2Id = t2.id;
    t.addPoint(t1);
    t.addPoint(t2);
    Segment tSeg;
    tSeg.startPointId = t1Id;
    tSeg.endPointId = t2Id;
    const QUuid tSegId = tSeg.id;
    t.addSegment(tSeg);
    fx.targetBlock = t.id;   // id is stable across the move
    fx.targetSegId = tSegId;
    doc.addBlock(std::move(t));

    // ── Origin: W's Polar end, unless the mirrored variant moves it to an
    //    aux block that follows W (and carries its own L-driven Polar end).
    fx.originBlock = fx.leaderBlock;
    QUuid originId = wEndId;
    if (originOnAux) {
        Block x;
        x.layer = fx.auxLayer;
        ParamPoint xAnchor;
        xAnchor.constraint = PointConstraint::Free;
        xAnchor.freePos = cad::geo::Vec2(0.0, -100.0);
        const QUuid xAnchorId = xAnchor.id;
        ParamPoint xEnd;
        xEnd.constraint = PointConstraint::Polar;
        xEnd.refPointId = xAnchorId;
        xEnd.distance = 0.0;
        xEnd.distanceFormula = QStringLiteral("L");
        xEnd.angle = 0.0;
        const QUuid xEndId = xEnd.id;
        x.addPoint(xAnchor);
        x.addPoint(xEnd);
        Segment xSeg;
        xSeg.startPointId = xAnchorId;
        xSeg.endPointId = xEndId;
        x.addSegment(xSeg);
        const QUuid xId = x.id;
        doc.addBlock(std::move(x));
        fx.originBlock = xId;
        originId = xEndId;

        // aux follower (X) -> working leader (T): the one permitted direction.
        Attachment ax;
        ax.fromBlockId = xId;
        ax.fromPointId = xAnchorId;
        ax.toBlockId = fx.targetBlock;
        ax.toPointId = t1Id;
        ax.followerAngle = 0.0;
        fx.attachmentAdded = doc.addAttachment(ax);
    } else {
        // aux follower (T) -> working leader (W), snapped to W's ANCHOR.
        Attachment at;
        at.fromBlockId = fx.targetBlock;
        at.fromPointId = t1Id;
        at.toBlockId = fx.leaderBlock;
        at.toPointId = fx.anchorId;
        at.followerAngle = 0.0;
        fx.attachmentAdded = doc.addAttachment(at);
    }

    // ── Intersection on T's segment, origin = the L-driven point ──────────
    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.serial = doc.newPointSerial();
    ix.isAuxiliary = true;
    ix.visible = true;
    ix.showName = false;
    ix.refPointA = originId;
    ix.hostSegmentId = tSegId;
    ix.interAngle = 90.0;
    ix.interBidirectional = false;
    const QUuid ixId = ix.id;

    // ── Interpolated point 15mm along the segment from the intersection ───
    ParamPoint ip;
    ip.constraint = PointConstraint::Interpolated;
    ip.serial = doc.newPointSerial();
    ip.isAuxiliary = true;
    ip.visible = true;
    ip.showName = false;
    ip.hostSegmentId = tSegId;
    ip.interpRefPointId = ixId;
    ip.interpPercent = 0.0;
    ip.interpConstant = 15.0;
    const QUuid ipId = ip.id;

    if (cad::param::Block* tb = doc.findBlock(fx.targetBlock)) {
        tb->addPoint(ix);
        tb->addPoint(ip);
        if (cad::param::Segment* seg = tb->findSegment(tSegId)) {
            seg->auxPointIds.push_back(ixId);
            seg->auxPointIds.push_back(ipId);
        }
    }

    fx.originId = originId;
    fx.interId = ixId;
    fx.interpId = ipId;

    doc.setParameter(QStringLiteral("L"), 8.0);
    doc.resolveAll();
    if (!doc.diagnostics().empty()) {
        for (const auto& d : doc.diagnostics())
            qWarning() << "fixture diagnostic kind" << static_cast<int>(d.kind);
    }
}

/// World position of a point id across every block of @p doc.
cad::geo::Vec2 worldPosOf(const cad::param::ParamDocument& doc, const QUuid& id)
{
    for (const auto& b : doc.blocks())
        if (b.findPoint(id))
            return b.worldPos(id);
    return {};
}

} // namespace

class TestIntersectionUpdate : public QObject
{
    Q_OBJECT
private slots:
    void crossLayerIntersectionFollowsVariable();
    void sweepMutationsAndDragKeepOnRay();
    void workingSideIntersectionWithAuxOrigin();
    void sameLayerIntersectionFollowsVariable();
    void sameLayerCycleEndpointRefsIntersection();
    void canvasRepaintsIntersectionAfterVariable();
    void liveSequenceToolCreatedIntersectionFollows();
    void absoluteWorldAngleIntersectionStaysAbsolute();
};

// 期望交点：射线 origin + s·dir(theta)，theta = 目标线段方向 + interAngle
// （interAngle 恒为线段相对角度）；与线段交叉积求交。
static cad::geo::Vec2 expectedHit(const cad::param::Block& host,
                                  const cad::param::Segment& seg,
                                  const cad::param::ParamPoint& pt,
                                  const cad::geo::Vec2& originWorld,
                                  bool* okOut)
{
    *okOut = false;
    const auto* sp = host.findPoint(seg.startPointId);
    const auto* ep = host.findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return {};
    cad::geo::Vec2 w1 = host.transform.toWorld(sp->resolvedPos);
    cad::geo::Vec2 w2 = host.transform.toWorld(ep->resolvedPos);
    cad::geo::Vec2 segDir = w2 - w1;
    if (segDir.length() < 1e-9) return {};
    double theta = std::atan2(segDir.y, segDir.x) + pt.interAngle * M_PI / 180.0;
    cad::geo::Vec2 d{std::cos(theta), std::sin(theta)};
    double denom = d.cross(segDir);
    if (std::abs(denom) < 1e-9) return {};
    cad::geo::Vec2 w = w1 - originWorld;
    double s = w.cross(segDir) / denom;
    double t = w.cross(d) / denom;
    if (t < -1e-6 || t > 1.0 + 1e-6) return {};   // 不在线段上
    if (!pt.interBidirectional && s < -1e-6) return {};  // 反向射线
    *okOut = true;
    return originWorld + d * s;
}

void TestIntersectionUpdate::crossLayerIntersectionFollowsVariable()
{
    // Phase 2.5 (跨层交点重解): the target segment lives on the AUX block, the
    // ray origin on the WORKING block. Changing L moves the origin; the
    // intersection must be re-solved against the NEW origin and stay on-ray,
    // and the 15mm interpolated point must follow.
    CrossLayerDoc fx;
    buildCrossLayerDoc(fx, /*originOnAux=*/false);
    QVERIFY2(fx.attachmentAdded,
             "aux follower -> working leader must be accepted");
    QVERIFY(fx.doc.hasCrossLayerAttachments());   // Phase 3 must be exercised
    cad::param::ParamDocument& doc = fx.doc;

    const cad::param::Block* host = doc.findBlock(fx.targetBlock);
    QVERIFY(host);
    const cad::param::Segment* seg = host->findSegment(fx.targetSegId);
    QVERIFY(seg);
    const cad::param::ParamPoint* ixPt = host->findPoint(fx.interId);
    QVERIFY(ixPt);

    auto snap = [&]() {
        return SnapPos{worldPosOf(doc, fx.originId),
                       worldPosOf(doc, fx.interId),
                       worldPosOf(doc, fx.interpId)};
    };
    auto deviation = [&]() -> double {
        bool ok = false;
        const cad::geo::Vec2 exp =
            expectedHit(*host, *seg, *ixPt, worldPosOf(doc, fx.originId), &ok);
        if (!ok) return -1.0;
        return (worldPosOf(doc, fx.interId) - exp).length();
    };

    // 基线：交点已解析且落在射线×线段上。
    const SnapPos before = snap();
    QVERIFY2(ixPt->resolved, "基线: 交点未解析");
    const double baseDev = deviation();
    QVERIFY2(baseDev >= 0.0 && baseDev < 0.5,
             qPrintable(QStringLiteral("基线交点偏离 %1 mm").arg(baseDev)));

    // ① 引擎级直改参数 L: 8 → 14cm（起点距离公式直接引用）。
    doc.setParameter(QStringLiteral("L"), 14.0);
    const SnapPos after = snap();
    QVERIFY2((after.origin - before.origin).length() > 20.0,
             qPrintable(QStringLiteral("L=14 后射线起点未移动（前提失效, 位移 %1）")
                            .arg((after.origin - before.origin).length())));
    QVERIFY2((after.inter - before.inter).length() > 20.0,
             qPrintable(QStringLiteral("L=14 后交点未更新（位移 %1）")
                            .arg((after.inter - before.inter).length())));
    const double devA = deviation();
    QVERIFY2(devA >= 0.0 && devA < 0.5,
             qPrintable(QStringLiteral("L=14 后交点偏离正确位置 %1 mm").arg(devA)));

    // ② 插值辅助点契约：沿线段距交点 15mm（方向 start→end）。
    const auto* sp = host->findPoint(seg->startPointId);
    const auto* ep = host->findPoint(seg->endPointId);
    QVERIFY(sp && ep && sp->resolved && ep->resolved);
    cad::geo::Vec2 dir = host->transform.toWorld(ep->resolvedPos)
                       - host->transform.toWorld(sp->resolvedPos);
    const double segLen = dir.length();
    QVERIFY(segLen > 1e-9);
    dir = dir / segLen;
    const double along = (after.interp - after.inter).dot(dir);
    qInfo() << "interp along-segment distance" << along << "mm (expect 15)";
    QVERIFY2(std::abs(along - 15.0) < 0.5,
             qPrintable(QStringLiteral("插值辅助点偏离交点 15mm 契约: %1 mm").arg(along)));

    // ③ 恢复参数 → 交点回到原位（同一文档内的往返一致性）。
    doc.setParameter(QStringLiteral("L"), 8.0);
    QVERIFY2(worldPosOf(doc, fx.interId).distanceTo(before.inter) < 0.5,
             "恢复 L=8 后交点未回到基线位置");
    QVERIFY(doc.diagnostics().empty());
}

void TestIntersectionUpdate::sweepMutationsAndDragKeepOnRay()
{
    // 变异扫描 + 拖帧扫描：任何一次参数修改 / 拖动之后，交点必须仍位于
    // （新起点 → 新射线方向）× 新目标线段上，且 15mm 插值点跟随。
    CrossLayerDoc fx;
    buildCrossLayerDoc(fx, /*originOnAux=*/false);
    cad::param::ParamDocument& doc = fx.doc;

    const cad::param::Block* host = doc.findBlock(fx.targetBlock);
    QVERIFY(host);
    const cad::param::Segment* seg = host->findSegment(fx.targetSegId);
    QVERIFY(seg);
    const cad::param::ParamPoint* ixPt = host->findPoint(fx.interId);
    QVERIFY(ixPt);

    auto verify = [&](const char* tag) -> double {
        bool ok = false;
        const cad::geo::Vec2 exp =
            expectedHit(*host, *seg, *ixPt, worldPosOf(doc, fx.originId), &ok);
        if (!ok) { qWarning() << tag << "ray misses segment"; return -1.0; }
        const double dev = (worldPosOf(doc, fx.interId) - exp).length();
        if (dev > 0.01)
            qWarning() << tag << "deviation" << dev
                       << "actual(" << worldPosOf(doc, fx.interId).x << ","
                       << worldPosOf(doc, fx.interId).y << ")";
        return dev;
    };

    // ① 参数扫描：全程 t 必须有效（交点落在目标线段内）。
    for (double L : {4.0, 6.0, 8.0, 12.0, 16.0, 22.0, 28.0}) {
        doc.setParameter(QStringLiteral("L"), L);
        const double dev = verify("param-sweep");
        QVERIFY2(dev >= 0.0 && dev < 0.01,
                 qPrintable(QStringLiteral("L=%1 后交点偏离 %2 mm").arg(L).arg(dev)));
    }
    doc.setParameter(QStringLiteral("L"), 8.0);
    QVERIFY(verify("restored") < 0.01);

    // ② 拖帧扫描：拖工作层根块（整链 + 跨层跟随），逐帧验证。
    const cad::geo::Vec2 step(0.7, -0.4);
    for (int f = 0; f < 15; ++f) {
        cad::param::Block* rb = doc.findBlock(fx.leaderBlock);
        QVERIFY(rb);
        rb->transform.origin += step;
        doc.invalidateLayer(fx.workLayer);
        doc.resolveForDrag(QList<QUuid>{fx.leaderBlock});
        const double dev = verify("drag-frame");
        QVERIFY2(dev >= 0.0 && dev < 0.5,
                 qPrintable(QStringLiteral("拖帧 %1 后交点偏离 %2 mm").arg(f).arg(dev)));
    }
    QVERIFY(doc.diagnostics().empty());
}

void TestIntersectionUpdate::workingSideIntersectionWithAuxOrigin()
{
    // 镜像场景（Phase 4 工作侧跨层交点重解）：目标线段在工作层块上，射线
    // 起点在辅助层块上，而该辅助块经跨层连接跟随工作层链。改变量 → 辅助层
    // 在 Phase 3 沉降 → 工作侧交点必须在沉降之后重解并落在射线上。
    CrossLayerDoc fx;
    buildCrossLayerDoc(fx, /*originOnAux=*/true);
    QVERIFY2(fx.attachmentAdded,
             "aux follower -> working leader must be accepted");
    QVERIFY(fx.doc.hasCrossLayerAttachments());
    cad::param::ParamDocument& doc = fx.doc;

    const cad::param::Block* host = doc.findBlock(fx.targetBlock);
    QVERIFY(host);
    const cad::param::Segment* seg = host->findSegment(fx.targetSegId);
    QVERIFY(seg);
    const cad::param::ParamPoint* ixPt = host->findPoint(fx.interId);
    QVERIFY(ixPt);

    auto verify = [&]() -> double {
        bool ok = false;
        const cad::geo::Vec2 exp =
            expectedHit(*host, *seg, *ixPt, worldPosOf(doc, fx.originId), &ok);
        if (!ok) return -1.0;
        return (worldPosOf(doc, fx.interId) - exp).length();
    };

    const cad::geo::Vec2 before = worldPosOf(doc, fx.interId);
    QVERIFY2(ixPt->resolved, "基线: 工作侧交点未解析");
    const double baseDev = verify();
    QVERIFY2(baseDev >= 0.0 && baseDev < 0.5,
             qPrintable(QStringLiteral("基线工作侧交点偏离 %1 mm").arg(baseDev)));

    // 辅助层起点随 L 移动 → 工作侧交点必须重解（不重解就会漂离射线）。
    for (double L : {5.0, 9.0, 13.0, 18.0, 24.0}) {
        doc.setParameter(QStringLiteral("L"), L);
        const double dev = verify();
        QVERIFY2(dev >= 0.0 && dev < 0.5,
                 qPrintable(QStringLiteral("L=%1 后工作侧交点偏离 %2 mm").arg(L).arg(dev)));
        QVERIFY2(worldPosOf(doc, fx.interId).distanceTo(before) > 1.0,
                 qPrintable(QStringLiteral("L=%1 后工作侧交点未更新").arg(L)));
    }
    QVERIFY(doc.diagnostics().empty());
}

void TestIntersectionUpdate::sameLayerIntersectionFollowsVariable()
{
    using cad::param::Block;
    using cad::param::ParamPoint;
    using cad::param::PointConstraint;
    using cad::param::Segment;
    using cad::param::ParamDocument;
    using cad::geo::Vec2;

    ParamDocument doc;
    QUuid workLayer;
    for (const auto& l : doc.layers())
        if (l.type == cad::param::LayerType::Working) { workLayer = l.id; break; }
    QVERIFY(!workLayer.isNull());

    // ── 目标块 T: (0,0)-(300,0) ──
    Block tb;
    tb.layer = workLayer;
    ParamPoint t1; t1.constraint = PointConstraint::Free; t1.freePos = Vec2(0, 0);
    const QUuid t1Id = t1.id;
    ParamPoint t2; t2.constraint = PointConstraint::Free; t2.freePos = Vec2(300, 0);
    const QUuid t2Id = t2.id;
    tb.addPoint(t1);
    tb.addPoint(t2);
    Segment tseg; tseg.startPointId = t1Id; tseg.endPointId = t2Id;
    const QUuid tsegId = tseg.id;
    tb.addSegment(tseg);
    const QUuid tbId = tb.id;
    doc.addBlock(tb);

    // ── 起点块 O: 锚 (0,-100) + Polar(0°, 距离公式 L) → 起点 (L*10, -100) ──
    Block ob;
    ob.layer = workLayer;
    ParamPoint o1; o1.constraint = PointConstraint::Free; o1.freePos = Vec2(0, -100);
    const QUuid o1Id = o1.id;
    ParamPoint o2; o2.constraint = PointConstraint::Polar;
    o2.refPointId = o1Id;
    o2.distance = 0.0;
    o2.distanceFormula = QStringLiteral("L");
    o2.angle = 0.0;
    const QUuid o2Id = o2.id;
    ob.addPoint(o1);
    ob.addPoint(o2);
    Segment oseg; oseg.startPointId = o1Id; oseg.endPointId = o2Id;
    ob.addSegment(oseg);
    doc.addBlock(ob);

    doc.setParameter(QStringLiteral("L"), 8.0);

    // ── 交点挂 T 段, 起点 = o2, 90° (段相对 = 世界向上) ──
    cad::param::Block* tmut = doc.findBlock(tbId);
    QVERIFY(tmut);
    ParamPoint ix; ix.constraint = PointConstraint::Intersection;
    ix.refPointA = o2Id;
    ix.hostSegmentId = tsegId;
    ix.interAngle = 90.0;
    ix.interBidirectional = false;
    ix.isAuxiliary = true;
    ix.visible = true;
    const QUuid ixId = ix.id;
    tmut->addPoint(ix);
    tmut->findSegment(tsegId)->auxPointIds.push_back(ixId);
    doc.resolveAll();

    auto worldPos = [&](const QUuid& id) -> cad::geo::Vec2 {
        for (const auto& b : doc.blocks())
            if (const auto* p = b.findPoint(id))
                return b.worldPos(id);
        return {};
    };
    const auto* host = doc.findBlock(tbId);
    QVERIFY(host);
    const auto* seg = host->findSegment(tsegId);
    QVERIFY(seg);
    const auto* ixPt = host->findPoint(ixId);
    QVERIFY(ixPt);
    auto verify = [&](const char* tag) -> double {
        bool ok = false;
        const Vec2 exp = expectedHit(*host, *seg, *ixPt, worldPos(o2Id), &ok);
        if (!ok) { qWarning() << tag << "ray misses segment"; return -1.0; }
        const Vec2 act = worldPos(ixId);
        const double dev = (act - exp).length();
        if (dev > 0.01)
            qWarning() << tag << "deviation" << dev
                       << "actual(" << act.x << "," << act.y << ")"
                       << "expected(" << exp.x << "," << exp.y << ")";
        return dev;
    };

    // A) 跨块同层: 起点随 L 移动, 交点必须滑动到新交叉 (t=0.17..0.67 全程有效).
    const Vec2 before = worldPos(ixId);
    double lastDev = -1.0;
    for (double L : {5.0, 8.0, 12.0, 15.0, 20.0}) {
        doc.setParameter(QStringLiteral("L"), L);
        const double dev = verify("same-layer A");
        QVERIFY2(dev >= 0.0 && dev < 0.01,
                 qPrintable(QStringLiteral("同层跨块 L=%1 后交点偏离 %2 mm").arg(L).arg(dev)));
        lastDev = dev;
    }
    const Vec2 after = worldPos(ixId);
    QVERIFY2((after - before).length() > 20.0,
             qPrintable(QStringLiteral("交点应随变量移动 (位移 %1 mm)").arg((after - before).length())));
    qInfo() << "same-layer A last deviation" << lastDev;

    // B) 同块: 起点与目标段同块 (Block::resolve 本地交点分支).
    Block cb;
    cb.layer = workLayer;
    ParamPoint c1; c1.constraint = PointConstraint::Free; c1.freePos = Vec2(0, 0);
    const QUuid c1Id = c1.id;
    ParamPoint c2; c2.constraint = PointConstraint::Free; c2.freePos = Vec2(300, 0);
    const QUuid c2Id = c2.id;
    ParamPoint ca; ca.constraint = PointConstraint::Free; ca.freePos = Vec2(30, -80);
    const QUuid caId = ca.id;
    ParamPoint co; co.constraint = PointConstraint::Polar;
    co.refPointId = caId;
    co.distance = 0.0;
    co.distanceFormula = QStringLiteral("L");
    co.angle = 45.0;
    const QUuid coId = co.id;
    cb.addPoint(c1);
    cb.addPoint(c2);
    cb.addPoint(ca);
    cb.addPoint(co);
    Segment cseg; cseg.startPointId = c1Id; cseg.endPointId = c2Id;
    const QUuid csegId = cseg.id;
    cb.addSegment(cseg);
    // 起点块自身线段 (Polar 端) — 需要存在, 但不挂交点.
    Segment cseg2; cseg2.startPointId = caId; cseg2.endPointId = coId;
    cb.addSegment(cseg2);
    const QUuid cbId = cb.id;
    doc.addBlock(cb);

    // A 场景循环结束时 L=20 (起点升到线段上方, 向上射线必然空交) — 先复位.
    doc.setParameter(QStringLiteral("L"), 8.0);

    cad::param::Block* cmut = doc.findBlock(cbId);
    QVERIFY(cmut);
    ParamPoint ix2; ix2.constraint = PointConstraint::Intersection;
    ix2.refPointA = coId;
    ix2.hostSegmentId = csegId;
    ix2.interAngle = 90.0;
    ix2.interBidirectional = false;
    ix2.isAuxiliary = true;
    ix2.visible = true;
    const QUuid ix2Id = ix2.id;
    cmut->addPoint(ix2);
    cmut->findSegment(csegId)->auxPointIds.push_back(ix2Id);
    doc.resolveAll();

    const auto* host2 = doc.findBlock(cbId);
    const auto* seg2 = host2->findSegment(csegId);
    const auto* ix2Pt = host2->findPoint(ix2Id);
    {
        // 诊断: 同块场景失败定位.
        const auto* coPt = host2->findPoint(coId);
        const auto* caPt = host2->findPoint(caId);
        const auto* c1Pt = host2->findPoint(c1Id);
        const auto* c2Pt = host2->findPoint(c2Id);
        qInfo() << "B debug: ix2 resolved =" << bool(ix2Pt && ix2Pt->resolved)
                << "co resolved =" << bool(coPt && coPt->resolved)
                << "ca resolved =" << bool(caPt && caPt->resolved)
                << "c1 resolved =" << bool(c1Pt && c1Pt->resolved)
                << "c2 resolved =" << bool(c2Pt && c2Pt->resolved)
                << "para L =" << doc.parameters().value("L", -1.0);
        if (coPt && coPt->resolved)
            qInfo() << "B debug: origin local (" << coPt->resolvedPos.x << "," << coPt->resolvedPos.y << ")";
        if (caPt && caPt->resolved)
            qInfo() << "B debug: anchor local (" << caPt->resolvedPos.x << "," << caPt->resolvedPos.y << ")";
    }
    QVERIFY(host2 && seg2 && ix2Pt && ix2Pt->resolved);
    auto verify2 = [&](const char* tag) -> double {
        bool ok = false;
        const Vec2 exp = expectedHit(*host2, *seg2, *ix2Pt, worldPos(coId), &ok);
        if (!ok) { qWarning() << tag << "ray misses segment"; return -1.0; }
        const Vec2 act = worldPos(ix2Id);
        const double dev = (act - exp).length();
        if (dev > 0.01)
            qWarning() << tag << "deviation" << dev
                       << "actual(" << act.x << "," << act.y << ")"
                       << "expected(" << exp.x << "," << exp.y << ")";
        return dev;
    };
    qInfo() << "same-layer B baseline deviation" << verify2("same-layer B baseline");
    QVERIFY(verify2("same-layer B baseline") < 0.01);
    const Vec2 before2 = worldPos(ix2Id);
    for (double L : {6.0, 9.0, 11.0}) {
        doc.setParameter(QStringLiteral("L"), L);
        const double dev = verify2("same-layer B");
        QVERIFY2(dev >= 0.0 && dev < 0.01,
                 qPrintable(QStringLiteral("同层同块 L=%1 后交点偏离 %2 mm").arg(L).arg(dev)));
    }
    const Vec2 after2 = worldPos(ix2Id);
    QVERIFY2((after2 - before2).length() > 10.0,
             qPrintable(QStringLiteral("同块交点应随变量移动 (位移 %1 mm)").arg((after2 - before2).length())));
}


// ---------------------------------------------------------------------------
// P612 族自环复现 (TROUBLESHOOTING: Polar 端点锚在本线段交点辅助点 → 闭环):
// 目标段端点 c2 = Polar(ref = 交点 ix3, −45°, 200mm) — 端点依赖交点, 交点依赖
// 该段。变量修改后整条闭环必须仍收敛到新交叉 (用户文档同族结构)。
void TestIntersectionUpdate::sameLayerCycleEndpointRefsIntersection()
{
    using cad::param::Block;
    using cad::param::ParamPoint;
    using cad::param::PointConstraint;
    using cad::param::Segment;
    using cad::param::ParamDocument;
    using cad::geo::Vec2;

    ParamDocument doc;
    QUuid workLayer;
    for (const auto& l : doc.layers())
        if (l.type == cad::param::LayerType::Working) { workLayer = l.id; break; }

    Block cb;
    cb.layer = workLayer;
    ParamPoint c1; c1.constraint = PointConstraint::Free; c1.freePos = Vec2(0, 0);
    const QUuid c1Id = c1.id;
    ParamPoint ca; ca.constraint = PointConstraint::Free; ca.freePos = Vec2(30, -80);
    const QUuid caId = ca.id;
    ParamPoint co; co.constraint = PointConstraint::Polar;
    co.refPointId = caId;
    co.distance = 0.0;
    co.distanceFormula = QStringLiteral("L");
    co.angle = 45.0;
    const QUuid coId = co.id;
    Segment cseg; cseg.startPointId = c1Id; cseg.endPointId = QUuid();
    const QUuid csegId = cseg.id;
    Segment cseg2; cseg2.startPointId = caId; cseg2.endPointId = coId;
    const QUuid cseg2Id = cseg2.id;

    ParamPoint ix3; ix3.constraint = PointConstraint::Intersection;
    ix3.refPointA = coId;
    ix3.hostSegmentId = csegId;   // ← 宿主段 (c1→c2), 缺省不设会在种子判定里失败
    ix3.interAngle = 90.0;
    ix3.interBidirectional = false;
    ix3.isAuxiliary = true;
    ix3.visible = true;
    const QUuid ix3Id = ix3.id;
    // c2 锚定 ix3 (闭环) — 先创建, 后由其 ref 值构成段.
    // +45° (向上): 闭环存在可达固定点 (交点 = 射线×段, s>0); −45° 的镜像
    // 数学上无有效解 (交点在射线反向), 引擎保持未解析属正确行为.
    ParamPoint c2; c2.constraint = PointConstraint::Polar;
    c2.refPointId = ix3Id;              // ← 端点依赖交点
    c2.distance = 200.0;
    c2.angle = 45.0;
    const QUuid c2Id = c2.id;

    cb.addPoint(c1);
    cb.addPoint(ca);
    cb.addPoint(co);
    cb.addPoint(ix3);
    cb.addPoint(c2);

    cseg.endPointId = c2Id;
    cb.addSegment(cseg);
    cb.addSegment(cseg2);
    const QUuid cbId = cb.id;
    doc.addBlock(cb);
    doc.setParameter(QStringLiteral("L"), 8.0);
    doc.resolveAll();

    auto worldPos = [&](const QUuid& id) -> cad::geo::Vec2 {
        for (const auto& b : doc.blocks())
            if (const auto* p = b.findPoint(id))
                return b.worldPos(id);
        return {};
    };
    auto verify = [&](const char* tag) -> double {
        const auto* host = doc.findBlock(cbId);
        const auto* seg = host ? host->findSegment(csegId) : nullptr;
        const auto* ix = host ? host->findPoint(ix3Id) : nullptr;
        bool ok = false;
        if (!ix || !ix->resolved) {
            qWarning() << tag << "intersection unresolved";
            return -1000.0;
        }
        const Vec2 exp = expectedHit(*host, *seg, *ix, worldPos(coId), &ok);
        if (!ok) { qWarning() << tag << "ray misses segment"; return -1000.0; }
        const Vec2 act = worldPos(ix3Id);
        const double dev = (act - exp).length();
        if (dev > 0.01)
            qWarning() << tag << "deviation" << dev
                       << "actual(" << act.x << "," << act.y << ")"
                       << "expected(" << exp.x << "," << exp.y << ")";
        return dev;
    };

    qInfo() << "cycle baseline deviation" << verify("cycle baseline");
    QVERIFY(verify("cycle baseline") < 0.5);
    const Vec2 before = worldPos(ix3Id);
    double last = -1.0;
    for (double L : {5.0, 8.0, 11.0, 13.0}) {
        doc.setParameter(QStringLiteral("L"), L);
        const double dev = verify("cycle var");
        QVERIFY2(dev >= 0.0 && dev < 0.5,
                 qPrintable(QStringLiteral("闭环 L=%1 后交点偏离 %2 mm").arg(L).arg(dev)));
        last = dev;
    }
    const Vec2 after = worldPos(ix3Id);
    QVERIFY2((after - before).length() > 5.0,
             qPrintable(QStringLiteral("闭环交点应随变量移动 (位移 %1 mm)").arg((after - before).length())));
    qInfo() << "cycle last deviation" << last;
}


// ---------------------------------------------------------------------------
// 渲染路径验证（用户 2026-08-24: 现场会话交点不动、保存重载后正常 → 怀疑
// 引擎动了但画布没刷）。引擎侧断言 on-ray 之后, 渲染交点区域像素:
// 变脸前后必须有像素差异 (交点移动了), 且新位置出现绿色辅助点像素。
void TestIntersectionUpdate::canvasRepaintsIntersectionAfterVariable()
{
    using cad::geo::Vec2;

    // 渲染侧回归：交点移动后，画布必须在新位置画出辅助点（引擎动了画布得刷）。
    // 文档是本测试自建的跨层合成档（不再依赖用户存档）。
    CrossLayerDoc fx;
    buildCrossLayerDoc(fx, /*originOnAux=*/false);
    cad::param::ParamDocument& doc = fx.doc;

    CanvasScene scene(&doc);
    for (const auto& b : doc.blocks())
        scene.addBlockItem(b.id);
    scene.syncBlockPositions();

    auto renderRegion = [&](const Vec2& center) {
        constexpr double kScale = 4.0;
        const double half = 45.0;
        const QRectF region(center.x - half, -(center.y + half),
                            half * 2, half * 2);
        QImage img(static_cast<int>(region.width() * kScale),
                   static_cast<int>(region.height() * kScale),
                   QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        scene.render(&p, img.rect(), region);
        p.end();
        return img;
    };

    const Vec2 beforePos = worldPosOf(doc, fx.interId);

    // 改变量 → 交点移动（引擎侧）。
    doc.setParameter(QStringLiteral("L"), 20.0);
    scene.syncBlockPositions();

    const Vec2 afterPos = worldPosOf(doc, fx.interId);
    const QImage imgAfter = renderRegion(afterPos);
    QVERIFY2((afterPos - beforePos).length() > 5.0,
             qPrintable(QStringLiteral("引擎: 交点未随 L 移动 (位移 %1 mm)")
                            .arg((afterPos - beforePos).length())));

    // 渲染侧: 同一块区域在"交点尚未移过来"时必须与"移过来之后"不同 ——
    // 即画布确实在新位置重绘了。用像素差分而不是硬编码辅助点颜色:
    // 辅助层非活动时的点色随主题/状态变化（旧用例硬编码绿色, 换个合成档就
    // 假失败），而"重绘了没有"才是这个用例真正要守的行为。
    auto inkPixels = [](const QImage& img) {
        long n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (img.pixelColor(x, y) != Qt::white) ++n;
        return n;
    };

    doc.setParameter(QStringLiteral("L"), 8.0);   // 交点移回, 同一区域应重画
    scene.syncBlockPositions();
    const QImage imgRegionBefore = renderRegion(afterPos);

    const long inkAfter = inkPixels(imgAfter);
    const long inkBefore = inkPixels(imgRegionBefore);
    qInfo() << "render check: ink pixels at new intersection position —"
            << "after L=20:" << inkAfter << ", at L=8 (point moved away):" << inkBefore;
    QVERIFY2(inkAfter > 0, "画布在新交点位置什么都没画");
    QVERIFY2(inkAfter != inkBefore,
             qPrintable(QStringLiteral("画布未随交点移动而重绘该区域 (像素数 %1 vs %2)")
                            .arg(inkAfter).arg(inkBefore)));
}

void TestIntersectionUpdate::liveSequenceToolCreatedIntersectionFollows()
{
    using cad::geo::Vec2;

    // 现场序列复现：交点经 AddAuxPointCommand（交点工具的创建路径）加入文档
    // 后，改变量 → 该交点必须跟随，且原有交点不被破坏。
    CrossLayerDoc fx;
    buildCrossLayerDoc(fx, /*originOnAux=*/false);
    cad::param::ParamDocument& doc = fx.doc;

    const cad::param::Block* host = doc.findBlock(fx.targetBlock);
    QVERIFY(host);
    const cad::param::ParamPoint* orig = host->findPoint(fx.interId);
    QVERIFY(orig);

    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::Intersection;
    pt.serial = doc.newPointSerial();
    pt.isAuxiliary = true;
    pt.visible = true;
    pt.showName = false;
    pt.refPointA = fx.originId;
    pt.hostSegmentId = fx.targetSegId;
    pt.interAngle = orig->interAngle + 15.0;   // 与原交点错开，便于独立校验
    pt.interBidirectional = orig->interBidirectional;
    const QUuid newId = pt.id;

    const size_t blocksBefore = doc.blocks().size();
    QUndoStack stack;
    stack.push(new cad::cmd::AddAuxPointCommand(&doc, fx.targetBlock,
                                                fx.targetSegId, pt));
    QCOMPARE(doc.blocks().size(), blocksBefore);   // 未增块，仅加辅助点

    auto deviationOf = [&](const QUuid& id) -> double {
        const cad::param::Block* h = doc.findBlock(fx.targetBlock);
        const cad::param::Segment* s = h ? h->findSegment(fx.targetSegId) : nullptr;
        const cad::param::ParamPoint* ix = h ? h->findPoint(id) : nullptr;
        if (!s || !ix || !ix->resolved) return -1000.0;
        bool ok = false;
        const Vec2 exp = expectedHit(*h, *s, *ix, worldPosOf(doc, fx.originId), &ok);
        if (!ok) return -1000.0;
        return (worldPosOf(doc, id) - exp).length();
    };

    const Vec2 b0 = worldPosOf(doc, newId);
    QVERIFY2(deviationOf(newId) < 0.5, "新建交点未解析");

    doc.setParameter(QStringLiteral("L"), 16.0);
    const Vec2 b1 = worldPosOf(doc, newId);
    QVERIFY2(deviationOf(newId) < 0.5,
             qPrintable(QStringLiteral("工具创建的交点改变量后偏离 %1 mm")
                            .arg(deviationOf(newId))));
    QVERIFY2((b1 - b0).length() > 3.0,
             qPrintable(QStringLiteral("工具创建的交点未随变量移动 (位移 %1 mm)")
                            .arg((b1 - b0).length())));

    // 原交点同时保持 on-ray（新建不破坏既有结构）。
    const double devOrig = deviationOf(fx.interId);
    QVERIFY2(devOrig >= 0.0 && devOrig < 0.5,
             qPrintable(QStringLiteral("原交点在新建+改变量后偏离 %1 mm").arg(devOrig)));
    qInfo() << "live-sequence: new inter moved" << (b1 - b0).length()
            << "mm; original dev" << devOrig;

    // undo 必须收回新建的交点。
    stack.undo();
    QVERIFY(!doc.findBlock(fx.targetBlock)->findPoint(newId));
}

void TestIntersectionUpdate::absoluteWorldAngleIntersectionStaysAbsolute()
{
    using cad::param::ParamDocument;
    using cad::param::Block;
    using cad::param::ParamPoint;
    using cad::param::Segment;
    using cad::param::PointConstraint;
    using cad::geo::Vec2;

    ParamDocument doc;
    QUuid workLayer;
    for (const auto& l : doc.layers())
        if (l.type == cad::param::LayerType::Working) { workLayer = l.id; break; }
    QVERIFY(!workLayer.isNull());

    Block tb;
    tb.layer = workLayer;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2(0.0, 0.0);
    const QUuid p1Id = p1.id;
    ParamPoint p2;
    p2.constraint = PointConstraint::Free;
    p2.freePos = Vec2(300.0, 0.0);
    const QUuid p2Id = p2.id;
    tb.addPoint(p1);
    tb.addPoint(p2);

    Segment seg;
    seg.startPointId = p1Id;
    seg.endPointId = p2Id;
    const QUuid segId = seg.id;
    tb.addSegment(seg);

    ParamPoint origin;
    origin.constraint = PointConstraint::Free;
    origin.freePos = Vec2(150.0, -100.0);
    const QUuid originId = origin.id;
    tb.addPoint(origin);

    ParamPoint ix;
    ix.constraint = PointConstraint::Intersection;
    ix.refPointA = originId;
    ix.hostSegmentId = segId;
    ix.interAngle = 90.0;              // 世界角度：正上方
    ix.interUseWorldAngle = true;
    ix.isAuxiliary = true;
    ix.visible = true;
    const QUuid ixId = ix.id;
    tb.addPoint(ix);
    tb.findSegment(segId)->auxPointIds.push_back(ixId);

    const QUuid tbId = tb.id;
    doc.addBlock(std::move(tb));
    doc.resolveAll();

    auto worldPos = [&](const QUuid& id) -> Vec2 {
        for (const auto& b : doc.blocks())
            if (const auto* p = b.findPoint(id))
                return b.worldPos(id);
        return {};
    };
    auto rayDeg = [&]() {
        const Vec2 o = worldPos(originId);
        const Vec2 p = worldPos(ixId);
        return std::atan2(p.y - o.y, p.x - o.x) * 180.0 / M_PI;
    };

    QVERIFY2(std::abs(rayDeg() - 90.0) < 0.01,
             qPrintable(QStringLiteral("绝对角度交点初始射线方向 %1°，应约 90°").arg(rayDeg())));

    // 旋转整个目标块 30°；绝对世界角射线方向不应改变。
    Block* b = doc.findBlock(tbId);
    QVERIFY(b);
    b->transform.rotation += 30.0 * M_PI / 180.0;
    doc.resolveAll();

    const double r = rayDeg();
    QVERIFY2(std::abs(r - 90.0) < 0.01,
             qPrintable(QStringLiteral("旋转后绝对角度交点射线方向 %1°，仍应约 90°").arg(r)));
}


QTEST_MAIN(TestIntersectionUpdate)
#include "test_intersection_update.moc"
