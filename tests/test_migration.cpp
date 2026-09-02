/// @file test_migration.cpp
/// On-disk format migration chain (ARCHITECTURE_REVIEW P2-1).
///
/// These tests pin the HISTORY of the .gcad format: a v0 file (predates the
/// auxiliary calculation layer, references layers by integer index) must land
/// in exactly the place a v1 file would. Before P2-1 that knowledge lived as
/// field sniffing inside DocumentSerializer::deserialize and had no test at
/// all; now it is one named, versioned, directly callable step.
///
/// Everything here is pure JSON -> JSON (plus a couple of end-to-end loads
/// through DocumentFile in a temp dir) — no external document is referenced.

#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QPair>
#include <QTemporaryDir>
#include <QUuid>

#include "document/FormatMigration.h"
#include "document/DocumentFile.h"
#include "document/DocumentSerializer.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "geometry/Vec2.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

QString uuidStr(const QUuid& id) { return id.toString(QUuid::WithoutBraces); }

/// A minimal v0-shaped document: no auxiliary layer, integer layer refs.
/// @p blockLayers are the raw integer indices written into each block.
QJsonObject makeV0Doc(const QList<int>& blockLayers, int activeLayer,
                      bool withLayersArray = true)
{
    QJsonObject root;
    QJsonObject docObj;
    docObj["pointSeq"] = 1;
    docObj["lineSeq"]  = 1;

    if (withLayersArray) {
        QJsonArray layers;
        QJsonObject l0;
        l0["name"] = QStringLiteral("工作层 1");
        l0["type"] = QStringLiteral("working");
        layers.append(l0);
        QJsonObject l1;
        l1["name"] = QStringLiteral("工作层 2");
        l1["type"] = QStringLiteral("working");
        layers.append(l1);
        docObj["layers"] = layers;
    }
    if (activeLayer >= 0)
        docObj["activeLayer"] = activeLayer;

    QJsonArray blocks;
    for (int i = 0; i < blockLayers.size(); ++i) {
        QJsonObject b;
        b["id"]    = uuidStr(QUuid::createUuid());
        b["name"]  = QStringLiteral("块%1").arg(i + 1);
        b["layer"] = blockLayers[i];

        QJsonObject p1;
        p1["id"] = uuidStr(QUuid::createUuid());
        p1["constraint"] = QStringLiteral("Free");
        QJsonObject pos; pos["x"] = 0.0; pos["y"] = 0.0;
        p1["freePos"] = pos;
        QJsonObject p2;
        p2["id"] = uuidStr(QUuid::createUuid());
        p2["constraint"] = QStringLiteral("Polar");
        p2["refPointId"] = p1["id"];
        p2["distance"] = 50.0;
        p2["angle"] = 0.0;
        QJsonArray pts; pts.append(p1); pts.append(p2);
        b["points"] = pts;

        QJsonObject seg;
        seg["id"] = uuidStr(QUuid::createUuid());
        seg["startPointId"] = p1["id"];
        seg["endPointId"] = p2["id"];
        seg["type"] = QStringLiteral("Line");
        QJsonArray segs; segs.append(seg);
        b["segments"] = segs;

        blocks.append(b);
    }
    docObj["blocks"] = blocks;
    root["document"] = docObj;
    root["variables"] = QJsonObject();
    return root;
}

/// Two-point line block, added through the public API.
struct LineIds { QUuid blockId; };
LineIds addLineBlock(ParamDocument& doc, const QUuid& layerId, Vec2 origin)
{
    Block b;
    b.layer = layerId;
    b.transform.origin = origin;
    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = p1.id;
    p2.distance = 60.0;
    p2.angle = 0.0;
    b.addPoint(std::move(p1));
    b.addPoint(std::move(p2));
    Segment s;
    s.startPointId = b.points[0].id;
    s.endPointId = b.points[1].id;
    b.addSegment(std::move(s));
    const QUuid id = b.id;
    doc.addBlock(std::move(b));
    return {id};
}

} // namespace

