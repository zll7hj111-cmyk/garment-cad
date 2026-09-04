#include <QtTest>
#include <QUuid>
#include <QUndoStack>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/ExpressionEvaluator.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/VariableCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/LayerCommands.h"
#include "geometry/Vec2.h"
#include "geometry/CurveMath.h"
#include "geometry/Units.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

/// Test convenience: stable id of the display layer at @p row.
QUuid layerIdAt(const cad::param::ParamDocument& doc, int row)
{
    const auto& ls = doc.layers();
    return (row >= 0 && row < static_cast<int>(ls.size()))
        ? ls[static_cast<size_t>(row)].id : QUuid();
}

/// Create a minimal horizontal line block and add it to the document.
struct LineSetup {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

LineSetup makeLine(ParamDocument& doc, double lenMm, const Vec2& origin = Vec2::zero())
{
    Block block;
    block.transform.origin = origin;
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = lenMm;
    p2.angle = 0.0;
    QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId, segId};
}

} // namespace

class TestAttachmentCommands : public QObject
{
    Q_OBJECT

private slots:
    void setAttachmentAngleOnly_keepsFollowAngle();
    void reattachPreservesAngleRefTwoPointBasis();
    void angleRefTwoPointBasis_engineAndUndo();
    void setAttachmentAngleOnly_docHelperAndLockedClosure();
    void shadowDetach_mountChainFollowsHost();
    void shadowDetach_formulaOffsetPreserved();
    void shadowLifecycle_stateMachineTransitions();
    void shadowDetach_degradeAndClearShadow();
    void slideMode_alongAndPerpConstraints();
    void slideMode_dragOffsetsUndoRedo();
    void slideMode_formulaOverridesNumericAndDragClears();
    void curvePointAttach_followsLeader();
    void endPinnedLengthEditMovesFreeStart();
    void dartLine_computesEndAndFollows();
    void dartLine_undoRedo();
    void dartLine_degradeOnHostDelete();
};

void TestAttachmentCommands::setAttachmentAngleOnly_keepsFollowAngle()
{
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);   // leader A: (0,0)→(100,0)
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);     // follower B: 50mm
    for (const auto& b : doc.blocks())
        if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);

    Attachment att;
    att.fromBlockId = bId; att.fromPointId = bStart;
    att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
    att.followerAngle = 90.0;   // 闭合基准: 90° = 垂直
    QVERIFY(doc.addAttachment(att));
    QVERIFY(doc.attachments().front().isLocked);   // 新建连接默认勾选拖动保护 (焊接)

    // Baseline: B's start sits exactly on A's end; B is perpendicular.
    const Vec2 joint = doc.findBlock(aId)->worldPos(aEnd);
    QVERIFY((doc.findBlock(bId)->worldPos(bStart) - joint).length() < 1e-6);
    const double rotBefore = doc.findBlock(bId)->transform.rotation;
    QVERIFY(std::abs(rotBefore - M_PI / 2.0) < 1e-9);

    // 拆开 (影子换代): convert to angle-only through the undo command.
    QUndoStack stack;
    stack.push(new cad::cmd::SetAttachmentAngleOnlyCommand(&doc, att.id, true));
    {
        const Attachment& a = doc.attachments().front();
        QVERIFY2(a.angleOnly, "拆开 = angleOnly");
        QVERIFY2(!a.isLocked, "位置自由 ↔ 拖动保护互斥");
        // 影子换代 (R2): 基准指向影子块, offset 原样保留。
        const Block* shadow = doc.blockById(a.toBlockId);
        QVERIFY2(shadow && shadow->isShadow, "基准换代为影子块 (isShadow)");
        QVERIFY2(shadow->shadowMasterBlockId == aId, "影子 master = 本体 A");
        QVERIFY2(a.followerAngle == 90.0, "offset 原样保留 (R2)");
        QVERIFY2(a.toSegmentId != aSeg, "toSegmentId 换代为影子段 (与本体无引用关系)");
        // 冻结克隆: 影子世界几何 = 本体拆开瞬间姿态 (逐位一致)。
        const Block* master = doc.findBlock(aId);
        QVERIFY(std::abs(shadow->transform.rotation - master->transform.rotation) < 1e-9);
        QVERIFY(std::abs(shadow->worldPos(a.toPointId).distanceTo(
                   master->worldPos(att.toPointId))) < 1e-6);
    }
    // Conversion itself changes nothing geometrically.
    QVERIFY(std::abs(doc.findBlock(bId)->transform.rotation - rotBefore) < 1e-9);

    // Translate B far away: angle must NOT change (位置自由, 平移不动角度).
    {
        auto* b = doc.blockById(bId);
        b->transform.origin = b->transform.origin + Vec2{120.0, 40.0};
    }
    doc.resolveAll();
    QVERIFY(std::abs(doc.findBlock(bId)->transform.rotation - rotBefore) < 1e-9);
    QVERIFY((doc.findBlock(bId)->worldPos(bStart) - joint).length() > 100.0);

    // R1 去耦合 (2026-xx 翻案, 设计稿 §3): 旋转本体 A +30° → B 方向不变
    // (影子 = 快照, 拆开态不再跟随本体旋转)。
    {
        auto* a = doc.blockById(aId);
        a->transform.rotation += 30.0 * M_PI / 180.0;
    }
    doc.resolveAll();
    QVERIFY(std::abs(doc.findBlock(bId)->transform.rotation - rotBefore) < 1e-9);

    // Undo: 完整连接恢复 + 影子删除 —— B 重新吸附到 A 的 (已旋转) 端点。
    stack.undo();
    {
        const Attachment& a = doc.attachments().front();
        QVERIFY(!a.angleOnly);
        QVERIFY(a.isLocked);   // undo 恢复原态 (新建默认焊接, 快照还原不得丢锁)
        QVERIFY2(a.toBlockId == aId, "基准还原为本体 (活引用恢复)");
        QVERIFY2(!doc.findShadowOfMaster(aId), "undo 删除影子块");
    }
    const Vec2 joint2 = doc.findBlock(aId)->worldPos(aEnd);
    QVERIFY((doc.findBlock(bId)->worldPos(bStart) - joint2).length() < 1e-6);

    // Redo: 影子换代 verbatim 重放 (影子 id 与首次一致)。
    stack.redo();
    QVERIFY(doc.attachments().front().angleOnly);
    {
        const Block* shadow2 = doc.blockById(doc.attachments().front().toBlockId);
        QVERIFY(shadow2 && shadow2->isShadow);
        QVERIFY2(shadow2->shadowMasterBlockId == aId, "redo 影子 id 复现");
    }
}


// ---------------------------------------------------------------------------
// 解焊重连保持两点基准 (preserveAngleRefOnReattach 点交换, 2026-12 用户报告
// 「重挂瞬间翻转」): 解焊 (断开拖动保护, 完整连接保持) 后重连走
// ReattachAttachmentCommand, 自动态下把旧所连线段固化为两点基准 —— 点1 =
// 旧线段另一端、点2 = 旧目标点, 两点连线方向与拆开前 exitDirectionAtPoint
// (终点 = start→end) 一致, 不翻转。此路径是 preserveAngleRefOnReattach 点
// 交换改动的唯一覆盖。
// ---------------------------------------------------------------------------

void TestAttachmentCommands::reattachPreservesAngleRefTwoPointBasis()
{
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);   // 旧宿主 A: (0,0)→(100,0)
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);     // 跟随 B
    auto [cId, cStart, cEnd, cSeg] =
        makeLine(doc, 80.0, Vec2(200.0, 0.0));                // 新宿主 C
    for (const auto& b : doc.blocks())
        if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);

    Attachment att;
    att.fromBlockId = bId; att.fromPointId = bStart;
    att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();
    QVERIFY(doc.attachments().front().angleRefBlockId.isNull());  // 自动态

    // 解焊 (拖动保护取消) —— 完整连接保持, 无冻结语义。
    doc.setAttachmentLocked(att.id, false);
    QVERIFY(!doc.attachments().front().isLocked);

    // 解焊重连到 C (ReattachAttachmentCommand): 自动态固化为两点基准。
    QUndoStack stack;
    stack.push(new cad::cmd::ReattachAttachmentCommand(
        &doc, att.id, cId, cEnd, cSeg));
    {
        const Attachment& a = doc.attachments().front();
        QVERIFY2(a.angleRefBlockId == aId,
                 "解焊重连: 点1 基准块 = 旧所连线段 (不被新宿主覆盖)");
        QVERIFY2(a.angleRefPointId == aStart,
                 "解焊重连: 点1 = 旧线段另一端 (方向与拆开前 exitDirectionAtPoint 一致)");
        QVERIFY2(a.angleRef2BlockId == aId && a.angleRef2PointId == aEnd,
                 "解焊重连: 点2 = 旧目标点 (两点基准完整)");
        QVERIFY2(a.angleRefSegmentId == aSeg,
                 "解焊重连: 基准线段 = 旧所连线段");
        QVERIFY2(a.toBlockId == cId, "解焊重连: 位置重挂到新宿主");
    }
}

