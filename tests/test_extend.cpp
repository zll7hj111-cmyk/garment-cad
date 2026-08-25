#include <QtTest>
#include <QUuid>

#include <cmath>

#include <QGraphicsView>
#include <QPainter>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "document/commands/BreakCommands.h"
#include "document/commands/BlockCommands.h"
#include "document/DocumentSerializer.h"
#include "document/DocumentFile.h"
#include "geometry/Vec2.h"
#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

/// Create a horizontal line block (origin at (0,0), end Polar lenMm/0°).
struct LineSetup {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

LineSetup makeLine(ParamDocument& doc, double lenMm)
{
    Block block;
    block.transform.origin = Vec2::zero();
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = lenMm;
    p2.angle = 0.0;
    QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId, segId};
}

/// Add an auxiliary (Interpolated) point on a segment.
QUuid addAuxPoint(ParamDocument& doc, const QUuid& blockId,
                  const QUuid& segId, double percent,
                  double constantMm = 0.0, bool fromEnd = false)
{
    auto* block = doc.findBlock(blockId);
    auto* seg = block ? block->findSegment(segId) : nullptr;
    if (!block || !seg) return {};

    ParamPoint pt;
    pt.constraint = PointConstraint::Interpolated;
    pt.hostSegmentId = segId;
    pt.isAuxiliary = true;
    pt.interpPercent = percent;
    pt.interpConstant = constantMm;
    pt.interpFromEnd = fromEnd;

    QUuid ptId = pt.id;
    block->addPoint(std::move(pt));
    seg->auxPointIds.push_back(ptId);
    doc.resolveAll();
    return ptId;
}

/// Add a free point to a block (used as intersection ray origin).
QUuid addFreePoint(ParamDocument& doc, const QUuid& blockId, const Vec2& pos)
{
    auto* block = doc.findBlock(blockId);
    if (!block) return {};
    ParamPoint pt;
    pt.constraint = PointConstraint::Free;
    pt.freePos = pos;
    QUuid id = pt.id;
    block->addPoint(std::move(pt));
    return id;
}

void setEndExtend(ParamDocument& doc, const QUuid& blockId,
                  const QUuid& segId, double mm)
{
    auto* b = doc.findBlock(blockId);
    auto* s = b ? b->findSegment(segId) : nullptr;
    QVERIFY(s);
    s->extendEndMm = mm;
    doc.resolveAll();
}

} // namespace

class TestExtend : public QObject
{
    Q_OBJECT

private slots:
    // D7: 百分比辅助点按本体计算 —— 终点延长后辅助点原地不动。
    void auxPercentStaysOnBase()
    {
        ParamDocument doc;
        auto L = makeLine(doc, 100.0);
        const QUuid auxId = addAuxPoint(doc, L.blockId, L.segId, 0.5);
        setEndExtend(doc, L.blockId, L.segId, 50.0);

        const auto* b = doc.findBlock(L.blockId);
        const auto* aux = b->findPoint(auxId);
        QVERIFY(aux && aux->resolved);
        QVERIFY2(aux->resolvedPos.distanceTo(Vec2(50.0, 0.0)) < 1e-6,
                 "percent 0.5 aux on 10cm base must stay at 5cm after end extend");

        // 有效端点 = (150,0); 本体 = (100,0)。
        QVERIFY2(b->worldPos(L.endId).distanceTo(Vec2(150.0, 0.0)) < 1e-6,
                 "effective end must include the extension tail");
        QVERIFY2(b->segmentEffectiveLength(L.segId) > 149.999
                     && b->segmentEffectiveLength(L.segId) < 150.001,
                 "effective length = base + tail");
        QVERIFY2(b->segmentBaseLength(L.segId) > 99.999
                     && b->segmentBaseLength(L.segId) < 100.001,
                 "base length unchanged");
        QVERIFY2(std::abs(b->segmentExtendEnd(L.segId) - 50.0) < 1e-9,
                 "evaluated end extension = 50mm");
    }

    // D7: fromEnd 辅助点按本体计算 —— 终点延长后同样原地不动。
    void fromEndAuxStaysOnBase()
    {
        ParamDocument doc;
        auto L = makeLine(doc, 100.0);
        const QUuid auxId = addAuxPoint(doc, L.blockId, L.segId, 0.5, 0.0, true);
        setEndExtend(doc, L.blockId, L.segId, 50.0);

        const auto* b = doc.findBlock(L.blockId);
        const auto* aux = b->findPoint(auxId);
        QVERIFY(aux && aux->resolved);
        QVERIFY2(aux->resolvedPos.distanceTo(Vec2(50.0, 0.0)) < 1e-6,
                 "fromEnd percent 0.5 aux must stay at 5cm from start");
    }

    // D7: 带公式的 percent 辅助点同样不漂移 (公式保留活性)。
    void auxFormulaPercentStaysOnBase()
    {
        ParamDocument doc;
        auto L = makeLine(doc, 100.0);
        auto* b = doc.findBlock(L.blockId);
        auto* seg = b->findSegment(L.segId);
        ParamPoint pt;
        pt.constraint = PointConstraint::Interpolated;
        pt.hostSegmentId = L.segId;
        pt.isAuxiliary = true;
        pt.interpPercentFormula = QStringLiteral("0.5");
        QUuid auxId = pt.id;
        b->addPoint(std::move(pt));
        seg->auxPointIds.push_back(auxId);
        doc.resolveAll();

        setEndExtend(doc, L.blockId, L.segId, 50.0);
        const auto* aux = b->findPoint(auxId);
        QVERIFY(aux && aux->resolved);
        QVERIFY2(aux->resolvedPos.distanceTo(Vec2(50.0, 0.0)) < 1e-6,
                 "formula percent aux must stay at 5cm");
        QVERIFY2(!aux->interpPercentFormula.isEmpty(),
                 "percent formula must be preserved (原参数化不清除)");
    }

