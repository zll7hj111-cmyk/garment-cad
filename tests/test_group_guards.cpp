#include <QtTest>
#include <QUndoStack>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "canvas/GroupBadgeItem.h"
#include "tools/ToolSelect.h"
#include "parametric/ParamDocument.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::test::makeLine;
using cad::test::LineSetup;
using cad::test::layerIdAt;

namespace {

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

/// Tool-layer group guards (保护守卫): the selection tool treats a user group
/// as one atomic unit — a member cannot be deleted in isolation, and the
/// internal connections cannot be torn apart by the endpoint gesture.
class TestGroupGuards : public QObject
{
    Q_OBJECT

private slots:
    void delOnMemberDeletesWholeGroup();
    void internalConnectionCannotBeDetached();
    void badgeCreatedAndPositioned();
    void badgeClickEmitsSignal();
    void badgeAtGuard();
};

// ─────────────────────────────────────────────────────────────────────────────
// 组内单线不可删: picking ONE member expands the selection to the WHOLE group,
// so Del can only ever delete the complete group — a partial delete is
// unreachable by construction (and deleteSelectedBlocks guards it too).
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::delOnMemberDeletesWholeGroup()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    auto outsider = makeLine(doc, 100.0, Vec2{0.0, -100.0});
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    doc.resolveAll();

    QUndoStack stack;
    cad::tools::ToolSelect select;
    select.activate(scene, &doc);
    select.setUndoStack(&stack);

    // Request ONE member → the tool selects the WHOLE group (confirmed).
    select.selectBlocksExternally({a.blockId});
    QCOMPARE(select.state(), cad::tools::SelectState::Confirmed);

    // Del removes the entire group, never a single member; the outsider stays.
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    select.keyPress(&del);

    QCOMPARE(doc.blocks().size(), size_t(1));
    QVERIFY(doc.findBlock(outsider.blockId) != nullptr);
    QVERIFY(doc.groups().empty());
    QVERIFY(doc.groupOfBlock(a.blockId).isNull());
    (void)gid;

    // Undo restores both members AND the group record.
    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(3));
    QCOMPARE(doc.groups().size(), size_t(1));
    QCOMPARE(doc.groupOfBlock(a.blockId), gid);
    QCOMPARE(doc.groupOfBlock(b.blockId), gid);

    select.deactivate();
}

// ─────────────────────────────────────────────────────────────────────────────
// 组内连接拆不开: with the whole group selected (>= 2 blocks) the endpoint
// connect/detach gesture never fires, so the internal attachment survives a
// press-drag on the shared endpoint.
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::internalConnectionCannotBeDetached()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);                       // (0,0)→(100,0)
    auto b = makeLine(doc, 100.0);                       // B follows A's end
    QVERIFY(doc.addAttachment(makeFollowAtt(b, a)));     // joint at (100,0)
    doc.createGroup({a.blockId, b.blockId});
    doc.resolveAll();

    cad::tools::ToolSelect select;
    select.activate(scene, &doc);
    select.selectBlocksExternally({a.blockId, b.blockId});
    QCOMPARE(select.state(), cad::tools::SelectState::Confirmed);

    // Press right on the internal joint (user coords; the scene flips Y).
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(cad::geo::Coord::toScene(100.0, 0.0));
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    select.mousePress(&press);

    // The internal attachment must be intact — no quick-detach on groups.
    QCOMPARE(doc.attachments().size(), size_t(1));
    QCOMPARE(doc.attachments().front().fromBlockId, b.blockId);
    QCOMPARE(doc.attachments().front().toBlockId, a.blockId);
    QVERIFY(select.state() != cad::tools::SelectState::Connecting);

    select.deactivate();
}

// ─────────────────────────────────────────────────────────────────────────────
// 组徽标: one pill per group, anchored above the member union bounds.
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::badgeCreatedAndPositioned()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);                       // (0,0)→(100,0)
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});     // (0,-50)→(100,-50)
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    doc.resolveAll();
    scene.refreshAllBlockItems();

    GroupBadgeItem* badge = scene.groupBadge(gid);
    QVERIFY(badge != nullptr);
    QVERIFY(badge->isVisible());
    QVERIFY(!badge->boundingRect().isEmpty());

    // Position: above the top-left of the member union bounds.
    BlockItem* biA = scene.findBlockItem(a.blockId);
    BlockItem* biB = scene.findBlockItem(b.blockId);
    QVERIFY(biA && biB);
    const QRectF bounds = biA->sceneBoundingRect().united(biB->sceneBoundingRect());
    QCOMPARE(badge->pos().x(), bounds.left());
    QVERIFY(badge->pos().y() < bounds.top());

    // Dissolving removes the badge; recreating brings it back.
    doc.dissolveGroup(gid);
    QVERIFY(scene.groupBadge(gid) == nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// 点击徽标 → groupBadgeClicked(组 id)（宿主窗口据此选中整组）.
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::badgeClickEmitsSignal()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    doc.resolveAll();

    GroupBadgeItem* badge = scene.groupBadge(gid);
    QVERIFY(badge);
    QUuid clicked;
    QObject::connect(&scene, &CanvasScene::groupBadgeClicked,
                     &scene, [&](const QUuid& id) { clicked = id; });

    const QPointF center = badge->boundingRect().center();
    const QPointF sceneCenter = badge->pos() + center;
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setPos(center);
    press.setScenePos(sceneCenter);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    scene.sendEvent(badge, &press);
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setPos(center);
    release.setScenePos(sceneCenter);
    release.setButton(Qt::LeftButton);
    release.setButtons(Qt::NoButton);
    scene.sendEvent(badge, &release);

    QCOMPARE(clicked, gid);
}

// ─────────────────────────────────────────────────────────────────────────────
// 徽标命中守卫: ToolSelect::badgeAt 识别徽标位置，工具不会误操作其下的线段.
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::badgeAtGuard()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    doc.resolveAll();

    cad::tools::ToolSelect select;
    select.activate(scene, &doc);
    GroupBadgeItem* badge = scene.groupBadge(gid);
    QVERIFY(badge);

    // Badge center in USER coords (scene Y is flipped).
    const QPointF sceneCenter = badge->pos() + badge->boundingRect().center();
    const cad::geo::Vec2 user(sceneCenter.x(), -sceneCenter.y());
    QVERIFY(select.badgeAt(user));
    // Away from the badge → false.
    QVERIFY(!select.badgeAt(cad::geo::Vec2(500.0, 500.0)));

    select.deactivate();
}

QTEST_MAIN(TestGroupGuards)
#include "test_group_guards.moc"
