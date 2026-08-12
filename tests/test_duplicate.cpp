#include <QtTest>
#include <QUuid>

#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Duplicate.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "geometry/Vec2.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

/// Handles of a simple 2-point / 1-segment line block.
struct LineInfo {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

/// Add a horizontal line block: Free start at local (0,0), Polar end.
LineInfo makeLine(ParamDocument& doc, const Vec2& origin, double lenMm,
                  const QString& distanceFormula = {})
{
    Block block;
    block.transform.origin = origin;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = lenMm;
    p2.angle = 0.0;
    p2.distanceFormula = distanceFormula;
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

/// Bridge pinned between two host points (same construction as ToolSmartPen).
LineInfo makeBridge(ParamDocument& doc,
                    const QUuid& hostA, const QUuid& hostAPoint,
                    const QUuid& hostB, const QUuid& hostBPoint)
{
    const Vec2 startWorld = doc.findBlock(hostA)->worldPos(hostAPoint);
    const Vec2 endWorld   = doc.findBlock(hostB)->worldPos(hostBPoint);

    Block block;
    block.isBridge = true;
    block.transform.origin = startWorld;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;

    const Vec2 delta = endWorld - startWorld;
    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = delta.length();
    p2.angle = std::atan2(delta.y, delta.x) * 180.0 / M_PI;
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

    Attachment pinStart;
    pinStart.isPin = true;
    pinStart.fromBlockId = blockId;
    pinStart.fromPointId = startId;
    pinStart.toBlockId = hostA;
    pinStart.toPointId = hostAPoint;

    Attachment pinEnd;
    pinEnd.isPin = true;
    pinEnd.fromBlockId = blockId;
    pinEnd.fromPointId = endId;
    pinEnd.toBlockId = hostB;
    pinEnd.toPointId = hostBPoint;

    doc.addAttachment(std::move(pinStart));
    doc.addAttachment(std::move(pinEnd));
    return {blockId, startId, endId, segId};
}

/// Resolved length (mm) of the first segment of a block in the document.
double segmentLength(const ParamDocument& doc, const QUuid& blockId)
{
    const Block* b = doc.findBlock(blockId);
    if (!b || b->segments.empty()) return -1.0;
    const Segment& seg = b->segments.front();
    const ParamPoint* sp = b->findPoint(seg.startPointId);
    const ParamPoint* ep = b->findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return -1.0;
    return sp->resolvedPos.distanceTo(ep->resolvedPos);
}

} // namespace

class TestDuplicate : public QObject
{
    Q_OBJECT

private slots:
    void singleLineClone();
    void numericLengthCopyTracksOriginal();
    void sourceDeletionBakesNumericLength();
    void crossBoundaryAttachmentDropped();
    void internalAttachmentKept();
    void bridgeFullSetStaysBridge();
    void bridgeAloneReleasedWithLinkedVar();
    void bridgeMixedPinBecomesFollower();
    void existingLinkedVarReused();
    void endTargetOutsideClearedOnClone();
    void endTargetInsideRemappedOnClone();
    void followAndInterpRefOutsideClearedOnClone();
};

// 单线克隆：除 ID/serial 外信息全同，内部引用重映射，参数联动保留。
void TestDuplicate::singleLineClone()
{
    ParamDocument doc;
    doc.setParameter(QStringLiteral("b"), 50.0);  // cm domain

    LineInfo line = makeLine(doc, Vec2(10.0, 20.0), 250.0,
                             QStringLiteral("b/2"));
    Block* orig = doc.findBlock(line.blockId);
    QVERIFY(orig);
    orig->name = QStringLiteral("肩线");
    orig->segments.front().name = QStringLiteral("S1");
    orig->segments.front().role = SegmentRole::Internal;
    orig->segments.front().lineStyle = LineStyle::Dashed;
    doc.resolveAll();

    const size_t blockCountBefore = doc.blocks().size();
    DuplicateResult result = duplicateBlocks(doc, {line.blockId});

    // duplicateBlocks itself must not mutate the document.
    QCOMPARE(doc.blocks().size(), blockCountBefore);

    QCOMPARE(result.blocks.size(), size_t(1));
    QVERIFY(result.attachments.empty());
    QVERIFY(result.newLinked.empty());

    const Block& clone = result.blocks.front();
    QVERIFY(clone.id != orig->id);
    QCOMPARE(clone.name, orig->name);
    QCOMPARE(clone.transform.origin.x, orig->transform.origin.x);
    QCOMPARE(clone.transform.origin.y, orig->transform.origin.y);

    // Fresh point/segment ids + serials, parametric info identical.
    QCOMPARE(clone.points.size(), size_t(2));
    QVERIFY(clone.points[0].id != orig->points[0].id);
    QVERIFY(clone.points[1].id != orig->points[1].id);
    QVERIFY(!clone.points[0].serial.isEmpty());
    QVERIFY(clone.points[0].serial != orig->points[0].serial);
    QVERIFY(clone.points[1].serial != orig->points[1].serial);
    QCOMPARE(clone.points[1].distanceFormula, QStringLiteral("b/2"));

    const Segment& cseg = clone.segments.front();
    QVERIFY(cseg.id != line.segId);
    QVERIFY(!cseg.serial.isEmpty());
    QVERIFY(cseg.serial != orig->segments.front().serial);
    QCOMPARE(cseg.name, QStringLiteral("S1"));
    QCOMPARE(cseg.role, SegmentRole::Internal);
    QCOMPARE(cseg.lineStyle, LineStyle::Dashed);

    // Internal references remapped onto the clone's own points.
    QCOMPARE(cseg.startPointId, clone.points[0].id);
    QCOMPARE(cseg.endPointId, clone.points[1].id);
    QCOMPARE(clone.points[1].refPointId, clone.points[0].id);

    // The clone keeps following the parameter after joining the document.
    const QUuid cloneId = clone.id;
    doc.addBlock(clone);
    doc.setParameter(QStringLiteral("b"), 60.0);  // → 30cm → 300mm
    QVERIFY(std::abs(segmentLength(doc, cloneId) - 300.0) < 1e-6);
    QVERIFY(std::abs(segmentLength(doc, line.blockId) - 300.0) < 1e-6);
}

// 数值长度的副本：自动发布原线段的关联变量，改原线长度副本跟随。
// （用户拍板：只关联长度，角度保持数值。）
void TestDuplicate::numericLengthCopyTracksOriginal()
{
    ParamDocument doc;
    LineInfo line = makeLine(doc, Vec2::zero(), 210.0);  // 21cm, 纯数值

    DuplicateResult result = duplicateBlocks(doc, {line.blockId});
    QCOMPARE(result.blocks.size(), size_t(1));
    QCOMPARE(result.newLinked.size(), size_t(1));

    // Linked variable published against the ORIGINAL segment.
    const LinkedVariable& lv = result.newLinked.front();
    QCOMPARE(lv.sourceBlockId, line.blockId);
    QCOMPARE(lv.sourceSegmentId, line.segId);
    const Block* orig = doc.findBlock(line.blockId);
    QCOMPARE(lv.refName,
             QStringLiteral("L") + orig->segments.front().serial);

    // The copy's length is driven by the linked variable; angle stays numeric.
    const Block& clone = result.blocks.front();
    const Segment& cseg = clone.segments.front();
    QCOMPARE(cseg.lengthFormula, lv.refName);
    const ParamPoint* cEnd = clone.findPoint(cseg.endPointId);
    QVERIFY(cEnd);
    QCOMPARE(cEnd->distanceFormula, lv.refName);
    QVERIFY(cEnd->angleFormula.isEmpty());
    QCOMPARE(cEnd->angle, 0.0);

    doc.addLinked(lv);
    doc.addBlock(clone);
    doc.resolveAll();
    QVERIFY(std::abs(segmentLength(doc, clone.id) - 210.0) < 1e-6);

    // Edit the ORIGINAL: 21 → 50. The copy follows within the same resolve.
    doc.findBlock(line.blockId)->findPoint(line.endId)->distance = 500.0;
    doc.resolveAll();
    QVERIFY(std::abs(segmentLength(doc, line.blockId) - 500.0) < 1e-6);
    QVERIFY(std::abs(segmentLength(doc, clone.id) - 500.0) < 1e-6);
}

// 删除被引用的原线段：副本长度固化为数值（引用对象没了）。
void TestDuplicate::sourceDeletionBakesNumericLength()
{
    ParamDocument doc;
    LineInfo line = makeLine(doc, Vec2::zero(), 210.0);

    DuplicateResult result = duplicateBlocks(doc, {line.blockId});
    doc.addLinked(result.newLinked.front());
    doc.addBlock(result.blocks.front());
    doc.resolveAll();
    const QUuid cloneId = result.blocks.front().id;

    // Stretch the original so the frozen value is the LATEST measurement.
    doc.findBlock(line.blockId)->findPoint(line.endId)->distance = 500.0;
    doc.resolveAll();
    QVERIFY(std::abs(segmentLength(doc, cloneId) - 500.0) < 1e-6);

    doc.removeBlock(line.blockId);

    // Linked variable auto-deleted with its source; the copy's formulas are
    // baked back to the frozen number and it keeps resolving on its own.
    QVERIFY(doc.linkedVars().empty());
    const Block* clone = doc.findBlock(cloneId);
    QVERIFY(clone);
    QVERIFY(clone->segments.front().lengthFormula.isEmpty());
    const ParamPoint* cEnd =
        clone->findPoint(clone->segments.front().endPointId);
    QVERIFY(cEnd);
    QVERIFY(cEnd->distanceFormula.isEmpty());
    QVERIFY(std::abs(cEnd->distance - 500.0) < 1e-6);
    QVERIFY(std::abs(segmentLength(doc, cloneId) - 500.0) < 1e-6);
}

// 只复制跟随线：跨界连接丢弃，副本变自由线段并冻结当前世界姿态。
void TestDuplicate::crossBoundaryAttachmentDropped()
{
    ParamDocument doc;
    LineInfo leader = makeLine(doc, Vec2::zero(), 100.0);
    LineInfo follower = makeLine(doc, Vec2(100.0, 0.0), 80.0);

    Attachment att;
    att.fromBlockId = follower.blockId;
    att.fromPointId = follower.startId;
    att.toBlockId = leader.blockId;
    att.toPointId = leader.endId;
    att.toSegmentId = leader.segId;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));

    DuplicateResult result = duplicateBlocks(doc, {follower.blockId});
    QCOMPARE(result.blocks.size(), size_t(1));
    QVERIFY(result.attachments.empty());

    // World pose frozen: the copied transform equals the follower's current one.
    const Block* origFollower = doc.findBlock(follower.blockId);
    const Block& clone = result.blocks.front();
    QCOMPARE(clone.transform.origin.x, origFollower->transform.origin.x);
    QCOMPARE(clone.transform.origin.y, origFollower->transform.origin.y);
    QCOMPARE(clone.transform.rotation, origFollower->transform.rotation);
}