    // D7b: 交叉点跟实际线走 —— 射线与延长尾巴相交时交点在 (120,0)。
    void intersectionFollowsEffectiveLine()
    {
        ParamDocument doc;
        auto L = makeLine(doc, 100.0);
        setEndExtend(doc, L.blockId, L.segId, 50.0);

        auto* b = doc.findBlock(L.blockId);
        QUuid originId = addFreePoint(doc, L.blockId, Vec2(120.0, 50.0));

        ParamPoint isecPt;
        isecPt.constraint = PointConstraint::Intersection;
        isecPt.refPointA = originId;
        isecPt.hostSegmentId = L.segId;
        isecPt.interAngle = 270.0;   // 相对宿主段方向 0°，射线垂直向下
        isecPt.interBidirectional = false;
        QUuid ipId = isecPt.id;
        b->addPoint(std::move(isecPt));
        doc.resolveAll();

        const auto* ip = b->findPoint(ipId);
        QVERIFY2(ip && ip->resolved,
                 "intersection on the extended tail must resolve (D7b)");
        QVERIFY2(ip->resolvedPos.distanceTo(Vec2(120.0, 0.0)) < 1e-6,
                 "intersection must land on the actual (extended) line");
    }

    // D4: 跨线共点 —— 附着在延长端点上的跟随线自动跟到有效端点。
    void followerFollowsExtendedLeader()
    {
        ParamDocument doc;
        auto A = makeLine(doc, 100.0);
        setEndExtend(doc, A.blockId, A.segId, 50.0);

        // Follower B: 起点 Free, 终点 Polar 50mm。
        Block b;
        b.transform.origin = Vec2::zero();
        b.transform.rotation = 0.0;
        ParamPoint p1;
        p1.constraint = PointConstraint::Free;
        p1.freePos = Vec2::zero();
        QUuid bs = p1.id;
        ParamPoint p2;
        p2.constraint = PointConstraint::Polar;
        p2.refPointId = bs;
        p2.distance = 50.0;
        p2.angle = 0.0;
        QUuid be = p2.id;
        b.addPoint(std::move(p1));
        b.addPoint(std::move(p2));
        Segment seg;
        seg.startPointId = bs;
        seg.endPointId = be;
        b.addSegment(std::move(seg));
        const QUuid B = b.id;
        doc.addBlock(std::move(b));

        Attachment att;
        att.fromBlockId = B;
        att.fromPointId = bs;
        att.toBlockId = A.blockId;
        att.toPointId = A.endId;
        att.toSegmentId = A.segId;
        att.followerAngle = 180.0;  // 直行延伸 (闭合基准 0°=折叠)
        QVERIFY2(doc.addAttachment(att), "follower attachment must be accepted");
        doc.resolveAll();

        const auto* bBlk = doc.findBlock(B);
        QVERIFY(bBlk);
        QVERIFY2(bBlk->worldPos(bs).distanceTo(Vec2(150.0, 0.0)) < 1e-6,
                 "follower must follow the extended (effective) endpoint (D4)");
    }

    // D2: 负延长量防御性钳制为 0 (只往外)。
    void negativeExtendClamped()
    {
        ParamDocument doc;
        auto L = makeLine(doc, 100.0);
        auto* b = doc.findBlock(L.blockId);
        auto* s = b->findSegment(L.segId);
        s->extendEndMm = -20.0;
        doc.resolveAll();
        QVERIFY2(std::abs(b->segmentExtendEnd(L.segId)) < 1e-9,
                 "negative extension must clamp to 0 (D2)");
        QVERIFY2(b->worldPos(L.endId).distanceTo(Vec2(100.0, 0.0)) < 1e-6,
                 "effective end stays at base when clamped");
    }

    // D8: 打断后延长量随端点归属 (前段断点=0, 后段继承原终点延长量)。
    void breakKeepsEndExtensionOnBack()
    {
        ParamDocument doc;
        auto L = makeLine(doc, 100.0);
        setEndExtend(doc, L.blockId, L.segId, 50.0);
        const QUuid auxId = addAuxPoint(doc, L.blockId, L.segId, 0.5);

        cad::cmd::BreakSegmentCommand cmd(&doc, L.blockId, L.segId, auxId);
        QVERIFY2(cmd.isValid(), "break at mid aux with extended end must be valid");
        cmd.redo();

        QCOMPARE(doc.blocks().size(), 2);
        const Block* back = nullptr;
        const Block* front = nullptr;
        for (const auto& blk : doc.blocks()) {
            if (blk.id == L.blockId) front = &blk;
            else back = &blk;
        }
        QVERIFY(front && back);
        const auto& fseg = front->segments.front();
        const auto& bseg = back->segments.front();
        QVERIFY2(std::abs(fseg.extendEndMm) < 1e-9,
                 "front break-point end extension must be 0 (D8)");
        QVERIFY2(std::abs(bseg.extendEndMm - 50.0) < 1e-9,
                 "back segment must inherit the original end extension (D8)");
        QVERIFY2(std::abs(bseg.extendStartMm) < 1e-9,
                 "back break-point start extension must be 0 (D8)");
        QVERIFY2(back->segmentExtendEnd(bseg.id) > 49.999
                     && back->segmentExtendEnd(bseg.id) < 50.001,
                 "evaluated back extension = 50mm");

        cmd.undo();
        QCOMPARE(doc.blocks().size(), 1);
        const auto* orig = doc.findBlock(L.blockId);
        QVERIFY2(std::abs(orig->segments.front().extendEndMm - 50.0) < 1e-9,
                 "undo must restore the original extension");
    }

