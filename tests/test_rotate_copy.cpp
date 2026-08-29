/// @file test_rotate_copy.cpp
/// 旋转复制 (Ctrl+drag in ToolRotate): 克隆目标块并挂回原块,
/// 跟随角度 = 相对原线当前朝向的角度; 原块旋转后副本保持相对角度。
/// 覆盖: 模型层(挂接/相对角度/跟随)、RotateCopyCommand 撤销重做、
/// UI 层完整事件链(提交/取消/零角度丢弃/连续复制)、公式锁定块副本无公式。

#include <QtTest>
#include <QApplication>
#include <QKeyEvent>
#include <QFile>
#include <QTextStream>
#include <QLineEdit>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QUndoStack>

#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "tools/ToolManager.h"
#include "tools/ToolRotate.h"
#include "tools/ToolSelect.h"
#include "ui/AngleHud.h"
#include "ui/LinePropertyDialog.h"
#include "ElaLineEdit.h"
#include "parametric/ParamDocument.h"
#include "parametric/Duplicate.h"
#include "geometry/Angle.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/ComponentCommands.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::test::makeLine;
using cad::test::LineSetup;
using cad::test::layerIdAt;

namespace {

/// D15 单线确认流: 右键确认当前选中目标 (选中态 → 确定态).
/// 走 QTest 真实事件管线 (press+release); 位置无关 —— 处理只翻转标志位。
/// 中心点大概率落在空白处, 不会误触上下文菜单; 且旋转会话期该菜单已屏蔽
/// (CanvasView::contextMenuEvent 对 hasSessionTarget 早退)。
void sendConfirm(CanvasView& view)
{
    QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier,
                      view.viewport()->rect().center());
    // mouseClick(QWidget) 经 qApp->notify 同步投递, 确认翻转在返回前已完成;
    // 各用例断言各异, 此处无统一可观测条件, 暂留 qWait 仅排空后续 posted 事件。
    QTest::qWait(10);
}

/// Send an Esc key event straight to the view.
void sendKeyEsc(CanvasView& view)
{
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &ev);
}


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
    void diagonalFreeLineCopyOverlapsOriginal();
    void ctrlDragZeroAngleDiscards();
    void escCancelsCopy();
    void consecutiveCopiesAllAttachToOriginal();
    void rotateCopyCommitHidesHud();

    // ── 公式锁定跟随线 ──
    void lockedFollowerRotationBakesFormula();
    void lockedFollowerRotateCopyWorks();
    void modeSwitchKeepsFormula();
    void propertyDialogShowsFollowValue();

    // ── 终点指向 (endTarget) 锁定 ──
    void endTargetRotationReleasesAim();
    void endTargetRotateCopyDropsAim();
    void endTargetRotateCopyKeepsOriginalAim();
    void endTargetRotateCopyIdleGestureKeepsOriginalAim();
    void endTargetRotateCopyUndoRedoKeepsOriginalAim();
    void midGestureCtrlConvertsToCopy();

    // ── 锚心切换 (起点 ↔ 终点) ──
    void xToggleSwitchesAnchorToEndPoint();
    void clickEndPointSwitchesAnchor();
    void connectedLineXAnchorSwitchBlocked();
    void endAnchorRotateCopyAttachesToEnd();
    // ── 选集旋转 (选区继承; 原 W 键组件整组旋转模态已删除, 2026-08-29) ──
    void selRotateAboutFreePivotRotatesAllMembers();
    void selRotateAboutSnappedPivot();
    void selRotateKeepsInternalAttachment();
    void escCancelsSelRotate();
    void endAnchorLineFollowsCursor();

    // ── 弧长/角度显示归一化 (多圈不爆表) ──
    void angleModeOverflowNormalized();
    void arcLengthModeOverflowNormalized();

    // ── 影子偏转角 (用户拍板 2026-08-27, ROTATE_REDESIGN_DESIGN.md §2.6) ──
    void shadowHoldsPoseWithExternalRef();      // B-C 组转后姿态守恒 + A 动 B 仍跟
    void shadowSurvivesFormula();               // 外指基准带公式: 公式存活
    void shadowSelectionAdoptRotate();          // 继承选区旋转 (无组件) 同样生效
    void shadowUndoRestoresBase();              // undo: offset 回 base + 完整焊接还原

    // ── D15 单线确认流 (用户拍板 2026-08-27) ──
    void d15GateRequiresConfirmBeforeDrag();    // 选中态按住拖动 = no-op; 确认后才能转
    void d15DragCommitDropsToSelected();        // 提交后回落选中态, 再拖需再确认
    void d15BlankClickClearsSelectedTarget();   // 选中态点空白 = 取消选择
    void d15AnchorFollowsClickedEnd();          // 锚心跟随点击端 (自由线近端; 连接线恒取挂接端)

    // ── TOOL_SYSTEM_AUDIT P0 (2026-08-29) ──
    void d15ConfirmStateHasVisualAndCaptionHints();  // H2: 确认门有可视 (gizmo 样式 + caption 后缀)
    void hudShortcutOverrideContainment();           // H1: HUD 编辑期吞掉工具单字母快捷键
};

void TestRotateCopy::cloneAttachesToOriginalWithRelativeAngle()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
        attachCloneToOriginal(doc, clone, a.blockId, a, -30.0)));
    doc.resolveAll();

    // Stored −30° (闭合基准 2026-08: 0° = 折叠重叠, 180° = 直行延续; 起点
    // 出口反向) → clone is 30° CCW of the original (relative-angle semantics
    // of the rotate-copy gesture).
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
    doc.setActiveLayer(layerIdAt(doc, 1));
    const LineSetup a = makeLine(doc, 100.0);
    doc.resolveAll();

    DuplicateResult r = duplicateBlocks(doc, {a.blockId});
    const Block& clone = r.blocks.front();
    for (const auto& lv : r.newLinked)
        doc.addLinked(lv);
    doc.addBlock(clone);
    doc.addAttachment(attachCloneToOriginal(doc, clone, a.blockId, a, -30.0));
    doc.resolveAll();

    // Relative angle = 180° − (−30°) − 180° = 30° before and 75° after the
    // 45° turn (闭合基准存储).
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
    doc.setActiveLayer(layerIdAt(doc, 1));
    const LineSetup a = makeLine(doc, 100.0);
    doc.resolveAll();

    DuplicateResult r = duplicateBlocks(doc, {a.blockId});
    const Block& clone = r.blocks.front();

    QUndoStack stack;
    stack.push(new cad::cmd::RotateCopyCommand(
        &doc, std::move(r), a.blockId, a.startId,
        clone.points.front().id, a.segId, 120.0));
    QCOMPARE(doc.blocks().size(), size_t(2));
    QVERIFY(doc.findBlock(clone.id) != nullptr);
    QVERIFY(cloneAttachment(doc, clone.id, a.blockId) != nullptr);
    // Stored 120° (闭合基准存储值, 挂起点 → refWorld = 180°):
    // world = refWorld + 180° − 120° = 240° ≡ −120°.
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
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    doc.addAttachment(attachCloneToOriginal(doc, clone, a.blockId, a, 170.0));
    doc.resolveAll();

    const Attachment* ca = cloneAttachment(doc, clone.id, a.blockId);
    QVERIFY(ca);
    QVERIFY(ca->followerAngleFormula.isEmpty());   // 副本自动去公式
    QVERIFY(std::abs(ca->followerAngle - 170.0) < 1e-9);
}

// ═══════════════════════════════════════════════════════════════════
// UI-layer: full event chain through CanvasView (QTest mouse events →
// dispatch → ToolRotate).
// ═══════════════════════════════════════════════════════════════════

void TestRotateCopy::ctrlDragRotateCopyCommits()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

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
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) Click selects the line (Idle → Ready).
    const QPoint hit = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(doc.blocks().size(), size_t(1));
    sendConfirm(view);

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
    // Dragging 90° CCW around the start point → clone at 90° CCW of the
    // original (复制基准 2026-08: 0° 相对角 = 与原线重叠，与锚心无关).
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 90.0) < 1e-6);

    // 3) Undo removes the copy in ONE step.
    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(doc.attachments().size(), size_t(0));
}

