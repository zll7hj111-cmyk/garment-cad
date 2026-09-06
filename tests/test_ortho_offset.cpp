#include <QtTest>
#include <cmath>
#include <numbers>

#include "geometry/Vec2.h"
#include "geometry/Units.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/ParamPoint.h"
#include "ui/LineGeometrySection.h"
#include "ui/LinePropertySession.h"
#include "document/DocumentSerializer.h"
#include <QPushButton>
#include <QButtonGroup>
#include "ElaLineEdit.h"
#include "ElaText.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::ui::LineGeometrySection;

class TestOrthoOffset : public QObject
{
    Q_OBJECT

private slots:
    void orthoDistInputAutoSwitchesDirection();
    void orthoOffsetPreservesCenterAxisAngle();
    void orthoOffsetSwitchLeftRightPreservesAngleAndLength();
    void orthoOffsetLengthDecoupledFromHypot();
    void centerAxisToggleAndSerialization();
    void orthoOffsetUiControls();
};

void TestOrthoOffset::orthoDistInputAutoSwitchesDirection()
{
    ParamDocument doc;
    Block block;
    block.name = "TestAutoDir";

    ParamPoint pStart;
    pStart.name = "A";
    pStart.constraint = PointConstraint::Free;
    pStart.freePos = {0.0, 0.0};
    QUuid idStart = block.addPoint(pStart);

    ParamPoint pEnd;
    pEnd.name = "B";
    pEnd.constraint = PointConstraint::Polar;
    pEnd.refPointId = idStart;
    pEnd.distance = 100.0;
    pEnd.angle = 0.0;
    QUuid idEnd = block.addPoint(pEnd);

    Segment seg;
    seg.name = "L1";
    seg.startPointId = idStart;
    seg.endPointId = idEnd;
    QUuid segId = block.addSegment(seg);
    QUuid blockId = doc.addBlock(std::move(block));

    LineGeometrySection section(&doc, nullptr);
    section.setTarget(blockId, segId);

    auto* editOrtho = section.findChild<ElaLineEdit*>("editOrthoDist");
    auto* groupOrtho = section.findChild<QButtonGroup*>("orthoDirGroup");
    QVERIFY(editOrtho != nullptr);
    QVERIFY(groupOrtho != nullptr);

    // 初始状态应为 0（无偏置）
    QCOMPARE(groupOrtho->checkedId(), 0);

    // 模拟用户在偏置框输入 "1.5" (cm)
    editOrtho->setText("1.5");
    QMetaObject::invokeMethod(&section, "onOrthoDistEdited");

    // 应自动切到 1（左偏置）
    QCOMPARE(groupOrtho->checkedId(), 1);

    const auto* bAfter = doc.findBlock(blockId);
    QVERIFY(bAfter != nullptr);
    const auto* ep = bAfter->findPoint(idEnd);
    QVERIFY(ep != nullptr);
    QCOMPARE(ep->constraint, PointConstraint::OrthoOffset);
    QCOMPARE(ep->orthoOffsetDist, 15.0); // 1.5cm = 15mm (正为左)
}

void TestOrthoOffset::orthoOffsetPreservesCenterAxisAngle()
{
    ParamDocument doc;
    Block block;
    block.name = "AngleHold";

    ParamPoint pStart;
    pStart.name = "A";
    pStart.constraint = PointConstraint::Free;
    pStart.freePos = {0.0, 0.0};
    QUuid idStart = block.addPoint(pStart);

    ParamPoint pEnd;
    pEnd.name = "B";
    pEnd.constraint = PointConstraint::Polar;
    pEnd.refPointId = idStart;
    pEnd.distance = 100.0;
    pEnd.angle = 45.0; // 主轴基准角度 45°
    QUuid idEnd = block.addPoint(pEnd);

    Segment seg;
    seg.name = "L1";
    seg.startPointId = idStart;
    seg.endPointId = idEnd;
    QUuid segId = block.addSegment(seg);
    QUuid blockId = doc.addBlock(std::move(block));

    LineGeometrySection section(&doc, nullptr);
    section.setTarget(blockId, segId);

    auto* groupOrtho = section.findChild<QButtonGroup*> ("orthoDirGroup");
    auto* editOrtho = section.findChild<ElaLineEdit*> ("editOrthoDist");
    QVERIFY(groupOrtho != nullptr && editOrtho != nullptr);

    // 输入偏置 2cm
    editOrtho->setText("2.0");
    QMetaObject::invokeMethod(&section, "onOrthoDistEdited");

    const auto* b1 = doc.findBlock(blockId);
    const auto* ep1 = b1->findPoint(idEnd);
    // 基准角度必须保持 45° 绝对不变！
    QCOMPARE(ep1->angle, 45.0);

    // 切换为右偏置
    groupOrtho->button(2)->click();
    const auto* b2 = doc.findBlock(blockId);
    const auto* ep2 = b2->findPoint(idEnd);
    QCOMPARE(ep2->angle, 45.0);
    QCOMPARE(ep2->orthoOffsetDist, -20.0); // 右偏置为负
}

