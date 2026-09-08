#include <QtTest>
#include <QApplication>
#include <QGraphicsSceneMouseEvent>

#include "canvas/CanvasScene.h"
#include "canvas/overlay/TransientOverlay.h"
#include "parametric/ParamDocument.h"
#include "tools/ToolManager.h"
#include "geometry/Units.h"

using namespace cad::canvas;
using namespace cad::tools;
using namespace cad::param;

class TestTransientOverlay : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
    }

    void testBasicVisibilityAndLifecycle()
    {
        ParamDocument doc;
        CanvasScene scene(&doc);
        auto* overlay = scene.overlay();
        QVERIFY(overlay != nullptr);

        // 初始状态：全部不可见
        QCOMPARE(overlay->isEndpointHoverVisible(), false);
        QCOMPARE(overlay->isSnapAimVisible(), false);
        QCOMPARE(overlay->isMarkerCrossVisible(), false);
        QCOMPARE(overlay->isRotateGizmoVisible(), false);

        // 1. 展示 Tier 1: Hover 图元
        overlay->showEndpointHover(cad::geo::Vec2(100.0, 50.0));
        overlay->showSnapAim(cad::geo::Vec2(120.0, 60.0));
        overlay->showMarkerCross(cad::geo::Vec2(80.0, 40.0), QColor(255, 0, 0));

        QCOMPARE(overlay->isEndpointHoverVisible(), true);
        QCOMPARE(overlay->isSnapAimVisible(), true);
        QCOMPARE(overlay->isMarkerCrossVisible(), true);

        // 2. 展示 Tier 3: RotateGizmo
        overlay->showRotateGizmo(cad::geo::Vec2(0.0, 0.0), 0.0, 1.57);
        QCOMPARE(overlay->isRotateGizmoVisible(), true);

        // 3. 执行 clear(OverlayTier::Hover) -> Hover 应该隐藏，Session (Gizmo) 必须依然保持可见！
        overlay->clear(OverlayTier::Hover);
        QCOMPARE(overlay->isEndpointHoverVisible(), false);
        QCOMPARE(overlay->isSnapAimVisible(), false);
        QCOMPARE(overlay->isMarkerCrossVisible(), false);
        QCOMPARE(overlay->isRotateGizmoVisible(), true);

        // 4. 执行 clear(OverlayTier::Session) -> Gizmo 隐藏
        overlay->clear(OverlayTier::Session);
        QCOMPARE(overlay->isRotateGizmoVisible(), false);

        // 5. 重新展示并测试 clearAll
        overlay->showEndpointHover(cad::geo::Vec2(10.0, 20.0));
        overlay->showRotateGizmo(cad::geo::Vec2(0.0, 0.0), 0.0, 1.0);
        QCOMPARE(overlay->isEndpointHoverVisible(), true);
        QCOMPARE(overlay->isRotateGizmoVisible(), true);

        overlay->clearAll();
        QCOMPARE(overlay->isEndpointHoverVisible(), false);
        QCOMPARE(overlay->isRotateGizmoVisible(), false);
    }

    void testCoordinateAndAngleMapping()
    {
        ParamDocument doc;
        CanvasScene scene(&doc);
        auto* overlay = scene.overlay();

        // 世界坐标 (50, 100) -> 经过 Coord::toScene 应当 Y 轴取负
        cad::geo::Vec2 worldPt(50.0, 100.0);
        QPointF expectedScenePt = cad::geo::Coord::toScene(worldPt.x, worldPt.y);
        QCOMPARE(expectedScenePt.x(), 50.0);
        QCOMPARE(expectedScenePt.y(), -100.0);

        overlay->showEndpointHover(worldPt, ScreenPx(8.0));
        QCOMPARE(overlay->isEndpointHoverVisible(), true);
    }

    void testToolManagerMousePressLifecycleBus()
    {
        ParamDocument doc;
        CanvasScene scene(&doc);
        ToolManager toolMgr(&scene);
        toolMgr.setParamDocument(&doc);

        auto* overlay = scene.overlay();
        QVERIFY(overlay != nullptr);

        // 先让 overlay 显示 Hover 青圈
        overlay->showEndpointHover(cad::geo::Vec2(20.0, 30.0));
        QCOMPARE(overlay->isEndpointHoverVisible(), true);

        // 模拟鼠标按下 dispatchMousePress
        QGraphicsSceneMouseEvent pressEvt(QEvent::GraphicsSceneMousePress);
        pressEvt.setScenePos(QPointF(20.0, -30.0));
        pressEvt.setButton(Qt::LeftButton);
        pressEvt.setButtons(Qt::LeftButton);

        toolMgr.dispatchMousePress(&pressEvt);

        // 核心契约验证：鼠标按下的瞬间，生命周期总线必须无条件原子清空 Hover！
        QCOMPARE(overlay->isEndpointHoverVisible(), false);
    }

    void testToolManagerSwitchToolClearsAll()
    {
        ParamDocument doc;
        CanvasScene scene(&doc);
        ToolManager toolMgr(&scene);
        toolMgr.setParamDocument(&doc);

        auto* overlay = scene.overlay();

        overlay->showEndpointHover(cad::geo::Vec2(10.0, 10.0));
        overlay->showRotateGizmo(cad::geo::Vec2(0.0, 0.0), 0.0, 1.0);
        QCOMPARE(overlay->isEndpointHoverVisible(), true);
        QCOMPARE(overlay->isRotateGizmoVisible(), true);

        // 切换到智能笔工具
        toolMgr.switchTool(ToolType::SmartPen);

        // 核心契约验证：切换工具后必须全清
        QCOMPARE(overlay->isEndpointHoverVisible(), false);
        QCOMPARE(overlay->isRotateGizmoVisible(), false);
    }

    void testRotateGizmoBadgeAndBullseye()
    {
        ParamDocument doc;
        CanvasScene scene(&doc);
        auto* overlay = scene.overlay();
        QVERIFY(overlay != nullptr);

        // 验证带有度数徽标与专业靶心的调用
        overlay->showRotateGizmo(cad::geo::Vec2(20.0, 30.0), 0.0, 0.0, QStringLiteral("45.0°"));
        QCOMPARE(overlay->isRotateGizmoVisible(), true);

        // 隐藏旋转手柄
        overlay->hideRotateGizmo();
        QCOMPARE(overlay->isRotateGizmoVisible(), false);
    }
};

QTEST_MAIN(TestTransientOverlay)
#include "test_transient_overlay.moc"