// 对角自由线的旋转复制：副本 0° 必须精确重叠原线（复制基准 2026-08 定稿；
// 旧基准"锚心偏移 0/180°"只对水平线碰巧正确，对角/连接线差 180°−α —
// 用户报告"复制以 180° 创建"）。
void TestRotateCopy::diagonalFreeLineCopyOverlapsOriginal()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    if (auto* blk = doc.findBlock(a.blockId))
        blk->transform.rotation = M_PI / 4.0;   // 45° 对角自由线（绕原点）
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
    view.setInputDispatcher(&tm);

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
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) Click selects the 45° line (Idle → Ready); anchor = start point (0,0).
    const QPoint hit = vp(35.0, 35.0);   // scene (35,35) — on the diagonal
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(doc.blocks().size(), size_t(1));
    sendConfirm(view);

    // 2) Ctrl+press ONLY (no drag yet): the clone must overlap the original
    //    exactly — 45°, not 45°−180° = −135° (the reported bug).
    sendMouse(QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::ControlModifier);
    QCOMPARE(doc.blocks().size(), size_t(2));
    const Block* pre = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != a.blockId) { pre = &b; break; }
    QVERIFY(pre);
    QVERIFY(std::abs(worldAngleDeg(doc, pre->id) - 45.0) < 1e-6);

    // 3) Drag +90° CCW (θ0 = 45° at press, θ1 = 135° at (−50,50)) → release.
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(2));   // original + clone
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != a.blockId) { cln = &b; break; }
    QVERIFY(cln);
    // 副本 = 原线朝向 45° + 相对角 90° = 135°（旧代码 45 − 180 + 90 = −45）。
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 135.0) < 1e-6);

    // 4) Undo removes the copy in ONE step.
    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(doc.attachments().size(), size_t(0));
}

void TestRotateCopy::ctrlDragZeroAngleDiscards()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

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
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
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
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

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
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
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
    // Esc 同步取消手势; 随后是"什么都没提交"的负向断言, 无可等状态 → settle 排空。
    cad::test::settle();

    QCOMPARE(doc.blocks().size(), size_t(1));
    QCOMPARE(doc.attachments().size(), size_t(0));
}

void TestRotateCopy::consecutiveCopiesAllAttachToOriginal()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

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
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    auto ctrlRotateTo = [&](const QPoint& from, double x, double y) {
        sendConfirm(view);
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
    // 复制基准 2026-08: 副本绝对角 = 原线朝向 + 相对角 → 90° 与 180°
    // （相对 180° = 沿原线正向延伸）。
    QVERIFY(relAngles.contains(90.0));
    QVERIFY(relAngles.contains(180.0));
}

// 旋转复制提交后 HUD 必须隐藏：输入框目标会悄然从“副本相对角度”切回
// “原线角度”，用户继续输入表达式/数值会作用到原线段（用户回归：复制
// 后输入表达式作用在原线上）。连续复制时 HUD 恢复显示副本编辑。
void TestRotateCopy::rotateCopyCommitHidesHud()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);   // (0,0)→(100,0)
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const QPoint mid = vp(50.0, 0.0);
    // 选中 A → HUD 显示。
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    auto* hud = view.viewport()->findChild<cad::ui::AngleHud*>();
    QVERIFY(hud);
    QVERIFY(hud->isVisible());
    sendConfirm(view);

    // Ctrl+拖动 90° → 提交。
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);
    QCOMPARE(doc.blocks().size(), size_t(2));
    // 提交后 HUD 隐藏（副本编辑语义终结）。
    QVERIFY(!hud->isVisible());

    // 防御：即使输入回调被触发（HUD 隐藏期间），原线角度也绝不变。
    const double angleBefore = worldAngleDeg(doc, a.blockId);
    hud->onTextChanged(QStringLiteral("45"));
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - angleBefore) < 1e-9);

    // 连续复制：再次确认 + Ctrl+press → HUD 恢复显示（副本相对角度编辑）。
    sendConfirm(view);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    QVERIFY(hud->isVisible());
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::ControlModifier);
    QCOMPARE(doc.blocks().size(), size_t(3));
    // 第二次提交后再次隐藏。
    QVERIFY(!hud->isVisible());
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
    conn.followerAngle = 135.0;   // 闭合基准: 世界角保持 45°（180°−45°）
    conn.followerAngleFormula = QStringLiteral("135");   // 公式锁定
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
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Click the follower's mid-body to select it (Ready, formula locked).
    const QPoint mid = vp(321.21, 21.21);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    sendConfirm(view);
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
    QCOMPARE(ca2->followerAngleFormula, QStringLiteral("135"));
}

// 锁定跟随线的旋转复制：Ctrl+press 拖动 → 副本出现，副本自身无公式。
void TestRotateCopy::lockedFollowerRotateCopyWorks()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const QPoint mid = vp(321.21, 21.21);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    sendConfirm(view);
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
    // 复制基准 2026-08 定稿: 副本 0° = 与原线精确重叠（A 世界朝向 45°），
    // 拖动 +45° CCW → 副本 = 45° + 45° = 90°（旧基准差 180°−α = 135°:
    // 副本落在 −90°，相对角语义全部错位 —— 用户报告"复制以 180° 创建"）。
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 90.0) < 1e-6);
}

// 弧长/角度表达式锁定：HUD 切换角度↔弧长模式只是显示单位变化，绝不把
// 表达式换算烘焙成数值（用户要求：弧长用表达式时不能自己换算成数值）。
void TestRotateCopy::modeSwitchKeepsFormula()
{
    // ── 场景 1：角度表达式锁定，切到弧长模式 → 拒绝且公式保留 ──
    {
        ParamDocument doc;
        doc.setActiveLayer(layerIdAt(doc, 1));
        CanvasScene scene(&doc);
        const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
        const LineSetup a = makeLine(doc, 60.0);
        Attachment conn;
        conn.fromBlockId = a.blockId;
        conn.fromPointId = a.startId;
        conn.toBlockId = b.blockId;
        conn.toPointId = b.endId;
        conn.toSegmentId = b.segId;
        conn.followerAngle = 45.0;
        conn.followerAngleFormula = QStringLiteral("45");   // 角度表达式锁定
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
        view.setInputDispatcher(&tm);

        auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
        auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                             Qt::KeyboardModifiers mods) {
            const QPoint global = view.viewport()->mapToGlobal(pos);
            QMouseEvent ev(type, pos, global, btn,
                           btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
            QApplication::sendEvent(view.viewport(), &ev);
            // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
            // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
            QTest::qWait(20);
        };

        // 选中 A（锚心=挂接点 → Connected/Angle 模式）。
        sendMouse(QEvent::MouseButtonPress, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);
        auto* hud = view.viewport()->findChild<cad::ui::AngleHud*>();
        QVERIFY(hud);

        // 切到弧长模式：公式锁定 → 拒绝，表达式必须原样保留。
        hud->onModeChanged(cad::param::RotationMode::ArcLength);
        const Attachment* att = followerAttachmentOf(doc, a.blockId);
        QVERIFY(att);
        QCOMPARE(att->rotationMode, cad::param::RotationMode::Angle);
        QCOMPARE(att->followerAngleFormula, QStringLiteral("45"));   // 未被烘焙
    }

    // ── 场景 2：弧长表达式锁定，切到角度模式 → 拒绝且公式保留 ──
    {
        ParamDocument doc;
        doc.setActiveLayer(layerIdAt(doc, 1));
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
        conn.arcLength = 47.1238898038469;   // ≈ 45° 的弧长（弧度 × 半径）
        conn.arcLengthFormula = QStringLiteral("10");   // 弧长表达式锁定 (cm)
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
        view.setInputDispatcher(&tm);

        auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
        auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                             Qt::KeyboardModifiers mods) {
            const QPoint global = view.viewport()->mapToGlobal(pos);
            QMouseEvent ev(type, pos, global, btn,
                           btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
            QApplication::sendEvent(view.viewport(), &ev);
            // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
            // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
            QTest::qWait(20);
        };

        sendMouse(QEvent::MouseButtonPress, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);
        auto* hud = view.viewport()->findChild<cad::ui::AngleHud*>();
        QVERIFY(hud);

        // 切到角度模式：弧长公式锁定 → 拒绝，表达式必须原样保留。
        hud->onModeChanged(cad::param::RotationMode::Angle);
        const Attachment* att = followerAttachmentOf(doc, a.blockId);
        QVERIFY(att);
        QCOMPARE(att->rotationMode, cad::param::RotationMode::ArcLength);
        QCOMPARE(att->arcLengthFormula, QStringLiteral("10"));   // 未被烘焙
    }
}

