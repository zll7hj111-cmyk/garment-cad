/// @file test_rotate_copy.cpp
/// 旋转复制 (Ctrl+drag in ToolRotate): 克隆目标块并挂回原块,
/// 跟随角度 = 相对原线当前朝向的角度; 原块旋转后副本保持相对角度。
/// 覆盖: 模型层(挂接/相对角度/跟随)、RotateCopyCommand 撤销重做、
/// UI 层完整事件链(提交/取消/零角度丢弃/连续复制)、公式锁定块副本无公式。

#include <QtTest>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QUndoStack>

#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "tools/ToolManager.h"
#include "tools/ToolRotate.h"
#include "tools/AngleHud.h"
#include "parametric/ParamDocument.h"
#include "parametric/Duplicate.h"
#include "document/commands/BlockCommands.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::test::makeLine;
using cad::test::LineSetup;

namespace {

/// World angle of the block's first segment (degrees, +Y up).
double worldAngleDeg(const ParamDocument& doc, const QUuid& blockId)
{
    const Block* b = doc.findBlock(blockId);
    if (!b || b->segments.empty()) return 0.0;
    const Segment& seg = b->segments.front();
    const ParamPoint* sp = b->findPoint(seg.startPointId);
    const ParamPoint* ep = b->findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return 0.0;
    const Vec2 w1 = b->transform.toWorld(sp->resolvedPos);
    const Vec2 w2 = b->transform.toWorld(ep->resolvedPos);
    return (w2 - w1).angle() * 180.0 / M_PI;
}

/// The clone's non-pin attachment back to @p original.
const Attachment* cloneAttachment(const ParamDocument& doc,
                                  const QUuid& cloneId, const QUuid& originalId)
{
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == cloneId && a.toBlockId == originalId && !a.isPin)
            return &a;
    return nullptr;
}

/// The follower's non-pin attachment (any leader).
const Attachment* followerAttachmentOf(const ParamDocument& doc,
                                       const QUuid& followerId)
{
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == followerId && !a.isPin)
            return &a;
    return nullptr;
}

/// Attach @p clone back to @p original at the original's start point with
/// the given MODEL follower angle (模型语义: followerAngle 相对 leader 出口
/// 方向；起点处出口方向 = 反向，故 180° = 与原线同向重合).
Attachment attachCloneToOriginal(ParamDocument& doc, const Block& clone,
                                 const QUuid& originalId,
                                 const LineSetup& orig, double followerAngle)
{
    Attachment att;
    att.fromBlockId = clone.id;
    att.fromPointId = clone.points.front().id;
    att.toBlockId = originalId;
    att.toPointId = orig.startId;
    att.toSegmentId = orig.segId;
    att.followerAngle = followerAngle;
    att.rotationMode = RotationMode::Angle;
    return att;
}

} // namespace

class TestRotateCopy : public QObject
{
    Q_OBJECT

private slots:
    // ── 模型层 ──
    void cloneAttachesToOriginalWithRelativeAngle();
    void cloneFollowsOriginalRotation();
    void rotateCopyCommandUndoRedo();
    void formulaLockedOriginalCopyIsFree();

    // ── UI 层完整事件链 ──
    void ctrlDragRotateCopyCommits();
    void ctrlDragZeroAngleDiscards();
    void escCancelsCopy();
    void consecutiveCopiesAllAttachToOriginal();

    // ── 公式锁定跟随线 ──
    void lockedFollowerRotationBakesFormula();
    void lockedFollowerRotateCopyWorks();

    // ── 终点指向 (endTarget) 锁定 ──
    void endTargetRotationReleasesAim();
    void endTargetRotateCopyDropsAim();

    // ── 锚心切换 (起点 ↔ 终点) ──
    void xToggleSwitchesAnchorToEndPoint();
    void clickEndPointSwitchesAnchor();
    void followerEndAnchorRotateReleasesLink();
    void endAnchorRotateCopyAttachesToEnd();
    void endAnchorLineFollowsCursor();