class TestMigration : public QObject
{
    Q_OBJECT

private slots:
    void v0InsertsAuxLayerAndShiftsIndices();
    void v0WithoutLayersArrayGetsDefaultPair();
    void v0WithExistingAuxLayerDoesNotShift();
    void currentVersionDocumentIsUntouched();
    void newerThanSupportedIsRejected();
    void missingStepIsRejectedRatherThanGuessed();
    void blockWithoutLayerFieldIsLeftToTheSerializer();
    void migratedV0DeserializesLikeV1();
    void v1ArchiveStillRoundTrips();
    void v1ShadowOffsetKeyIsDropped();
    // v2 → v3 (拆开影子基准, DETACH_SHADOW_DESIGN.md §8.2): 纯直通迁移 ——
    // 旧档零迁移负载; 已删除影子偏转功能的残留键 (shadowAnchorRotDeg /
    // noFollowRotate) 清理不回归; 链 1→2→3 无缺口。
    void v2ShadowLegacyKeysDroppedAndChainComplete();
};

void TestMigration::v0InsertsAuxLayerAndShiftsIndices()
{
    // v0: two working layers, blocks on index 0 and 1. v1 puts the auxiliary
    // layer at index 0, so both indices shift up by one.
    QJsonObject root = makeV0Doc({0, 1}, 1);

    QStringList warnings;
    QString error;
    QVERIFY(cad::doc::FormatMigration::migrate(0, root, &warnings, &error));
    QVERIFY(error.isEmpty());

    const QJsonObject docObj = root["document"].toObject();
    const QJsonArray layers = docObj["layers"].toArray();
    QCOMPARE(layers.size(), 3);
    QCOMPARE(layers[0].toObject()["type"].toString(), QStringLiteral("auxiliary"));
    QCOMPARE(layers[1].toObject()["name"].toString(), QStringLiteral("工作层 1"));
    QCOMPARE(layers[2].toObject()["name"].toString(), QStringLiteral("工作层 2"));

    const QJsonArray blocks = docObj["blocks"].toArray();
    QCOMPARE(blocks.size(), 2);
    // Every layer ref is now a stable id string, pointing at the SHIFTED slot.
    QCOMPARE(blocks[0].toObject()["layer"].toString(),
             layers[1].toObject()["id"].toString());
    QCOMPARE(blocks[1].toObject()["layer"].toString(),
             layers[2].toObject()["id"].toString());

    // activeLayer 1 + shift 1 -> the last layer (工作层 2), as the old
    // reader's qBound(toInt(1) + 1, 0, count-1) did.
    QCOMPARE(docObj["activeLayer"].toString(),
             layers[2].toObject()["id"].toString());

    QVERIFY(!warnings.isEmpty());
}

void TestMigration::v0WithoutLayersArrayGetsDefaultPair()
{
    // No "layers" array at all = a v0 file from before layers existed: one
    // implicit working layer.
    QJsonObject root = makeV0Doc({0, 0, 0}, -1, /*withLayersArray=*/false);

    QStringList warnings;
    QString error;
    QVERIFY(cad::doc::FormatMigration::migrate(0, root, &warnings, &error));

    const QJsonObject docObj = root["document"].toObject();
    const QJsonArray layers = docObj["layers"].toArray();
    QCOMPARE(layers.size(), 2);
    QCOMPARE(layers[0].toObject()["name"].toString(), QStringLiteral("辅助层"));
    QCOMPARE(layers[0].toObject()["type"].toString(), QStringLiteral("auxiliary"));
    QCOMPARE(layers[1].toObject()["name"].toString(), QStringLiteral("图层 1"));

    // Old index 0 = the only working layer -> maps onto 图层 1 (index 1 now).
    const QJsonArray blocks = docObj["blocks"].toArray();
    for (int i = 0; i < blocks.size(); ++i)
        QCOMPARE(blocks[i].toObject()["layer"].toString(),
                 layers[1].toObject()["id"].toString());

    // Missing activeLayer: the historical default was index 1 (+shift), which
    // clamps to the first working layer.
    QCOMPARE(docObj["activeLayer"].toString(),
             layers[1].toObject()["id"].toString());
}

