#include "test_attachment_helpers.h"

class TestAttachmentShadow : public QObject
{
    Q_OBJECT

private slots:
    void shadowDetach_mountChainFollowsHost();
    void shadowDetach_formulaOffsetPreserved();
    void shadowLifecycle_stateMachineTransitions();
    void shadowReconnect_cachesLastConnectedObject();
    void shadowDetach_degradeAndClearShadow();
    void auxPointAttachment_detachAndReconnect();
};

void TestAttachmentShadow::shadowDetach_mountChainFollowsHost()
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

void TestAttachmentShadow::shadowDetach_formulaOffsetPreserved()
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

void TestAttachmentShadow::shadowLifecycle_stateMachineTransitions()
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

void TestAttachmentShadow::shadowReconnect_cachesLastConnectedObject()
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

    QUndoStack stack;

    // 1. 初次拆开 (从 A 拆开): 建立影子 S, 缓存上次宿主为 A
    stack.push(new cad::cmd::SetAttachmentAngleOnlyCommand(&doc, att.id, true));
    const auto* attAfterDetach = doc.findAttachment(att.id);
    QVERIFY(attAfterDetach && attAfterDetach->angleOnly);
    const auto* shadow = doc.blockById(attAfterDetach->toBlockId);
    QVERIFY(shadow && shadow->isShadow);
    QCOMPARE(shadow->shadowMasterBlockId, aId);
    QCOMPARE(shadow->shadowLastHostBlockId, aId);
    QCOMPARE(shadow->shadowLastHostPointId, aEnd);

    // 2. 挂载到新线 C (ShadowMountCommand): 验证焊接 isLocked 与拖动闭包
    const QUuid shadowId = shadow->id;
    stack.push(new cad::cmd::ShadowMountCommand(&doc, shadowId, cId, cStart, cSeg));
    const auto* att1 = doc.findAtt1OfShadow(shadowId);
    QVERIFY(att1 != nullptr);
    QCOMPARE(att1->toBlockId, cId);
    QCOMPARE(att1->toPointId, cStart);
    QVERIFY2(att1->isLocked, "影子挂载 Att1 必须焊接锁定 (isLocked=true)");
    const auto* shadowMounted = doc.blockById(shadowId);
    QCOMPARE(shadowMounted->shadowLastHostBlockId, cId);

    // 拖动闭包验证: 拖 B 时, lockedClosure 必须包含 B, 影子, C (整体跟随不撕裂)
    const QSet<QUuid> closure = doc.lockedClosure(QSet<QUuid>{bId});
    QVERIFY2(closure.contains(bId) && closure.contains(shadowId) && closure.contains(cId),
             "焊接闭包包含 B、影子与 C (选择工具拖动不撕裂)");

    // 3. 从新线 C 拆开 (ReDetach): 影子断开 C, 处于拆开态, 但记住上次宿主是 C
    stack.push(new cad::cmd::SetAttachmentAngleOnlyCommand(&doc, att.id, true));
    QCOMPARE(doc.attachments().size(), size_t(1));
    const auto* attDetachedFromC = doc.findAttachment(att.id);
    QVERIFY(attDetachedFromC && attDetachedFromC->angleOnly);
    const auto* shadowDetachedFromC = doc.blockById(shadowId);
    QVERIFY(shadowDetachedFromC != nullptr);
    QCOMPARE(shadowDetachedFromC->shadowLastHostBlockId, cId);
    QCOMPARE(shadowDetachedFromC->shadowLastHostPointId, cStart);

    // 4. 重连 (SetAttachmentAngleOnlyCommand, angleOnly=false):
    // 核心断言: 必须重连到最近连接的 C, 而不是本体 A!
    stack.push(new cad::cmd::SetAttachmentAngleOnlyCommand(&doc, att.id, false));
    QCOMPARE(doc.attachments().size(), size_t(2));
    const auto* att1Reconnected = doc.findAtt1OfShadow(shadowId);
    QVERIFY2(att1Reconnected != nullptr, "重连后重新建立 Att1");
    QCOMPARE(att1Reconnected->toBlockId, cId);
    QCOMPARE(att1Reconnected->toPointId, cStart);
    QVERIFY(att1Reconnected->isLocked);
    const auto* att2Reconnected = doc.findAtt2OfShadow(shadowId);
    QVERIFY(att2Reconnected != nullptr && !att2Reconnected->angleOnly && att2Reconnected->isLocked);

    // 验证位置: B 的起点与 C 的起点对齐
    const Vec2 bStartWorld = doc.findBlock(bId)->worldPos(bStart);
    const Vec2 cStartWorld = doc.findBlock(cId)->worldPos(cStart);
    QVERIFY2(bStartWorld.distanceTo(cStartWorld) < 1e-4, "B 重连回 C 的端点位置对齐");

    // 5. 撤销重连 (undo): 回到从 C 拆开态
    stack.undo();
    QCOMPARE(doc.attachments().size(), size_t(1));
    QVERIFY(doc.findAttachment(att.id)->angleOnly);
    QVERIFY(doc.findAtt1OfShadow(shadowId) == nullptr);

    // 重做 (redo): 再次恢复连到 C
    stack.redo();
    QCOMPARE(doc.attachments().size(), size_t(2));
    QVERIFY(!doc.findAttachment(att.id)->angleOnly);
    QVERIFY(doc.findAtt1OfShadow(shadowId) != nullptr);

    // 6. 再次拆开后, 尝试连回本体 A (通过挂回本体路由)
    stack.push(new cad::cmd::SetAttachmentAngleOnlyCommand(&doc, att.id, true));
    QVERIFY(doc.reattachShadowToMaster(att.id));
    QVERIFY2(doc.blockById(shadowId) == nullptr, "连回本体后影子销毁");
    QCOMPARE(doc.findAttachment(att.id)->toBlockId, aId);
}

