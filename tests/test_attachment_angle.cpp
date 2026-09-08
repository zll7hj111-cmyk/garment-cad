#include "test_attachment_helpers.h"

class TestAttachmentAngle : public QObject
{
    Q_OBJECT

private slots:
    void setAttachmentAngleOnly_keepsFollowAngle();
    void reattachPreservesAngleRefTwoPointBasis();
    void angleRefTwoPointBasis_engineAndUndo();
    void setAttachmentAngleOnly_docHelperAndLockedClosure();
    void curvePointAttach_followsLeader();
    void endPinnedLengthEditMovesFreeStart();
    void chordLength_openingDistanceSolvingAndSwitching();
};

void TestAttachmentAngle::setAttachmentAngleOnly_keepsFollowAngle()
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

void TestAttachmentAngle::reattachPreservesAngleRefTwoPointBasis()
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

void TestAttachmentAngle::angleRefTwoPointBasis_engineAndUndo()
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

void TestAttachmentAngle::setAttachmentAngleOnly_docHelperAndLockedClosure()
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

void TestAttachmentAngle::curvePointAttach_followsLeader()
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

void TestAttachmentAngle::endPinnedLengthEditMovesFreeStart()
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
void TestAttachmentAngle::chordLength_openingDistanceSolvingAndSwitching()
{
    ParamDocument doc;
    // Leader A: (0,0) -> (100,0), length 100mm
    auto [aId, aStart, aEnd, aSeg] = makeLine(doc, 100.0);
    // Follower B: length 100mm
    auto [bId, bStart, bEnd, bSeg] = makeLine(doc, 100.0);

    for (const auto& b : doc.blocks())
        if (auto* mb = doc.blockById(b.id)) mb->layer = layerIdAt(doc, 1);

    Attachment att;
    att.fromBlockId = bId;
    att.fromPointId = bStart;
    att.toBlockId = aId;
    att.toPointId = aEnd;
    att.toSegmentId = aSeg;
    att.rotationMode = RotationMode::ChordLength;
    att.chordLength = 30.0; // 30 mm opening
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    // In closed state, B folds onto A: angle 0°, B's end is at aStart (0, 0).
    // With chord = 30.0mm on radius R = 100.0mm:
    // theta = 2 * asin(30 / 200) = 2 * asin(0.15) ≈ 17.254°
    const Vec2 bEndWorld = doc.findBlock(bId)->worldPos(bEnd);
    const Vec2 closedEndWorld = doc.findBlock(aId)->worldPos(aStart);
    const double physicalChord = bEndWorld.distanceTo(closedEndWorld);
    QVERIFY2(std::abs(physicalChord - 30.0) < 1e-4,
             qPrintable(QString("端点直线开度应为 30mm, 实际=%1").arg(physicalChord)));

    // Test formula overriding chordLength: formula in cm domain (e.g. "D_dart" = 3.0 cm -> 30 mm)
    FormulaVariable fv;
    fv.name = QStringLiteral("D_dart");
    fv.expression = QStringLiteral("3.0");
    fv.comment = QStringLiteral("测试省道开度");
    doc.addFormula(fv);
    auto* mutAtt = doc.findAttachment(att.id);
    QVERIFY(mutAtt);
    mutAtt->chordLengthFormula = QStringLiteral("D_dart");
    doc.resolveAll();

    const Vec2 bEndFormulaWorld = doc.findBlock(bId)->worldPos(bEnd);
    const double formulaChord = bEndFormulaWorld.distanceTo(closedEndWorld);
    QVERIFY2(std::abs(formulaChord - 30.0) < 1e-4, "公式求值开度应严格等于 30mm");

    // Test extreme / degenerate cases:
    // 1. C = 0: fully closed (0°)
    mutAtt->chordLengthFormula.clear();
    mutAtt->chordLength = 0.0;
    doc.resolveAll();
    const Vec2 closedPos = doc.findBlock(bId)->worldPos(bEnd);
    QVERIFY2(closedPos.distanceTo(closedEndWorld) < 1e-4, "开度 0 应完全闭合折叠");

    // 2. C > 2R (e.g. 250mm > 200mm): clamped safely to 180° straight continuation, no NaN
    mutAtt->chordLength = 250.0;
    doc.resolveAll();
    const Vec2 straightPos = doc.findBlock(bId)->worldPos(bEnd);
    // 180° continuation from (100,0) along segment direction: (200, 0)
    QVERIFY2(straightPos.distanceTo(Vec2{200.0, 0.0}) < 1e-4, "超出直径应平滑钳制为 180° 直行");
    QVERIFY(std::isfinite(straightPos.x) && std::isfinite(straightPos.y));

    // 3. Negative chordLength: reverse opening (opposite side)
    mutAtt->chordLengthFormula.clear();
    mutAtt->chordLength = -30.0;
    doc.resolveAll();
    const Vec2 negPos = doc.findBlock(bId)->worldPos(bEnd);
    const double negChord = negPos.distanceTo(closedEndWorld);
    QVERIFY2(std::abs(negChord - 30.0) < 1e-4, "负开度物理跨度应等于 30mm");
    // bEndWorld 的 y 与 negPos 的 y 应互为相反数（对称反向展开）
    QVERIFY2(std::abs(negPos.y - (-bEndWorld.y)) < 1e-4, "负开度应向相反侧（顺时针）对称反向展开");

    // 4. Negative formula overriding chordLength (e.g. "-D_dart" -> -3.0 cm -> -30 mm)
    mutAtt->chordLengthFormula = QStringLiteral("-D_dart");
    doc.resolveAll();
    const Vec2 negFormulaPos = doc.findBlock(bId)->worldPos(bEnd);
    const double negFormulaChord = negFormulaPos.distanceTo(closedEndWorld);
    QVERIFY2(std::abs(negFormulaChord - 30.0) < 1e-4, "负公式开度物理跨度应等于 30mm");
    QVERIFY2(negFormulaPos.distanceTo(negPos) < 1e-4, "负公式与负数值几何位置严格一致");

    // 5. Compound expression (e.g. "5.0 - 8.0" -> -3.0 cm -> -30 mm)
    mutAtt->chordLengthFormula = QStringLiteral("5.0 - 8.0");
    doc.resolveAll();
    const Vec2 exprPos = doc.findBlock(bId)->worldPos(bEnd);
    QVERIFY2(exprPos.distanceTo(negPos) < 1e-4, "复合表达式 5-8 求值应与负数开度严格一致");

    // 6. Mode switch preserving negative direction
    mutAtt->chordLengthFormula.clear();
    mutAtt->chordLength = -30.0;
    doc.resolveAll();
    auto resNegAngle = followerModeSwitchValues(*mutAtt, 100.0, RotationMode::Angle, doc.parameters(), {});
    mutAtt->rotationMode = RotationMode::Angle;
    mutAtt->followerAngle = resNegAngle.angle;
    doc.resolveAll();
    const Vec2 switchedAnglePos = doc.findBlock(bId)->worldPos(bEnd);
    QVERIFY2(switchedAnglePos.distanceTo(negPos) < 1e-4, "负开度切角度几何保持");

    auto resNegChord = followerModeSwitchValues(*mutAtt, 100.0, RotationMode::ChordLength, doc.parameters(), {});
    QVERIFY2(resNegChord.chordMm < -1.0, "切回开度应保持为负开度");
    QVERIFY2(std::abs(resNegChord.chordMm - (-30.0)) < 1e-4, "切回开度数值严格保真");
    mutAtt->rotationMode = RotationMode::ChordLength;
    mutAtt->chordLength = resNegChord.chordMm;
    doc.resolveAll();
    const Vec2 switchedBackPos = doc.findBlock(bId)->worldPos(bEnd);
    QVERIFY2(switchedBackPos.distanceTo(negPos) < 1e-4, "切回开度几何位置保持");

    // Test 3-mode zero-jump geometry switch
    mutAtt->chordLength = 30.0;
    doc.resolveAll();
    const Vec2 origPos = doc.findBlock(bId)->worldPos(bEnd);

    // Switch to ArcLength
    auto resArc = followerModeSwitchValues(*mutAtt, 100.0, RotationMode::ArcLength, doc.parameters(), {});
    mutAtt->rotationMode = RotationMode::ArcLength;
    mutAtt->arcLength = resArc.arcMm;
    doc.resolveAll();
    const Vec2 arcPos = doc.findBlock(bId)->worldPos(bEnd);
    QVERIFY2(arcPos.distanceTo(origPos) < 1e-4, "ChordLength -> ArcLength 几何位置保持");

    // Switch to Angle
    auto resAngle = followerModeSwitchValues(*mutAtt, 100.0, RotationMode::Angle, doc.parameters(), {});
    mutAtt->rotationMode = RotationMode::Angle;
    mutAtt->followerAngle = resAngle.angle;
    doc.resolveAll();
    const Vec2 anglePos = doc.findBlock(bId)->worldPos(bEnd);
    QVERIFY2(anglePos.distanceTo(origPos) < 1e-4, "ArcLength -> Angle 几何位置保持");

    // Switch back to ChordLength
    auto resChord = followerModeSwitchValues(*mutAtt, 100.0, RotationMode::ChordLength, doc.parameters(), {});
    mutAtt->rotationMode = RotationMode::ChordLength;
    mutAtt->chordLength = resChord.chordMm;
    doc.resolveAll();
    const Vec2 backPos = doc.findBlock(bId)->worldPos(bEnd);
    QVERIFY2(backPos.distanceTo(origPos) < 1e-4, "Angle -> ChordLength 几何位置保持");
    QVERIFY2(std::abs(mutAtt->chordLength - 30.0) < 1e-4, "回切弦长数值保真");

    // Test serialization round-trip
    mutAtt->chordLengthFormula = QStringLiteral("D_dart * 1.5");
    const QJsonObject json = DocumentSerializer::serialize(doc);
    ParamDocument doc2;
    DocumentSerializer::deserialize(doc2, json);
    const auto* roundAtt = doc2.findAttachment(att.id);
    QVERIFY(roundAtt);
    QCOMPARE(roundAtt->rotationMode, RotationMode::ChordLength);
    QVERIFY2(std::abs(roundAtt->chordLength - 30.0) < 1e-6, "序列化往返 chordLength 保真");
    QCOMPARE(roundAtt->chordLengthFormula, QStringLiteral("D_dart * 1.5"));
}


QTEST_MAIN(TestAttachmentAngle)
#include "test_attachment_angle.moc"