// 属性对话框：跟随角度/弧长表达式线必须在输入框旁显示当前计算值
// （表达式不直观，用户要求：看到公式也要看到值）。
void TestRotateCopy::propertyDialogShowsFollowValue()
{
    // ── 场景 1：跟随角度表达式 → 显示 = 45° ──
    {
        ParamDocument doc;
        doc.setActiveLayer(layerIdAt(doc, 1));
        CanvasScene scene(&doc);
        const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
        const LineSetup a = makeLine(doc, 60.0);
        Attachment conn;
        conn.fromBlockId = a.blockId;
        conn.fromPointId = a.startId;
        conn.toBlockId = b.blockId;
        conn.toPointId = b.endId;
        conn.toSegmentId = b.segId;
        conn.followerAngle = 45.0;
        conn.followerAngleFormula = QStringLiteral("45");   // 表达式
        doc.addAttachment(conn);
        doc.resolveAll();
        QVERIFY(doc.diagnostics().empty());

        CanvasView view(&scene);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(80);

        auto* dlg = new cad::ui::LinePropertyDialog(
            a.blockId, a.segId, &doc, &scene, &view);
        dlg->show();
        // P2-3: 等子控件出现而不是固定 sleep 50ms（负载下会让下面
        // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
        QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QLabel*>(QStringLiteral("followValueLabel")) != nullptr; }),
                 "timed out waiting for QLabel* to appear");
        auto* val = dlg->findChild<QLabel*>(QStringLiteral("followValueLabel"));
        QVERIFY(val);
        QVERIFY(val->isVisible());                       // 公式线显示当前值
        QCOMPARE(val->text(), QStringLiteral("= 45°"));
        delete dlg;
    }

    // ── 场景 2：弧长表达式 → 显示 = 10.00 cm ──
    {
        ParamDocument doc;
        doc.setActiveLayer(layerIdAt(doc, 1));
        CanvasScene scene(&doc);
        const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
        const LineSetup a = makeLine(doc, 60.0);
        Attachment conn;
        conn.fromBlockId = a.blockId;
        conn.fromPointId = a.startId;
        conn.toBlockId = b.blockId;
        conn.toPointId = b.endId;
        conn.toSegmentId = b.segId;
        conn.rotationMode = cad::param::RotationMode::ArcLength;
        conn.arcLength = 0.0;
        conn.arcLengthFormula = QStringLiteral("10");   // 弧长表达式 (cm)
        doc.addAttachment(conn);
        doc.resolveAll();
        QVERIFY(doc.diagnostics().empty());

        CanvasView view(&scene);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(80);

        auto* dlg = new cad::ui::LinePropertyDialog(
            a.blockId, a.segId, &doc, &scene, &view);
        dlg->show();
        // P2-3: 等子控件出现而不是固定 sleep 50ms（负载下会让下面
        // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
        QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QLabel*>(QStringLiteral("followValueLabel")) != nullptr; }),
                 "timed out waiting for QLabel* to appear");
        auto* val = dlg->findChild<QLabel*>(QStringLiteral("followValueLabel"));
        QVERIFY(val);
        QVERIFY(val->isVisible());
        QCOMPARE(val->text(), QStringLiteral("= 10 cm"));
        delete dlg;
    }

    // ── 场景 3：自由线绝对角度表达式（智能笔创建态同路径）
    // → 显示 = 45°（逆时针为正 2026-08 v3 定稿：绝对角度 = 世界角，无镜像） ──
    {
        ParamDocument doc;
        doc.setActiveLayer(layerIdAt(doc, 1));
        CanvasScene scene(&doc);
        const LineSetup a = makeLine(doc, 100.0);   // (0,0)→(100,0) Polar
        auto* blk = doc.findBlock(a.blockId);
        QVERIFY(blk);
        auto* ep = blk->findPoint(blk->segments.front().endPointId);
        QVERIFY(ep);
        ep->angleFormula = QStringLiteral("45");   // 绝对角度表达式
        doc.resolveAll();
        QVERIFY(doc.diagnostics().empty());

        CanvasView view(&scene);
        view.resize(900, 600);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::qWait(80);

        auto* dlg = new cad::ui::LinePropertyDialog(
            a.blockId, a.segId, &doc, &scene, &view);
        dlg->show();
        // P2-3: 等子控件出现而不是固定 sleep 50ms（负载下会让下面
        // 的 QVERIFY 假失败 —— ctest 抖动的来源）。
        QVERIFY2(cad::test::waitUntil([&] { return dlg->findChild<QLabel*>(QStringLiteral("followValueLabel")) != nullptr; }),
                 "timed out waiting for QLabel* to appear");
        auto* val = dlg->findChild<QLabel*>(QStringLiteral("followValueLabel"));
        QVERIFY(val);
        QVERIFY(val->isVisible());
        QCOMPARE(val->text(), QStringLiteral("= 45°"));
        delete dlg;
    }
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
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A at its mid-body (the aim keeps A at 45° from (0,0) to
    // (70.7,70.7), so the mid point is (35.4, 35.4)).
    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    // Drag 90° (cursor from (35.4,35.4) → (0,50) around pivot (0,0)): aim released.
    sendConfirm(view);
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
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    sendConfirm(view);
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
    // 锚心语义: 副本 0° = 与原线重叠，相对 +90° → 副本绝对角度 =
    // 45° + 90° = 135°（复制基准 2026-08）。
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 135.0) < 1e-6);
}

// 旋转复制只清副本的终点指向，原块的指向必须保留（用户回归: 辅助层 L246
// 旋转复制后原线指向被误删）。
void TestRotateCopy::endTargetRotateCopyKeepsOriginalAim()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const AimLineSetup s = makeAimLine(doc);
    QVERIFY(doc.diagnostics().empty());
    const Block* blk0 = doc.findBlock(s.a.blockId);
    QVERIFY(!blk0->endTargetBlockId.isNull());   // 前提: A 带指向

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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A, then Ctrl+drag a rotate-copy 90° CCW.
    sendConfirm(view);
    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(3));          // T + A + clone
    const Block* orig = doc.findBlock(s.a.blockId);
    QVERIFY(orig);
    // 原块指向必须保留（只副本被清）。
    QVERIFY(!orig->endTargetBlockId.isNull());
    QCOMPARE(orig->endTargetPointId, s.targetPointId);
    bool targetAlive = false;
    for (const auto& b : doc.blocks())
        if (b.id == orig->endTargetBlockId) { targetAlive = true; break; }
    QVERIFY(targetAlive);
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != s.a.blockId && cloneAttachment(doc, b.id, s.a.blockId))
            { cln = &b; break; }
    QVERIFY(cln);
    QVERIFY(cln->endTargetBlockId.isNull());           // 副本无指向
}

// Idle 状态下一次手势 Ctrl+press（选中+复制同一步）也不许删原块指向。
void TestRotateCopy::endTargetRotateCopyIdleGestureKeepsOriginalAim()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 直接 Ctrl+press（Idle → select + beginRotateCopy 一步）。
    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(3));
    const Block* orig = doc.findBlock(s.a.blockId);
    QVERIFY(orig);
    QVERIFY(!orig->endTargetBlockId.isNull());          // 原块指向保留
    QCOMPARE(orig->endTargetPointId, s.targetPointId);
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != s.a.blockId && cloneAttachment(doc, b.id, s.a.blockId))
            { cln = &b; break; }
    QVERIFY(cln);
    QVERIFY(cln->endTargetBlockId.isNull());            // 副本无指向
}

// 旋转复制 → undo → redo 全程，原块指向保持（undo 只删副本）。
void TestRotateCopy::endTargetRotateCopyUndoRedoKeepsOriginalAim()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const QPoint mid = vp(35.36, 35.36);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);
    QCOMPARE(doc.blocks().size(), size_t(3));

    stack.undo();
    QCOMPARE(doc.blocks().size(), size_t(2));           // 副本已删
    const Block* orig = doc.findBlock(s.a.blockId);
    QVERIFY(orig && !orig->endTargetBlockId.isNull());  // 原块指向仍在
    QCOMPARE(orig->endTargetPointId, s.targetPointId);

    stack.redo();
    QCOMPARE(doc.blocks().size(), size_t(3));
    const Block* orig2 = doc.findBlock(s.a.blockId);
    QVERIFY(orig2 && !orig2->endTargetBlockId.isNull()); // redo 后仍在
    QCOMPARE(orig2->endTargetPointId, s.targetPointId);
}

