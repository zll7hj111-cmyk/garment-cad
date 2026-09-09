// End-to-end regression test for ToolCurveEdit's Alt+drag tangent-handle
// unlock (P3-1, CURVE_P3_DESIGN.md):
//   - Alt+drag a tangent handle = corner mode (tangentLocked → false PERSISTENTLY).
//   - Plain drag keeps the lock; the opposite handle mirrors collinearly while
//     preserving its own length (existing 平滑不等长 semantics).
//   - The undo command snapshots tangentLocked, so undo restores the pre-drag
//     lock state and the dragged tangents, redo re-applies the corner mode.
// The full interaction is driven by REAL events (viewport → CanvasView →
// ToolManager → ToolCurveEdit): Ctrl+click places the curve points on a
// straight line, a plain click on the anchor shows its tangent handles, and a
// handle press+drag+release (with/without Alt) edits the tangents.
#include <QtTest>
#include <QApplication>
#include <QMouseEvent>
#include <QUndoStack>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "tools/ToolManager.h"
#include "tools/ToolCurveEdit.h"
#include "tools/CircleFactory.h"
#include "parametric/ParamDocument.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::test::makeLine;
using cad::test::layerIdAt;

class TestCurveEdit : public QObject
{
    Q_OBJECT

private slots:
    void altDragHandleBreaksLock();
    void plainDragKeepsLock();
    void altDragUndoRestoresLock();
    void circleSegmentsAreLocked();
};

namespace {

/// A curve made the way the tool user makes one: a straight 120mm line plus
/// two Ctrl+click curve points (A at ~40mm, B at ~80mm along the chord).
/// The FIRST anchor (A) is the active one whose tangent handles are shown.
struct CurveEditFixture {
    QUuid blockId;
    QUuid segId;
    QUuid anchorId;          ///< Pass point A.
    Vec2  anchorLocal;       ///< Resolved local pos of A (world == local).
    Vec2  outTipLocal;       ///< OUT handle tip = ctrl1 of the span past A.
    Vec2  inTipLocal;        ///< IN handle tip  = ctrl2 of the span before A.
    Vec2  tanInAuto;         ///< Auto in-tangent from the span cache.
    Vec2  tanOutAuto;        ///< Auto out-tangent from the span cache.
};

/// Recomputed from the CURRENT cache (use after undo/redo to re-read the
/// effective auto tangents — the point's stored tangents are zero while
/// autoTangent==true, the cache holds the rendered values).
void readFixture(ParamDocument& doc, const QUuid& blockId, const QUuid& segId,
                 const QUuid& anchorId, CurveEditFixture* f)
{
    const Block* blk = doc.findBlock(blockId);
    QVERIFY(blk);
    const auto* entry = blk->curveSpanEntry(segId);
    QVERIFY(entry && entry->spans.size() == 3);   // start→A→B→end
    QVERIFY(entry->anchors.size() == 4);
    // Anchor A = anchors[1] (start, A, B, end); world==local (identity block).
    f->anchorLocal = entry->anchors[1];
    f->outTipLocal = entry->spans[1].ctrl1;       // A + tanOut/3
    f->inTipLocal  = entry->spans[0].ctrl2;       // A − tanIn/3
    f->tanInAuto  = (entry->anchors[1] - entry->spans[0].ctrl2) * 3.0;
    f->tanOutAuto = (entry->spans[1].ctrl1 - entry->anchors[1]) * 3.0;
    Q_UNUSED(anchorId);
    QVERIFY(f->tanInAuto.length() > 1e-6);
    QVERIFY(f->tanOutAuto.length() > 1e-6);
}

} // namespace

/// Real-event harness: CanvasScene + CanvasView + ToolManager on the CurveEdit
/// tool. Construction drives the full setup chain through real mouse events.
struct CurveEditHarness {
    ParamDocument doc;
    CanvasScene scene;
    CanvasView view;
    cad::tools::ToolManager tm;
    QUndoStack stack;
    QUuid blockId;
    QUuid segId;
    QUuid anchorId;
    CurveEditFixture f;

    CurveEditHarness()
        : scene(&doc)
        , view(&scene)
        , tm(&scene)
    {
        doc.setActiveLayer(layerIdAt(doc, 1));
        const auto line = makeLine(doc, 120.0);   // (0,0)-(120,0)
        blockId = line.blockId;
        segId = line.segId;
        doc.resolveAll();

        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(80);

        tm.setParamDocument(&doc);
        tm.setUndoStack(&stack);
        tm.switchTool(cad::tools::ToolType::CurveEdit);
        view.setInputDispatcher(&tm);

        // 直线 + Ctrl 放两个曲线点成曲线 (真实事件流)。
        click(40.0, 0.0, Qt::ControlModifier);
        click(80.0, 0.0, Qt::ControlModifier);

        // 点锚点显示手柄 —— 第一个锚点 A 的手柄成为当前显示组。
        const Block* blk = doc.findBlock(blockId);
        QVERIFY(blk);
        const auto* seg = blk->findSegment(segId);
        QVERIFY(seg && seg->passPointIds.size() == 2);
        anchorId = seg->passPointIds.front();     // A (percent-ordered first)
        readFixture(doc, blockId, segId, anchorId, &f);
        click(f.anchorLocal.x, f.anchorLocal.y);  // show A's handles
    }