void TestOrthoOffset::orthoOffsetSwitchLeftRightPreservesAngleAndLength()
{
    ParamDocument doc;
    Block block;
    block.name = "TestSymmetry";

    ParamPoint pStart;
    pStart.name = "A";
    pStart.constraint = PointConstraint::Free;
    pStart.freePos = {100.0, 200.0};
    QUuid idStart = block.addPoint(pStart);

    ParamPoint pEnd;
    pEnd.name = "B";
    pEnd.constraint = PointConstraint::Polar;
    pEnd.refPointId = idStart;
    pEnd.distance = 150.0;
    pEnd.angle = 30.0; // 基准方向 30°
    QUuid idEnd = block.addPoint(pEnd);

    Segment seg;
    seg.name = "L1";
    seg.startPointId = idStart;
    seg.endPointId = idEnd;
    QUuid segId = block.addSegment(seg);
    QUuid blockId = doc.addBlock(std::move(block));

    LineGeometrySection section(&doc, nullptr);
    section.setTarget(blockId, segId);

    auto* editOrtho = section.findChild<ElaLineEdit*>("editOrthoDist");
    auto* groupOrtho = section.findChild<QButtonGroup*>("orthoDirGroup");

    // 1. 设置左偏置 2.0 cm (20mm)
    editOrtho->setText("2.0");
    QMetaObject::invokeMethod(&section, "onOrthoDistEdited");
    doc.resolveAll();

    const auto* bLeft = doc.findBlock(blockId);
    const auto* epLeft = bLeft->findPoint(idEnd);
    QCOMPARE(epLeft->angle, 30.0);
    QCOMPARE(epLeft->distance, 150.0);
    QCOMPARE(epLeft->orthoOffsetDist, 20.0);

    Vec2 posLeft = epLeft->resolvedPos;

    // 2. 切换为右偏置
    groupOrtho->button(2)->click();
    doc.resolveAll();

    const auto* bRight = doc.findBlock(blockId);
    const auto* epRight = bRight->findPoint(idEnd);
    // 左右切换：基准角和基准长必须纹丝不动
    QCOMPARE(epRight->angle, 30.0);
    QCOMPARE(epRight->distance, 150.0);
    QCOMPARE(epRight->orthoOffsetDist, -20.0);

    Vec2 posRight = epRight->resolvedPos;

    // 3. 几何对称性验证：左右端点的中点，必须严格落在主轴垂足 (A + 150mm @ 30°) 上！
    const double rad = 30.0 * std::numbers::pi / 180.0;
    Vec2 centerFoot = Vec2{100.0, 200.0} + Vec2{std::cos(rad), std::sin(rad)} * 150.0;
    Vec2 midPos = (posLeft + posRight) * 0.5;

    QVERIFY(std::abs(midPos.x - centerFoot.x) < 1e-4);
    QVERIFY(std::abs(midPos.y - centerFoot.y) < 1e-4);

    // 左右两点之间的距离必须严格等于 2 * 20mm = 40mm
    double distBetween = (posLeft - posRight).length();
    QVERIFY(std::abs(distBetween - 40.0) < 1e-4);
}

void TestOrthoOffset::orthoOffsetLengthDecoupledFromHypot()
{
    ParamDocument doc;
    Block block;
    block.name = "TestDecouple";

    ParamPoint pStart;
    pStart.name = "A";
    pStart.constraint = PointConstraint::Free;
    pStart.freePos = {0.0, 0.0};
    QUuid idStart = block.addPoint(pStart);

    ParamPoint pEnd;
    pEnd.name = "B";
    pEnd.constraint = PointConstraint::OrthoOffset;
    pEnd.refPointId = idStart;
    pEnd.distance = 100.0; // 主轴基准长 100mm
    pEnd.angle = 0.0;
    pEnd.orthoOffsetDist = 15.0; // 偏置 15mm, 此时斜长 sqrt(100^2 + 15^2) ≈ 101.12mm
    QUuid idEnd = block.addPoint(pEnd);

    Segment seg;
    seg.name = "L1";
    seg.startPointId = idStart;
    seg.endPointId = idEnd;
    QUuid segId = block.addSegment(seg);
    QUuid blockId = doc.addBlock(std::move(block));
    doc.resolveAll();

    const auto* bDbg = doc.findBlock(blockId);

    LineGeometrySection section(&doc, nullptr);
    section.setTarget(blockId, segId);
    section.populateFromModel(*bDbg, *bDbg->findSegment(segId));

    auto* editLen = section.findChild<ElaLineEdit*>("editLength");
    auto* lblHypot = section.findChild<ElaText*>("lblOrthoHypot");
    QVERIFY(editLen != nullptr);
    QVERIFY(lblHypot != nullptr);

    // 主长度框必须显示基准长 10.0 cm，而不是斜长 10.11 cm！
    QCOMPARE(editLen->text(), "10");
    // 斜长标签必须显示斜长信息
    QVERIFY(!lblHypot->isHidden());
    QVERIFY(lblHypot->text().contains("10.11"));

    // 多次连续触发 apply，基准长度绝不能发生膨胀
    for (int i = 0; i < 5; ++i) {
        QMetaObject::invokeMethod(&section, "onLengthApply");
        const auto* b = doc.findBlock(blockId);
        const auto* ep = b->findPoint(idEnd);
        QCOMPARE(ep->distance, 100.0);
    }
}