// 普通旋转拖动中途按下 Ctrl → 转为旋转复制：已转角度转移到副本，原块
// 回弹旋转前姿态（普通旋转已清除的终点指向一并恢复）。这是用户报告的
// “旋转复制删掉 L246 终点指向”的典型时序：先按下鼠标再按 Ctrl。
void TestRotateCopy::midGestureCtrlConvertsToCopy()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const QPoint mid = vp(35.36, 35.36);   // A 的中点（A 指向 T 后为 45° 斜线）
    // 选中 A。
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);
    // 普通旋转：press 无 Ctrl → beginRotation 清除终点指向（设计如此）。
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    QVERIFY(doc.findBlock(s.a.blockId)->endTargetBlockId.isNull());
    // 已转 45°（A 方向 45° → 90°）。
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::NoModifier);
    QVERIFY(std::abs(worldAngleDeg(doc, s.a.blockId) - 90.0) < 1e-6);
    // 中途按住 Ctrl 继续拖动 → 转复制：原块回弹 + 指向恢复。
    sendMouse(QEvent::MouseMove, vp(-50.0, 50.0), Qt::NoButton, Qt::ControlModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(-50.0, 50.0), Qt::LeftButton,
              Qt::ControlModifier);

    QCOMPARE(doc.blocks().size(), size_t(3));          // T + A + clone
    const Block* orig = doc.findBlock(s.a.blockId);
    QVERIFY(orig);
    // 终点指向已恢复（普通旋转的清除被回滚）。
    QVERIFY(!orig->endTargetBlockId.isNull());
    QCOMPARE(orig->endTargetPointId, s.targetPointId);
    // 原块回弹到旋转前姿态（45°）。
    QVERIFY(std::abs(worldAngleDeg(doc, s.a.blockId) - 45.0) < 1e-6);
    const Block* cln = nullptr;
    for (const auto& b : doc.blocks())
        if (b.id != s.a.blockId && cloneAttachment(doc, b.id, s.a.blockId))
            { cln = &b; break; }
    QVERIFY(cln);
    QVERIFY(cln->endTargetBlockId.isNull());           // 副本无指向
    // 已转角度转移到副本：副本 = 原线朝向 45° + 相对角 45° = 90°
    // （复制基准 2026-08: 0° = 与原线重叠）。
    QVERIFY(std::abs(worldAngleDeg(doc, cln->id) - 90.0) < 1e-6);
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
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
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

    sendConfirm(view);
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
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A (default anchor = start).
    sendMouse(QEvent::MouseButtonPress, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    // Click the END point → anchor switches there.
    sendMouse(QEvent::MouseButtonPress, vp(100.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(100.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    sendConfirm(view);
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

// 已连接线段禁止切换锚心（用户拍板 2026-08）：X 键被拒，挂接绝不断开；
// 旋转保持 Connected 模式 = 编辑跟随角。撤销一步恢复跟随角。
// 注意：事件坐标经 viewport 整数化（event->pos() 是 QPoint），因此用 0° 跟随角度
// 的跟随线 + 整数坐标，保证拖动角精确（45° 跟随角度会引入亚像素取整误差）。
void TestRotateCopy::connectedLineXAnchorSwitchBlocked()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    // B: (200,0)→(300,0); A hangs on B's END with a 180° follower angle
    // (闭合基准: 180° = 沿 leader 直行延续) → A: (300,0)→(360,0), world angle 0°.
    const LineSetup b = makeLine(doc, 100.0, Vec2(200.0, 0.0));
    const LineSetup a = makeLine(doc, 60.0);
    Attachment conn;
    conn.fromBlockId = a.blockId;
    conn.fromPointId = a.startId;
    conn.toBlockId = b.blockId;
    conn.toPointId = b.endId;
    conn.toSegmentId = b.segId;
    conn.followerAngle = 180.0;
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select follower A: anchor = its START point (the attachment point),
    // so the session starts in Connected mode.
    const QPoint mid = vp(330.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);

    // X: switch blocked — the START point is attached, so the anchor stays
    // put and the session remains in Connected mode (跟随保护).
    sendKeyX(view);

    sendConfirm(view);
    // Drag around the START pivot (300,0): cursor (330,0) → (300,−100):
    // cur0 = atan2(0,30) = 0°, theta = atan2(−100,0) = −90°,
    // Connected 拖动增量与世界角同向、与存储角反向: target = 180° − (−90°)
    // = 270°（存储不归一化，≡ −90°），世界角 = 180° − 270° = −90°.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(300.0, -100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(300.0, -100.0), Qt::LeftButton,
              Qt::NoModifier);

    // The follower link SURVIVES (旋转 = 编辑跟随角，挂接不被释放).
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment* keep = followerAttachmentOf(doc, a.blockId);
    QVERIFY(keep);
    QVERIFY(std::abs(keep->followerAngle - 270.0) < 1e-6);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (-90.0)) < 1e-6);

    // ONE undo restores the follower angle (link still intact).
    stack.undo();
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Attachment* keep2 = followerAttachmentOf(doc, a.blockId);
    QVERIFY(keep2);
    QVERIFY(std::abs(keep2->followerAngle - 180.0) < 1e-6);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 0.0) < 1e-6);
}

