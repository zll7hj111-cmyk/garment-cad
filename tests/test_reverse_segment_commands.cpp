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

class TestReverseSegmentCommands : public QObject
{
    Q_OBJECT

private slots:
    void reverseSegment_keepsGeometryAndSwapsDrivenEnd();
    void reverseSegment_extendTailsStayPhysical_undoRestores();
    void reverseSegment_auxPointStaysPut_interpFromEndFlips();
    void reverseSegment_rejectsNonStandardStructures();
    void reverseSegment_refSegmentConsumerCompensated();
    void reverseSegment_relativeIntersectionCompensated();
    void reverseSegment_followerAngleCompensated_noJump();
    void reverseSegment_connectedAsLeader_exitStable();
    void reverseSegment_legacyAngleRefBackfilled();
    void reverseSegment_curveShapePreserved();
    void reverseSegment_namesSurviveUndoRoundTrip();
    void reverseSegment_editDrivenEndAfterReverse();
    void reverseSegment_rejectsV2RemainingCases();
};

void TestReverseSegmentCommands::reverseSegment_keepsGeometryAndSwapsDrivenEnd()
{
    ParamDocument doc;
    // 斜线: P1=(10,20) 锚点, P2 Polar 60mm @ 30°。
    Block block;
    block.transform.origin = Vec2(10.0, 20.0);
    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    const QUuid startId = p1.id;
    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = 60.0;
    p2.angle = 30.0;
    const QUuid endId = p2.id;
    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));
    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    const QUuid segId = seg.id;
    block.addSegment(std::move(seg));
    const QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    doc.resolveAll();

    const auto* blk = doc.findBlock(blockId);
    const Vec2 w1Before = blk->worldPos(startId);
    const Vec2 w2Before = blk->worldPos(endId);

    // 资格: 标准结构 → 通过。
    QString why;
    QVERIFY(cad::cmd::ReverseSegmentCommand::canReverse(&doc, blockId, segId, &why));

    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, blockId, segId));
    doc.resolveAll();

    // 几何保形: 两端世界位置不变。
    const auto* blk2 = doc.findBlock(blockId);
    QVERIFY(blk2);
    const auto* seg2 = blk2->findSegment(segId);
    QVERIFY(seg2);
    QCOMPARE(seg2->startPointId, endId);   // 身份互换
    QCOMPARE(seg2->endPointId, startId);
    QVERIFY(blk2->worldPos(startId).distanceTo(w1Before) < 1e-6);
    QVERIFY(blk2->worldPos(endId).distanceTo(w2Before) < 1e-6);

    // 驱动端互换: 旧起点 (新终点) 成为 Polar 驱动端, ref = 旧终点;
    // 角度 = 30° + 180° = 210°, 距离不变。
    const auto* driven = blk2->findPoint(startId);
    QVERIFY(driven);
    QCOMPARE(static_cast<int>(driven->constraint),
             static_cast<int>(PointConstraint::Polar));
    QCOMPARE(driven->refPointId, endId);
    QVERIFY(std::abs(driven->distance - 60.0) < 1e-9);
    QVERIFY(std::abs(driven->angle - 210.0) < 1e-9);
    // 新起点 (旧终点) 落为 Free 锚点。
    const auto* anchor = blk2->findPoint(endId);
    QVERIFY(anchor);
    QCOMPARE(static_cast<int>(anchor->constraint),
             static_cast<int>(PointConstraint::Free));

    // 换向后改长度 → 驱动端 (旧起点/新终点) 动, 锚点 (旧终点/新起点) 不动。
    QUndoStack stack2;
    // 直接写模型模拟 "改长度" (角度卡 applyAngle 写的就是这个 Polar):
    auto* drivenMut = const_cast<Block*>(blk2)->findPoint(startId);
    drivenMut->distance = 100.0;
    const_cast<Block*>(blk2)->touchGeometry();
    doc.resolveAll();
    QVERIFY(blk2->worldPos(endId).distanceTo(w2Before) < 1e-6);   // 锚点不动
    QVERIFY(blk2->worldPos(startId).distanceTo(w1Before) > 1.0);  // 驱动端动了

    // Undo: 恢复原结构与数值。
    stack.undo();
    doc.resolveAll();
    const auto* blk3 = doc.findBlock(blockId);
    const auto* seg3 = blk3->findSegment(segId);
    QCOMPARE(seg3->startPointId, startId);
    QCOMPARE(seg3->endPointId, endId);
    QVERIFY(blk3->worldPos(startId).distanceTo(w1Before) < 1e-6);
    QVERIFY(blk3->worldPos(endId).distanceTo(w2Before) < 1e-6);
    QCOMPARE(static_cast<int>(blk3->findPoint(endId)->constraint),
             static_cast<int>(PointConstraint::Polar));
    // Redo: 再次换向成功。
    stack.redo();
    doc.resolveAll();
    QCOMPARE(doc.findBlock(blockId)->findSegment(segId)->startPointId, endId);
}

