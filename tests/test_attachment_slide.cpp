#include "test_attachment_helpers.h"

class TestAttachmentSlide : public QObject
{
    Q_OBJECT

private slots:
    void slideMode_alongAndPerpConstraints();
    void slideMode_dragOffsetsUndoRedo();
    void slideMode_formulaOverridesNumericAndDragClears();
};

void TestAttachmentSlide::slideMode_alongAndPerpConstraints()
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

void TestAttachmentSlide::slideMode_dragOffsetsUndoRedo()
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

void TestAttachmentSlide::slideMode_formulaOverridesNumericAndDragClears()
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


QTEST_MAIN(TestAttachmentSlide)
#include "test_attachment_slide.moc"
