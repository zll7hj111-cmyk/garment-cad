/// @file test_circle_strip.cpp
/// 圆专属底部属性条 CircleStripBar (CIRCLE_TOOL_DESIGN.md §6.1; 2026-12 用户
/// 拍板「圆应该有自己的底部状态栏, 不应该复用线段的状态」)。覆盖: 六槽填充
/// (圆徽标/编号/名称/半径 R/直径 D/周长 C)、悬停只读预览、半径数值与公式
/// 落笔、直径与周长反算半径、名称写入、Esc 解除锁定不落笔、换选直线后圆条带
/// 退场、撤销回退半径。

#include <QtTest>
#include <QApplication>
#include <QLineEdit>
#include <QUndoStack>

#include "app/ContextStrip.h"
#include "app/CircleStripBar.h"
#include "ElaLineEdit.h"
#include "ElaText.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Serial.h"
#include "geometry/Angle.h"
#include "geometry/Units.h"
#include "geometry/Vec2.h"
#include "tools/CircleFactory.h"
#include "TestHelpers.h"

using namespace cad::param;
using namespace cad::app;

namespace {

/// One circle block (fitKind == Circle) built through the M1 creation path.
struct CircleRef {
    QUuid blockId;
    QUuid segId;
    QUuid centerId;
    QUuid startId;
    QUuid endId;
};

CircleRef makeCircle(ParamDocument& doc, double radiusMm, double startAngleDeg = 0.0)
{
    doc.setActiveLayer(cad::test::layerIdAt(doc, 1));
    cad::tools::CircleFactory factory(&doc, nullptr);
    CircleRef r;
    r.blockId = factory.createCircle(cad::geo::Vec2(0.0, 0.0), radiusMm, startAngleDeg);
    if (const Block* b = doc.findBlock(r.blockId); b && !b->segments.empty()) {
        r.segId = b->segments.front().id;
        r.startId = b->segments.front().startPointId;
        r.endId = b->segments.front().endPointId;
        if (const ParamPoint* sp = b->findPoint(r.startId))
            r.centerId = sp->refPointId;
    }
    doc.resolveAll();
    return r;
}

/// One block with a single line (对照: 换选后圆条带必须退场)。
struct LineRef {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

LineRef makeLine(ParamDocument& doc, double lenMm = 100.0)
{
    doc.setActiveLayer(cad::test::layerIdAt(doc, 1));
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

class TestCircleStrip : public QObject
{
    Q_OBJECT

private slots:
    void pinnedCircleRoutesToCircleBarAndFillsSixFields();
    void hoverPreviewIsReadOnly();
    void radiusEditResizesCircle();
    void radiusFormulaWritesStartDistanceFormula();
    void diameterEditBackCalculatesRadius();
    void circumferenceEditBackCalculatesRadius();
    void nameEditApplies();
    void escUnpinsWithoutWriting();
    void switchingToLineLeavesCircleBar();
    void undoRestoresPreviousRadius();
    // 落圆后路由 (一期补充②, CIRCLE_TOOL_DESIGN.md §5.6)
    void pinCreatedLineRoutesCircleToCircleBar();
    // 绘制会话 (一期补充, CIRCLE_TOOL_DESIGN.md §5.5)
    void sessionShowsRadiusAndDiameterOnly();
    void sessionValuesDriveDiameterAndLockChip();
    void sessionRadiusInputEmitsLockedValues();
    void sessionEnterCommitsEscCancels();
    void endingSessionRestoresCircleFields();
};

// ── 选中圆: 专属条带接管, 六槽填充 ──

void TestCircleStrip::pinnedCircleRoutesToCircleBarAndFillsSixFields()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0, 0.0);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(c.blockId, c.segId);

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    QVERIFY2(bar->hasTarget(), "选中圆后应切到圆专属条带");
    QVERIFY2(bar->isVisible(), "圆条带应可见");
    QCOMPARE(bar->badgeText(), QString::fromUtf8("圆"));

    const auto* b = doc.findBlock(c.blockId);
    const auto* seg = b->findSegment(c.segId);
    QVERIFY(b && seg);
    QCOMPARE(bar->serialText(), cad::param::Serial::tag(seg->serial));

    // 50 mm 半径 → R 5 cm, D 10 cm, C 2πR ≈ 31.42 cm
    QCOMPARE(bar->radiusEdit()->text(), QStringLiteral("5"));
    QCOMPARE(bar->diameterEdit()->text(), QStringLiteral("10"));
    QCOMPARE(bar->circumferenceEdit()->text(), QStringLiteral("31.42"));