// ---------------------------------------------------------------------------
// ReverseSegmentCommand: 延长尾巴物理不动 + 宿主辅助点不动 + undo 恢复。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_extendTailsStayPhysical_undoRestores()
{
    ParamDocument doc;
    auto ls = makeLine(doc, 100.0);   // P1=(0,0) → P2=(100,0)
    doc.resolveAll();

    auto* block = doc.findBlock(ls.blockId);
    auto* seg = block->findSegment(ls.segId);
    // 起点延长 20mm (往 -x), 终点延长 15mm (往 +x)。
    seg->extendStartMm = 20.0;
    seg->extendEndMm = 15.0;
    block->touchGeometry();
    doc.resolveAll();

    const Vec2 tailStartBefore = block->worldPos(ls.startId);   // 本体位置
    const auto* epEffBefore = block;                            // (世界端点语义见下)
    Q_UNUSED(epEffBefore);

    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, ls.blockId, ls.segId));
    doc.resolveAll();

    const auto* blk2 = doc.findBlock(ls.blockId);
    const auto* seg2 = blk2->findSegment(ls.segId);
    // 延长量随端点角色互换: 新起点 (旧终点) = 15, 新终点 (旧起点) = 20
    // → 物理尾巴 (世界位置) 不动。
    QVERIFY(std::abs(seg2->extendStartMm - 15.0) < 1e-9);
    QVERIFY(std::abs(seg2->extendEndMm - 20.0) < 1e-9);
    // 有效端点世界位置不变: 新起点有效位 = 旧 (100,0)+(15,0 方向 +x) = (115,0);
    // 有效位求解走 applyEffectivePositions, 直接验证 effectiveLocalPos。
    const Vec2 effNewStart = blk2->effectiveLocalPos(seg2->startPointId);
    const Vec2 effNewEnd   = blk2->effectiveLocalPos(seg2->endPointId);
    QVERIFY(std::abs(effNewStart.x - 115.0) < 1e-6);
    QVERIFY(std::abs(effNewStart.y) < 1e-6);
    QVERIFY(std::abs(effNewEnd.x - (-20.0)) < 1e-6);
    QVERIFY(std::abs(effNewEnd.y) < 1e-6);
    Q_UNUSED(tailStartBefore);

    // Undo: 延长量归位。
    stack.undo();
    const auto* seg3 = doc.findBlock(ls.blockId)->findSegment(ls.segId);
    QVERIFY(std::abs(seg3->extendStartMm - 20.0) < 1e-9);
    QVERIFY(std::abs(seg3->extendEndMm - 15.0) < 1e-9);
    QCOMPARE(seg3->startPointId, ls.startId);
}

// ---------------------------------------------------------------------------
// ReverseSegmentCommand: 宿主辅助点位置零漂移 (interpFromEnd 翻转补偿)。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_auxPointStaysPut_interpFromEndFlips()
{
    ParamDocument doc;
    auto ls = makeLine(doc, 100.0);
    doc.resolveAll();

    // 宿主 30% 处辅助点 (percent=0.3, fromEnd=false)。
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = ls.segId;
    aux.interpPercent = 0.3;
    aux.isAuxiliary = true;
    const QUuid auxId = aux.id;
    auto* block = const_cast<Block*>(doc.findBlock(ls.blockId));
    block->addPoint(std::move(aux));
    doc.resolveAll();

    const Vec2 auxBefore = doc.findBlock(ls.blockId)->worldPos(auxId);
    QVERIFY(std::abs(auxBefore.x - 30.0) < 1e-6);

    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, ls.blockId, ls.segId));
    doc.resolveAll();

    // 辅助点世界位置不变; fromEnd 翻转 (percent 语义跟随新方向)。
    const auto* blk2 = doc.findBlock(ls.blockId);
    QVERIFY(blk2->worldPos(auxId).distanceTo(auxBefore) < 1e-6);
    const auto* aux2 = blk2->findPoint(auxId);
    QCOMPARE(aux2->interpFromEnd, true);
    QVERIFY(std::abs(aux2->interpPercent - 0.3) < 1e-9);

    stack.undo();
    const auto* aux3 = doc.findBlock(ls.blockId)->findPoint(auxId);
    QCOMPARE(aux3->interpFromEnd, false);
}