// 整组复制：集合内部连接克隆并重映射，跟随角度保留。
void TestDuplicate::internalAttachmentKept()
{
    ParamDocument doc;
    LineInfo leader = makeLine(doc, Vec2::zero(), 100.0);
    LineInfo follower = makeLine(doc, Vec2(100.0, 0.0), 80.0);

    Attachment att;
    att.fromBlockId = follower.blockId;
    att.fromPointId = follower.startId;
    att.toBlockId = leader.blockId;
    att.toPointId = leader.endId;
    att.toSegmentId = leader.segId;
    att.followerAngle = 45.0;
    QVERIFY(doc.addAttachment(att));

    DuplicateResult result =
        duplicateBlocks(doc, {leader.blockId, follower.blockId});
    QCOMPARE(result.blocks.size(), size_t(2));
    QCOMPARE(result.attachments.size(), size_t(1));

    const Attachment& catt = result.attachments.front();
    QVERIFY(catt.id != att.id);
    QVERIFY(!catt.isPin);
    QCOMPARE(catt.followerAngle, 45.0);

    // Endpoints remapped onto the two clones (not the originals).
    QUuid cloneLeaderId, cloneFollowerId;
    for (const Block& b : result.blocks) {
        QVERIFY(b.id != leader.blockId && b.id != follower.blockId);
        if (b.segments.front().startPointId == catt.toPointId
            || b.segments.front().endPointId == catt.toPointId)
            cloneLeaderId = b.id;
        if (b.findPoint(catt.fromPointId))
            cloneFollowerId = b.id;
    }
    QCOMPARE(catt.toBlockId, cloneLeaderId);
    QCOMPARE(catt.fromBlockId, cloneFollowerId);

    // The cloned pair joins the document as a working group.
    for (const LinkedVariable& lv : result.newLinked)
        doc.addLinked(lv);   // numeric lengths were auto-linked (长度关联)
    for (const Block& b : result.blocks)
        doc.addBlock(b);
    QVERIFY(doc.addAttachment(catt));
    doc.resolveAll();
    const Block* cl = doc.findBlock(cloneLeaderId);
    const Block* cf = doc.findBlock(cloneFollowerId);
    const Vec2 leaderEnd = cl->worldPos(cl->segments.front().endPointId);
    const Vec2 followerStart = cf->worldPos(cf->segments.front().startPointId);
    QVERIFY(leaderEnd.distanceTo(followerStart) < 1e-6);
}