    // 编辑命令: undo/redo 完整还原 (数值+公式)。
    void setExtendCommandUndoRedo()
    {
        ParamDocument doc;
        auto L = makeLine(doc, 100.0);
        doc.setParameter(QStringLiteral("ext"), 5.0);

        cad::cmd::SetSegmentExtendCommand::Values v;
        v.endMm = 0.0;
        v.endFormula = QStringLiteral("ext");   // 5cm → 50mm
        cad::cmd::SetSegmentExtendCommand cmd(&doc, L.blockId, L.segId, v);
        cmd.redo();
        auto* b = doc.findBlock(L.blockId);
        QVERIFY2(std::abs(b->segmentExtendEnd(L.segId) - 50.0) < 1e-9,
                 "formula-driven extension (cm domain) evaluates to 50mm");
        QVERIFY2(b->worldPos(L.endId).distanceTo(Vec2(150.0, 0.0)) < 1e-6,
                 "redo moves the effective endpoint");

        cmd.undo();
        QVERIFY2(b->segmentExtendEnd(L.segId) < 1e-9,
                 "undo restores zero extension");
        QVERIFY2(b->worldPos(L.endId).distanceTo(Vec2(100.0, 0.0)) < 1e-6,
                 "undo restores base endpoint");
    }

    // 参数驱动: 变量变化后延长量自动重算 (引用扫描联动)。
    void parameterDrivenExtend()
    {
        ParamDocument doc;
        auto L = makeLine(doc, 100.0);
        auto* b = doc.findBlock(L.blockId);
        auto* s = b->findSegment(L.segId);
        doc.setParameter(QStringLiteral("ext"), 3.0);
        s->extendEndFormula = QStringLiteral("ext");
        doc.resolveAll();
        QVERIFY2(std::abs(b->segmentExtendEnd(L.segId) - 30.0) < 1e-9,
                 "ext=3cm → 30mm tail");

        doc.setParameter(QStringLiteral("ext"), 7.0);
        doc.resolveAll();
        QVERIFY2(std::abs(b->segmentExtendEnd(L.segId) - 70.0) < 1e-9,
                 "parameter change must re-evaluate the extension formula");
    }

    // 序列化 round-trip: 延长字段完整保留; 旧档缺省 0。
    void serializeRoundTrip()
    {
        ParamDocument src;
        auto L = makeLine(src, 100.0);
        auto* b = src.findBlock(L.blockId);
        auto* s = b->findSegment(L.segId);
        s->extendEndMm = 0.0;
        s->extendEndFormula = QStringLiteral("ext");
        s->extendStartMm = 20.0;
        // 变量走 Variable (refName 进公式域; Variable.value 存储 mm,
        // 参数表值 = cm) —— 40mm = 4cm → 延长 40mm。
        Variable v;
        v.name = QStringLiteral("延长");
        v.refName = QStringLiteral("ext");
        v.value = 40.0;
        src.addVariable(v);
        src.resolveAll();

        QJsonObject json = DocumentSerializer::serialize(src);
        ParamDocument dst;
        DocumentSerializer::deserialize(dst, json);
        dst.resolveAll();   // 求值缓存 (延长量求值行式) 需一次解析
        const auto* db = dst.findBlock(L.blockId);
        QVERIFY(db);
        const auto* ds = db->findSegment(L.segId);
        QVERIFY(ds);
        QVERIFY2(std::abs(ds->extendStartMm - 20.0) < 1e-9,
                 "extendStartMm survives round trip");
        QVERIFY2(ds->extendEndFormula == QStringLiteral("ext"),
                 "extendEndFormula survives round trip");
        QVERIFY2(db->segmentExtendEnd(L.segId) > 39.999
                     && db->segmentExtendEnd(L.segId) < 40.001,
                 "evaluated extension survives round trip");

        // 旧档: 无字段 = 0。
        QJsonObject legacy = json;
        {
            QJsonObject docObj = legacy["document"].toObject();
            QJsonArray blocks = docObj["blocks"].toArray();
            QJsonObject first = blocks.first().toObject();
            QJsonArray segs = first["segments"].toArray();
            QJsonObject segObj = segs.first().toObject();
            segObj.remove("extendStartMm");
            segObj.remove("extendStartFormula");
            segObj.remove("extendEndMm");
            segObj.remove("extendEndFormula");
            segs.replace(0, segObj);
            first["segments"] = segs;
            blocks.replace(0, first);
            docObj["blocks"] = blocks;
            legacy["document"] = docObj;
        }
        ParamDocument legacyDoc;
        DocumentSerializer::deserialize(legacyDoc, legacy);
        const auto* lb = legacyDoc.findBlock(L.blockId);
        QVERIFY(lb);
        const auto* ls = lb->findSegment(L.segId);
        QVERIFY(ls);
        QVERIFY2(ls->extendEndMm == 0.0 && ls->extendEndFormula.isEmpty()
                     && ls->extendStartMm == 0.0 && ls->extendStartFormula.isEmpty(),
                 "legacy doc without extension fields must load as 0");
    }

