#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QFileInfo>

#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/FormulaGroup.h"
#include "parametric/Condition.h"
#include "document/DocumentSerializer.h"
#include "document/DocumentFile.h"
#include "tools/SnapEngine.h"
#include "geometry/Vec2.h"

using namespace cad::param;

class TestSerializer : public QObject
{
    Q_OBJECT

private slots:
    void emptyDocument();
    void variablesRoundTrip();
    void formulasRoundTrip();
    void formulaGroupsRoundTrip();
    void formulaGroupsLegacyDocument();
    void layersRoundTrip();
    void layersLegacyDocument();
    void removeLayerMovesBlocks();
    void blocksRoundTrip();
    void attachmentsRoundTrip();
    void bridgeRoundTrip();
    void serialCountersRoundTrip();
    void fullDocumentRoundTrip();
    void auxiliaryPointSnappable();
    void bridgeAuxPointSnappableAndAttachable();
    void auxPointFromEndDirection();
    void auxFromEndRoundTrip();
    void diagL54ConnectionOffset();
    void degradedValuesProduceWarnings();

private:
    /// Build a document with representative data for testing.
    static void populateDocument(ParamDocument& doc);
};

// ---------------------------------------------------------------------------
// Deserialization degradation warnings (反序列化降级警告): values that cannot
// be interpreted (unknown constraint / segment type / layer type) must be
// reported through the warnings list instead of silently defaulting — the
// load still SUCCEEDS, but the user is told what was downgraded.
// ---------------------------------------------------------------------------
void TestSerializer::degradedValuesProduceWarnings()
{
    // A clean round trip produces NO warnings.
    {
        ParamDocument doc;
        populateDocument(doc);
        ParamDocument dst;
        QStringList warnings;
        DocumentSerializer::deserialize(dst, DocumentSerializer::serialize(doc), &warnings);
        QVERIFY(warnings.isEmpty());
    }

    // Corrupt three semantic fields and re-load: every degradation is reported.
    ParamDocument doc;
    populateDocument(doc);
    QJsonObject root = DocumentSerializer::serialize(doc);
    QJsonObject docObj = root["document"].toObject();

    // First block's first point gets an unknown constraint.
    QJsonArray blocks = docObj["blocks"].toArray();
    QVERIFY(!blocks.isEmpty());
    QJsonObject blk = blocks[0].toObject();
    QJsonArray pts = blk["points"].toArray();
    QVERIFY(!pts.isEmpty());
    QJsonObject pt = pts[0].toObject();
    pt["constraint"] = QStringLiteral("HyperCurve");
    pts[0] = pt;
    blk["points"] = pts;
    // ...and its first segment gets an unknown type.
    QJsonArray segs = blk["segments"].toArray();
    if (!segs.isEmpty()) {
        QJsonObject seg = segs[0].toObject();
        seg["type"] = QStringLiteral("Circle");
        segs[0] = seg;
        blk["segments"] = segs;
    }
    blocks[0] = blk;
    docObj["blocks"] = blocks;

    // First layer gets an unknown type.
    QJsonArray layers = docObj["layers"].toArray();
    QVERIFY(!layers.isEmpty());
    QJsonObject lay = layers[0].toObject();
    lay["type"] = QStringLiteral("magic");
    layers[0] = lay;
    docObj["layers"] = layers;
    root["document"] = docObj;

    ParamDocument dst;
    QStringList warnings;
    DocumentSerializer::deserialize(dst, root, &warnings);

    // Three degradations, each with a human-readable notice. Order follows
    // deserialize(): layers first, then blocks (points before segments).
    QCOMPARE(warnings.size(), 3);
    QVERIFY(warnings[0].contains(QStringLiteral("magic")));
    QVERIFY(warnings[1].contains(QStringLiteral("约束类型")));
    QVERIFY(warnings[1].contains(QStringLiteral("HyperCurve")));
    QVERIFY(warnings[2].contains(QStringLiteral("Circle")));

    // The degraded defaults are the documented safe ones. deserialize() seals
    // the invariant by INSERTING an aux layer at index 0 when the file lacks
    // one — the downgraded "magic" layer lands at index 1 as a working layer.
    const Block* b = dst.findBlock(QUuid(blk["id"].toString()));
    QVERIFY(b);
    QCOMPARE(b->points.front().constraint, PointConstraint::Free);
    QCOMPARE(b->segments.front().type, SegmentType::Line);
    QCOMPARE(dst.layers().front().type, LayerType::Auxiliary);  // auto-sealed
    QCOMPARE(dst.layers().at(1).type, LayerType::Working);      // degraded
}