// 桥接线 + 两个宿主全部复制：副本保持桥接（两个 pin 均克隆重映射）。
void TestDuplicate::bridgeFullSetStaysBridge()
{
    ParamDocument doc;
    LineInfo h1 = makeLine(doc, Vec2::zero(), 100.0);
    LineInfo h2 = makeLine(doc, Vec2(200.0, 50.0), 100.0);
    LineInfo bridge = makeBridge(doc, h1.blockId, h1.endId,
                                 h2.blockId, h2.startId);

    DuplicateResult result = duplicateBlocks(
        doc, {h1.blockId, h2.blockId, bridge.blockId});
    QCOMPARE(result.blocks.size(), size_t(3));
    QCOMPARE(result.attachments.size(), size_t(2));
    // Host lines had numeric lengths → auto-linked; the bridge copy stays
    // passive (no linked variable of its own).
    QCOMPARE(result.newLinked.size(), size_t(2));

    const Block* cloneBridge = nullptr;
    for (const Block& b : result.blocks)
        if (b.isBridge) cloneBridge = &b;
    QVERIFY(cloneBridge);

    for (const Attachment& a : result.attachments) {
        QVERIFY(a.isPin);
        QCOMPARE(a.fromBlockId, cloneBridge->id);
        QVERIFY(a.toBlockId != h1.blockId && a.toBlockId != h2.blockId);
    }

    // The cloned trio works as a live bridge in the document.
    for (const LinkedVariable& lv : result.newLinked)
        doc.addLinked(lv);
    for (const Block& b : result.blocks)
        doc.addBlock(b);
    for (const Attachment& a : result.attachments)
        QVERIFY(doc.addAttachment(a));
    doc.resolveAll();
    QVERIFY(std::abs(segmentLength(doc, cloneBridge->id)
                     - std::sqrt(100.0 * 100.0 + 50.0 * 50.0)) < 1e-6);
}