    // 尾巴范围判定: segmentSnapWithinBase 锁本体 (建点/打断前置)。
    void snapTWithinBaseGuard()
    {
        ParamDocument doc;
        auto L = makeLine(doc, 100.0);
        setEndExtend(doc, L.blockId, L.segId, 50.0);
        const auto* b = doc.findBlock(L.blockId);
        // 有效段 [0,150]; t 沿有效段。距本体起点 = t*150 - 起点延长量(0)。
        QVERIFY2(b->segmentSnapWithinBase(L.segId, 50.0 / 150.0),
                 "50mm from start (base region) must be within base");
        QVERIFY2(b->segmentSnapWithinBase(L.segId, 100.0 / 150.0),
                 "100mm (base end) must be within base");
        QVERIFY2(!b->segmentSnapWithinBase(L.segId, 120.0 / 150.0),
                 "120mm (tail) must be rejected");
        QVERIFY2(!b->segmentSnapWithinBase(L.segId, 0.999),
                 "near effective end (tail) must be rejected");
    }

    // ── 画布刷新回归 (用户报告 2026-11: "正向延长线不刷新显示") ──
    // 走真实 GUI 路径: undoStack->push(SetSegmentExtendCommand) → redo
    // ++geometryEpoch → resolveAll → resolved → scene sync → rebuildCache。
    // 渲染像素级验证: 终点延长后线段右端必须右移, 起点延长后左端必须左移。
    void canvasRefreshOnEndExtend()
    {
        ParamDocument doc;
        CanvasScene scene(&doc);
        scene.setSceneRect(-50.0, -50.0, 300.0, 200.0);
        QGraphicsView view(&scene);
        view.resize(600, 400);

        auto L = makeLine(doc, 100.0);      // 水平线 (0,0)→(100,0)
        // 偏离原点轴 (避开 OriginCrosshair 的行 100): 原点 y=+40 → scene y=−40
        // → 图像行 (−40+50)*2 = 20。
        doc.findBlock(L.blockId)->transform.origin = Vec2(0.0, 40.0);
        scene.addBlockItem(L.blockId);
        doc.resolveAll();
        QVERIFY2(scene.findBlockItem(L.blockId) != nullptr,
                 "block item must exist in the scene");

        auto render = [&]() {
            QImage img(600, 400, QImage::Format_ARGB32);
            img.fill(Qt::white);
            QPainter p(&img);
            scene.render(&p, QRectF(0.0, 0.0, 600.0, 400.0), scene.sceneRect());
            p.end();
            return img;
        };
        // 扫描某行最右侧非白像素 (scene y 范围 [−50,150] → 2px/mm;
        // 线段场景行 = −40 → 图像行 20, 避开十字轴行 100)。
        constexpr int kLineRow = 20;
        auto rightmostLit = [](const QImage& img) {
            for (int x = img.width() - 1; x >= 0; --x)
                if (img.pixelColor(x, kLineRow) != QColor(Qt::white))
                    return x;
            return -1;
        };
        auto leftmostLit = [](const QImage& img) {
            for (int x = 0; x < img.width(); ++x)
                if (img.pixelColor(x, kLineRow) != QColor(Qt::white))
                    return x;
            return -1;
        };

        const QImage before = render();
        const auto baseRight = rightmostLit(before);
        const auto baseLeft  = leftmostLit(before);
        QVERIFY2(baseRight > 0, "baseline line must be visible");

        // 终点延长 5cm (正向) —— 画布同步后右端必须右移。
        const quint64 epoch0 = doc.findBlock(L.blockId)->geometryEpoch;
        cad::cmd::SetSegmentExtendCommand::Values vEnd;
        vEnd.endMm = 50.0;
        doc.undoStack()->push(new cad::cmd::SetSegmentExtendCommand(
            &doc, L.blockId, L.segId, vEnd));
        const auto* b = doc.findBlock(L.blockId);
        QVERIFY2(b->geometryEpoch != epoch0,
                 "end extension must bump geometryEpoch (canvas redraw rule)");
        QVERIFY2(b->worldPos(L.endId).distanceTo(Vec2(150.0, 40.0)) < 1e-6,
                 "end effective position must include the tail");
        const QImage afterEnd = render();
        const auto endRight = rightmostLit(afterEnd);
        QVERIFY2(endRight > baseRight + 40,
                 QStringLiteral("end extension must visually extend the line "
                                "(right lit pixel %1 → %2)")
                     .arg(baseRight).arg(endRight).toUtf8().constData());

        // 起点延长 3cm (负向) —— 左端必须左移。
        cad::cmd::SetSegmentExtendCommand::Values vStart;
        vStart.endMm = 50.0;        // 保留终点延长
        vStart.startMm = 30.0;
        doc.undoStack()->push(new cad::cmd::SetSegmentExtendCommand(
            &doc, L.blockId, L.segId, vStart));
        QVERIFY2(b->worldPos(L.startId).distanceTo(Vec2(-30.0, 40.0)) < 1e-6,
                 "start effective position must include the backward tail");
        const QImage afterStart = render();
        const auto startLeft = leftmostLit(afterStart);
        QVERIFY2(startLeft < baseLeft - 20,
                 QStringLiteral("start extension must visually extend the line "
                                "(left lit pixel %1 → %2)")
                     .arg(baseLeft).arg(startLeft).toUtf8().constData());
    }

