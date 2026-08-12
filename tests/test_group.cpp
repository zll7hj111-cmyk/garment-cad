#include <QtTest>
#include <QUndoStack>
#include <QJsonArray>
#include <QJsonObject>

#include "parametric/ParamDocument.h"
#include "parametric/Group.h"
#include "parametric/Duplicate.h"
#include "document/commands/GroupCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/BlockCommands.h"
#include "document/DocumentSerializer.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::test::makeLine;
using cad::test::LineSetup;
using cad::test::layerIdAt;

namespace {

/// Attach followerBlock to leaderBlock's END point (follower angle 0).
Attachment makeFollowAtt(const LineSetup& follower, const LineSetup& leader)
{
    Attachment att;
    att.fromBlockId = follower.blockId;
    att.fromPointId = follower.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.followerAngle = 0.0;
    return att;
}

} // namespace

class TestGroup : public QObject
{
    Q_OBJECT

private slots:
    void createGroupValidation();
    void makeGroupKeepsCrossBoundary();
    void makeGroupKeepsAims();
    void outwardConnectionsUnlimited();
    void removeBlockCascade();
    void makeGroupUndoRedo();
    void ungroupUndo();
    void renameGroupUndo();
    void deleteWholeGroupUndo();
    void duplicateWholeGroup();
    void moveGroupReorder();
    void serializeRoundTrip();
    void serializeOrphanWarning();
};

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::createGroupValidation()
{
    ParamDocument doc;
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});

    // < 2 members rejected.
    QVERIFY(doc.createGroup({a.blockId}).isNull());
    // Missing member rejected.
    QVERIFY(doc.createGroup({a.blockId, QUuid::createUuid()}).isNull());

    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    QVERIFY(!gid.isNull());
    QCOMPARE(doc.blocksInGroup(gid).size(), 2);
    QCOMPARE(doc.groupOfBlock(a.blockId), gid);

    // Nesting rejected: either member already grouped.
    auto c = makeLine(doc, 100.0, Vec2{0.0, -100.0});
    QVERIFY(doc.createGroup({a.blockId, c.blockId}).isNull());

    // Cross-layer rejected.
    auto d = makeLine(doc, 100.0, Vec2{0.0, -150.0});
    doc.findBlock(d.blockId)->layer = doc.auxLayerId();   // aux ≠ working layer
    QVERIFY(doc.createGroup({c.blockId, d.blockId}).isNull());
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::makeGroupKeepsCrossBoundary()
{
    ParamDocument doc;
    // Chain A ← B ← C (B follows A, C follows B).
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0);
    auto c = makeLine(doc, 100.0);
    QVERIFY(doc.addAttachment(makeFollowAtt(b, a)));
    QVERIFY(doc.addAttachment(makeFollowAtt(c, b)));
    QCOMPARE(doc.attachments().size(), size_t(2));

    const Vec2 cOriginBefore = doc.findBlock(c.blockId)->transform.origin;

    // Group {A, B}: 组零限制 —— 跨边界连接 B→C 保持不断, 内部 A←B 不动,
    // 几何零改动 (成组是纯选择标签).
    doc.undoStack()->push(new cad::cmd::MakeGroupCommand(
        &doc, {a.blockId, b.blockId}));

    QCOMPARE(doc.attachments().size(), size_t(2));

    const Vec2 cOriginAfter = doc.findBlock(c.blockId)->transform.origin;
    QVERIFY(std::abs(cOriginAfter.x - cOriginBefore.x) < 1e-6);
    QVERIFY(std::abs(cOriginAfter.y - cOriginBefore.y) < 1e-6);

    // Both members grouped; C free.
    const QUuid gid = doc.groupOfBlock(a.blockId);
    QVERIFY(!gid.isNull());
    QCOMPARE(doc.groupOfBlock(b.blockId), gid);
    QVERIFY(doc.groupOfBlock(c.blockId).isNull());

    // Undo: 只解散组, 连接仍然全保留.
    doc.undoStack()->undo();
    QVERIFY(doc.groups().empty());
    QCOMPARE(doc.attachments().size(), size_t(2));
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::makeGroupKeepsAims()
{
    ParamDocument doc;
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    auto c = makeLine(doc, 100.0, Vec2{200.0, 0.0});    // external aim target
    auto d = makeLine(doc, 100.0, Vec2{200.0, -50.0});  // aims INTO the group

    // Cross-boundary aims (双向): member a aims at external c; external d
    // aims at member a. 组零限制 —— 成组不清除任何 endTarget.
    Block* blkA = doc.findBlock(a.blockId);
    Block* blkD = doc.findBlock(d.blockId);
    blkA->endTargetBlockId  = c.blockId;
    blkA->endTargetPointId  = c.endId;
    blkA->endTargetOffset   = 15.0;
    blkA->endTargetOffsetFormula = QStringLiteral("b/2");
    blkD->endTargetBlockId  = a.blockId;
    blkD->endTargetPointId  = a.endId;
    blkD->endTargetOffset   = -7.5;

    doc.undoStack()->push(new cad::cmd::MakeGroupCommand(
        &doc, {a.blockId, b.blockId}));

    // Both directions stay intact after grouping.
    QCOMPARE(doc.findBlock(a.blockId)->endTargetBlockId, c.blockId);
    QCOMPARE(doc.findBlock(a.blockId)->endTargetPointId, c.endId);
    QCOMPARE(doc.findBlock(a.blockId)->endTargetOffset, 15.0);
    QCOMPARE(doc.findBlock(d.blockId)->endTargetBlockId, a.blockId);
    QCOMPARE(doc.findBlock(d.blockId)->endTargetPointId, a.endId);
    QCOMPARE(doc.findBlock(d.blockId)->endTargetOffset, -7.5);

    // Undo only dissolves the group — aims untouched.
    doc.undoStack()->undo();
    QVERIFY(doc.groups().empty());
    QCOMPARE(doc.findBlock(a.blockId)->endTargetBlockId, c.blockId);
    QCOMPARE(doc.findBlock(d.blockId)->endTargetBlockId, a.blockId);
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::outwardConnectionsUnlimited()
{
    ParamDocument doc;
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    auto c = makeLine(doc, 100.0, Vec2{200.0, 0.0});
    auto e = makeLine(doc, 100.0, Vec2{400.0, 0.0});
    auto f = makeLine(doc, 100.0, Vec2{400.0, -50.0});
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});

    // 组零限制: 外部 follower 挂组员 (内向) 与组员连接外部 (外向) 均自由,
    // 无主连接预算 —— 多个外向连接也全部允许.
    QVERIFY(doc.addAttachment(makeFollowAtt(c, a)));
    QVERIFY(doc.addAttachment(makeFollowAtt(a, e)));
    QVERIFY(doc.addAttachment(makeFollowAtt(b, f)));
    QCOMPARE(doc.attachments().size(), size_t(3));
    QCOMPARE(doc.groupOfBlock(a.blockId), gid);
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::removeBlockCascade()
{
    ParamDocument doc;
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    auto c = makeLine(doc, 100.0, Vec2{0.0, -100.0});
    const QUuid gid = doc.createGroup({a.blockId, b.blockId, c.blockId});

    // 3 → 2: the group survives.
    doc.removeBlock(c.blockId);
    QCOMPARE(doc.groups().size(), size_t(1));
    QCOMPARE(doc.blocksInGroup(gid).size(), 2);

    // 2 → 1: the group dissolves automatically.
    doc.removeBlock(b.blockId);
    QVERIFY(doc.groups().empty());
    QVERIFY(doc.groupOfBlock(a.blockId).isNull());
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::makeGroupUndoRedo()
{
    ParamDocument doc;
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0);
    auto c = makeLine(doc, 100.0);
    // Internal A←B link; C follows B (跨边界连接, 成组时保持).
    QVERIFY(doc.addAttachment(makeFollowAtt(b, a)));
    QVERIFY(doc.addAttachment(makeFollowAtt(c, b)));
    QUuid gid;

    doc.undoStack()->push(new cad::cmd::MakeGroupCommand(
        &doc, {a.blockId, b.blockId}));
    gid = doc.groupOfBlock(a.blockId);
    QVERIFY(!gid.isNull());
    QCOMPARE(doc.attachments().size(), size_t(2));

    // Undo: group gone, attachments untouched (组零限制).
    doc.undoStack()->undo();
    QVERIFY(doc.groups().empty());
    QVERIFY(doc.groupOfBlock(a.blockId).isNull());
    QCOMPARE(doc.attachments().size(), size_t(2));

    // Redo: the SAME group record (id + serial) comes back — the serial
    // counter must not advance again.
    doc.undoStack()->redo();
    QCOMPARE(doc.groupOfBlock(a.blockId), gid);
    QCOMPARE(doc.attachments().size(), size_t(2));
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::ungroupUndo()
{
    ParamDocument doc;
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});

    doc.undoStack()->push(new cad::cmd::UngroupCommand(&doc, gid));
    QVERIFY(doc.groups().empty());
    QVERIFY(doc.groupOfBlock(a.blockId).isNull());

    doc.undoStack()->undo();
    QCOMPARE(doc.groups().size(), size_t(1));
    QCOMPARE(doc.groupOfBlock(a.blockId), gid);
    QCOMPARE(doc.groupOfBlock(b.blockId), gid);
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::renameGroupUndo()
{
    ParamDocument doc;
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    const QUuid gid = doc.createGroup({a.blockId, b.blockId}, QStringLiteral("旧名"));

    doc.undoStack()->push(new cad::cmd::RenameGroupCommand(
        &doc, gid, QStringLiteral("前片")));
    QCOMPARE(doc.findGroup(gid)->name, QStringLiteral("前片"));

    doc.undoStack()->undo();
    QCOMPARE(doc.findGroup(gid)->name, QStringLiteral("旧名"));

    doc.undoStack()->redo();
    QCOMPARE(doc.findGroup(gid)->name, QStringLiteral("前片"));
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::deleteWholeGroupUndo()
{
    ParamDocument doc;
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});

    doc.undoStack()->beginMacro(QStringLiteral("del"));
    doc.undoStack()->push(new cad::cmd::DeleteBlockCommand(&doc, a.blockId));
    doc.undoStack()->push(new cad::cmd::DeleteBlockCommand(&doc, b.blockId));
    doc.undoStack()->endMacro();

    QVERIFY(doc.groups().empty());
    QCOMPARE(doc.blocks().size(), size_t(0));

    // 删整组后 Ctrl+Z：块与组记录完整还原.
    doc.undoStack()->undo();
    QCOMPARE(doc.blocks().size(), size_t(2));
    QCOMPARE(doc.groups().size(), size_t(1));
    QCOMPARE(doc.groupOfBlock(a.blockId), gid);
    QCOMPARE(doc.groupOfBlock(b.blockId), gid);
    QCOMPARE(doc.blocksInGroup(gid).size(), 2);
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::duplicateWholeGroup()
{
    ParamDocument doc;
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0);
    QVERIFY(doc.addAttachment(makeFollowAtt(b, a)));
    doc.createGroup({a.blockId, b.blockId});

    // Whole-group copy → the clones form a NEW group (副本成新组).
    DuplicateResult whole = duplicateBlocks(doc, {a.blockId, b.blockId});
    QVERIFY(whole.newGroup.has_value());
    QCOMPARE(whole.newGroupMembers.size(), 2);
    QCOMPARE(whole.attachments.size(), size_t(1));   // internal link remapped

    // Partial copy → no group.
    DuplicateResult part = duplicateBlocks(doc, {a.blockId});
    QVERIFY(!part.newGroup.has_value());

    // Command replay registers the clone group; undo dissolves it.
    doc.undoStack()->push(new cad::cmd::DuplicateBlocksCommand(&doc, std::move(whole)));
    QCOMPARE(doc.groups().size(), size_t(2));
    doc.undoStack()->undo();
    QCOMPARE(doc.groups().size(), size_t(1));
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::moveGroupReorder()
{
    ParamDocument doc;
    auto a1 = makeLine(doc, 100.0);
    auto b1 = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    auto a2 = makeLine(doc, 100.0, Vec2{200.0, 0.0});
    auto b2 = makeLine(doc, 100.0, Vec2{200.0, -50.0});
    auto a3 = makeLine(doc, 100.0, Vec2{400.0, 0.0});
    auto b3 = makeLine(doc, 100.0, Vec2{400.0, -50.0});
    const QUuid g1 = doc.createGroup({a1.blockId, b1.blockId});
    const QUuid g2 = doc.createGroup({a2.blockId, b2.blockId});
    const QUuid g3 = doc.createGroup({a3.blockId, b3.blockId});
    QCOMPARE(doc.groups().size(), size_t(3));
    QCOMPARE(doc.groups()[0].id, g1);

    doc.moveGroup(0, 2);   // g1 → end
    QCOMPARE(doc.groups()[0].id, g2);
    QCOMPARE(doc.groups()[1].id, g3);
    QCOMPARE(doc.groups()[2].id, g1);

    doc.moveGroup(2, 0);   // back to the original order
    QCOMPARE(doc.groups()[0].id, g1);
    QCOMPARE(doc.groups()[1].id, g2);
    QCOMPARE(doc.groups()[2].id, g3);

    // Out-of-range / no-op moves are ignored.
    doc.moveGroup(-1, 1);
    doc.moveGroup(0, 99);
    doc.moveGroup(0, 0);
    QCOMPARE(doc.groups().size(), size_t(3));
    QCOMPARE(doc.groups()[1].id, g2);

    // The order survives a serialization round trip (groups array order).
    const QJsonObject json = DocumentSerializer::serialize(doc);
    ParamDocument restored;
    QStringList warnings;
    DocumentSerializer::deserialize(restored, json, &warnings);
    QVERIFY(warnings.isEmpty());
    QCOMPARE(restored.groups().size(), size_t(3));
    QCOMPARE(restored.groups()[0].id, g1);
    QCOMPARE(restored.groups()[1].id, g2);
    QCOMPARE(restored.groups()[2].id, g3);
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::serializeRoundTrip()
{
    ParamDocument doc;
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    doc.setGroupName(gid, QStringLiteral("前片"));

    const QJsonObject json = DocumentSerializer::serialize(doc);

    ParamDocument restored;
    QStringList warnings;
    DocumentSerializer::deserialize(restored, json, &warnings);
    QVERIFY(warnings.isEmpty());

    QCOMPARE(restored.groups().size(), size_t(1));
    const QUuid rgid = restored.groups().front().id;
    QCOMPARE(rgid, gid);
    QCOMPARE(restored.groups().front().name, QStringLiteral("前片"));
    QCOMPARE(restored.blocksInGroup(rgid).size(), 2);
    QCOMPARE(restored.groupOfBlock(a.blockId), rgid);
    QCOMPARE(restored.groupOfBlock(b.blockId), rgid);
}

// ─────────────────────────────────────────────────────────────────────────────
void TestGroup::serializeOrphanWarning()
{
    ParamDocument doc;
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    doc.createGroup({a.blockId, b.blockId});

    QJsonObject json = DocumentSerializer::serialize(doc);

    // Corrupt the file: drop block B — its membership becomes orphaned.
    QJsonObject docObj = json["document"].toObject();
    QJsonArray blocks = docObj["blocks"].toArray();
    QJsonArray filtered;
    for (const auto& v : blocks) {
        if (v.toObject()["id"].toString()
                != b.blockId.toString(QUuid::WithoutBraces))
            filtered.append(v);
    }
    docObj["blocks"] = filtered;
    json["document"] = docObj;

    ParamDocument restored;
    QStringList warnings;
    DocumentSerializer::deserialize(restored, json, &warnings);

    // Orphan membership reported; the group (1 valid member) dropped — both
    // degradations must surface (逐条报告, no silent degradation).
    QVERIFY(!warnings.isEmpty());
    QVERIFY(restored.groups().empty());
    QCOMPARE(restored.blocks().size(), size_t(1));
}

QTEST_MAIN(TestGroup)
#include "test_group.moc"