    // ── 弧长/角度显示归一化 (多圈不爆表) ──
    void angleModeOverflowNormalized();
    void arcLengthModeOverflowNormalized();
};

void TestRotateCopy::cloneAttachesToOriginalWithRelativeAngle()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    const LineSetup a = makeLine(doc, 100.0);   // horizontal at origin
    doc.resolveAll();

    // Single-block duplicate → clone overlapping the original.
    DuplicateResult r = duplicateBlocks(doc, {a.blockId});
    QCOMPARE(r.blocks.size(), size_t(1));
    const Block& clone = r.blocks.front();

    // Auto-published length link must exist before the clone resolves.
    for (const auto& lv : r.newLinked)
        doc.addLinked(lv);
    doc.addBlock(clone);
    QVERIFY(doc.addAttachment(
        attachCloneToOriginal(doc, clone, a.blockId, a, 210.0)));
    doc.resolveAll();

    // Stored 210° = 180°(起点出口反向) + 30° → clone is 30° CCW of the
    // original (relative-angle semantics of the rotate-copy gesture).
    const Block* orig = doc.findBlock(a.blockId);
    const Block* cln = doc.findBlock(clone.id);
    QVERIFY(orig && cln);
    QVERIFY(cln->worldPos(cln->points.front().id)
                .distanceTo(orig->worldPos(a.startId)) < 1e-6);
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) - 30.0) < 1e-6);
}

void TestRotateCopy::cloneFollowsOriginalRotation()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    const LineSetup a = makeLine(doc, 100.0);
    doc.resolveAll();

    DuplicateResult r = duplicateBlocks(doc, {a.blockId});
    const Block& clone = r.blocks.front();
    for (const auto& lv : r.newLinked)
        doc.addLinked(lv);
    doc.addBlock(clone);
    doc.addAttachment(attachCloneToOriginal(doc, clone, a.blockId, a, 210.0));
    doc.resolveAll();

    // Relative angle = stored − 180° = 30° before and 75° after the 45° turn.
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) - 30.0) < 1e-6);

    // Rotate the ORIGINAL 45° about its start → the clone keeps its 30°
    // RELATIVE angle (75° world) and its pivot stays on the original start.
    Block* orig = doc.findBlock(a.blockId);
    const Vec2 pivot = orig->worldPos(a.startId);
    const Vec2 startLocal = orig->findPoint(a.startId)->resolvedPos;
    const double newRot = 45.0 * M_PI / 180.0;
    orig->transform.rotation = newRot;
    orig->transform.origin = pivot - startLocal.rotated(newRot);
    doc.invalidateAllLayers();
    doc.resolveAll();

    QVERIFY(doc.diagnostics().empty());
    const Block* cln = doc.findBlock(clone.id);
    QVERIFY(cln);
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) - 75.0) < 1e-6);
    QVERIFY(cln->worldPos(cln->points.front().id)
                .distanceTo(orig->worldPos(a.startId)) < 1e-6);
}

void TestRotateCopy::rotateCopyCommandUndoRedo()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    const LineSetup a = makeLine(doc, 100.0);
    doc.resolveAll();

    DuplicateResult r = duplicateBlocks(doc, {a.blockId});
    const Block& clone = r.blocks.front();

    QUndoStack stack;
    stack.push(new cad::cmd::RotateCopyCommand(
        &doc, std::move(r), a.blockId, a.startId,
        clone.points.front().id, a.segId, 60.0));
    QCOMPARE(doc.blocks().size(), size_t(2));
    QVERIFY(doc.findBlock(clone.id) != nullptr);
    QVERIFY(cloneAttachment(doc, clone.id, a.blockId) != nullptr);
    // Stored 60° (model angle) → world = 180 + 60 = 240 ≡ −120°.
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) + 120.0) < 1e-6);

    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(1));
    QVERIFY(doc.findBlock(clone.id) == nullptr);
    QCOMPARE(doc.attachments().size(), size_t(0));

    stack.redo();
    QCOMPARE(doc.blocks().size(), size_t(2));
    QVERIFY(doc.findBlock(clone.id) != nullptr);
    QVERIFY(cloneAttachment(doc, clone.id, a.blockId) != nullptr);
    QVERIFY(std::abs(worldAngleDeg(doc, clone.id) + 120.0) < 1e-6);
}

