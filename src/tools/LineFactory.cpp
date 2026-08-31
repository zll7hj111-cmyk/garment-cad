#include "LineFactory.h"





#include <cmath>


#include <optional>





#include <QUndoStack>





#include "parametric/ParamDocument.h"


#include "parametric/Block.h"


#include "parametric/MeasureVariable.h"


#include "parametric/Serial.h"


#include "geometry/Angle.h"


#include "canvas/CanvasScene.h"


#include "document/commands/BlockCommands.h"


#include "document/commands/DocumentCommands.h"


#include "ui/LayerFeedback.h"





namespace cad::tools {





namespace {





/// True when the attachment with @p attId is actually present in the document


/// (commands may reject the edge — only toast for genuinely established ones).


bool attachmentEstablished(const cad::param::ParamDocument* doc, const QUuid& attId)


{


    if (!doc || attId.isNull()) return false;


    for (const auto& a : doc->attachments())


        if (a.id == attId) return true;


    return false;


}





} // namespace





LineFactory::LineFactory(cad::param::ParamDocument* doc, QUndoStack* undoStack,


                         CanvasScene* scene)


    : m_paramDoc(doc)


    , m_undoStack(undoStack)


    , m_scene(scene)


{


}





void LineFactory::createFreeLine(const Vec2& start, const Vec2& end,


                                 const LineBuildOptions& opts)


{


    if (!m_paramDoc) return;





    cad::param::Block block;


    block.layer = m_paramDoc->layersView().activeLayer();
    block.lengthAuto = false;  // 自由线: 长度指定


    // Segment names default to empty; the pre-input strip may provide one.





    // Block origin at start point (local (0,0) = start)


    block.transform.origin = start;


    block.transform.rotation = 0.0;





    // Start point: Free at local (0,0)


    cad::param::ParamPoint ptStart;


    ptStart.constraint = cad::param::PointConstraint::Free;


    ptStart.freePos = cad::geo::Vec2::zero();


    QUuid startId = ptStart.id;





    // End point: Polar relative to start


    const Vec2 delta = end - start;


    const double dist = opts.hasLength ? opts.lengthMm : delta.length();


    // 自由起点：预输入角度 = 绝对世界角；无预输入则从几何反解。


    const double angleDeg = opts.hasAngle


        ? opts.displayAngleDeg


        : std::atan2(delta.y, delta.x) * 180.0 / M_PI;





    cad::param::ParamPoint ptEnd;


    ptEnd.constraint = cad::param::PointConstraint::Polar;


    ptEnd.refPointId = startId;


    ptEnd.distance = dist;


    ptEnd.distanceFormula = opts.lengthFormula;


    ptEnd.angle = angleDeg;


    ptEnd.angleFormula = opts.angleFormula;


    QUuid endId = ptEnd.id;





    block.addPoint(std::move(ptStart));


    block.addPoint(std::move(ptEnd));





    // Segment connecting them


    cad::param::Segment seg;


    seg.name = opts.name;


    seg.startPointId = startId;


    seg.endPointId = endId;


    seg.lengthFormula = opts.lengthFormula;


    block.addSegment(std::move(seg));





    if (m_undoStack) {


        cad::param::Attachment dummy;


        m_undoStack->push(new cad::cmd::DrawLineCommand(


            m_paramDoc, std::move(block), dummy, false));


    } else {


        m_paramDoc->addBlock(std::move(block));


    }


}





void LineFactory::createAttachedLine(const SnapResult& snapStart, const Vec2& end,


                                     int leaderIndex,


                                     const std::vector<LeaderCandidate>& candidates,


                                     const LineBuildOptions& opts)


{


    if (!m_paramDoc) return;





    // Attachment target: the user-selected leader candidate (click/W during


    // rubber band) wins; without candidates fall back to the raw snap result


    // + auto-picked exit segment. Resolved first because the block origin must


    // sit exactly on the point we actually attach to (coincident points from


    // different blocks may differ by sub-pixel amounts).


    QUuid toBlockId   = snapStart.blockId;


    QUuid toPointId   = snapStart.pointId;


    QUuid toSegmentId;


    if (leaderIndex >= 0 && leaderIndex < static_cast<int>(candidates.size())) {


        const LeaderCandidate& cand = candidates[static_cast<size_t>(leaderIndex)];


        toBlockId   = cand.blockId;


        toPointId   = cand.pointId;


        toSegmentId = cand.segmentId;


    }





    Vec2 startWorld = snapStart.worldPos;


    double refWorldRad = 0.0;


    if (const auto* targetBlock = m_paramDoc->findBlock(toBlockId)) {


        startWorld = targetBlock->worldPos(toPointId);


        if (toSegmentId.isNull())


            toSegmentId = targetBlock->exitSegmentAtPoint(toPointId);


        // Reference world direction = target block rotation + local exit


        // direction. exitDirectionAtPoint handles snapping to either endpoint


        // of the leader segment, orienting the reference so 0° always means


        // "keep going straight". The leader segment is recorded explicitly


        // (toSegmentId) so the reference never silently switches when the


        // point gains more segments.


        refWorldRad = targetBlock->transform.rotation


                    + targetBlock->exitDirectionAtPoint(toPointId, toSegmentId);


    }





    cad::param::Block block;


    block.layer = m_paramDoc->layersView().activeLayer();
    block.lengthAuto = false;  // 起点吸附: 长度指定


    // Segment names default to empty; the pre-input strip may provide one.





    // Block origin at the attached point


    block.transform.origin = startWorld;


    block.transform.rotation = 0.0;





    // Start point: Free at local (0,0) — coincides with snapped point


    cad::param::ParamPoint ptStart;


    ptStart.constraint = cad::param::PointConstraint::Free;


    ptStart.freePos = cad::geo::Vec2::zero();


    QUuid startId = ptStart.id;





    // End point: Polar relative to start


    const Vec2 delta = end - startWorld;


    const double dist = opts.hasLength ? opts.lengthMm : delta.length();


    const double angleDeg = std::atan2(delta.y, delta.x) * 180.0 / M_PI;





    cad::param::ParamPoint ptEnd;


    ptEnd.constraint = cad::param::PointConstraint::Polar;


    ptEnd.refPointId = startId;


    ptEnd.distance = dist;


    ptEnd.distanceFormula = opts.lengthFormula;


    ptEnd.angle = angleDeg;


    QUuid endId = ptEnd.id;





    block.addPoint(std::move(ptStart));


    block.addPoint(std::move(ptEnd));





    cad::param::Segment seg;


    seg.name = opts.name;


    seg.startPointId = startId;


    seg.endPointId = endId;


    seg.lengthFormula = opts.lengthFormula;


    block.addSegment(std::move(seg));





    const QUuid newBlockId = block.id;





    cad::param::Attachment att;


    att.fromBlockId = newBlockId;


    att.fromPointId = startId;


    att.toBlockId   = toBlockId;


    att.toPointId   = toPointId;


    att.toSegmentId = toSegmentId;





    // Follower angle = 180° − (new line's world angle − leader segment world


    // direction)（闭合基准, 用户拍板 2026-08 定稿：angle 0° = 折叠重叠，


    // 180° = 延伸展开）, 归一化 [0, 360°)。预输入角度直接就是该显示基准。


    double followerAngle;


    if (opts.hasAngle) {


        followerAngle = cad::geo::normalizeDeg360(opts.displayAngleDeg);


    } else {


        followerAngle = cad::geo::normalizeDeg360(180.0 - (angleDeg - refWorldRad * 180.0 / M_PI));


    }


    att.followerAngle = followerAngle;


    att.followerAngleFormula = opts.angleFormula;





    // Cross-layer toast bookkeeping (captured BEFORE block is moved away).


    const QUuid fromLayer = block.layer;


    const QUuid toLayer = [&] {


        const auto* leaderBlk = m_paramDoc->findBlock(toBlockId);


        return leaderBlk ? leaderBlk->layer : QUuid();


    }();


    const QUuid attId = att.id;





    if (m_undoStack) {


        m_undoStack->push(new cad::cmd::DrawLineCommand(


            m_paramDoc, std::move(block), att, true));


    } else {


        m_paramDoc->addBlock(std::move(block));


        m_paramDoc->addAttachment(std::move(att));


    }





    // Toast at the creation site (closest to the user gesture); the id check


    // guards against a rejected edge, and undo/redo replays never re-toast.


    if (m_scene && attachmentEstablished(m_paramDoc, attId)) {


        if (const QString toast = cad::ui::crossLayerToast(m_paramDoc, fromLayer, toLayer);


            !toast.isEmpty())


            m_scene->showToast(toast);


    }


}





void LineFactory::createBridgeLine(const SnapResult& snapStart,


                                   const SnapResult& snapEnd,


                                   int leaderIndex,


                                   const std::vector<LeaderCandidate>& candidates,


                                   const LineBuildOptions& opts)


{


    if (!m_paramDoc) return;





    // Resolve the actual host points (leader candidate wins for the start).


    QUuid startBlockId = snapStart.blockId;


    QUuid startPointId = snapStart.pointId;


    if (leaderIndex >= 0 && leaderIndex < static_cast<int>(candidates.size())) {


        const LeaderCandidate& cand = candidates[static_cast<size_t>(leaderIndex)];


        startBlockId = cand.blockId;


        startPointId = cand.pointId;


    }





    Vec2 startWorld = snapStart.worldPos;


    if (const auto* hb = m_paramDoc->findBlock(startBlockId))


        startWorld = hb->worldPos(startPointId);


    Vec2 endWorld = snapEnd.worldPos;


    if (const auto* hb = m_paramDoc->findBlock(snapEnd.blockId))


        endWorld = hb->worldPos(snapEnd.pointId);





    if (startWorld.distanceSquaredTo(endWorld) < 1e-10)


        return;  // Both points on the same spot — nothing to draw.





    // --- Measure variable: |P1 - P2| published as a formula parameter ---


    cad::param::MeasureVariable mv;


    mv.blockA = startBlockId;


    mv.pointA = startPointId;


    mv.blockB = snapEnd.blockId;


    mv.pointB = snapEnd.pointId;


    mv.value = startWorld.distanceTo(endWorld);


    mv.name = opts.name;  // 桥接线测量卡跟随线段名（与属性面板/编辑条一致）。


    // Reference names are uppercase by convention (CopyChip force-uppercases


    // them for display/editing); generate uppercase so the stored refName


    // matches what the user sees and types back into formula fields.


    mv.refName = QStringLiteral("M_") + cad::param::Serial::randomPrefix().toUpper();





    // --- Free line (new bridge model): length = measure var, angle = world ---


    const Vec2 delta = endWorld - startWorld;


    const double lenMm = delta.length();


    const double worldAngleDeg = std::atan2(delta.y, delta.x) * 180.0 / M_PI;





    cad::param::Block block;


    block.layer = m_paramDoc->layersView().activeLayer();
    block.lengthAuto = true;  // 桥接线: 长度自动


    block.transform.origin = startWorld;


    block.transform.rotation = worldAngleDeg * M_PI / 180.0;


    // The measurement belongs to this bridge line: deleting the line deletes


    // the variable, and clicking the card highlights the line (not the hosts).


    mv.ownerBlockId = block.id;





    cad::param::ParamPoint ptStart;


    ptStart.constraint = cad::param::PointConstraint::Free;


    ptStart.freePos = cad::geo::Vec2::zero();


    QUuid startId = ptStart.id;





    // End point: Polar along local X (block rotation carries the world angle).


    // Length is driven by the measure variable formula.


    cad::param::ParamPoint ptEnd;


    ptEnd.constraint = cad::param::PointConstraint::Polar;


    ptEnd.refPointId = startId;


    ptEnd.distance = lenMm;


    ptEnd.distanceFormula = mv.refName;  // length = M_xxx


    ptEnd.angle = 0.0;


    QUuid endId = ptEnd.id;





    block.addPoint(std::move(ptStart));


    block.addPoint(std::move(ptEnd));





    cad::param::Segment seg;


    seg.name = opts.name;


    seg.startPointId = startId;


    seg.endPointId = endId;


    seg.lengthFormula = mv.refName;


    block.addSegment(std::move(seg));





    // --- Default follow (构造线默认跟随): start follows host A, end aims at


    // host B. Both can be released later via the property dialog.


    block.endTargetBlockId = snapEnd.blockId;


    block.endTargetPointId = snapEnd.pointId;


    block.endTargetOffset = 0.0;





    std::optional<cad::param::Attachment> followAtt;


    if (const auto* leader = m_paramDoc->findBlock(startBlockId)) {


        cad::param::Attachment att;


        att.fromBlockId = block.id;


        att.fromPointId = startId;


        att.toBlockId   = startBlockId;


        att.toPointId   = startPointId;


        att.toSegmentId = leader->exitSegmentAtPoint(startPointId);


        // Back-solve the follower angle so the initial world direction is


        // preserved（闭合基准: rotation = refWorld + π − angle·π/180）:


        // angle = 180° − (world angle − leader world direction)。


        const double refWorldRad = leader->transform.rotation


            + leader->exitDirectionAtPoint(startPointId, att.toSegmentId);


        att.followerAngle = cad::geo::normalizeDeg180(180.0


            - (worldAngleDeg - refWorldRad * 180.0 / M_PI));


        followAtt = std::move(att);


    }





    // Cross-layer toast bookkeeping (captured BEFORE block is moved away).


    const QUuid fromLayer = block.layer;


    const QUuid toLayer = followAtt


        ? ([&] { const auto* l = m_paramDoc->findBlock(followAtt->toBlockId);


                 return l ? l->layer : QUuid(); })()


        : QUuid();


    const QUuid attId = followAtt ? followAtt->id : QUuid();





    if (m_undoStack) {


        m_undoStack->push(new cad::cmd::DrawMeasureLineCommand(


            m_paramDoc, std::move(block), std::move(mv), std::move(followAtt)));


    } else {


        m_paramDoc->addMeasure(std::move(mv));


        m_paramDoc->addBlock(std::move(block));


        if (followAtt)


            m_paramDoc->addAttachment(std::move(*followAtt));


    }





    // Toast at the creation site; the id check covers the command rejecting


    // the follow attachment (DrawMeasureLineCommand::m_attAdded guard).


    if (m_scene && attachmentEstablished(m_paramDoc, attId)) {


        if (const QString toast = cad::ui::crossLayerToast(m_paramDoc, fromLayer, toLayer);


            !toast.isEmpty())


            m_scene->showToast(toast);


    }


}





void LineFactory::createDartLine(const SnapResult& startA, const SnapResult& refB,


                                 double offsetMm, double angleDeg,


                                 const LineBuildOptions& opts,


                                 const QString& offsetFormula,


                                 const QString& angleFormula)


{


    if (!m_paramDoc) return;





    const auto* aBlock = m_paramDoc->findBlock(startA.blockId);


    const auto* bBlock = m_paramDoc->findBlock(refB.blockId);


    if (!aBlock || !bBlock) return;


    const cad::param::ParamPoint* aPt = aBlock->findPoint(startA.pointId);


    const cad::param::ParamPoint* bPt = bBlock->findPoint(refB.pointId);


    if (!aPt || !bPt) return;





    const geo::Vec2 aWorld = aBlock->worldPos(startA.pointId);


    const geo::Vec2 bWorld = bBlock->worldPos(refB.pointId);





    // Direction basis = the reference segment's exit direction at B (extend


    // straight past the point) — the angle reference is ALWAYS B's segment.


    QUuid refSegmentId = bBlock->exitSegmentAtPoint(refB.pointId);


    const double thetaB = bBlock->transform.rotation


        + bBlock->exitDirectionAtPoint(refB.pointId, refSegmentId);





    const double betaRad = angleDeg * M_PI / 180.0;


    const geo::Vec2 eWorld = bWorld


        + geo::Vec2(std::cos(thetaB + betaRad), std::sin(thetaB + betaRad)) * offsetMm;





    cad::param::Block block;


    block.layer = m_paramDoc->layersView().activeLayer();





    // Placed by the Resolver every pass; born already placed.


    block.transform.origin = aWorld;


    const geo::Vec2 delta = eWorld - aWorld;


    block.transform.rotation = std::atan2(delta.y, delta.x);


    const double lenMm = delta.length();





    cad::param::ParamPoint ptStart;


    ptStart.constraint = cad::param::PointConstraint::Free;


    ptStart.freePos = geo::Vec2::zero();


    QUuid startId = ptStart.id;





    // End point: Polar along local X — the block rotation carries the A→E


    // direction and the Resolver writes back the computed |A−E| distance.


    cad::param::ParamPoint ptEnd;


    ptEnd.constraint = cad::param::PointConstraint::Polar;


    ptEnd.refPointId = startId;


    ptEnd.distance = lenMm;


    ptEnd.angle = 0.0;


    QUuid endId = ptEnd.id;





    block.addPoint(std::move(ptStart));


    block.addPoint(std::move(ptEnd));





    cad::param::Segment seg;


    seg.name = opts.name;


    seg.startPointId = startId;


    seg.endPointId = endId;


    block.addSegment(std::move(seg));





    // The dart constraint (references + parameters). Start A must be attached


    // (enforced at the tool layer); the Resolver recomputes origin/rotation/


    // length from these fields on every pass.


    block.dartStartBlockId = startA.blockId;


    block.dartStartPointId = startA.pointId;


    block.dartRefBlockId   = refB.blockId;


    block.dartRefPointId   = refB.pointId;


    block.dartRefSegmentId = refSegmentId;


    block.dartOffsetMm     = offsetMm;


    block.dartOffsetFormula = offsetFormula;


    block.dartAngleDeg     = angleDeg;


    block.dartAngleFormula = angleFormula;





    if (m_undoStack) {


        cad::param::Attachment dummy;


        m_undoStack->push(new cad::cmd::DrawLineCommand(


            m_paramDoc, std::move(block), dummy, false));


    } else {


        m_paramDoc->addBlock(std::move(block));


    }


}





} // namespace cad::tools