// ---------------------------------------------------------------------------
// 角度基准两点化 (PANEL_REDESIGN §6.4, 2026-08-31 修复「点2 无效」):
//   · doc API 六参 setAttachmentAngleRef: 点1→点2 连线方向为角度基准, 设置
//     本身反算零跳变; 平移点2 宿主块后跟随线世界方向 = 新两点方向 (闭合
//     基准, followerAngle 不变) —— 引擎两点分支此前零测试覆盖;
//   · SetAttachmentAngleRefCommand 六参重载: undo/redo 全量还原 ref2 字段
//     与几何 (此前 ref2 无任何测试覆盖且 UI 写入路径失效)。
// ---------------------------------------------------------------------------

void TestAttachmentCommands::angleRefTwoPointBasis_engineAndUndo()
{
    auto angDiff = [](double a, double b) {
        double d = std::abs(a - b);
        d = std::fmod(d, 2.0 * M_PI);
        return d > M_PI ? 2.0 * M_PI - d : d;
    };
    auto worldDirOf = [](ParamDocument& d, const QUuid& blockId,
                         const QUuid& p1, const QUuid& p2) {
        const auto* b = d.findBlock(blockId);
        const Vec2 w1 = b->worldPos(p1);
        const Vec2 w2 = b->worldPos(p2);
        return std::atan2(w2.y - w1.y, w2.x - w1.x);
    };

    // ── Part 1: doc API 六参 + 引擎两点方向消费 ──
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);    // 宿主 A
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);     // 跟随 B
    auto [cId, cStart, cEnd, cSeg] =
        makeLine(doc, 40.0, Vec2(30.0, 80.0));                // 点2 宿主 C
    for (const auto& b : doc.blocks())
        if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);

    Attachment att;
    att.fromBlockId = bId; att.fromPointId = bStart;
    att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const double dirBefore = worldDirOf(doc, bId, bStart, bEnd);

    // 点1 = A 起点, 点2 = C 起点 → 基准 = A.start→C.start 连线方向。
    doc.setAttachmentAngleRef(att.id, aId, aSeg, aStart, cId, cStart);
    {
        const Attachment& a = doc.attachments().front();
        QCOMPARE(a.angleRefBlockId, aId);
        QCOMPARE(a.angleRef2BlockId, cId);
        QCOMPARE(a.angleRef2PointId, cStart);
        QVERIFY2(!a.angleIndependent, "设置角度基准退出独立角");
    }
    QVERIFY2(angDiff(worldDirOf(doc, bId, bStart, bEnd), dirBefore) < 1e-9,
             "设置两点基准 = 反算零跳变");

    // 平移 C → 两点方向变化 → B 转向新两点方向 (followerAngle 保持不变)。
    {
        auto* c = doc.blockById(cId);
        c->transform.origin = c->transform.origin + Vec2(50.0, -30.0);
    }
    doc.resolveAll();
    {
        const Vec2 w1 = doc.findBlock(aId)->worldPos(aStart);
        const Vec2 w2 = doc.findBlock(cId)->worldPos(cStart);
        const double refWorld = std::atan2(w2.y - w1.y, w2.x - w1.x);
        const double fA = doc.attachments().front().followerAngle * M_PI / 180.0;
        QVERIFY2(angDiff(worldDirOf(doc, bId, bStart, bEnd),
                         refWorld + M_PI - fA) < 1e-9,
                 "跟随线世界方向 = 点1→点2 连线方向 (闭合基准)");
    }

    // ── Part 2: 命令 undo/redo 全量还原 (含 ref2 字段) ──
    ParamDocument doc2;
    auto [a2, a2s, a2e, a2seg] = makeLine(doc2, 100.0);
    auto [b2, b2s, b2e, b2seg] = makeLine(doc2, 50.0);
    auto [c2, c2s, c2e, c2seg] = makeLine(doc2, 40.0, Vec2(30.0, 80.0));
    for (const auto& b : doc2.blocks())
        if (auto* mb = doc2.blockById(b.id)) mb->layer = layerIdAt(doc2, 1);

    Attachment att2;
    att2.fromBlockId = b2; att2.fromPointId = b2s;
    att2.toBlockId = a2;   att2.toPointId = a2e; att2.toSegmentId = a2seg;
    att2.followerAngle = 90.0;
    QVERIFY(doc2.addAttachment(att2));
    doc2.resolveAll();

    const double fA0 = doc2.attachments().front().followerAngle;
    const double dir0 = worldDirOf(doc2, b2, b2s, b2e);

    QUndoStack stack;
    stack.push(new cad::cmd::SetAttachmentAngleRefCommand(
        &doc2, att2.id, a2, a2seg, a2s, c2, c2s));
    {
        const Attachment& a = doc2.attachments().front();
        QCOMPARE(a.angleRefBlockId, a2);
        QCOMPARE(a.angleRef2BlockId, c2);
        QCOMPARE(a.angleRef2PointId, c2s);
    }
    QVERIFY2(angDiff(worldDirOf(doc2, b2, b2s, b2e), dir0) < 1e-9,
             "命令设置两点基准 = 零跳变");

    stack.undo();
    {
        const Attachment& a = doc2.attachments().front();
        QVERIFY2(a.angleRefBlockId.isNull() && a.angleRef2BlockId.isNull(),
                 "undo 还原 ref1/ref2 全空 (默认自动跟随)");
        QCOMPARE(a.followerAngle, fA0);
    }
    QVERIFY2(angDiff(worldDirOf(doc2, b2, b2s, b2e), dir0) < 1e-9,
             "undo 几何还原");

    stack.redo();
    {
        const Attachment& a = doc2.attachments().front();
        QCOMPARE(a.angleRefBlockId, a2);
        QCOMPARE(a.angleRef2BlockId, c2);
        QCOMPARE(a.angleRef2PointId, c2s);
    }
}

// ---------------------------------------------------------------------------
// 拆开保留角度 doc helper: setAttachmentAngleOnly() toggles the mode with the
// welded/position-free invariant, and lockedClosure() must never weld an
// angle-only pair back together.
// ---------------------------------------------------------------------------

void TestAttachmentCommands::setAttachmentAngleOnly_docHelperAndLockedClosure()
{
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);
    for (const auto& b : doc.blocks())
        if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);

    Attachment att;
    att.fromBlockId = bId; att.fromPointId = bStart;
    att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
    QVERIFY(doc.addAttachment(att));
    const QUuid attId = doc.attachments().front().id;

    // Full connection (默认焊接): 拖动保护默认勾选 → 闭包焊对; 面板取消
    // 勾选 (解焊仍完整连接) → 闭包不跨对.
    QCOMPARE(static_cast<int>(doc.lockedClosure({bId}).size()), 2);
    doc.setAttachmentLocked(attId, false);
    QCOMPARE(static_cast<int>(doc.lockedClosure({bId}).size()), 1);
    doc.setAttachmentLocked(attId, true);
    QCOMPARE(static_cast<int>(doc.lockedClosure({bId}).size()), 2);

    // 拆开: angleOnly + unlocked; closure no longer spans the pair.
    // 影子换代 (DETACH_SHADOW_DESIGN.md §7.1, 2026-xx 翻案活引用): 拆开同时
    // 创建影子基准块; 恢复完整连接 = 挂回本体 (⑤, 删影子 + 活引用)。
    doc.setAttachmentAngleOnly(attId, true);
    QVERIFY(doc.attachments().front().angleOnly);
    QVERIFY(!doc.attachments().front().isLocked);
    QVERIFY(doc.lockedClosure({bId}) == QSet<QUuid>{bId});
    {
        const auto* shadow = doc.blockById(doc.attachments().front().toBlockId);
        QVERIFY(shadow && shadow->isShadow);
        QVERIFY(shadow->shadowMasterBlockId == aId);
    }

    // 恢复完整连接: re-welded (只要建立跟随就保护), angleOnly cleared.
    doc.setAttachmentAngleOnly(attId, false);
    QVERIFY(!doc.attachments().front().angleOnly);
    QVERIFY(doc.attachments().front().isLocked);
    QCOMPARE(static_cast<int>(doc.lockedClosure({bId}).size()), 2);
    QVERIFY2(doc.attachments().front().toBlockId == aId, "挂回本体 (⑤)");
    QVERIFY2(!doc.findShadowOfMaster(aId), "挂回本体删影子 (⑤)");
}

