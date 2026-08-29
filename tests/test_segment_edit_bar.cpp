/// SegmentEditBar (创建后内嵌编辑条): 智能笔创建线段后, 状态栏编辑条
/// 替代旧的创建弹窗。覆盖: 填充(ID/名称/长度/角度)、名称/长度/角度
/// 编辑实时生效、跟随线角度编辑、桥接线只读、Esc → cancelRequested。

#include <QtTest>
#include <QApplication>
#include <QLineEdit>
#include <QLabel>
#include <QKeyEvent>
#include <QUndoStack>

#include "app/SegmentEditBar.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/Attachment.h"
#include "parametric/Serial.h"
#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "TestHelpers.h"

using namespace cad::param;
using namespace cad::app;

namespace {

/// One block with a single line (origin at start, end at +X).
struct LineRef {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

/// One block with TWO lines sharing the start point (seg1: p0→p1, seg2: p0→p2).
struct TwoLineRef {
    QUuid blockId;
    QUuid p0, p1, p2;
    QUuid seg1Id, seg2Id;
};

/// Block with two segments sharing their start point (p1 at 0°, p2 at 90°).
TwoLineRef makeTwoSegmentBlock(ParamDocument& doc)
{
    doc.setActiveLayer(cad::test::layerIdAt(doc, 1));
    Block block;
    block.transform.origin = cad::geo::Vec2::zero();
    ParamPoint p0;
    p0.constraint = PointConstraint::Free;
    p0.freePos = cad::geo::Vec2::zero();
    ParamPoint p1;
    p1.constraint = PointConstraint::Polar;
    p1.refPointId = p0.id;
    p1.distance = 100.0;
    p1.angle = 0.0;
    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = p0.id;
    p2.distance = 100.0;
    p2.angle = 90.0;
    Segment s1;
    s1.startPointId = p0.id;
    s1.endPointId = p1.id;
    Segment s2;
    s2.startPointId = p0.id;
    s2.endPointId = p2.id;
    const QUuid p0id = p0.id, p1id = p1.id, p2id = p2.id;
    const QUuid s1id = s1.id, s2id = s2.id;
    block.addPoint(std::move(p0));
    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));
    block.addSegment(std::move(s1));
    block.addSegment(std::move(s2));
    const QUuid bid = doc.addBlock(std::move(block));
    doc.resolveAll();
    return {bid, p0id, p1id, p2id, s1id, s2id};
}

LineRef makeLine(ParamDocument& doc, double lenMm = 100.0, double layer = 1)
{
    doc.setActiveLayer(cad::test::layerIdAt(doc, static_cast<int>(layer)));
    Block block;
    block.transform.origin = cad::geo::Vec2::zero();
    ParamPoint sp;
    sp.constraint = PointConstraint::Free;
    sp.freePos = cad::geo::Vec2::zero();
    ParamPoint ep;
    ep.constraint = PointConstraint::Polar;
    ep.refPointId = sp.id;
    ep.distance = lenMm;
    ep.angle = 0.0;
    Segment seg;
    seg.startPointId = sp.id;
    seg.endPointId = ep.id;
    block.addPoint(std::move(sp));
    block.addPoint(std::move(ep));
    block.addSegment(std::move(seg));
    const QUuid bid = doc.addBlock(std::move(block));
    doc.resolveAll();
    const auto* b = doc.findBlock(bid);
    return {bid, b->segments.front().startPointId,
            b->segments.front().endPointId, b->segments.front().id};
}

} // namespace

class TestSegmentEditBar : public QObject
{
    Q_OBJECT

private slots:
    void fillPopulatesFields();
    void nameEditApplies();
    void lengthEditApplies();
    void angleEditApplies();
    void followerAngleEditApplies();
    void multiLineFollowerAttachedToOtherLine();
    void multiLineUndoRestoresOwnLine();
    void bridgeLineReadOnly();
    void escEmitsCancel();
};