void TestRotateCopy::formulaLockedOriginalCopyIsFree()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    // Leader B + follower A whose follower angle is FORMULA-locked.
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.followerAngle = 45.0;
    conn.followerAngleFormula = QStringLiteral("45");   // locked (evaluates)
    doc.addAttachment(conn);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    // Rotate-copy of A: the clone's own attachment carries NO formula.
    DuplicateResult r = duplicateBlocks(doc, {a.blockId});
    const Block& clone = r.blocks.front();
    doc.addBlock(clone);
    doc.addAttachment(attachCloneToOriginal(doc, clone, a.blockId, a, 10.0));
    doc.resolveAll();

    const Attachment* ca = cloneAttachment(doc, clone.id, a.blockId);
    QVERIFY(ca);
    QVERIFY(ca->followerAngleFormula.isEmpty());   // 副本自动去公式
    QVERIFY(std::abs(ca->followerAngle - 10.0) < 1e-9);
}

// ═══════════════════════════════════════════════════════════════════
// UI-layer: full event chain through CanvasView (QTest mouse events →
// dispatch → ToolRotate).
// ═══════════════════════════════════════════════════════════════════

void TestRotateCopy::ctrlDragRotateCopyCommits()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // 1) Click selects the line (Idle → Ready).
    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(doc.blocks().size(), size_t(1));

    // 2) Ctrl+press → drag up 90° → release commits the rotate-copy.
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(2));   // original + clone
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != a.blockId) { cln = &b; break; }
    QVERIFY(cln);
    QVERIFY(cloneAttachment(doc, cln->id, a.blockId) != nullptr);
    // Dragging 90° CCW around the start point → clone 90° CCW of original.
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 90.0) < 1e-6);

    // 3) Undo removes the copy in ONE step.
    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(doc.attachments().size(), size_t(0));
}

void TestRotateCopy::ctrlDragZeroAngleDiscards()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);

    // Ctrl+press → drag away → drag BACK to the exact start angle → release:
    // zero relative angle = copy discarded (转回原位 = 不复制).
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(50.0, 0.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 0.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(doc.attachments().size(), size_t(0));
}

void TestRotateCopy::escCancelsCopy()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);

    // Ctrl+press → drag → Esc mid-gesture: preview clone dropped, original
    // untouched, tool back to Ready (target kept).
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &esc);
    QTest::qWait(20);

    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(doc.attachments().size(), size_t(0));
}

void TestRotateCopy::consecutiveCopiesAllAttachToOriginal()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) {
        return view.mapFromScene(QPointF(x, -y));
    };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton,
                       mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };
    auto ctrlRotateTo = [&](const QPoint& from, double x, double y) {
        sendMouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::ControlModifier);
        sendMouse(QEvent::MouseMove, vp(x, y), Qt::NoButton, Qt::ControlModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(x, y), Qt::LeftButton,
                  Qt::ControlModifier);
    };

    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);

    // Copy #1 → 90°; copy #2 → 180°. Both attach to the ORIGINAL and the
    // tool stays Ready (连续复制).
    ctrlRotateTo(hit, 0.0, 50.0);
    QCOMPARE(doc.blocks().size(), size_t(2));
    ctrlRotateTo(hit, -50.0, 0.0);
    QCOMPARE(doc.blocks().size(), size_t(3));
    QCOMPARE(doc.attachments().size(), size_t(2));

    QList<double> relAngles;
    for (const auto& b : doc.blocks()) {
        if (b.id == a.blockId) continue;
        QVERIFY(cloneAttachment(doc, b.id, a.blockId) != nullptr);
        relAngles << worldAngleDeg(doc, b.id);
    }
    QCOMPARE(relAngles.size(), 2);
    QVERIFY(relAngles.contains(90.0));
    QVERIFY(relAngles.contains(180.0));
}