// ---------------------------------------------------------------------------
// R3 链式随动 (DETACH_SHADOW_DESIGN.md §3/§4): 拆开 (影子换代) 后把影子挂到
// 新宿主 C —— L3 旋转 → 影子随动 (标准附着) → Att2 传导 → B 跟着转 (位置 +
// 角度链式), 接点 (C.end = 影子锚点 = B.start) 保持不动。挂载瞬间 Δ 反算
// 保向: B 的世界方向零跳变。
// ---------------------------------------------------------------------------

void TestAttachmentCommands::shadowDetach_mountChainFollowsHost()
{
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);              // 本体 A
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0, Vec2{0, -80}); // 跟随 B
    auto [cId, cStart, cEnd, cSeg] = makeLine(doc, 80.0, Vec2{300, -80}); // 新宿主 C
    for (const auto& b : doc.blocks())
        if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);

    Attachment att;
    att.fromBlockId = bId; att.fromPointId = bStart;
    att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));

    // ② 拆开 (影子换代): 影子 master = A, Att2 指向影子。
    const QUuid shadowId = doc.detachWithShadow(att.id);
    QVERIFY2(!shadowId.isNull(), "拆开 = 影子换代 (非降级场景)");
    QVERIFY(doc.blockById(shadowId)->isShadow);

    // ③ 影子挂载到 C: Δ 反算保向 —— 挂载瞬间 B 世界方向不变 (零跳变)。
    const double rotB0 = doc.findBlock(bId)->transform.rotation;
    QVERIFY(doc.mountShadowTo(shadowId, cId, cEnd, cSeg));
    QCOMPARE(doc.attachments().size(), size_t(2));
    QVERIFY2(std::abs(doc.findBlock(bId)->transform.rotation - rotB0) < 1e-9,
             "挂载瞬间影子/B 方向零跳变 (Δ 反算保向)");
    QVERIFY2(!doc.findAttachment(att.id)->angleOnly
             && doc.findAttachment(att.id)->isLocked,
             "Att2 恢复位置钉点并重新焊接");
    // 链式位置: B.start 钉在影子锚点 = C.end。
    QVERIFY2(doc.findBlock(bId)->worldPos(bStart).distanceTo(
                 doc.findBlock(cId)->worldPos(cEnd)) < 1e-6,
             "B.start = 影子锚点 = C.end (链式枢轴)");

    // R3: C 旋转 +30° → B 链式跟转 +30°, 接点不动。
    const double rotB1 = doc.findBlock(bId)->transform.rotation;
    doc.blockById(cId)->transform.rotation += 30.0 * M_PI / 180.0;
    doc.resolveAll();
    QVERIFY2(std::abs(doc.findBlock(bId)->transform.rotation
                      - (rotB1 + 30.0 * M_PI / 180.0)) < 1e-9,
             "L3 旋转 → 影子随动 → B 链式跟转 (R3)");
    QVERIFY2(doc.findBlock(bId)->worldPos(bStart).distanceTo(
                 doc.findBlock(cId)->worldPos(cEnd)) < 1e-6,
             "挂载态旋转 = 绕接点转 (接点保持)");
}

// ---------------------------------------------------------------------------
// R2 保关系 (含公式) + R1 去耦合: 拆开 (影子换代) 前后 offset 公式字符串
// 原样保留; 旋转本体 A, 跟随线 B 方向不变 (影子 = 快照, 不是活引用)。
// ---------------------------------------------------------------------------

void TestAttachmentCommands::shadowDetach_formulaOffsetPreserved()
{
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0, Vec2{0, -80});
    for (const auto& b : doc.blocks())
        if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);

    Attachment att;
    att.fromBlockId = bId; att.fromPointId = bStart;
    att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
    att.followerAngle = 90.0;
    att.followerAngleFormula = QStringLiteral("45+45");  // 公式驱动 offset (锁定)
    QVERIFY(doc.addAttachment(att));
    const double rotBefore = doc.findBlock(bId)->transform.rotation;

    const QUuid shadowId = doc.detachWithShadow(att.id);
    QVERIFY(!shadowId.isNull());
    // R2: offset 公式原样保留 (不烘焙、不清除)。
    const Attachment* att2 = doc.findAttachment(att.id);
    QVERIFY2(att2->followerAngleFormula == QStringLiteral("45+45"),
             "offset 公式原样保留 (R2)");
    QVERIFY2(std::abs(doc.findBlock(bId)->transform.rotation - rotBefore) < 1e-9,
             "拆开换代零跳变");

    // R1: 旋转本体 A +30° → B 方向不变; 影子也不动 (与本体无耦合)。
    doc.blockById(aId)->transform.rotation += 30.0 * M_PI / 180.0;
    doc.resolveAll();
    QVERIFY2(std::abs(doc.findBlock(bId)->transform.rotation - rotBefore) < 1e-9,
             "本体旋转不影响跟随线 (R1)");
    QVERIFY2(std::abs(doc.blockById(shadowId)->transform.rotation
                      - doc.findBlock(aId)->transform.rotation + 30.0 * M_PI / 180.0)
                 < 1e-9,
             "影子保持拆开瞬间姿态 (快照)");
    QVERIFY2(doc.findAttachment(att.id)->followerAngleFormula
                 == QStringLiteral("45+45"),
             "R1 旋转后公式仍原样");
}

// ---------------------------------------------------------------------------
// R5 生命周期状态机 (DETACH_SHADOW_DESIGN.md §6): ②拆开 → ③挂 C → ④再拆开
// (影子冻结当前方向, undo 可还原挂载) → ⑤挂回本体 (删影子 + 活引用) →
// ⑥本体删除 (影子级联删除 + B 独立) → ⑦宿主删除 (影子弹回拆开态)。
// ---------------------------------------------------------------------------