// ---------------------------------------------------------------------------
// ReverseSegmentCommand::canReverse: v2 仍拒绝的场景。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_rejectsNonStandardStructures()
{
    // 1) 端点被其他线段共享拒绝 (块内两段共点)。
    {
        ParamDocument doc;
        auto ls = makeLine(doc, 100.0);
        auto* block = const_cast<Block*>(doc.findBlock(ls.blockId));
        Segment seg2;
        seg2.startPointId = ls.endId;                    // 共享 P2
        ParamPoint p3;
        p3.constraint = PointConstraint::Polar;
        p3.refPointId = ls.endId;
        p3.distance = 40.0;
        p3.angle = 90.0;
        const QUuid p3Id = p3.id;
        block->addPoint(std::move(p3));
        seg2.endPointId = p3Id;
        block->addSegment(std::move(seg2));
        doc.resolveAll();
        QString why;
        QVERIFY(!cad::cmd::ReverseSegmentCommand::canReverse(&doc, ls.blockId,
                                                             ls.segId, &why));
    }
    // 2) 角度测量引用拒绝 (start→end 是测量基准, 补偿会改变测量语义)。
    {
        ParamDocument doc;
        auto a = makeLine(doc, 100.0);
        auto b = makeLine(doc, 60.0, Vec2(120.0, 0.0));
        doc.resolveAll();
        AngleMeasureVariable am;
        am.blockA = a.blockId; am.segmentA = a.segId;
        am.blockB = b.blockId; am.segmentB = b.segId;
        doc.addAngleMeasure(am);
        QString why;
        QVERIFY(!cad::cmd::ReverseSegmentCommand::canReverse(&doc, b.blockId,
                                                             b.segId, &why));
    }
    // 3) 终点指向拒绝。
    {
        ParamDocument doc;
        auto a = makeLine(doc, 100.0);
        auto b = makeLine(doc, 60.0, Vec2(0.0, 120.0));
        auto* blkB = const_cast<Block*>(doc.findBlock(b.blockId));
        blkB->endTargetBlockId = a.blockId;
        blkB->endTargetPointId = a.endId;
        doc.resolveAll();
        QString why;
        QVERIFY(!cad::cmd::ReverseSegmentCommand::canReverse(&doc, b.blockId,
                                                             b.segId, &why));
    }
    // 4) 滑轨模式连接拒绝 (基准线局部系快照会镜像)。
    {
        ParamDocument doc;
        auto [lId, lStart, lEnd, lSeg] = makeLine(doc, 100.0);
        auto [fId, fStart, fEnd, fSeg] = makeLine(doc, 60.0, Vec2(0.0, 0.0));
        doc.resolveAll();
        Attachment att;
        att.fromBlockId = fId;
        att.fromPointId = fStart;
        att.toBlockId = lId;
        att.toPointId = lStart;
        att.followerAngle = 180.0;
        att.slideMode = cad::param::SlideMode::AlongLeader;
        att.slideAlongMm = 20.0;
        QVERIFY(doc.addAttachment(att));
        QString why;
        QVERIFY(!cad::cmd::ReverseSegmentCommand::canReverse(&doc, fId, fSeg, &why));
        Q_UNUSED(lId); Q_UNUSED(lStart); Q_UNUSED(lEnd); Q_UNUSED(lSeg);
    }
    // 5) 需补偿的弧长模式连接拒绝 (πr 不可参数化表达)。
    {
        ParamDocument doc;
        auto [lId, lStart, lEnd, lSeg] = makeLine(doc, 100.0);
        auto [fId, fStart, fEnd, fSeg] = makeLine(doc, 60.0, Vec2(0.0, 0.0));
        doc.resolveAll();
        Attachment att;
        att.fromBlockId = fId;
        att.fromPointId = fStart;
        att.toBlockId = lId;
        att.toPointId = lStart;
        att.rotationMode = cad::param::RotationMode::ArcLength;
        att.arcLength = 30.0;
        QVERIFY(doc.addAttachment(att));
        QString why;
        QVERIFY(!cad::cmd::ReverseSegmentCommand::canReverse(&doc, fId, fSeg, &why));
        Q_UNUSED(lId); Q_UNUSED(lStart); Q_UNUSED(lEnd); Q_UNUSED(lSeg);
    }
}