    /// User coords (x, y) → viewport pixel (scene y is flipped on the canvas).
    QPoint vp(double x, double y) const
    {
        return view.mapFromScene(QPointF(x, -y));
    }

    void sendMouse(QEvent::Type type, const QPoint& pos,
                   Qt::MouseButton btn, Qt::KeyboardModifiers mods)
    {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn == Qt::NoButton ? Qt::LeftButton : btn);
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    }

    void click(double x, double y, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        const QPoint p = vp(x, y);
        sendMouse(QEvent::MouseButtonPress, p, Qt::LeftButton, mods);
        sendMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, mods);
    }

    /// Alt+press on the OUT handle tip → move → release (corner mode when
    /// mods carry Alt; the mods are captured at press, D3).
    void dragOutHandleTo(double tx, double ty, Qt::KeyboardModifiers mods)
    {
        const QPoint tip = vp(f.outTipLocal.x, f.outTipLocal.y);
        sendMouse(QEvent::MouseButtonPress, tip, Qt::LeftButton, mods);
        sendMouse(QEvent::MouseMove, vp(60.0, 25.0), Qt::NoButton, mods);
        sendMouse(QEvent::MouseMove, vp(tx, ty), Qt::NoButton, mods);
        sendMouse(QEvent::MouseButtonRelease, vp(tx, ty), Qt::LeftButton, mods);
    }

    ParamPoint* anchorPt()
    {
        if (auto* blk = doc.findBlock(blockId)) return blk->findPoint(anchorId);
        return nullptr;
    }

    /// Effective (rendered) auto tangents from the CURRENT cache.
    void effectiveAutoTangents(Vec2* tanIn, Vec2* tanOut) const
    {
        const Block* blk = doc.findBlock(blockId);
        const auto* entry = blk ? blk->curveSpanEntry(segId) : nullptr;
        QVERIFY(entry);
        *tanIn  = (entry->anchors[1] - entry->spans[0].ctrl2) * 3.0;
        *tanOut = (entry->spans[1].ctrl1 - entry->anchors[1]) * 3.0;
    }
};

// Alt+拖拽切线手柄 = 持久断锁 (尖角模式)：拖完 tangentLocked==false、
// 被拖手柄更新、对侧手柄保持断锁前值 (未共线同步)、autoTangent 首次物化。
void TestCurveEdit::altDragHandleBreaksLock()
{
    CurveEditHarness h;

    auto* pt = h.anchorPt();
    QVERIFY(pt);
    QVERIFY(pt->tangentLocked);      // 默认锁定 (平滑不等长)
    QVERIFY(pt->autoTangent);        // 自动切线
    const Vec2 tanInBefore = h.f.tanInAuto;

    // Alt+拖 OUT 手柄到 (80,60) —— 断锁模式下对侧手柄不动。
    h.dragOutHandleTo(80.0, 60.0, Qt::AltModifier);

    const Vec2 expectOut = (Vec2(80.0, 60.0) - h.f.anchorLocal) * 3.0;
    QVERIFY2(!pt->tangentLocked, "Alt+drag 必须持久断开切线锁定 (尖角模式)");
    QVERIFY2(!pt->autoTangent, "首次手动物化自动切线");
    QVERIFY2(pt->tangentOut.distanceTo(expectOut) < 1e-6,
             "被拖手柄必须跟随光标 (断锁 = 不镜像)");
    QVERIFY2(pt->tangentIn.distanceTo(tanInBefore) < 1e-6,
             "对侧手柄必须保持断锁前值 (未共线同步)");
}