void TestSerializer::populateDocument(ParamDocument& doc)
{
    // Variables
    Variable v1;
    v1.name = QStringLiteral("胸围");
    v1.refName = QStringLiteral("b");
    v1.value = 88.0;
    doc.addVariable(v1);

    Variable v2;
    v2.name = QStringLiteral("腰围");
    v2.refName = QStringLiteral("w");
    v2.value = 66.0;
    doc.addVariable(v2);

    // Formula
    FormulaVariable f1;
    f1.name = QStringLiteral("前胸宽");
    f1.expression = QStringLiteral("b/4+0.6");
    Condition cond;
    cond.watchVar = QStringLiteral("b");
    cond.lowerOn = true;
    cond.lower = 80.0;
    cond.upperOn = false;
    cond.mode = AdjustMode::Flat;
    cond.amount = 1.0;
    f1.conditions.append(cond);
    f1.conditionsEnabled = true;
    doc.addFormula(f1);

    // Block with 2 points + 1 segment
    Block block;
    block.transform.origin = cad::geo::Vec2(10.0, 20.0);
    block.transform.rotation = 0.5;

    ParamPoint pt1;
    pt1.constraint = PointConstraint::Free;
    pt1.freePos = cad::geo::Vec2(0.0, 0.0);
    pt1.name = QStringLiteral("A");
    QUuid pt1Id = pt1.id;

    ParamPoint pt2;
    pt2.constraint = PointConstraint::Polar;
    pt2.refPointId = pt1Id;
    pt2.distance = 50.0;
    pt2.angle = 45.0;
    pt2.name = QStringLiteral("B");
    QUuid pt2Id = pt2.id;

    block.addPoint(std::move(pt1));
    block.addPoint(std::move(pt2));

    Segment seg;
    seg.startPointId = pt1Id;
    seg.endPointId = pt2Id;
    seg.name = QStringLiteral("测试线段");
    seg.role = SegmentRole::Outline;
    seg.lineStyle = LineStyle::Dashed;
    seg.color = QColor(0xFF, 0x00, 0x80);
    seg.weight = 2.5;
    seg.visible = true;
    seg.showName = true;
    seg.showLength = true;
    seg.lengthFormula = QStringLiteral("b/2");
    block.addSegment(std::move(seg));

    doc.addBlock(std::move(block));

    // Second block (for attachment)
    Block block2;
    block2.transform.origin = cad::geo::Vec2(60.0, 70.0);
    block2.transform.rotation = 0.0;

    ParamPoint pt3;
    pt3.constraint = PointConstraint::Free;
    pt3.freePos = cad::geo::Vec2(0.0, 0.0);
    QUuid pt3Id = pt3.id;

    ParamPoint pt4;
    pt4.constraint = PointConstraint::Polar;
    pt4.refPointId = pt3Id;
    pt4.distance = 30.0;
    pt4.angle = 90.0;
    QUuid pt4Id = pt4.id;

    block2.addPoint(std::move(pt3));
    block2.addPoint(std::move(pt4));

    Segment seg2;
    seg2.startPointId = pt3Id;
    seg2.endPointId = pt4Id;
    block2.addSegment(std::move(seg2));

    QUuid block2Id = block2.id;
    doc.addBlock(std::move(block2));

    // Attachment: block2 follows block1
    Attachment att;
    att.fromBlockId = block2Id;
    att.fromPointId = pt3Id;
    att.toBlockId = doc.blocks().front().id;
    att.toPointId = pt2Id;
    att.toSegmentId = doc.blocks().front().segments.front().id;
    att.followerAngle = 30.0;
    doc.addAttachment(std::move(att));
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void TestSerializer::emptyDocument()
{
    ParamDocument src;
    QJsonObject json = DocumentSerializer::serialize(src);

    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    QVERIFY(dst.blocks().empty());
    QVERIFY(dst.attachments().empty());
    QVERIFY(dst.variables().empty());
    QVERIFY(dst.formulas().empty());
    QVERIFY(dst.freePoints().empty());
}

void TestSerializer::variablesRoundTrip()
{
    ParamDocument src;
    Variable v;
    v.name = QStringLiteral("臀围");
    v.refName = QStringLiteral("h");
    v.value = 92.5;
    src.addVariable(v);

    QJsonObject json = DocumentSerializer::serialize(src);
    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    QCOMPARE((int)dst.variables().size(), 1);
    const auto& rv = dst.variables().front();
    QCOMPARE(rv.id, v.id);
    QCOMPARE(rv.name, v.name);
    QCOMPARE(rv.refName, v.refName);
    QCOMPARE(rv.value, v.value);
}

void TestSerializer::formulasRoundTrip()
{
    ParamDocument src;
    FormulaVariable f;
    f.name = QStringLiteral("肩宽");
    f.expression = QStringLiteral("b/4-0.5");
    Condition c;
    c.watchVar = QStringLiteral("b");
    c.lowerOn = true;
    c.lower = 80.0;
    c.upperOn = true;
    c.upper = 100.0;
    c.mode = AdjustMode::PerStep;
    c.step = 2.0;
    c.amount = 0.5;
    f.conditions.append(c);
    f.conditionsEnabled = true;
    src.addFormula(f);

    QJsonObject json = DocumentSerializer::serialize(src);
    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    QCOMPARE((int)dst.formulas().size(), 1);
    const auto& rf = dst.formulas().front();
    QCOMPARE(rf.id, f.id);
    QCOMPARE(rf.name, f.name);
    QCOMPARE(rf.expression, f.expression);
    QCOMPARE(rf.conditionsEnabled, true);
    QCOMPARE((int)rf.conditions.size(), 1);
    QCOMPARE(rf.conditions.first().watchVar, c.watchVar);
    QCOMPARE(rf.conditions.first().lower, c.lower);
    QCOMPARE(rf.conditions.first().upper, c.upper);
    QCOMPARE(rf.conditions.first().mode, c.mode);
    QCOMPARE(rf.conditions.first().step, c.step);
    QCOMPARE(rf.conditions.first().amount, c.amount);
}

void TestSerializer::formulaGroupsRoundTrip()
{
    // Formula groups (id/name/collapsed) and each formula's groupId must
    // survive a serialize/deserialize round trip.
    ParamDocument src;

    FormulaGroup g1;
    g1.name = QStringLiteral("围度");
    g1.collapsed = true;
    src.addFormulaGroup(g1);

    FormulaGroup g2;
    g2.name = QStringLiteral("长度");
    g2.collapsed = false;
    src.addFormulaGroup(g2);

    FormulaVariable f1;
    f1.name = QStringLiteral("胸宽");
    f1.expression = QStringLiteral("b/4+0.6");
    f1.groupId = g1.id;
    src.addFormula(f1);

    FormulaVariable f2;
    f2.name = QStringLiteral("背长");
    f2.expression = QStringLiteral("h/2");
    // f2 stays ungrouped (null groupId).
    src.addFormula(f2);

    QJsonObject json = DocumentSerializer::serialize(src);
    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    QCOMPARE((int)dst.formulaGroups().size(), 2);
    const auto& rg1 = dst.formulaGroups()[0];
    QCOMPARE(rg1.id, g1.id);
    QCOMPARE(rg1.name, g1.name);
    QCOMPARE(rg1.collapsed, true);
    const auto& rg2 = dst.formulaGroups()[1];
    QCOMPARE(rg2.id, g2.id);
    QCOMPARE(rg2.name, g2.name);
    QCOMPARE(rg2.collapsed, false);

    QCOMPARE((int)dst.formulas().size(), 2);
    QCOMPARE(dst.findFormula(f1.id)->groupId, g1.id);
    QVERIFY(dst.findFormula(f2.id)->groupId.isNull());
}

void TestSerializer::formulaGroupsLegacyDocument()
{
    // A legacy file without "formulaGroups" / "groupId" fields must load
    // cleanly: no groups, every formula ungrouped. A dangling groupId (group
    // missing from the file) must be dropped on load.
    ParamDocument src;
    FormulaVariable f;
    f.name = QStringLiteral("肩宽");
    f.expression = QStringLiteral("b/4-0.5");
    src.addFormula(f);

    QJsonObject json = DocumentSerializer::serialize(src);

    // Simulate a legacy file: strip the group array and inject a dangling
    // groupId into the formula entry.
    QJsonObject varObj = json["variables"].toObject();
    varObj.remove(QStringLiteral("formulaGroups"));
    QJsonArray formulasArr = varObj["formulas"].toArray();
    QJsonObject fObj = formulasArr[0].toObject();
    fObj["groupId"] = QUuid::createUuid().toString();
    formulasArr[0] = fObj;
    varObj["formulas"] = formulasArr;
    json["variables"] = varObj;

    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    QVERIFY(dst.formulaGroups().empty());
    QCOMPARE((int)dst.formulas().size(), 1);
    QVERIFY(dst.formulas().front().groupId.isNull());
}

namespace {
/// Build a minimal single-segment block for layer tests.
cad::param::Block makeLineBlock(int layer)
{
    cad::param::Block b;
    cad::param::ParamPoint pa;
    pa.constraint = cad::param::PointConstraint::Free;
    pa.freePos = cad::geo::Vec2(0.0, 0.0);
    QUuid paId = pa.id;
    cad::param::ParamPoint pb;
    pb.constraint = cad::param::PointConstraint::Free;
    pb.freePos = cad::geo::Vec2(10.0, 0.0);
    QUuid pbId = pb.id;
    b.addPoint(std::move(pa));
    b.addPoint(std::move(pb));
    cad::param::Segment s;
    s.startPointId = paId;
    s.endPointId = pbId;
    b.addSegment(std::move(s));
    b.layer = layer;
    return b;
}
} // namespace

void TestSerializer::layersRoundTrip()
{
    // Layer registry (name/visible/type), each block's layer index, and the
    // active layer must survive a serialize/deserialize round trip.
    // A fresh document starts with [辅助层(aux), 图层 1].
    ParamDocument src;
    src.addLayer(QStringLiteral("辅助"));   // index 2
    src.addLayer(QStringLiteral("工作"));   // index 3

    cad::param::Block b1 = makeLineBlock(1);
    QUuid b1Id = b1.id;
    src.addBlock(std::move(b1));

    src.setActiveLayer(2);

    QJsonObject json = DocumentSerializer::serialize(src);
    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    QCOMPARE((int)dst.layers().size(), 4);
    QCOMPARE(dst.layers()[0].name, QStringLiteral("辅助层"));
    QCOMPARE(dst.layers()[0].type, cad::param::LayerType::Auxiliary);
    QCOMPARE(dst.layers()[1].name, QStringLiteral("图层 1"));
    QCOMPARE(dst.layers()[1].type, cad::param::LayerType::Working);
    QCOMPARE(dst.layers()[2].name, QStringLiteral("辅助"));
    QCOMPARE(dst.layers()[3].name, QStringLiteral("工作"));
    QCOMPARE(dst.layers()[2].visible, true);
    QCOMPARE(dst.activeLayer(), 2);
    QCOMPARE(dst.findBlock(b1Id)->layer, 1);
}

void TestSerializer::layersLegacyDocument()
{
    // A legacy file without "layers"/"activeLayer" fields must load cleanly:
    // the default aux+working pair is kept and legacy blocks (old layer 0)
    // shift up onto the first working layer.
    ParamDocument src;
    src.addBlock(makeLineBlock(0));

    QJsonObject json = DocumentSerializer::serialize(src);
    QJsonObject docObj = json["document"].toObject();
    docObj.remove(QStringLiteral("layers"));
    docObj.remove(QStringLiteral("activeLayer"));
    json["document"] = docObj;

    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    QCOMPARE((int)dst.layers().size(), 2);
    QCOMPARE(dst.layers()[0].name, QStringLiteral("辅助层"));
    QCOMPARE(dst.layers()[0].type, cad::param::LayerType::Auxiliary);
    QCOMPARE(dst.layers()[1].name, QStringLiteral("图层 1"));
    QCOMPARE(dst.activeLayer(), 1);
    QCOMPARE(dst.blocks().front().layer, 1);  // shifted above the aux layer
}

void TestSerializer::removeLayerMovesBlocks()
{
    // Removing a layer drops its blocks to the layer below (never into the
    // aux layer) and shifts every higher layer's blocks down by one.
    ParamDocument doc;   // [辅助层(0), 图层 1(1)]
    doc.addLayer(QStringLiteral("L1"));  // index 2
    doc.addLayer(QStringLiteral("L2"));  // index 3

    cad::param::Block a = makeLineBlock(2);
    QUuid aId = a.id;
    doc.addBlock(std::move(a));
    cad::param::Block b = makeLineBlock(3);
    QUuid bId = b.id;
    doc.addBlock(std::move(b));

    // The auxiliary layer cannot be removed (no-op).
    doc.removeLayer(0);
    QCOMPARE((int)doc.layers().size(), 4);
    QCOMPARE(doc.layers()[0].type, cad::param::LayerType::Auxiliary);

    doc.removeLayer(2);  // remove "L1"

    QCOMPARE((int)doc.layers().size(), 3);
    QCOMPARE(doc.findBlock(aId)->layer, 1);  // fell to layer below (not aux)
    QCOMPARE(doc.findBlock(bId)->layer, 2);  // shifted down
}

void TestSerializer::blocksRoundTrip()
{
    ParamDocument src;

    Block block;
    block.transform.origin = cad::geo::Vec2(5.5, -3.2);
    block.transform.rotation = 1.23;

    ParamPoint pt1;
    pt1.constraint = PointConstraint::Free;
    pt1.freePos = cad::geo::Vec2(1.0, 2.0);
    pt1.name = QStringLiteral("P1");
    QUuid pt1Id = pt1.id;

    ParamPoint pt2;
    pt2.constraint = PointConstraint::Polar;
    pt2.refPointId = pt1Id;
    pt2.distance = 42.0;
    pt2.angle = 60.0;
    pt2.distanceFormula = QStringLiteral("b/2+1");
    QUuid pt2Id = pt2.id;

    block.addPoint(std::move(pt1));
    block.addPoint(std::move(pt2));

    Segment seg;
    seg.startPointId = pt1Id;
    seg.endPointId = pt2Id;
    seg.name = QStringLiteral("轮廓");
    seg.role = SegmentRole::Internal;
    seg.lineStyle = LineStyle::Dotted;
    seg.color = QColor(0x12, 0x34, 0x56);
    seg.weight = 1.8;
    seg.visible = false;
    seg.showName = true;
    seg.showLength = false;
    seg.lengthFormula = QStringLiteral("w/3");
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    src.addBlock(std::move(block));

    QJsonObject json = DocumentSerializer::serialize(src);
    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    QCOMPARE((int)dst.blocks().size(), 1);
    const auto& rb = dst.blocks().front();
    QCOMPARE(rb.id, blockId);
    QVERIFY(qFuzzyCompare(rb.transform.origin.x, 5.5));
    QVERIFY(qFuzzyCompare(rb.transform.origin.y, -3.2));
    QVERIFY(qFuzzyCompare(rb.transform.rotation, 1.23));
    QCOMPARE((int)rb.points.size(), 2);
    QCOMPARE((int)rb.segments.size(), 1);

    const auto& rs = rb.segments.front();
    QCOMPARE(rs.name, QStringLiteral("轮廓"));
    QCOMPARE(rs.role, SegmentRole::Internal);
    QCOMPARE(rs.lineStyle, LineStyle::Dotted);
    QCOMPARE(rs.color, QColor(0x12, 0x34, 0x56));
    QVERIFY(qFuzzyCompare(rs.weight, 1.8));
    QCOMPARE(rs.visible, false);
    QCOMPARE(rs.showName, true);
    QCOMPARE(rs.showLength, false);
    QCOMPARE(rs.lengthFormula, QStringLiteral("w/3"));
}

void TestSerializer::attachmentsRoundTrip()
{
    ParamDocument src;
    populateDocument(src);

    QJsonObject json = DocumentSerializer::serialize(src);
    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    QCOMPARE((int)dst.attachments().size(), (int)src.attachments().size());
    const auto& sa = src.attachments().front();
    const auto& da = dst.attachments().front();
    QCOMPARE(da.id, sa.id);
    QCOMPARE(da.fromBlockId, sa.fromBlockId);
    QCOMPARE(da.fromPointId, sa.fromPointId);
    QCOMPARE(da.toBlockId, sa.toBlockId);
    QCOMPARE(da.toPointId, sa.toPointId);
    QCOMPARE(da.toSegmentId, sa.toSegmentId);
    QVERIFY(!da.toSegmentId.isNull());
    QVERIFY(qFuzzyCompare(da.followerAngle, sa.followerAngle));
}

void TestSerializer::bridgeRoundTrip()
{
    // A bridge block (isBridge) with two pin attachments (isPin) must survive
    // a serialize/deserialize round trip with both flags intact.
    ParamDocument src;

    // Two independent host blocks.
    Block hostA;
    ParamPoint ha;
    ha.constraint = PointConstraint::Free;
    ha.freePos = cad::geo::Vec2(0.0, 0.0);
    QUuid haId = ha.id;
    hostA.addPoint(std::move(ha));
    QUuid hostAId = hostA.id;
    src.addBlock(std::move(hostA));

    Block hostB;
    ParamPoint hb;
    hb.constraint = PointConstraint::Free;
    hb.freePos = cad::geo::Vec2(100.0, 0.0);
    QUuid hbId = hb.id;
    hostB.addPoint(std::move(hb));
    QUuid hostBId = hostB.id;
    src.addBlock(std::move(hostB));

    // Bridge block.
    Block bridge;
    bridge.isBridge = true;
    ParamPoint bs;
    bs.constraint = PointConstraint::Free;
    bs.freePos = cad::geo::Vec2(0.0, 0.0);
    QUuid bsId = bs.id;
    ParamPoint be;
    be.constraint = PointConstraint::Polar;
    be.refPointId = bsId;
    be.distance = 100.0;
    QUuid beId = be.id;
    bridge.addPoint(std::move(bs));
    bridge.addPoint(std::move(be));
    Segment bseg;
    bseg.startPointId = bsId;
    bseg.endPointId = beId;
    bridge.addSegment(std::move(bseg));
    QUuid bridgeId = bridge.id;
    src.addBlock(std::move(bridge));

    Attachment pinStart;
    pinStart.isPin = true;
    pinStart.fromBlockId = bridgeId;
    pinStart.fromPointId = bsId;
    pinStart.toBlockId = hostAId;
    pinStart.toPointId = haId;
    QVERIFY(src.addAttachment(pinStart));

    Attachment pinEnd;
    pinEnd.isPin = true;
    pinEnd.fromBlockId = bridgeId;
    pinEnd.fromPointId = beId;
    pinEnd.toBlockId = hostBId;
    pinEnd.toPointId = hbId;
    QVERIFY(src.addAttachment(pinEnd));

    QJsonObject json = DocumentSerializer::serialize(src);
    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    // The bridge block keeps its isBridge flag.
    const Block* rb = dst.findBlock(bridgeId);
    QVERIFY(rb != nullptr);
    QVERIFY(rb->isBridge);
    // Both pins keep isPin; non-bridge blocks stay non-bridge.
    QCOMPARE((int)dst.attachments().size(), 2);
    int pinCount = 0;
    for (const auto& a : dst.attachments())
        if (a.isPin) ++pinCount;
    QCOMPARE(pinCount, 2);
    QVERIFY(!dst.findBlock(hostAId)->isBridge);
    QVERIFY(!dst.findBlock(hostBId)->isBridge);
}

void TestSerializer::serialCountersRoundTrip()
{
    ParamDocument src;
    // Advance serial counters
    (void)src.newPointSerial();
    (void)src.newPointSerial();
    (void)src.newPointSerial();
    (void)src.newLineSerial();
    (void)src.newLineSerial();
    (void)src.newGroupSerial();

    QJsonObject json = DocumentSerializer::serialize(src);
    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    QCOMPARE(dst.pointSeq(), src.pointSeq());
    QCOMPARE(dst.lineSeq(), src.lineSeq());
    QCOMPARE(dst.groupSeq(), src.groupSeq());
}

void TestSerializer::fullDocumentRoundTrip()
{
    ParamDocument src;
    populateDocument(src);

    QJsonObject json = DocumentSerializer::serialize(src);

    // Serialize again to verify determinism
    QJsonObject json2 = DocumentSerializer::serialize(src);
    QCOMPARE(json, json2);

    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    // Structural equality
    QCOMPARE((int)dst.blocks().size(), (int)src.blocks().size());
    QCOMPARE((int)dst.attachments().size(), (int)src.attachments().size());
    QCOMPARE((int)dst.variables().size(), (int)src.variables().size());
    QCOMPARE((int)dst.formulas().size(), (int)src.formulas().size());
    QCOMPARE((int)dst.freePoints().size(), (int)src.freePoints().size());
    QCOMPARE(dst.pointSeq(), src.pointSeq());
    QCOMPARE(dst.lineSeq(), src.lineSeq());
    QCOMPARE(dst.groupSeq(), src.groupSeq());

    // Re-serialize the restored document and compare JSON
    QJsonObject json3 = DocumentSerializer::serialize(dst);
    QCOMPARE(json3, json);
}

void TestSerializer::auxiliaryPointSnappable()
{
    // Auxiliary (Interpolated) points must be valid snap targets for the smart
    // pen: they are visible, selectable and resolved, so SnapEngine must return
    // them. Regression guard for "auxiliary points cannot be picked by the pen".
    ParamDocument doc;

    Block block;  // origin (0,0), rotation 0 → world == local
    block.layer = doc.activeLayer();  // working layer (layer 0 is the sealed aux layer)
    ParamPoint pA;
    pA.constraint = PointConstraint::Free;
    pA.freePos = cad::geo::Vec2(0.0, 0.0);
    QUuid idA = pA.id;
    ParamPoint pB;
    pB.constraint = PointConstraint::Free;
    pB.freePos = cad::geo::Vec2(100.0, 0.0);
    QUuid idB = pB.id;
    block.addPoint(std::move(pA));
    block.addPoint(std::move(pB));

    Segment seg;
    seg.startPointId = idA;
    seg.endPointId = idB;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    // Auxiliary point at 50% of the host segment → world (50, 0).
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = segId;
    aux.isAuxiliary = true;
    aux.interpPercent = 0.5;
    QUuid auxId = aux.id;
    block.addPoint(std::move(aux));

    doc.addBlock(std::move(block));
    doc.resolveAll();

    // Sanity: the auxiliary point actually resolved.
    const Block* rb = doc.findBlock(doc.blocks().front().id);
    QVERIFY(rb != nullptr);
    const ParamPoint* raux = rb->findPoint(auxId);
    QVERIFY(raux != nullptr);
    QVERIFY(raux->resolved);

    // Snap exactly on the auxiliary point.
    cad::tools::SnapEngine se;
    auto snap = se.findSnap(cad::geo::Vec2(50.0, 0.0), &doc, 1.0);
    QVERIFY(snap.has_value());
    QCOMPARE(snap->pointId, auxId);

    // Snap slightly off (within the 12px radius at zoom 1) still hits the aux
    // point rather than the far endpoints.
    auto snap2 = se.findSnap(cad::geo::Vec2(55.0, 3.0), &doc, 1.0);
    QVERIFY(snap2.has_value());
    QCOMPARE(snap2->pointId, auxId);
}

void TestSerializer::bridgeAuxPointSnappableAndAttachable()
{
    // Regression for "auxiliary point on a BRIDGE cannot be picked by the smart
    // pen". A bridge's pinned endpoints are pure leaves (not snappable, cannot
    // anchor followers), but an AUXILIARY point on the bridge must be snappable
    // AND able to lead a follower, which the Resolver places after the bridge.
    ParamDocument doc;

    // Host A with a point at world (0,0).
    Block hostA;
    hostA.layer = doc.activeLayer();  // working layer
    ParamPoint ha;
    ha.constraint = PointConstraint::Free;
    ha.freePos = cad::geo::Vec2(0.0, 0.0);
    QUuid haId = ha.id;
    hostA.addPoint(std::move(ha));
    QUuid hostAId = hostA.id;
    doc.addBlock(std::move(hostA));

    // Host B with a point at world (100,0).
    Block hostB;
    hostB.layer = doc.activeLayer();  // working layer
    ParamPoint hb;
    hb.constraint = PointConstraint::Free;
    hb.freePos = cad::geo::Vec2(100.0, 0.0);
    QUuid hbId = hb.id;
    hostB.addPoint(std::move(hb));
    QUuid hostBId = hostB.id;
    doc.addBlock(std::move(hostB));

    // Bridge: local construction is 50mm long, but the pins stretch it to 100.
    Block bridge;
    bridge.layer = doc.activeLayer();  // working layer
    bridge.isBridge = true;
    ParamPoint bs;
    bs.constraint = PointConstraint::Free;
    bs.freePos = cad::geo::Vec2(0.0, 0.0);
    QUuid bsId = bs.id;
    ParamPoint be;
    be.constraint = PointConstraint::Polar;
    be.refPointId = bsId;
    be.distance = 50.0;
    be.angle = 0.0;
    QUuid beId = be.id;
    bridge.addPoint(std::move(bs));
    bridge.addPoint(std::move(be));
    Segment bseg;
    bseg.startPointId = bsId;
    bseg.endPointId = beId;
    QUuid bsegId = bseg.id;
    bridge.addSegment(std::move(bseg));
    // Auxiliary point at 50% of the bridge segment.
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = bsegId;
    aux.isAuxiliary = true;
    aux.interpPercent = 0.5;
    QUuid auxId = aux.id;
    bridge.addPoint(std::move(aux));
    QUuid bridgeId = bridge.id;
    doc.addBlock(std::move(bridge));

    // Pin both bridge endpoints to the hosts.
    Attachment pinStart;
    pinStart.isPin = true;
    pinStart.fromBlockId = bridgeId;
    pinStart.fromPointId = bsId;
    pinStart.toBlockId = hostAId;
    pinStart.toPointId = haId;
    QVERIFY(doc.addAttachment(pinStart));

    Attachment pinEnd;
    pinEnd.isPin = true;
    pinEnd.fromBlockId = bridgeId;
    pinEnd.fromPointId = beId;
    pinEnd.toBlockId = hostBId;
    pinEnd.toPointId = hbId;
    QVERIFY(doc.addAttachment(pinEnd));

    doc.resolveAll();

    // The bridge stretched to [0,100]; the aux point must track the STRETCHED
    // geometry (50% of 100 = 50), not the pre-stretch local construction (25).
    const Block* rb = doc.findBlock(bridgeId);
    QVERIFY(rb != nullptr);
    const ParamPoint* raux = rb->findPoint(auxId);
    QVERIFY(raux != nullptr && raux->resolved);
    const cad::geo::Vec2 auxWorld = rb->transform.toWorld(raux->resolvedPos);
    QVERIFY(std::abs(auxWorld.x - 50.0) < 1e-6);
    QVERIFY(std::abs(auxWorld.y) < 1e-6);

    // The aux point is snappable...
    cad::tools::SnapEngine se;
    auto snap = se.findSnap(auxWorld, &doc, 1.0);
    QVERIFY(snap.has_value());
    QCOMPARE(snap->pointId, auxId);

    // ...but the pinned endpoints are NOT: snapping at (0,0) hits host A's
    // point, never the bridge endpoint sitting on top of it.
    auto snapEnd = se.findSnap(cad::geo::Vec2(0.0, 0.0), &doc, 1.0);
    QVERIFY(snapEnd.has_value());
    QVERIFY(snapEnd->pointId != bsId);
    QCOMPARE(snapEnd->pointId, haId);

    // A follower may attach to the bridge's aux point...
    Block follower;
    follower.layer = doc.activeLayer();  // working layer (same group as bridge)
    ParamPoint fs;
    fs.constraint = PointConstraint::Free;
    fs.freePos = cad::geo::Vec2(0.0, 0.0);
    QUuid fsId = fs.id;
    ParamPoint fe;
    fe.constraint = PointConstraint::Polar;
    fe.refPointId = fsId;
    fe.distance = 20.0;
    fe.angle = 0.0;
    QUuid feId = fe.id;
    follower.addPoint(std::move(fs));
    follower.addPoint(std::move(fe));
    Segment fseg;
    fseg.startPointId = fsId;
    fseg.endPointId = feId;
    follower.addSegment(std::move(fseg));
    QUuid followerId = follower.id;
    doc.addBlock(std::move(follower));

    Attachment att;
    att.fromBlockId = followerId;
    att.fromPointId = fsId;
    att.toBlockId = bridgeId;
    att.toPointId = auxId;
    att.toSegmentId = bsegId;
    att.followerAngle = 0.0;
    QVERIFY2(doc.addAttachment(att), "attach to bridge aux point must succeed");

    // ...but NOT to a bridge's pinned endpoint (fresh leader block so the
    // rejection is unambiguously due to the endpoint, not the follower rule).
    Block probe;
    probe.layer = doc.activeLayer();  // working layer
    ParamPoint pp;
    pp.constraint = PointConstraint::Free;
    pp.freePos = cad::geo::Vec2(0.0, 0.0);
    QUuid ppId = pp.id;
    probe.addPoint(std::move(pp));
    QUuid probeId = probe.id;
    doc.addBlock(std::move(probe));

    Attachment bad;
    bad.fromBlockId = probeId;
    bad.fromPointId = ppId;
    bad.toBlockId = bridgeId;
    bad.toPointId = beId;   // pinned endpoint, not auxiliary
    QVERIFY2(!doc.addAttachment(bad), "attach to bridge endpoint must be rejected");

    doc.resolveAll();

    // The follower lands on the aux point's world position, oriented along the
    // (stretched) bridge direction.
    const Block* rf = doc.findBlock(followerId);
    QVERIFY(rf != nullptr);
    const cad::geo::Vec2 fsWorld = rf->worldPos(fsId);
    const cad::geo::Vec2 feWorld = rf->worldPos(feId);
    QVERIFY(std::abs(fsWorld.x - 50.0) < 1e-6);
    QVERIFY(std::abs(fsWorld.y) < 1e-6);
    QVERIFY(std::abs(feWorld.x - 70.0) < 1e-6);
    QVERIFY(std::abs(feWorld.y) < 1e-6);
}

void TestSerializer::auxPointFromEndDirection()
{
    // interpFromEnd flips the reference direction: percent/constant are measured
    // from the END point toward the start, and the offset-angle 0° baseline is
    // flipped accordingly.
    ParamDocument doc;

    Block block;  // origin (0,0), rotation 0 → world == local
    ParamPoint pA;
    pA.constraint = PointConstraint::Free;
    pA.freePos = cad::geo::Vec2(0.0, 0.0);
    QUuid idA = pA.id;
    ParamPoint pB;
    pB.constraint = PointConstraint::Free;
    pB.freePos = cad::geo::Vec2(100.0, 0.0);
    QUuid idB = pB.id;
    block.addPoint(std::move(pA));
    block.addPoint(std::move(pB));

    Segment seg;
    seg.startPointId = idA;
    seg.endPointId = idB;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    // Default direction (from start): 30% → world (30, 0).
    ParamPoint auxStart;
    auxStart.constraint = PointConstraint::Interpolated;
    auxStart.hostSegmentId = segId;
    auxStart.isAuxiliary = true;
    auxStart.interpPercent = 0.3;
    auxStart.interpFromEnd = false;
    QUuid auxStartId = auxStart.id;
    block.addPoint(std::move(auxStart));

    // Flipped direction (from end): 30% from the end → world (70, 0).
    ParamPoint auxEnd;
    auxEnd.constraint = PointConstraint::Interpolated;
    auxEnd.hostSegmentId = segId;
    auxEnd.isAuxiliary = true;
    auxEnd.interpPercent = 0.3;
    auxEnd.interpFromEnd = true;
    QUuid auxEndId = auxEnd.id;
    block.addPoint(std::move(auxEnd));

    // Flipped direction + offset: baseline runs end→start (−X), so a +90°
    // deflection points −Y. Base at 50% = (50,0) → (50, −10).
    ParamPoint auxOff;
    auxOff.constraint = PointConstraint::Interpolated;
    auxOff.hostSegmentId = segId;
    auxOff.isAuxiliary = true;
    auxOff.interpPercent = 0.5;
    auxOff.interpFromEnd = true;
    auxOff.interpOffsetAngle = 90.0;
    auxOff.interpOffsetDist = 10.0;  // mm
    QUuid auxOffId = auxOff.id;
    block.addPoint(std::move(auxOff));

    doc.addBlock(std::move(block));
    doc.resolveAll();

    const Block* rb = doc.findBlock(doc.blocks().front().id);
    QVERIFY(rb != nullptr);

    // From start: 30% of 100 → (30, 0).
    const ParamPoint* ps = rb->findPoint(auxStartId);
    QVERIFY(ps != nullptr && ps->resolved);
    const cad::geo::Vec2 ws = rb->transform.toWorld(ps->resolvedPos);
    QVERIFY(std::abs(ws.x - 30.0) < 1e-6);
    QVERIFY(std::abs(ws.y) < 1e-6);

    // From end: 30% measured from (100,0) toward (0,0) → (70, 0).
    const ParamPoint* pe = rb->findPoint(auxEndId);
    QVERIFY(pe != nullptr && pe->resolved);
    const cad::geo::Vec2 we = rb->transform.toWorld(pe->resolvedPos);
    QVERIFY(std::abs(we.x - 70.0) < 1e-6);
    QVERIFY(std::abs(we.y) < 1e-6);

    // From end + 90° offset → (50, −10).
    const ParamPoint* po = rb->findPoint(auxOffId);
    QVERIFY(po != nullptr && po->resolved);
    const cad::geo::Vec2 wo = rb->transform.toWorld(po->resolvedPos);
    QVERIFY(std::abs(wo.x - 50.0) < 1e-6);
    QVERIFY(std::abs(wo.y + 10.0) < 1e-6);
}

void TestSerializer::auxFromEndRoundTrip()
{
    // The interpFromEnd flag must survive a serialize/deserialize round trip.
    ParamDocument src;
    Block block;
    ParamPoint pA;
    pA.constraint = PointConstraint::Free;
    pA.freePos = cad::geo::Vec2(0.0, 0.0);
    QUuid idA = pA.id;
    ParamPoint pB;
    pB.constraint = PointConstraint::Free;
    pB.freePos = cad::geo::Vec2(10.0, 0.0);
    QUuid idB = pB.id;
    block.addPoint(std::move(pA));
    block.addPoint(std::move(pB));
    Segment seg;
    seg.startPointId = idA;
    seg.endPointId = idB;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = segId;
    aux.isAuxiliary = true;
    aux.interpFromEnd = true;
    QUuid auxId = aux.id;
    block.addPoint(std::move(aux));
    QUuid blockId = block.id;
    src.addBlock(std::move(block));

    QJsonObject json = DocumentSerializer::serialize(src);
    ParamDocument dst;
    DocumentSerializer::deserialize(dst, json);

    const Block* rb = dst.findBlock(blockId);
    QVERIFY(rb != nullptr);
    const ParamPoint* ra = rb->findPoint(auxId);
    QVERIFY(ra != nullptr);
    QCOMPARE(ra->interpFromEnd, true);
}

// Diagnostic for the reported L54/P128->P127 connection offset. Loads the
// user's document, resolves, and measures how far the follower point P128
// lands from its leader point P127. A correct attachment must coincide.
void TestSerializer::diagL54ConnectionOffset()
{
    const QString path = QStringLiteral("e:/\u5b58\u6863/1.gcad");  // e:/存档/1.gcad
    if (!QFileInfo::exists(path))
        QSKIP("user document 1.gcad not present");

    ParamDocument doc;
    QString err;
    QVERIFY2(cad::doc::DocumentFile::load(path, doc, &err),
             qPrintable(QStringLiteral("load failed: %1").arg(err)));

    doc.resolveAll();

    // P127 = 697f002b-... (leader point, on block 996f42fb)
    // P128 = d2fab945-... (L54 start, follower)
    const QUuid p127Block{QStringLiteral("996f42fb-b8ce-46b6-a3b8-2b43cad525f2")};
    const QUuid p127Id{QStringLiteral("697f002b-e213-479a-bf4a-6a5d3fd84bc7")};
    const QUuid l54Block{QStringLiteral("efd61f21-e25e-4670-9ede-a9838d926a56")};
    const QUuid p128Id{QStringLiteral("d2fab945-e0f3-4f86-b782-d0ffb3ffd9e6")};

    const Block* b127 = doc.findBlock(p127Block);
    const Block* bL54 = doc.findBlock(l54Block);
    // This is a diagnostic regression test pinned to a specific user document.
    // The document is user-mutable (it gets re-saved/edited), so if the blocks
    // it references are gone the test is simply no longer applicable — skip
    // rather than fail.
    if (!(b127 && bL54))
        QSKIP("document 1.gcad no longer contains the referenced blocks");

    cad::geo::Vec2 w127 = b127->worldPos(p127Id);
    cad::geo::Vec2 w128 = bL54->worldPos(p128Id);

    qDebug() << "P127 world:" << w127.x << w127.y;
    qDebug() << "P128 world:" << w128.x << w128.y;
    qDebug() << "offset mm :" << w127.distanceTo(w128);

    // A snapped attachment must place P128 exactly on P127.
    QVERIFY2(w127.distanceTo(w128) < 1e-3,
             qPrintable(QStringLiteral("P128 is offset from P127 by %1 mm")
                        .arg(w127.distanceTo(w128))));
}

QTEST_MAIN(TestSerializer)
#include "test_serializer.moc"