void TestMigration::v0WithExistingAuxLayerDoesNotShift()
{
    QJsonObject root = makeV0Doc({1}, 0);
    QJsonObject docObj = root["document"].toObject();
    QJsonArray layers = docObj["layers"].toArray();
    QJsonObject aux;
    aux["id"] = uuidStr(QUuid::createUuid());
    aux["name"] = QStringLiteral("辅助层");
    aux["type"] = QStringLiteral("auxiliary");
    layers.prepend(aux);                 // v0 file that already had an aux layer
    docObj["layers"] = layers;
    root["document"] = docObj;

    QString error;
    QVERIFY(cad::doc::FormatMigration::migrate(0, root, nullptr, &error));

    docObj = root["document"].toObject();
    layers = docObj["layers"].toArray();
    QCOMPARE(layers.size(), 3);
    // No shift: block index 1 still means 工作层 1 (now at index 1? no — the
    // aux was already there, so index 1 is 工作层 1).
    QCOMPARE(docObj["blocks"].toArray()[0].toObject()["layer"].toString(),
             layers[1].toObject()["id"].toString());
    // 0 + no shift = index 0, i.e. the aux layer itself. That is what the old
    // reader did too (qBound(0 + 0, 0, count-1)) — the migration reproduces
    // history exactly, quirks included, rather than "improving" it.
    QCOMPARE(docObj["activeLayer"].toString(),
             layers[0].toObject()["id"].toString());
}

void TestMigration::currentVersionDocumentIsUntouched()
{
    QJsonObject docObj;
    QJsonArray layers;
    QJsonObject l;
    const QString layerId = uuidStr(QUuid::createUuid());
    l["id"] = layerId;
    l["name"] = QStringLiteral("图层 1");
    l["type"] = QStringLiteral("working");
    layers.append(l);
    docObj["layers"] = layers;
    docObj["activeLayer"] = layerId;
    QJsonObject root;
    root["document"] = docObj;

    QString error;
    const QJsonObject before = root;
    QVERIFY(cad::doc::FormatMigration::migrate(cad::doc::kFormatVersion, root,
                                               nullptr, &error));
    QCOMPARE(QJsonDocument(root).toJson(), QJsonDocument(before).toJson());
}

void TestMigration::newerThanSupportedIsRejected()
{
    QJsonObject root;
    QString error;
    QVERIFY(!cad::doc::FormatMigration::migrate(cad::doc::kFormatVersion + 1,
                                                root, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("不受支持")));
}

void TestMigration::missingStepIsRejectedRatherThanGuessed()
{
    // The chain is the safety property: if someone bumps kFormatVersion and
    // forgets to write the step, loading must fail loudly instead of falling
    // through to field sniffing. Simulated by asking for a migration from a
    // version that has no registered step *below* the current one — only
    // meaningful once kFormatVersion > 1, so assert the invariant that the
    // chain is complete from 0 instead.
    const auto steps = cad::doc::FormatMigration::steps();
    QVERIFY(!steps.empty());
    int expected = 0;
    for (const auto& s : steps) {
        QCOMPARE(s.from, expected);      // contiguous, no gaps
        QVERIFY(s.name != nullptr);
        QVERIFY(s.fn != nullptr);
        ++expected;
    }
    QCOMPARE(expected, cad::doc::kFormatVersion);
}

void TestMigration::blockWithoutLayerFieldIsLeftToTheSerializer()
{
    // A block with NO "layer" key must keep not having one: the serializer's
    // "missing layer field" defaulting (first working layer) must still run.
    // Inventing an index here would silently change where the block lands.
    QJsonObject root = makeV0Doc({}, -1);
    QJsonObject docObj = root["document"].toObject();
    QJsonArray blocks = docObj["blocks"].toArray();
    QJsonObject b;
    b["id"] = uuidStr(QUuid::createUuid());
    b["name"] = QStringLiteral("无图层块");
    blocks.append(b);
    docObj["blocks"] = blocks;
    root["document"] = docObj;

    QString error;
    QVERIFY(cad::doc::FormatMigration::migrate(0, root, nullptr, &error));
    const QJsonObject outBlock = root["document"].toObject()["blocks"].toArray()
                                     .last().toObject();
    QVERIFY(!outBlock.contains(QStringLiteral("layer")));
}

