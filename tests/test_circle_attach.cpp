/// @file test_circle_attach.cpp
/// D9 acceptance: 圆段作角度基准 (docs/design/CIRCLE_TOOL_DESIGN.md §20,
/// 2026-12 修正「禁止」为「修正基准」)。
///
/// 背景: 拖动连接 (ConnectGesture::attachToTarget 线级分支) 曾用「块 rotation +
/// 母线弦覆盖」的旧公式计算角度基准方向。整圆两端点位置重合 ⇒ 弦长为 0 ⇒
/// 基准退化成块 rotation (与圆周无关的垃圾值) ⇒ 首次 resolveAll() 后跟随线
/// 跳变。修正为 cad::param::effectiveAngleRefWorld —— 与解析器
/// (ResolverAttachment) / 面板重连 / SmartPen 建线同一函数:
///   ① 整圆宿主: 基准 = 接缝 (起点) 的出口切向, 与块 rotation 无关;
///   ② 圆弧宿主: 基准 = 母线弦 (两端点不重合), 与旧公式一致;
///   ③ 直线宿主: 基准 = 母线弦, 存量行为逐位不变;
///   ④ 「连接不改变几何」对圆宿主同样成立 (反算角 = 解析器同源 → 不跳变);
///   ⑤ 旋转圆块后跟随线保持相对角 (切线跟随能力保留)。
///
/// Run: test_circle_attach

#include <QtTest>
#include <QApplication>
#include <QUndoStack>

#include <cmath>

#include "geometry/Angle.h"
#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/FollowerAngle.h"
#include "parametric/ParamDocument.h"
#include "parametric/ParamPoint.h"
#include "canvas/CanvasScene.h"
#include "tools/CircleFactory.h"
#include "tools/ConnectGesture.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

struct CircleIds {
    QUuid blockId;
    QUuid segId;
    QUuid centerId;
    QUuid startId;
    QUuid endId;
    std::vector<QUuid> anchorIds;
};

/// 整圆: 圆心 Free 于块局部原点, 起点 Polar = 接缝 (半径唯一权威), 3 个象限锚.
CircleIds makeCircle(ParamDocument& doc, double radiusMm, double startAngleDeg = 0.0,
                     const Vec2& center = Vec2::zero())
{
    CircleIds ids;
    cad::tools::CircleFactory factory(&doc, doc.undoStack());
    ids.blockId = factory.createCircle(center, radiusMm, startAngleDeg);
    if (Block* b = doc.findBlock(ids.blockId); b && !b->segments.empty()) {
        ids.segId = b->segments.front().id;
        ids.startId = b->segments.front().startPointId;
        ids.endId = b->segments.front().endPointId;
        ids.anchorIds = b->segments.front().passPointIds;
        if (const ParamPoint* sp = b->findPoint(ids.startId))
            ids.centerId = sp->refPointId;
    }
    doc.resolveAll();
    return ids;
}

/// 线级连接手势: 把 @p line 的起点拖到 @p target 释放。返回是否建立附件
/// (true = 手势进入 AngleInput)。无画布视图 (CanvasScene::currentZoom 空视图
/// 返回 1.0), 全程无 GUI 交互。
bool dragConnect(ParamDocument& doc, CanvasScene& scene,
                 const cad::test::LineSetup& line, const Vec2& target)
{
    cad::tools::ConnectGesture gesture(&scene, &doc, doc.undoStack(),
        [](cad::tools::SelectState) {},                    // setState
        [](const QString&) {},                             // showToast
        [] {},                                             // clearSelectionAndIdle
        [] { return false; },                              // selectionEmpty
        [](const QUuid&, const QUuid&, const QUuid&, double) {},  // beginAngleSession
        [](bool) {});                                      // angleValidity
    gesture.beginConnect(line.blockId, line.startId, target);
    gesture.move(target);
    gesture.release(target);
    return gesture.state() == cad::tools::SelectState::AngleInput;
}

