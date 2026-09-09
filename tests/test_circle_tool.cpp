/// @file test_circle_tool.cpp
/// Drives ToolCircle's two draw modes end-to-end
/// (docs/design/CIRCLE_TOOL_DESIGN.md §5.1/§5.2, D5):
///   · 圆心+半径：press 落圆心 → drag 预览 → release 提交；
///   · 两点直径：press A（Armed）→ press B → 圆心 = AB 中点，半径 = |AB|/2；
///   · W 在两种模式间切换，且**切换即取消进行中的手势**（两类手势的锚点语义
///     不同，跨模式续命会把"刚落的圆心"当成"直径首点"）；
///   · Esc / 右键 取消手势不留下半个圆；
///   · 静态 describe().hintText 与运行期默认态提示逐字同源。
///
/// 这是 ToolCircle 的第一条驱动测试 —— 此前 tests/test_circle_*.cpp 都直接调
/// CircleFactory，工具层的手势/模式逻辑（含 W 键）无人覆盖。

#include <QtTest>
#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QStringList>

#include "canvas/CanvasScene.h"
#include "parametric/Block.h"
#include "parametric/ParamDocument.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "tools/ToolCircle.h"
#include "tools/ToolManager.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::tools::CircleMode;
using cad::tools::ToolCircle;

namespace {

/// 状态栏工具名（ModeIndicator::hint 的第一个参数，与 describe().hintText 同源）。
const char* const kCircleName = reinterpret_cast<const char*>(u8"画圆");

/// 无头 ToolHost 桩：只记录状态栏提示 (L1)。画布角标 (L2) 在没有视口的场景里
/// 是 no-op —— CanvasScene.cpp:362 明写"无头场合没有视口可锚定, 角标无从谈起",
/// 角标机制本身由 test_mode_indicator 覆盖。
struct HostStub : cad::tools::ToolHost
{
    QStringList hints;

    // ── 绘制会话上报 (一期补充, §5.5) ──
    bool   circleSessionActive = false;
    int    circleSessionStarts = 0;
    int    circleSessionEnds   = 0;
    int    circleValueReports  = 0;
    double lastRadiusCm        = -1.0;
    bool   lastRadiusLocked    = false;

    void requestToolSwitch(cad::tools::ToolType) override {}
    void setHintOverride(const QString& hint) override { hints << hint; }
    void setCircleSession(bool active) override
    {
        circleSessionActive = active;
        if (active) ++circleSessionStarts;
        else        ++circleSessionEnds;
    }
    void updateCircleSession(double radiusCm, bool locked) override
    {
        lastRadiusCm     = radiusCm;
        lastRadiusLocked = locked;
        ++circleValueReports;
    }

    [[nodiscard]] QString last() const
    { return hints.isEmpty() ? QString() : hints.last(); }
};

/// 最小环境：文档 + 场景 + 文档自带 undo 栈（工具提交走它，便于断言单步 undo）。
struct Fixture {
    ParamDocument doc;
    CanvasScene scene{&doc};
    HostStub host;

    Fixture() { doc.setActiveLayer(cad::test::layerIdAt(doc, 1)); }

    [[nodiscard]] cad::tools::ToolContext ctx()
    {
        cad::tools::ToolContext c;
        c.scene = &scene;
        c.paramDoc = &doc;
        c.undoStack = doc.undoStack();
        c.host = &host;
        return c;
    }
};

void press(ToolCircle& t, double x, double y)
{
    QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMousePress);
    e.setScenePos(QPointF(x, y));
    e.setButton(Qt::LeftButton);
    e.setButtons(Qt::LeftButton);
    t.mousePress(&e);
}

void move(ToolCircle& t, double x, double y)
{
    QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMouseMove);
    e.setScenePos(QPointF(x, y));
    t.mouseMove(&e);
}