// ---------------------------------------------------------------------------
// v2: 基准段消费者 (Polar refSegmentId=本段) 换向 + 角度补偿 → 世界位置不变。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_refSegmentConsumerCompensated()
{
    ParamDocument doc;
    auto ls = makeLine(doc, 100.0);
    doc.resolveAll();
    // C 挂在 P1, 角度基准 = 本段方向 (start→end) + 90°。
    ParamPoint c;
    c.constraint = PointConstraint::Polar;
    c.refPointId = ls.startId;
    c.refSegmentId = ls.segId;
    c.distance = 40.0;
    c.angle = 90.0;
    const QUuid cId = c.id;
    {
        auto* block = const_cast<Block*>(doc.findBlock(ls.blockId));
        block->addPoint(std::move(c));
    }
    doc.resolveAll();
    const Vec2 cBefore = doc.findBlock(ls.blockId)->worldPos(cId);

    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, ls.blockId, ls.segId));
    doc.resolveAll();

    const auto* blk = doc.findBlock(ls.blockId);
    QVERIFY(blk->worldPos(cId).distanceTo(cBefore) < 1e-6);   // 世界位置不变
    const auto* c2 = blk->findPoint(cId);
    QVERIFY(std::abs(c2->angle - 270.0) < 1e-9);              // 90 + 180 补偿

    stack.undo();
    doc.resolveAll();
    const auto* c3 = doc.findBlock(ls.blockId)->findPoint(cId);
    QVERIFY(std::abs(c3->angle - 90.0) < 1e-9);
    QVERIFY(doc.findBlock(ls.blockId)->worldPos(cId).distanceTo(cBefore) < 1e-6);
}

// ---------------------------------------------------------------------------
// v2: 相对交点 (射线角相对段方向) 换向 + interAngle 补偿 → 交点位置不变。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_relativeIntersectionCompensated()
{
    ParamDocument doc;
    auto host = makeLine(doc, 100.0);                 // 宿主段 (0,0)→(100,0)
    doc.resolveAll();
    // 射线原点: 同块内点 (50, 60) (交点求解 refPointA 同块查找)。
    ParamPoint o;
    o.constraint = PointConstraint::Free;
    o.freePos = Vec2(50.0, 60.0);
    const QUuid oId = o.id;
    {
        auto* block = const_cast<Block*>(doc.findBlock(host.blockId));
        block->addPoint(std::move(o));
    }
    doc.resolveAll();

    // 交点: 射线从 (50,60) 垂直向下 (相对宿主段方向 −90°), 打在 (50,0)。
    ParamPoint ip;
    ip.constraint = PointConstraint::Intersection;
    ip.refPointA = oId;
    ip.hostSegmentId = host.segId;
    ip.interAngle = -90.0;
    const QUuid ipId = ip.id;
    {
        auto* block = const_cast<Block*>(doc.findBlock(host.blockId));
        block->addPoint(std::move(ip));
    }
    doc.resolveAll();
    const Vec2 ipBefore = doc.findBlock(host.blockId)->worldPos(ipId);
    QVERIFY(std::abs(ipBefore.x - 50.0) < 1e-6);
    QVERIFY(std::abs(ipBefore.y) < 1e-6);

    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, host.blockId, host.segId));
    doc.resolveAll();

    const auto* blk = doc.findBlock(host.blockId);
    QVERIFY(blk->worldPos(ipId).distanceTo(ipBefore) < 1e-6);  // 交点不动
    const auto* ip2 = blk->findPoint(ipId);
    QVERIFY(std::abs(ip2->interAngle - 90.0) < 1e-9);          // −90 + 180 补偿

    stack.undo();
    doc.resolveAll();
    QVERIFY(doc.findBlock(host.blockId)->worldPos(ipId).distanceTo(ipBefore) < 1e-6);
}