    // 锁定态四个框可编辑
    QVERIFY(!bar->radiusEdit()->isReadOnly());
    QVERIFY(!bar->diameterEdit()->isReadOnly());
    QVERIFY(!bar->circumferenceEdit()->isReadOnly());
    QVERIFY(!bar->nameEdit()->isReadOnly());
}

// ── 悬停: 只读预览, 移出收起 ──

void TestCircleStrip::hoverPreviewIsReadOnly()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0);

    ContextStrip strip(&doc);
    strip.setHoverTarget(c.blockId, c.segId);
    // 悬停节流 80ms (设计 §4.2): 等状态成立而不是猜时长。
    QVERIFY2(cad::test::waitUntil([&] { return strip.focusState() == StripFocus::Hover; }),
             "悬停节流到期后应进入 Hover 态");

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    QVERIFY2(bar->hasTarget(), "悬停圆也应走圆条带");
    QVERIFY(bar->isVisible());
    QCOMPARE(bar->radiusEdit()->text(), QStringLiteral("5"));
    QVERIFY2(bar->radiusEdit()->isReadOnly(), "Hover 态半径只读");
    QVERIFY2(bar->diameterEdit()->isReadOnly(), "Hover 态直径只读");
    QVERIFY2(bar->circumferenceEdit()->isReadOnly(), "Hover 态周长只读");
    QVERIFY2(bar->nameEdit()->isReadOnly(), "Hover 态名称只读");

    strip.clearHover();
    QVERIFY(!bar->hasTarget());
    QCOMPARE(strip.focusState(), StripFocus::Empty);
}

// ── 半径: 数值写入起点 distance (圆心不动) ──

void TestCircleStrip::radiusEditResizesCircle()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(c.blockId, c.segId);

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    bar->radiusEdit()->setText(QStringLiteral("7.5"));            // 75 mm
    emit bar->radiusEdit()->editingFinished();

    const QUuid bid = c.blockId, sid = c.segId;
    QVERIFY2(cad::test::waitUntil([&] {
        const auto* b = doc.findBlock(bid);
        const auto* s = b ? b->findSegment(sid) : nullptr;
        return b && s && qAbs(b->circleRadiusMm(*s) - 75.0) < 1e-6;
    }), "半径编辑应写回起点 distance");

    const auto* b = doc.findBlock(bid);
    const auto* seg = b->findSegment(sid);
    const auto* sp = b->findPoint(c.startId);
    QVERIFY(sp);
    QVERIFY(qAbs(sp->distance - 75.0) < 1e-6);
    QVERIFY(sp->distanceFormula.isEmpty());
    // 圆心不动, 包角仍 360
    QVERIFY(b->findPoint(c.centerId)->resolvedPos.distanceTo(cad::geo::Vec2::zero()) < 1e-9);
    QVERIFY(qAbs(b->circleSweepDeg(*seg) - 360.0) < 1e-6);
    // 直径/周长同步回读 (派生量)
    QCOMPARE(bar->radiusEdit()->text(), QStringLiteral("7.5"));
    QCOMPARE(bar->diameterEdit()->text(), QStringLiteral("15"));
    QCOMPARE(bar->circumferenceEdit()->text(), QStringLiteral("47.12"));
}

// ── 半径公式: 写 distanceFormula, 解算后半径一致 ──

void TestCircleStrip::radiusFormulaWritesStartDistanceFormula()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(c.blockId, c.segId);

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    bar->radiusEdit()->setText(QStringLiteral("3+2"));             // 5 cm = 50 mm
    emit bar->radiusEdit()->editingFinished();

    const QUuid bid = c.blockId, sid = c.segId;
    QVERIFY2(cad::test::waitUntil([&] {
        const auto* b = doc.findBlock(bid);
        const auto* s = b ? b->findSegment(sid) : nullptr;
        return b && s && qAbs(b->circleRadiusMm(*s) - 50.0) < 1e-6;
    }), "半径公式应经求值写回起点 distance");

    const auto* b = doc.findBlock(bid);
    const auto* sp = b->findPoint(c.startId);
    QCOMPARE(sp->distanceFormula, QStringLiteral("3+2"));
    QVERIFY(qAbs(sp->distance - 50.0) < 1e-6);
}