void release(ToolCircle& t, double x, double y)
{
    QGraphicsSceneMouseEvent e(QEvent::GraphicsSceneMouseRelease);
    e.setScenePos(QPointF(x, y));
    e.setButton(Qt::LeftButton);
    e.setButtons(Qt::NoButton);
    t.mouseRelease(&e);
}

void pressW(ToolCircle& t)
{
    QKeyEvent e(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
    t.keyPress(&e);
}

void pressEsc(ToolCircle& t)
{
    QKeyEvent e(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    t.keyPress(&e);
}

void pressEnter(ToolCircle& t)
{
    QKeyEvent e(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    t.keyPress(&e);
}

int circleCount(const ParamDocument& doc)
{
    int n = 0;
    for (const Block& b : doc.blocks())
        for (const Segment& s : b.segments)
            if (s.fitKind == FitKind::Circle) ++n;
    return n;
}

/// 唯一的圆段视图（调用方保证 doc 里恰好一个）。
struct CircleView {
    const Block* block = nullptr;
    const Segment* seg = nullptr;
    const ParamPoint* start = nullptr;
    const ParamPoint* end = nullptr;
};

CircleView onlyCircle(const ParamDocument& doc)
{
    for (const Block& b : doc.blocks()) {
        for (const Segment& s : b.segments) {
            if (s.fitKind != FitKind::Circle) continue;
            return CircleView{&b, &s, b.findPoint(s.startPointId),
                              b.findPoint(s.endPointId)};
        }
    }
    return {};
}

} // namespace

class TestCircleTool : public QObject
{
    Q_OBJECT

private slots:
    void centerRadiusDragCreatesCircle();
    void bareClickKeepsSessionOpen();
    void staticHintTextMatchesRuntimeDefault();
    void wTogglesDiameterMode();
    void twoPointDiameterCreatesCircle();
    void diameterSamePointCreatesNothing();
    void wToggleCancelsInFlightGesture();
    void escapeCancelsInFlightGesture();
    // 绘制会话 (一期补充, CIRCLE_TOOL_DESIGN.md §5.5)
    void centerPressOpensSessionAndReportsRadius();
    void stripRadiusLocksPreviewAndEnterCommits();
    void secondClickCommitsSession();
    void stripCancelClosesSessionWithoutCircle();
    void stripCommitUsesLockedRadius();
    void diameterModeIgnoresStripInput();
    // 落圆后上报宿主 (一期补充②, CIRCLE_TOOL_DESIGN.md §5.6)
    void commitReportsCreatedCircleToHost();
};

void TestCircleTool::centerRadiusDragCreatesCircle()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());

    press(tool, 0.0, 0.0);            // 落圆心
    move(tool, 30.0, 40.0);           // 3-4-5 → r = 50
    QCOMPARE(tool.modeIndicator().modeName, QString::fromUtf8("拖半径"));
    release(tool, 30.0, 40.0);

    QCOMPARE(circleCount(f.doc), 1);
    const CircleView c = onlyCircle(f.doc);
    QVERIFY(c.block && c.seg && c.start && c.end);
    QCOMPARE(c.seg->fitKind, FitKind::Circle);

    // 圆心 = press 处（块原点），半径 = 拖出距离。
    QVERIFY2(std::abs(c.block->transform.origin.x) < 1e-9
                 && std::abs(c.block->transform.origin.y) < 1e-9,
             qPrintable(QStringLiteral("center = %1,%2")
                            .arg(c.block->transform.origin.x)
                            .arg(c.block->transform.origin.y)));
    const ParamPoint* center = c.block->findPoint(c.start->refPointId);
    QVERIFY(center);
    QVERIFY2(center->resolvedPos.length() < 1e-9, "center point must stay at the block origin");
    QVERIFY2(std::abs(c.start->distance - 50.0) < 1e-9,
             qPrintable(QStringLiteral("radius = %1").arg(c.start->distance)));
    QVERIFY2(std::abs(c.start->resolvedPos.x - 50.0) < 1e-9
                 && std::abs(c.start->resolvedPos.y) < 1e-9,
             qPrintable(QStringLiteral("seam = %1,%2")
                            .arg(c.start->resolvedPos.x).arg(c.start->resolvedPos.y)));

    // 提交后回到 Idle 的默认态，且整个手势 = 一条 undo 命令。
    QVERIFY(tool.modeIndicator().isDefault);
    QCOMPARE(tool.modeIndicator().modeName, QString::fromUtf8("圆心"));
    QCOMPARE(f.doc.undoStack()->count(), 1);
    f.doc.undoStack()->undo();
    QCOMPARE(circleCount(f.doc), 0);
    f.doc.undoStack()->redo();
    QCOMPARE(circleCount(f.doc), 1);
}

