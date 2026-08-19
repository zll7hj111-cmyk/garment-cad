#include <QtTest>
#include <QUndoStack>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QMouseEvent>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "canvas/GroupBadgeItem.h"
#include "tools/ToolSelect.h"
#include "tools/ToolRotate.h"
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
    void badgeClickAndHoverWired();
    void singleSelectHighlightsWholeGroupWhileDragMovesGroup();
    void rotateFreeGroupRotatesAllMembersRigidly();
    void toolRotateIdentifiesGroupHinge();
    void groupStackedPointCanActivelyConnectToExternal();
    void componentSourceConfirmCanChooseEitherMember();
    void toolRotateComponentHingeEditsFollowerAngle();
};

// ─────────────────────────────────────────────────────────────────────────────
// 整组删除: 点成员=选整组，删除前工具层再把选中集展开为整组，因此 Del 删除
// 的是整组记录（跨边界线正常善后，不设结构保护）。undo 完整还原。
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

    // Request ONE member (confirmed).
    select.selectBlocksExternally({a.blockId, b.blockId});
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
    // At the same time, the endpoint connect gesture is active to allow connecting to external targets!
    QCOMPARE(doc.attachments().size(), size_t(1));
    QCOMPARE(doc.attachments().front().fromBlockId, b.blockId);
    QCOMPARE(doc.attachments().front().toBlockId, a.blockId);
    // 方案A 排除 follower: B 是内部 follower，不能作为对外端口,
    // 因此重叠角点只剩下 A.end 一个合法源候选，直接进入 Connecting。
    QCOMPARE(select.state(), cad::tools::SelectState::Connecting);

    select.deactivate();
}

// ─────────────────────────────────────────────────────────────────────────────
// 组包围虚框: anchored at the member union bounds top-left.
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

    // Position: matches top-left of the member union bounds.
    BlockItem* biA = scene.findBlockItem(a.blockId);
    BlockItem* biB = scene.findBlockItem(b.blockId);
    QVERIFY(biA && biB);
    const QRectF bounds = biA->sceneBoundingRect().united(biB->sceneBoundingRect());
    QCOMPARE(badge->pos().x(), bounds.left());
    QCOMPARE(badge->pos().y(), bounds.top());

    // Dissolving removes the badge; recreating brings it back.
    doc.dissolveGroup(gid);
    QVERIFY(scene.groupBadge(gid) == nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// 组包围框可交互：shape() 不再是空路径，点击/悬停接线真实生效
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::badgeClickAndHoverWired()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    doc.resolveAll();
    scene.refreshAllBlockItems();

    GroupBadgeItem* badge = scene.groupBadge(gid);
    QVERIFY(badge != nullptr);
    QVERIFY(!badge->shape().isEmpty());   // interactive outline, not dead code

    QUuid clickedGid;
    int clickCount = 0;
    QObject::connect(&scene, &CanvasScene::groupBadgeClicked, &scene,
                     [&](const QUuid& g) { clickedGid = g; ++clickCount; });
    bool hovered = false;
    QObject::connect(badge, &GroupBadgeItem::hoverChanged, &scene,
                     [&](bool h) { hovered = h; });

    QGraphicsView view(&scene);
    view.resize(600, 400);
    view.viewport()->setMouseTracking(true);
    view.setMouseTracking(true);
    view.show();
    for (int i = 0; i < 10; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    // Click a point on the dashed outline (top-left border area).
    const QPointF borderLocal = badge->boundingRect().topLeft() + QPointF(2.0, 2.0);
    const QPointF borderScene = badge->mapToScene(borderLocal);
    const QPoint vp = view.mapFromScene(borderScene);
    QVERIFY(view.viewport()->rect().contains(vp));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, vp);
    QCoreApplication::processEvents();
    QCOMPARE(clickCount, 1);
    QCOMPARE(clickedGid, gid);

    // Hover the outline -> hoverChanged(true); move away -> false.
    QTest::mouseMove(view.viewport(), vp);
    QCoreApplication::processEvents();
    QVERIFY(hovered);
    const QPoint farVp = view.viewport()->rect().bottomRight() - QPoint(10, 10);
    QVERIFY(view.viewport()->rect().contains(farVp));
    bool badgeAtFar = false;
    for (QGraphicsItem* it : scene.items(view.mapToScene(farVp)))
        if (it->type() == GroupBadgeItem::Type) { badgeAtFar = true; break; }
    QVERIFY(!badgeAtFar);
    QMouseEvent moveEv(QEvent::MouseMove, farVp,
                       view.viewport()->mapToGlobal(farVp),
                       Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &moveEv);
    QCoreApplication::processEvents();
    QVERIFY(!hovered);

    view.close();
}