void TestAttachmentCommands::shadowLifecycle_stateMachineTransitions()
{
    // ── ②→③→④: 再拆开 = 结构复位, 影子冻结当前方向 (带过挂载期间转量)。──
    {
        ParamDocument doc;
        auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
        auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0, Vec2{0, -80});
        auto [cId, cStart, cEnd, cSeg] = makeLine(doc, 80.0, Vec2{300, -80});
        for (const auto& b : doc.blocks())
            if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);
        Attachment att;
        att.fromBlockId = bId; att.fromPointId = bStart;
        att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
        att.followerAngle = 90.0;
        QVERIFY(doc.addAttachment(att));

        const QUuid shadowId = doc.detachWithShadow(att.id);          // ②
        QVERIFY(!shadowId.isNull());
        QVERIFY(doc.mountShadowTo(shadowId, cId, cEnd, cSeg));        // ③
        QCOMPARE(doc.attachments().size(), size_t(2));

        // 宿主 C 旋转 +30° (挂载期间影子被带转) —— ④ 再拆开时影子冻结在
        // 当前值 (不跳回拆开瞬间值)。
        doc.blockById(cId)->transform.rotation += 30.0 * M_PI / 180.0;
        doc.resolveAll();
        const double shadowRotMounted =
            doc.blockById(shadowId)->transform.rotation;

        // ④ 再拆开 (经命令: ReDetach 模式, 一步 undo)。
        QUndoStack stack;
        stack.push(new cad::cmd::SetAttachmentAngleOnlyCommand(&doc, att.id, true));
        QCOMPARE(doc.attachments().size(), size_t(1));
        QVERIFY(doc.findAttachment(att.id)->angleOnly);
        QVERIFY2(std::abs(doc.blockById(shadowId)->transform.rotation
                          - shadowRotMounted) < 1e-9,
                 "④ 影子冻结当前方向 (不跳变)");
        QVERIFY2(std::abs(doc.findBlock(bId)->transform.rotation
                          - (doc.blockById(shadowId)->transform.rotation
                             + M_PI - 90.0 * M_PI / 180.0)) < 1e-9,
                 "④ B 方向 = 影子基准 + offset (随影子冻结值, 无跳变)");

        // ④ undo: 挂载态 verbatim 还原 (Att1 回填 + Att2 焊接)。
        stack.undo();
        QCOMPARE(doc.attachments().size(), size_t(2));
        QVERIFY(!doc.findAttachment(att.id)->angleOnly);
        QVERIFY2(std::abs(doc.blockById(shadowId)->transform.rotation
                          - shadowRotMounted) < 1e-9,
                 "④ undo 影子回到挂载姿态");
        stack.redo();
        QCOMPARE(doc.attachments().size(), size_t(1));

        // ⑤ 挂回本体: 删影子 + Att2 还原到本体 (活引用恢复 + 重新焊接)。
        QVERIFY(doc.reattachShadowToMaster(att.id));
        QVERIFY2(!doc.findShadowOfMaster(aId), "⑤ 影子删除");
        QVERIFY2(doc.findAttachment(att.id)->toBlockId == aId
                 && !doc.findAttachment(att.id)->angleOnly
                 && doc.findAttachment(att.id)->isLocked,
                 "⑤ Att2 → 本体, 活引用恢复 + 焊接");
        QCOMPARE(doc.attachments().size(), size_t(1));
    }

    // ── ⑥: 本体 (master) 被删 → 影子级联删除 + Att2 移除, B 转独立线。──
    {
        ParamDocument doc;
        auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
        auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0, Vec2{0, -80});
        for (const auto& b : doc.blocks())
            if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);
        Attachment att;
        att.fromBlockId = bId; att.fromPointId = bStart;
        att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
        att.followerAngle = 90.0;
        QVERIFY(doc.addAttachment(att));

        const QUuid shadowId = doc.detachWithShadow(att.id);
        QVERIFY(!shadowId.isNull());
        const double rotB = doc.findBlock(bId)->transform.rotation;

        doc.removeBlock(aId);   // ⑥
        QVERIFY2(!doc.blockById(shadowId), "⑥ 影子随本体级联删除");
        QVERIFY2(doc.attachments().empty(), "⑥ Att2 一并移除 (B 独立)");
        QVERIFY2(doc.findBlock(bId) != nullptr, "⑥ 跟随线保留");
        QVERIFY2(std::abs(doc.findBlock(bId)->transform.rotation - rotB) < 1e-9,
                 "⑥ B 独立线方向冻结 (原地保留)");
    }

    // ── ⑦: 挂载宿主 (L3) 被删 → 影子弹回拆开态 (冻结当前方向)。──
    {
        ParamDocument doc;
        auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
        auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0, Vec2{0, -80});
        auto [cId, cStart, cEnd, cSeg] = makeLine(doc, 80.0, Vec2{300, -80});
        for (const auto& b : doc.blocks())
            if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);
        Attachment att;
        att.fromBlockId = bId; att.fromPointId = bStart;
        att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
        att.followerAngle = 90.0;
        QVERIFY(doc.addAttachment(att));

        const QUuid shadowId = doc.detachWithShadow(att.id);
        QVERIFY(doc.mountShadowTo(shadowId, cId, cEnd, cSeg));
        doc.blockById(cId)->transform.rotation += 20.0 * M_PI / 180.0;
        doc.resolveAll();
        const double shadowRot = doc.blockById(shadowId)->transform.rotation;

        doc.removeBlock(cId);   // ⑦
        QVERIFY2(doc.blockById(shadowId) != nullptr, "⑦ 影子弹回拆开态 (保留)");
        QVERIFY2(std::abs(doc.blockById(shadowId)->transform.rotation
                          - shadowRot) < 1e-9,
                 "⑦ 影子冻结删除前最后姿态");
        QCOMPARE(doc.attachments().size(), size_t(1));
        QVERIFY2(doc.findAttachment(att.id)->angleOnly
                 && !doc.findAttachment(att.id)->isLocked,
                 "⑦ Att2 回 angleOnly (拆开态)");
        QVERIFY2(!doc.findBlock(cId), "⑦ 宿主已删");
    }
}

// ---------------------------------------------------------------------------
// 降级门 (计划 L2-2.1): 多段块本体 → 不建影子, 拆开保持旧 angleOnly 行为
// (活引用, 旋转基准线仍带动跟随线); 清除影子 (removeShadow) → Att2 移除,
// 跟随线变纯自由线 (方向/位置冻结)。
// ---------------------------------------------------------------------------

void TestAttachmentCommands::shadowDetach_degradeAndClearShadow()
{
    // ── 降级: 多段块本体 → Legacy 拆开 (无影子, 活引用)。──
    {
        ParamDocument doc;
        // 多段块本体: p1→p2 (100mm) →p3 (50mm @90°)。
        Block multi;
        multi.transform.origin = Vec2{0, 0};
        ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2::zero();
        const QUuid p1id = p1.id;
        ParamPoint p2; p2.constraint = PointConstraint::Polar;
        p2.refPointId = p1id; p2.distance = 100.0; p2.angle = 0.0;
        const QUuid p2id = p2.id;
        ParamPoint p3; p3.constraint = PointConstraint::Polar;
        p3.refPointId = p2id; p3.distance = 50.0; p3.angle = 90.0;
        const QUuid p3id = p3.id;
        multi.addPoint(p1); multi.addPoint(p2); multi.addPoint(p3);
        Segment s1; s1.startPointId = p1id; s1.endPointId = p2id;
        const QUuid s1id = s1.id;
        Segment s2; s2.startPointId = p2id; s2.endPointId = p3id;
        multi.addSegment(s1); multi.addSegment(s2);
        const QUuid multiId = multi.id;
        doc.addBlock(std::move(multi));

        auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0, Vec2{0, -80});
        for (const auto& b : doc.blocks())
            if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);
        Attachment att;
        att.fromBlockId = bId; att.fromPointId = bStart;
        att.toBlockId = multiId; att.toPointId = p2id; att.toSegmentId = s1id;
        att.followerAngle = 90.0;
        QVERIFY(doc.addAttachment(att));

        // 门面降级: detachWithShadow 返回空且不改模型。
        QVERIFY2(doc.detachWithShadow(att.id).isNull(),
                 "多段块本体 = 降级 (无影子)");
        QVERIFY2(!doc.findShadowOfMaster(multiId), "降级不产生影子");
        // 命令路径降级: Legacy 模式 = 旧 angleOnly 行为 (活引用)。
        QUndoStack stack;
        stack.push(new cad::cmd::SetAttachmentAngleOnlyCommand(&doc, att.id, true));
        QVERIFY(doc.findAttachment(att.id)->angleOnly);
        QVERIFY2(doc.findAttachment(att.id)->toBlockId == multiId,
                 "降级拆开: 基准保持本体 (活引用, 不换代)");
        // 旧语义保底: 旋转基准线, 跟随线仍跟转 (与旧版逐位一致)。
        const double rotB0 = doc.findBlock(bId)->transform.rotation;
        doc.blockById(multiId)->transform.rotation += 30.0 * M_PI / 180.0;
        doc.resolveAll();
        QVERIFY2(std::abs(doc.findBlock(bId)->transform.rotation
                          - (rotB0 + 30.0 * M_PI / 180.0)) < 1e-9,
                 "降级场景保持旧活引用语义 (旋转基准线 B 跟转)");
    }

    // ── 清除影子 (removeShadow): Att2 移除, B 变纯自由线。──
    {
        ParamDocument doc;
        auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
        auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0, Vec2{0, -80});
        for (const auto& b : doc.blocks())
            if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);
        Attachment att;
        att.fromBlockId = bId; att.fromPointId = bStart;
        att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
        att.followerAngle = 90.0;
        QVERIFY(doc.addAttachment(att));

        const QUuid shadowId = doc.detachWithShadow(att.id);
        QVERIFY(!shadowId.isNull());
        const double rotB = doc.findBlock(bId)->transform.rotation;
        const Vec2 originB = doc.findBlock(bId)->transform.origin;

        QVERIFY(doc.removeShadow(shadowId));
        QVERIFY2(doc.attachments().empty(), "清除影子: Att2 移除");
        QVERIFY2(!doc.blockById(shadowId), "清除影子: 影子块删除");
        QVERIFY2(std::abs(doc.findBlock(bId)->transform.rotation - rotB) < 1e-9
                 && doc.findBlock(bId)->transform.origin.distanceTo(originB) < 1e-9,
                 "清除影子: B 纯自由线 (姿态冻结)");
        // 旋转本体不再有任何影响 (B 已无连接)。
        doc.blockById(aId)->transform.rotation += 45.0 * M_PI / 180.0;
        doc.resolveAll();
        QVERIFY2(std::abs(doc.findBlock(bId)->transform.rotation - rotB) < 1e-9,
                 "清除影子后 B 完全独立");
    }
}