// ── 直径: 反算半径后落一步撤销 ──

void TestCircleStrip::diameterEditBackCalculatesRadius()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0);

    QUndoStack stack;
    ContextStrip strip(&doc);
    strip.setUndoStack(&stack);
    strip.setPinnedTarget(c.blockId, c.segId);

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    bar->diameterEdit()->setText(QStringLiteral("12"));            // D = 12 cm → R = 6 cm
    emit bar->diameterEdit()->editingFinished();

    const QUuid bid = c.blockId, sid = c.segId;
    QVERIFY2(cad::test::waitUntil([&] {
        const auto* b = doc.findBlock(bid);
        const auto* s = b ? b->findSegment(sid) : nullptr;
        return b && s && qAbs(b->circleRadiusMm(*s) - 60.0) < 1e-6;
    }), "直径编辑应反算半径并写回起点 distance");

    QCOMPARE(bar->radiusEdit()->text(), QStringLiteral("6"));
    QCOMPARE(bar->diameterEdit()->text(), QStringLiteral("12"));
    QCOMPARE(stack.count(), 1);                                    // 一步撤销
}

// ── 周长: 反算半径 (÷2π, 与属性面板同源) ──

void TestCircleStrip::circumferenceEditBackCalculatesRadius()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(c.blockId, c.segId);

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    bar->circumferenceEdit()->setText(QStringLiteral("10"));       // C = 10 cm
    emit bar->circumferenceEdit()->editingFinished();

    // 数值周长先 ÷2π 换算成半径文本 (cm, 2 位小数去尾零 —— 与属性面板
    // CircleGeometrySection::onCircumferenceEdited 同口径), 再由半径落笔。
    const QString rText = cad::geo::Units::formatCm(
        cad::geo::Units::cmToMm(10.0) / (2.0 * cad::geo::kPi));
    const double expectedMm = cad::geo::Units::cmToMm(rText.toDouble());
    const QUuid bid = c.blockId, sid = c.segId;
    QVERIFY2(cad::test::waitUntil([&] {
        const auto* b = doc.findBlock(bid);
        const auto* s = b ? b->findSegment(sid) : nullptr;
        return b && s && qAbs(b->circleRadiusMm(*s) - expectedMm) < 1e-6;
    }), "周长编辑应 ÷2π 反算半径");

    QCOMPARE(bar->radiusEdit()->text(), rText);
}

// ── 名称: 写入段名 ──

void TestCircleStrip::nameEditApplies()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(c.blockId, c.segId);

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    bar->nameEdit()->setText(QString::fromUtf8("领圆"));
    emit bar->nameEdit()->editingFinished();

    const QUuid bid = c.blockId, sid = c.segId;
    QVERIFY2(cad::test::waitUntil([&] {
        const auto* b = doc.findBlock(bid);
        const auto* s = b ? b->findSegment(sid) : nullptr;
        return s && s->name == QString::fromUtf8("领圆");
    }), "名称编辑应写入段名");
}

// ── Esc: 解除锁定且不把输入框里的半成品写进模型 ──

void TestCircleStrip::escUnpinsWithoutWriting()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(c.blockId, c.segId);

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    bar->radiusEdit()->setText(QStringLiteral("9"));               // 未提交
    QTest::keyClick(bar->radiusEdit(), Qt::Key_Escape);

    QVERIFY2(!bar->hasTarget(), "Esc 应解除锁定并清空目标");
    const auto* b = doc.findBlock(c.blockId);
    const auto* seg = b->findSegment(c.segId);
    QVERIFY(qAbs(b->circleRadiusMm(*seg) - 50.0) < 1e-9);          // 半径未变
}

// ── 换选直线: 圆条带退场, 线段条带回归 ──

void TestCircleStrip::switchingToLineLeavesCircleBar()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0);
    const LineRef l = makeLine(doc, 100.0);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(c.blockId, c.segId);
    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    QVERIFY(bar->hasTarget());
    QCOMPARE(bar->badgeText(), QString::fromUtf8("圆"));

    strip.setPinnedTarget(l.blockId, l.segId);
    QVERIFY2(!bar->hasTarget(), "换选直线后圆条带必须退场");
    QVERIFY2(!bar->isVisible(), "圆条带应隐藏");
    QVERIFY2(strip.badgeText() != QString::fromUtf8("圆"), "线段条带不应残留圆文案");
    QCOMPARE(strip.lengthEdit()->text(), QStringLiteral("10"));    // 100 mm = 10 cm
}

