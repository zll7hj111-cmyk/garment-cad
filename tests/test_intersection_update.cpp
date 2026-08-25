/// @file test_intersection_update.cpp
/// 真实文档回归：跨图层交点随参数变化联动更新。
///
/// 场景（用户 2026-08 报告，E:\3.gcad）：辅助层上的射线-线段交点
/// （目标线段在辅助层块上），射线起点位于工作层（距离公式引用
/// 肩褶E）。修改变量 → 工作层起点移动 → 交点位置必须同步更新
/// （Phase 2.5 跨层交点重解）。本测试量化验证：
///   ① 起点确实移动；
///   ② 交点移动且仍位于（新起点 → 新射线方向）与目标线段的交点；
///   ③ 引用交点的插值辅助点（P190，距交点 15mm 沿线段）同步跟随。
/// 依赖 E:/3.gcad 存在（不存在则跳过）。

#include <QtTest>
#include <QFileInfo>
#include <QPainter>

#include <cmath>

#include "document/DocumentFile.h"
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
    const QString path = QStringLiteral("e:/3.gcad");
    if (!QFileInfo::exists(path))
        QSKIP("e:/3.gcad 不存在，跳过");

    cad::param::ParamDocument doc;
    QString err;
    QStringList warnings;
    QVERIFY2(cad::doc::DocumentFile::load(path, doc, &err, &warnings),
             qPrintable(QStringLiteral("加载 %1 失败: %2").arg(path, err)));

    // 定位跨层交点：Intersection 且射线起点不在同一块的层组里。
    QUuid interId, originId, interpId;
    const cad::param::Block* hostBlock = nullptr;
    for (const auto& b : doc.blocks()) {
        for (const auto& p : b.points) {
            if (p.constraint != cad::param::PointConstraint::Intersection) continue;
            if (p.refPointA.isNull()) continue;
            // 射线起点在另一块（跨块交点）。
            for (const auto& ob : doc.blocks()) {
                const auto* op = ob.findPoint(p.refPointA);
                if (op && &ob != &b) {
                    interId = p.id;
                    originId = p.refPointA;
                    hostBlock = &b;
                    break;
                }
            }
            if (!interId.isNull()) break;
        }
        if (!interId.isNull()) break;
    }
    QVERIFY2(!interId.isNull(), "未找到跨块交点（e:/3.gcad 与预期不符）");
    QVERIFY(hostBlock);

    // 引用交点的插值辅助点（沿线段 15mm; 用户后续编辑可能移除, 存在才校验）。
    for (const auto& b : doc.blocks())
        for (const auto& p : b.points)
            if (p.constraint == cad::param::PointConstraint::Interpolated
                && p.interpRefPointId == interId)
                interpId = p.id;

    const cad::param::ParamPoint* interPt = hostBlock->findPoint(interId);
    QVERIFY(interPt);
    const cad::param::Segment* seg = hostBlock->findSegment(interPt->hostSegmentId);
    QVERIFY(seg);

    auto worldPos = [&](const QUuid& id) -> cad::geo::Vec2 {
        for (const auto& b : doc.blocks())
            if (const auto* p = b.findPoint(id))
                return b.worldPos(id);
        return {};
    };
    auto snap = [&]() {
        return SnapPos{worldPos(originId), worldPos(interId), worldPos(interpId)};
    };
    auto dump = [&](const char* tag, const SnapPos& s, const cad::geo::Vec2& exp) {
        qInfo() << tag << "origin(" << s.origin.x << "," << s.origin.y << ")"
                << "inter(" << s.inter.x << "," << s.inter.y << ")"
                << "interp(" << s.interp.x << "," << s.interp.y << ")"
                << "expected(" << exp.x << "," << exp.y << ")";
    };


    // 基线状态：交点应已位于射线×线段上。
    {
        bool ok = false;
        cad::geo::Vec2 exp = expectedHit(*hostBlock, *seg, *interPt, worldPos(originId), &ok);
        QVERIFY2(ok, "基线：交点不在射线×线段上（文档初始状态异常）");
        const double dev = (worldPos(interId) - exp).length();
        qInfo() << "baseline deviation" << dev << "mm";
        QVERIFY2(dev < 0.5, qPrintable(QStringLiteral("基线交点偏离 %1 mm").arg(dev)));
    }

    // ① 引擎级直改参数：肩褶E 9.95 → 12.0（起点距离公式直接引用）。
    const double oldShoulderE = doc.parameters().value(QString::fromUtf8("肩褶E"), -1.0);
    QVERIFY2(oldShoulderE > 0.0, "缺少参数 肩褶E");
    const SnapPos before = snap();
    doc.setParameter(QString::fromUtf8("肩褶E"), 12.0);
    bool ok = false;
    cad::geo::Vec2 exp = expectedHit(*hostBlock, *seg, *interPt, worldPos(originId), &ok);
    const SnapPos after = snap();
    dump("[A] set 肩褶E=12", after, exp);
    QVERIFY2((after.origin - before.origin).length() > 0.5,
             "肩褶E=12 后射线起点未移动（前提失效）");
    QVERIFY2(ok, "肩褶E=12 后射线不再穿过目标线段（t/s 无效）");
    const double devA = (after.inter - exp).length();
    // 交点必须移动（用户报告的 bug = 不更新）；且必须落在正确位置上。
    QVERIFY2((after.inter - before.inter).length() > 0.5,
             qPrintable(QStringLiteral("肩褶E=12 后交点未更新（原位置 %1,%2，新位置 %3,%4）")
                            .arg(before.inter.x).arg(before.inter.y)
                            .arg(after.inter.x).arg(after.inter.y)));
    QVERIFY2(devA < 0.5, qPrintable(QStringLiteral("肩褶E=12 后交点偏离正确位置 %1 mm").arg(devA)));

    // ② 恢复，走真实变量路径：胸围(B) 840 → 880 mm，公式链重算后检查。
    doc.setParameter(QString::fromUtf8("肩褶E"), oldShoulderE);
    for (const auto& v : doc.variables()) {
        if (v.refName == QLatin1String("B")) {
            cad::param::Variable nv = v;
            nv.value = 880.0;
            doc.updateVariable(nv);
            break;
        }
    }
    exp = expectedHit(*hostBlock, *seg, *interPt, worldPos(originId), &ok);
    const SnapPos after2 = snap();
    dump("[B] B=88cm", after2, exp);
    QVERIFY2((after2.origin - before.origin).length() > 0.5,
             "B=88 后射线起点未移动（前提失效）");
    QVERIFY2(ok, "B=88 后射线不再穿过目标线段（t/s 无效）");
    const double devB = (after2.inter - exp).length();
    QVERIFY2((after2.inter - before.inter).length() > 0.5,
             qPrintable(QStringLiteral("B=88 后交点未更新（原位置 %1,%2，新位置 %3,%4）")
                            .arg(before.inter.x).arg(before.inter.y)
                            .arg(after2.inter.x).arg(after2.inter.y)));
    QVERIFY2(devB < 0.5, qPrintable(QStringLiteral("B=88 后交点偏离正确位置 %1 mm").arg(devB)));

    // ③ 插值辅助点跟随（存在才校验）：沿线段距交点 15mm（方向 start→end）。
    if (const auto* interpPt = hostBlock->findPoint(interpId)) {
        const auto* sp = hostBlock->findPoint(seg->startPointId);
        const auto* ep = hostBlock->findPoint(seg->endPointId);
        QVERIFY(sp && ep && sp->resolved && ep->resolved);
        cad::geo::Vec2 w1 = hostBlock->transform.toWorld(sp->resolvedPos);
        cad::geo::Vec2 w2 = hostBlock->transform.toWorld(ep->resolvedPos);
        cad::geo::Vec2 dir = w2 - w1;
        const double segLen = dir.length();
        QVERIFY(segLen > 1e-9);
        dir = dir / segLen;
        const double along = (after2.interp - after2.inter).dot(dir);
        qInfo() << "interp along-segment distance" << along << "mm (expect ~15)";
        QVERIFY2(std::abs(along - 15.0) < 0.5,
                 qPrintable(QStringLiteral("插值辅助点偏离交点 15mm 契约: %1 mm").arg(along)));
    }
}