void TestSegmentEditBar::fillPopulatesFields()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);

    SegmentEditBar bar(&doc);
    bar.showForLine(l.blockId, l.segId);

    const auto* b = doc.findBlock(l.blockId);
    const auto* seg = b->findSegment(l.segId);
    QVERIFY(seg);
    // ID shows the human-friendly tag only (L1), never the random prefix.
    auto* idLabel = bar.findChild<QLabel*>();
    QVERIFY(idLabel);
    QCOMPARE(idLabel->text(), cad::param::Serial::tag(seg->serial));
    QVERIFY(!idLabel->text().contains("k") && !idLabel->text().contains("a"));

    auto* nameEdit = bar.findChild<QLineEdit*>(QStringLiteral("nameEdit"));
    QVERIFY(nameEdit);
    QCOMPARE(nameEdit->text(), seg->name);  // empty by default
    QVERIFY(bar.isVisible());
}

void TestSegmentEditBar::nameEditApplies()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);

    SegmentEditBar bar(&doc);
    bar.showForLine(l.blockId, l.segId);
    auto* nameEdit = bar.findChild<QLineEdit*>(QStringLiteral("nameEdit"));
    QVERIFY(nameEdit);

    nameEdit->setText(QString::fromUtf8("肩线"));
    nameEdit->setFocus();
    // 断言"值变了"(名称写入模型): 谓词 = 该断言本身。
    QVERIFY2(cad::test::waitUntil([&] {
        const auto* bb = doc.findBlock(l.blockId);
        return bb && bb->findSegment(l.segId)
               && bb->findSegment(l.segId)->name == QString::fromUtf8("肩线");
    }), "名称应经 SegmentEditBar 应用到模型");

    const auto* b = doc.findBlock(l.blockId);
    QCOMPARE(b->findSegment(l.segId)->name, QString::fromUtf8("肩线"));
}

void TestSegmentEditBar::lengthEditApplies()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc, /*lenMm=*/100.0);

    SegmentEditBar bar(&doc);
    bar.showForLine(l.blockId, l.segId);

    // Numeric edit → endpoint distance updates (cm input → mm storage).
    auto* lenEdit = bar.findChild<QLineEdit*>(QStringLiteral("lenEdit"));
    QVERIFY(lenEdit);
    lenEdit->setText(QStringLiteral("25"));
    emit lenEdit->editingFinished();
    // P2-3: wait for the applied value instead of sleeping 10 ms — the write
    // goes through a command + resolve, which is not guaranteed to have
    // finished when editingFinished() returns.
    const QUuid blockId = l.blockId;
    const QUuid endId   = l.endId;
    QVERIFY2(cad::test::waitUntil([&] {
                 auto* bb = doc.findBlock(blockId);
                 auto* e  = bb ? bb->findPoint(endId) : nullptr;
                 return e && std::abs(e->distance - cad::geo::Units::cmToMm(25.0)) < 1e-9;
             }),
             "timed out waiting for the length edit to reach the model");

    auto* b = doc.findBlock(l.blockId);
    const auto* ep = b->findPoint(l.endId);
    QVERIFY(ep);
    QVERIFY(std::abs(ep->distance - cad::geo::Units::cmToMm(25.0)) < 1e-9);
    QVERIFY(ep->distanceFormula.isEmpty());
}

void TestSegmentEditBar::angleEditApplies()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);

    SegmentEditBar bar(&doc);
    bar.showForLine(l.blockId, l.segId);

    auto* angleEdit = bar.findChild<QLineEdit*>(QStringLiteral("angleEdit"));
    QVERIFY(angleEdit);
    angleEdit->setText(QStringLiteral("90"));
    emit angleEdit->editingFinished();
    // P2-3: condition wait instead of a 10 ms sleep (see lengthEditApplies).
    const QUuid blockId = l.blockId;
    const QUuid endId   = l.endId;
    QVERIFY2(cad::test::waitUntil([&] {
                 auto* bb = doc.findBlock(blockId);
                 auto* e  = bb ? bb->findPoint(endId) : nullptr;
                 return e && std::abs(e->angle - 90.0) < 1e-9;
             }),
             "timed out waiting for the angle edit to reach the model");

    auto* b = doc.findBlock(l.blockId);
    const auto* ep = b->findPoint(l.endId);
    QVERIFY(ep);
    // Free line: stored angle = world angle - block rotation (0 here).
    QVERIFY(std::abs(ep->angle - 90.0) < 1e-9);
}