// ── 撤销: 半径回到上一步 ──

void TestCircleStrip::undoRestoresPreviousRadius()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0);

    QUndoStack stack;
    ContextStrip strip(&doc);
    strip.setUndoStack(&stack);
    strip.setPinnedTarget(c.blockId, c.segId);

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    bar->radiusEdit()->setText(QStringLiteral("7.5"));
    emit bar->radiusEdit()->editingFinished();

    const QUuid bid = c.blockId, sid = c.segId;
    QVERIFY2(cad::test::waitUntil([&] {
        const auto* b = doc.findBlock(bid);
        const auto* s = b ? b->findSegment(sid) : nullptr;
        return b && s && qAbs(b->circleRadiusMm(*s) - 75.0) < 1e-6;
    }), "半径编辑应先落笔");
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QVERIFY2(cad::test::waitUntil([&] {
        const auto* b = doc.findBlock(bid);
        const auto* s = b ? b->findSegment(sid) : nullptr;
        return b && s && qAbs(b->circleRadiusMm(*s) - 50.0) < 1e-6;
    }), "撤销后半径应回到 50 mm");
    QCOMPARE(bar->radiusEdit()->text(), QStringLiteral("5"));
}

// ── 绘制会话: 只有半径可输入, 直径只读联动 (§5.5) ──

void TestCircleStrip::sessionShowsRadiusAndDiameterOnly()
{
    ParamDocument doc;
    ContextStrip strip(&doc);
    strip.beginCircleSession();

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    QVERIFY(bar->isSession());
    QVERIFY(bar->isVisible());
    QCOMPARE(bar->badgeText(), QString::fromUtf8("绘制"));
    QVERIFY2(!bar->hasTarget(), "绘制态还没有模型目标");

    QVERIFY2(!bar->radiusEdit()->isHidden(), "绘制态半径框必须在");
    QVERIFY2(!bar->radiusEdit()->isReadOnly(), "绘制态半径可输入");
    QVERIFY2(!bar->diameterEdit()->isHidden(), "绘制态直径框在 (只读联动)");
    QVERIFY2(bar->diameterEdit()->isReadOnly(), "绘制态直径只读");
    QVERIFY2(bar->nameEdit()->isHidden(), "绘制态名称让位");
    QVERIFY2(bar->circumferenceEdit()->isHidden(), "绘制态周长让位");
    QVERIFY2(bar->lockChip()->isHidden(), "未输入时不该有锁定 chip");
}

void TestCircleStrip::sessionValuesDriveDiameterAndLockChip()
{
    ParamDocument doc;
    ContextStrip strip(&doc);
    strip.beginCircleSession();

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);

    strip.updateCircleSessionValues(2.5, false);   // 光标拖出 2.5 cm
    QCOMPARE(bar->radiusEdit()->text(), QStringLiteral("2.5"));
    QCOMPARE(bar->diameterEdit()->text(), QStringLiteral("5"));
    QVERIFY(!bar->isRadiusLocked());
    QVERIFY(bar->lockChip()->isHidden());

    // 输入定值 → 锁定: 半径框保留用户输入 (不许被回声覆盖), 直径联动到定值。
    bar->radiusEdit()->setText(QStringLiteral("3"));
    strip.updateCircleSessionValues(3.0, true);
    QCOMPARE(bar->radiusEdit()->text(), QStringLiteral("3"));
    QCOMPARE(bar->diameterEdit()->text(), QStringLiteral("6"));
    QVERIFY(bar->isRadiusLocked());
    QVERIFY2(!bar->lockChip()->isHidden(), "锁定后 chip 应出现");

    // 公式输入: 锁定回声 5.0 不得把「3+2」改写成「5」, 但直径照算。
    bar->radiusEdit()->setText(QStringLiteral("3+2"));
    strip.updateCircleSessionValues(5.0, true);
    QCOMPARE(bar->radiusEdit()->text(), QStringLiteral("3+2"));
    QCOMPARE(bar->diameterEdit()->text(), QStringLiteral("10"));
}