void TestCircleTool::bareClickKeepsSessionOpen()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());

    press(tool, 10.0, 10.0);
    release(tool, 10.0, 10.0);        // 半径 0 → 不生成退化圆
    QCOMPARE(circleCount(f.doc), 0);
    // 裸点击不再丢弃会话 (一期补充): 圆心已落, 条带接着输半径就能成圆。
    QVERIFY(!tool.modeIndicator().isDefault);
    QCOMPARE(tool.modeIndicator().modeName, QString::fromUtf8("拖半径"));
    QVERIFY(f.host.circleSessionActive);
    QCOMPARE(f.host.circleSessionStarts, 1);
    QCOMPARE(f.doc.undoStack()->count(), 0);

    pressEsc(tool);                   // Esc 才收掉会话
    QVERIFY(tool.modeIndicator().isDefault);
    QVERIFY(!f.host.circleSessionActive);
    QCOMPARE(f.host.circleSessionEnds, 1);
    QCOMPARE(circleCount(f.doc), 0);
}

void TestCircleTool::staticHintTextMatchesRuntimeDefault()
{
    // 静态文案与运行期首条提示必须逐字同源（test_mode_indicator 的同一条契约，
    // 这里覆盖 Circle —— 它不在那边的 withMode[] 列表里）。
    QCOMPARE(ToolCircle::describe().hintText,
             ToolCircle::modeIndicatorFor(CircleMode::CenterRadius, false).hint(kCircleName));
    QVERIFY(ToolCircle::describe().hintText.startsWith(QString::fromUtf8("画圆")));

    // 直径模式是非常驻态 → 画布角标要出现；圆心默认态 → 不占像素。
    const cad::tools::ModeIndicator dia =
        ToolCircle::modeIndicatorFor(CircleMode::TwoPointDiameter, false);
    QCOMPARE(dia.modeName, QString::fromUtf8("直径"));
    QVERIFY(!dia.isDefault);
    QVERIFY(dia.wAction.contains(QString::fromUtf8("W 切圆心")));
    QVERIFY(ToolCircle::modeIndicatorFor(CircleMode::CenterRadius, false).isDefault);
}

void TestCircleTool::wTogglesDiameterMode()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());
    QCOMPARE(tool.mode(), CircleMode::CenterRadius);

    pressW(tool);
    QCOMPARE(tool.mode(), CircleMode::TwoPointDiameter);
    QCOMPARE(tool.modeIndicator().modeName, QString::fromUtf8("直径"));
    QVERIFY(!tool.modeIndicator().isDefault);
    // L1 状态栏 (ModeIndicator::hint)：模式名进方括号，W 的下一步动作跟着变。
    QVERIFY2(f.host.last().contains(QString::fromUtf8("[直径]")), qPrintable(f.host.last()));
    QVERIFY2(f.host.last().contains(QString::fromUtf8("W 切圆心")), qPrintable(f.host.last()));

    pressW(tool);
    QCOMPARE(tool.mode(), CircleMode::CenterRadius);
    QCOMPARE(tool.modeIndicator().modeName, QString::fromUtf8("圆心"));
    QVERIFY(tool.modeIndicator().isDefault);
    QVERIFY2(f.host.last().contains(QString::fromUtf8("[圆心]")), qPrintable(f.host.last()));
    QVERIFY2(f.host.last().contains(QString::fromUtf8("W 切直径")), qPrintable(f.host.last()));
}