// ---------------------------------------------------------------------------
// v2: 跟随线换向 — followerAngle +180 补偿, 跟随线世界姿态零跳变。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_followerAngleCompensated_noJump()
{
    ParamDocument doc;
    auto [lId, lStart, lEnd, lSeg] = makeLine(doc, 100.0);       // leader
    auto [fId, fStart, fEnd, fSeg] = makeLine(doc, 60.0, Vec2(100.0, 0.0));
    doc.resolveAll();
    // follower 起点钉在 leader 终点, 跟随角 90° (向上)。
    Attachment att;
    att.fromBlockId = fId;
    att.fromPointId = fStart;
    att.toBlockId = lId;
    att.toPointId = lEnd;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const auto* fb = doc.findBlock(fId);
    const Vec2 fsBefore = fb->worldPos(fStart);
    const Vec2 feBefore = fb->worldPos(fEnd);

    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, fId, fSeg));
    doc.resolveAll();

    // 跟随线两端世界位置零跳变; 跟随角 +180 补偿。
    const auto* fb2 = doc.findBlock(fId);
    QVERIFY(fb2->worldPos(fStart).distanceTo(fsBefore) < 1e-6);
    QVERIFY(fb2->worldPos(fEnd).distanceTo(feBefore) < 1e-6);
    const auto* att2 = doc.findAttachment(att.id);
    QVERIFY(att2);
    QVERIFY(std::abs(att2->followerAngle - 270.0) < 1e-9);
    QCOMPARE(att2->toPointId, lEnd);   // 连接拓扑不动

    stack.undo();
    doc.resolveAll();
    const auto* att3 = doc.findAttachment(att.id);
    QVERIFY(std::abs(att3->followerAngle - 90.0) < 1e-9);
    QVERIFY(doc.findBlock(fId)->worldPos(fEnd).distanceTo(feBefore) < 1e-6);
}

// ---------------------------------------------------------------------------
// v2: 被连接线 (leader) 换向 — 端点出方向不变 → 跟随者世界姿态零跳变。
// 位置连接的参照 = 端点出方向 (离体), 无需补偿也无需回填。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_connectedAsLeader_exitStable()
{
    ParamDocument doc;
    auto [lId, lStart, lEnd, lSeg] = makeLine(doc, 100.0);
    auto [fId, fStart, fEnd, fSeg] = makeLine(doc, 60.0, Vec2(100.0, 0.0));
    doc.resolveAll();
    Attachment att;
    att.fromBlockId = fId;
    att.fromPointId = fStart;
    att.toBlockId = lId;
    att.toPointId = lEnd;
    att.toSegmentId = lSeg;          // 角度参照 = leader 端点出方向
    att.followerAngle = 180.0;       // 直行延续
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const Vec2 fsBefore = doc.findBlock(fId)->worldPos(fStart);
    const Vec2 feBefore = doc.findBlock(fId)->worldPos(fEnd);

    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, lId, lSeg));
    doc.resolveAll();

    // leader 换向: 端点出方向不变 (离体语义) → follower 姿态/位置不变。
    const auto* fb = doc.findBlock(fId);
    QVERIFY(fb->worldPos(fStart).distanceTo(fsBefore) < 1e-6);
    QVERIFY(fb->worldPos(fEnd).distanceTo(feBefore) < 1e-6);
    const auto* att2 = doc.findAttachment(att.id);
    QVERIFY(att2);
    QVERIFY(std::abs(att2->followerAngle - 180.0) < 1e-9);   // 无翻转 → 不补偿
    QVERIFY(att2->angleRefPointId.isNull());                 // 无回填 (出方向稳定)

    stack.undo();
    doc.resolveAll();
    QVERIFY(doc.findBlock(fId)->worldPos(fEnd).distanceTo(feBefore) < 1e-6);
}