// 只复制桥接线：副本释放为自由线段，长度填入原桥的关联变量并持续跟踪。
void TestDuplicate::bridgeAloneReleasedWithLinkedVar()
{
    ParamDocument doc;
    LineInfo h1 = makeLine(doc, Vec2::zero(), 100.0);
    LineInfo h2 = makeLine(doc, Vec2(200.0, 50.0), 100.0);
    LineInfo bridge = makeBridge(doc, h1.blockId, h1.endId,
                                 h2.blockId, h2.startId);
    const double origLen = std::sqrt(100.0 * 100.0 + 50.0 * 50.0);

    DuplicateResult result = duplicateBlocks(doc, {bridge.blockId});
    QCOMPARE(result.blocks.size(), size_t(1));
    QVERIFY(result.attachments.empty());
    QCOMPARE(result.newLinked.size(), size_t(1));

    const Block& clone = result.blocks.front();
    QVERIFY(!clone.isBridge);

    // Frozen stretched geometry: origin at the bridge's world start point.
    QVERIFY(std::abs(clone.transform.origin.x - 100.0) < 1e-6);
    QVERIFY(std::abs(clone.transform.origin.y - 0.0) < 1e-6);
    const ParamPoint* cEnd = clone.findPoint(clone.segments.front().endPointId);
    QVERIFY(cEnd);
    QVERIFY(std::abs(cEnd->distance - origLen) < 1e-6);

    // Linked variable published against the ORIGINAL bridge segment.
    const LinkedVariable& lv = result.newLinked.front();
    QCOMPARE(lv.sourceBlockId, bridge.blockId);
    const Block* origBridge = doc.findBlock(bridge.blockId);
    QCOMPARE(lv.refName,
             QStringLiteral("L") + origBridge->segments.front().serial);
    QCOMPARE(clone.segments.front().lengthFormula, lv.refName);
    QCOMPARE(cEnd->distanceFormula, lv.refName);

    // Copy keeps tracking the bridge's passive length: stretch the bridge
    // by moving host2, then resolve (resolveAll re-measures linked variables
    // after the pass and converges in the same call).
    doc.addLinked(lv);
    doc.addBlock(clone);
    doc.findBlock(h2.blockId)->transform.origin = Vec2(200.0, 100.0);
    doc.resolveAll();
    const double newLen = std::sqrt(100.0 * 100.0 + 100.0 * 100.0);
    QVERIFY(std::abs(segmentLength(doc, bridge.blockId) - newLen) < 1e-6);
    QVERIFY(std::abs(segmentLength(doc, clone.id) - newLen) < 1e-6);
}