// ---------------------------------------------------------------------------
// 抽屉式单向滑动 — 滑轨模式 (slideMode, 用户拍板 2026-08): the follower keeps
// its driven rotation (relative angle α preserved) while its position loses
// exactly ONE degree of freedom in the leader-local frame. AlongLeader = slide
// along the leader's direction (perpendicular offset locked); PerpLeader =
// pull perpendicular (along-position locked). The leader's rigid motion
// carries the follower along the rail (rail coordinates preserved).
// ---------------------------------------------------------------------------

void TestAttachmentCommands::slideMode_alongAndPerpConstraints()
{
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);   // leader A
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);     // follower B
    for (const auto& b : doc.blocks())
        if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);

    Attachment att;
    att.fromBlockId = bId; att.fromPointId = bStart;
    att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
    att.followerAngle = 90.0;   // 垂直
    QVERIFY(doc.addAttachment(att));
    const QUuid attId = doc.attachments().front().id;

    const Vec2 joint = doc.findBlock(aId)->worldPos(aEnd);
    QVERIFY((doc.findBlock(bId)->worldPos(bStart) - joint).length() < 1e-6);
    const double rotBefore = doc.findBlock(bId)->transform.rotation;
    QVERIFY(std::abs(rotBefore - M_PI / 2.0) < 1e-9);

    // ── AlongLeader: 沿线滑动, 垂直锁定 (激活快照 = 0) ──
    QUndoStack stack;
    stack.push(new cad::cmd::SetAttachmentSlideModeCommand(
        &doc, attId, cad::param::SlideMode::AlongLeader));
    {
        const Attachment& a = doc.attachments().front();
        QVERIFY(a.slideMode == cad::param::SlideMode::AlongLeader);
        QVERIFY(!a.isLocked);   // 滑轨必须可滑动 (拖动保护互斥)
        QVERIFY(!a.angleOnly);  // 与拆开互斥
        QVERIFY(std::abs(a.slideAlongMm) < 1e-9);
        QVERIFY(std::abs(a.slidePerpMm) < 1e-9);
    }
    // 滑轨附件不参与焊接闭包.
    QCOMPARE(static_cast<int>(doc.lockedClosure({bId}).size()), 1);
    // 激活不改几何.
    QVERIFY(std::abs(doc.findBlock(bId)->transform.rotation - rotBefore) < 1e-9);
    QVERIFY((doc.findBlock(bId)->worldPos(bStart) - joint).length() < 1e-6);

    // 斜着拖 (30, 12): 沿线分量生效 (s=30), 垂直分量被锁回 0 (贴基准线).
    {
        auto* b = doc.blockById(bId);
        b->transform.origin = b->transform.origin + Vec2{30.0, 12.0};
    }
    doc.updateSlideOffsetsFromCurrent(attId);
    doc.resolveAll();
    {
        const auto* b = doc.findBlock(bId);
        QVERIFY(std::abs(b->transform.rotation - rotBefore) < 1e-9);
        const Vec2 p = b->worldPos(bStart);
        QVERIFY(std::abs(p.x - 130.0) < 1e-6);   // 沿线滑到 +30
        QVERIFY(std::abs(p.y - 0.0) < 1e-6);     // 垂直锁 0
    }

    // 纯垂直推 (0, 20): 被锁回, 沿线位置保持 30.
    {
        auto* b = doc.blockById(bId);
        b->transform.origin = b->transform.origin + Vec2{0.0, 20.0};
    }
    doc.updateSlideOffsetsFromCurrent(attId);
    doc.resolveAll();
    {
        const auto* b = doc.findBlock(bId);
        const Vec2 p = b->worldPos(bStart);
        QVERIFY(std::abs(p.x - 130.0) < 1e-6);
        QVERIFY(std::abs(p.y - 0.0) < 1e-6);
    }

    // 基准线旋转 +30°: 滑轨跟着转 — 相对角 α 保持 + 局部坐标 (s=30, t=0) 不变.
    {
        auto* a = doc.blockById(aId);
        a->transform.rotation += 30.0 * M_PI / 180.0;
    }
    doc.resolveAll();
    {
        const auto* b = doc.findBlock(bId);
        QVERIFY(std::abs(b->transform.rotation
                         - (rotBefore + 30.0 * M_PI / 180.0)) < 1e-9);
        const auto* a = doc.findBlock(aId);
        const Vec2 anchor = a->worldPos(aEnd);
        const Vec2 p = b->worldPos(bStart);
        const double rail = a->transform.rotation
                          + a->exitDirectionAtPoint(aEnd, aSeg);
        const Vec2 unit(std::cos(rail), std::sin(rail));
        const Vec2 rel = p - anchor;
        // 骑在滑轨上: 沿线 30, 垂直 0 (刚性携带).
        QVERIFY(std::abs(rel.x * unit.x + rel.y * unit.y - 30.0) < 1e-6);
        QVERIFY(std::abs(-rel.x * unit.y + rel.y * unit.x) < 1e-6);
    }

    // ── PerpLeader: 垂直拉出, 沿线锁定 (激活快照 = 当前投影 s=30, t=0) ──
    stack.push(new cad::cmd::SetAttachmentSlideModeCommand(
        &doc, attId, cad::param::SlideMode::PerpLeader));
    {
        const Attachment& a = doc.attachments().front();
        QVERIFY(a.slideMode == cad::param::SlideMode::PerpLeader);
        QVERIFY(!a.isLocked);
        QVERIFY(std::abs(a.slideAlongMm - 30.0) < 1e-6);
        QVERIFY(std::abs(a.slidePerpMm) < 1e-6);
    }
    // 拖动 (30, 60): 沿线锁回 30, 垂直分量生效 (拖向的垂直投影).
    {
        auto* b = doc.blockById(bId);
        b->transform.origin = b->transform.origin + Vec2{30.0, 60.0};
    }
    doc.updateSlideOffsetsFromCurrent(attId);
    doc.resolveAll();
    {
        const auto* b = doc.findBlock(bId);
        QVERIFY(std::abs(b->transform.rotation
                         - (rotBefore + 30.0 * M_PI / 180.0)) < 1e-9);
        const auto* a = doc.findBlock(aId);
        const Vec2 anchor = a->worldPos(aEnd);
        const double rail = a->transform.rotation
                          + a->exitDirectionAtPoint(aEnd, aSeg);
        const Vec2 unit(std::cos(rail), std::sin(rail));
        const Vec2 rel = b->worldPos(bStart) - anchor;
        QVERIFY(std::abs(rel.x * unit.x + rel.y * unit.y - 30.0) < 1e-6);  // 沿线锁 30
        const double dragPerp = 30.0 * (-unit.y) + 60.0 * unit.x;          // (30,60) 的垂直投影
        QVERIFY(std::abs(-rel.x * unit.y + rel.y * unit.x - dragPerp) < 1e-6);
    }

    // Undo ×2: PerpLeader → AlongLeader → 完整连接 (重新焊接) — B 重新吸附回锚点.
    stack.undo();
    QVERIFY(doc.attachments().front().slideMode == cad::param::SlideMode::AlongLeader);
    stack.undo();
    {
        const Attachment& a = doc.attachments().front();
        QVERIFY(a.slideMode == cad::param::SlideMode::None);
        QVERIFY(a.isLocked);   // undo 恢复原态 (新建默认焊接)
    }
    const Vec2 joint2 = doc.findBlock(aId)->worldPos(aEnd);
    QVERIFY((doc.findBlock(bId)->worldPos(bStart) - joint2).length() < 1e-6);

    // Redo: 回到 AlongLeader (位置从当前几何重快照 = 吸附点 → (0,0)).
    stack.redo();
    QVERIFY(doc.attachments().front().slideMode == cad::param::SlideMode::AlongLeader);
    QVERIFY(std::abs(doc.attachments().front().slideAlongMm) < 1e-6);
}