// ---------------------------------------------------------------------------
// v2: 旧档独立角度基准 (angleRefSegment=本段, 空 angleRefPointId) 换向 —
// 回填基准点 = 旧终点 (出方向 = 原 start→end), 跟随者零跳变。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_legacyAngleRefBackfilled()
{
    ParamDocument doc;
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 80.0, Vec2(200.0, 0.0));
    auto [lId, lStart, lEnd, lSeg] = makeLine(doc, 100.0);
    auto [fId, fStart, fEnd, fSeg] = makeLine(doc, 60.0, Vec2(100.0, 0.0));
    doc.resolveAll();
    // follower 位置钉在独立 leader A 上, 角度基准 = L 段 (旧档: 无基准点)。
    Attachment att;
    att.fromBlockId = fId;
    att.fromPointId = fStart;
    att.toBlockId = aId;
    att.toPointId = aEnd;
    att.angleRefBlockId = lId;
    att.angleRefSegmentId = lSeg;    // 角度基准 = L 的 start→end (旧档语义)
    att.followerAngle = 0.0;         // 与 L 方向折叠平行
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    const Vec2 fsBefore = doc.findBlock(fId)->worldPos(fStart);
    const Vec2 feBefore = doc.findBlock(fId)->worldPos(fEnd);

    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, lId, lSeg));
    doc.resolveAll();

    // 回填基准点吸收基准翻转 → 跟随者姿态/位置零跳变, 跟随角不变。
    const auto* fb = doc.findBlock(fId);
    QVERIFY(fb->worldPos(fStart).distanceTo(fsBefore) < 1e-6);
    QVERIFY(fb->worldPos(fEnd).distanceTo(feBefore) < 1e-6);
    const auto* att2 = doc.findAttachment(att.id);
    QVERIFY(att2);
    QVERIFY(std::abs(att2->followerAngle - 0.0) < 1e-9);
    QCOMPARE(att2->angleRefPointId, lEnd);   // 回填旧终点

    stack.undo();
    doc.resolveAll();
    const auto* att3 = doc.findAttachment(att.id);
    QVERIFY(att3->angleRefPointId.isNull());   // 恢复旧档语义
    QVERIFY(doc.findBlock(fId)->worldPos(fEnd).distanceTo(feBefore) < 1e-6);
    Q_UNUSED(lStart);
}

// ---------------------------------------------------------------------------
// v2: 曲线保形换向 — 过点反序 + 切线互换取反 + 锚点镜像, 采样零跳变。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_curveShapePreserved()
{
    ParamDocument doc;
    auto ls = makeLine(doc, 100.0);
    doc.resolveAll();

    // 升级为过两点的 Bezier: A(30%, +15) / B(70%, -15) (弦上锚点)。
    ParamPoint a;
    a.constraint = PointConstraint::CurveAnchor;
    a.hostSegmentId = ls.segId;
    a.interpPercent = 0.3;
    a.interpOffsetDist = 15.0;
    const QUuid aId = a.id;
    ParamPoint b;
    b.constraint = PointConstraint::CurveAnchor;
    b.hostSegmentId = ls.segId;
    b.interpPercent = 0.7;
    b.interpOffsetDist = -15.0;
    const QUuid bId = b.id;
    auto* block = const_cast<Block*>(doc.findBlock(ls.blockId));
    block->addPoint(std::move(a));
    block->addPoint(std::move(b));
    auto* seg = block->findSegment(ls.segId);
    seg->type = SegmentType::Bezier;
    seg->passPointIds = {aId, bId};
    block->touchGeometry();
    doc.resolveAll();

    // 采样换向前: 锚点世界位置 + 弧长 + spans 按值快照 (活指针在 resolve
    // 后会读脏缓存 — 必须拷贝)。
    const auto* blk = doc.findBlock(ls.blockId);
    const Vec2 aBefore = blk->worldPos(aId);
    const Vec2 bBefore = blk->worldPos(bId);
    const auto* entryBeforePtr = blk->curveSpanEntry(ls.segId);
    QVERIFY(entryBeforePtr);
    const double arcBefore = entryBeforePtr->arcLengthMm;
    const auto spansBefore = entryBeforePtr->spans;   // 按值快照

    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, ls.blockId, ls.segId));
    doc.resolveAll();

    // 保形: 锚点世界位置不变 + 过点反序 + 弧长/包围盒相等。
    const auto* blk2 = doc.findBlock(ls.blockId);
    const auto* seg2 = blk2->findSegment(ls.segId);
    QVERIFY(seg2->isCurve());
    QCOMPARE(seg2->passPointIds.front(), bId);   // 过点反序
    QVERIFY(blk2->worldPos(aId).distanceTo(aBefore) < 1e-6);
    QVERIFY(blk2->worldPos(bId).distanceTo(bBefore) < 1e-6);
    const auto* entryAfter = blk2->curveSpanEntry(ls.segId);
    QVERIFY(entryAfter);
    // 逐 span 精确镜像比对 (before[i] ↔ after[n-1-i], 端点/控制点互换):
    // 世界坐标 1e-6 级一致 = 曲线形状精确保持。
    for (size_t i = 0; i < spansBefore.size(); ++i) {
        const auto& sb = spansBefore[i];
        const auto& sa = entryAfter->spans[spansBefore.size() - 1 - i];
        QVERIFY2((blk2->transform.toWorld(sa.p3) - blk->transform.toWorld(sb.p0)).length() < 1e-6
                 && (blk2->transform.toWorld(sa.ctrl2) - blk->transform.toWorld(sb.ctrl1)).length() < 1e-6
                 && (blk2->transform.toWorld(sa.ctrl1) - blk->transform.toWorld(sb.ctrl2)).length() < 1e-6,
                 qPrintable(QStringLiteral("span %1 control points drifted").arg(i)));
    }
    QVERIFY(std::abs(entryAfter->arcLengthMm - arcBefore) < 1e-6);

    stack.undo();
    doc.resolveAll();
    const auto* seg3 = doc.findBlock(ls.blockId)->findSegment(ls.segId);
    QCOMPARE(seg3->passPointIds.front(), aId);   // 过点顺序恢复
}