// ═══════════════════════════════════════════════════════════════════
// Formula-locked follower (角度公式锁定的跟随线)
// ═══════════════════════════════════════════════════════════════════

namespace {

/// Build: horizontal leader B (200,0)→(300,0) + follower A attached at its
/// end with a FORMULA-locked 45° follower angle. A's start = (300,0),
/// A points 45° for 60 mm (mid-body ≈ (321.2, 21.2)).
struct LockedFollowerSetup {
    LineSetup b;
    LineSetup a;
};

LockedFollowerSetup makeLockedFollower(ParamDocument& doc)
{
    LockedFollowerSetup s;
    s.b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    s.a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = s.a.blockId;
    conn.fromPointId = s.a.startId;
    conn.toBlockId = s.b.blockId;
    conn.toPointId = s.b.endId;
    conn.toSegmentId = s.b.segId;
    conn.followerAngle = 45.0;
    conn.followerAngleFormula = QStringLiteral("45");   // 公式锁定
    doc.addAttachment(conn);
    doc.resolveAll();
    return s;
}

} // namespace

// 旋转锁定跟随线：允许旋转 = 公式烘成数值（几何不变），角度自由修改；
// 撤销一步恢复公式。
void TestRotateCopy::lockedFollowerRotationBakesFormula()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const LockedFollowerSetup s = makeLockedFollower(doc);
    QVERIFY(doc.diagnostics().empty());

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // Click the follower's mid-body to select it (Ready, formula locked).
    const QPoint mid = vp(321.21, 21.21);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    // Ordinary drag (NO Ctrl): rotation is allowed — the formula is baked
    // into a plain number (geometry preserved) and the angle changes by 45°.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(300.0, 40.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(300.0, 40.0), Qt::LeftButton,
              Qt::NoModifier);

    const Attachment* ca = followerAttachmentOf(doc, s.a.blockId);
    QVERIFY(ca);
    QVERIFY(ca->followerAngleFormula.isEmpty());        // 公式已烘成数值
    QVERIFY(std::abs(ca->followerAngle - 90.0) < 1e-6); // 45° + 45° 拖动

    // Undo restores the formula in ONE step.
    stack.undo();
    const Attachment* ca2 = followerAttachmentOf(doc, s.a.blockId);
    QVERIFY(ca2);
    QCOMPARE(ca2->followerAngleFormula, QStringLiteral("45"));
}

// 锁定跟随线的旋转复制：Ctrl+press 拖动 → 副本出现，副本自身无公式。
void TestRotateCopy::lockedFollowerRotateCopyWorks()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const LockedFollowerSetup s = makeLockedFollower(doc);
    QVERIFY(doc.diagnostics().empty());

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    const QPoint mid = vp(321.21, 21.21);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    // Ctrl+press on the LOCKED follower → drag 45° CCW → release.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(300.0, 40.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(300.0, 40.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(3));          // B + A + clone
    QCOMPARE(doc.attachments().size(), size_t(2));     // A→B + clone→A
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != s.a.blockId && b.id != s.b.blockId) { cln = &b; break; }
    QVERIFY(cln);
    const Attachment* ca = cloneAttachment(doc, cln->id, s.a.blockId);
    QVERIFY(ca);
    QVERIFY(ca->followerAngleFormula.isEmpty());         // 副本无公式
    // 副本绝对角度 = A 朝向(45°) + 相对 45° = 90°.
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 90.0) < 1e-6);
}

// ═══════════════════════════════════════════════════════════════════
// Endpoint-aim (终点指向) — resolver Step 7 would pull the direction back
// to the target point on every frame, so rotation must RELEASE the aim.
// ═══════════════════════════════════════════════════════════════════