// ---------------------------------------------------------------------------
// 滑轨拖动撤销: dragging a slide follower writes the free-axis coordinate back
// with updateSlideOffsetsFromCurrent(); the tool wraps it into
// SetSlideOffsetsCommand + MoveBlockCommand in one macro so a single undo
// restores the pre-drag rail position (and redo re-applies it). The offsets
// command does NOT resolve by itself — the move command's resolve settles.
// ---------------------------------------------------------------------------

void TestAttachmentCommands::slideMode_dragOffsetsUndoRedo()
{
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);
    for (const auto& b : doc.blocks())
        if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);

    Attachment att;
    att.fromBlockId = bId; att.fromPointId = bStart;
    att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
    att.followerAngle = 0.0;
    QVERIFY(doc.addAttachment(att));
    const QUuid attId = doc.attachments().front().id;

    QUndoStack stack;
    stack.push(new cad::cmd::SetAttachmentSlideModeCommand(
        &doc, attId, cad::param::SlideMode::AlongLeader));

    // Simulate a tool drag: follower origin + (40, 0) — along the leader.
    const Vec2 delta{40.0, 0.0};
    const Vec2 preOrigin = doc.findBlock(bId)->transform.origin;
    {
        auto* b = doc.blockById(bId);
        b->transform.origin = b->transform.origin + delta;
    }
    doc.updateSlideOffsetsFromCurrent(attId);
    doc.resolveAll();
    const double slidAlong = doc.attachments().front().slideAlongMm;
    QVERIFY(std::abs(slidAlong - 40.0) < 1e-9);   // 沿线自由轴回写生效
    QVERIFY(std::abs(doc.attachments().front().slidePerpMm) < 1e-9);

    // Mirror the tool commit: restore pre-drag origin, then the macro
    // [SetSlideOffsets(old→new), MoveBlockCommand(delta)].
    doc.blockById(bId)->transform.origin = preOrigin;
    stack.beginMacro(QStringLiteral("滑动并移动"));
    stack.push(new cad::cmd::SetSlideOffsetsCommand(
        &doc, attId, 0.0, 0.0, slidAlong,
        doc.attachments().front().slidePerpMm));
    stack.push(new cad::cmd::MoveBlockCommand(&doc, {bId}, delta));
    stack.endMacro();

    const Vec2 slidPos = doc.findBlock(bId)->worldPos(bStart);
    QVERIFY(std::abs(slidPos.x - 140.0) < 1e-6);   // 滑轨上 x=140
    QVERIFY(std::abs(slidPos.y) < 1e-6);

    // Undo: rail position + origin both restored.
    stack.undo();
    {
        const Attachment& a = doc.attachments().front();
        QVERIFY(std::abs(a.slideAlongMm) < 1e-9);
        QVERIFY(std::abs(a.slidePerpMm) < 1e-9);
    }
    const Vec2 backPos = doc.findBlock(bId)->worldPos(bStart);
    QVERIFY(std::abs(backPos.x - 100.0) < 1e-6);
    QVERIFY(std::abs(backPos.y) < 1e-6);

    // Redo: rail position re-applied.
    stack.redo();
    const Vec2 rePos = doc.findBlock(bId)->worldPos(bStart);
    QVERIFY(std::abs(rePos.x - 140.0) < 1e-6);
    QVERIFY(std::abs(rePos.y) < 1e-6);
}

// 滑轨公式 (2026-12 用户提问"自动 .00 是否意味着不能用变量/表达式"):
// 公式 (cm 域) 优先于存储值生效; 拖动沿自由轴 = 手调 → 清公式回写数值。

void TestAttachmentCommands::slideMode_formulaOverridesNumericAndDragClears()
{
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);   // leader 0→100
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);     // follower
    for (const auto& b : doc.blocks())
        if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);

    Attachment att;
    att.fromBlockId = bId; att.fromPointId = bStart;
    att.toBlockId = aId;   att.toPointId = aEnd; att.toSegmentId = aSeg;
    att.followerAngle = 0.0;
    QVERIFY(doc.addAttachment(att));
    const QUuid attId = doc.attachments().front().id;

    QUndoStack stack;
    stack.push(new cad::cmd::SetAttachmentSlideModeCommand(
        &doc, attId, cad::param::SlideMode::AlongLeader));

    // 公式驱动 (cm 域): 沿线 20cm=200mm, 垂直 3*2=6cm=60mm; 锚点=基准线终点,
    // 沿线方向 = start→end (+x), 垂直 = (+y)。
    {
        auto* a = doc.findAttachment(attId);
        a->slideAlongFormula = QStringLiteral("20");
        a->slidePerpFormula = QStringLiteral("3*2");
    }
    doc.resolveAll();
    {
        const Vec2 p = doc.findBlock(bId)->worldPos(bStart);
        QVERIFY(std::abs(p.x - 300.0) < 1e-6);   // 100 + 200
        QVERIFY(std::abs(p.y - 60.0) < 1e-6);    // 60
        const Attachment& a = doc.attachments().front();
        QCOMPARE(a.slideAlongFormula, QStringLiteral("20"));   // 公式保留
        QCOMPARE(a.slidePerpFormula, QStringLiteral("3*2"));
    }

    // 拖动沿自由轴: updateSlideOffsetsFromCurrent 回写 + 清公式 (手调优先),
    // 锁轴公式继续生效 (垂直推 30mm 被锁回 60mm)。
    {
        auto* b = doc.blockById(bId);
        b->transform.origin += Vec2(0.0, 30.0);   // 垂直推
    }
    doc.updateSlideOffsetsFromCurrent(attId);
    doc.resolveAll();
    {
        const Attachment& a = doc.attachments().front();
        QVERIFY(a.slideAlongFormula.isEmpty());   // 自由轴公式被清
        QCOMPARE(a.slidePerpFormula, QStringLiteral("3*2"));   // 锁轴保留
        const Vec2 p = doc.findBlock(bId)->worldPos(bStart);
        QVERIFY(std::abs(p.x - 300.0) < 1e-6);   // 沿线 200mm
        QVERIFY(std::abs(p.y - 60.0) < 1e-6);    // 垂直公式 6cm
    }
}

// ---------------------------------------------------------------------------
// 曲线点连接跟随 (用户报告 2026-08: 连接曲线点的线"不可靠、不跟随"):
// ① follower attached to a CurveAnchor / Interpolated aux point on the leader
//    must track the point on rigid moves AND shape changes (full resolve and
//    the resolveForDrag path);
// ② the exact reported scenario — a curve anchor that FOLLOWS a target point
//    moves in the ParamDocument follow post-pass which runs AFTER the
//    attachment settle; a line attached to that anchor must still land on the
//    anchor's NEW position within the SAME drag frame (regression for the
//    follow re-settle fix).
// ---------------------------------------------------------------------------