// 变异扫描 + 拖帧扫描：任何一次参数修改 / 拖动后，交点必须仍位于
// （新起点 → 新射线方向）× 新目标线段，且 15mm 插值点跟随。
void TestIntersectionUpdate::sweepMutationsAndDragKeepOnRay()  // private slot
{
    const QString path = QStringLiteral("e:/3.gcad");
    if (!QFileInfo::exists(path))
        QSKIP("e:/3.gcad 不存在，跳过");

    cad::param::ParamDocument doc;
    QString err;
    QStringList warnings;
    QVERIFY(cad::doc::DocumentFile::load(path, doc, &err, &warnings));

    QUuid interId, originId, interpId;
    const cad::param::Block* hostBlock = nullptr;
    for (const auto& b : doc.blocks())
        for (const auto& p : b.points)
            if (p.constraint == cad::param::PointConstraint::Intersection
                && !p.refPointA.isNull()) {
                interId = p.id;
                originId = p.refPointA;
                hostBlock = &b;
                break;
            }
    QVERIFY2(!interId.isNull(), "未找到交点");
    for (const auto& b : doc.blocks())
        for (const auto& p : b.points)
            if (p.constraint == cad::param::PointConstraint::Interpolated
                && p.interpRefPointId == interId)
                interpId = p.id;   // 可选: 存在才校验 15mm 跟随

    const cad::param::ParamPoint* interPt = hostBlock->findPoint(interId);
    QVERIFY(interPt);
    const cad::param::Segment* seg = hostBlock->findSegment(interPt->hostSegmentId);
    QVERIFY(seg);

    auto worldPos = [&](const QUuid& id) -> cad::geo::Vec2 {
        for (const auto& b : doc.blocks())
            if (const auto* p = b.findPoint(id))
                return b.worldPos(id);
        return {};
    };
    auto verify = [&](const char* tag) -> double {
        bool ok = false;
        const cad::geo::Vec2 exp =
            expectedHit(*hostBlock, *seg, *interPt, worldPos(originId), &ok);
        if (!ok) {
            qWarning() << tag << "ray misses segment after mutation";
            return -1.0;
        }
        const double dev = (worldPos(interId) - exp).length();
        if (dev > 0.5)
            qWarning() << tag << "deviation" << dev
                       << "actual(" << worldPos(interId).x << "," << worldPos(interId).y
                       << ") expected(" << exp.x << "," << exp.y << ")";
        return dev;
    };

    // --- ① 全部基础变量 ×1.05 / 明显扰动，逐项验证 ---
    const auto vars = doc.variables();
    for (const auto& v : vars) {
        cad::param::Variable nv = v;
        nv.value = v.value * 1.05 + 5.0;
        doc.updateVariable(nv);
        const double dev = verify("var-sweep");
        QVERIFY2(dev >= 0.0 && dev < 0.5,
                 qPrintable(QStringLiteral("变量 %1 修改后交点偏离 %2 mm")
                                .arg(v.name, QString::number(dev))));
        doc.updateVariable(v);  // 恢复
    }
    QVERIFY(verify("restored") < 0.5);

    // --- ② 拖帧扫描：拖工作层根块（整链 + 跨层跟随），逐帧验证 ---
    QUuid root;
    {
        QSet<QUuid> followers;
        for (const auto& a : doc.attachments())
            followers.insert(a.fromBlockId);
        for (const auto& b : doc.blocks()) {
            if (b.layer == doc.auxLayerId()) continue;
            if (followers.contains(b.id)) continue;   // 有 incoming
            root = b.id;
            break;
        }
    }
    QVERIFY2(!root.isNull(), "未找到工作层自由根");
    qInfo() << "drag root" << root.toString();

    const cad::geo::Vec2 step(0.7, -0.4);
    double lastDev = 0.0;
    for (int f = 0; f < 15; ++f) {
        cad::param::Block* rb = doc.findBlock(root);
        QVERIFY(rb);
        rb->transform.origin += step;
        doc.invalidateLayer(rb->layer);
        doc.resolveForDrag({root});
        const double dev = verify("drag-frame");
        QVERIFY2(dev >= 0.0 && dev < 0.5,
                 qPrintable(QStringLiteral("拖帧 %1 后交点偏离 %2 mm").arg(f).arg(dev)));
        lastDev = dev;
    }
    qInfo() << "drag sweep last deviation" << lastDev;
}