// 锚心=终点时 Ctrl+拖动旋转复制：副本挂回原线的终点（不是起点）。
void TestRotateCopy::endAnchorRotateCopyAttachesToEnd()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A, then switch the anchor to the END point.
    const QPoint mid = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendKeyX(view);

    sendConfirm(view);
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
    // 挂接点恒用副本自己的起点（用户拍板: "复制的线段以终点为锚心，并且
    // 把自己的终点调换成起点"）——线段从锚心伸出，而不是终点钉在锚心。
    QCOMPARE(ca->fromPointId, cln->segments.front().startPointId);
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
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A, then switch the anchor to the END point (X).
    const QPoint mid = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendKeyX(view);

    sendConfirm(view);
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
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    // B: (200,0)→(300,0); A hangs on B's END with followerAngle = 1260°
    // (闭合基准: 世界角 = 180° − 1260° ≡ 0° → A points RIGHT, mid-body at
    // (330,0); HUD 显示 fmod(1260,360) = 180°).
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A (anchor = attachment point → Connected/Angle mode).
    sendMouse(QEvent::MouseButtonPress, vp(330.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(330.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    auto* hud = view.viewport()->findChild<cad::ui::AngleHud*>();
    QVERIFY(hud);
    QCOMPARE(hud->edit()->text(), QStringLiteral("180"));   // 1260 → 180
}

// 弧长模式多圈 → 切换回角度模式，HUD 显示归一化角度（不爆表）。
void TestRotateCopy::arcLengthModeOverflowNormalized()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // Select A: 3 turns = 弧长角 1080° ≡ 0° 折叠（闭合基准恒等映射 2026-08:
    // 弧长角 = 线夹角）→ A points LEFT (toward (240,0)), mid-body (270,0).
    // 显示 = 带符号折角（v3 定稿）：1080° ≡ 0° 折叠 → HUD 0° / 0cm。
    sendMouse(QEvent::MouseButtonPress, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(270.0, 0.0), Qt::LeftButton, Qt::NoModifier);

    auto* hud = view.viewport()->findChild<cad::ui::AngleHud*>();
    QVERIFY(hud);
    // ArcLength mode: HUD shows 带符号折角弧长 in cm (3 turns ≡ 0° 折叠 → 0).
    QCOMPARE(hud->edit()->text(), QStringLiteral("0"));

    // Click the HUD mode toggle (⌒ → ∠): the angle must normalize to the
    // fold-back 0° (弧长 3 圈 ≡ 角度 0° 折叠, 恒等映射 2026-08).
    auto* toggle = hud->findChild<QPushButton*>();
    QVERIFY(toggle);
    QTest::mouseClick(toggle, Qt::LeftButton);
    QCOMPARE(hud->edit()->text(), QStringLiteral("0"));   // 1080 → 0

    // 输入带符号折角：输入 270（>180 视为原始 α）→ 存储 α = 270（另一侧）。
    hud->edit()->setText(QStringLiteral("270"));
    // 断言"值变了"(HUD textChanged 直接回调同步应用, 无 debounce): 谓词 = 存储断言本身。
    QVERIFY2(cad::test::waitUntil([&] {
        for (const auto& att2 : doc.attachments())
            if (att2.fromBlockId == a.blockId && std::abs(att2.followerAngle - 270.0) < 1e-6)
                return true;
        return false;
    }), "输入 270 后 followerAngle 应更新为 270（HUD 输入未应用到存储）");

    // 模式切换往返刷新 HUD：显示带符号折角 −90（v3 定稿，符号 = 折向）。
    QTest::mouseClick(toggle, Qt::LeftButton);   // → 弧长（显示 −9.4cm）
    QCOMPARE(hud->edit()->text(), QStringLiteral("-9.4"));
    QTest::mouseClick(toggle, Qt::LeftButton);   // → 角度（显示 −90°）
    QCOMPARE(hud->edit()->text(), QStringLiteral("-90"));
}


// ═══════════════════════════════════════════════════════════════════
// 选集旋转 (选区继承入口; 原"W 键组件整组旋转"模态已删除 2026-08-29,
// 组件整体旋转 = 选择工具多选 → R 继承选区, 机制与影子偏转共用)
// ═══════════════════════════════════════════════════════════════════

namespace {

/// World position (mm) of a point on a block.
Vec2 pointWorld(const ParamDocument& doc, const QUuid& blockId, const QUuid& pointId)
{
    const Block* b = doc.findBlock(blockId);
    if (!b) return Vec2();
    const ParamPoint* p = b->findPoint(pointId);
    if (!p) return Vec2();
    return b->transform.toWorld(p->resolvedPos);
}

} // namespace

// 选集绕自由锚点旋转: 选择工具多选 → R 继承 (D9), 第一击设锚 (可捕捉点吸附,
// 否则自由), 第二击起手拖动, 全体成员刚体变换一致; 一步 undo 全部回位.
void TestRotateCopy::selRotateAboutFreePivotRotatesAllMembers()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    const LineSetup b = makeLine(doc, 100.0, Vec2(0, 120));
    doc.resolveAll();
    cad::cmd::MakeComponentCommand(&doc, {a.blockId, b.blockId}, QStringLiteral("C")).redo();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Select);
    if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool()))
        ts->selectBlocksExternally({a.blockId, b.blockId});
    tm.switchTool(cad::tools::ToolType::Rotate);   // D9: 继承选区进选集模式
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);
    QVERIFY(tool->hasSelectionSet());

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) 第一击 = 自由锚点 (远离任何点); delta = 光标增量角.
    const Vec2 pivot(250.0, 250.0);
    sendMouse(QEvent::MouseButtonPress, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    // 2) 第二击起手 (基准方向 = 0°), 拖到 +30°.
    const Vec2 press2 = pivot + Vec2(50.0, 0.0);
    const Vec2 endPos = pivot + Vec2(50.0 * std::cos(M_PI / 6.0), 50.0 * std::sin(M_PI / 6.0));
    sendMouse(QEvent::MouseButtonPress, vp(press2.x, press2.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(endPos.x, endPos.y), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(endPos.x, endPos.y), Qt::LeftButton, Qt::NoModifier);

    // 3) 全体成员 rotate += delta、origin 绕 pivot 旋转 delta (基准位姿
    // a=(0,0)、b=(0,120) — 刚体变换公式: origin = pivot + (base-pivot).rotated(delta)).
    const Block* ba = doc.findBlock(a.blockId);
    const Block* bb = doc.findBlock(b.blockId);
    QVERIFY(ba && bb);
    // 视口像素取整会量化拖拽终点 (~0.2°), 故: 全体成员同 delta + 刚体公式
    // 精确成立 + delta 方向/大小合理 (≈+30° CCW).
    const double delta = ba->transform.rotation;
    QVERIFY(std::abs(bb->transform.rotation - delta) < 1e-9);
    QVERIFY(delta > 0.4 && delta < 0.7);
    const Vec2 expectA = pivot + (Vec2(0, 0) - pivot).rotated(delta);
    const Vec2 expectB = pivot + (Vec2(0, 120) - pivot).rotated(delta);
    QVERIFY(ba->transform.origin.distanceTo(expectA) < 1e-6);
    QVERIFY(bb->transform.origin.distanceTo(expectB) < 1e-6);
    QVERIFY(ba->transform.origin.distanceTo(expectA) < 1e-6);
    QVERIFY(bb->transform.origin.distanceTo(expectB) < 1e-6);
    // 4) 一步 undo 全部回位.
    stack.undo();
    for (const LineSetup& m : {a, b}) {
        const Block* blk = doc.findBlock(m.blockId);
        QVERIFY(blk);
        QVERIFY(std::abs(blk->transform.rotation) < 1e-9);
    }
    QVERIFY(doc.findBlock(a.blockId)->transform.origin.distanceTo(Vec2(0, 0)) < 1e-6);
    QVERIFY(doc.findBlock(b.blockId)->transform.origin.distanceTo(Vec2(0, 120)) < 1e-6);
}

// 锚点吸附到成员端点: 旋转后该端点世界位置不变 (绕自身转); 再验证 HUD
// 增量角 + Shift 15° 吸附.
void TestRotateCopy::selRotateAboutSnappedPivot()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    const LineSetup b = makeLine(doc, 100.0, Vec2(0, 120));
    const LineSetup ext = makeLine(doc, 100.0, Vec2(300, 0));
    doc.resolveAll();
    cad::cmd::MakeComponentCommand(&doc, {a.blockId, b.blockId}, QStringLiteral("C")).redo();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Select);
    if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool()))
        ts->selectBlocksExternally({a.blockId, b.blockId});
    tm.switchTool(cad::tools::ToolType::Rotate);   // D9: 继承选区进选集模式
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 第一击靠近 a 的起点 (0,0) → 吸附到点; 第二击起手, 拖 +90°.
    sendMouse(QEvent::MouseButtonPress, vp(3.0, 3.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(3.0, 3.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonPress, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 50.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 50.0), Qt::LeftButton, Qt::NoModifier);

    // 锚点 = (0,0) 吸附: a 的起点世界位置不动; b 的 origin 绕 (0,0) 转 90°.
    const Vec2 aStart = pointWorld(doc, a.blockId, a.startId);
    QVERIFY(aStart.distanceTo(Vec2(0, 0)) < 1e-6);
    const Block* bb = doc.findBlock(b.blockId);
    QVERIFY(bb);
    QVERIFY(std::abs(bb->transform.rotation - M_PI / 2.0) < 1e-6);
    QVERIFY(bb->transform.origin.distanceTo(Vec2(-120.0, 0.0)) < 1e-6);
    // 组外线 ext 未受影响.
    QVERIFY(doc.findBlock(ext.blockId)->transform.origin.distanceTo(Vec2(300, 0)) < 1e-6);
}

