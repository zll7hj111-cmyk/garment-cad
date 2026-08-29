/// @file test_mode_indicator.cpp
/// W 键模式切换的持久化显示标识 (2026-08-29)。
///
/// W 是各工具统一的模式切换 leader key, 但切换后画布上没有任何常驻标识 ——
/// 旧实现只有一句 1.4 秒就消失的 toast, 用户按完 W 就再也看不出自己在哪个
/// 模式 (选择工具尤其致命: 多选下点空白是加选、单选下是清选, 模式错了直接
/// 就是误操作)。修复 = toast 之外补一层持久显示 (状态栏), 本测试锁四条契约:
/// ①按完 W, 状态栏必须立刻反映新模式 (持久层真的存在);
/// ②静态 describe().hintText 与激活后首条运行期提示逐字相同 (两处同源,
///   分开维护的话改一处另一处就会说谎);
/// ③三态循环要带位置 ("2/3"), 二态要给出目标态 ("W 切单选");
/// ④W 的语义随工具状态变化 —— 非瞄准态/画线中不能声称"W 切模式"。

#include <QtTest>
#include <QApplication>
#include <QKeyEvent>
#include <QStringList>

#include "canvas/CanvasScene.h"
#include "tools/ToolManager.h"
#include "tools/ToolRegistry.h"
#include "parametric/ParamDocument.h"
#include "TestHelpers.h"

#include <QGraphicsView>
#include <QScrollBar>
#include <QKeyEvent>
#include <QStringList>

using cad::tools::ToolType;
using cad::tools::toolHintText;

namespace {

/// 发一个真实 W 键到工具链路 (与 CanvasView → InputDispatcher → Tool 同路径)。
void pressW(cad::tools::ToolManager& tm)
{
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
    tm.dispatchKeyPress(&ev);
}

/// 最小环境: 文档 + 场景 + 视口 + 工具管理器, 并把状态栏提示逐条记进 hints。
///
/// 视口刻意用**普通 QGraphicsView** 而不是 CanvasView: 角标锚定只需要
/// `mapToScene`, 键盘走 ToolManager::dispatchKeyPress 根本不经视口 ——
/// 而 CanvasView 的 viewport 是 QOpenGLWidget (CanvasView.cpp:50), 每个用例
/// 建一个 = 5 个 GL 上下文, 整批 ctest 下会 SEGFAULT (与 AGENTS.md 记的
/// test_canvas_perf singleCurveFrame 段错误同源, 疑似环境/软渲染)。
/// 代价: zoomFactorChanged 那条重定位接线不在自动测试覆盖内 (它只在真实
/// CanvasView 上才有), 滚动这条覆盖了同样的"视口动了 → 重定位"机制。
///
/// 视口也不 show(): 窗口可见性对这两条链路都不必要, 测试因此快且稳
/// (GUI 测试要避免 qWaitForWindowExposed 这类固定等待, AGENTS.md P2-3)。
struct Fixture {
    cad::param::ParamDocument doc;
    CanvasScene               scene{&doc};
    QGraphicsView             view{&scene};
    cad::tools::ToolManager   tm{&scene};
    QStringList               hints;

    Fixture()
    {
        doc.setActiveLayer(cad::test::layerIdAt(doc, 1));
        view.resize(900, 600);
        QObject::connect(&tm, &cad::tools::ToolManager::hintOverrideChanged,
                         [this](const QString& h) { hints << h; });
        // 默认激活选择工具 —— 顺带触发第一条提示。
        tm.setParamDocument(&doc);
    }

    [[nodiscard]] QString last() const
    { return hints.isEmpty() ? QString() : hints.last(); }
};

} // namespace

class TestModeIndicator : public QObject
{
    Q_OBJECT

private slots:
    void staticHintMatchesRuntimeHintOnActivate();
    void selectTogglePersistsInStatusBar();
    void measureCycleReportsPosition();
    void intersectionWActionIsContextual();
    void smartPenDartModePersists();
    // ── L2 画布角标 (P2) ──
    void canvasBadgeShowsOnlyForNonDefaultMode();
    void canvasBadgeFollowsViewportScroll();
};

void TestModeIndicator::staticHintMatchesRuntimeHintOnActivate()
{
    // 静态文案与运行期提示同源 —— describe() 用各自 modeIndicatorFor 的
    // 默认态组装, 二者必须逐字相同。这条一红, 就意味着有人改了一处忘了
    // 另一处, 用户会在"切工具瞬间"看到状态栏跳变。
    // 一个场景反复切工具即可 —— 为每个工具重建 CanvasScene 是浪费, 也会
    // 反复初始化场景级全局状态。
    Fixture f;
    const ToolType withMode[] = { ToolType::Select, ToolType::SmartPen,
                                  ToolType::Measure, ToolType::Intersection };
    for (ToolType t : withMode) {
        f.tm.switchTool(t);
        QVERIFY2(!f.last().isEmpty(),
                 qPrintable(QStringLiteral("activating a tool must report a hint (%1)")
                            .arg(int(t))));
        QCOMPARE(f.last(), toolHintText(t));
    }
}

void TestModeIndicator::selectTogglePersistsInStatusBar()
{
    Fixture f;   // 默认激活选择工具
    QVERIFY2(f.last().contains(QString::fromUtf8("[单选]")), qPrintable(f.last()));

    pressW(f.tm);
    QVERIFY2(f.last().contains(QString::fromUtf8("[多选]")), qPrintable(f.last()));
    // 二态给目标态而不是"W 切换单选/多选": 用户要知道的是"按下去会变成什么"。
    QVERIFY2(f.last().contains(QString::fromUtf8("W 切单选")), qPrintable(f.last()));

    pressW(f.tm);
    QVERIFY2(f.last().contains(QString::fromUtf8("[单选]")), qPrintable(f.last()));
}