// 工作侧交点 + 辅助层射线起点（镜像场景）：目标线段在工作层块上，射线
// 起点为辅助层块上的点（该辅助块经跨层连接跟随工作层链）。变量修改 →
// 辅助层在 Phase 3 才随工作层沉降 → 工作侧交点必须在沉降后重解并落在
// 射线上（Phase 3b）。
void TestIntersectionUpdate::workingSideIntersectionWithAuxOrigin()
{
    const QString path = QStringLiteral("e:/3.gcad");
    if (!QFileInfo::exists(path))
        QSKIP("e:/3.gcad 不存在，跳过");

    cad::param::ParamDocument doc;
    QString err;
    QStringList warnings;
    QVERIFY(cad::doc::DocumentFile::load(path, doc, &err, &warnings));

    // 从 3.gcad 现成结构：取辅助块 3fe15b56 的 Free 点 (93148742) 作射线起点，
    // 目标线段取工作层块 d8f6c44e 的线段 (bd95ec08)。构造工作侧交点。
    QUuid auxOriginId, workSegBlock, workSegId;
    const QUuid auxBlock = QUuid::fromString("3fe15b56-d2ad-4902-a195-761f06dbec4a");
    for (const auto& b : doc.blocks()) {
        if (b.id == auxBlock)
            for (const auto& p : b.points)
                if (p.constraint == cad::param::PointConstraint::Free) auxOriginId = p.id;
    }
    for (const auto& b : doc.blocks()) {
        if (b.layer == doc.auxLayerId()) continue;
        if (b.id == auxBlock) continue;
        workSegBlock = b.id;
        if (!b.segments.empty()) { workSegId = b.segments.front().id; break; }
    }
    QVERIFY2(!auxOriginId.isNull() && !workSegId.isNull(), "3.gcad 结构不符");

    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::Intersection;
    pt.serial = QStringLiteral("MIRROR") + doc.newPointSerial();
    pt.isAuxiliary = true;   // 交点辅助点挂在工作层块上
    pt.visible = true;
    pt.showName = false;
    pt.refPointA = auxOriginId;
    pt.hostSegmentId = workSegId;
    pt.interAngle = 315.0;   // 线段相对角度 (目标段 -90° 方向, 315° → 世界 225°, 向左下穿过)
    pt.interBidirectional = true;

    cad::param::Block* host = doc.findBlock(workSegBlock);
    QVERIFY(host);
    cad::param::Segment* seg = host->findSegment(workSegId);
    QVERIFY(seg);
    host->addPoint(pt);
    seg->auxPointIds.push_back(pt.id);
    doc.resolveAll();

    const QUuid interId = pt.id;
    const auto* interPt = host->findPoint(interId);
    {
        // 诊断: 打印解析状态/几何, 便于定位镜像场景失败原因.
        qInfo() << "mirror debug: interPt resolved =" << bool(interPt && interPt->resolved)
                << "host" << workSegBlock.toString() << "seg" << workSegId.toString();
        const auto* op = doc.findBlock(auxBlock) ? doc.findBlock(auxBlock)->findPoint(auxOriginId) : nullptr;
        qInfo() << "mirror debug: origin resolved =" << bool(op && op->resolved);
        if (op) {
            const cad::geo::Vec2 o = doc.findBlock(auxBlock)->worldPos(auxOriginId);
            qInfo() << "mirror debug: origin world (" << o.x << "," << o.y << ")";
        }
        const auto* sp = host->findPoint(seg->startPointId);
        const auto* ep = host->findPoint(seg->endPointId);
        if (sp && ep) {
            const cad::geo::Vec2 w1 = host->transform.toWorld(sp->resolvedPos);
            const cad::geo::Vec2 w2 = host->transform.toWorld(ep->resolvedPos);
            qInfo() << "mirror debug: seg world (" << w1.x << "," << w1.y << ") -> ("
                    << w2.x << "," << w2.y << ")";
        } else {
            qInfo() << "mirror debug: seg endpoints missing or unresolved";
        }
    }
    QVERIFY(interPt && interPt->resolved);
    const auto* originPt = doc.findBlock(auxBlock) ? doc.findBlock(auxBlock)->findPoint(auxOriginId) : nullptr;
    QVERIFY(originPt && originPt->resolved);

    auto worldPos = [&](const QUuid& id) -> cad::geo::Vec2 {
        for (const auto& b : doc.blocks())
            if (const auto* p = b.findPoint(id))
                return b.worldPos(id);
        return {};
    };
    auto verify = [&](const char* tag) -> double {
        bool ok = false;
        const cad::geo::Vec2 exp =
            expectedHit(*host, *seg, *interPt, worldPos(auxOriginId), &ok);
        if (!ok) { qWarning() << tag << "ray misses segment"; return -1.0; }
        const double dev = (worldPos(interId) - exp).length();
        if (dev > 0.5)
            qWarning() << tag << "deviation" << dev
                       << "actual(" << worldPos(interId).x << "," << worldPos(interId).y
                       << ") expected(" << exp.x << "," << exp.y << ")";
        return dev;
    };

    qInfo() << "mirror baseline deviation" << verify("mirror-baseline");
    QVERIFY(verify("mirror-baseline") < 0.5);

    // 变量修改（不同变量扰动辅助层/工作层），逐项验证镜像交点更新。
    for (const auto& v : doc.variables()) {
        cad::param::Variable nv = v;
        nv.value = v.value * 1.05 + 5.0;
        doc.updateVariable(nv);
        const double dev = verify("mirror-var");
        QVERIFY2(dev >= 0.0 && dev < 0.5,
                 qPrintable(QStringLiteral("镜像场景变量 %1 修改后交点偏离 %2 mm")
                                .arg(v.name, QString::number(dev))));
        doc.updateVariable(v);
    }

    // 拖帧扫描（跨层跟随链移动 → 辅助层起点随 Phase 3 沉降），逐帧验证。
    {
        QUuid root;
        const auto& followers = doc.attachments();
        for (const auto& b : doc.blocks()) {
            if (b.layer == doc.auxLayerId()) continue;
            bool hasIncoming = false;
            for (const auto& a : followers)
                if (a.fromBlockId == b.id) { hasIncoming = true; break; }
            if (hasIncoming) continue;
            root = b.id;
            break;
        }
        QVERIFY2(!root.isNull(), "未找到工作层自由根");
        const cad::geo::Vec2 step(0.6, -0.35);
        for (int f = 0; f < 12; ++f) {
            cad::param::Block* rb = doc.findBlock(root);
            QVERIFY(rb);
            rb->transform.origin += step;
            doc.invalidateLayer(rb->layer);
            doc.resolveForDrag({root});
            const double dev = verify("mirror-drag");
            QVERIFY2(dev >= 0.0 && dev < 0.5,
                     qPrintable(QStringLiteral("镜像拖帧 %1 后交点偏离 %2 mm").arg(f).arg(dev)));
        }
    }
}