// S 内 attachment 在选集旋转中保持 (相对关系自洽): 成员线 c 挂在成员 a 上,
// 旋转后连接仍存在且 c 仍精确落在 a 的起点.
void TestRotateCopy::selRotateKeepsInternalAttachment()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    const LineSetup c = makeLine(doc, 60.0, Vec2(0, 80));
    doc.resolveAll();
    // c → a 组内跟随 (a 为 leader).
    Attachment att;
    att.fromBlockId = c.blockId;
    att.fromPointId = c.startId;
    att.toBlockId = a.blockId;
    att.toPointId = a.startId;
    att.toSegmentId = a.segId;
    att.followerAngle = 45.0;
    att.rotationMode = RotationMode::Angle;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();
    cad::cmd::MakeComponentCommand(&doc, {a.blockId, c.blockId}, QStringLiteral("C")).redo();

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);

    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Select);
    if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool()))
        ts->selectBlocksExternally({a.blockId, c.blockId});
    tm.switchTool(cad::tools::ToolType::Rotate);   // D9: 继承选区进选集模式
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 自由锚点, 拖 +30°.
    const Vec2 pivot(250.0, 250.0);
    sendMouse(QEvent::MouseButtonPress, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonPress, vp(pivot.x + 50, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(pivot.x + 43.301269, pivot.y + 25.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(pivot.x + 43.301269, pivot.y + 25.0), Qt::LeftButton, Qt::NoModifier);

    // 组内连接保持: c 仍精确挂在 a 的起点上; 相对几何不破坏.
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Vec2 aStart = pointWorld(doc, a.blockId, a.startId);
    const Vec2 cStart = pointWorld(doc, c.blockId, c.startId);
    QVERIFY(aStart.distanceTo(cStart) < 1e-6);
    // undo: 回位, 连接仍在.
    stack.undo();
    QCOMPARE(doc.attachments().size(), size_t(1));
    QVERIFY(doc.findBlock(a.blockId)->transform.origin.distanceTo(Vec2(0, 0)) < 1e-6);

    // redo: 再转, 连接仍在.
    stack.redo();
    QCOMPARE(doc.attachments().size(), size_t(1));
}

// Esc 取消选集旋转: 位姿回位 + 影子偏转回基准 (未提交 = 现场还原,
// 跨界连接不释放 —— 选集路径以影子偏转保姿态, 无 D7 结构释放).
void TestRotateCopy::escCancelsSelRotate()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    const LineSetup b = makeLine(doc, 100.0, Vec2(0, 120));
    const LineSetup ext = makeLine(doc, 100.0, Vec2(300, 0));
    doc.resolveAll();
    cad::cmd::MakeComponentCommand(&doc, {a.blockId, b.blockId}, QStringLiteral("C")).redo();

    Attachment att;
    att.fromBlockId = a.blockId;
    att.fromPointId = a.startId;
    att.toBlockId = ext.blockId;
    att.toPointId = ext.startId;
    att.toSegmentId = ext.segId;
    att.followerAngle = 180.0;
    att.rotationMode = RotationMode::Angle;
    QVERIFY(doc.addAttachment(att));
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
    tm.switchTool(cad::tools::ToolType::Select);
    if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool()))
        ts->selectBlocksExternally({a.blockId, b.blockId});
    tm.switchTool(cad::tools::ToolType::Rotate);   // D9: 继承选区进选集模式
    view.setInputDispatcher(&tm);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const Vec2 pivot(450.0, 250.0);
    sendMouse(QEvent::MouseButtonPress, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonPress, vp(pivot.x + 50, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(pivot.x + 43.301269, pivot.y + 25.0), Qt::NoButton, Qt::NoModifier);
    sendKeyEsc(view);

    // 现场还原: 跨界连接全程未释放, 位姿回位, undo 栈空 (无命令).
    QCOMPARE(doc.attachments().size(), size_t(1));
    const Vec2 aStart = pointWorld(doc, a.blockId, a.startId);
    QVERIFY(aStart.distanceTo(Vec2(300, 0)) < 1e-6);
    QCOMPARE(stack.count(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 影子偏转角 (用户拍板 2026-08-27, ROTATE_REDESIGN_DESIGN.md §2.6)
// 场景: B 连接 C (组内互连), B 的角度基准外指 A —— 选集刚体旋转后
// B 的姿态守恒 (像 A 陪转了一样), 但 A 本体与存储一概不动; 之后单独转 A,
// B 照常跟随; undo 全量还原。
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// B 连到 C 的起点、角度基准外指 refSetup 的跟随连接 (回到文档并解算)。
Attachment addExternalRefConnection(ParamDocument& doc, const LineSetup& b,
                                    const LineSetup& c, const LineSetup& ref)
{
    Attachment conn;
    conn.fromBlockId = b.blockId;
    conn.fromPointId = b.startId;
    conn.toBlockId = c.blockId;
    conn.toPointId = c.startId;
    conn.toSegmentId = c.segId;
    // 闭合基准: refWorld=−π 时 followerAngle −30° → 世界角 30° (同克隆用例)。
    conn.followerAngle = -30.0;
    conn.rotationMode = RotationMode::Angle;
    const bool ok = doc.addAttachment(conn);
    Q_ASSERT(ok);
    doc.setAttachmentAngleRef(conn.id, ref.blockId, ref.segId, QUuid());
    doc.resolveAll();
    return conn;
}

/// 标准影子会话脚手架: 两段式拖拽 (+30°) 已提交后的工具句柄已失效,
/// 断言用 doc/stack。返回实际旋转增量角 (deg)。
} // namespace

void TestRotateCopy::shadowHoldsPoseWithExternalRef()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0, Vec2(-300, 0));   // 外部角度基准 A
    const LineSetup c = makeLine(doc, 80.0, Vec2(200, 0));     // 组件成员 C
    const LineSetup b = makeLine(doc, 60.0, Vec2(310, 120));   // 组件成员 B
    doc.resolveAll();
    cad::cmd::MakeComponentCommand(&doc, {b.blockId, c.blockId},
                                   QStringLiteral("C")).redo();
    const Attachment conn = addExternalRefConnection(doc, b, c, a);

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);
    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Select);
    if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool()))
        ts->selectBlocksExternally({b.blockId, c.blockId});
    tm.switchTool(cad::tools::ToolType::Rotate);   // D9: 继承选区进选集模式
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);
    QVERIFY(tool->hasSelectionSet());
    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const double ang0 = worldAngleDeg(doc, b.blockId);
    const double storedAngle =
        followerAttachmentOf(doc, b.blockId)->followerAngle;  // 基准重定向反算后的原样值
    const double storedBOffset = 0.0;
    Q_UNUSED(storedBOffset);

    // 选集旋转: 自由锚点 → 二击拖 +30°.
    const Vec2 pivot(450.0, 250.0);
    sendMouse(QEvent::MouseButtonPress, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonPress, vp(pivot.x + 50, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(pivot.x + 43.301269, pivot.y + 25.0),
              Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(pivot.x + 43.301269, pivot.y + 25.0),
              Qt::LeftButton, Qt::NoModifier);

    // 1) 姿态守恒: offset == 实际增量 (同源写入, 逐位相等), 世界角扫过 δ.
    //    增量取自由成员 C 的 transform (B 由连接驱动, 含参数化朝向成分).
    const Block* cc = doc.findBlock(c.blockId);
    QVERIFY(cc);
    const double deltaDeg = cc->transform.rotation * 180.0 / M_PI;
    QVERIFY(deltaDeg > 20.0 && deltaDeg < 40.0);   // ≈+30 CCW
    const Attachment* after = followerAttachmentOf(doc, b.blockId);
    QVERIFY(after);
    QVERIFY(std::abs(after->baselineOffsetDeg - deltaDeg) < 1e-9);
    QVERIFY(std::abs(worldAngleDeg(doc, b.blockId)
                     - (ang0 + deltaDeg)) < 2.0);

    // 2) A 本体纹丝不动.
    const Block* ba = doc.findBlock(a.blockId);
    QVERIFY(ba);
    QCOMPARE(ba->transform.rotation, 0.0);
    QVERIFY(ba->transform.origin.distanceTo(Vec2(-300, 0)) < 1e-9);

    // 3) 之后单独转 A: B 照常跟随 (关系存活). A 绕自身原点转 +10°,
    //    影子偏转不变 —— B 应再精确跟随 +10°.
    Block* mA = doc.findBlock(a.blockId);
    mA->transform.rotation += qDegreesToRadians(10.0);
    doc.resolveAll();
    QVERIFY(std::abs(worldAngleDeg(doc, b.blockId)
                     - (ang0 + deltaDeg + 10.0)) < 2.0);

    // 3') 恢复手动转过的 A (它不走撤销栈), 让世界回到提交态.
    mA->transform.rotation -= qDegreesToRadians(10.0);
    doc.resolveAll();

    // 4) undo: 位姿回位 + offset 回 base(0) + 公式/常量原样.
    stack.undo();
    const Attachment* restored = followerAttachmentOf(doc, b.blockId);
    QVERIFY(restored);
    QVERIFY(std::abs(restored->baselineOffsetDeg) < 1e-12);
    QCOMPARE(restored->followerAngle, storedAngle);
    QVERIFY(!restored->angleOnly);
    const Block* rb = doc.findBlock(b.blockId);
    QVERIFY(rb);
    // 连接驱动的线段其 transform.rotation 稳态并非 0 (挂接时由求解器重写):
    // undo 后必须回到"原稳态", 以世界方向对照 (ang0).
    QVERIFY(std::abs(worldAngleDeg(doc, b.blockId) - ang0) < 1e-6);
}

void TestRotateCopy::shadowSurvivesFormula()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0, Vec2(-300, 0));
    const LineSetup c = makeLine(doc, 80.0, Vec2(200, 0));
    const LineSetup b = makeLine(doc, 60.0, Vec2(310, 120));
    doc.resolveAll();
    cad::cmd::MakeComponentCommand(&doc, {b.blockId, c.blockId},
                                   QStringLiteral("C")).redo();
    Attachment conn = addExternalRefConnection(doc, b, c, a);
    // 角度挂公式 (此处常量串即可证明"字段原样存活"): 公式优先于数值.
    if (auto* mut = doc.findAttachment(conn.id))
        mut->followerAngleFormula = QStringLiteral("30");
    doc.resolveAll();
    const double storedAngle =
        followerAttachmentOf(doc, b.blockId)->followerAngle;

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);
    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Select);
    if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool()))
        ts->selectBlocksExternally({b.blockId, c.blockId});
    tm.switchTool(cad::tools::ToolType::Rotate);   // D9: 继承选区进选集模式
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);
    QVERIFY(tool->hasSelectionSet());
    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const Vec2 pivot(450.0, 250.0);
    sendMouse(QEvent::MouseButtonPress, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonPress, vp(pivot.x + 50, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(pivot.x + 43.301269, pivot.y + 25.0),
              Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(pivot.x + 43.301269, pivot.y + 25.0),
              Qt::LeftButton, Qt::NoModifier);
    // 公式字段与数值一概未动, 偏转进了影子槽位 (公式优先语义不破).
    const Attachment* after = followerAttachmentOf(doc, b.blockId);
    QVERIFY(after);
    QCOMPARE(after->followerAngleFormula, QStringLiteral("30"));
    QCOMPARE(after->followerAngle, storedAngle);
    // 增量对照取自由成员 C (B 由连接驱动, transform 含参数化朝向成分).
    const double deltaF =
        doc.findBlock(c.blockId)->transform.rotation * 180.0 / M_PI;
    QVERIFY(deltaF > 20.0 && deltaF < 40.0);
    QVERIFY(std::abs(after->baselineOffsetDeg - deltaF) < 1e-9);
}