namespace {

/// Build: free line A from (0,0) to (100,0) with endTarget aiming at point P
/// at (100, 100) — the resolver keeps A pointing 45° at P.
struct AimLineSetup {
    LineSetup a;
    QUuid targetPointId;
};

AimLineSetup makeAimLine(ParamDocument& doc)
{
    AimLineSetup s;
    s.a = makeLine(doc, 100.0);
    // Target block T: a vertical line so its start point sits at (100,100).
    const LineSetup t = makeLine(doc, 50.0, Vec2(100.0, 100.0));
    (void)t;
    Block* blk = doc.findBlock(s.a.blockId);
    Q_ASSERT(blk);
    blk->endTargetBlockId = doc.blocks().back().id;
    blk->endTargetPointId = doc.blocks().back().points.front().id;
    s.targetPointId = blk->endTargetPointId;
    doc.resolveAll();
    return s;
}

} // namespace

// 旋转带终点指向的自由线：拖动即解除指向（否则 Resolver 每帧拉回），
// 角度自由变化；撤销一步恢复指向约束。
void TestRotateCopy::endTargetRotationReleasesAim()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const AimLineSetup s = makeAimLine(doc);
    QVERIFY(doc.diagnostics().empty());
    // A aims at P before the gesture.
    const Block* blk0 = doc.findBlock(s.a.blockId);
    QVERIFY(!blk0->endTargetBlockId.isNull());
    QVERIFY(std::abs(worldAngleDeg(doc, s.a.blockId) - 45.0) < 1e-6);

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // Select A at its mid-body (the aim keeps A at 45° from (0,0) to
    // (70.7,70.7), so the mid point is (35.4, 35.4)).
    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    // Drag 90° (cursor from (35.4,35.4) → (0,50) around pivot (0,0)): aim released.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 50.0), Qt::LeftButton,
              Qt::NoModifier);

    const Block* blk = doc.findBlock(s.a.blockId);
    QVERIFY(blk->endTargetBlockId.isNull());           // 指向已解除
    QVERIFY(std::abs(worldAngleDeg(doc, s.a.blockId) - 90.0) < 1e-6);

    // Undo restores the aim constraint AND the old direction.
    stack.undo();
    const Block* blk2 = doc.findBlock(s.a.blockId);
    QVERIFY(!blk2->endTargetBlockId.isNull());
    QCOMPARE(blk2->endTargetPointId, s.targetPointId);
}

// 终点指向线的旋转复制：副本不继承指向，可自由转动。
void TestRotateCopy::endTargetRotateCopyDropsAim()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const AimLineSetup s = makeAimLine(doc);
    QVERIFY(doc.diagnostics().empty());

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    // Ctrl+drag on the AIMING line → clone appears with NO aim constraint
    // and rotates freely (原线仍指向 P，副本相对转 90°).
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(3));          // T + A + clone
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != s.a.blockId && cloneAttachment(doc, b.id, s.a.blockId))
            { cln = &b; break; }
    QVERIFY(cln);
    QVERIFY(cln->endTargetBlockId.isNull());           // 副本无指向
    // 副本绝对角度 = A 指向角(45°) + 相对 90° = 135°.
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 135.0) < 1e-6);
}

// ═══════════════════════════════════════════════════════════════════
// Anchor switching (锚心: 起点 ↔ 终点)
// ═══════════════════════════════════════════════════════════════════

namespace {

/// Send an X key event straight to the view (bypasses the HUD's focus).
void sendKeyX(CanvasView& view)
{
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(&view, &ev);
}

} // namespace