void TestModeIndicator::measureCycleReportsPosition()
{
    Fixture f;
    f.tm.switchTool(ToolType::Measure);
    QVERIFY2(f.last().contains(QString::fromUtf8("1/3")), qPrintable(f.last()));

    // 三态循环带位置感的理由: 二态按错了再按一次就回来, 三态按错了要按
    // 两次, 而且不知道自己停在第几个。
    pressW(f.tm);
    QVERIFY2(f.last().contains(QString::fromUtf8("[水平]")), qPrintable(f.last()));
    QVERIFY2(f.last().contains(QString::fromUtf8("2/3")), qPrintable(f.last()));

    pressW(f.tm);
    QVERIFY2(f.last().contains(QString::fromUtf8("[垂直]")), qPrintable(f.last()));
    QVERIFY2(f.last().contains(QString::fromUtf8("3/3")), qPrintable(f.last()));

    pressW(f.tm);   // 回绕
    QVERIFY2(f.last().contains(QString::fromUtf8("1/3")), qPrintable(f.last()));
}

void TestModeIndicator::intersectionWActionIsContextual()
{
    Fixture f;
    f.tm.switchTool(ToolType::Intersection);
    // 未进入瞄准态时 W 切不动角度基准 —— 提示必须说清"瞄准中才能切",
    // 否则用户按了没反应只会以为键坏了。
    QVERIFY2(!f.last().contains(QString::fromUtf8("W 切跟随角度")), qPrintable(f.last()));
    QVERIFY2(!f.last().contains(QString::fromUtf8("W 切绝对角度")), qPrintable(f.last()));
    QVERIFY2(f.last().contains(QString::fromUtf8("瞄准中")), qPrintable(f.last()));
}

void TestModeIndicator::smartPenDartModePersists()
{
    Fixture f;
    f.tm.switchTool(ToolType::SmartPen);
    QVERIFY2(f.last().contains(QString::fromUtf8("[直线]")), qPrintable(f.last()));

    pressW(f.tm);
    QVERIFY2(f.last().contains(QString::fromUtf8("[省道线]")), qPrintable(f.last()));
    // 省道线的操作序列与直线完全不同 (起点必须吸附已有点), 模式名必须在
    // 状态栏常驻 —— 只看一眼 toast 是不够的。
    QVERIFY2(f.last().contains(QString::fromUtf8("W 切直线")), qPrintable(f.last()));
}

void TestModeIndicator::canvasBadgeShowsOnlyForNonDefaultMode()
{
    // L2 角标的经济学: 默认态零像素成本, 只有切到非常驻态才占地方 ——
    // 默认态不需要提示, 需要提示的是"我已经不在默认态了"。
    Fixture f;
    QCOMPARE(f.scene.modeBadgeText(), QString());   // 单选 = 默认态

    pressW(f.tm);
    QCOMPARE(f.scene.modeBadgeText(), QString::fromUtf8("多选"));

    pressW(f.tm);                                   // 回到默认态 → 撤下
    QCOMPARE(f.scene.modeBadgeText(), QString());

    // 角标归**场景**所有而非工具所有 —— 切工具时必须显式撤下, 否则上一个
    // 工具的「多选」会一直挂在画布上 (Tool::deactivate 负责这件事)。
    pressW(f.tm);
    QVERIFY(!f.scene.modeBadgeText().isEmpty());
    f.tm.switchTool(ToolType::Measure);
    QCOMPARE(f.scene.modeBadgeText(), QString());

    // 测量切到非常驻态也挂角标 (距离是默认态)。
    pressW(f.tm);
    QCOMPARE(f.scene.modeBadgeText(), QString::fromUtf8("水平"));
}

void TestModeIndicator::canvasBadgeFollowsViewportScroll()
{
    // 角标"钉在视口左上角", 但它是场景图元 —— 视口一滚动, 同一屏幕位置
    // 对应的场景坐标就变了, 必须显式重定位 (toast 是瞬态, 漂移无所谓;
    // 常驻的不行)。这条锁的就是 connectModeBadgeViewSignals 的滚动接线;
    // 缩放接线 (CanvasView::zoomFactorChanged) 需要真实 CanvasView, 为避开
    // GL 上下文压力不在这里覆盖 —— 见 Fixture 注释。
    Fixture f;
    f.scene.setSceneRect(-2000, -2000, 4000, 4000);   // 场景远大于视口 → 可滚动
    f.view.resize(400, 300);

    pressW(f.tm);
    QVERIFY2(f.scene.modeBadgeText() == QString::fromUtf8("多选"),
             qPrintable(f.scene.modeBadgeText()));

    QScrollBar* vsb = f.view.verticalScrollBar();
    QVERIFY2(vsb->maximum() > 0, "viewport must be scrollable for this assertion");

    const QPointF before = f.scene.modeBadgeScenePos();
    vsb->setValue(vsb->value() + 40);
    const QPointF after = f.scene.modeBadgeScenePos();
    QVERIFY2((after - before).manhattanLength() > 1.0,
             qPrintable(QStringLiteral("badge must follow the viewport; stayed at %1,%2")
                        .arg(after.x()).arg(after.y())));
}

QTEST_MAIN(TestModeIndicator)
#include "test_mode_indicator.moc"