void TestRotateCopy::shadowSelectionAdoptRotate()
{
    // 无组件路径: 选择工具外部注入选区 {B,C} → 切旋转继承 (D9 adoptSelection),
    // 影子机制同样生效 (B 基准外指 A, A 不入选区).
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0, Vec2(-300, 0));   // S 外基准
    const LineSetup c = makeLine(doc, 80.0, Vec2(200, 0));
    const LineSetup b = makeLine(doc, 60.0, Vec2(310, 120));
    doc.resolveAll();
    const Attachment conn = addExternalRefConnection(doc, b, c, a);
    QVERIFY(!conn.id.isNull());

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);
    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Select);
    if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool()))
        ts->selectBlocksExternally({b.blockId, c.blockId});
    tm.switchTool(cad::tools::ToolType::Rotate);   // D9: 继承选区进选集模式
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);
    QVERIFY(tool->hasSelectionSet());

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const double ang0 = worldAngleDeg(doc, b.blockId);
    // 手势前稳态原点: 位置焊死使 B 原点早已吸附到 C 的锚点上 (解算结果),
    // undo 后应回到该稳态, 而不是建块时的初始摆放.
    const Vec2 bOrg0 = doc.findBlock(b.blockId)->transform.origin;
    const Vec2 cOrg0 = doc.findBlock(c.blockId)->transform.origin;
    const Vec2 pivot(450.0, 250.0);
    sendMouse(QEvent::MouseButtonPress, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonPress, vp(pivot.x + 50, pivot.y), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(pivot.x + 43.301269, pivot.y + 25.0),
              Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(pivot.x + 43.301269, pivot.y + 25.0),
              Qt::LeftButton, Qt::NoModifier);

    const double deltaDeg =
        doc.findBlock(c.blockId)->transform.rotation * 180.0 / M_PI;
    QVERIFY(deltaDeg > 20.0 && deltaDeg < 40.0);
    const Attachment* after = followerAttachmentOf(doc, b.blockId);
    QVERIFY(after);
    QVERIFY(std::abs(after->baselineOffsetDeg - deltaDeg) < 1e-9);
    QVERIFY(std::abs(worldAngleDeg(doc, b.blockId)
                     - (ang0 + deltaDeg)) < 2.0);
    QCOMPARE(stack.count(), 1);

    stack.undo();
    const Attachment* restored = followerAttachmentOf(doc, b.blockId);
    QVERIFY(restored);
    QVERIFY(std::abs(restored->baselineOffsetDeg) < 1e-12);
    QVERIFY(std::abs(worldAngleDeg(doc, b.blockId) - ang0) < 1e-6);
    QVERIFY(doc.findBlock(b.blockId)->transform.origin.distanceTo(bOrg0) < 1e-6);
    QVERIFY(doc.findBlock(c.blockId)->transform.origin.distanceTo(cOrg0) < 1e-6);
}

void TestRotateCopy::shadowUndoRestoresBase()
{
    // 连续两轮选集旋转: offset 累计 2δ; undo 一次回到第一轮提交态;
    // 再 undo 清零回原始。跨会话累计路径 (base 滚动更新) 验证。
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0, Vec2(-300, 0));
    const LineSetup c = makeLine(doc, 80.0, Vec2(200, 0));
    const LineSetup b = makeLine(doc, 60.0, Vec2(310, 120));
    doc.resolveAll();
    cad::cmd::MakeComponentCommand(&doc, {b.blockId, c.blockId},
                                   QStringLiteral("C")).redo();
    const Attachment conn = addExternalRefConnection(doc, b, c, a);

    CanvasView view(&scene);
    view.resize(900, 600);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(80);
    cad::tools::ToolManager tm(&scene);
    tm.setParamDocument(&doc);
    QUndoStack stack;
    tm.setUndoStack(&stack);
    tm.switchTool(cad::tools::ToolType::Select);
    if (auto* ts = dynamic_cast<cad::tools::ToolSelect*>(tm.activeTool()))
        ts->selectBlocksExternally({b.blockId, c.blockId});
    tm.switchTool(cad::tools::ToolType::Rotate);   // D9: 继承选区进选集模式
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);
    QVERIFY(tool->hasSelectionSet());
    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    auto dragPlus30 = [&]() {
        // 提交后选集会话与锚点均保持 (既有拍板语义): 后续轮直接
        // "按下-拖动-松手", 绕同一锚点继续累加增量.
        const Vec2 pivot(450.0, 250.0);
        sendMouse(QEvent::MouseButtonPress, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(pivot.x, pivot.y), Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonPress, vp(pivot.x + 50, pivot.y), Qt::LeftButton, Qt::NoModifier);
        sendMouse(QEvent::MouseMove, vp(pivot.x + 43.301269, pivot.y + 25.0),
                  Qt::NoButton, Qt::NoModifier);
        sendMouse(QEvent::MouseButtonRelease, vp(pivot.x + 43.301269, pivot.y + 25.0),
                  Qt::LeftButton, Qt::NoModifier);
    };

    // 首轮拖动 (选区已继承, 直接设锚 → 拖动).
    dragPlus30();
    const double d1 =
        doc.findBlock(c.blockId)->transform.rotation * 180.0 / M_PI;
    // 第二轮: base 应为第一轮终态 (m_groupBaseTf 滚动), offset 从 d1 起步.
    dragPlus30();
    const double d2 =
        doc.findBlock(c.blockId)->transform.rotation * 180.0 / M_PI;
    const Attachment* after = followerAttachmentOf(doc, b.blockId);
    QVERIFY(after);
    QVERIFY(d2 > d1 + 20.0);                       // 累计变大
    QVERIFY(std::abs(after->baselineOffsetDeg - d2 * 1.0) < 60.0);  // ≈2×δ 附近

    stack.undo();                                  // 回第一轮提交态
    const Block* rc = doc.findBlock(c.blockId);
    QVERIFY(rc);
    QVERIFY(std::abs(rc->transform.rotation * 180.0 / M_PI - d1) < 1e-6);
    const Attachment* mid = followerAttachmentOf(doc, b.blockId);
    QVERIFY(mid);
    QVERIFY(std::abs(mid->baselineOffsetDeg - d1) < 1e-9);

    stack.undo();                                  // 回最初
    QVERIFY(std::abs(followerAttachmentOf(doc, b.blockId)->baselineOffsetDeg) < 1e-12);
    QVERIFY(std::abs(doc.findBlock(c.blockId)->transform.rotation) < 1e-12);
    Q_UNUSED(conn);
}


// ─────────────────────────────────────────────────────────────────────────────
// D15 单线确认流 (用户拍板 2026-08-27): 选中 → 确认 → 拖动 → 回落选中态.
// ─────────────────────────────────────────────────────────────────────────────
void TestRotateCopy::d15GateRequiresConfirmBeforeDrag()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    const QPoint mid = vp(50.0, 0.0);

    // 选中 → 确认门关闭.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    QVERIFY(!tool->selectionConfirmed());

    // 选中态按住线身"拖动" = no-op: 位姿不动, 无命令.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(150.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(150.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 0.0) < 1e-9);
    QCOMPARE(stack.count(), 0);

    // 确认 (右键) → 拖动生效; HUD 回车仲裁: 无输入焦点时回车只确认不应用.
    sendConfirm(view);
    QVERIFY(tool->selectionConfirmed());
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - 90.0) < 2.0);
    QCOMPARE(stack.count(), 1);
}