void TestCircleStrip::sessionRadiusInputEmitsLockedValues()
{
    ParamDocument doc;
    ContextStrip strip(&doc);
    strip.beginCircleSession();

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    QSignalSpy radiusSpy(&strip, &ContextStrip::circleSessionRadiusChanged);

    bar->radiusEdit()->setFocus();
    QTest::keyClicks(bar->radiusEdit(), QStringLiteral("4"));
    QCOMPARE(radiusSpy.count(), 1);
    QCOMPARE(radiusSpy.at(0).at(0).toDouble(), 4.0);
    QCOMPARE(radiusSpy.at(0).at(1).toBool(), true);

    QTest::keyClicks(bar->radiusEdit(), QStringLiteral("+1"));   // 公式 4+1 → 5
    QCOMPARE(radiusSpy.count(), 2);
    QCOMPARE(radiusSpy.at(1).at(0).toDouble(), 5.0);
    QCOMPARE(radiusSpy.at(1).at(1).toBool(), true);

    bar->radiusEdit()->selectAll();                              // 清空 → 解锁
    QTest::keyClick(bar->radiusEdit(), Qt::Key_Backspace);
    QCOMPARE(radiusSpy.count(), 3);
    QCOMPARE(radiusSpy.at(2).at(0).toDouble(), 0.0);
    QCOMPARE(radiusSpy.at(2).at(1).toBool(), false);
}

void TestCircleStrip::sessionEnterCommitsEscCancels()
{
    ParamDocument doc;
    ContextStrip strip(&doc);
    strip.beginCircleSession();

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    QSignalSpy commitSpy(&strip, &ContextStrip::circleSessionCommitted);
    QSignalSpy cancelSpy(&strip, &ContextStrip::circleSessionCancelled);
    QSignalSpy radiusSpy(&strip, &ContextStrip::circleSessionRadiusChanged);

    bar->radiusEdit()->setFocus();
    QTest::keyClick(bar->radiusEdit(), Qt::Key_Return);
    QCOMPARE(commitSpy.count(), 1);
    QCOMPARE(radiusSpy.count(), 0);   // Enter 不该顺手落笔

    QTest::keyClick(bar->radiusEdit(), Qt::Key_Escape);
    QCOMPARE(cancelSpy.count(), 1);
    QCOMPARE(commitSpy.count(), 1);
}

void TestCircleStrip::endingSessionRestoresCircleFields()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0);

    ContextStrip strip(&doc);
    strip.setPinnedTarget(c.blockId, c.segId);
    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    QVERIFY(bar->hasTarget());

    strip.beginCircleSession();
    QVERIFY(bar->isSession());
    QVERIFY(!bar->hasTarget());

    strip.endCircleSession();
    QVERIFY(!bar->isSession());
    QVERIFY(!bar->hasTarget());
    QVERIFY2(!bar->isVisible(), "会话结束圆条带应退场");
    QCOMPARE(bar->badgeText(), QString::fromUtf8("圆"));
    QVERIFY2(!bar->nameEdit()->isHidden(), "会话结束名称框回归");
    QVERIFY2(!bar->circumferenceEdit()->isHidden(), "会话结束周长框回归");
    QVERIFY(bar->lockChip()->isHidden());
}

// ── 落圆后路由: 创建锁定通道同样落到圆专属条带 (一期补充②) ──

void TestCircleStrip::pinCreatedLineRoutesCircleToCircleBar()
{
    ParamDocument doc;
    const CircleRef c = makeCircle(doc, 50.0);

    ContextStrip strip(&doc);
    QSignalSpy cancelSpy(&strip, &ContextStrip::cancelRequested);
    // 工具落圆后走的就是这条 (CanvasScene::lineCreated → pinCreatedLine)。
    strip.pinCreatedLine(c.blockId, c.segId);

    CircleStripBar* bar = strip.circleBar();
    QVERIFY(bar);
    QVERIFY2(bar->hasTarget(), "落圆后应路由到圆专属条带");
    QVERIFY2(bar->isVisible(), "圆条带应可见");
    QVERIFY2(!bar->isSession(), "创建锁定不是绘制会话");
    QVERIFY2(!bar->radiusEdit()->isReadOnly(), "创建锁定态半径可编辑");
    QCOMPARE(bar->radiusEdit()->text(), QStringLiteral("5"));
    QCOMPARE(strip.focusState(), StripFocus::Pinned);

    // Esc = 创建锁定 → 取消创建 (与线段条带同语义)。
    emit bar->cancelRequested();
    QCOMPARE(cancelSpy.count(), 1);
}

QTEST_MAIN(TestCircleStrip)
#include "test_circle_strip.moc"