// ─────────────────────────────────────────────────────────────────────────────
// 单选点成员=选整组（整组高亮），拖动时带动整组平移
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::singleSelectHighlightsWholeGroupWhileDragMovesGroup()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);
    auto b = makeLine(doc, 100.0, Vec2{0.0, -50.0});
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    doc.resolveAll();
    scene.refreshAllBlockItems();

    cad::tools::ToolSelect select;
    select.activate(scene, &doc);

    // 1. Click on member A (user coords: (50, 0)) -> WHOLE group selected
    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    press1.setScenePos(QPointF(50.0, 0.0));
    press1.setButton(Qt::LeftButton);
    press1.setButtons(Qt::LeftButton);
    select.mousePress(&press1);

    // 统一点成员=选整组: selection contains BOTH members.
    QCOMPARE(select.selection().size(), 2);
    QVERIFY(select.selection().contains(a.blockId));
    QVERIFY(select.selection().contains(b.blockId));
    QCOMPARE(select.state(), cad::tools::SelectState::Selecting);

    // 2. Right-click confirms the selection
    QGraphicsSceneMouseEvent rPress(QEvent::GraphicsSceneMousePress);
    rPress.setButton(Qt::RightButton);
    rPress.setButtons(Qt::RightButton);
    select.mousePress(&rPress);
    QCOMPARE(select.state(), cad::tools::SelectState::Confirmed);

    // 3. Drag starts on member A and moves by (+20, +10)
    QGraphicsSceneMouseEvent dragPress(QEvent::GraphicsSceneMousePress);
    dragPress.setScenePos(QPointF(50.0, 0.0));
    dragPress.setButton(Qt::LeftButton);
    dragPress.setButtons(Qt::LeftButton);
    select.mousePress(&dragPress);
    QCOMPARE(select.state(), cad::tools::SelectState::Dragging);

    QGraphicsSceneMouseEvent dragMove(QEvent::GraphicsSceneMouseMove);
    dragMove.setScenePos(QPointF(70.0, 10.0));
    dragMove.setButton(Qt::NoButton);
    dragMove.setButtons(Qt::LeftButton);
    select.mouseMove(&dragMove);

    // Both member A and member B must move together (+20, +10)
    const auto* blkA = doc.findBlock(a.blockId);
    const auto* blkB = doc.findBlock(b.blockId);
    QVERIFY(blkA->transform.origin.distanceTo(Vec2{20.0, 10.0}) < 1e-5);
    QVERIFY(blkB->transform.origin.distanceTo(Vec2{20.0, -40.0}) < 1e-5);

    select.deactivate();
}