// ---------------------------------------------------------------------------
// 同层复现（用户 2026-08-24 反馈：交点不更新**不限于跨图层**）。
// 两种最简结构 + 变量驱动的起点:
//   A) 跨块: 交点挂在目标块段上, 射线起点 = 另一工作块的 Polar 点
//      (距离公式引用变量 L) — 变量修改后起点移动, 交点必须滑到新交叉。
//   B) 同块: 起点与目标段在同一块内 (Block::resolve 本地交点分支)。
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
    const QString path = QStringLiteral("e:/3.gcad");
    if (!QFileInfo::exists(path))
        QSKIP("e:/3.gcad 不存在，跳过");

    cad::param::ParamDocument doc;
    QString err;
    QStringList warnings;
    QVERIFY(cad::doc::DocumentFile::load(path, doc, &err, &warnings));

    // 定位交点 + 主机块.
    QUuid interId, originId;
    const cad::param::Block* host = nullptr;
    for (const auto& b : doc.blocks())
        for (const auto& p : b.points)
            if (p.constraint == cad::param::PointConstraint::Intersection
                && !p.refPointA.isNull()) {
                interId = p.id;
                originId = p.refPointA;
                host = &b;
                break;
            }
    QVERIFY2(!interId.isNull(), "未找到交点");

    auto worldPos = [&](const QUuid& id) -> Vec2 {
        for (const auto& b : doc.blocks())
            if (const auto* p = b.findPoint(id))
                return b.worldPos(id);
        return {};
    };

    // 场景 + BlockItem.
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

    // 基线: B=84. 记录交点世界位置 + 渲染.
    const Vec2 beforePos = worldPos(interId);
    const QImage imgBefore = renderRegion(beforePos);
    const double baselineDev = [&]() {
        const auto* seg = host->findSegment(host->findPoint(interId)->hostSegmentId);
        bool ok = false;
        const Vec2 exp = expectedHit(*host, *seg, *host->findPoint(interId),
                                     worldPos(originId), &ok);
        return ok ? (worldPos(interId) - exp).length() : -1.0;
    }();
    QVERIFY2(baselineDev >= 0.0 && baselineDev < 0.5,
             qPrintable(QStringLiteral("基线交点偏离 %1 mm").arg(baselineDev)));

    // 胸围 840 → 900mm (公式链重算: 肩褶E 等).
    for (const auto& v : doc.variables()) {
        if (v.refName == QLatin1String("B")) {
            cad::param::Variable nv = v;
            nv.value = 900.0;
            doc.updateVariable(nv);
            break;
        }
    }

    const Vec2 afterPos = worldPos(interId);
    const QImage imgAfter = renderRegion(afterPos);
    qInfo() << "render check: inter moved"
            << (afterPos - beforePos).length() << "mm;"
            << "bbox render" << imgBefore.size();

    // 引擎侧: 交点移动了且 on-ray.
    QVERIFY2((afterPos - beforePos).length() > 5.0,
             qPrintable(QStringLiteral("引擎: 交点未随胸围移动 (位移 %1 mm)")
                            .arg((afterPos - beforePos).length())));

    // 渲染侧: 交点新位置的渲染中必须出现绿色辅助点缀 (引擎动了, 画布得刷).
    const QColor auxGreen = scene.style()->pointColor(EntityState::Normal, true);
    int green = 0;
    for (int y = 0; y < imgAfter.height(); ++y)
        for (int x = 0; x < imgAfter.width(); ++x) {
            const QRgb rgb = imgAfter.pixel(x, y);
            if (qRed(rgb) == auxGreen.red() && qGreen(rgb) == auxGreen.green()
                && qBlue(rgb) == auxGreen.blue())
                ++green;
        }
    qInfo() << "render check: aux-green pixels near new intersection =" << green;
    QVERIFY2(green >= 1,
             qPrintable(QStringLiteral("画布未在新交点位置绘制辅助点 (绿像素 %1)").arg(green)));
}