// X 键把锚心从起点切到终点：独立线绕终点转（终点钉住、起点绕圈），
// 绝对角度按拖动角度计算；撤销一步恢复原姿态。
void TestRotateCopy::xToggleSwitchesAnchorToEndPoint()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) → (100,0)
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // Select A (default anchor = START point).
    const QPoint mid = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    const Block* blk = doc.findBlock(a.blockId);
    const Vec2 endBefore = blk->worldPos(a.endId);
    const Vec2 startBefore = blk->worldPos(a.startId);

    // X: switch the anchor to the END point.
    sendKeyX(view);

    // Drag: cursor (50,0) → (0,100) around the end pivot (100,0) ⇒ −45°.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);

    const Block* blk2 = doc.findBlock(a.blockId);
    // The END point stays pinned to the pivot; the START point swung around.
    QVERIFY(blk2->worldPos(a.endId).distanceTo(endBefore) < 1e-6);
    QVERIFY(blk2->worldPos(a.startId).distanceTo(startBefore) > 1e-3);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (-45.0)) < 1e-6);

    // One undo restores the original pose.
    stack.undo();
    const Block* blk3 = doc.findBlock(a.blockId);
    QVERIFY(blk3->worldPos(a.endId).distanceTo(endBefore) < 1e-6);
    QVERIFY(blk3->worldPos(a.startId).distanceTo(startBefore) < 1e-6);
}

// 直接点击另一端端点 = 切换锚心（与 X 键等价）。
void TestRotateCopy::clickEndPointSwitchesAnchor()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) → (100,0)
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // Select A (default anchor = start).
    sendMouse(QEvent::MouseButtonPress, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    // Click the END point → anchor switches there.
    sendMouse(QEvent::MouseButtonPress, vp(100.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(100.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    // Drag: cursor (50,0) → (0,100) around the end pivot (100,0).
    sendMouse(QEvent::MouseButtonPress, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);

    const Block* blk = doc.findBlock(a.blockId);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (-45.0)) < 1e-6);
    // End pivot stayed pinned: (100,0).
    QVERIFY(blk->worldPos(a.endId).distanceTo(Vec2(100.0, 0.0)) < 1e-6);
}

// 跟随线切终点锚心：旋转开始即解除挂接（旋转 = 放弃跟随），绝对角度自由旋转；
// 撤销一步同时恢复挂接（含公式）与旋转前姿态。
// 注意：事件坐标经 viewport 整数化（event->pos() 是 QPoint），因此用 0° 跟随角度
// 的跟随线 + 整数坐标，保证拖动角精确（45° 跟随角度会引入亚像素取整误差）。
void TestRotateCopy::followerEndAnchorRotateReleasesLink()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    // B: (200,0)→(300,0); A hangs on B's END with a 0° follower angle
    // (no formula) → A: (300,0)→(360,0), world angle 0°.
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.followerAngle = 0.0;
    doc.addAttachment(conn);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());
    QCOMPARE(doc.attachments().size(), size_t(1));

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // Select follower A: anchor = its START point (the attachment point),
    // so the session starts in Connected mode.
    const QPoint mid = vp(330.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    // X: switch the anchor to A's END point → Free mode + pending release.
    sendKeyX(view);

    // Drag: cursor (330,0) → (360,100) around the end pivot (360,0):
    // cur0 = atan2(0,-30) = 180°, theta = atan2(100,0) = 90°,
    // delta = −90° ⇒ world angle 0 − 90 = −90° (exact, integer coords).
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(360.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(360.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);

    // The follower link is GONE (旋转 = 放弃跟随) and A rotated freely.
    QVERIFY(doc.attachments().empty());
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (-90.0)) < 1e-6);

    // ONE undo restores the link and the pre-rotation pose.
    stack.undo();
    QCOMPARE(doc.attachments().size(), size_t(1));
    QVERIFY(followerAttachmentOf(doc, a.blockId) != nullptr);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 0.0) < 1e-6);
}

// 锚心=终点时 Ctrl+拖动旋转复制：副本挂回原线的终点（不是起点）。
void TestRotateCopy::endAnchorRotateCopyAttachesToEnd()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) → (100,0)
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // Select A, then switch the anchor to the END point.
    const QPoint mid = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendKeyX(view);

    // Ctrl+drag: cursor (50,0) → (0,100) around the END pivot (100,0)
    // ⇒ relative −45° (original stays horizontal at 0°).
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(2));
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != a.blockId && cloneAttachment(doc, b.id, a.blockId))
            { cln = &b; break; }
    QVERIFY(cln);
    // The clone hangs on the ORIGINAL's END point (not the start).
    const Attachment* ca = cloneAttachment(doc, cln->id, a.blockId);
    QVERIFY(ca);
    QCOMPARE(ca->toPointId, a.endId);
    // Clone at relative −45°: the END-point exit direction is FORWARD (0°,
    // unlike the start point's backward exit), so the stored 180°+deg fold
    // puts the clone at 0° + 180° − 45° = 135° world angle. The original
    // stays untouched at 0°.
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 135.0) < 1e-6);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 0.0) < 1e-6);
}