// ---------------------------------------------------------------------------
// 换向后点的名称/备注/序列号必须原样保留 (undo round-trip 亦然)。
// 回归: 对话框端点回填曾因信号回波把"另一输入框的旧名"写进新驱动端,
// 永久覆盖点名 (populateFromModel 未静音 textChanged → applyToModel 回波)。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_namesSurviveUndoRoundTrip()
{
    ParamDocument doc;
    auto ls = makeLine(doc, 100.0);
    doc.resolveAll();
    {
        auto* block = const_cast<Block*>(doc.findBlock(ls.blockId));
        auto* sp = block->findPoint(ls.startId);
        sp->name = QStringLiteral("肩点");
        sp->annotation = QStringLiteral("起点备注");
        auto* ep = block->findPoint(ls.endId);
        ep->name = QStringLiteral("颈点");
        ep->annotation = QStringLiteral("终点备注");
    }

    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, ls.blockId, ls.segId));

    // 换向后: 身份互换但名称/备注跟点走, 一律保留。
    const auto* blk = doc.findBlock(ls.blockId);
    QVERIFY(blk);
    const auto* p1 = blk->findPoint(ls.startId);
    const auto* p2 = blk->findPoint(ls.endId);
    QCOMPARE(p1->name, QStringLiteral("肩点"));
    QCOMPARE(p1->annotation, QStringLiteral("起点备注"));
    QCOMPARE(p2->name, QStringLiteral("颈点"));
    QCOMPARE(p2->annotation, QStringLiteral("终点备注"));

    stack.undo();
    const auto* blk2 = doc.findBlock(ls.blockId);
    QCOMPARE(blk2->findPoint(ls.startId)->name, QStringLiteral("肩点"));
    QCOMPARE(blk2->findPoint(ls.endId)->name, QStringLiteral("颈点"));
    stack.redo();
    QCOMPARE(doc.findBlock(ls.blockId)->findPoint(ls.startId)->name,
             QStringLiteral("肩点"));
}