    // ── 截图复现 (用户 2026-11): 原始段延长 + 端点挂跟随线 ──
    // 用户观察: 跟随线 (模型读 effective 位置) 跟着尾巴动了, 但原始线段的
    // 画布缓存没刷新 (画的还是本体长)。此测试验证延长端点上挂附着时,
    // 被延长段的画布像素必须同步外移。
    void endExtendWithFollowerRedrawsLeader()
    {
        ParamDocument doc;
        CanvasScene scene(&doc);
        scene.setSceneRect(-60.0, -60.0, 320.0, 240.0);
        QGraphicsView view(&scene);
        view.resize(640, 480);

        // 原始段: (0,0)→(100,0) 水平, 原点 (0,40) → 场景行 20 (避开十字轴)。
        auto L = makeLine(doc, 100.0);
        doc.findBlock(L.blockId)->transform.origin = Vec2(0.0, 40.0);
        scene.addBlockItem(L.blockId);

        // 跟随线: 起点挂 L 终点, 相对角 90° (向上), 50mm。
        Block f;
        f.transform.origin = Vec2::zero();
        f.transform.rotation = 0.0;
        ParamPoint fp1;
        fp1.constraint = PointConstraint::Free;
        fp1.freePos = Vec2::zero();
        QUuid fs = fp1.id;
        ParamPoint fp2;
        fp2.constraint = PointConstraint::Polar;
        fp2.refPointId = fs;
        fp2.distance = 50.0;
        fp2.angle = 0.0;
        QUuid fe = fp2.id;
        f.addPoint(std::move(fp1));
        f.addPoint(std::move(fp2));
        Segment fseg;
        fseg.startPointId = fs;
        fseg.endPointId = fe;
        f.addSegment(std::move(fseg));
        QUuid F = f.id;
        doc.addBlock(std::move(f));
        scene.addBlockItem(F);

        Attachment att;
        att.fromBlockId = F;
        att.fromPointId = fs;
        att.toBlockId = L.blockId;
        att.toPointId = L.endId;
        att.toSegmentId = L.segId;
        att.followerAngle = 90.0;   // 挂延长端点
        QVERIFY2(doc.addAttachment(att),
                 "attachment at the (to-be-extended) end must be accepted");
        doc.resolveAll();

        auto render = [&]() {
            QImage img(640, 480, QImage::Format_ARGB32);
            img.fill(Qt::white);
            QPainter p(&img);
            scene.render(&p, QRectF(0.0, 0.0, 640.0, 480.0),
                         QRectF(-60.0, -60.0, 320.0, 240.0));
            p.end();
            return img;
        };
        // 原始段所在行 (scene y=-40): 行 (−40+60)*2 = 40。
        constexpr int kRow = 40;
        auto rightmostLit = [](const QImage& img) {
            for (int x = img.width() - 1; x >= 0; --x)
                if (img.pixelColor(x, kRow) != QColor(Qt::white))
                    return x;
            return -1;
        };

        const QImage before = render();
        const auto baseRight = rightmostLit(before);
        QVERIFY2(baseRight > 0, "baseline leader must be visible");

        // 终点延长 1.5cm (15mm) —— 走真实卡片路径 (undo stack push)。
        cad::cmd::SetSegmentExtendCommand::Values v;
        v.endMm = 15.0;
        doc.undoStack()->push(new cad::cmd::SetSegmentExtendCommand(
            &doc, L.blockId, L.segId, v));

        const auto* b = doc.findBlock(L.blockId);
        QVERIFY2(b->worldPos(L.endId).distanceTo(Vec2(115.0, 40.0)) < 1e-6,
                 "leader effective end must include the tail");
        const auto* fb = doc.findBlock(F);
        QVERIFY2(fb->worldPos(fs).distanceTo(Vec2(115.0, 40.0)) < 1e-6,
                 "follower must ride the extended effective end (D4)");

        const QImage after = render();
        const auto endRight = rightmostLit(after);
        QVERIFY2(endRight > baseRight + 15,
                 QStringLiteral(
                     "leader line cache must refresh with the tail "
                     "(right lit pixel %1 → %2; 15mm = 30px)")
                     .arg(baseRight).arg(endRight).toUtf8().constData());
    }