// 终点锚心新语义：HUD 角度 = 从终点指向线的方向（线方向+180°），
// 线始终跟随光标 —— 光标拖到终点正上方，线从终点向上伸出（方向 −90°），
// 而不是旧行为的“背对光标”朝下。
void TestRotateCopy::endAnchorLineFollowsCursor()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0) → (100,0)
    doc.resolveAll();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // Select A, then switch the anchor to the END point (X).
    const QPoint mid = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendKeyX(view);

    // Drag the cursor from the line body (50,0) straight UP to (100,100)
    // — from the END pivot (100,0) that is the 90° direction. The line must
    // point AT the cursor: start swings to (100,100), end stays pinned.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(100.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(100.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);

    const Block* blk = doc.findBlock(a.blockId);
    // End pinned at (100,0); start swung to the cursor side (100,100):
    // line direction = −90° (起点→终点朝下), display angle (from end) = 90°.
    QVERIFY(blk->worldPos(a.endId).distanceTo(Vec2(100.0, 0.0)) < 1e-6);
    QVERIFY(blk->worldPos(a.startId).distanceTo(Vec2(100.0, 100.0)) < 1e-6);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (-90.0)) < 1e-6);
}

// 弧长/角度显示归一化：存储多圈角度（1260° = 3.5 圈）时 HUD 必须显示
// [0, 360) 内的归一化值（用户报告: 400°+ 爆表回归 2026-08）。
void TestRotateCopy::angleModeOverflowNormalized()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    // B: (200,0)→(300,0); A hangs on B's END with followerAngle = 1260°
    // (≡ 180°: A points LEFT, mid-body at (270,0)).
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.followerAngle = 1260.0;   // multi-turn stored value
    doc.addAttachment(conn);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // Select A (anchor = attachment point → Connected/Angle mode).
    sendMouse(QEvent::MouseButtonPress, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    auto* hud = view.viewport()->findChild<cad::tools::AngleHud*>();
    QVERIFY(hud);
    QCOMPARE(hud->edit()->text(), QStringLiteral("180"));   // 1260 → 180
}

// 弧长模式多圈 → 切换回角度模式，HUD 显示归一化角度（不爆表）。
void TestRotateCopy::arcLengthModeOverflowNormalized()
{
    ParamDocument doc;
    doc.setActiveLayer(0);
    CanvasScene scene(&doc);
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);   // radius 60mm
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.rotationMode = cad::param::RotationMode::ArcLength;
    conn.arcLength = 3.0 * 2.0 * M_PI * 60.0;   // 3 full turns
    doc.addAttachment(conn);
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Rotate);
    view.setToolManager(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        QTest::qWait(20);
    };

    // Select A: 3 turns ≡ 180° fold-back → A points LEFT, mid-body (270,0).
    sendMouse(QEvent::MouseButtonPress, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    auto* hud = view.viewport()->findChild<cad::tools::AngleHud*>();
    QVERIFY(hud);
    // ArcLength mode: HUD shows arc length in cm (3 turns × 2π × 60mm = 113.1cm).
    QCOMPARE(hud->edit()->text(), QStringLiteral("113.1"));

    // Click the HUD mode toggle (⌒ → ∠): the angle must normalize to 180°.
    auto* toggle = hud->findChild<QPushButton*>();
    QVERIFY(toggle);
    QTest::mouseClick(toggle, Qt::LeftButton);
    QCOMPARE(hud->edit()->text(), QStringLiteral("180"));   // 1260 → 180
}

QTEST_MAIN(TestRotateCopy)
#include "test_rotate_copy.moc"
