/// @file test_context_strip.cpp
/// 上下文属性条 ContextStrip (CONTEXT_STRIP_DESIGN.md 一期): 取代原来的
/// SegmentEditBar + SmartPenPreInputBar 两条互斥 bar。覆盖: 填充(ID/名称/
/// 长度/角度)、名称/长度/角度编辑实时生效、跟随线角度编辑、多线块附件匹配、
/// undo 一致性、桥接线只读、Esc → cancelRequested(创建后)、输入包含
/// (ShortcutOverride) 与 Tab/Enter 流转。

#include <QtTest>
#include <QApplication>
#include <QLineEdit>
#include <QLabel>
#include <QKeyEvent>
#include <QUndoStack>

#include "app/ContextStrip.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
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

class TestContextStrip : public QObject
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
    void escEmitsCancelOnCreation();
    void escUnpinsPlainSelection();
    void shortcutOverrideContainment();
    void tabEnterKeyNavigation();
    void typingShortcutLettersWorks();
    // ── 只读悬停 (三期: 测量/角度测量/交点/打断 扫过即看) ──
    void hoverShowsReadOnlyPreview();
    // ── 连接角度会话 (二期: 连接手势的角度输入并入条带, AngleHud 退场) ──
    void connectSessionShowsFollowerOnlyAngleEditable();
    void connectSessionTextForwardsSignal();
    void connectSessionEnterCommitsEscCancels();
    void connectSessionModeToggleEmitsSignal();
    void connectSessionValidSetsProperty();
    void connectSessionEndRestoresNormalPin();
    // ── 基准读数串号 tag + 换向翻转 (2026-12: 修复"换向部分恒显 P1/P2"bug) ──
    void basisShowsSerialTagsAndReverseFlips();
    // ── 旋转会话锚心 (2026-12: 旋转工具换向 = 切换锚心) ──
    void rotateAnchorStateFlipsBasisAndRoutesReverse();
};

void TestContextStrip::fillPopulatesFields()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(l.blockId, l.segId);

    const auto* b = doc.findBlock(l.blockId);
    const auto* seg = b->findSegment(l.segId);
    QVERIFY(seg);
    // ID shows the human-friendly tag only (L1), never the random prefix.
    auto* idLabel = strip.findChild<QLabel*>(QStringLiteral("serialBadge"));
    QVERIFY(idLabel);
    QCOMPARE(idLabel->text(), cad::param::Serial::tag(seg->serial));
    QVERIFY(!idLabel->text().contains("k") && !idLabel->text().contains("a"));

    QCOMPARE(strip.nameEdit()->text(), seg->name);  // empty by default
    QVERIFY(strip.isVisible());
    // 锁定态 = 可编辑 (非桥线)。
    QVERIFY(!strip.nameEdit()->isReadOnly());
    QVERIFY(!strip.lengthEdit()->isReadOnly());
    QVERIFY(!strip.angleEdit()->isReadOnly());
}

void TestContextStrip::nameEditApplies()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(l.blockId, l.segId);

    strip.nameEdit()->setText(QString::fromUtf8("肩线"));
    strip.nameEdit()->setFocus();
    // 断言"值变了"(名称写入模型): 谓词 = 该断言本身。
    QVERIFY2(cad::test::waitUntil([&] {
        const auto* bb = doc.findBlock(l.blockId);
        return bb && bb->findSegment(l.segId)
               && bb->findSegment(l.segId)->name == QString::fromUtf8("肩线");
    }), "名称应经 ContextStrip 应用到模型");

    const auto* b = doc.findBlock(l.blockId);
    QCOMPARE(b->findSegment(l.segId)->name, QString::fromUtf8("肩线"));
}

void TestContextStrip::lengthEditApplies()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc, /*lenMm=*/100.0);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(l.blockId, l.segId);

    // Numeric edit → endpoint distance updates (cm input → mm storage).
    strip.lengthEdit()->setText(QStringLiteral("25"));
    emit strip.lengthEdit()->editingFinished();
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

void TestContextStrip::angleEditApplies()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(l.blockId, l.segId);

    strip.angleEdit()->setText(QStringLiteral("90"));
    emit strip.angleEdit()->editingFinished();
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