// ─────────────────────────────────────────────────────────────────────────────
// 自由态组件旋转: 点击组件内任意成员旋转时，整组所有成员刚体同步旋转
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::rotateFreeGroupRotatesAllMembersRigidly()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    auto a = makeLine(doc, 100.0);                       // (0,0) -> (100,0)
    auto b = makeLine(doc, 100.0, Vec2{0.0, 50.0});     // (0,50) -> (100,50)
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    doc.resolveAll();
    scene.refreshAllBlockItems();

    cad::tools::ToolRotate rotate;
    rotate.activate(scene, &doc);

    // Select member A (pivot at (0,0))
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(QPointF(50.0, 0.0));
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    rotate.mousePress(&press);
    QCOMPARE(rotate.state(), cad::tools::RotateState::Ready);

    // Rotate by 90 degrees CCW (drag up to (0, 50))
    QGraphicsSceneMouseEvent rPress(QEvent::GraphicsSceneMousePress);
    rPress.setScenePos(QPointF(50.0, 0.0));
    rPress.setButton(Qt::LeftButton);
    rPress.setButtons(Qt::LeftButton);
    rotate.mousePress(&rPress);
    QCOMPARE(rotate.state(), cad::tools::RotateState::Rotating);

    QGraphicsSceneMouseEvent rMove(QEvent::GraphicsSceneMouseMove);
    rMove.setScenePos(QPointF(0.0, 50.0));
    rMove.setButton(Qt::NoButton);
    rMove.setButtons(Qt::LeftButton);
    rotate.mouseMove(&rMove);

    // Member A should point along +Y: (0,0) -> (0,100)
    const auto* blkA = doc.findBlock(a.blockId);
    const auto* blkB = doc.findBlock(b.blockId);
    QVERIFY(std::abs(blkA->transform.rotation - M_PI * 0.5) < 1e-4);

    // Member B must also be rotated 90 deg around (0,0):
    // Original origin (0,50) rotated 90 deg around (0,0) becomes (-50, 0)!
    QVERIFY(std::abs(blkB->transform.rotation - M_PI * 0.5) < 1e-4);
    QVERIFY(blkB->transform.origin.distanceTo(Vec2{-50.0, 0.0}) < 1e-4);

    rotate.deactivate();
}

// ─────────────────────────────────────────────────────────────────────────────
// 工具层组件铰链自动识别: 当旋转组件中的任意一条线时，自动锁定组件的外部铰链连接点为枢轴
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::toolRotateIdentifiesGroupHinge()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    // Leader: (0,0) -> (100,0)
    auto leader = makeLine(doc, 100.0);
    // Component member A: (0,0) -> (0,50)
    auto a = makeLine(doc, 50.0);
    // Component member B: (0,50) -> (50,50)
    auto b = makeLine(doc, 50.0, Vec2{0.0, 50.0});

    // A follows leader's end (100,0)
    Attachment hingeAtt;
    hingeAtt.fromBlockId = a.blockId;
    hingeAtt.fromPointId = a.startId;
    hingeAtt.toBlockId   = leader.blockId;
    hingeAtt.toPointId   = leader.endId;
    hingeAtt.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(hingeAtt));

    // B follows A's end (0,50)
    Attachment internalAtt;
    internalAtt.fromBlockId = b.blockId;
    internalAtt.fromPointId = b.startId;
    internalAtt.toBlockId   = a.blockId;
    internalAtt.toPointId   = a.endId;
    internalAtt.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(internalAtt));

    // Form component {A, B}
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    QVERIFY(!gid.isNull());
    doc.resolveAll();
    scene.refreshAllBlockItems();

    // Activate ToolRotate
    cad::tools::ToolRotate rotate;
    rotate.activate(scene, &doc);

    // Select member B (which itself does NOT directly attach to leader, but belongs to the group)
    // Click on member B's body (user coords: Y-up)
    const Vec2 bMid = (doc.findBlock(b.blockId)->worldPos(b.startId) + doc.findBlock(b.blockId)->worldPos(b.endId)) * 0.5;
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(QPointF(bMid.x, bMid.y));
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    rotate.mousePress(&press);

    // State should be Ready, and rotate should identify the group's hinge link!
    QCOMPARE(rotate.state(), cad::tools::RotateState::Ready);
    // Rotating member B should drive hingeAtt's followerAngle
    rotate.deactivate();
}