    // ── 截图复现 v2 (用户 2026-11): 起点延长量 = 变量公式「后长补正」+
    //    起点端挂跟随线 ── 用户观察: 模型已延长 (跟随线动了) 但原始段画布
    //    像素未刷新。此用例复现并锁定根因。
    void startFormulaExtendWithFollowerRedrawsLeader()
    {
        ParamDocument doc;
        CanvasScene scene(&doc);
        scene.setSceneRect(-60.0, -60.0, 320.0, 240.0);
        QGraphicsView view(&scene);
        view.resize(640, 480);

        // 原始段: (0,0)→(100,0) 水平, 原点 (0,40) → 场景行 20 (避开十字轴)。
        auto L = makeLine(doc, 100.0);
        doc.findBlock(L.blockId)->transform.origin = Vec2(0.0, 40.0);
        scene.addBlockItem(L.blockId);

        // 变量「后长补正」= 5 (cm 域)。
        doc.setParameter(QStringLiteral("后长补正"), 5.0);

        // 跟随线: 起点挂 L 起点 (延长端!), 相对角 90°, 50mm。
        Block f;
        f.transform.origin = Vec2::zero();
        f.transform.rotation = 0.0;
        ParamPoint fp1;
        fp1.constraint = PointConstraint::Free;
        fp1.freePos = Vec2::zero();
        QUuid fs = fp1.id;
        ParamPoint fp2;
        fp2.constraint = PointConstraint::Polar;
        fp2.refPointId = fs;
        fp2.distance = 50.0;
        fp2.angle = 0.0;
        QUuid fe = fp2.id;
        f.addPoint(std::move(fp1));
        f.addPoint(std::move(fp2));
        Segment fseg;
        fseg.startPointId = fs;
        fseg.endPointId = fe;
        f.addSegment(std::move(fseg));
        QUuid F = f.id;
        doc.addBlock(std::move(f));
        scene.addBlockItem(F);

        Attachment att;
        att.fromBlockId = F;
        att.fromPointId = fs;
        att.toBlockId = L.blockId;
        att.toPointId = L.startId;   // ← 挂起点 (延长端)
        att.toSegmentId = L.segId;
        att.followerAngle = 90.0;
        QVERIFY2(doc.addAttachment(att),
                 "attachment at the (to-be-extended) start must be accepted");
        doc.resolveAll();

        auto render = [&]() {
            QImage img(640, 480, QImage::Format_ARGB32);
            img.fill(Qt::white);
            QPainter p(&img);
            scene.render(&p, QRectF(0.0, 0.0, 640.0, 480.0),
                         QRectF(-60.0, -60.0, 320.0, 240.0));
            p.end();
            return img;
        };
        constexpr int kRow = 40;   // scene y = -40 (原点 y=40, y 翻转)。
        auto leftmostLit = [](const QImage& img) {
            for (int x = 0; x < img.width(); ++x)
                if (img.pixelColor(x, kRow) != QColor(Qt::white))
                    return x;
            return -1;
        };

        const QImage before = render();
        const auto baseLeft = leftmostLit(before);
        QVERIFY2(baseLeft > 0, "baseline leader must be visible");

        // 起点延长量 = 公式「后长补正」(5cm → 50mm) —— 真实卡片路径。
        cad::cmd::SetSegmentExtendCommand::Values v;
        v.startFormula = QStringLiteral("后长补正");
        doc.undoStack()->push(new cad::cmd::SetSegmentExtendCommand(
            &doc, L.blockId, L.segId, v));

        const auto* b = doc.findBlock(L.blockId);
        QVERIFY2(std::abs(b->segmentExtendStart(L.segId) - 50.0) < 1e-6,
                 "formula 后长补正 must evaluate to 50mm start tail");
        QVERIFY2(b->worldPos(L.startId).distanceTo(Vec2(-50.0, 40.0)) < 1e-6,
                 "leader effective start must include the backward tail");
        const auto* fb = doc.findBlock(F);
        QVERIFY2(fb->worldPos(fs).distanceTo(Vec2(-50.0, 40.0)) < 1e-6,
                 "follower must ride the extended effective start (D4)");

        const QImage after = render();
        const auto afterLeft = leftmostLit(after);
        QVERIFY2(afterLeft < baseLeft - 60,
                 QStringLiteral(
                     "leader line cache must refresh with the start tail "
                     "(left lit pixel %1 → %2; 50mm = 100px)")
                     .arg(baseLeft).arg(afterLeft).toUtf8().constData());
    }