void TestCircleTool::twoPointDiameterCreatesCircle()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());
    pressW(tool);                     // → 两点直径

    press(tool, 10.0, 10.0);          // 首点 A（Armed）
    QVERIFY2(tool.modeIndicator().detail.contains(QString::fromUtf8("第二点")),
             qPrintable(tool.modeIndicator().detail));
    press(tool, 10.0, 70.0);          // 第二点 B → 提交

    QCOMPARE(circleCount(f.doc), 1);
    const CircleView c = onlyCircle(f.doc);
    QVERIFY(c.block && c.start);
    // 圆心 = AB 中点，半径 = |AB|/2 = 30。
    QVERIFY2(std::abs(c.block->transform.origin.x - 10.0) < 1e-9
                 && std::abs(c.block->transform.origin.y - 40.0) < 1e-9,
             qPrintable(QStringLiteral("center = %1,%2")
                            .arg(c.block->transform.origin.x)
                            .arg(c.block->transform.origin.y)));
    QVERIFY2(std::abs(c.start->distance - 30.0) < 1e-9,
             qPrintable(QStringLiteral("radius = %1").arg(c.start->distance)));

    // 提交在第二次 press 上完成：一次点击序列仍是一条命令，且模式保持直径。
    QCOMPARE(f.doc.undoStack()->count(), 1);
    QCOMPARE(tool.modeIndicator().modeName, QString::fromUtf8("直径"));
    QVERIFY(!tool.modeIndicator().isDefault);
    QCOMPARE(tool.modeIndicator().detail, QString::fromUtf8("点选两点定直径"));
}

void TestCircleTool::diameterSamePointCreatesNothing()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());
    pressW(tool);

    press(tool, 20.0, 20.0);
    press(tool, 20.0, 20.0);          // 双击同点 → 直径 0，忽略
    QCOMPARE(circleCount(f.doc), 0);
    QCOMPARE(f.doc.undoStack()->count(), 0);
    QVERIFY(!tool.modeIndicator().isDefault);   // 仍停在直径模式
}

void TestCircleTool::wToggleCancelsInFlightGesture()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());

    pressW(tool);                     // → 直径
    press(tool, 0.0, 0.0);            // A 已落，等第二点
    pressW(tool);                     // 切回圆心 → A 必须被丢弃（不能续命成圆心）
    QCOMPARE(tool.mode(), CircleMode::CenterRadius);
    QVERIFY2(tool.modeIndicator().isDefault, "switching modes must drop the in-flight gesture");

    press(tool, 100.0, 100.0);        // 新会话：圆心 (100,100)
    move(tool, 100.0, 140.0);         // r = 40
    release(tool, 100.0, 140.0);

    QCOMPARE(circleCount(f.doc), 1);
    const CircleView c = onlyCircle(f.doc);
    QVERIFY(c.block && c.start);
    QVERIFY2(std::abs(c.block->transform.origin.x - 100.0) < 1e-9
                 && std::abs(c.block->transform.origin.y - 100.0) < 1e-9,
             qPrintable(QStringLiteral("stale anchor leaked: center = %1,%2")
                            .arg(c.block->transform.origin.x)
                            .arg(c.block->transform.origin.y)));
    QVERIFY(std::abs(c.start->distance - 40.0) < 1e-9);
}

void TestCircleTool::escapeCancelsInFlightGesture()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());

    press(tool, 5.0, 5.0);
    move(tool, 45.0, 5.0);
    pressEsc(tool);
    QVERIFY(tool.modeIndicator().isDefault);
    QCOMPARE(tool.modeIndicator().modeName, QString::fromUtf8("圆心"));

    release(tool, 45.0, 5.0);         // 手势已取消 → release 不得提交
    QCOMPARE(circleCount(f.doc), 0);
    QCOMPARE(f.doc.undoStack()->count(), 0);
}