// 混合情况：一个 pin 宿主在集合内 → 该 pin 转普通跟随连接，另一端释放。
void TestDuplicate::bridgeMixedPinBecomesFollower()
{
    ParamDocument doc;
    LineInfo h1 = makeLine(doc, Vec2::zero(), 100.0);
    LineInfo h2 = makeLine(doc, Vec2(200.0, 50.0), 100.0);
    LineInfo bridge = makeBridge(doc, h1.blockId, h1.endId,
                                 h2.blockId, h2.startId);

    DuplicateResult result =
        duplicateBlocks(doc, {bridge.blockId, h1.blockId});
    QCOMPARE(result.blocks.size(), size_t(2));
    QCOMPARE(result.attachments.size(), size_t(1));
    // Two linked variables: the released bridge copy + host1's numeric length.
    QCOMPARE(result.newLinked.size(), size_t(2));

    const Block* cloneBridge = nullptr;
    const Block* cloneH1 = nullptr;
    for (const Block& b : result.blocks) {
        if (b.findPoint(result.attachments.front().fromPointId))
            cloneBridge = &b;
        else
            cloneH1 = &b;
    }
    QVERIFY(cloneBridge && cloneH1);
    QVERIFY(!cloneBridge->isBridge);  // released copy

    const Attachment& catt = result.attachments.front();
    QVERIFY(!catt.isPin);  // pin downgraded to a normal follower attachment
    QCOMPARE(catt.rotationMode, RotationMode::Angle);
    QCOMPARE(catt.fromBlockId, cloneBridge->id);
    QCOMPARE(catt.toBlockId, cloneH1->id);
    QVERIFY(catt.followerAngleFormula.isEmpty());

    // Whole result joins the document; the follower sticks to its new leader.
    for (const LinkedVariable& lv : result.newLinked)
        doc.addLinked(lv);
    for (const Block& b : result.blocks)
        doc.addBlock(b);
    QVERIFY(doc.addAttachment(catt));
    doc.resolveAll();

    const Block* cb = doc.findBlock(cloneBridge->id);
    const Block* ch = doc.findBlock(cloneH1->id);
    const Vec2 hostEnd = ch->worldPos(ch->segments.front().endPointId);
    const Vec2 bridgeStart = cb->worldPos(cb->segments.front().startPointId);
    QVERIFY(hostEnd.distanceTo(bridgeStart) < 1e-6);

    // Frozen direction preserved: the back-solved follower angle keeps
    // the copy's segment direction identical to the original bridge's.
    QVERIFY(std::abs(segmentLength(doc, cb->id)
                     - std::sqrt(100.0 * 100.0 + 50.0 * 50.0)) < 1e-6);
    const Vec2 bridgeEnd = cb->worldPos(cb->segments.front().endPointId);
    const Vec2 dir = bridgeEnd - bridgeStart;
    const double worldAngle = std::atan2(dir.y, dir.x);
    QVERIFY(std::abs(worldAngle - std::atan2(50.0, 100.0)) < 1e-6);
}