void TestSegmentEditBar::followerAngleEditApplies()
{
    ParamDocument doc;
    const LineRef leader = makeLine(doc);
    const LineRef follower = makeLine(doc, /*lenMm=*/60.0);

    Attachment att;
    att.fromBlockId = follower.blockId;
    att.fromPointId = follower.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    SegmentEditBar bar(&doc);
    bar.showForLine(follower.blockId, follower.segId);

    auto* angleEdit = bar.findChild<QLineEdit*>(QStringLiteral("angleEdit"));
    QVERIFY(angleEdit);
    angleEdit->setText(QStringLiteral("45"));
    emit angleEdit->editingFinished();
    // P2-3: condition wait instead of a 10 ms sleep.
    QVERIFY2(cad::test::waitUntil([&] {
                 for (const auto& x : doc.attachments())
                     if (x.fromBlockId == follower.blockId
                         && std::abs(x.followerAngle - 45.0) < 1e-9)
                         return true;
                 return false;
             }),
             "timed out waiting for the follower angle edit to reach the model");

    const Attachment* a = nullptr;
    for (const auto& x : doc.attachments())
        if (x.fromBlockId == follower.blockId) { a = &x; break; }
    QVERIFY(a);
    QVERIFY(std::abs(a->followerAngle - 45.0) < 1e-9);
}

// 块带两条线段、follower 只锚在 seg1 端点时：编辑 seg2 的角度必须走自由线
// 分支（改 seg2 终点 Polar 角），绝不误改 seg1 的跟随角（旧实现按块取第一个
// attachment，会把 seg2 的角度写进 seg1 的 followerAngle）。
void TestSegmentEditBar::multiLineFollowerAttachedToOtherLine()
{
    ParamDocument doc;
    const LineRef leader = makeLine(doc);
    const TwoLineRef a = makeTwoSegmentBlock(doc);

    // Follower attachment anchored at seg1's end point (p1).
    Attachment att;
    att.fromBlockId = a.blockId;
    att.fromPointId = a.p1;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.followerAngle = 180.0;   // 闭合基准: 180° = 沿 leader 出口方向直行
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    // Edit the OTHER line (seg2: p0→p2) — must not touch the attachment.
    SegmentEditBar bar(&doc);
    bar.showForLine(a.blockId, a.seg2Id);
    auto* angleEdit = bar.findChild<QLineEdit*>(QStringLiteral("angleEdit"));
    QVERIFY(angleEdit);
    angleEdit->setText(QStringLiteral("30"));
    emit angleEdit->editingFinished();
    // P2-3: this test asserts that NOTHING happened (editing seg2 must not
    // disturb the attachment on seg1), so there is no state to wait FOR —
    // settle() drains the event loop instead of guessing a duration.
    cad::test::settle();

    const Attachment* a2 = nullptr;
    for (const auto& x : doc.attachments())
        if (x.fromBlockId == a.blockId) { a2 = &x; break; }
    QVERIFY(a2);
    QVERIFY(std::abs(a2->followerAngle - 180.0) < 1e-9);  // untouched
    auto* b = doc.findBlock(a.blockId);
    const auto* ep2 = b->findPoint(a.p2);
    QVERIFY(ep2);
    QVERIFY(std::abs(ep2->angle - 30.0) < 1e-9);  // free-line branch

    // Editing seg1 DOES drive the attachment (it anchors on seg1's endpoint).
    bar.showForLine(a.blockId, a.seg1Id);
    angleEdit->setText(QStringLiteral("45"));
    emit angleEdit->editingFinished();
    // P2-3: condition wait instead of a 10 ms sleep.
    QVERIFY2(cad::test::waitUntil([&] {
                 for (const auto& x : doc.attachments())
                     if (x.fromBlockId == a.blockId
                         && std::abs(x.followerAngle - 45.0) < 1e-9)
                         return true;
                 return false;
             }),
             "timed out waiting for the re-edited follower angle to apply");

    for (const auto& x : doc.attachments())
        if (x.fromBlockId == a.blockId) { a2 = &x; break; }
    QVERIFY(std::abs(a2->followerAngle - 45.0) < 1e-9);
}