    // ── 真实文档复现 (用户 2026-11 报告场景): 装入 E:\3.gcad, 对修正线 /
    //    后肩线施加终点延长, 验证有效端点确实外移。缺文档时跳过。
    void realDocExtensionMovesEffectiveEnd()
    {
        const QString path = QStringLiteral("E:/3.gcad");
        if (!QFile::exists(path)) QSKIP("E:\\3.gcad not present");

        ParamDocument doc;
        QString err;
        QVERIFY2(cad::doc::DocumentFile::load(path, doc, &err),
                 qPrintable(QStringLiteral("load failed: %1").arg(err)));
        doc.resolveAll();

        // 修正线片断 (aux 层, 带交点/插值辅助点) 与 后肩线片断。
        const QStringList wantSerial = {
            QStringLiteral("f55zfL78"),   // 修正线 segment
            QStringLiteral("k043rL82"),   // 后肩线 segment
        };
        for (const QString& serial : wantSerial) {
            const Block* blk = nullptr;
            const Segment* seg = nullptr;
            for (const auto& b : doc.blocks()) {
                for (const auto& s : b.segments) {
                    if (s.serial == serial) { blk = &b; seg = &s; break; }
                }
                if (seg) break;
            }
            if (!blk || !seg) {
                qWarning() << "segment serial" << serial << "not in doc — skipped";
                continue;
            }
            const auto* sp = blk->findPoint(seg->startPointId);
            const auto* ep = blk->findPoint(seg->endPointId);
            QVERIFY(sp && ep);
            const Vec2 baseStartWorld = blk->worldPos(sp->id);
            const Vec2 baseEndWorld   = blk->worldPos(ep->id);
            const double baseLen = baseStartWorld.distanceTo(baseEndWorld);
            QVERIFY2(baseLen > 1.0, qPrintable(
                QStringLiteral("segment %1 must have a non-degenerate base")
                    .arg(serial)));

            // 终点延长 2cm (emulating the card: endMm = cmToMm(2)).
            cad::cmd::SetSegmentExtendCommand::Values v;
            v.endMm = 20.0;
            doc.undoStack()->push(new cad::cmd::SetSegmentExtendCommand(
                &doc, blk->id, seg->id, v));
            const auto* b2 = doc.findBlock(blk->id);
            const Vec2 endWorld = b2->worldPos(ep->id);
            const Vec2 dir = (baseEndWorld - baseStartWorld) / baseLen;
            const double moved = (endWorld - baseEndWorld).length();
            QVERIFY2(std::abs(moved - 20.0) < 1e-6,
                     qPrintable(QStringLiteral(
                         "segment %1 end must move 20mm (moved %2)")
                         .arg(serial).arg(moved)));
            const double dot = (endWorld - baseEndWorld).x * dir.x
                             + (endWorld - baseEndWorld).y * dir.y;
            QVERIFY2(dot > 19.9,
                     qPrintable(QStringLiteral(
                         "segment %1 tail must extend OUTWARD (dot %2)")
                         .arg(serial).arg(dot)));

            // 渲染级验证: 真实文档的画布在终点延长后必须出现新像素 (尾巴)。
            // 渲染区域 = 该线段包围盒 + 40mm 余量; 其他块不受本次延长影响,
            // 因此出现变化像素 = 尾巴被画出来了。
            // 辅助层块在非激活时整块隐身 (不绘制) → 只对工作层块做渲染级检查。
            const bool isWorking = !doc.layers().empty()
                && blk->layer != doc.layers().front().id;
            if (!isWorking) {
                qWarning() << "segment" << serial
                           << "on aux layer — skipping render check";
                continue;
            }
            {
                const Vec2 lo(std::min(baseStartWorld.x, baseEndWorld.x) - 40.0,
                              std::min(baseStartWorld.y, baseEndWorld.y) - 40.0);
                const Vec2 hi(std::max(baseStartWorld.x, baseEndWorld.x) + 40.0,
                              std::max(baseStartWorld.y, baseEndWorld.y) + 40.0);
                CanvasScene scene(&doc);
                // 块在场景构造前已加载 → 逐块补齐 BlockItem。
                for (const auto& b : doc.blocks())
                    scene.addBlockItem(b.id);
                // 完整渲染一遍 (同步块项 + 层模式), 然后只取局部区域比较。
                constexpr double kScale = 3.0;
                const QRectF region(lo.x, -hi.y, hi.x - lo.x, hi.y - lo.y);
                auto renderRegion = [&]() {
                    const QRectF sceneRect(
                        region.left(), region.top(), region.width(), region.height());
                    QImage img(static_cast<int>(sceneRect.width() * kScale),
                               static_cast<int>(sceneRect.height() * kScale),
                               QImage::Format_ARGB32);
                    img.fill(Qt::white);
                    QPainter p(&img);
                    scene.render(&p, img.rect(), sceneRect);
                    p.end();
                    return img;
                };
                scene.syncBlockPositions();          // 同步最近一次 resolve 的结果
                const QImage q0 = renderRegion();
                auto* item = scene.findBlockItem(blk->id);
                QVERIFY(item);
                const QRectF b0 = item->boundingRect();
                const quint64 epochA = doc.findBlock(blk->id)->geometryEpoch;
                cad::cmd::SetSegmentExtendCommand::Values v2;
                v2.endMm = 35.0;   // 20mm (上一步) → 35mm: 真正的变更
                doc.undoStack()->push(new cad::cmd::SetSegmentExtendCommand(
                    &doc, blk->id, seg->id, v2));
                const quint64 epochB = doc.findBlock(blk->id)->geometryEpoch;
                const QRectF b1 = item->boundingRect();
                qWarning().noquote() << QStringLiteral(
                    "[extend-render] %1 epoch %2→%3 bounds %4→%5 "
                    "(width %6→%7) visible=%8")
                    .arg(serial)
                    .arg(epochA).arg(epochB)
                    .arg(QLocale::c().toString(b0.width(), 'f', 1))
                    .arg(QLocale::c().toString(b1.width(), 'f', 1))
                    .arg(QLocale::c().toString(b0.width(), 'f', 1))
                    .arg(QLocale::c().toString(b1.width(), 'f', 1))
                    .arg(item->isVisible());
                const QImage q1 = renderRegion();
                int diffPx = 0;
                for (int yy = 0; yy < q0.height(); ++yy) {
                    for (int xx = 0; xx < q0.width(); ++xx) {
                        if (q0.pixel(xx, yy) != q1.pixel(xx, yy)) ++diffPx;
                    }
                }
                QVERIFY2(diffPx > 30,
                         qPrintable(QStringLiteral(
                             "segment %1 render must gain tail pixels "
                             "(changed %2 px)")
                             .arg(serial).arg(diffPx)));
            }
        }
    }