// ---------------------------------------------------------------------------
// 现场序列复现：交点经 AddAuxPointCommand (交点工具创建路径) 加入文档后,
// 修改胸围 → 该交点必须跟随 (与 P207 同规)。覆盖 undo 栈创建路径。
void TestIntersectionUpdate::liveSequenceToolCreatedIntersectionFollows()
{
    using cad::geo::Vec2;
    const QString path = QStringLiteral("e:/3.gcad");
    if (!QFileInfo::exists(path))
        QSKIP("e:/3.gcad 不存在，跳过");

    cad::param::ParamDocument doc;
    QString err;
    QStringList warnings;
    QVERIFY(cad::doc::DocumentFile::load(path, doc, &err, &warnings));

    // 复用 P207 的宿主段 + 起点, 另建一个交点 (tool-like: AddAuxPointCommand).
    QUuid interId, originId, hostBlockId, hostSegId;
    for (const auto& b : doc.blocks())
        for (const auto& p : b.points)
            if (p.constraint == cad::param::PointConstraint::Intersection
                && !p.refPointA.isNull()) {
                interId = p.id;
                originId = p.refPointA;
                hostBlockId = b.id;
                hostSegId = p.hostSegmentId;
                break;
            }
    QVERIFY2(!interId.isNull(), "未找到交点");

    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::Intersection;
    pt.serial = doc.newPointSerial();
    pt.isAuxiliary = true;
    pt.visible = true;
    pt.showName = false;
    pt.refPointA = originId;
    pt.hostSegmentId = hostSegId;
    // 射线方向: 与原交点错开 15° (穿过同一线段), 便于独立校验.
    const cad::param::Block* host = doc.findBlock(hostBlockId);
    const auto* orig = host->findPoint(interId);
    QVERIFY(orig);
    pt.interAngle = orig->interAngle + 15.0;
    pt.interBidirectional = orig->interBidirectional;
    const QUuid newId = pt.id;

    QUndoStack stack;
    stack.push(new cad::cmd::AddAuxPointCommand(&doc, hostBlockId, hostSegId, pt));
    QCOMPARE(doc.blocks().size(), size_t(9));   // 未增块, 仅加辅助点

    auto worldPos = [&](const QUuid& id) -> Vec2 {
        for (const auto& b : doc.blocks())
            if (const auto* p = b.findPoint(id))
                return b.worldPos(id);
        return {};
    };
    auto verify = [&](const char* tag, const Vec2& prev, const Vec2& cur) -> double {
        const auto* h = doc.findBlock(hostBlockId);
        const auto* s = h->findSegment(hostSegId);
        const auto* ix = h->findPoint(newId);
        bool ok = false;
        if (!ix || !ix->resolved) { qWarning() << tag << "new intersection unresolved"; return -1000.0; }
        const Vec2 exp = expectedHit(*h, *s, *ix, worldPos(originId), &ok);
        if (!ok) { qWarning() << tag << "ray misses segment"; return -1000.0; }
        const double dev = (worldPos(newId) - exp).length();
        if (dev > 0.01)
            qWarning() << tag << "deviation" << dev
                       << "actual(" << worldPos(newId).x << "," << worldPos(newId).y << ")"
                       << "expected(" << exp.x << "," << exp.y << ")";
        Q_UNUSED(prev) Q_UNUSED(cur)
        return dev;
    };
    // 同时也是 check P207 未破坏.
    auto verifyOrig = [&](const char* tag) -> double {
        const auto* h = doc.findBlock(hostBlockId);
        const auto* s = h->findSegment(hostSegId);
        bool ok = false;
        const Vec2 exp = expectedHit(*h, *s, *h->findPoint(interId), worldPos(originId), &ok);
        if (!ok) { qWarning() << tag << "P207 ray misses segment"; return -1000.0; }
        return (worldPos(interId) - exp).length();
    };

    const Vec2 b0 = worldPos(newId);
    QVERIFY2(verify("tool-create baseline", b0, b0) < 0.5, "新建交点未解析");

    for (const auto& v : doc.variables()) {
        if (v.refName == QLatin1String("B")) {
            cad::param::Variable nv = v;
            nv.value = 900.0;
            doc.updateVariable(nv);
            break;
        }
    }
    const Vec2 b1 = worldPos(newId);
    const double devNew = verify("tool-create after B", b0, b1);
    QVERIFY2(devNew >= 0.0 && devNew < 0.5,
             qPrintable(QStringLiteral("工具创建的交点在胸围修改后偏离 %1 mm").arg(devNew)));
    QVERIFY2((b1 - b0).length() > 3.0,
             qPrintable(QStringLiteral("工具创建的交点未随胸围移动 (位移 %1 mm)").arg((b1 - b0).length())));
    const double devOrig = verifyOrig("P207 after B");
    QVERIFY2(devOrig >= 0.0 && devOrig < 0.5,
             qPrintable(QStringLiteral("P207 在胸围修改后偏离 %1 mm").arg(devOrig)));
    qInfo() << "live-sequence: new inter moved" << (b1 - b0).length() << "mm; P207 dev" << devOrig;
}

// ---------------------------------------------------------------------------
// 绝对世界角度交点：勾选/构建为世界角度后，目标线段旋转不应改变射线方向。
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