void TestContextStrip::followerAngleEditApplies()
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

    ContextStrip strip(&doc);
    strip.setPinnedTarget(follower.blockId, follower.segId);

    strip.angleEdit()->setText(QStringLiteral("45"));
    emit strip.angleEdit()->editingFinished();
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
void TestContextStrip::multiLineFollowerAttachedToOtherLine()
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
    ContextStrip strip(&doc);
    strip.setPinnedTarget(a.blockId, a.seg2Id);
    strip.angleEdit()->setText(QStringLiteral("30"));
    emit strip.angleEdit()->editingFinished();
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
    strip.setPinnedTarget(a.blockId, a.seg1Id);
    strip.angleEdit()->setText(QStringLiteral("45"));
    emit strip.angleEdit()->editingFinished();
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
void TestContextStrip::multiLineUndoRestoresOwnLine()
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
    ContextStrip strip(&doc);
    strip.setUndoStack(&stack);
    strip.setPinnedTarget(a.blockId, a.seg2Id);
    strip.angleEdit()->setText(QStringLiteral("30"));
    emit strip.angleEdit()->editingFinished();
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

void TestContextStrip::bridgeLineReadOnly()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);
    // Simulate a bridge/measure line: length driven by a formula.
    auto* b = doc.findBlock(l.blockId);
    auto* seg = b->findSegment(l.segId);
    seg->lengthFormula = QStringLiteral("M_1");
    if (auto* ep = b->findPoint(l.endId))
        ep->distanceFormula = QStringLiteral("M_1");

    ContextStrip strip(&doc);
    strip.setPinnedTarget(l.blockId, l.segId);

    QVERIFY2(strip.lengthEdit()->isReadOnly(), "bridge length must be read-only");
    QVERIFY2(strip.angleEdit()->isReadOnly(), "bridge angle must be read-only");
    // Name stays editable.
    QVERIFY(!strip.nameEdit()->isReadOnly());
}

void TestContextStrip::escEmitsCancelOnCreation()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);

    ContextStrip strip(&doc);
    strip.pinCreatedLine(l.blockId, l.segId);

    int cancelCount = 0;
    connect(&strip, &ContextStrip::cancelRequested, &strip, [&cancelCount] {
        ++cancelCount;
    });

    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(strip.nameEdit(), &esc);
    QCOMPARE(cancelCount, 1);
}

void TestContextStrip::escUnpinsPlainSelection()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(l.blockId, l.segId);
    QCOMPARE(strip.focusState(), StripFocus::Pinned);

    int cancelCount = 0;
    connect(&strip, &ContextStrip::cancelRequested, &strip, [&cancelCount] {
        ++cancelCount;
    });

    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(strip.nameEdit(), &esc);
    // 普通锁定 (非创建) Esc = 解除锁定, 不发 cancelRequested。
    QCOMPARE(cancelCount, 0);
    QVERIFY(strip.blockId().isNull());
}

void TestContextStrip::shortcutOverrideContainment()
{
    // 无文档也可挂事件过滤器 —— 只验证输入包含 (同旧 SmartPenPreInputBar
    // 测试范式; ContextStrip 的 eventFilter 不依赖模型)。
    ContextStrip strip(nullptr);
    strip.show();
    strip.nameEdit()->setFocus();

    const QVector<int> shortcutKeys = {
        Qt::Key_V, Qt::Key_L, Qt::Key_C, Qt::Key_R, Qt::Key_B,
        Qt::Key_I, Qt::Key_A, Qt::Key_H, Qt::Key_W, Qt::Key_N, Qt::Key_M
    };

    for (int key : shortcutKeys) {
        QKeyEvent scEvent(QEvent::ShortcutOverride, key, Qt::NoModifier);
        scEvent.ignore();
        QCoreApplication::sendEvent(strip.nameEdit(), &scEvent);
        QVERIFY2(scEvent.isAccepted(),
                 QString("ShortcutOverride for key %1 must be accepted").arg(key).toUtf8().constData());
    }

    // Also with Ctrl modifier (Ctrl+Z, Ctrl+Y, Ctrl+D, Ctrl+S, etc.):
    const QVector<int> ctrlKeys = { Qt::Key_Z, Qt::Key_Y, Qt::Key_D, Qt::Key_S, Qt::Key_1, Qt::Key_2 };
    for (int key : ctrlKeys) {
        QKeyEvent scEvent(QEvent::ShortcutOverride, key, Qt::ControlModifier);
        scEvent.ignore();
        QCoreApplication::sendEvent(strip.lengthEdit(), &scEvent);
        QVERIFY2(scEvent.isAccepted(), QString("ShortcutOverride for Ctrl+%1 must be accepted").arg(key).toUtf8().constData());
    }
}