// 原桥已发布过关联变量：直接复用，不重复发布。
void TestDuplicate::existingLinkedVarReused()
{
    ParamDocument doc;
    LineInfo h1 = makeLine(doc, Vec2::zero(), 100.0);
    LineInfo h2 = makeLine(doc, Vec2(200.0, 50.0), 100.0);
    LineInfo bridge = makeBridge(doc, h1.blockId, h1.endId,
                                 h2.blockId, h2.startId);

    LinkedVariable existing;
    existing.sourceBlockId = bridge.blockId;
    existing.sourceSegmentId = bridge.segId;
    existing.refName = QStringLiteral("Lcustom");
    existing.name = QStringLiteral("桥长");
    doc.addLinked(existing);

    DuplicateResult result = duplicateBlocks(doc, {bridge.blockId});
    QCOMPARE(result.blocks.size(), size_t(1));
    QVERIFY(result.newLinked.empty());  // reused, not re-published
    QCOMPARE(result.blocks.front().segments.front().lengthFormula,
             QStringLiteral("Lcustom"));
}

// 副本不继承指向复制集外的 endTarget（与 RotateCopyGesture 语义一致）。
void TestDuplicate::endTargetOutsideClearedOnClone()
{
    ParamDocument doc;
    LineInfo a = makeLine(doc, Vec2::zero(), 100.0);
    LineInfo b = makeLine(doc, Vec2(300.0, 0.0), 100.0);

    // A aims at B's end point (B stays outside the copied set).
    Block* blk = doc.findBlock(a.blockId);
    blk->endTargetBlockId = b.blockId;
    blk->endTargetPointId = b.endId;

    DuplicateResult result = duplicateBlocks(doc, {a.blockId});
    QCOMPARE(result.blocks.size(), size_t(1));
    const Block& clone = result.blocks.front();
    QVERIFY(clone.endTargetBlockId.isNull());
    QVERIFY(clone.endTargetPointId.isNull());
    // The original keeps its aim constraint untouched.
    QCOMPARE(doc.findBlock(a.blockId)->endTargetBlockId, b.blockId);
    QCOMPARE(doc.findBlock(a.blockId)->endTargetPointId, b.endId);
}