// 对照: 普通拖拽 (不按 Alt) 保持锁定 —— 对侧手柄同方向且保持自身长度。
void TestCurveEdit::plainDragKeepsLock()
{
    CurveEditHarness h;

    auto* pt = h.anchorPt();
    QVERIFY(pt);
    const double inLenBefore = h.f.tanInAuto.length();

    h.dragOutHandleTo(80.0, 60.0, Qt::NoModifier);

    const Vec2 expectOut = (Vec2(80.0, 60.0) - h.f.anchorLocal) * 3.0;
    QVERIFY2(pt->tangentLocked, "普通拖拽必须保持切线锁定");
    QVERIFY2(!pt->autoTangent, "首次手动物化自动切线");
    QVERIFY2(pt->tangentOut.distanceTo(expectOut) < 1e-6,
             "被拖手柄必须跟随光标");
    // 共线 (平滑): 对侧 = 被拖方向 × 自身长度。
    QVERIFY2(std::abs(pt->tangentIn.length() - inLenBefore) < 1e-6,
             "锁定拖拽下对侧手柄长度必须保持不变 (平滑不等长)");
    const double dot = pt->tangentIn.normalized().dot(pt->tangentOut.normalized());
    QVERIFY2(std::abs(dot - 1.0) < 1e-9,
             "锁定拖拽下对侧手柄必须与被拖手柄共线 (平滑)");
}

// undo 栈: SetCurveTangentCommand 快照含 tangentLocked —— undo 恢复断锁前
// 状态 (锁定回 true + 有效切线回拖前自动值), redo 复原断锁态。
void TestCurveEdit::altDragUndoRestoresLock()
{
    CurveEditHarness h;

    auto* pt = h.anchorPt();
    QVERIFY(pt);
    const Vec2 tanInBefore  = h.f.tanInAuto;
    const Vec2 tanOutBefore = h.f.tanOutAuto;

    h.dragOutHandleTo(80.0, 60.0, Qt::AltModifier);
    QVERIFY(!pt->tangentLocked);
    const Vec2 tanOutDragged = pt->tangentOut;

    h.stack.undo();
    pt = h.anchorPt();
    QVERIFY(pt);
    QVERIFY2(pt->tangentLocked, "undo 必须恢复断锁前的锁定标志 (快照完整性)");
    QVERIFY2(pt->autoTangent, "undo 必须恢复自动切线");
    // 恢复后 autoTangent==true → 有效切线由缓存给出 (显示值 = 拖前自动值)。
    Vec2 effIn, effOut;
    h.effectiveAutoTangents(&effIn, &effOut);
    QVERIFY2(effIn.distanceTo(tanInBefore) < 1e-6,
             "undo 后有效 in 切线必须回拖前自动值");
    QVERIFY2(effOut.distanceTo(tanOutBefore) < 1e-6,
             "undo 后有效 out 切线必须回拖前自动值");

    h.stack.redo();
    pt = h.anchorPt();
    QVERIFY(pt);
    QVERIFY2(!pt->tangentLocked, "redo 必须复原断锁态");
    QVERIFY2(!pt->autoTangent, "redo 必须保持手动模式");
    QVERIFY2(pt->tangentOut.distanceTo(tanOutDragged) < 1e-6,
             "redo 必须重放拖后切线");
    QVERIFY2(pt->tangentIn.distanceTo(tanInBefore) < 1e-6,
             "redo 对侧切线保持断锁前材料化值");
}

// ---------------------------------------------------------------------------
// D8: 曲线编辑对圆段置灰 (CIRCLE_TOOL_DESIGN.md D8)
// ---------------------------------------------------------------------------

/// 圆段 fixture：r=40、a0=0°、圆心在原点 → 三个象限锚在 90°/180°/270°，
/// 两端点重合于 0°。所有坐标都经 block.transform 取世界值 (不假设恒等变换)。
struct CircleLockHarness {
    ParamDocument doc;
    CanvasScene scene;
    CanvasView view;
    cad::tools::ToolManager tm;
    QUndoStack stack;
    QUuid blockId;
    QUuid segId;
    QUuid anchorId;        ///< 90° 象限锚 (passPointIds.front())。
    Vec2  anchorWorld;     ///< 该锚的世界坐标 → view.mapFromScene 可直接用。
    Vec2  bodyWorld;       ///< 圆周 45° 处：距最近的点 30.6mm > 12mm 吸附半径。
    double radiusMm = 40.0;

    CircleLockHarness()
        : scene(&doc)
        , view(&scene)
        , tm(&scene)
    {
        doc.setActiveLayer(layerIdAt(doc, 1));
        cad::tools::CircleFactory factory(&doc, doc.undoStack());
        blockId = factory.createCircle(Vec2(0.0, 0.0), radiusMm, 0.0);
        QVERIFY(!blockId.isNull());
        doc.resolveAll();

        const Block* blk = doc.findBlock(blockId);
        QVERIFY(blk);
        QCOMPARE(blk->segments.size(), std::size_t(1));
        const Segment& seg = blk->segments.front();
        segId = seg.id;
        QCOMPARE(seg.fitKind, FitKind::Circle);
        QCOMPARE(seg.passPointIds.size(), std::size_t(3));
        anchorId = seg.passPointIds.front();
        const ParamPoint* ap = blk->findPoint(anchorId);
        QVERIFY(ap && ap->resolved);
        anchorWorld = blk->transform.toWorld(ap->resolvedPos);
        const double d45 = radiusMm * 0.7071067811865476;
        bodyWorld = blk->transform.toWorld(Vec2(d45, d45));

        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(80);

        tm.setParamDocument(&doc);
        tm.setUndoStack(&stack);
        tm.switchTool(cad::tools::ToolType::CurveEdit);
        view.setInputDispatcher(&tm);
    }

