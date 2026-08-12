#include "Duplicate.h"

#include <QHash>
#include <QSet>
#include <QtMath>
#include <cmath>

#include "parametric/FollowerAngle.h"
#include "parametric/ParamDocument.h"

namespace cad::param {

namespace {

/// Remap an id through the old→new table; null / unknown ids pass through
/// unchanged (unknown = reference to an entity outside the copied set).
QUuid remap(const QHash<QUuid, QUuid>& idMap, const QUuid& id)
{
    if (id.isNull()) return id;
    return idMap.value(id, id);
}

/// Remap an id that points INSIDE the copied set; references outside the set
/// are CLEARED. A clone must never depend on geometry outside the copied set:
/// an outside endTarget/follow would pull the clone toward the original block
/// (the same rule RotateCopyGesture applies by clearing them), and an outside
/// interpRefPointId would never resolve (findPoint misses the fresh UUIDs).
QUuid remapOrClear(const QHash<QUuid, QUuid>& idMap, const QUuid& id)
{
    if (id.isNull()) return id;
    return idMap.value(id, QUuid());
}


/// Linked variable (关联变量) tracking the length of an ORIGINAL segment:
/// reuse the published one, else one already queued in this result, else
/// publish a fresh one (same convention as LinePropertyDialog::onPublishLength).
QString linkedRefForSegment(ParamDocument& doc, const Block& orig,
                            const Segment& origSeg, DuplicateResult& result)
{
    if (const LinkedVariable* lv = doc.findLinkedBySource(orig.id, origSeg.id))
        return lv->refName;
    for (const auto& lv : result.newLinked)
        if (lv.sourceBlockId == orig.id && lv.sourceSegmentId == origSeg.id)
            return lv.refName;

    LinkedVariable fresh = LinkedVariable::fromSegment(orig, origSeg);
    const QString refName = fresh.refName;
    result.newLinked.push_back(std::move(fresh));
    return refName;
}

} // namespace

DuplicateResult duplicateBlocks(ParamDocument& doc, const QList<QUuid>& blockIds)
{
    DuplicateResult result;

    // ── Pass 1: deep-copy blocks, regenerate every UUID into idMap ──
    QHash<QUuid, QUuid> idMap;   // old block/point/segment id → new id
    QHash<QUuid, QUuid> origOf;  // new block id → old block id
    QSet<QUuid> inSet;
    for (const QUuid& id : blockIds) {
        const Block* orig = doc.blockById(id);
        if (!orig || inSet.contains(id)) continue;
        inSet.insert(id);

        Block clone = *orig;               // full parametric deep copy
        clone.id = QUuid::createUuid();
        idMap.insert(orig->id, clone.id);
        origOf.insert(clone.id, orig->id);
        for (auto& pt : clone.points) {
            const QUuid fresh = QUuid::createUuid();
            idMap.insert(pt.id, fresh);
            pt.id = fresh;
            pt.serial = doc.newPointSerial();
        }
        for (auto& seg : clone.segments) {
            const QUuid fresh = QUuid::createUuid();
            idMap.insert(seg.id, fresh);
            seg.id = fresh;
            seg.serial = doc.newLineSerial();
        }
        result.blocks.push_back(std::move(clone));
    }
    if (result.blocks.empty()) return result;

    // ── Pass 2: remap all internal references inside each clone ──
    for (auto& clone : result.blocks) {
        for (auto& pt : clone.points) {
            pt.refPointId    = remap(idMap, pt.refPointId);
            pt.refSegmentId  = remap(idMap, pt.refSegmentId);
            pt.refPointA     = remap(idMap, pt.refPointA);
            pt.refPointB     = remap(idMap, pt.refPointB);
            pt.hostSegmentId = remap(idMap, pt.hostSegmentId);
            pt.interAimPointId = remap(idMap, pt.interAimPointId);
            // Curve-anchor follow + interpolated measurement reference: only
            // references INSIDE the copied set survive (outside ones are
            // cleared — see remapOrClear).
            pt.followBlockId = remapOrClear(idMap, pt.followBlockId);
            pt.followPointId = pt.followBlockId.isNull()
                ? QUuid() : remapOrClear(idMap, pt.followPointId);
            pt.interpRefPointId = remapOrClear(idMap, pt.interpRefPointId);
        }
        // Endpoint-aim constraint (终点指向): a target outside the copied set
        // would drag the clone back on every resolve — clear it (same rule as
        // RotateCopyGesture); an inside target keeps the relative aim.
        clone.endTargetBlockId = remapOrClear(idMap, clone.endTargetBlockId);
        clone.endTargetPointId = clone.endTargetBlockId.isNull()
            ? QUuid() : remapOrClear(idMap, clone.endTargetPointId);
        for (auto& seg : clone.segments) {
            seg.startPointId = remap(idMap, seg.startPointId);
            seg.endPointId   = remap(idMap, seg.endPointId);
            seg.ctrlPointId  = remap(idMap, seg.ctrlPointId);
            seg.ctrlPoint2Id = remap(idMap, seg.ctrlPoint2Id);
            for (auto& auxId : seg.auxPointIds)
                auxId = remap(idMap, auxId);
        }
        clone.rebuildPointIndex();
        // Segment IDs were regenerated above — the copied index is stale even
        // though the count matches (the size-guard cannot detect this case).
        clone.rebuildSegmentIndex();
    }

    // ── Pass 3: clone attachments whose BOTH endpoints are inside the set ──
    // Cross-boundary attachments are dropped: the clone keeps its copied
    // transform, i.e. the current world pose is frozen.
    QHash<QUuid, QList<const Attachment*>> internalPinsOfBridge;  // orig bridge id → pins
    for (const auto& att : doc.attachments()) {
        const bool fromIn = inSet.contains(att.fromBlockId);
        const bool toIn   = inSet.contains(att.toBlockId);
        if (!fromIn) continue;
        if (att.isPin)   // collect for the bridge pass (cloned only if kept)
            internalPinsOfBridge[att.fromBlockId].append(toIn ? &att : nullptr);
        if (!toIn) continue;
        if (att.isPin) continue;  // pins handled by the bridge pass below
        Attachment copy = att;
        copy.id          = QUuid::createUuid();
        copy.fromBlockId = remap(idMap, copy.fromBlockId);
        copy.fromPointId = remap(idMap, copy.fromPointId);
        copy.toBlockId   = remap(idMap, copy.toBlockId);
        copy.toPointId   = remap(idMap, copy.toPointId);
        copy.toSegmentId = remap(idMap, copy.toSegmentId);
        result.attachments.push_back(std::move(copy));
    }

    // ── Pass 3.5: numeric lengths keep tracking the original (长度关联) ──
    // A formula-driven length already follows its variables; a plain numeric
    // length would go dead on the copy, so drive it with the ORIGINAL
    // segment's linked variable instead (用户拍板: 只关联长度, 角度保持数值).
    for (auto& clone : result.blocks) {
        if (clone.isBridge) continue;   // bridge copies handled in Pass 4
        const Block* orig = doc.blockById(origOf.value(clone.id));
        if (!orig) continue;
        for (const Segment& origSeg : orig->segments) {
            Segment* cloneSeg = clone.findSegment(remap(idMap, origSeg.id));
            if (!cloneSeg) continue;
            ParamPoint* pEnd = clone.findPoint(cloneSeg->endPointId);
            // Only a Polar end point measured from the segment's own start
            // is the length driver; anything else defines other geometry.
            if (!pEnd || pEnd->constraint != PointConstraint::Polar) continue;
            if (pEnd->refPointId != cloneSeg->startPointId) continue;
            if (!pEnd->distanceFormula.isEmpty()) continue;
            const QString refName =
                linkedRefForSegment(doc, *orig, origSeg, result);
            cloneSeg->lengthFormula = refName;
            pEnd->distanceFormula = refName;
        }
    }

    // ── Pass 4: bridges (桥接线) ──
    for (auto& clone : result.blocks) {
        if (!clone.isBridge) continue;

        const QUuid origId = origOf.value(clone.id);
        const Block* orig = doc.blockById(origId);
        if (!orig || orig->segments.empty()) continue;

        const auto pins = internalPinsOfBridge.value(origId);
        int keptPins = 0;
        for (const Attachment* p : pins)
            if (p) ++keptPins;

        if (keptPins == 2) {
            // Both hosts copied along → the clone stays a full bridge.
            for (const Attachment* p : pins) {
                Attachment copy = *p;
                copy.id          = QUuid::createUuid();
                copy.fromBlockId = remap(idMap, copy.fromBlockId);
                copy.fromPointId = remap(idMap, copy.fromPointId);
                copy.toBlockId   = remap(idMap, copy.toBlockId);
                copy.toPointId   = remap(idMap, copy.toPointId);
                copy.toSegmentId = remap(idMap, copy.toSegmentId);
                result.attachments.push_back(std::move(copy));
            }
            continue;
        }

        // At least one pin host stays outside → release the COPY (原件不动):
        // freeze the stretched geometry into an independent segment.
        clone.isBridge = false;
        if (!clone.freezeSegmentGeometry()) continue;

        // Length keeps tracking the ORIGINAL bridge via its linked variable
        // (用户拍板: 桥接线副本填入关联变量).
        const Segment& origSeg = orig->segments.front();
        const QString refName =
            linkedRefForSegment(doc, *orig, origSeg, result);
        Segment& cloneSeg = clone.segments.front();
        cloneSeg.lengthFormula = refName;
        if (auto* pEnd = clone.findPoint(cloneSeg.endPointId))
            pEnd->distanceFormula = refName;   // the actual length driver

        // A surviving inside pin becomes a normal follower attachment whose
        // follower angle preserves the frozen direction (same back-solve
        // as ParamDocument::releaseBridge).
        for (const Attachment* p : pins) {
            if (!p) continue;
            const Block* leader = doc.blockById(p->toBlockId);
            if (!leader) continue;
            Attachment copy = *p;
            copy.id          = QUuid::createUuid();
            copy.isPin       = false;
            copy.fromBlockId = remap(idMap, copy.fromBlockId);
            copy.fromPointId = remap(idMap, copy.fromPointId);
            copy.toBlockId   = remap(idMap, copy.toBlockId);
            copy.toPointId   = remap(idMap, copy.toPointId);
            const QUuid origToSeg = leader->exitSegmentAtPoint(p->toPointId);
            copy.toSegmentId = remap(idMap, origToSeg);
            const double refWorld = leader->transform.rotation
                + leader->exitDirectionAtPoint(p->toPointId, origToSeg);
            const double localDir = clone.directionAtPoint(copy.fromPointId);
            copy.followerAngle = backSolveFollowerAngle(
                clone.transform.rotation, localDir, refWorld);
            copy.followerAngleFormula.clear();
            copy.rotationMode = RotationMode::Angle;
            copy.arcLength = 0.0;
            copy.arcLengthFormula.clear();
            result.attachments.push_back(std::move(copy));
        }
    }

    // ── Pass 5: user group clone (副本成新组) ──
    // When the source set IS exactly one complete user group, the clones
    // form a fresh group (fresh id + serial; internal connections were
    // already remapped in Pass 3/4).
    if (!blockIds.isEmpty()) {
        const QUuid srcGroupId = doc.groupOfBlock(blockIds.first());
        bool exactGroup = !srcGroupId.isNull();
        if (exactGroup) {
            const QList<QUuid> members = doc.blocksInGroup(srcGroupId);
            const QSet<QUuid> memberSet(members.begin(), members.end());
            exactGroup = (memberSet == inSet);   // 整组复制才成组
        }
        if (exactGroup) {
            Group g;
            g.serial = doc.newGroupSerial();
            result.newGroup = std::move(g);
            for (const QUuid& origId : inSet)
                result.newGroupMembers.push_back(idMap.value(origId));
        }
    }

    return result;
}

} // namespace cad::param