// ---------------------------------------------------------------------------
// 降级门 (计划 L2-2.1): 多段块本体 → 不建影子, 拆开保持旧 angleOnly 行为
// (活引用, 旋转基准线仍带动跟随线); 清除影子 (removeShadow) → Att2 移除,
// 跟随线变纯自由线 (方向/位置冻结)。
// ---------------------------------------------------------------------------

void TestAttachmentShadow::shadowDetach_degradeAndClearShadow()
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

void TestAttachmentShadow::auxPointAttachment_detachAndReconnect()
{
    ParamDocument doc;
    // L1: 宿主线 (0,0) -> (100,0)
    auto [l1Id, l1Start, l1End, l1Seg] = makeLine(doc, 100.0);

    // 在 L1 上添加辅助点 (percent=0.5, 即 (50,0))
    ParamPoint aux;
    aux.constraint = PointConstraint::Interpolated;
    aux.hostSegmentId = l1Seg;
    aux.interpPercent = 0.5;
    aux.isAuxiliary = true;
    const QUuid auxId = aux.id;
    doc.blockById(l1Id)->addPoint(std::move(aux));
    doc.blockById(l1Id)->findSegment(l1Seg)->auxPointIds.push_back(auxId);
    doc.resolveAll();

    const Vec2 auxWorld = doc.findBlock(l1Id)->worldPos(auxId);
    QVERIFY(std::abs(auxWorld.x - 50.0) < 1e-6);
    QVERIFY(std::abs(auxWorld.y - 0.0) < 1e-6);

    // L2: 跟随线，起点附着到 L1 的辅助点上
    auto [l2Id, l2Start, l2End, l2Seg] = makeLine(doc, 50.0, Vec2{50.0, 0.0});
    Attachment att;
    att.fromBlockId = l2Id;
    att.fromPointId = l2Start;
    att.toBlockId = l1Id;
    att.toPointId = auxId;
    att.toSegmentId = l1Seg;
    att.followerAngle = 90.0;
    QVERIFY(doc.addAttachment(att));
    doc.resolveAll();

    // 验证跟随线起点吸附在辅助点上
    QVERIFY((doc.findBlock(l2Id)->worldPos(l2Start) - auxWorld).length() < 1e-6);

    // 辅助点挂载拆开：必须彻底释放连接 (RemoveAttachmentCommand)
    QUndoStack stack;
    stack.push(new cad::cmd::RemoveAttachmentCommand(&doc, att.id));
    doc.resolveAll();

    // 验证连接已在数据中彻底释放，无诊断报错
    QVERIFY(doc.findAttachment(att.id) == nullptr);
    QVERIFY(doc.diagnostics().empty());

    // 拆开后：L2 为完全自由线，可以自由连接任何其他线段 (如 L3)
    auto [l3Id, l3Start, l3End, l3Seg] = makeLine(doc, 80.0, Vec2{100.0, 100.0});
    Attachment attNew;
    attNew.fromBlockId = l2Id;
    attNew.fromPointId = l2Start;
    attNew.toBlockId = l3Id;
    attNew.toPointId = l3Start;
    attNew.toSegmentId = l3Seg;
    attNew.followerAngle = 45.0;
    QVERIFY2(doc.addAttachment(attNew), "拆开辅助点后，跟随线必须能成功连接到其他端点");
    doc.resolveAll();
    QVERIFY(doc.diagnostics().empty());

    // 移除新连接，验证撤销与重做
    doc.removeAttachment(attNew.id);
    doc.resolveAll();

    // 验证 undo: 原连接完整恢复回辅助点
    stack.undo();
    doc.resolveAll();
    const auto* restoredAtt = doc.findAttachment(att.id);
    QVERIFY(restoredAtt != nullptr);
    QCOMPARE(restoredAtt->toPointId, auxId);
    QVERIFY(doc.diagnostics().empty());
    QVERIFY((doc.findBlock(l2Id)->worldPos(l2Start) - auxWorld).length() < 1e-6);

    // 验证 redo: 再次彻底释放连接
    stack.redo();
    doc.resolveAll();
    QVERIFY(doc.findAttachment(att.id) == nullptr);
    QVERIFY(doc.diagnostics().empty());
}


QTEST_MAIN(TestAttachmentShadow)
#include "test_attachment_shadow.moc"