    /// 世界/场景坐标 → 视口像素 (harness 里的点已是场景坐标，不再翻转 y)。
    QPoint vpScene(const Vec2& p) const
    {
        return view.mapFromScene(QPointF(p.x, p.y));
    }

    void sendMouse(QEvent::Type type, const QPoint& pos,
                   Qt::MouseButton btn, Qt::KeyboardModifiers mods)
    {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease)
            ? Qt::NoButton
            : (btn == Qt::NoButton ? Qt::LeftButton : btn);
        QMouseEvent ev(type, pos, global, btn, buttons, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    }

    void clickAt(const Vec2& p, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        const QPoint px = vpScene(p);
        sendMouse(QEvent::MouseButtonPress, px, Qt::LeftButton, mods);
        sendMouse(QEvent::MouseButtonRelease, px, Qt::LeftButton, mods);
    }

    void dragAt(const Vec2& from, const Vec2& to,
                Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        sendMouse(QEvent::MouseButtonPress, vpScene(from), Qt::LeftButton, mods);
        sendMouse(QEvent::MouseMove, vpScene((from + to) * 0.5), Qt::NoButton, mods);
        sendMouse(QEvent::MouseMove, vpScene(to), Qt::NoButton, mods);
        sendMouse(QEvent::MouseButtonRelease, vpScene(to), Qt::LeftButton, mods);
    }
};

// 圆段在曲线编辑里三种手势 (拖锚 / Shift 删锚 / Ctrl 加点) 全部无效：
// 几何逐点不变、不产生任何 undo 命令；改形必须走「解除圆约束」。
void TestCurveEdit::circleSegmentsAreLocked()
{
    CircleLockHarness h;

    const Block* blk = h.doc.findBlock(h.blockId);
    QVERIFY(blk);
    const Segment* seg = blk->findSegment(h.segId);
    QVERIFY(seg);
    const int baseCmds = h.stack.count();
    const ParamPoint* ap = blk->findPoint(h.anchorId);
    QVERIFY(ap);
    const double pctBefore = ap->interpPercent;
    const Vec2 posBefore = ap->resolvedPos;

    // ① 拖象限锚 → 不进入拖拽 (interpPercent / 解算位置 / 命令数全不变)。
    h.dragAt(h.anchorWorld, h.anchorWorld + Vec2(25.0, 25.0));
    blk = h.doc.findBlock(h.blockId);
    QVERIFY(blk);
    ap = blk->findPoint(h.anchorId);
    QVERIFY(ap);
    QVERIFY2(std::abs(ap->interpPercent - pctBefore) < 1e-12,
             "D8: 圆段象限锚不得被曲线编辑拖动");
    QVERIFY2(ap->resolvedPos.distanceTo(posBefore) < 1e-9,
             "D8: 拖拽后象限锚的解算位置必须不变");
    QCOMPARE(h.stack.count(), baseCmds);

    // ② Shift+点象限锚 → 不删除锚点。
    h.clickAt(h.anchorWorld, Qt::ShiftModifier);
    blk = h.doc.findBlock(h.blockId);
    QVERIFY(blk);
    QVERIFY2(blk->findPoint(h.anchorId) != nullptr,
             "D8: 圆段象限锚不得被 Shift+点删除");
    QCOMPARE(blk->findSegment(h.segId)->passPointIds.size(), std::size_t(3));
    QCOMPARE(h.stack.count(), baseCmds);

    // ③ Ctrl+点圆身 (45°) → 不加曲线点。
    h.clickAt(h.bodyWorld, Qt::ControlModifier);
    blk = h.doc.findBlock(h.blockId);
    QVERIFY(blk);
    QCOMPARE(blk->findSegment(h.segId)->passPointIds.size(), std::size_t(3));
    QCOMPARE(h.stack.count(), baseCmds);

    // ④ 圆几何不变：半径 / 包角 / 圆度。
    seg = blk->findSegment(h.segId);
    QVERIFY(seg);
    QVERIFY2(std::abs(blk->circleRadiusMm(*seg) - h.radiusMm) < 1e-9,
             "D8: 圆半径必须保持 40mm");
    QVERIFY2(std::abs(blk->circleSweepDeg(*seg) - 360.0) < 1e-9,
             "D8: 整圆包角必须保持 360°");
    QCOMPARE(seg->tension, 0.0);
}

QTEST_MAIN(TestCurveEdit)
#include "test_curve_edit.moc"