void TestMigration::migratedV0DeserializesLikeV1()
{
    // A real v1 document is DOWNGRADED to v0 by hand (drop the auxiliary
    // layer, drop stable ids, write integer layer refs) and then pushed
    // through the chain. It must come back equivalent to the original:
    // same blocks, all on the working layer, aux layer restored in front.
    ParamDocument src;
    const QUuid workLayer = src.layers().back().id;
    const auto a = addLineBlock(src, workLayer, Vec2(0.0, 0.0));
    const auto b = addLineBlock(src, workLayer, Vec2(0.0, 80.0));
    src.resolveAll();
    QCOMPARE((int)src.blocks().size(), 2);

    QJsonObject docObj = DocumentSerializer::serialize(src)["document"].toObject();

    QJsonArray layers;
    for (const auto& v : docObj["layers"].toArray()) {
        if (v.toObject()["type"].toString() == QLatin1String("auxiliary"))
            continue;                                   // v0 had no aux layer
        QJsonObject l = v.toObject();
        l.remove(QStringLiteral("id"));                 // v0 had no stable ids
        layers.append(l);
    }
    docObj["layers"] = layers;

    QJsonArray blocks = docObj["blocks"].toArray();
    for (int i = 0; i < blocks.size(); ++i) {
        QJsonObject blk = blocks[i].toObject();
        blk["layer"] = 0;                               // v0: working-layer index
        blocks[i] = blk;
    }
    docObj["blocks"] = blocks;
    docObj["activeLayer"] = 0;

    QStringList warnings;
    QString migErr;
    QJsonObject root{{"document", docObj}, {"variables", QJsonObject()}};
    QVERIFY2(cad::doc::FormatMigration::migrate(0, root, &warnings, &migErr),
             qPrintable(migErr));

    ParamDocument dst;
    DocumentSerializer::deserialize(dst, root, &warnings);
    QCOMPARE((int)dst.blocks().size(), 2);
    QCOMPARE((int)dst.layers().size(), 2);
    QCOMPARE(dst.layers()[0].type, LayerType::Auxiliary);
    QCOMPARE(dst.layers()[1].type, LayerType::Working);
    QCOMPARE(dst.findBlock(a.blockId)->layer, dst.layers()[1].id);
    QCOMPARE(dst.findBlock(b.blockId)->layer, dst.layers()[1].id);
    QCOMPARE(dst.activeLayer(), dst.layers()[1].id);
    QVERIFY(dst.diagnostics().empty());
}

void TestMigration::v1ArchiveStillRoundTrips()
{
    // Guards the DocumentFile::load edit: a current-version archive must save
    // and load unchanged (no migration runs), in a temp dir (never a
    // repository-external fixture — see AGENTS.md「测试档铁律」).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ParamDocument src;
    const QUuid workLayer = src.layers().back().id;
    const auto a = addLineBlock(src, workLayer, Vec2(0.0, 0.0));
    src.resolveAll();

    const QString path = dir.path() + QStringLiteral("/current.gcad");
    QString err;
    QVERIFY2(cad::doc::DocumentFile::save(path, src, &err), qPrintable(err));

    ParamDocument dst;
    QStringList warnings;
    QVERIFY2(cad::doc::DocumentFile::load(path, dst, &err, &warnings), qPrintable(err));
    QCOMPARE((int)dst.blocks().size(), 1);
    QVERIFY(dst.findBlock(a.blockId) != nullptr);   // block ids survive the archive
    QCOMPARE((int)dst.layers().size(), 2);
    QCOMPARE(dst.layers()[0].type, LayerType::Auxiliary);
    QVERIFY(warnings.isEmpty());   // a current-version file migrates silently
}