/// 块在 @p pointId 处的出口方向 (**世界域**, 度): 局部弦方向 + 块旋转。
/// directionAtPoint 返回的是局部坐标下的弦方向, 块整体旋转时它不变 —— 断言
/// 「连接不改变几何」必须看世界方向, 否则块被转 90° 也测不出来。
double worldDirDeg(const ParamDocument& doc, const QUuid& blockId, const QUuid& pointId)
{
    const Block* b = doc.findBlock(blockId);
    return b ? cad::geo::radToDeg(b->transform.rotation + b->directionAtPoint(pointId))
             : 1e18;
}

double angleDiffDeg(double a, double b)
{
    return cad::geo::normalizeDeg180(a - b);
}

} // namespace

class TestCircleAttach : public QObject
{
    Q_OBJECT

private slots:
    void fullCircleLeaderKeepsFollowerGeometry();
    void fullCircleRotationRotatesFollower();
    void partialArcLeaderUsesChord();
    void straightLineLeaderStillUsesChord();
};

/// 整圆作基准: 连接不改变几何, 基准 = 接缝切向 (≠ 块 rotation)。
void TestCircleAttach::fullCircleLeaderKeepsFollowerGeometry()
{
    ParamDocument doc;
    doc.setActiveLayer(cad::test::layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    const auto line = cad::test::makeLine(doc, 100.0);                       // (0,0)→(100,0)
    const auto circle = makeCircle(doc, 40.0, 0.0, Vec2(200.0, 0.0));        // 接缝 (240,0)
    doc.resolveAll();

    const double dirBefore = worldDirDeg(doc, line.blockId, line.startId);
    QVERIFY(dragConnect(doc, scene, line, Vec2(240.0, 0.0)));

    QCOMPARE(static_cast<int>(doc.attachments().size()), 1);
    const Attachment att = doc.attachments().front();
    QCOMPARE(att.fromBlockId, line.blockId);
    QCOMPARE(att.toBlockId, circle.blockId);
    QCOMPARE(att.toPointId, circle.startId);

    // ① 位置吸附: 跟随线起点落在圆周接缝上。
    const Block* fb = doc.findBlock(line.blockId);
    const Block* cb = doc.findBlock(circle.blockId);
    QVERIFY(fb && cb);
    QVERIFY(fb->worldPos(line.startId).distanceTo(Vec2(240.0, 0.0)) < 1e-6);

    // ② 整圆基准 = 接缝切向, 不再退化成块 rotation (旧公式的垃圾值)。
    const double refDeg =
        cad::geo::radToDeg(cad::param::effectiveAngleRefWorld(&doc, att));
    const double blockRotDeg = cad::geo::radToDeg(cb->transform.rotation);
    QVERIFY(std::abs(angleDiffDeg(refDeg, blockRotDeg)) > 1.0);

    // ③ 反算角与解析器同源 (连接路径 = effectiveAngleRefWorld)。
    const double expectAngle = cad::param::backSolveFollowerAngle(
        fb->transform.rotation, fb->directionAtPoint(line.startId),
        cad::geo::degToRad(refDeg));
    QVERIFY(std::abs(angleDiffDeg(att.followerAngle, expectAngle)) < 1e-9);

    // ④ 「连接不改变几何」: 跟随线方向不变 (旧公式在此跳变 90°)。
    QVERIFY(std::abs(angleDiffDeg(worldDirDeg(doc, line.blockId, line.startId),
                                  dirBefore)) < 1e-9);
}

/// 旋转圆块 → 跟随线保持相对角 (切线跟随能力), 且位置仍钉在接缝上。
void TestCircleAttach::fullCircleRotationRotatesFollower()
{
    ParamDocument doc;
    doc.setActiveLayer(cad::test::layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    const auto line = cad::test::makeLine(doc, 100.0);
    const auto circle = makeCircle(doc, 40.0, 0.0, Vec2(200.0, 0.0));
    doc.resolveAll();
    QVERIFY(dragConnect(doc, scene, line, Vec2(240.0, 0.0)));

    const double dirBefore = worldDirDeg(doc, line.blockId, line.startId);

    Block* cb = doc.findBlock(circle.blockId);
    QVERIFY(cb);
    cb->transform.rotation += cad::geo::degToRad(30.0);
    doc.resolveAll();

    // 接缝随块旋转 (绕圆心), 跟随线仍钉在接缝上。
    const Vec2 seamWorld(200.0 + 40.0 * std::cos(cad::geo::degToRad(30.0)),
                         40.0 * std::sin(cad::geo::degToRad(30.0)));
    const Block* fb = doc.findBlock(line.blockId);
    QVERIFY(fb);
    QVERIFY(fb->worldPos(line.startId).distanceTo(seamWorld) < 1e-6);

    // 切线基准随圆块旋转 30° ⇒ 跟随线方向也旋转 30°。
    QVERIFY(std::abs(angleDiffDeg(worldDirDeg(doc, line.blockId, line.startId),
                                  dirBefore) - 30.0) < 1e-6);
}

/// 圆弧宿主 (两端点不重合): 基准 = 母线弦, 与旧公式一致。
void TestCircleAttach::partialArcLeaderUsesChord()
{
    ParamDocument doc;
    doc.setActiveLayer(cad::test::layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    const auto line = cad::test::makeLine(doc, 100.0);
    const auto circle = makeCircle(doc, 40.0, 0.0, Vec2(200.0, 0.0));
    // 整圆 → 120° 弧: 终点角度 = a0 + 120 (绝不归一化)。
    Block* cb = doc.findBlock(circle.blockId);
    QVERIFY(cb && !cb->segments.empty());
    ParamPoint* ep = cb->findPoint(cb->segments.front().endPointId);
    QVERIFY(ep);
    ep->angle = 120.0;
    doc.resolveAll();

    const Vec2 seamWorld = cb->worldPos(circle.startId);
    const Vec2 endWorld  = cb->worldPos(circle.endId);
    const double chordDeg = cad::geo::radToDeg(
        std::atan2(endWorld.y - seamWorld.y, endWorld.x - seamWorld.x));

    const double dirBefore = worldDirDeg(doc, line.blockId, line.startId);
    QVERIFY(dragConnect(doc, scene, line, seamWorld));

    QCOMPARE(static_cast<int>(doc.attachments().size()), 1);
    const Attachment att = doc.attachments().front();
    QCOMPARE(att.toBlockId, circle.blockId);
    QVERIFY(std::abs(angleDiffDeg(
        cad::geo::radToDeg(cad::param::effectiveAngleRefWorld(&doc, att)),
        chordDeg)) < 1e-6);

    // 弧宿主同样不跳变。
    QVERIFY(std::abs(angleDiffDeg(worldDirDeg(doc, line.blockId, line.startId),
                                  dirBefore)) < 1e-9);
}

/// 直线宿主的存量行为: 基准 = 母线弦 (起点吸附也取 start→end, 不反向)。
void TestCircleAttach::straightLineLeaderStillUsesChord()
{
    ParamDocument doc;
    doc.setActiveLayer(cad::test::layerIdAt(doc, 1));
    CanvasScene scene(&doc);

    const auto line = cad::test::makeLine(doc, 100.0);
    const auto host = cad::test::makeLine(doc, 100.0, Vec2(200.0, 0.0));
    // 宿主竖直: (200,0)→(200,100)。
    doc.findBlock(host.blockId)->transform.rotation = cad::geo::degToRad(90.0);
    doc.resolveAll();

    const double dirBefore = worldDirDeg(doc, line.blockId, line.startId);
    QVERIFY(dragConnect(doc, scene, line, Vec2(200.0, 0.0)));

    QCOMPARE(static_cast<int>(doc.attachments().size()), 1);
    const Attachment att = doc.attachments().front();
    QCOMPARE(att.toBlockId, host.blockId);
    QCOMPARE(att.toPointId, host.startId);
    QVERIFY(std::abs(angleDiffDeg(
        cad::geo::radToDeg(cad::param::effectiveAngleRefWorld(&doc, att)),
        90.0)) < 1e-6);
    QVERIFY(std::abs(angleDiffDeg(worldDirDeg(doc, line.blockId, line.startId),
                                  dirBefore)) < 1e-9);
}

QTEST_MAIN(TestCircleAttach)
#include "test_circle_attach.moc"