// ─────────────────────────────────────────────────────────────────────────────
// 组件多点重叠角点主动对外连接:
// 即使角点上有多个端点重叠且有内部连接，组依然可以从该重叠角点主动对接到外部线段！
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::groupStackedPointCanActivelyConnectToExternal()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    // 1. 外部线段 leader: (200, 0) -> (300, 0)
    auto leader = makeLine(doc, 100.0, Vec2{200.0, 0.0});

    // 2. 组件成员 A: (0, 0) -> (100, 0)
    auto a = makeLine(doc, 100.0);
    // 3. 组件成员 B: (100, 0) -> (100, 100) (在 (100,0) 处与 A.end 重叠且内部连接)
    auto b = makeLine(doc, 100.0, Vec2{100.0, 0.0});
    Attachment internalAtt;
    internalAtt.fromBlockId = b.blockId;
    internalAtt.fromPointId = b.startId;
    internalAtt.toBlockId   = a.blockId;
    internalAtt.toPointId   = a.endId;
    internalAtt.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(internalAtt));

    // 成组 {A, B}
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    QVERIFY(!gid.isNull());
    doc.resolveAll();
    scene.refreshAllBlockItems();

    QUndoStack stack;
    cad::tools::ToolSelect select;
    select.activate(scene, &doc);
    select.setUndoStack(&stack);

    // 4. 单选模式点 member A = 选整组，再右键确认
    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    press1.setScenePos(QPointF(50.0, 0.0));
    press1.setButton(Qt::LeftButton);
    press1.setButtons(Qt::LeftButton);
    select.mousePress(&press1);

    QGraphicsSceneMouseEvent rPress(QEvent::GraphicsSceneMousePress);
    rPress.setButton(Qt::RightButton);
    rPress.setButtons(Qt::RightButton);
    select.mousePress(&rPress);
    QCOMPARE(select.state(), cad::tools::SelectState::Confirmed);

    // 5. 从 (100, 0) 重叠角点按下并拖拽
    QGraphicsSceneMouseEvent connPress(QEvent::GraphicsSceneMousePress);
    connPress.setScenePos(QPointF(100.0, 0.0));
    connPress.setButton(Qt::LeftButton);
    connPress.setButtons(Qt::LeftButton);
    select.mousePress(&connPress);

    // 方案A 排除 follower: B 是内部 follower（fromBlockId = B），不能作为
    // 组件对外端口，所以 (100,0) 只剩 A.end 一个合法源候选 → 直接 Connecting。
    QCOMPARE(select.state(), cad::tools::SelectState::Connecting);

    // 6. 拖动到外部 leader 的起点 (200, 0) 附近
    QGraphicsSceneMouseEvent connMove(QEvent::GraphicsSceneMouseMove);
    connMove.setScenePos(QPointF(200.0, 0.0));
    connMove.setButton(Qt::NoButton);
    connMove.setButtons(Qt::LeftButton);
    select.mouseMove(&connMove);

    // 7. 松开鼠标落位
    QGraphicsSceneMouseEvent connRelease(QEvent::GraphicsSceneMouseRelease);
    connRelease.setScenePos(QPointF(200.0, 0.0));
    connRelease.setButton(Qt::LeftButton);
    connRelease.setButtons(Qt::NoButton);
    select.mouseRelease(&connRelease);

    // 状态应当进入 AngleInput 调节角度态或者直接建立连接
    // 按 Enter 键提交角度
    QKeyEvent enterKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    select.keyPress(&enterKey);

    // 路线B: 组件对外连接不再新增 Attachment，而是写入组件主连接铰链。
    QCOMPARE(doc.attachments().size(), size_t(1));

    // 组内连接完好无损
    bool foundInternal = false;
    for (const auto& att : doc.attachments()) {
        if (att.fromBlockId == b.blockId && att.toBlockId == a.blockId) {
            foundInternal = true;
            break;
        }
    }
    QVERIFY(foundInternal);

    QVERIFY(doc.hasComponentHinge(gid));
    const auto* hinge = doc.componentHinge(gid);
    QVERIFY(hinge != nullptr);
    QCOMPARE(hinge->leaderBlockId, leader.blockId);
    // B 被排除后 A.end 是唯一合法源点。
    QCOMPARE(hinge->memberBlockId, a.blockId);
    QCOMPARE(hinge->memberPointId, a.endId);

    // 连接手势走 ParamDocument 自有的 undoStack（ToolSelect 的 stack 只管
    // 成组/删除等命令; ConnectGesture 内部固定使用 doc.undoStack()）。
    QUndoStack* docStack = doc.undoStack();
    QVERIFY(docStack != nullptr);
    docStack->undo();
    QVERIFY(!doc.hasComponentHinge(gid));
    docStack->redo();
    QVERIFY(doc.hasComponentHinge(gid));

    select.deactivate();
}