void TestCircleTool::centerPressOpensSessionAndReportsRadius()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());
    QVERIFY(!f.host.circleSessionActive);

    press(tool, 0.0, 0.0);            // 落圆心 → 条带进绘制态
    QVERIFY(f.host.circleSessionActive);
    QCOMPARE(f.host.circleSessionStarts, 1);

    move(tool, 30.0, 40.0);           // r = 50mm → 条带实时读数 5.00cm
    QVERIFY(f.host.circleValueReports > 0);
    QVERIFY2(std::abs(f.host.lastRadiusCm - 5.0) < 1e-9,
             qPrintable(QStringLiteral("radiusCm = %1").arg(f.host.lastRadiusCm)));
    QVERIFY(!f.host.lastRadiusLocked);

    release(tool, 30.0, 40.0);        // 拖动落圆 → 会话自动收起
    QCOMPARE(circleCount(f.doc), 1);
    QVERIFY(!f.host.circleSessionActive);
    QCOMPARE(f.host.circleSessionEnds, 1);
}

void TestCircleTool::stripRadiusLocksPreviewAndEnterCommits()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());

    press(tool, 0.0, 0.0);
    move(tool, 30.0, 40.0);           // 光标给出 r = 50mm
    tool.circleRadiusInput(2.0, true);   // 条带输入 2cm → 锁定
    QVERIFY(f.host.lastRadiusLocked);
    QVERIFY2(std::abs(f.host.lastRadiusCm - 2.0) < 1e-9,
             qPrintable(QStringLiteral("radiusCm = %1").arg(f.host.lastRadiusCm)));

    move(tool, 500.0, 500.0);         // 锁定后光标不得再改半径
    QVERIFY2(std::abs(f.host.lastRadiusCm - 2.0) < 1e-9,
             qPrintable(QStringLiteral("locked radius leaked: %1").arg(f.host.lastRadiusCm)));

    pressEnter(tool);
    QCOMPARE(circleCount(f.doc), 1);
    const CircleView c = onlyCircle(f.doc);
    QVERIFY(c.block && c.start);
    QVERIFY2(std::abs(c.start->distance - 20.0) < 1e-9,
             qPrintable(QStringLiteral("radius = %1").arg(c.start->distance)));
    QVERIFY2(std::abs(c.block->transform.origin.x) < 1e-9
                 && std::abs(c.block->transform.origin.y) < 1e-9, "center must be the press point");
    QCOMPARE(f.doc.undoStack()->count(), 1);
    QVERIFY(!f.host.circleSessionActive);
    QVERIFY(tool.modeIndicator().isDefault);
}

void TestCircleTool::secondClickCommitsSession()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());

    press(tool, 10.0, 10.0);
    move(tool, 10.0, 50.0);           // r = 40mm
    press(tool, 10.0, 50.0);          // 会话中再次左键 = 落圆 (点击-点击画法)

    QCOMPARE(circleCount(f.doc), 1);
    const CircleView c = onlyCircle(f.doc);
    QVERIFY(c.start);
    QVERIFY2(std::abs(c.start->distance - 40.0) < 1e-9,
             qPrintable(QStringLiteral("radius = %1").arg(c.start->distance)));
    QCOMPARE(f.doc.undoStack()->count(), 1);
    QVERIFY(!f.host.circleSessionActive);
}