// endTarget 指向复制集内 → 副本保持相对指向（remap 到副本目标块）。
void TestDuplicate::endTargetInsideRemappedOnClone()
{
    ParamDocument doc;
    LineInfo a = makeLine(doc, Vec2::zero(), 100.0);
    LineInfo b = makeLine(doc, Vec2(300.0, 0.0), 100.0);

    Block* blk = doc.findBlock(a.blockId);
    blk->endTargetBlockId = b.blockId;
    blk->endTargetPointId = b.endId;

    DuplicateResult result = duplicateBlocks(doc, {a.blockId, b.blockId});
    QCOMPARE(result.blocks.size(), size_t(2));
    const Block* cloneA = nullptr;
    const Block* cloneB = nullptr;
    for (const Block& c : result.blocks) {
        if (c.endTargetBlockId.isNull())
            cloneB = &c;
        else
            cloneA = &c;
    }
    QVERIFY(cloneA && cloneB);
    QCOMPARE(cloneA->endTargetBlockId, cloneB->id);

    // The clone-B end point maps through the idMap (same point index).
    const Block* origB = doc.findBlock(b.blockId);
    int endIdx = -1;
    for (size_t i = 0; i < origB->points.size(); ++i) {
        if (origB->points[i].id == b.endId) { endIdx = static_cast<int>(i); break; }
    }
    QVERIFY(endIdx >= 0);
    QVERIFY(cloneB->points.size() > size_t(endIdx));
    QCOMPARE(cloneA->endTargetPointId, cloneB->points[size_t(endIdx)].id);
}

// CurveAnchor follow 与 Interpolated 参考点指向集外 → 副本清空（防止副本
// 被原块拉动 / 永不解析），原块保持不变。
void TestDuplicate::followAndInterpRefOutsideClearedOnClone()
{
    ParamDocument doc;
    LineInfo a = makeLine(doc, Vec2::zero(), 100.0);
    LineInfo b = makeLine(doc, Vec2(300.0, 0.0), 100.0);

    // A carries a curve anchor following B's start point...
    Block* blk = doc.findBlock(a.blockId);
    ParamPoint anchor;
    anchor.constraint = PointConstraint::CurveAnchor;
    anchor.hostSegmentId = a.segId;
    anchor.followBlockId = b.blockId;
    anchor.followPointId = b.startId;
    blk->addPoint(anchor);

    // ...and an interpolated aux point measuring from B's start point.
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = a.segId;
    aux.interpPercent = 0.5;
    aux.interpRefPointId = b.startId;
    blk->addPoint(aux);

    DuplicateResult result = duplicateBlocks(doc, {a.blockId});
    QCOMPARE(result.blocks.size(), size_t(1));
    const Block& clone = result.blocks.front();

    bool foundAnchor = false, foundAux = false;
    for (const ParamPoint& p : clone.points) {
        if (p.constraint == PointConstraint::CurveAnchor) {
            foundAnchor = true;
            QVERIFY(p.followBlockId.isNull());
            QVERIFY(p.followPointId.isNull());
        }
        if (p.constraint == PointConstraint::Interpolated) {
            foundAux = true;
            QVERIFY(p.interpRefPointId.isNull());
        }
    }
    QVERIFY(foundAnchor && foundAux);

    // The original keeps its follow / measurement references.
    const Block* orig = doc.findBlock(a.blockId);
    for (const ParamPoint& p : orig->points) {
        if (p.constraint == PointConstraint::CurveAnchor) {
            QCOMPARE(p.followBlockId, b.blockId);
            QCOMPARE(p.followPointId, b.startId);
        }
        if (p.constraint == PointConstraint::Interpolated)
            QCOMPARE(p.interpRefPointId, b.startId);
    }
}

QTEST_MAIN(TestDuplicate)
#include "test_duplicate.moc"