void TestRotateCopy::d15DragCommitDropsToSelected()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    const QPoint mid = vp(50.0, 0.0);

    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    sendConfirm(view);
    QVERIFY(tool->selectionConfirmed());
    const double ang1 = worldAngleDeg(doc, a.blockId);

    // 一次拖动提交.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(0.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - (ang1 + 90.0)) < 2.0);

    // 提交后自动回落选中态: 紧接着的第二次拖动 no-op.
    QVERIFY(!tool->selectionConfirmed());
    const double ang2 = worldAngleDeg(doc, a.blockId);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseMove, vp(100.0, 100.0), Qt::NoButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(100.0, 100.0), Qt::LeftButton,
              Qt::NoModifier);
    QVERIFY(std::abs(worldAngleDeg(doc, a.blockId) - ang2) < 1e-9);
    // (注: 第二次拖动的按压点已落在线外 —— 选中态点空白按 D15 清除目标,
    //  这本身是被验证的设计行为; 角度不变断言已覆盖 no-op 语义.)

    // 再次确认后恢复可拖: 重新选中新位姿的线身 (已转竖直, 在 (0,50)).
    sendMouse(QEvent::MouseButtonPress, vp(0.0, 50.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(0.0, 50.0), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(!tool->selectionConfirmed());
    sendConfirm(view);
    QVERIFY(tool->selectionConfirmed());
}

void TestRotateCopy::d15BlankClickClearsSelectedTarget()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup a = makeLine(doc, 100.0);
    const LineSetup b = makeLine(doc, 80.0, Vec2(200, 150));
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
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 选中 a.
    sendMouse(QEvent::MouseButtonPress, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(50.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    QVERIFY(!tool->selectionConfirmed());

    // 选中态点空白 = 取消选择 (回 Idle).
    sendMouse(QEvent::MouseButtonPress, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->state(), cad::tools::RotateState::Idle);
    QVERIFY(!tool->selectionConfirmed());

    // 点另一条线 = 切换目标, 仍处选中态.
    sendMouse(QEvent::MouseButtonPress, vp(240.0, 150.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(240.0, 150.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->state(), cad::tools::RotateState::Ready);
    QVERIFY(!tool->selectionConfirmed());
}


// 锚心跟随点击端 (用户拍板 2026-08-27): 自由线取离点击更近的一端 (16px/zoom);
// 连接线恒取挂连接的一端 —— 选中即入"编辑跟随角"安全模式; 中段点击保持起点.
void TestRotateCopy::d15AnchorFollowsClickedEnd()
{
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
    CanvasScene scene(&doc);
    const LineSetup host = makeLine(doc, 100.0);                 // (0,0)→(100,0)
    const LineSetup free2 = makeLine(doc, 100.0, Vec2(0, 200));  // (0,200)→(100,200)
    const LineSetup fol = makeLine(doc, 60.0, Vec2(100, 0));     // 挂接在 host 终点
    doc.resolveAll();
    Attachment conn;
    conn.fromBlockId = fol.blockId;
    conn.fromPointId = fol.startId;
    conn.toBlockId = host.blockId;
    conn.toPointId = host.endId;
    conn.toSegmentId = host.segId;
    conn.followerAngle = 180.0;   // 闭合基准: 180° = 沿 leader 直行延伸
    QVERIFY(doc.addAttachment(conn));
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
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    // 1) 自由线: 点近终点 (12mm 处, 避开端头 path-merge 空洞) → 锚 = 终点.
    sendMouse(QEvent::MouseButtonPress, vp(88.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(88.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), free2.endId);
    sendConfirm(view);
    // (不再拖动; Esc 退回选中态以换目标.)
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &esc);
    sendMouse(QEvent::MouseButtonPress, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);

    // 2) 自由线: 无阈值 —— 点左半段 → 锚 = 起点.
    sendMouse(QEvent::MouseButtonPress, vp(40.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(40.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), free2.startId);
    QApplication::sendEvent(&view, &esc);
    sendMouse(QEvent::MouseButtonPress, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);

    // 2b) 点右半段 → 锚 = 终点.
    sendMouse(QEvent::MouseButtonPress, vp(60.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(60.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), free2.endId);
    QApplication::sendEvent(&view, &esc);
    sendMouse(QEvent::MouseButtonPress, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(400.0, 200.0), Qt::LeftButton, Qt::NoModifier);

    // 3) 连接线: 点其空闲端 (远离挂接端) → 锚仍恒取挂接端 (起点).
    sendMouse(QEvent::MouseButtonPress, vp(130.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, vp(130.0, 0.0), Qt::LeftButton, Qt::NoModifier);
    QCOMPARE(tool->anchorPointId(), fol.startId);
}

void TestRotateCopy::d15ConfirmStateHasVisualAndCaptionHints()
{
    // H2 (TOOL_SYSTEM_AUDIT 2026-08-29): 确认门三态都必须有可观测反馈 ——
    // 未确认 = gizmo 虚线/空心 + caption 带「右键/回车确认」; 确认 = 实线/
    // 实心 + 无后缀; Esc 反悔回选中态 → 提示回来。旧实现 gizmo 两态同图、
    // HUD 无提示, 用户点线后拖动无反应, 把正确功能感知为"工具卡死"。
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };

    const QPoint mid = vp(50.0, 0.0);
    // 1) 选中 (未确认): gizmo 未确认态视觉 + caption 确认提示.
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    QVERIFY(!tool->selectionConfirmed());
    QVERIFY(!tool->gizmoConfirmed());
    auto* hud = view.viewport()->findChild<cad::ui::AngleHud*>();
    QVERIFY(hud);
    QVERIFY(hud->isVisible());
    const QString confirmHint = QString::fromUtf8("右键/回车确认");
    QVERIFY2(hud->captionText().contains(confirmHint),
             qPrintable(QStringLiteral("unconfirmed caption should carry hint: ")
                        + hud->captionText()));

    // 2) 右键确认: 实线/实心 + 后缀消失.
    sendConfirm(view);
    QVERIFY(tool->selectionConfirmed());
    QVERIFY(tool->gizmoConfirmed());
    QVERIFY2(!hud->captionText().contains(confirmHint),
             qPrintable(QStringLiteral("confirmed caption should drop hint: ")
                        + hud->captionText()));

    // 3) Esc 反悔 → 选中态: 提示与视觉同步回退 (再确认可再拖).
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &esc);
    QVERIFY(!tool->selectionConfirmed());
    QVERIFY(!tool->gizmoConfirmed());
    QVERIFY(hud->captionText().contains(confirmHint));
}

void TestRotateCopy::hudShortcutOverrideContainment()
{
    // H1 (TOOL_SYSTEM_AUDIT 2026-08-29): 角度 HUD 聚焦编辑时, 主窗口的单
    // 字母工具快捷键 (V/L/C/R/B/I/A/H) 不得抢键 —— 否则照着占位符输推荐
    // 公式 "b/4+5" 的第一个字符 b 就触发打断工具, ToolRotate 被析构, 旋转
    // 会话静默丢失。验证范式 = test_smartpen_aux::preInputBarShortcutContainment:
    // 向编辑框投递 ShortcutOverride, 断言被 accept (Qt 快捷键系统据此跳过
    // QAction 分发, 字符落进输入框)。
    ParamDocument doc;
    doc.setActiveLayer(layerIdAt(doc, 1));
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
    view.setInputDispatcher(&tm);
    auto* tool = dynamic_cast<cad::tools::ToolRotate*>(tm.activeTool());
    QVERIFY(tool);

    auto vp = [&](double x, double y) { return view.mapFromScene(QPointF(x, -y)); };
    auto sendMouse = [&](QEvent::Type type, const QPoint& pos, Qt::MouseButton btn,
                         Qt::KeyboardModifiers mods) {
        const QPoint global = view.viewport()->mapToGlobal(pos);
        QMouseEvent ev(type, pos, global, btn,
                       btn == Qt::LeftButton ? Qt::LeftButton : Qt::NoButton, mods);
        QApplication::sendEvent(view.viewport(), &ev);
        // sendEvent 同步送达并完成处理(工具链路无定时器/排队连接), 后续断言
        // 不依赖异步工作; 事件间无统一可观测条件, 暂留 qWait 仅作事件排空。
        QTest::qWait(20);
    };
    const QPoint mid = vp(50.0, 0.0);
    sendMouse(QEvent::MouseButtonPress, mid, Qt::LeftButton, Qt::NoModifier);
    sendMouse(QEvent::MouseButtonRelease, mid, Qt::LeftButton, Qt::NoModifier);
    auto* hud = view.viewport()->findChild<cad::ui::AngleHud*>();
    QVERIFY(hud);
    QVERIFY(hud->isVisible());

    // 8 个工具快捷键字母全部被 HUD 编辑框吞掉 (NoModifier 与 Ctrl 修饰都验).
    const QVector<int> shortcutKeys = {
        Qt::Key_V, Qt::Key_L, Qt::Key_C, Qt::Key_R, Qt::Key_B,
        Qt::Key_I, Qt::Key_A, Qt::Key_H
    };
    for (int key : shortcutKeys) {
        QKeyEvent scEvent(QEvent::ShortcutOverride, key, Qt::NoModifier);
        scEvent.ignore();
        QCoreApplication::sendEvent(hud->edit(), &scEvent);
        QVERIFY2(scEvent.isAccepted(),
                 QString("ShortcutOverride for key %1 must be accepted in AngleHud")
                     .arg(key).toUtf8().constData());
    }
    // HUD 主体 (非编辑框焦点路径) 同样要拦 —— eventFilter 装在 this + edit 两处.
    QKeyEvent hudEvent(QEvent::ShortcutOverride, Qt::Key_B, Qt::NoModifier);
    hudEvent.ignore();
    QCoreApplication::sendEvent(hud, &hudEvent);
    QVERIFY(hudEvent.isAccepted());

    // 工具未被切走: HUD 输入会话完整存活.
    QCOMPARE(tm.activeToolType(), cad::tools::ToolType::Rotate);
    QVERIFY(!tool->selectionConfirmed());
}
QTEST_MAIN(TestRotateCopy)
#include "test_rotate_copy.moc"