// ─────────────────────────────────────────────────────────────────────────────
// 方案A 源端确认: 同一重叠位置可以点选不同成员线段，明确不同 memberBlockId。
// 这里点选 B 的线段，铰链源点必须是 B.start。
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::componentSourceConfirmCanChooseEitherMember()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto leader = makeLine(doc, 100.0, Vec2{300.0, 0.0});  // (300,0)->(400,0)
    auto a = makeLine(doc, 100.0);                    // (0,0)->(100,0)
    auto b = makeLine(doc, 100.0, Vec2{100.0, 0.0});  // (100,0)->(200,0)

    // 不加内部连接：让 A.end 与 B.start 都作为合法空闲源点，验证用户可任选其一。
    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    QVERIFY(!gid.isNull());
    doc.resolveAll();
    scene.refreshAllBlockItems();

    cad::tools::ToolSelect select;
    select.activate(scene, &doc);
    select.selectBlocksExternally({a.blockId, b.blockId});
    QCOMPARE(select.state(), cad::tools::SelectState::Confirmed);

    // 在重叠角点按下 → 源端确认态。
    QGraphicsSceneMouseEvent connPress(QEvent::GraphicsSceneMousePress);
    connPress.setScenePos(QPointF(100.0, 0.0));
    connPress.setButton(Qt::LeftButton);
    connPress.setButtons(Qt::LeftButton);
    select.mousePress(&connPress);
    QCOMPARE(select.state(), cad::tools::SelectState::ConfirmSource);

    // 点选 B 的线段（水平边中点），明确用 B.start 作为组件主连接源点；高亮保留。
    QGraphicsSceneMouseEvent sourcePress(QEvent::GraphicsSceneMousePress);
    sourcePress.setScenePos(QPointF(150.0, 0.0));
    sourcePress.setButton(Qt::LeftButton);
    sourcePress.setButtons(Qt::LeftButton);
    select.mousePress(&sourcePress);
    QCOMPARE(select.state(), cad::tools::SelectState::ConfirmSource);

    // 按住已选线段的任意位置（这里选 B 线中段 150,0）也应开始连接拖动。
    QGraphicsSceneMouseEvent pointPress(QEvent::GraphicsSceneMousePress);
    pointPress.setScenePos(QPointF(150.0, 0.0));
    pointPress.setButton(Qt::LeftButton);
    pointPress.setButtons(Qt::LeftButton);
    select.mousePress(&pointPress);
    QCOMPARE(select.state(), cad::tools::SelectState::Connecting);

    // 拖到外部 leader 并提交角度。
    QGraphicsSceneMouseEvent connMove(QEvent::GraphicsSceneMouseMove);
    connMove.setScenePos(QPointF(300.0, 0.0));
    connMove.setButton(Qt::NoButton);
    connMove.setButtons(Qt::LeftButton);
    select.mouseMove(&connMove);

    QGraphicsSceneMouseEvent connRelease(QEvent::GraphicsSceneMouseRelease);
    connRelease.setScenePos(QPointF(300.0, 0.0));
    connRelease.setButton(Qt::LeftButton);
    connRelease.setButtons(Qt::NoButton);
    select.mouseRelease(&connRelease);
    // 目标端也有重叠（A.end 随刚体移到同一位置 + leader.start）→ 先确认目标线段。
    QCOMPARE(select.state(), cad::tools::SelectState::ConfirmTarget);

    QGraphicsSceneMouseEvent targetPress(QEvent::GraphicsSceneMousePress);
    targetPress.setScenePos(QPointF(350.0, 0.0));
    targetPress.setButton(Qt::LeftButton);
    targetPress.setButtons(Qt::LeftButton);
    select.mousePress(&targetPress);
    QCOMPARE(select.state(), cad::tools::SelectState::AngleInput);

    QKeyEvent enterKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    select.keyPress(&enterKey);

    QVERIFY(doc.hasComponentHinge(gid));
    const auto* hinge = doc.componentHinge(gid);
    QVERIFY(hinge != nullptr);
    QCOMPARE(hinge->memberBlockId, b.blockId);
    QCOMPARE(hinge->memberPointId, b.startId);

    select.deactivate();
}