// ---------------------------------------------------------------------------
// 换向后"角度/长度编辑驱动新终点 (旧起点)、锚点 (旧终点) 纹丝不动" —
// 模拟属性卡 applyAngle 的写法 (写新终点的 Polar angle) 验证端到端语义。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_editDrivenEndAfterReverse()
{
    ParamDocument doc;
    auto ls = makeLine(doc, 100.0);   // P1(0,0)→P2(100,0), 驱动端 = P2
    doc.resolveAll();

    QUndoStack stack;
    stack.push(new cad::cmd::ReverseSegmentCommand(&doc, ls.blockId, ls.segId));
    doc.resolveAll();

    // 换向后: 驱动端 = 旧起点 P1 (新终点), 锚点 = 旧终点 P2 (新起点)。
    const Vec2 anchorBefore = doc.findBlock(ls.blockId)->worldPos(ls.endId);
    const Vec2 drivenBefore = doc.findBlock(ls.blockId)->worldPos(ls.startId);

    // 模拟角度卡 applyAngle 自由线分支: 写新终点 (P1) 的 Polar 角度 +90°。
    {
        auto* block = const_cast<Block*>(doc.findBlock(ls.blockId));
        auto* ep = block->findPoint(ls.startId);   // seg->endPointId == 旧起点
        QVERIFY(ep->constraint == PointConstraint::Polar);
        QVERIFY(ep->refPointId == ls.endId);
        ep->angle = 90.0;
        block->touchGeometry();
    }
    doc.resolveAll();

    const auto* blk = doc.findBlock(ls.blockId);
    QVERIFY(blk->worldPos(ls.endId).distanceTo(anchorBefore) < 1e-6);   // 锚不动
    QVERIFY(blk->worldPos(ls.startId).distanceTo(drivenBefore) > 1.0);  // 驱动端转了
    // 转到正上方: (0,0)→(100,0) 换向后锚在 (100,0), P1 应在 (100,-100)
    // (y 向下为正的画布约定: 角度 +90 = 顺时针, 世界 (100,-100))。
    const Vec2 drivenAfter = blk->worldPos(ls.startId);
    QVERIFY(std::abs(drivenAfter.x - 100.0) < 1e-6);
    QVERIFY(std::abs(std::abs(drivenAfter.y - anchorBefore.y) - 100.0) < 1e-6);
}

// ---------------------------------------------------------------------------
// v2 仍拒绝: 共享端点 / 角度测量 / 终点指向 / 滑轨 / 需补偿弧长。
// ---------------------------------------------------------------------------

void TestReverseSegmentCommands::reverseSegment_rejectsV2RemainingCases()
{
    // 滑轨连接拒绝。
    {
        ParamDocument doc;
        auto [lId, lStart, lEnd, lSeg] = makeLine(doc, 100.0);
        auto [fId, fStart, fEnd, fSeg] = makeLine(doc, 60.0, Vec2(0.0, 0.0));
        doc.resolveAll();
        Attachment att;
        att.fromBlockId = fId;
        att.fromPointId = fStart;
        att.toBlockId = lId;
        att.toPointId = lStart;
        att.followerAngle = 180.0;
        att.slideMode = cad::param::SlideMode::AlongLeader;
        att.slideAlongMm = 20.0;
        QVERIFY(doc.addAttachment(att));
        QString why;
        QVERIFY(!cad::cmd::ReverseSegmentCommand::canReverse(&doc, fId, fSeg, &why));
        QVERIFY(why.contains(QString::fromUtf8("滑轨")));
        Q_UNUSED(lId); Q_UNUSED(lStart); Q_UNUSED(lEnd); Q_UNUSED(lSeg);
    }
    // 需补偿的弧长连接拒绝。
    {
        ParamDocument doc;
        auto [lId, lStart, lEnd, lSeg] = makeLine(doc, 100.0);
        auto [fId, fStart, fEnd, fSeg] = makeLine(doc, 60.0, Vec2(0.0, 0.0));
        doc.resolveAll();
        Attachment att;
        att.fromBlockId = fId;
        att.fromPointId = fStart;
        att.toBlockId = lId;
        att.toPointId = lStart;
        att.rotationMode = cad::param::RotationMode::ArcLength;
        att.arcLength = 30.0;
        QVERIFY(doc.addAttachment(att));
        QString why;
        QVERIFY(!cad::cmd::ReverseSegmentCommand::canReverse(&doc, fId, fSeg, &why));
        QVERIFY(why.contains(QString::fromUtf8("弧长")));
        Q_UNUSED(lId); Q_UNUSED(lStart); Q_UNUSED(lEnd); Q_UNUSED(lSeg);
    }
}

// P2-5: the document's undo stack must be bounded. Commands snapshot the whole
// model (delete commands even snapshot their cascade subgraph), so an
// unbounded stack is an unbounded memory leak over a long editing session.
// QUndoStack drops the oldest command once the limit is reached.

QTEST_MAIN(TestReverseSegmentCommands)
#include "test_reverse_segment_commands.moc"