void TestContextStrip::tabEnterKeyNavigation()
{
    QWidget dummyCanvas;
    ContextStrip strip(nullptr);
    strip.setCanvasView(&dummyCanvas);
    strip.show();

    // Focus first field:
    strip.nameEdit()->setFocus();
    QCOMPARE(strip.focusWidget(), strip.nameEdit());

    // Tab -> lengthEdit
    QKeyEvent tab1(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(strip.nameEdit(), &tab1);
    QCOMPARE(strip.focusWidget(), strip.lengthEdit());

    // Tab -> angleEdit
    QKeyEvent tab2(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(strip.lengthEdit(), &tab2);
    QCOMPARE(strip.focusWidget(), strip.angleEdit());

    // Tab -> nameEdit (cycle)
    QKeyEvent tab3(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(strip.angleEdit(), &tab3);
    QCOMPARE(strip.focusWidget(), strip.nameEdit());

    // Backtab -> angleEdit
    QKeyEvent btab1(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
    QCoreApplication::sendEvent(strip.nameEdit(), &btab1);
    QCOMPARE(strip.focusWidget(), strip.angleEdit());

    // Return in nameEdit -> lengthEdit
    strip.nameEdit()->setFocus();
    QKeyEvent ret1(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(strip.nameEdit(), &ret1);
    QCOMPARE(strip.focusWidget(), strip.lengthEdit());

    // Return in lengthEdit -> angleEdit
    QKeyEvent ret2(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(strip.lengthEdit(), &ret2);
    QCOMPARE(strip.focusWidget(), strip.angleEdit());

    // Return in angleEdit -> canvasView
    QKeyEvent ret3(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(strip.angleEdit(), &ret3);
}

void TestContextStrip::typingShortcutLettersWorks()
{
    ContextStrip strip(nullptr);
    strip.show();

    strip.nameEdit()->setFocus();
    // Simulate typing "V_collar_L1" which has V and L (tool shortcuts).
    QTest::keyClicks(strip.nameEdit(), "V_collar_L1");
    QCOMPARE(strip.nameEdit()->text(), QStringLiteral("V_collar_L1"));

    strip.lengthEdit()->setFocus();
    // Simulate typing formula "C+R/2" which has C and R.
    QTest::keyClicks(strip.lengthEdit(), "C+R/2");
    QCOMPARE(strip.lengthEdit()->text(), QStringLiteral("C+R/2"));

    strip.angleEdit()->setFocus();
    // Simulate typing formula "A_angle-90" which has A.
    QTest::keyClicks(strip.angleEdit(), "A_angle-90");
    QCOMPARE(strip.angleEdit()->text(), QStringLiteral("A_angle-90"));
}

// ── 只读悬停 (三期: 测量/角度测量/交点/打断 统一"扫过即看") ──
// 悬停上报 → 条带只读预览 (字段全只读, 显示线段信息); 移出 → 收起。
void TestContextStrip::hoverShowsReadOnlyPreview()
{
    ParamDocument doc;
    const LineRef l = makeLine(doc);

    ContextStrip strip(&doc);
    strip.setHoverTarget(l.blockId, l.segId);
    // 悬停节流 80ms (设计 §4.2): 到期才真正进入 Hover 态 —— 等状态成立
    // 而不是猜时长 (P2-3 waitUntil)。
    QVERIFY2(cad::test::waitUntil([&] { return strip.focusState() == StripFocus::Hover; }),
             "悬停节流到期后应进入 Hover 态");

    QVERIFY(strip.isVisible());
    QCOMPARE(strip.blockId(), l.blockId);
    QCOMPARE(strip.segmentId(), l.segId);
    // Hover = 只读预览: 三个字段全部只读。
    QVERIFY2(strip.nameEdit()->isReadOnly(), "Hover 态名称只读");
    QVERIFY2(strip.lengthEdit()->isReadOnly(), "Hover 态长度只读");
    QVERIFY2(strip.angleEdit()->isReadOnly(), "Hover 态角度只读");
    // 信息回填: 长度读数非空。
    QVERIFY(!strip.lengthEdit()->text().isEmpty());

    // 移出 → Hover 态收起。
    strip.clearHover();
    QVERIFY(!strip.isVisible());
    QCOMPARE(strip.focusState(), StripFocus::Empty);
}

// ─────────────────────────────────────────────────────────────────────────────
// 连接角度会话 (CONTEXT_STRIP_DESIGN.md 二期): 连接手势的角度输入并入条带。
// 条带是纯输入面 —— 击键/单位切换/Enter/Esc 经信号回传宿主; 连接语义留在
// 手势里。这里直驱条带验证会话形态与信号出口。
// ─────────────────────────────────────────────────────────────────────────────

namespace {
/// leader + follower (follower 起点挂在 leader 终点, 跟随角 = @p angleDeg)。
/// 返回的 attId 为空 = 连接建立失败 (调用方断言)。
struct SessionRef {
    QUuid blockId, segId, attId;
};
SessionRef makeSessionDoc(ParamDocument& doc, double angleDeg = 45.0)
{
    const LineRef leader = makeLine(doc);
    const LineRef follower = makeLine(doc, 60.0);
    Attachment att;
    att.fromBlockId = follower.blockId;
    att.fromPointId = follower.startId;
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.followerAngle = angleDeg;
    if (!doc.addAttachment(att))
        return {QUuid(), QUuid(), QUuid()};
    doc.resolveAll();
    return {follower.blockId, follower.segId, att.id};
}
} // namespace

void TestContextStrip::connectSessionShowsFollowerOnlyAngleEditable()
{
    ParamDocument doc;
    const SessionRef s = makeSessionDoc(doc, 45.0);
    QVERIFY(!s.attId.isNull());

    ContextStrip strip(&doc);
    strip.beginConnectAngleSession(s.blockId, s.segId, s.attId, 45.0);

    QVERIFY2(strip.connectSession(), "会话应激活");
    QCOMPARE(strip.focusState(), StripFocus::Pinned);
    QVERIFY(strip.isVisible());
    // 仅角度可编辑; 名称/长度只读展示 (编辑会 push 命令, 破坏整步 undo)。
    QVERIFY2(strip.nameEdit()->isReadOnly(), "会话内名称只读");
    QVERIFY2(strip.lengthEdit()->isReadOnly(), "会话内长度只读");
    QVERIFY2(!strip.angleEdit()->isReadOnly(), "会话内角度可编辑");
    // 初始角度回显 = 带符号折角 (v3 定稿)。
    QCOMPARE(strip.angleEdit()->text(), QStringLiteral("45"));
    // 状态徽标: 跟随 leader (附件存在)。
    QVERIFY2(strip.badgeText().contains(QString::fromUtf8("跟随")),
             qPrintable(QStringLiteral("badge='%1'").arg(strip.badgeText())));
    // 换向禁用 (会话内不得 push 命令)。
    QVERIFY(!strip.reverseButton()->isEnabled());
}

void TestContextStrip::connectSessionTextForwardsSignal()
{
    ParamDocument doc;
    const SessionRef s = makeSessionDoc(doc);
    QVERIFY(!s.attId.isNull());

    ContextStrip strip(&doc);
    strip.beginConnectAngleSession(s.blockId, s.segId, s.attId, 45.0);

    QString lastText;
    int count = 0;
    connect(&strip, &ContextStrip::connectAngleTextChanged,
            &strip, [&](const QString& t) { lastText = t; ++count; });

    strip.angleEdit()->setText(QStringLiteral("30"));
    QCOMPARE(count, 1);
    QCOMPARE(lastText, QStringLiteral("30"));
}

void TestContextStrip::connectSessionEnterCommitsEscCancels()
{
    ParamDocument doc;
    const SessionRef s = makeSessionDoc(doc);
    QVERIFY(!s.attId.isNull());

    ContextStrip strip(&doc);
    strip.beginConnectAngleSession(s.blockId, s.segId, s.attId, 45.0);

    int committed = 0, cancelled = 0, cancelReq = 0;
    connect(&strip, &ContextStrip::connectAngleCommitted,
            &strip, [&] { ++committed; });
    connect(&strip, &ContextStrip::connectAngleCancelled,
            &strip, [&] { ++cancelled; });
    connect(&strip, &ContextStrip::cancelRequested,
            &strip, [&] { ++cancelReq; });

    // Enter (任意字段) = 确认收尾, 不走普通锁定导航。
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(strip.angleEdit(), &enter);
    QCOMPARE(committed, 1);
    QCOMPARE(cancelled, 0);
    QCOMPARE(cancelReq, 0);
    QVERIFY2(strip.connectSession(), "宿主尚未结束会话, 条带应保持会话态");

    // Esc = 取消收尾 (保留连接、角度回退), 也不走 解除锁定/撤销创建。
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(strip.nameEdit(), &esc);
    QCOMPARE(cancelled, 1);
    QCOMPARE(cancelReq, 0);
}

void TestContextStrip::connectSessionModeToggleEmitsSignal()
{
    ParamDocument doc;
    const SessionRef s = makeSessionDoc(doc);
    QVERIFY(!s.attId.isNull());

    ContextStrip strip(&doc);
    strip.beginConnectAngleSession(s.blockId, s.segId, s.attId, 45.0);

    int modeCount = 0;
    cad::param::RotationMode lastMode = cad::param::RotationMode::Angle;
    connect(&strip, &ContextStrip::connectAngleModeChanged,
            &strip, [&](cad::param::RotationMode m) { lastMode = m; ++modeCount; });

    strip.unitArcButton()->click();
    QCOMPARE(modeCount, 1);
    QCOMPARE(lastMode, cad::param::RotationMode::ArcLength);

    strip.unitAngleButton()->click();
    QCOMPARE(modeCount, 2);
    QCOMPARE(lastMode, cad::param::RotationMode::Angle);
}

void TestContextStrip::connectSessionValidSetsProperty()
{
    ParamDocument doc;
    const SessionRef s = makeSessionDoc(doc);
    QVERIFY(!s.attId.isNull());

    ContextStrip strip(&doc);
    strip.beginConnectAngleSession(s.blockId, s.segId, s.attId, 45.0);

    QCOMPARE(strip.angleEdit()->property("angleInvalid").toBool(), false);
    strip.setConnectAngleValid(false);
    QCOMPARE(strip.angleEdit()->property("angleInvalid").toBool(), true);
    strip.setConnectAngleValid(true);
    QCOMPARE(strip.angleEdit()->property("angleInvalid").toBool(), false);

    // 会话外 (普通锁定) 的合法性调用是 no-op。
    strip.endConnectAngleSession();
    strip.setPinnedTarget(s.blockId, s.segId);
    strip.setConnectAngleValid(false);
    QCOMPARE(strip.angleEdit()->property("angleInvalid").toBool(), false);
}

void TestContextStrip::connectSessionEndRestoresNormalPin()
{
    ParamDocument doc;
    const SessionRef s = makeSessionDoc(doc);
    QVERIFY(!s.attId.isNull());

    ContextStrip strip(&doc);
    strip.beginConnectAngleSession(s.blockId, s.segId, s.attId, 45.0);
    QVERIFY(strip.connectSession());

    strip.endConnectAngleSession();
    QVERIFY2(!strip.connectSession(), "会话应结束");
    QVERIFY(!strip.isVisible());

    // 普通锁定恢复: 字段重新可编辑。
    strip.setPinnedTarget(s.blockId, s.segId);
    QCOMPARE(strip.focusState(), StripFocus::Pinned);
    QVERIFY(!strip.nameEdit()->isReadOnly());
    QVERIFY(!strip.lengthEdit()->isReadOnly());
    QVERIFY(!strip.angleEdit()->isReadOnly());
}

void TestContextStrip::basisShowsSerialTagsAndReverseFlips()
{
    // 2026-12 bug: 基准读数曾硬编码 "P1 → P2" 兜底 —— 与端点真实串号无关,
    // 换向后也纹丝不动 ("和点名称完全无关, 点击切换不调转")。修复 = 显示真实
    // 串号 tag (与属性对话框 SegmentAngleCard 同口径), 换向后随 start/end
    // 身份互换翻转。
    ParamDocument doc;
    const LineRef l = makeLine(doc);
    ContextStrip strip(&doc);
    QUndoStack stack;
    strip.setUndoStack(&stack);
    strip.setPinnedTarget(l.blockId, l.segId);

    const auto* b = doc.findBlock(l.blockId);
    const auto* seg = b->findSegment(l.segId);
    QVERIFY(seg);
    const auto* sp = b->findPoint(seg->startPointId);
    const auto* ep = b->findPoint(seg->endPointId);
    QVERIFY(sp && ep);
    const QString sTag = cad::param::Serial::tag(sp->serial);
    const QString eTag = cad::param::Serial::tag(ep->serial);
    const QString fwd = QString::fromUtf8("%1 → %2").arg(sTag, eTag);
    const QString rev = QString::fromUtf8("%1 → %2").arg(eTag, sTag);

    QCOMPARE(strip.basisText(), fwd);
    QVERIFY(strip.reverseButton()->isEnabled());

    strip.reverseButton()->click();
    QVERIFY2(cad::test::waitUntil([&] { return strip.basisText() == rev; }),
             "换向后基准读数应随端点身份翻转");
    QCOMPARE(strip.basisText(), rev);

    // 换回 (undo) → 读回复原。
    stack.undo();
    QVERIFY2(cad::test::waitUntil([&] { return strip.basisText() == fwd; }),
             "undo 后基准读数应恢复");
}

void TestContextStrip::rotateAnchorStateFlipsBasisAndRoutesReverse()
{
    // 2026-12: 旋转会话内条带换向 = 切换锚心 —— 基准读数锚心端在前,
    // 点击转发 reverseRequested (不 push ReverseSegmentCommand)。
    ParamDocument doc;
    const LineRef l = makeLine(doc);
    ContextStrip strip(&doc);
    QUndoStack stack;
    strip.setUndoStack(&stack);
    strip.setPinnedTarget(l.blockId, l.segId);

    const auto* b = doc.findBlock(l.blockId);
    const auto* seg = b->findSegment(l.segId);
    const auto* sp = b->findPoint(seg->startPointId);
    const auto* ep = b->findPoint(seg->endPointId);
    const QString sTag = cad::param::Serial::tag(sp->serial);
    const QString eTag = cad::param::Serial::tag(ep->serial);
    const QString fwd = QString::fromUtf8("%1 → %2").arg(sTag, eTag);
    const QString anchorEnd = QString::fromUtf8("%1 → %2").arg(eTag, sTag);

    bool routed = false;
    QUuid routedBlock, routedSeg;
    QObject::connect(&strip, &ContextStrip::reverseRequested,
                     &strip, [&](const QUuid& bq, const QUuid& sq) {
        routed = true;
        routedBlock = bq;
        routedSeg = sq;
    });

    // 旋转会话激活, 锚在终点 → 基准读数 终点→起点, 换向按钮可点;
    // 角度字段跟随锚心基准: 自由线锚在终点 = 模型方向 +180° (水平线 0 → 180)。
    strip.setRotateAnchorState(true, /*anchorIsEnd=*/true, /*canToggle=*/true, QString());
    QCOMPARE(strip.basisText(), anchorEnd);
    QVERIFY(strip.reverseButton()->isEnabled());
    QCOMPARE(strip.angleEdit()->text(), QStringLiteral("180"));
    QCOMPARE(stack.count(), 0);

    // 点击换向 → 转发 reverseRequested (含目标 id), 不 push 命令。
    strip.reverseButton()->click();
    QVERIFY(routed);
    QCOMPARE(routedBlock, l.blockId);
    QCOMPARE(routedSeg, l.segId);
    QCOMPARE(stack.count(), 0);

    // 锚切回起点 → 读数回到 起点→终点, 角度回到模型世界角 0°。
    strip.setRotateAnchorState(true, /*anchorIsEnd=*/false, /*canToggle=*/true, QString());
    QCOMPARE(strip.basisText(), fwd);
    QCOMPARE(strip.angleEdit()->text(), QStringLiteral("0"));

    // 会话结束 → 恢复模型视角 (普通换向语义, 可 push 命令)。
    strip.setRotateAnchorState(false, false, false, QString());
    QCOMPARE(strip.basisText(), fwd);
    routed = false;
    strip.reverseButton()->click();
    QVERIFY(!routed);
    QCOMPARE(stack.count(), 1);   // 普通换向: push ReverseSegmentCommand
}

QTEST_MAIN(TestContextStrip)
#include "test_context_strip.moc"