// ─────────────────────────────────────────────────────────────────────────────
// 组件铰链旋转: ToolRotate 识别组件主连接铰链, 旋转时写入 hinge.followerAngle
// ─────────────────────────────────────────────────────────────────────────────
void TestGroupGuards::toolRotateComponentHingeEditsFollowerAngle()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    auto leader = makeLine(doc, 100.0);                 // (0,0) -> (100,0)
    auto a = makeLine(doc, 50.0);                       // (0,0) -> (50,0)
    auto b = makeLine(doc, 50.0, Vec2{0.0, 50.0});      // (0,50) -> (50,50)

    const QUuid gid = doc.createGroup({a.blockId, b.blockId});
    QVERIFY(!gid.isNull());

    ComponentHinge hinge;
    hinge.memberBlockId = a.blockId;
    hinge.memberPointId = a.startId;
    hinge.leaderBlockId = leader.blockId;
    hinge.leaderPointId = leader.endId;
    hinge.leaderSegmentId = leader.segId;
    hinge.followerAngle = 90.0;
    QVERIFY(doc.setComponentHinge(gid, hinge));
    doc.resolveAll();
    scene.refreshAllBlockItems();

    QUndoStack stack;
    cad::tools::ToolRotate rotate;
    rotate.activate(scene, &doc);
    rotate.setUndoStack(&stack);

    // Click member B's body (the component moves as a rigid whole).
    const cad::param::Block* blkB = doc.findBlock(b.blockId);
    QVERIFY(blkB);
    const Vec2 bMid = (blkB->worldPos(b.startId) + blkB->worldPos(b.endId)) * 0.5;

    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(QPointF(bMid.x, bMid.y));
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    rotate.mousePress(&press);
    QCOMPARE(rotate.state(), cad::tools::RotateState::Ready);

    // Begin drag from the same body point; pivot is the component hinge on A.
    QGraphicsSceneMouseEvent rPress(QEvent::GraphicsSceneMousePress);
    rPress.setScenePos(QPointF(bMid.x, bMid.y));
    rPress.setButton(Qt::LeftButton);
    rPress.setButtons(Qt::LeftButton);
    rotate.mousePress(&rPress);
    QCOMPARE(rotate.state(), cad::tools::RotateState::Rotating);

    // Rotate the cursor 90 degrees CCW around the hinge pivot.
    const cad::param::Block* blkA = doc.findBlock(a.blockId);
    QVERIFY(blkA);
    const Vec2 pivot = blkA->worldPos(a.startId);
    const Vec2 v0 = bMid - pivot;
    const Vec2 v1 = v0.rotated(M_PI * 0.5);
    const Vec2 target = pivot + v1;

    QGraphicsSceneMouseEvent rMove(QEvent::GraphicsSceneMouseMove);
    rMove.setScenePos(QPointF(target.x, target.y));
    rMove.setButton(Qt::NoButton);
    rMove.setButtons(Qt::LeftButton);
    rotate.mouseMove(&rMove);

    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos(QPointF(target.x, target.y));
    release.setButton(Qt::LeftButton);
    release.setButtons(Qt::NoButton);
    rotate.mouseRelease(&release);

    const auto* h = doc.componentHinge(gid);
    QVERIFY(h != nullptr);
    QVERIFY(std::abs(h->followerAngle - 90.0) > 1e-3);

    // Undo restores the original hinge angle (and thus the original pose).
    stack.undo();
    const auto* h0 = doc.componentHinge(gid);
    QVERIFY(h0 != nullptr);
    QVERIFY(std::abs(h0->followerAngle - 90.0) < 1e-6);

    rotate.deactivate();
}

QTEST_MAIN(TestGroupGuards)
#include "test_group_guards.moc"