void TestAttachmentCommands::curvePointAttach_followsLeader()
{
    const auto mkLeader = [](ParamDocument& doc, bool curve) {
        Block block;
        ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2::zero();
        const QUuid p1Id = p1.id;
        ParamPoint p2; p2.constraint = PointConstraint::Polar; p2.refPointId = p1Id;
        p2.distance = 100.0; p2.angle = 0.0;
        const QUuid p2Id = p2.id;
        block.addPoint(std::move(p1));
        block.addPoint(std::move(p2));
        Segment seg;
        seg.startPointId = p1Id; seg.endPointId = p2Id;
        if (curve) {
            seg.type = SegmentType::Bezier;
            ParamPoint pp; pp.constraint = PointConstraint::CurveAnchor;
            pp.hostSegmentId = seg.id; pp.interpPercent = 0.5; pp.interpOffsetDist = 20.0;
            pp.autoTangent = true;
            const QUuid ppId = pp.id;
            block.addPoint(std::move(pp));
            seg.passPointIds = {ppId};
        }
        const QUuid segId = seg.id;
        block.addSegment(std::move(seg));
        const QUuid bId = block.id;
        doc.addBlock(std::move(block));
        return std::tuple{bId, segId};
    };
    const auto toWorking = [](ParamDocument& doc) {
        for (const auto& b : doc.blocks())
            if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);
    };

    // ① straight leader + CurveAnchor midpoint: move + rotate (full resolve).
    {
        ParamDocument doc;
        auto [aId, aSeg] = mkLeader(doc, false);
        auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);
        toWorking(doc);

        ParamPoint anchor; anchor.constraint = PointConstraint::CurveAnchor;
        anchor.hostSegmentId = aSeg; anchor.interpPercent = 0.5; anchor.interpOffsetDist = 0.0;
        const QUuid anchorId = anchor.id;
        doc.blockById(aId)->addPoint(std::move(anchor));
        doc.resolveAll();

        Attachment att; att.fromBlockId = bId; att.fromPointId = bStart;
        att.toBlockId = aId; att.toPointId = anchorId; att.toSegmentId = aSeg;
        att.followerAngle = 90.0;
        QVERIFY(doc.addAttachment(att));
        QVERIFY((doc.findBlock(bId)->worldPos(bStart)
                 - doc.findBlock(aId)->worldPos(anchorId)).length() < 1e-6);

        doc.blockById(aId)->transform.origin += Vec2{30.0, 0.0};
        doc.resolveAll();
        QVERIFY((doc.findBlock(bId)->worldPos(bStart)
                 - doc.findBlock(aId)->worldPos(anchorId)).length() < 1e-6);

        doc.blockById(aId)->transform.rotation += 30.0 * M_PI / 180.0;
        doc.resolveAll();
        QVERIFY((doc.findBlock(bId)->worldPos(bStart)
                 - doc.findBlock(aId)->worldPos(anchorId)).length() < 1e-6);
    }

    // ② Bezier leader + INTERPOLATED aux point on the curve: rigid move,
    //    shape change (anchor offset edit), and the resolveForDrag path.
    {
        ParamDocument doc;
        auto [aId, aSeg] = mkLeader(doc, true);
        auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);
        toWorking(doc);

        ParamPoint aux; aux.constraint = PointConstraint::Interpolated;
        aux.hostSegmentId = aSeg; aux.interpPercent = 0.5; aux.interpOffsetDist = 0.0;
        aux.isAuxiliary = true;
        const QUuid auxId = aux.id;
        doc.blockById(aId)->addPoint(std::move(aux));
        doc.resolveAll();

        Attachment att; att.fromBlockId = bId; att.fromPointId = bStart;
        att.toBlockId = aId; att.toPointId = auxId; att.toSegmentId = aSeg;
        att.followerAngle = 90.0;
        QVERIFY(doc.addAttachment(att));
        QVERIFY((doc.findBlock(bId)->worldPos(bStart)
                 - doc.findBlock(aId)->worldPos(auxId)).length() < 1e-6);

        doc.blockById(aId)->transform.origin += Vec2{30.0, 0.0};
        doc.resolveAll();
        QVERIFY((doc.findBlock(bId)->worldPos(bStart)
                 - doc.findBlock(aId)->worldPos(auxId)).length() < 1e-6);

        // Shape change via the drag path: pass-point offset 20 → 60.
        for (auto& p : doc.blockById(aId)->points)
            if (p.constraint == PointConstraint::CurveAnchor) p.interpOffsetDist = 60.0;
        doc.resolveForDrag({aId});
        QVERIFY((doc.findBlock(bId)->worldPos(bStart)
                 - doc.findBlock(aId)->worldPos(auxId)).length() < 1e-6);
    }

    // ③ THE REPORTED BUG: a curve anchor with a follow target moves in the
    //    follow post-pass AFTER the attachment settle — the line attached to
    //    that anchor must track it within the SAME drag frame (pre-fix it
    //    stayed on the old anchor position: delta = 30mm).
    {
        ParamDocument doc;
        auto [aId, aSeg] = mkLeader(doc, true);
        auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 50.0);
        toWorking(doc);

        QUuid anchorId;
        for (auto& p : doc.blockById(aId)->points)
            if (p.constraint == PointConstraint::CurveAnchor) { anchorId = p.id; break; }
        QVERIFY(!anchorId.isNull());

        // Anchor follows the follower line B's start point.
        auto* anchorPt = doc.blockById(aId)->findPoint(anchorId);
        anchorPt->followBlockId = bId;
        anchorPt->followPointId = bStart;
        anchorPt->followOffset = Vec2::zero();
        doc.resolveAll();

        // Line L attached to the anchor (leader = the curve block).
        auto [cId, cStart, cEnd, cSeg] = makeLine(doc, 30.0);
        doc.blockById(cId)->layer = layerIdAt(doc, 1);
        Attachment att; att.fromBlockId = cId; att.fromPointId = cStart;
        att.toBlockId = aId; att.toPointId = anchorId; att.toSegmentId = aSeg;
        att.followerAngle = 90.0;
        QVERIFY(doc.addAttachment(att));
        QVERIFY((doc.findBlock(cId)->worldPos(cStart)
                 - doc.findBlock(aId)->worldPos(anchorId)).length() < 1e-6);

        // Drag the anchor's follow target: the anchor follows via the post-pass
        // AND the attached line must land on the anchor in the SAME frame.
        doc.blockById(bId)->transform.origin += Vec2{30.0, 0.0};
        doc.resolveForDrag({bId});
        QVERIFY((doc.findBlock(aId)->worldPos(anchorId)
                 - Vec2{30.0, 0.0}).length() < 1e-6);   // anchor followed the target
        QVERIFY((doc.findBlock(cId)->worldPos(cStart)
                 - doc.findBlock(aId)->worldPos(anchorId)).length() < 1e-6);
    }
}

// ---------------------------------------------------------------------------
// 出端被钉 + 改长度 (2026-09 用户拍板转正): 长度编辑恒写终点 (出端) 的
// Polar 距离, 但 Resolver 的位置约束把钉点 (fromPointId) 钉回宿主点 ——
// 净效果 = 钉住的端世界位置不动, 长度变化全部表现为自由端 (进端) 伸缩。
// 这是设计行为 (与旋转工具 "start swings, end stays pinned" 同源), 本用例
// 锁定它, 防止将来被当作反直觉 bug "修掉"。
// ---------------------------------------------------------------------------

void TestAttachmentCommands::endPinnedLengthEditMovesFreeStart()
{
    ParamDocument doc;
    // L1: 宿主, (0,0)→(100,0)。L2: 本线, (100,0)→(160,0), 出端 P4 钉在 L1 终点 P2。
    const auto leader = makeLine(doc, 100.0);
    const auto line   = makeLine(doc, 60.0, Vec2(100.0, 0.0));
    doc.resolveAll();

    Attachment att;
    att.fromBlockId = line.blockId;
    att.fromPointId = line.endId;          // 出端 (P4) 被钉住 —— 倒挂配置
    att.toBlockId   = leader.blockId;
    att.toPointId   = leader.endId;
    att.toSegmentId = leader.segId;
    att.followerAngle = 180.0;             // 沿 L1 直行延续
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    auto* blk = doc.findBlock(line.blockId);
    const Vec2 p3Before = blk->worldPos(line.startId);
    const Vec2 p4Before = blk->worldPos(line.endId);
    QVERIFY((p4Before - doc.findBlock(leader.blockId)->worldPos(leader.endId))
                .length() < 1e-6);         // 钉点落在宿主点上

    // 改长度 (与 LinePropertyDialog::applyToModel / ContextStrip::applyLength
    // 同路径: 写终点 Polar 距离)。
    auto* ep = blk->findPoint(line.endId);
    ep->distance = 100.0;
    blk->touchGeometry();
    doc.resolveAll();

    const Vec2 p3After = blk->worldPos(line.startId);
    const Vec2 p4After = blk->worldPos(line.endId);
    // 钉住的出端不动, 自由进端沿本线方向伸缩 60→100mm。
    QVERIFY2((p4After - p4Before).length() < 1e-6,
             "钉住的出端世界位置必须不动");
    QVERIFY2(std::abs((p3After - p3Before).length() - 40.0) < 1e-6,
             "长度变化全部表现为自由进端伸缩 (60→100mm)");
    // 伸缩方向 = 本线方向 (start→end, 即 P3→P4 的反向)。
    const Vec2 dir = (p4Before - p3Before).normalized();
    const Vec2 moved = p3After - p3Before;
    QVERIFY2(std::abs(moved.x * dir.y - moved.y * dir.x) < 1e-6 &&
             moved.x * dir.x + moved.y * dir.y < 0.0,
             "进端沿本线方向反向伸缩 (远离钉点)");
}

// ---------------------------------------------------------------------------
// 省道线 (用户拍板 2026-08):
//   起点 A 挂已有点；终点 E = B 沿 B 所在线段方向转 β 角、偏移 d；
//   线方向/线长由 Resolver 自动算出，B 旋转/移动/A 移动时持续跟随。
// ---------------------------------------------------------------------------

