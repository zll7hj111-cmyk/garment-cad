#include "parametric/MeasurementStore.h"

#include <algorithm>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "parametric/PerfProbe.h"

namespace cad::param {

MeasurementStore::MeasurementStore(ParamDocument* doc, QObject* parent)
    : QObject(parent)
    , m_doc(doc)
{
}

// --- Linked variables ---

void MeasurementStore::addLinked(LinkedVariable lv)
{
    m_linkedVars.push_back(std::move(lv));
    emit linkedVarsChanged();
    m_doc->resolveAll();
}

void MeasurementStore::addLinkedRaw(LinkedVariable lv)
{
    m_linkedVars.push_back(std::move(lv));
}

void MeasurementStore::removeLinked(const QUuid& id)
{
    auto it = std::find_if(m_linkedVars.begin(), m_linkedVars.end(),
        [&id](const LinkedVariable& lv) { return lv.id == id; });
    if (it != m_linkedVars.end()) {
        // Remove the published parameter key.
        if (!it->refName.isEmpty())
            m_doc->removeParameterEntry(it->refName);
        m_linkedVars.erase(it);
        emit linkedVarsChanged();
        m_doc->resolveAll();
    }
}

void MeasurementStore::updateLinked(const LinkedVariable& lv)
{
    for (auto& existing : m_linkedVars) {
        if (existing.id == lv.id) {
            // Only name/comment are user-editable; source and value are not.
            const QString oldRef = existing.refName;
            existing.name = lv.name;
            existing.comment = lv.comment;
            // refName change: retire old key; new one published by
            // measureLinkedVars() inside the resolveAll() below.
            if (existing.refName != lv.refName && !lv.refName.isEmpty()) {
                m_doc->removeParameterEntry(oldRef);
                existing.refName = lv.refName;
            }
            break;
        }
    }
    emit linkedVarsChanged();
    m_doc->resolveAll();
}

LinkedVariable* MeasurementStore::findLinked(const QUuid& id)
{
    auto it = std::find_if(m_linkedVars.begin(), m_linkedVars.end(),
        [&id](const LinkedVariable& lv) { return lv.id == id; });
    return (it != m_linkedVars.end()) ? &(*it) : nullptr;
}

LinkedVariable* MeasurementStore::findLinkedBySource(const QUuid& blockId,
                                                     const QUuid& segmentId)
{
    for (auto& lv : m_linkedVars) {
        if (lv.sourceBlockId == blockId && lv.sourceSegmentId == segmentId)
            return &lv;
    }
    return nullptr;
}

QList<QUuid> MeasurementStore::linkedConsumerBlocks(const QUuid& sourceBlockId) const
{
    QList<QUuid> result;
    const auto& blocks = m_doc->blocks();
    for (const auto& lv : m_linkedVars) {
        if (lv.sourceBlockId != sourceBlockId || lv.refName.isEmpty()) continue;
        for (const auto& b : blocks) {
            if (result.contains(b.id)) continue;
            bool consumes = false;
            for (const auto& s : b.segments)
                if (s.lengthFormula == lv.refName ||
                    s.extendStartFormula == lv.refName ||
                    s.extendEndFormula == lv.refName) { consumes = true; break; }
            if (!consumes)
                for (const auto& p : b.points)
                    if (p.distanceFormula == lv.refName) { consumes = true; break; }
            if (consumes) result.push_back(b.id);
        }
    }
    return result;
}

bool MeasurementStore::measureLinkedVars(bool skipAuxSource)
{
    GCAD_PERF_SCOPE("meas.linked");
    if (m_linkedVars.empty()) return false;

    // Purge variables whose measurement target no longer exists (block or
    // segment deleted). Without a target the variable is destroyed
    // immediately (没有测量对象时立刻销毁).
    bool purged = false;
    auto pit = std::remove_if(m_linkedVars.begin(), m_linkedVars.end(),
        [&](const LinkedVariable& lv) {
            const Block* blk = m_doc->blockById(lv.sourceBlockId);
            if (blk && blk->findSegment(lv.sourceSegmentId)) return false;
            if (!lv.refName.isEmpty())
                m_doc->removeParameterEntry(lv.refName);
            purged = true;
            return true;
        });
    m_linkedVars.erase(pit, m_linkedVars.end());
    if (purged)
        emit linkedVarsChanged();

    bool dirty = false;
    // Cross-layer correction: aux blocks linked to the working layers via a
    // cross-layer attachment move in Phase 3 — their measurements are NOT
    // cacheable even though both endpoints sit on the aux layer. Empty set
    // (a single integer test) when the document has no cross-layer edges.
    const auto mobileAux =
        (skipAuxSource && m_doc->hasCrossLayerAttachments())
            ? m_doc->collectMobileAuxBlocks() : QSet<QUuid>();
    for (auto& lv : m_linkedVars) {
        const Block* blk = m_doc->blockById(lv.sourceBlockId);
        if (!blk) { lv.dangling = true; continue; }
        // Layered cache: the aux layer was not re-resolved, so measurements
        // sourced from it cannot have changed — keep the cached value.
        if (skipAuxSource && m_doc->isAuxBlock(*blk) && !mobileAux.contains(lv.sourceBlockId))
            continue;
        const Segment* seg = blk->findSegment(lv.sourceSegmentId);
        if (!seg) { lv.dangling = true; continue; }

        const ParamPoint* sp = blk->findPoint(seg->startPointId);
        const ParamPoint* ep = blk->findPoint(seg->endPointId);
        if (!sp || !ep || !sp->resolved || !ep->resolved) continue;

        // Block is a rigid body (scale = 1), so local distance == world distance.
        const double len = sp->resolvedPos.distanceTo(ep->resolvedPos);
        lv.dangling = false;
        if (std::abs(len - lv.value) > 1e-9) {
            lv.value = len;
            dirty = true;
        }
        // Publish into the parameter map (cm domain) for formula consumption.
        // Only the reference name is usable in formulas.
        if (!lv.refName.isEmpty())
            m_doc->publishParameter(lv.refName, geo::Units::mmToCm(len));
    }
    if (dirty)
        emit linkedVarsChanged();
    return dirty;
}

// --- Measure variables ---

void MeasurementStore::addMeasure(MeasureVariable mv)
{
    m_measureVars.push_back(std::move(mv));
    emit measureVarsChanged();
    m_doc->resolveAll();
}

void MeasurementStore::addMeasureRaw(MeasureVariable mv)
{
    m_measureVars.push_back(std::move(mv));
}

void MeasurementStore::removeMeasure(const QUuid& id)
{
    auto it = std::find_if(m_measureVars.begin(), m_measureVars.end(),
        [&id](const MeasureVariable& mv) { return mv.id == id; });
    if (it == m_measureVars.end()) return;

    if (!it->refName.isEmpty())
        m_doc->removeParameterEntry(it->refName);
    m_measureVars.erase(it);
    emit measureVarsChanged();
    m_doc->resolveAll();
}

void MeasurementStore::updateMeasure(const MeasureVariable& mv)
{
    for (auto& existing : m_measureVars) {
        if (existing.id == mv.id) {
            const QString oldRef = existing.refName;
            existing.name = mv.name;
            existing.comment = mv.comment;
            if (existing.refName != mv.refName && !mv.refName.isEmpty()) {
                m_doc->removeParameterEntry(oldRef);
                existing.refName = mv.refName;
            }
            // Keep the owned measure line's segment name in sync (测量变量名称
            // → 测量对象名称). Bump geometryEpoch so the canvas rebuilds the
            // cached name label on the next resolve (a rename moves no geometry,
            // so the resolve pass would not invalidate the cache by itself).
            if (!existing.ownerBlockId.isNull()) {
                if (Block* owner = m_doc->blockById(existing.ownerBlockId)) {
                    bool renamed = false;
                    for (auto& s : owner->segments) {
                        if (s.name != existing.name) {
                            s.name = existing.name;
                            renamed = true;
                        }
                    }
                    if (renamed)
                        ++owner->geometryEpoch;
                }
            }
            break;
        }
    }
    emit measureVarsChanged();
    m_doc->resolveAll();
}

void MeasurementStore::setOwnerMeasureName(const QUuid& ownerBlockId, const QString& name)
{
    if (ownerBlockId.isNull()) return;
    for (auto& mv : m_measureVars) {
        if (mv.ownerBlockId == ownerBlockId && mv.name != name) {
            mv.name = name;
            emit measureVarsChanged();
            return;
        }
    }
}

MeasureVariable* MeasurementStore::findMeasure(const QUuid& id)
{
    for (auto& mv : m_measureVars)
        if (mv.id == id) return &mv;
    return nullptr;
}

MeasureVariable* MeasurementStore::findMeasureByOwner(const QUuid& ownerBlockId)
{
    if (ownerBlockId.isNull()) return nullptr;
    for (auto& mv : m_measureVars)
        if (mv.ownerBlockId == ownerBlockId) return &mv;
    return nullptr;
}

const MeasureVariable* MeasurementStore::findMeasureByOwner(const QUuid& ownerBlockId) const
{
    if (ownerBlockId.isNull()) return nullptr;
    for (const auto& mv : m_measureVars)
        if (mv.ownerBlockId == ownerBlockId) return &mv;
    return nullptr;
}

bool MeasurementStore::measureMeasureVars(bool skipAuxSource)
{
    GCAD_PERF_SCOPE("meas.measure");
    if (m_measureVars.empty()) return false;

    // Purge variables whose measurement target no longer exists (block or
    // point deleted) — destroyed immediately (没有测量对象时立刻销毁).
    bool purged = false;
    auto pit = std::remove_if(m_measureVars.begin(), m_measureVars.end(),
        [&](const MeasureVariable& mv) {
            const Block* blkA = m_doc->blockById(mv.blockA);
            const Block* blkB = m_doc->blockById(mv.blockB);
            if (blkA && blkB &&
                blkA->findPoint(mv.pointA) && blkB->findPoint(mv.pointB))
                return false;
            if (!mv.refName.isEmpty())
                m_doc->removeParameterEntry(mv.refName);
            purged = true;
            return true;
        });
    m_measureVars.erase(pit, m_measureVars.end());
    if (purged)
        emit measureVarsChanged();

    bool dirty = false;
    // Cross-layer correction (see measureLinkedVars): mobile aux blocks track
    // the working layers via Phase 3, so their measurements cannot be cached.
    const auto mobileAux =
        (skipAuxSource && m_doc->hasCrossLayerAttachments())
            ? m_doc->collectMobileAuxBlocks() : QSet<QUuid>();
    for (auto& mv : m_measureVars) {
        const Block* blkA = m_doc->blockById(mv.blockA);
        const Block* blkB = m_doc->blockById(mv.blockB);
        if (!blkA || !blkB) { mv.dangling = true; continue; }
        // Layered cache: both endpoints on the (clean) aux layer → the value
        // cannot have changed — keep the cached measurement.
        if (skipAuxSource && m_doc->isAuxBlock(*blkA) && m_doc->isAuxBlock(*blkB)
            && !mobileAux.contains(mv.blockA) && !mobileAux.contains(mv.blockB))
            continue;

        const ParamPoint* pa = blkA->findPoint(mv.pointA);
        const ParamPoint* pb = blkB->findPoint(mv.pointB);
        if (!pa || !pb || !pa->resolved || !pb->resolved) continue;

        // World-space span (points may be on different blocks). The measured
        // quantity follows the variable's kind: Euclidean distance, or the
        // horizontal/vertical projection of it.
        const geo::Vec2 wa = blkA->transform.toWorld(pa->resolvedPos);
        const geo::Vec2 wb = blkB->transform.toWorld(pb->resolvedPos);
        double dist = wa.distanceTo(wb);
        switch (mv.kind) {
            case MeasureKind::Horizontal: dist = std::abs(wb.x - wa.x); break;
            case MeasureKind::Vertical:   dist = std::abs(wb.y - wa.y); break;
            case MeasureKind::Distance:   break;
        }

        mv.dangling = false;
        if (std::abs(dist - mv.value) > 1e-9) {
            mv.value = dist;
            dirty = true;
        }
        if (!mv.refName.isEmpty())
            m_doc->publishParameter(mv.refName, geo::Units::mmToCm(dist));
    }
    if (dirty)
        emit measureVarsChanged();
    return dirty;
}

// --- Angle measure variables ---

void MeasurementStore::addAngleMeasure(AngleMeasureVariable am)
{
    m_angleMeasures.push_back(std::move(am));
    emit angleMeasureVarsChanged();
    m_doc->resolveAll();
}

void MeasurementStore::addAngleMeasureRaw(AngleMeasureVariable am)
{
    m_angleMeasures.push_back(std::move(am));
}

void MeasurementStore::removeAngleMeasure(const QUuid& id)
{
    auto it = std::find_if(m_angleMeasures.begin(), m_angleMeasures.end(),
        [&id](const AngleMeasureVariable& am) { return am.id == id; });
    if (it == m_angleMeasures.end()) return;

    if (!it->refName.isEmpty())
        m_doc->removeParameterEntry(it->refName);
    m_angleMeasures.erase(it);
    emit angleMeasureVarsChanged();
    m_doc->resolveAll();
}

void MeasurementStore::updateAngleMeasure(const AngleMeasureVariable& am)
{
    for (auto& existing : m_angleMeasures) {
        if (existing.id == am.id) {
            existing.name = am.name;
            existing.comment = am.comment;
            break;
        }
    }
    emit angleMeasureVarsChanged();
    m_doc->resolveAll();
}

AngleMeasureVariable* MeasurementStore::findAngleMeasure(const QUuid& id)
{
    for (auto& am : m_angleMeasures)
        if (am.id == id) return &am;
    return nullptr;
}

bool MeasurementStore::measureAngleMeasureVars(bool skipAuxSource)
{
    GCAD_PERF_SCOPE("meas.angle");
    if (m_angleMeasures.empty()) return false;

    // Purge variables whose measurement target no longer exists (block or
    // segment deleted) — destroyed immediately (没有测量对象时立刻销毁).
    bool purged = false;
    auto pit = std::remove_if(m_angleMeasures.begin(), m_angleMeasures.end(),
        [&](const AngleMeasureVariable& am) {
            const Block* blkA = m_doc->blockById(am.blockA);
            const Block* blkB = m_doc->blockById(am.blockB);
            if (blkA && blkB &&
                blkA->findSegment(am.segmentA) && blkB->findSegment(am.segmentB))
                return false;
            if (!am.refName.isEmpty())
                m_doc->removeParameterEntry(am.refName);
            purged = true;
            return true;
        });
    m_angleMeasures.erase(pit, m_angleMeasures.end());
    if (purged)
        emit angleMeasureVarsChanged();

    bool dirty = false;
    // Cross-layer correction (see measureLinkedVars): mobile aux blocks track
    // the working layers via Phase 3, so their measurements cannot be cached.
    const auto mobileAux =
        (skipAuxSource && m_doc->hasCrossLayerAttachments())
            ? m_doc->collectMobileAuxBlocks() : QSet<QUuid>();
    for (auto& am : m_angleMeasures) {
        const Block* blkA = m_doc->blockById(am.blockA);
        const Block* blkB = m_doc->blockById(am.blockB);
        if (!blkA || !blkB) { am.dangling = true; continue; }
        // Layered cache: both segments on the (clean) aux layer → the value
        // cannot have changed — keep the cached measurement.
        if (skipAuxSource && m_doc->isAuxBlock(*blkA) && m_doc->isAuxBlock(*blkB)
            && !mobileAux.contains(am.blockA) && !mobileAux.contains(am.blockB))
            continue;

        const Segment* sa = blkA->findSegment(am.segmentA);
        const Segment* sb = blkB->findSegment(am.segmentB);
        if (!sa || !sb) { am.dangling = true; continue; }

        const ParamPoint* a0 = blkA->findPoint(sa->startPointId);
        const ParamPoint* a1 = blkA->findPoint(sa->endPointId);
        const ParamPoint* b0 = blkB->findPoint(sb->startPointId);
        const ParamPoint* b1 = blkB->findPoint(sb->endPointId);
        if (!a0 || !a1 || !b0 || !b1 ||
            !a0->resolved || !a1->resolved || !b0->resolved || !b1->resolved)
            continue;

        // World-space chord direction (start→end) of each segment.
        const geo::Vec2 wa0 = blkA->transform.toWorld(a0->resolvedPos);
        const geo::Vec2 wa1 = blkA->transform.toWorld(a1->resolvedPos);
        const geo::Vec2 wb0 = blkB->transform.toWorld(b0->resolvedPos);
        const geo::Vec2 wb1 = blkB->transform.toWorld(b1->resolvedPos);
        const double dax = wa1.x - wa0.x, day = wa1.y - wa0.y;
        const double dbx = wb1.x - wb0.x, dby = wb1.y - wb0.y;
        if (dax * dax + day * day < 1e-12 || dbx * dbx + dby * dby < 1e-12)
            continue;  // degenerate (zero-length) segment
        const double dirA = std::atan2(day, dax);
        const double dirB = std::atan2(dby, dbx);

        // Directed angle from A to B, same semantics as the construction
        // angle (跟随角度): normalized to (-180, 180].
        const double angleDeg = geo::normalizeDeg180(geo::radToDeg(dirB - dirA));

        am.dangling = false;
        if (std::abs(angleDeg - am.value) > 1e-9) {
            am.value = angleDeg;
            dirty = true;
        }
        if (!am.refName.isEmpty())
            m_doc->publishParameter(am.refName, angleDeg);  // degree domain
    }
    if (dirty)
        emit angleMeasureVarsChanged();
    return dirty;
}

// --- Block-removal cascade ---

void MeasurementStore::purgeBlockReferences(const QUuid& blockId)
{
    // Auto-delete linked variables whose source is this block. The
    // consumer length-freeze (引用对象被删, 长度恢复为数值) was already
    // orchestrated by the caller against the document's blocks.
    {
        bool linkedRemoved = false;
        auto lit = std::remove_if(m_linkedVars.begin(), m_linkedVars.end(),
            [&](const LinkedVariable& lv) {
                if (lv.sourceBlockId != blockId) return false;
                if (!lv.refName.isEmpty())
                    m_doc->removeParameterEntry(lv.refName);
                linkedRemoved = true;
                return true;
            });
        m_linkedVars.erase(lit, m_linkedVars.end());
        if (linkedRemoved)
            emit linkedVarsChanged();
    }

    // Auto-delete measure variables that reference the removed block — as
    // either endpoint, or as their OWNER (a bridge/measure line owns its
    // measurement: deleting the line deletes the variable). Without a
    // measurement target the variable is meaningless — destroyed immediately
    // (没有测量对象时立刻销毁).
    {
        bool measureRemoved = false;
        auto mit = std::remove_if(m_measureVars.begin(), m_measureVars.end(),
            [&](const MeasureVariable& mv) {
                if (mv.blockA != blockId && mv.blockB != blockId && mv.ownerBlockId != blockId)
                    return false;
                if (!mv.refName.isEmpty())
                    m_doc->removeParameterEntry(mv.refName);
                measureRemoved = true;
                return true;
            });
        m_measureVars.erase(mit, m_measureVars.end());
        if (measureRemoved)
            emit measureVarsChanged();
    }

    // Auto-delete angle measure variables that reference the removed block (as
    // either segment's host). Without a measurement target the variable is
    // meaningless — destroyed immediately (没有测量对象时立刻销毁).
    {
        bool angleRemoved = false;
        auto ait = std::remove_if(m_angleMeasures.begin(), m_angleMeasures.end(),
            [&](const AngleMeasureVariable& am) {
                if (am.blockA != blockId && am.blockB != blockId)
                    return false;
                if (!am.refName.isEmpty())
                    m_doc->removeParameterEntry(am.refName);
                angleRemoved = true;
                return true;
            });
        m_angleMeasures.erase(ait, m_angleMeasures.end());
        if (angleRemoved)
            emit angleMeasureVarsChanged();
    }
}

void MeasurementStore::clear()
{
    m_linkedVars.clear();
    m_measureVars.clear();
    m_angleMeasures.clear();
}

} // namespace cad::param