// undo 一致性：编辑 seg2 角度入栈后撤销，seg2 的角度恢复、seg1 的
// attachment 不受影响（命令的旧态快照也按端点匹配，不会把 attachment
// 快照进 seg2 的编辑命令）。
void TestSegmentEditBar::multiLineUndoRestoresOwnLine()
{
    ParamDocument doc;
    const LineRef leader = makeLine(doc);
    const TwoLineRef a = makeTwoSegmentBlock(doc);

    Attachment att;
    att.fromBlockId = a.blockId;
    att.fromPointId = a.p1;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    QUndoStack stack;
    SegmentEditBar bar(&doc);
    bar.setUndoStack(&stack);
    bar.showForLine(a.blockId, a.seg2Id);
    auto* angleEdit = bar.findChild<QLineEdit*>(QStringLiteral("angleEdit"));
    QVERIFY(angleEdit);
    angleEdit->setText(QStringLiteral("30"));
    emit angleEdit->editingFinished();
    // editingFinished 直发同步应用; 中间态未断言、无谓词可取, 暂留排空。
    QTest::qWait(10);

    stack.undo();
    // 断言"值变了"(undo 恢复构建角): 谓词 = 该断言本身。
    QVERIFY2(cad::test::waitUntil([&] {
        const auto* bb = doc.findBlock(a.blockId);
        return bb && bb->findPoint(a.p2)
               && std::abs(bb->findPoint(a.p2)->angle - 90.0) < 1e-9;
    }), "undo 应恢复构建角 90°");

    auto* b = doc.findBlock(a.blockId);
    const auto* ep2 = b->findPoint(a.p2);
    QVERIFY(ep2);
    QVERIFY(std::abs(ep2->angle - 90.0) < 1e-9);  // back to construction angle
    for (const auto& x : doc.attachments())
        if (x.fromBlockId == a.blockId)
            QVERIFY(std::abs(x.followerAngle - 0.0) < 1e-9);
}

void TestSegmentEditBar::bridgeLineReadOnly()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);
    // Simulate a bridge/measure line: length driven by a formula.
    auto* b = doc.findBlock(l.blockId);
    auto* seg = b->findSegment(l.segId);
    seg->lengthFormula = QStringLiteral("M_1");
    if (auto* ep = b->findPoint(l.endId))
        ep->distanceFormula = QStringLiteral("M_1");

    SegmentEditBar bar(&doc);
    bar.showForLine(l.blockId, l.segId);

    auto* lenEdit = bar.findChild<QLineEdit*>(QStringLiteral("lenEdit"));
    QVERIFY(lenEdit);
    QVERIFY2(lenEdit->isReadOnly(), "bridge length must be read-only");
    auto* angleEdit = bar.findChild<QLineEdit*>(QStringLiteral("angleEdit"));
    QVERIFY(angleEdit);
    QVERIFY2(angleEdit->isReadOnly(), "bridge angle must be read-only");
    // Name stays editable.
    auto* nameEdit = bar.findChild<QLineEdit*>(QStringLiteral("nameEdit"));
    QVERIFY(nameEdit);
    QVERIFY(!nameEdit->isReadOnly());
}

void TestSegmentEditBar::escEmitsCancel()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);

    SegmentEditBar bar(&doc);
    bar.showForLine(l.blockId, l.segId);

    auto* nameEdit = bar.findChild<QLineEdit*>(QStringLiteral("nameEdit"));
    QVERIFY(nameEdit);
    nameEdit->setFocus();

    int cancelCount = 0;
    connect(&bar, &SegmentEditBar::cancelRequested, &bar, [&cancelCount] {
        ++cancelCount;
    });

    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(nameEdit, &esc);
    QCOMPARE(cancelCount, 1);
}

QTEST_MAIN(TestSegmentEditBar)
#include "test_segment_edit_bar.moc"