/// Build a dart block (same construction as LineFactory::createDartLine) on a
/// horizontal datum block; returns the leak of created ids + the dart block id.
static QUuid addDartLine(ParamDocument& doc,
                         const QUuid& aBlockId, const QUuid& aPointId,
                         const QUuid& bBlockId, const QUuid& bPointId,
                         const QUuid& bSegId,
                         double offsetMm, double angleDeg)
{
    Block block;
    block.transform.origin = Vec2::zero();
    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;
    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = 100.0;  // placeholder — the Resolver writes the real |A−E|
    p2.angle = 0.0;
    QUuid endId = p2.id;
    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));
    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    block.addSegment(std::move(seg));
    block.dartStartBlockId = aBlockId;
    block.dartStartPointId = aPointId;
    block.dartRefBlockId   = bBlockId;
    block.dartRefPointId   = bPointId;
    block.dartRefSegmentId = bSegId;
    block.dartOffsetMm     = offsetMm;
    block.dartAngleDeg     = angleDeg;
    QUuid dartId = block.id;
    doc.addBlock(std::move(block));
    return dartId;
}


void TestAttachmentCommands::dartLine_computesEndAndFollows()
{
    ParamDocument doc;
    // Datum line B: horizontal (0,0)→(100,0), segment 0°.
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 100.0);
    // Line A: horizontal at y=50, start point is the dart's pin A.
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 50.0, Vec2{0.0, 50.0});

    const QUuid dartId = addDartLine(doc, aId, aStart, bId, bEnd, bSeg,
                                     20.0 /*d*/, 90.0 /*β*/);
    QVERIFY(doc.findBlock(dartId)->isDart());
    doc.resolveAll();

    // A = (0,50); θ_B = 0°; E = (100,0) + 20·dir(90°) = (100,20).
    const Block* dart = doc.findBlock(dartId);
    QVERIFY(dart);
    QVERIFY(dart->transform.origin.distanceTo(Vec2{0.0, 50.0}) < 1e-6);
    const double expectRotation = std::atan2(20.0 - 50.0, 100.0 - 0.0);
    QVERIFY(std::abs(dart->transform.rotation - expectRotation) < 1e-9);
    // End point world = E.
    const Segment& seg = dart->segments.front();
    const Vec2 endWorld = dart->transform.toWorld(
        dart->findPoint(seg.endPointId)->resolvedPos);
    QVERIFY(endWorld.distanceTo(Vec2{100.0, 20.0}) < 1e-6);
    // Line length = |A−E|.
    QVERIFY(std::abs(dart->findPoint(seg.endPointId)->distance
                     - Vec2{100.0, 20.0}.distanceTo(Vec2{0.0, 50.0})) < 1e-6);

    // Translate the reference block: E follows rigidly (B + same offset).
    doc.blockById(bId)->transform.origin += Vec2{10.0, 0.0};
    doc.resolveAll();
    {
        const Block* d2 = doc.findBlock(dartId);
        QVERIFY(std::abs(d2->transform.rotation
                         - std::atan2(20.0 - 50.0, 110.0 - 0.0)) < 1e-9);
        QVERIFY(d2->transform.toWorld(d2->findPoint(seg.endPointId)->resolvedPos)
                    .distanceTo(Vec2{110.0, 20.0}) < 1e-6);
    }

    // Rotate the reference block by +30°: the dart must rotate WITH the
    // segment (angle basis = B's segment, not the world).
    doc.blockById(bId)->transform.rotation = 30.0 * M_PI / 180.0;
    doc.resolveAll();
    {
        const Block* d3 = doc.findBlock(dartId);
        const Vec2 bEndWorld = doc.findBlock(bId)->worldPos(bEnd);
        // θ_B = 30°; E = B_world + 20·dir(30°+90°=120°).
        const Vec2 expectE = bEndWorld
            + Vec2(std::cos(120.0 * M_PI / 180.0),
                   std::sin(120.0 * M_PI / 180.0)) * 20.0;
        const Vec2 actualE = d3->transform.toWorld(
            d3->findPoint(seg.endPointId)->resolvedPos);
        QVERIFY(actualE.distanceTo(expectE) < 1e-6);
        QVERIFY(std::abs(d3->transform.rotation
                         - std::atan2(expectE.y - 50.0, expectE.x - 0.0)) < 1e-9);
    }

    // Moving A re-pins the origin and recomputes the direction.
    doc.blockById(aId)->transform.origin += Vec2{0.0, -30.0};
    doc.resolveAll();
    {
        const Block* d4 = doc.findBlock(dartId);
        QVERIFY(d4->transform.origin.distanceTo(Vec2{0.0, 20.0}) < 1e-6);
    }

    // Editing the offset d moves E along the β ray.
    doc.blockById(dartId)->dartOffsetMm = 40.0;
    doc.resolveAll();
    {
        const Block* d5 = doc.findBlock(dartId);
        const Vec2 bEndWorld = doc.findBlock(bId)->worldPos(bEnd);
        const Vec2 expectE = bEndWorld
            + Vec2(std::cos(120.0 * M_PI / 180.0),
                   std::sin(120.0 * M_PI / 180.0)) * 40.0;
        QVERIFY(d5->transform.toWorld(d5->findPoint(seg.endPointId)->resolvedPos)
                    .distanceTo(expectE) < 1e-6);
    }
}


void TestAttachmentCommands::dartLine_undoRedo()
{
    ParamDocument doc;
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 100.0);
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 50.0, Vec2{0.0, 50.0});

    // Create the dart through the same command the factory uses
    // (DrawLineCommand with a dummy attachment).
    Block block;
    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;
    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = 100.0;
    p2.angle = 0.0;
    QUuid endId = p2.id;
    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));
    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    block.addSegment(std::move(seg));
    block.dartStartBlockId = aId;
    block.dartStartPointId = aStart;
    block.dartRefBlockId   = bId;
    block.dartRefPointId   = bEnd;
    block.dartRefSegmentId = bSeg;
    block.dartOffsetMm     = 20.0;
    block.dartAngleDeg     = 90.0;
    const QUuid dartId = block.id;

    cad::param::Attachment dummy;
    cad::cmd::DrawLineCommand cmd(&doc, std::move(block), dummy, false);
    cmd.redo();
    QVERIFY(doc.findBlock(dartId));
    QVERIFY(doc.findBlock(dartId)->isDart());

    cmd.undo();
    QVERIFY(!doc.findBlock(dartId));

    cmd.redo();
    QVERIFY(doc.findBlock(dartId));
    QVERIFY(doc.findBlock(dartId)->isDart());
    QCOMPARE(doc.findBlock(dartId)->dartOffsetMm, 20.0);
    QCOMPARE(doc.findBlock(dartId)->dartAngleDeg, 90.0);
}


void TestAttachmentCommands::dartLine_degradeOnHostDelete()
{
    ParamDocument doc;
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 100.0);
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 50.0, Vec2{0.0, 50.0});
    const QUuid dartId = addDartLine(doc, aId, aStart, bId, bEnd, bSeg, 20.0, 90.0);
    doc.resolveAll();

    // Deleting the REFERENCE block degrades the dart to a plain line.
    const auto impact = doc.deleteImpactReport(bId);
    QCOMPARE(impact.dartLinesDegraded, 1);
    doc.removeBlock(bId);
    QVERIFY(!doc.findBlock(dartId)->isDart());
    QVERIFY(doc.findBlock(dartId)->dartStartBlockId.isNull());
    QVERIFY(doc.findBlock(dartId)->dartRefBlockId.isNull());

    // Deleting the START host after the line already degraded: no additional
    // dart impact (the constraint fields were cleared by the first delete).
    const auto impact2 = doc.deleteImpactReport(aId);
    QCOMPARE(impact2.dartLinesDegraded, 0);
    doc.removeBlock(aId);
    QVERIFY(!doc.findBlock(dartId)->isDart());
    QVERIFY(doc.findBlock(dartId)->dartStartBlockId.isNull());
}

// ---------------------------------------------------------------------------
// ReverseSegmentCommand (线段换向): 几何保形 + 驱动端互换。
// 换向后两端世界位置零跳变; 修改长度驱动另一端 (旧起点成为 Polar 驱动端);
// 角度 +180 换算 (视角补偿)。
// ---------------------------------------------------------------------------

QTEST_MAIN(TestAttachmentCommands)
#include "test_attachment_commands.moc"