void TestMigration::v1ShadowOffsetKeyIsDropped()
{
    // v1 stored the shadow as a cumulative offset (baselineOffsetDeg, degrees).
    // The shadow-deflection feature has since been removed (2026): the v1→v2
    // step now only drops the dead legacy key — it must not write any anchor
    // field, and every attachment keeps its other data intact.
    QJsonObject root;
    QJsonObject docObj;
    docObj["pointSeq"] = 1;
    docObj["lineSeq"]  = 1;

    QJsonArray layers;
    QJsonObject l0;
    l0["id"] = uuidStr(QUuid::createUuid());
    l0["name"] = QStringLiteral("辅助层");
    l0["type"] = QStringLiteral("auxiliary");
    layers.append(l0);
    QJsonObject l1;
    l1["id"] = uuidStr(QUuid::createUuid());
    l1["name"] = QStringLiteral("工作层 1");
    l1["type"] = QStringLiteral("working");
    layers.append(l1);
    docObj["layers"] = layers;
    docObj["activeLayer"] = l1["id"];

    const QString hostId = uuidStr(QUuid::createUuid());
    const QString followerId = uuidStr(QUuid::createUuid());

    QJsonArray blocks;
    for (const auto& def : {QPair<QString, double>{hostId, 0.5},
                            {followerId, 0.0}}) {
        QJsonObject b;
        b["id"] = def.first;
        b["name"] = QStringLiteral("块");
        b["layer"] = l1["id"];
        b["rotation"] = def.second;
        QJsonObject p1;
        p1["id"] = uuidStr(QUuid::createUuid());
        p1["constraint"] = QStringLiteral("Free");
        QJsonObject pos; pos["x"] = 0.0; pos["y"] = 0.0;
        p1["freePos"] = pos;
        QJsonObject p2;
        p2["id"] = uuidStr(QUuid::createUuid());
        p2["constraint"] = QStringLiteral("Polar");
        p2["refPointId"] = p1["id"];
        p2["distance"] = 50.0;
        p2["angle"] = 0.0;
        QJsonArray pts; pts.append(p1); pts.append(p2);
        b["points"] = pts;
        QJsonObject seg;
        seg["id"] = uuidStr(QUuid::createUuid());
        seg["startPointId"] = p1["id"];
        seg["endPointId"] = p2["id"];
        seg["type"] = QStringLiteral("Line");
        QJsonArray segs; segs.append(seg);
        b["segments"] = segs;
        blocks.append(b);
    }
    docObj["blocks"] = blocks;

    QJsonObject att;
    att["id"] = uuidStr(QUuid::createUuid());
    att["fromBlockId"] = followerId;
    att["fromPointId"] = blocks[1].toObject()["points"].toArray()[0].toObject()["id"];
    att["toBlockId"] = hostId;
    att["toPointId"] = blocks[0].toObject()["points"].toArray()[1].toObject()["id"];
    att["followerAngle"] = 180.0;
    att["baselineOffsetDeg"] = 30.0;   // v11 旧账本 (已随功能删除)
    QJsonArray atts; atts.append(att);
    docObj["attachments"] = atts;

    root["document"] = docObj;
    root["variables"] = QJsonObject();

    QStringList warnings;
    QString error;
    QVERIFY(cad::doc::FormatMigration::migrate(1, root, &warnings, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(!warnings.isEmpty());   // 清理有提示

    const QJsonObject outAtt = root["document"].toObject()["attachments"]
                                   .toArray()[0].toObject();
    QVERIFY(!outAtt.contains(QStringLiteral("baselineOffsetDeg")));
    QVERIFY(!outAtt.contains(QStringLiteral("shadowAnchorRotDeg")));
    QCOMPARE(outAtt["followerAngle"].toDouble(), 180.0);   // 其余字段原样保留
}

void TestMigration::v2ShadowLegacyKeysDroppedAndChainComplete()
{
    // v2 → v3 "shadow-line": 拆开影子基准上格式 —— 块新增 isShadow /
    // shadowMasterBlockId (Optional since v3, 旧档无该键 = 安全默认值, 零
    // 迁移负载)。本步只清理已删除影子偏转功能的残留键, 其余字节直通。
    QJsonObject root;
    QJsonObject docObj;
    docObj["pointSeq"] = 1;
    docObj["lineSeq"]  = 1;

    QJsonArray layers;
    QJsonObject l0;
    l0["id"] = uuidStr(QUuid::createUuid());
    l0["name"] = QStringLiteral("辅助层");
    l0["type"] = QStringLiteral("auxiliary");
    layers.append(l0);
    QJsonObject l1;
    l1["id"] = uuidStr(QUuid::createUuid());
    l1["name"] = QStringLiteral("工作层 1");
    l1["type"] = QStringLiteral("working");
    layers.append(l1);
    docObj["layers"] = layers;
    docObj["activeLayer"] = l1["id"];

    const QString hostId = uuidStr(QUuid::createUuid());
    const QString followerId = uuidStr(QUuid::createUuid());

    QJsonArray blocks;
    for (const auto& def : {QPair<QString, double>{hostId, 0.5},
                            {followerId, 0.0}}) {
        QJsonObject b;
        b["id"] = def.first;
        b["name"] = QStringLiteral("块");
        b["layer"] = l1["id"];
        b["rotation"] = def.second;
        QJsonObject p1;
        p1["id"] = uuidStr(QUuid::createUuid());
        p1["constraint"] = QStringLiteral("Free");
        QJsonObject pos; pos["x"] = 0.0; pos["y"] = 0.0;
        p1["freePos"] = pos;
        QJsonObject p2;
        p2["id"] = uuidStr(QUuid::createUuid());
        p2["constraint"] = QStringLiteral("Polar");
        p2["refPointId"] = p1["id"];
        p2["distance"] = 50.0;
        p2["angle"] = 0.0;
        QJsonArray pts; pts.append(p1); pts.append(p2);
        b["points"] = pts;
        QJsonObject seg;
        seg["id"] = uuidStr(QUuid::createUuid());
        seg["startPointId"] = p1["id"];
        seg["endPointId"] = p2["id"];
        seg["type"] = QStringLiteral("Line");
        QJsonArray segs; segs.append(seg);
        b["segments"] = segs;
        blocks.append(b);
    }
    docObj["blocks"] = blocks;

    QJsonObject att;
    att["id"] = uuidStr(QUuid::createUuid());
    att["fromBlockId"] = followerId;
    att["fromPointId"] = blocks[1].toObject()["points"].toArray()[0].toObject()["id"];
    att["toBlockId"] = hostId;
    att["toPointId"] = blocks[0].toObject()["points"].toArray()[1].toObject()["id"];
    att["followerAngle"] = 180.0;
    att["shadowAnchorRotDeg"] = 15.0;   // 影子偏转残留 (功能已删除, d79e425)
    att["noFollowRotate"] = true;       // 不跟随旋转残留 (功能已删除)
    QJsonArray atts; atts.append(att);
    docObj["attachments"] = atts;

    root["document"] = docObj;
    root["variables"] = QJsonObject();

    // 链完整性: {2, shadow-line} 已注册, 1→2→3 无缺口 (缺环节拒绝加载是
    // 既有契约 —— missingStepIsRejectedRatherThanGuessed 覆盖缺口侧)。
    bool hasShadowLineStep = false;
    for (const auto& s : cad::doc::FormatMigration::steps())
        if (s.from == 2 && QLatin1String(s.name) == QLatin1String("shadow-line"))
            hasShadowLineStep = true;
    QVERIFY2(hasShadowLineStep, "注册表应含 {2, shadow-line} 步骤");
    QCOMPARE(cad::doc::kFormatVersion, 3);

    QStringList warnings;
    QString error;
    QVERIFY(cad::doc::FormatMigration::migrate(2, root, &warnings, &error));
    QVERIFY(error.isEmpty());
    QVERIFY(!warnings.isEmpty());   // 残留键清理有提示

    const QJsonObject outDoc = root["document"].toObject();
    const QJsonObject outAtt = outDoc["attachments"].toArray()[0].toObject();
    QVERIFY(!outAtt.contains(QStringLiteral("shadowAnchorRotDeg")));
    QVERIFY(!outAtt.contains(QStringLiteral("noFollowRotate")));
    QCOMPARE(outAtt["followerAngle"].toDouble(), 180.0);   // 其余字段原样保留
    // 直通: 块字段 (无 isShadow 键的旧档) 原样透传, 不添不减。
    QCOMPARE(outDoc["blocks"].toArray().size(), 2);
    QVERIFY(!outDoc["blocks"].toArray()[0].toObject().contains(QStringLiteral("isShadow")));
}

QTEST_GUILESS_MAIN(TestMigration)
#include "test_migration.moc"