void TestOrthoOffset::centerAxisToggleAndSerialization()
{
    ParamDocument doc;
    Block block;
    block.name = "AxisTest";

    ParamPoint pStart;
    pStart.name = "A";
    pStart.constraint = PointConstraint::Free;
    pStart.freePos = {0.0, 0.0};
    QUuid idStart = block.addPoint(pStart);

    ParamPoint pEnd;
    pEnd.name = "B";
    pEnd.constraint = PointConstraint::OrthoOffset;
    pEnd.refPointId = idStart;
    pEnd.distance = 80.0;
    pEnd.angle = 90.0;
    pEnd.orthoOffsetDist = 10.0;
    QUuid idEnd = block.addPoint(pEnd);

    Segment seg;
    seg.name = "L1";
    seg.startPointId = idStart;
    seg.endPointId = idEnd;
    seg.showOrthoAxis = true;
    QUuid segId = block.addSegment(seg);
    QUuid blockId = doc.addBlock(std::move(block));

    LineGeometrySection section(&doc, nullptr);
    section.setTarget(blockId, segId);

    auto* btnAxis = section.findChild<QPushButton*>("btnToggleAxis");
    QVERIFY(btnAxis != nullptr);
    QVERIFY(btnAxis->isChecked());

    // 点击切换基准轴虚线显隐
    btnAxis->click();
    const auto* b1 = doc.findBlock(blockId);
    const auto* s1 = b1->findSegment(segId);
    QCOMPARE(s1->showOrthoAxis, false);

    // 序列化与反序列化持久化测试
    QJsonObject json = DocumentSerializer::serialize(doc);
    ParamDocument restoredDoc;
    DocumentSerializer::deserialize(restoredDoc, json);

    const auto* bRestored = restoredDoc.findBlock(blockId);
    QVERIFY(bRestored != nullptr);
    const auto* sRestored = bRestored->findSegment(segId);
    QVERIFY(sRestored != nullptr);
    QCOMPARE(sRestored->showOrthoAxis, false);
}

void TestOrthoOffset::orthoOffsetUiControls()
{
    ParamDocument doc;
    Block block;
    block.name = "UiTest";

    ParamPoint pStart;
    pStart.name = "A";
    pStart.constraint = PointConstraint::Free;
    pStart.freePos = {0.0, 0.0};
    QUuid idStart = block.addPoint(pStart);

    ParamPoint pEnd;
    pEnd.name = "B";
    pEnd.constraint = PointConstraint::Polar;
    pEnd.refPointId = idStart;
    pEnd.distance = 120.0;
    pEnd.angle = 15.0;
    QUuid idEnd = block.addPoint(pEnd);

    Segment seg;
    seg.name = "L1";
    seg.startPointId = idStart;
    seg.endPointId = idEnd;
    QUuid segId = block.addSegment(seg);
    QUuid blockId = doc.addBlock(std::move(block));

    LineGeometrySection section(&doc, nullptr);
    section.setTarget(blockId, segId);

    auto* groupOrtho = section.findChild<QButtonGroup*>("orthoDirGroup");
    auto* editOrtho = section.findChild<ElaLineEdit*>("editOrthoDist");
    auto* btnAxis = section.findChild<QPushButton*>("btnToggleAxis");

    QVERIFY(groupOrtho != nullptr);
    QVERIFY(editOrtho != nullptr);
    QVERIFY(btnAxis != nullptr);

    // 切换到无偏置
    groupOrtho->button(0)->click();
    const auto* bNone = doc.findBlock(blockId);
    const auto* epNone = bNone->findPoint(idEnd);
    QCOMPARE(epNone->constraint, PointConstraint::Polar);
    QCOMPARE(epNone->orthoOffsetDist, 0.0);
}

QTEST_MAIN(TestOrthoOffset)
#include "test_ortho_offset.moc"