    // ── 用户已保存文档 (E:\3.gcad, 3.gcad 存档 5wfkxL74): 起点延长量 =
    //    公式「后长补正」(5cm) 的真实场景 —— 加载后验证求值 + 有效端点 +
    //    渲染尾巴像素 (修复「延长了但图像没显示」)。缺文档时跳过。
    void savedDocFormulaStartExtendRenders()
    {
        const QString path = QStringLiteral("E:/3.gcad");
        if (!QFile::exists(path)) QSKIP("E:\\3.gcad not present");

        ParamDocument doc;
        QString err;
        QVERIFY2(cad::doc::DocumentFile::load(path, doc, &err),
                 qPrintable(QStringLiteral("load failed: %1").arg(err)));
        doc.resolveAll();

        const Block* blk = nullptr;
        const Segment* seg = nullptr;
        for (const auto& b : doc.blocks()) {
            for (const auto& s : b.segments) {
                if (s.serial == QStringLiteral("5wfkxL74")) { blk = &b; seg = &s; break; }
            }
            if (seg) break;
        }
        if (!blk || !seg) QSKIP("5wfkxL74 not present (user doc changed)");

        auto* sp = blk->findPoint(seg->startPointId);
        auto* ep = blk->findPoint(seg->endPointId);
        QVERIFY(sp && ep);

        // ① 公式求值: 后长补正 = 当前文档变量值 (mm)。测试不得硬编码 — 用户
        //    会调整该变量 (3.gcad 是活档)。
        double expectMm = -1.0;
        for (const auto& v : doc.variables())
            if (v.name == QString::fromUtf8("后长补正")) expectMm = v.value;
        QVERIFY2(expectMm > 0.0, "缺少变量 后长补正");
        QVERIFY2(std::abs(blk->segmentExtendStart(seg->id) - expectMm) < 1e-6,
                 qPrintable(QStringLiteral(
                     "后长补正 must evaluate to %1mm (got %2)")
                     .arg(expectMm).arg(blk->segmentExtendStart(seg->id))));
        // ② 有效起点 = 本体起点 − expectMm × 段方向 (起点向外)。
        const Vec2 baseStart = blk->transform.toWorld(
            sp->resolved ? sp->resolvedPos : Vec2::zero());
        const Vec2 effStart = blk->worldPos(sp->id);
        const Vec2 segDir = (blk->worldPos(ep->id) - baseStart).normalized();
        const double moved = (effStart - baseStart).length();
        QVERIFY2(std::abs(moved - expectMm) < 1e-6,
                 qPrintable(QStringLiteral(
                     "start must move %1mm (moved %2)").arg(expectMm).arg(moved)));
        const double dot = (effStart - baseStart).x * segDir.x
                         + (effStart - baseStart).y * segDir.y;
        QVERIFY2(dot < -(expectMm - 1.0),
                 qPrintable(QStringLiteral(
                     "start tail must extend OUTWARD (dot %1, expect ~-%2)")
                     .arg(dot).arg(expectMm)));

        // ③ 渲染级: 起点上方必须出现尾巴像素。
        CanvasScene scene(&doc);
        for (const auto& b : doc.blocks())
            scene.addBlockItem(b.id);
        scene.syncBlockPositions();
        constexpr double kScale = 3.0;
        const Vec2 lo(baseStart.x - 40.0, std::min(baseStart.y, effStart.y) - 15.0);
        const Vec2 hi(baseStart.x + 40.0, std::max(baseStart.y, effStart.y) + 15.0);
        const QRectF region(lo.x, -hi.y, hi.x - lo.x, hi.y - lo.y);
        auto renderRegion = [&]() {
            QImage img(static_cast<int>(region.width() * kScale),
                       static_cast<int>(region.height() * kScale),
                       QImage::Format_ARGB32);
            img.fill(Qt::white);
            QPainter p(&img);
            scene.render(&p, img.rect(), region);
            p.end();
            return img;
        };
        // 起点上方 expectMm/2 高度带 (scaled): 尾巴必须画在这里。
        const QImage img = renderRegion();
        // 图像行 = (region 顶世界高 − 采样世界高) × scale。
        const int kTailRow = static_cast<int>(
            (hi.y - (baseStart.y + expectMm * 0.5)) * kScale);
        // 诊断: 区域亮像素总量 + 亮像素包围盒 (列/行范围)。
        int litTotal = 0, minX = 99999, maxX = -1, minY = 99999, maxY = -1;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                if (img.pixelColor(x, y) != QColor(Qt::white)) {
                    ++litTotal;
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                }
            }
        }
        qWarning().noquote() << QStringLiteral(
            "[tail-diag] region %1x%2 lit %3 px bbox x[%4..%5] y[%6..%7] "
            "tailRow %8 (baseStartWorld y %9, effStartWorld y %10)")
            .arg(img.width()).arg(img.height()).arg(litTotal)
            .arg(minX).arg(maxX).arg(minY).arg(maxY)
            .arg(kTailRow).arg(baseStart.y).arg(effStart.y);
        int lit = 0;
        for (int x = 0; x < img.width(); ++x)
            if (img.pixelColor(x, kTailRow) != QColor(Qt::white)) ++lit;
        QVERIFY2(lit >= 1,
                 qPrintable(QStringLiteral(
                     "start tail (后长补正 %1mm) must render above the base "
                     "start (lit %1 px @row %2)")
                     .arg(expectMm).arg(lit).arg(kTailRow)));

        // ── 编辑流复现: 加载后 (尾巴已渲染) 再改值 (3cm), 画布必须刷新 ──
        // 用户报告场景: 卡片里改延长量 → 跟随线动了但原始段没刷新。
        {
            const QImage q0 = renderRegion();
            const quint64 epochA = doc.findBlock(blk->id)->geometryEpoch;
            cad::cmd::SetSegmentExtendCommand::Values v2;
            v2.startMm = 30.0;   // 公式 → 30mm (真实变更)
            doc.undoStack()->push(new cad::cmd::SetSegmentExtendCommand(
                &doc, blk->id, seg->id, v2));
            const quint64 epochB = doc.findBlock(blk->id)->geometryEpoch;
            const QImage q1 = renderRegion();
            int diffPx = 0;
            for (int y = 0; y < q0.height(); ++y)
                for (int x = 0; x < q0.width(); ++x)
                    if (q0.pixel(x, y) != q1.pixel(x, y)) ++diffPx;
            QVERIFY2(diffPx > 30,
                     qPrintable(QStringLiteral(
                         "edit flow must refresh the leader render "
                         "(epoch %1→%2, changed %3 px)")
                         .arg(epochA).arg(epochB).arg(diffPx)));
        }
    }
};

QTEST_MAIN(TestExtend)
#include "test_extend.moc"