void TestCircleTool::stripCancelClosesSessionWithoutCircle()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());

    press(tool, 0.0, 0.0);
    move(tool, 60.0, 0.0);
    tool.circleCancelled();           // 条带 Esc

    QCOMPARE(circleCount(f.doc), 0);
    QCOMPARE(f.doc.undoStack()->count(), 0);
    QVERIFY(!f.host.circleSessionActive);
    QVERIFY(tool.modeIndicator().isDefault);

    release(tool, 60.0, 0.0);         // 会话已收 → release 不得补交
    QCOMPARE(circleCount(f.doc), 0);
}

void TestCircleTool::stripCommitUsesLockedRadius()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());

    press(tool, -5.0, -5.0);
    tool.circleRadiusInput(3.0, true);   // 条带输入 3cm，无拖动
    tool.circleCommitted();              // 条带 Enter

    QCOMPARE(circleCount(f.doc), 1);
    const CircleView c = onlyCircle(f.doc);
    QVERIFY(c.start);
    QVERIFY2(std::abs(c.start->distance - 30.0) < 1e-9,
             qPrintable(QStringLiteral("radius = %1").arg(c.start->distance)));
    QVERIFY2(std::abs(c.block->transform.origin.x + 5.0) < 1e-9
                 && std::abs(c.block->transform.origin.y + 5.0) < 1e-9,
             "center must stay at the pressed point");
    QVERIFY(!f.host.circleSessionActive);
}

void TestCircleTool::diameterModeIgnoresStripInput()
{
    Fixture f;
    ToolCircle tool;
    tool.activate(f.ctx());
    pressW(tool);                     // → 两点直径

    press(tool, 20.0, 20.0);          // A 已落 (Armed)
    // 直径模式单值无法定圆 → 不开会话, 条带输入也不得改半径 (§5.5 有意偏差)。
    QVERIFY(!f.host.circleSessionActive);
    QCOMPARE(f.host.circleSessionStarts, 0);
    tool.circleRadiusInput(9.0, true);
    tool.circleCommitted();

    QCOMPARE(circleCount(f.doc), 0);
    QCOMPARE(f.doc.undoStack()->count(), 0);
    QVERIFY(!tool.modeIndicator().isDefault);   // 仍停在直径模式的 Armed 态
}

/// 落圆后必须走宿主通道上报 (一期补充②)：与智能笔同一条
/// CanvasScene::lineCreated → pinCreatedLine，条带才能据 fitKind 路由到圆
/// 专属条带。三种提交路径（拖拽 / 回车 / 两点直径）都要覆盖。
void TestCircleTool::commitReportsCreatedCircleToHost()
{
    Fixture f;
    int created = 0;
    QUuid seenBlock, seenSeg;
    QObject::connect(&f.scene, &CanvasScene::lineCreated,
                     [&](const QUuid& b, const QUuid& s) {
                         ++created;
                         seenBlock = b;
                         seenSeg   = s;
                     });

    ToolCircle tool;
    tool.activate(f.ctx());

    press(tool, 0.0, 0.0);
    move(tool, 30.0, 40.0);
    release(tool, 30.0, 40.0);         // 拖拽落圆

    QCOMPARE(created, 1);
    const CircleView c = onlyCircle(f.doc);
    QVERIFY(c.block && c.seg);
    QCOMPARE(seenBlock, c.block->id);
    QCOMPARE(seenSeg, c.seg->id);

    // 回车落圆同样上报 (半径 2 cm, 无拖动)。
    press(tool, 200.0, 0.0);
    tool.circleRadiusInput(2.0, true);
    pressEnter(tool);
    QCOMPARE(created, 2);
    QVERIFY(seenBlock != c.block->id);

    // 两点直径模式同样上报 (一条通道, 不复制)。
    pressW(tool);
    press(tool, 400.0, 0.0);
    press(tool, 400.0, 80.0);
    QCOMPARE(created, 3);

    // 退化半径 (双击同点) 不得上报。
    press(tool, 600.0, 0.0);
    press(tool, 600.0, 0.0);
    QCOMPARE(created, 3);
}

QTEST_MAIN(TestCircleTool)
#include "test_circle_tool.moc"
