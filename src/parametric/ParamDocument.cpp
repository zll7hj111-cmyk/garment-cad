#include "ParamDocument.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "parametric/Resolver.h"
#include "parametric/Serial.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ExpressionEvaluator.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "parametric/FollowerAngle.h"
#include "parametric/PerfProbe.h"

namespace cad::param {

ParamDocument::ParamDocument(QObject* parent)
    : QObject(parent)
    , m_undoStack(new QUndoStack(this))
{
    // Always an auxiliary calculation layer (index 0) + one working layer.
    m_layers.push_back(Layer{QStringLiteral("辅助层"), true, LayerType::Auxiliary});
    m_layers.push_back(Layer{QStringLiteral("图层 1"), true, LayerType::Working});
    m_activeLayer = 1;
}

ParamDocument::~ParamDocument() = default;

// --- Parameters ---

void ParamDocument::setParameter(const QString& name, double value)
{
    m_parameters[name] = value;
    resolveAll();
}

void ParamDocument::setParameters(const QHash<QString, double>& nameValues)
{
    for (auto it = nameValues.cbegin(); it != nameValues.cend(); ++it)
        m_parameters[it.key()] = it.value();
    resolveAll();
}

void ParamDocument::removeParameter(const QString& name)
{
    if (m_parameters.remove(name))
        resolveAll();
}

void ParamDocument::syncFormulaParameters(const QHash<QString, double>& cmValues)
{
    // Remove stale formula-derived parameters from the previous sync.
    for (const QString& old : std::as_const(m_formulaParamNames)) {
        if (!cmValues.contains(old))
            m_parameters.remove(old);
    }
    m_formulaParamNames = QSet<QString>(cmValues.keyBegin(), cmValues.keyEnd());

    // Insert / update current formula parameters (cm).
    for (auto it = cmValues.cbegin(); it != cmValues.cend(); ++it)
        m_parameters[it.key()] = it.value();

    resolveAll();
}

void ParamDocument::syncFormulaConditions(const QHash<QString, QList<Condition>>& conditioned)
{
    m_conditioned = conditioned;
}

double ParamDocument::parameter(const QString& name, double defaultVal) const
{
    return m_parameters.value(name, defaultVal);
}

// --- Free points ---

void ParamDocument::addFreePoint(ParamPoint pt)
{
    m_freePoints.push_back(std::move(pt));
    emit documentChanged();
}

void ParamDocument::removeFreePoint(const QUuid& id)
{
    auto it = std::find_if(m_freePoints.begin(), m_freePoints.end(),
        [&id](const ParamPoint& p) { return p.id == id; });
    if (it != m_freePoints.end()) {
        m_freePoints.erase(it);
        emit documentChanged();
    }
}

ParamPoint* ParamDocument::findFreePoint(const QUuid& id)
{
    auto it = std::find_if(m_freePoints.begin(), m_freePoints.end(),
        [&id](const ParamPoint& p) { return p.id == id; });
    return (it != m_freePoints.end()) ? &(*it) : nullptr;
}

// --- Blocks ---

QUuid ParamDocument::addBlock(Block block)
{
    QUuid id = block.id;
    // Assign readable serials to any points/segments that lack one.
    for (auto& pt : block.points)
        if (pt.serial.isEmpty()) pt.serial = newPointSerial();
    for (auto& seg : block.segments)
        if (seg.serial.isEmpty()) seg.serial = newLineSerial();
    block.resolve(m_parameters, m_conditioned);
    m_blockIndex.insert(id, static_cast<int>(m_blocks.size()));
    m_blocks.push_back(std::move(block));
    m_followersDirty = true;  // the new block may be a future attachment endpoint
    emit blockAdded(id);
    emit documentChanged();
    emit structureChanged();
    return id;
}

void ParamDocument::removeBlock(const QUuid& id)
{
    auto it = m_blockIndex.find(id);
    if (it == m_blockIndex.end()) return;

    const int idx = it.value();
    m_blocks.erase(m_blocks.begin() + idx);
    m_blockIndex.erase(it);

    // Rebuild index for elements after the removed one.
    for (int i = idx; i < static_cast<int>(m_blocks.size()); ++i)
        m_blockIndex[m_blocks[i].id] = i;

    // Also remove attachments referencing this block
    m_attachments.erase(
        std::remove_if(m_attachments.begin(), m_attachments.end(),
            [&id](const Attachment& a) {
                return a.fromBlockId == id || a.toBlockId == id;
            }),
        m_attachments.end());
    recountCrossLayerAttachments();
    m_followersDirty = true;  // attachments referencing the removed block vanished

    // Group membership cascade: drop the removed block from its group; a
    // group that shrinks below two members dissolves automatically
    // (组成员删到不足两条时自动解散).
    {
        const QUuid gid = m_blockGroup.value(id);
        if (!gid.isNull()) {
            m_blockGroup.remove(id);
            auto it = m_groupMembers.find(gid);
            if (it != m_groupMembers.end()) {
                it->erase(std::remove(it->begin(), it->end(), id), it->end());
                if (it->size() < 2) {
                    m_groupMembers.erase(it);
                    for (auto mi = m_blockGroup.begin(); mi != m_blockGroup.end(); )
                        mi = (mi.value() == gid) ? m_blockGroup.erase(mi) : std::next(mi);
                    m_groups.erase(std::remove_if(m_groups.begin(), m_groups.end(),
                        [&gid](const Group& g) { return g.id == gid; }),
                        m_groups.end());
                }
            }
            emit groupsChanged();
        }
    }

    // Auto-delete linked variables whose source is this block. Exact-match
    // consumers (length-linked copies, 复制的线段) first bake the frozen
    // measurement back to a plain number — the reference object is gone
    // (引用对象被删, 长度恢复为数值).
    {
        for (const auto& lv : m_linkedVars) {
            if (lv.sourceBlockId != id || lv.refName.isEmpty()) continue;
            for (auto& b : m_blocks) {
                for (auto& s : b.segments)
                    if (s.lengthFormula == lv.refName)
                        s.lengthFormula.clear();
                for (auto& p : b.points) {
                    if (p.distanceFormula != lv.refName) continue;
                    p.distance = lv.value;   // frozen measurement (mm)
                    p.distanceFormula.clear();
                }
            }
        }
        bool linkedRemoved = false;
        auto lit = std::remove_if(m_linkedVars.begin(), m_linkedVars.end(),
            [&](const LinkedVariable& lv) {
                if (lv.sourceBlockId != id) return false;
                if (!lv.refName.isEmpty())
                    m_parameters.remove(lv.refName);
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
                if (mv.blockA != id && mv.blockB != id && mv.ownerBlockId != id)
                    return false;
                if (!mv.refName.isEmpty())
                    m_parameters.remove(mv.refName);
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
                if (am.blockA != id && am.blockB != id)
                    return false;
                if (!am.refName.isEmpty())
                    m_parameters.remove(am.refName);
                angleRemoved = true;
                return true;
            });
        m_angleMeasures.erase(ait, m_angleMeasures.end());
        if (angleRemoved)
            emit angleMeasureVarsChanged();
    }

    emit blockRemoved(id);
    // Bridges pinned to the removed block just lost a pin — they are released
    // as independent segments (父线段删除后桥接线独立, see Block::isBridge).
    releaseOrphanedBridges();
    // Intersection points that lost their ray origin or target segment are
    // frozen at their last position (角度基准消失后交点冻结).
    degradeOrphanedIntersections();
    // The attachment graph changed: re-resolve so remaining blocks settle and
    // stale diagnostics (e.g. a dangling point on the removed block) refresh.
    resolveAll();
    emit structureChanged();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Delete-impact report (删除影响报告)
// ═══════════════════════════════════════════════════════════════════════════════
// Mirrors every cascade branch of removeBlock() — keep the two in sync when
// the cleanup logic grows. Prediction only: no mutation.
// ═══════════════════════════════════════════════════════════════════════════════

ParamDocument::DeleteImpact ParamDocument::deleteImpactReport(const QUuid& id) const
{
    DeleteImpact r;
    const Block* victim = blockById(id);
    if (!victim) return r;

    // 1. Attachments referencing the block vanish with it.
    for (const auto& a : m_attachments)
        if (a.fromBlockId == id || a.toBlockId == id)
            ++r.attachmentsRemoved;

    // 2. Bridges that lose at least one pin AND would drop below two pins are
    //    released as independent segments (releaseOrphanedBridges semantics).
    for (const auto& b : m_blocks) {
        if (!b.isBridge || b.id == id) continue;
        int pins = 0, pinsToVictim = 0;
        for (const auto& a : m_attachments) {
            if (!a.isPin || a.fromBlockId != b.id) continue;
            ++pins;
            if (a.toBlockId == id) ++pinsToVictim;
        }
        if (pinsToVictim > 0 && pins - pinsToVictim < 2)
            ++r.bridgesReleased;
    }

    // 3. Intersection points whose ray origin lives in the victim block are
    //    frozen at their last position (degradeOrphanedIntersections: the
    //    origin point can be cross-block; the target segment is always inside
    //    the point's own block, so only the origin can be lost here).
    for (const auto& b : m_blocks) {
        if (b.id == id) continue;
        for (const auto& pt : b.points) {
            if (pt.constraint != PointConstraint::Intersection) continue;
            if (victim->findPoint(pt.refPointA))
                ++r.intersectionsFrozen;
            // Aim-point reference (指向点) lost → the ray falls back to its
            // stored angle (reference cleared, point stays valid).
            if (!pt.interAimPointId.isNull() && victim->findPoint(pt.interAimPointId))
                ++r.intersectionsAimCleared;
        }
    }

    // 4+5. Linked variables sourced from the victim: consumers freeze their
    //    measurement back to a plain number; the variables themselves die.
    for (const auto& lv : m_linkedVars) {
        if (lv.sourceBlockId != id) continue;
        ++r.linkedVarsRemoved;
        if (lv.refName.isEmpty()) continue;
        for (const auto& b : m_blocks) {
            if (b.id == id) continue;  // the victim's own refs die with it
            for (const auto& s : b.segments)
                if (s.lengthFormula == lv.refName) ++r.linkedFrozen;
            for (const auto& p : b.points)
                if (p.distanceFormula == lv.refName) ++r.linkedFrozen;
        }
    }

    // 6. Measure variables referencing the victim (as endpoint or owner).
    for (const auto& mv : m_measureVars)
        if (mv.blockA == id || mv.blockB == id || mv.ownerBlockId == id)
            ++r.measureVarsRemoved;

    // 7. Angle measures referencing the victim (either segment's host).
    for (const auto& am : m_angleMeasures)
        if (am.blockA == id || am.blockB == id)
            ++r.angleVarsRemoved;

    // 8. Formulas referencing any measurement name removed above lose their
    //    operand and will report an evaluation error on the next resolve.
    QSet<QString> removedNames;
    for (const auto& lv : m_linkedVars)
        if (lv.sourceBlockId == id && !lv.refName.isEmpty())
            removedNames.insert(lv.refName);
    for (const auto& mv : m_measureVars)
        if ((mv.blockA == id || mv.blockB == id || mv.ownerBlockId == id)
            && !mv.refName.isEmpty())
            removedNames.insert(mv.refName);
    for (const auto& am : m_angleMeasures)
        if ((am.blockA == id || am.blockB == id) && !am.refName.isEmpty())
            removedNames.insert(am.refName);
    if (!removedNames.isEmpty()) {
        for (const auto& f : m_formulas) {
            const QStringList names = ExpressionEvaluator::referencedNames(f.expression);
            for (const QString& n : names) {
                if (removedNames.contains(n)) { ++r.formulasBroken; break; }
            }
        }
    }
    return r;
}

Block* ParamDocument::findBlock(const QUuid& id)
{
    return blockById(id);
}

const Block* ParamDocument::findBlock(const QUuid& id) const
{
    return blockById(id);
}

Block* ParamDocument::blockById(const QUuid& id)
{
    auto it = m_blockIndex.find(id);
    if (it == m_blockIndex.end()) return nullptr;
    return &m_blocks[it.value()];
}

const Block* ParamDocument::blockById(const QUuid& id) const
{
    auto it = m_blockIndex.find(id);
    if (it == m_blockIndex.end()) return nullptr;
    return &m_blocks[it.value()];
}

// --- Attachments ---

bool ParamDocument::addAttachment(Attachment att)
{
    // Enforce the forest invariant (see Attachment.h glossary): reject
    // attachments referencing missing blocks, a second leader for the same
    // follower, or links that would close a cycle.
    const Block* fromBlock = blockById(att.fromBlockId);
    const Block* toBlock = blockById(att.toBlockId);
    if (!fromBlock || !toBlock)
        return false;
    // Cross-layer boundary rule (单向跨层附着, one-way only):
    //   aux follower → working leader  PERMITTED — the Resolver settles these
    //     followers in Phase 3 (跨层沉降) after the working layers are final;
    //     the aux layer's geometry thus tracks working-layer movement.
    //   working follower → aux leader  REJECTED — working geometry must never
    //     be driven by the frozen calculation draft.
    const bool fromAux = isAuxBlock(*fromBlock);
    const bool toAux   = isAuxBlock(*toBlock);
    if (!fromAux && toAux)
        return false;
    const bool crossLayer = fromAux && !toAux;

    // Aux-layer connections are LOCKED by default (辅助层默认锁定): the aux
    // layer is a calculated draft whose wiring must not be torn apart by an
    // accidental drag. Callers may pre-set isLocked=false to opt out, but any
    // freshly created aux connection re-locks (重建即默认锁定). Working-layer
    // connections keep the caller's explicit value (manual lock allowed).
    att.isLocked = att.isLocked || fromAux;
    // A bridge is a pure downstream leaf: its pinned endpoints cannot anchor
    // followers, and bridge-to-bridge pins are forbidden. However, an AUXILIARY
    // point on a bridge is a legitimate leader target — the Resolver settles
    // bridge followers after the bridge (Step 4/5), so they land correctly.
    if (toBlock->isBridge) {
        if (att.isPin)
            return false;
        const ParamPoint* tp = toBlock->findPoint(att.toPointId);
        if (!tp || !tp->isAuxiliary)
            return false;
    }
    if (checkAttachment(m_attachments, att) != AttachmentIssue::Ok)
        return false;
    // NOTE: 组对连接零限制 (组只是选择快捷方式, 2026-08-04 设计定稿) ——
    // 无主连接预算, 组内外连接自由建立/断开, 与自由线段完全一致.

    // Value-cycle pre-check (值循环预检): a cross-layer edge that hangs a
    // measurement's SOURCE block beneath its CONSUMER would oscillate
    // (consumer pose → measured value → consumer pose). Reject it here.
    if (crossLayer && wouldCreateMeasureValueCycle(att))
        return false;

    m_attachments.push_back(std::move(att));
    if (crossLayer)
        ++m_crossLayerCount;
    m_followersDirty = true;  // new leader→follower edge
    resolveAll();
    emit structureChanged();
    return true;
}

void ParamDocument::removeAttachment(const QUuid& id)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it != m_attachments.end()) {
        m_attachments.erase(it);
        recountCrossLayerAttachments();
        m_followersDirty = true;  // edge removed
        // Detaching either pin of a bridge releases it as an independent segment.
        releaseOrphanedBridges();
        resolveAll();
        emit structureChanged();
    }
}

void ParamDocument::setAttachmentLocked(const QUuid& id, bool locked)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end() || it->isLocked == locked)
        return;
    it->isLocked = locked;
    resolveAll();
}

QSet<QUuid> ParamDocument::lockedClosure(const QSet<QUuid>& seed) const
{
    QSet<QUuid> result = seed;
    bool expanded = true;
    while (expanded) {
        expanded = false;
        for (const auto& att : m_attachments) {
            if (!att.isLocked) continue;
            const bool fromIn = result.contains(att.fromBlockId);
            const bool toIn   = result.contains(att.toBlockId);
            if (fromIn != toIn) {
                result.insert(fromIn ? att.toBlockId : att.fromBlockId);
                expanded = true;
            }
        }
    }
    return result;
}

void ParamDocument::removeAttachments(const QList<QUuid>& ids)
{
    if (ids.isEmpty()) return;
    const QSet<QUuid> idSet(ids.begin(), ids.end());
    m_attachments.erase(std::remove_if(m_attachments.begin(), m_attachments.end(),
        [&idSet](const Attachment& a) { return idSet.contains(a.id); }),
        m_attachments.end());
    recountCrossLayerAttachments();
    m_followersDirty = true;
    // Same cascade as removeAttachment(): a bridge that lost a pin is released.
    releaseOrphanedBridges();
    resolveAll();
    emit structureChanged();
}

void ParamDocument::addAttachmentsRaw(const std::vector<Attachment>& atts)
{
    if (atts.empty()) return;
    m_attachments.insert(m_attachments.end(), atts.begin(), atts.end());
    recountCrossLayerAttachments();
    m_followersDirty = true;
    resolveAll();
    emit structureChanged();
}

int ParamDocument::removeAttachmentsOfBlock(const QUuid& blockId)
{
    const auto before = m_attachments.size();
    m_attachments.erase(
        std::remove_if(m_attachments.begin(), m_attachments.end(),
            [&blockId](const Attachment& a) {
                return a.fromBlockId == blockId || a.toBlockId == blockId;
            }),
        m_attachments.end());
    const int removed = static_cast<int>(before - m_attachments.size());
    if (removed > 0) {
        recountCrossLayerAttachments();
        m_followersDirty = true;  // edges removed
        // Kicking a block out may have unpinned bridges attached to it (or the
        // kicked block itself may be a bridge that just lost both pins) —
        // release them as independent segments.
        releaseOrphanedBridges();
        resolveAll();
        emit structureChanged();
    }
    return removed;
}

std::vector<QUuid> ParamDocument::bridgesPinnedTo(const QUuid& hostBlockId) const
{
    std::vector<QUuid> result;
    for (const auto& a : m_attachments) {
        if (!a.isPin || a.toBlockId != hostBlockId) continue;
        if (std::find(result.begin(), result.end(), a.fromBlockId) == result.end())
            result.push_back(a.fromBlockId);
    }
    return result;
}

std::vector<QUuid> ParamDocument::releaseOrphanedBridges()
{
    std::vector<QUuid> released;
    for (auto& b : m_blocks) {
        if (!b.isBridge) continue;
        int pins = 0;
        for (const auto& a : m_attachments)
            if (a.isPin && a.fromBlockId == b.id) ++pins;
        if (pins >= 2) continue;

        // Lost at least one pin: become an independent segment instead of
        // being deleted (父线段被删后保留为独立线段).
        releaseBridge(b);
        released.push_back(b.id);
    }
    return released;
}

void ParamDocument::releaseBridge(Block& b)
{
    b.isBridge = false;

    // Freeze the current (stretched) world geometry into a self-contained
    // local construction (shared with the duplicate path).
    if (!b.freezeSegmentGeometry()) return;

    // A surviving pin becomes a normal follower attachment. Its construction
    // angle is back-solved from the Resolver formula
    //     rotation = refWorld + angle·π/180 − localDir
    // so the frozen world direction is preserved (角度约束不变, no jump).
    for (auto& a : m_attachments) {
        if (!a.isPin || a.fromBlockId != b.id) continue;
        const Block* leader = blockById(a.toBlockId);
        if (!leader) continue;            // host gone: the pin follows shortly
        a.isPin = false;
        a.toSegmentId = leader->exitSegmentAtPoint(a.toPointId);
        const double refWorld = leader->transform.rotation
            + leader->exitDirectionAtPoint(a.toPointId, a.toSegmentId);
        const double localDir = b.directionAtPoint(a.fromPointId);
        a.followerAngle = backSolveFollowerAngle(
            b.transform.rotation, localDir, refWorld);
        a.followerAngleFormula.clear();
        a.rotationMode = RotationMode::Angle;
        a.arcLength = 0.0;
        a.arcLengthFormula.clear();
    }
}

void ParamDocument::degradeOrphanedIntersections()
{
    for (auto& b : m_blocks) {
        for (auto& pt : b.points) {
            if (pt.constraint != PointConstraint::Intersection) continue;

            bool degraded = false;

            // Check if the target segment still exists in this block.
            const Segment* seg = b.findSegment(pt.hostSegmentId);
            if (!seg) {
                // Segment gone: freeze as Free point at last position.
                degraded = true;
                if (pt.resolved) {
                    pt.constraint = PointConstraint::Free;
                    pt.freePos = pt.resolvedPos;
                } else {
                    pt.constraint = PointConstraint::Free;
                    pt.freePos = geo::Vec2::zero();
                }
            } else {
                // Check if the ray origin point still exists (any block).
                bool originFound = false;
                for (const auto& ob : m_blocks) {
                    if (ob.findPoint(pt.refPointA)) { originFound = true; break; }
                }
                if (!originFound) {
                    // Origin gone: freeze as OnSegment (ratio from last position).
                    degraded = true;
                    const ParamPoint* sp = b.findPoint(seg->startPointId);
                    const ParamPoint* ep = b.findPoint(seg->endPointId);
                    double t = 0.5;
                    if (sp && ep && sp->resolved && ep->resolved && pt.resolved) {
                        geo::Vec2 d = ep->resolvedPos - sp->resolvedPos;
                        double len2 = d.lengthSquared();
                        if (len2 > 1e-12) {
                            t = (pt.resolvedPos - sp->resolvedPos).dot(d) / len2;
                            t = std::clamp(t, 0.0, 1.0);
                        }
                    }
                    pt.constraint = PointConstraint::OnSegment;
                    pt.refPointA = seg->startPointId;
                    pt.refPointB = seg->endPointId;
                    pt.ratio = t;
                }
                // Aim point (指向点) gone: fall back to the stored angle mode —
                // the point stays valid, only the point-aim link is dropped.
                if (!pt.interAimPointId.isNull()) {
                    bool aimFound = false;
                    for (const auto& ob : m_blocks) {
                        if (ob.findPoint(pt.interAimPointId)) { aimFound = true; break; }
                    }
                    if (!aimFound) pt.interAimPointId = QUuid();
                }
            }

            if (degraded) {
                // Clear intersection-specific fields.
                pt.interAngle = 90.0;
                pt.interAngleFormula.clear();
                pt.interBidirectional = false;
                pt.hostSegmentId = QUuid();
            }
        }
    }
}

// --- Readable serials ---

QString ParamDocument::newPointSerial()
{
    return Serial::make(Serial::randomPrefix(), QLatin1Char('P'), m_nextPointSeq++);
}

QString ParamDocument::newLineSerial()
{
    return Serial::make(Serial::randomPrefix(), QLatin1Char('L'), m_nextLineSeq++);
}

QString ParamDocument::newGroupSerial()
{
    return Serial::make(Serial::randomPrefix(), QLatin1Char('G'), m_nextGroupSeq++);
}

// --- User groups (成组) ---

QUuid ParamDocument::createGroup(const QList<QUuid>& memberIds, const QString& name)
{
    if (memberIds.size() < 2) return QUuid();

    // Validate: members exist, share one layer, none already grouped
    // (第一版禁嵌套、组成员必须同层).
    int layer = -1;
    for (const QUuid& id : memberIds) {
        const Block* b = blockById(id);
        if (!b) return QUuid();
        if (m_blockGroup.contains(id)) return QUuid();
        if (layer < 0) layer = b->layer;
        else if (b->layer != layer) return QUuid();
    }

    Group g;
    g.serial = newGroupSerial();
    g.name = name;
    const QUuid gid = g.id;
    m_groups.push_back(std::move(g));
    m_groupMembers.insert(gid, memberIds);
    for (const QUuid& id : memberIds)
        m_blockGroup.insert(id, gid);
    emit groupsChanged();
    return gid;
}

void ParamDocument::dissolveGroup(const QUuid& groupId)
{
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [&groupId](const Group& g) { return g.id == groupId; });
    if (it == m_groups.end()) return;
    m_groups.erase(it);
    m_groupMembers.remove(groupId);
    for (auto i = m_blockGroup.begin(); i != m_blockGroup.end(); )
        i = (i.value() == groupId) ? m_blockGroup.erase(i) : std::next(i);
    emit groupsChanged();
}

void ParamDocument::moveGroup(int fromIndex, int toIndex)
{
    const int n = static_cast<int>(m_groups.size());
    if (fromIndex < 0 || fromIndex >= n || toIndex < 0 || toIndex >= n)
        return;
    if (fromIndex == toIndex) return;
    Group g = std::move(m_groups[static_cast<size_t>(fromIndex)]);
    m_groups.erase(m_groups.begin() + fromIndex);
    m_groups.insert(m_groups.begin() + toIndex, std::move(g));
    emit groupsChanged();
}

void ParamDocument::restoreGroup(Group group, const QList<QUuid>& memberIds)
{
    // Replace any record with the same id (idempotent undo replay).
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [&group](const Group& g) { return g.id == group.id; });
    if (it != m_groups.end()) *it = group;
    else m_groups.push_back(group);
    // Replace the membership index entry wholesale (idempotent replay must
    // not duplicate members).
    QList<QUuid> members;
    for (const QUuid& id : memberIds)
        if (blockById(id)) {                   // skip members not (yet) restored
            members.push_back(id);
            m_blockGroup.insert(id, group.id);
        }
    m_groupMembers.insert(group.id, std::move(members));
    emit groupsChanged();
}

Group* ParamDocument::findGroup(const QUuid& groupId)
{
    auto it = std::find_if(m_groups.begin(), m_groups.end(),
        [&groupId](const Group& g) { return g.id == groupId; });
    return (it != m_groups.end()) ? &(*it) : nullptr;
}

QUuid ParamDocument::groupOfBlock(const QUuid& blockId) const
{
    return m_blockGroup.value(blockId);
}

QList<QUuid> ParamDocument::blocksInGroup(const QUuid& groupId) const
{
    return m_groupMembers.value(groupId);
}

void ParamDocument::setGroupName(const QUuid& groupId, const QString& name)
{
    if (Group* g = findGroup(groupId)) {
        if (g->name != name) {
            g->name = name;
            emit groupsChanged();
        }
    }
}

// --- Resolve ---

// ═══════════════════════════════════════════════════════════════════════════════
// Dirty-subgraph machinery (阶段2: 依赖边表 + 脏传播)
// ═══════════════════════════════════════════════════════════════════════════════

void ParamDocument::ensureFollowersIndex() const
{
    if (!m_followersDirty) return;
    m_followersOf.clear();
    for (const auto& att : m_attachments)
        m_followersOf[att.toBlockId].push_back(att.fromBlockId);
    m_followersDirty = false;
}

//---------------------------------------------------------------------------------------------------------------------
void ParamDocument::recountCrossLayerAttachments()
{
    int count = 0;
    for (const auto& a : m_attachments) {
        const Block* fb = blockById(a.fromBlockId);
        const Block* tb = blockById(a.toBlockId);
        if (fb && tb && isAuxBlock(*fb) && !isAuxBlock(*tb))
            ++count;
    }
    m_crossLayerCount = count;
}

//---------------------------------------------------------------------------------------------------------------------
QSet<QUuid> ParamDocument::collectMobileAuxBlocks() const
{
    QSet<QUuid> mobile;
    if (m_crossLayerCount == 0)
        return mobile;  // fast path: the boundary is sealed — nothing moves
    ensureFollowersIndex();
    QList<QUuid> queue;
    // Seeds: the aux followers of every cross-layer edge (pins included — a
    // pinned bridge moves with its host positionally).
    for (const auto& a : m_attachments) {
        const Block* fb = blockById(a.fromBlockId);
        const Block* tb = blockById(a.toBlockId);
        if (!fb || !tb) continue;
        if (isAuxBlock(*fb) && !isAuxBlock(*tb) && !mobile.contains(a.fromBlockId)) {
            mobile.insert(a.fromBlockId);
            queue.push_back(a.fromBlockId);
        }
    }
    // Everything beneath a cross-layer follower moves with it (all aux —
    // working→aux edges are rejected, so the subtree never leaves the layer).
    while (!queue.isEmpty()) {
        const QUuid cur = queue.takeFirst();
        const auto fit = m_followersOf.constFind(cur);
        if (fit == m_followersOf.constEnd()) continue;
        for (const QUuid& f : fit.value())
            if (!mobile.contains(f)) {
                mobile.insert(f);
                queue.push_back(f);
            }
    }
    return mobile;
}

//---------------------------------------------------------------------------------------------------------------------
bool ParamDocument::wouldCreateMeasureValueCycle(const Attachment& candidate) const
{
    // Conservative static check, invoked ONLY for cross-layer candidates
    // (same-layer topologies keep their long-standing behaviour untouched).
    //
    // 已知缺口 (formula side): this intercepts EDGE creation only. Editing an
    // existing topology's length/angle formula to reference a measurement
    // whose source already hangs beneath the consumer creates the same value
    // cycle — that guard belongs to the formula-commit entry (VariableCommands
    // layer) and is intentionally not handled here.

    // 1) Subtree beneath the candidate FOLLOWER over the EXISTING edges
    //    (pins included — a bridge rides on its hosts positionally).
    ensureFollowersIndex();
    QSet<QUuid> subTree{candidate.fromBlockId};
    QList<QUuid> queue{candidate.fromBlockId};
    while (!queue.isEmpty()) {
        const QUuid cur = queue.takeFirst();
        const auto fit = m_followersOf.constFind(cur);
        if (fit == m_followersOf.constEnd()) continue;
        for (const QUuid& f : fit.value())
            if (!subTree.contains(f)) {
                subTree.insert(f);
                queue.push_back(f);
            }
    }

    // 2) Ancestor set of the candidate LEADER. Walks EVERY attachment edge
    //    — pins included: a bridge's pose is driven by its pin hosts, so a
    //    bridge leader must pull its hosts into the ancestor set (otherwise
    //    the guard misses cycles whose path runs through a bridge).
    //    Multi-source BFS: a block can own several leader edges (a bridge
    //    has two pins), so a single-chain walk is insufficient.
    QSet<QUuid> ancestors{candidate.toBlockId};
    {
        QList<QUuid> queue{candidate.toBlockId};
        while (!queue.isEmpty()) {
            const QUuid cur = queue.takeFirst();
            for (const auto& a : m_attachments) {
                if (a.fromBlockId != cur) continue;
                if (!ancestors.contains(a.toBlockId)) {
                    ancestors.insert(a.toBlockId);
                    queue.push_back(a.toBlockId);
                }
            }
        }
    }

    // 3) Consumer test: does @p blockId's pose depend on the measurement
    //    @p refName? ownerBlock (measure line) plus every formula that can
    //    drive the block's geometry.
    const auto formulaRefs = [](const QString& formula, const QString& refName) {
        if (formula.isEmpty()) return false;
        const QStringList names = ExpressionEvaluator::referencedNames(formula);
        for (const QString& n : names)
            if (n.compare(refName, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };
    const auto consumedBy = [&](const QString& refName, const QUuid& ownerBlockId,
                                const QUuid& blockId) {
        if (!ownerBlockId.isNull() && blockId == ownerBlockId)
            return true;
        const Block* b = blockById(blockId);
        if (!b) return false;
        for (const auto& s : b->segments)
            if (formulaRefs(s.lengthFormula, refName)) return true;
        for (const auto& p : b->points)
            if (formulaRefs(p.distanceFormula, refName) ||
                formulaRefs(p.angleFormula, refName))
                return true;
        if (formulaRefs(b->endTargetOffsetFormula, refName)) return true;
        for (const auto& a : m_attachments)
            if (a.fromBlockId == blockId &&
                (formulaRefs(a.followerAngleFormula, refName) ||
                 formulaRefs(a.arcLengthFormula, refName)))
                return true;
        return false;
    };

    // 4) A cycle forms iff a measurement's SOURCE sits in the new follower
    //    subtree while one of its CONSUMERS is on the leader's ancestor
    //    chain (the only relation the new edge creates).
    const auto checkVar = [&](const QString& refName, const QUuid& ownerBlockId,
                              std::initializer_list<QUuid> sources) {
        if (refName.isEmpty()) return false;
        for (const QUuid& src : sources) {
            if (src.isNull() || !subTree.contains(src)) continue;
            for (const QUuid& anc : ancestors)
                if (consumedBy(refName, ownerBlockId, anc))
                    return true;
        }
        return false;
    };
    for (const auto& mv : m_measureVars)
        if (checkVar(mv.refName, mv.ownerBlockId, {mv.blockA, mv.blockB}))
            return true;
    for (const auto& lv : m_linkedVars)
        if (checkVar(lv.refName, QUuid(), {lv.sourceBlockId}))
            return true;
    for (const auto& am : m_angleMeasures)
        if (checkVar(am.refName, QUuid(), {am.blockA, am.blockB}))
            return true;
    return false;
}

//---------------------------------------------------------------------------------------------------------------------
bool ParamDocument::blockReferences(const Block& b, const QUuid& targetBlockId) const
{
    // NOTE: a NEW point constraint whose position depends on another block's
    // point/segment must be registered HERE, or drag-follow (阶段2 dirty
    // propagation) will silently MISS its cross-block dependency and the
    // follower will not move. See the registry in ParamPoint.h.
    // Endpoint-aim target (终点指向).
    if (b.endTargetBlockId == targetBlockId)
        return true;
    // Curve-anchor follow target (曲线点跟随).
    for (const auto& pt : b.points)
        if (pt.followBlockId == targetBlockId)
            return true;
    // Point-id references (Polar / Midpoint / OnSegment / Intersection ray
    // origin + aim point / Interpolated measurement origin) that land in the
    // target block.
    const Block* target = blockById(targetBlockId);
    if (!target) return false;
    for (const auto& pt : b.points) {
        const QUuid refs[] = {pt.refPointId, pt.refPointA, pt.refPointB,
                              pt.interpRefPointId, pt.interAimPointId};
        for (const QUuid& ref : refs)
            if (!ref.isNull() && target->findPoint(ref))
                return true;
    }
    return false;
}

//---------------------------------------------------------------------------------------------------------------------
QSet<QUuid> ParamDocument::collectAffected(const QList<QUuid>& seeds) const
{
    ensureFollowersIndex();
    QSet<QUuid> affected;
    QList<QUuid> queue;
    for (const QUuid& s : seeds)
        if (!s.isNull() && !affected.contains(s)) {
            affected.insert(s);
            queue.push_back(s);
        }

    while (!queue.isEmpty()) {
        const QUuid cur = queue.takeFirst();

        // 1) Attachment subtree: everything that follows the current block
        //    (regular attachments AND bridge pins — bridges depend on hosts).
        const auto fit = m_followersOf.constFind(cur);
        if (fit != m_followersOf.constEnd()) {
            for (const QUuid& f : fit.value())
                if (!affected.contains(f)) {
                    affected.insert(f);
                    queue.push_back(f);
                }
        }

        // 2) Cross-block referencers: blocks holding an aim / point reference
        //    into the current block must re-solve when it moves. One pass per
        //    dequeue (references are at most one hop; deeper chains re-enter
        //    via the queue). O(|affected| · N · P) simple id comparisons — far
        //    cheaper than re-resolving the untouched blocks.
        for (const auto& b : m_blocks) {
            if (affected.contains(b.id)) continue;
            if (blockReferences(b, cur)) {
                affected.insert(b.id);
                queue.push_back(b.id);
            }
        }
    }
    return affected;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Resolve
// ═══════════════════════════════════════════════════════════════════════════════

void ParamDocument::resolveAll()
{
    resolveAllInternal(true);
}

//---------------------------------------------------------------------------------------------------------------------
void ParamDocument::resolveForDrag(const QList<QUuid>& affectedBlockIds,
                                   const QList<QUuid>& ignoredAttachments)
{
    // Cross-layer attachments present: dragging a working-layer block must
    // move its aux followers within the SAME frame — escalate to dual-group
    // invalidation so Phase 1/2/3 all run and the cross-layer settle happens
    // inside this drag frame. Documents WITHOUT cross-layer attachments keep
    // the exact pre-existing narrowed behaviour (zero overhead).
    if (m_crossLayerCount > 0) {
        m_auxDirty = true;
        m_workingDirty = true;
        m_dirtyAnnotated = true;
    }

    // Dirty-subgraph mode: seeds are non-empty → narrow the pass to the
    // affected subgraph. Empty seeds = plain full resolve (minus panels).
    // NOTE: collectAffected() BFS rides the full edge table — cross-layer
    // edges included — so a working seed automatically pulls its aux
    // followers into the affected set (Phase 3 moves only that subset).
    const QSet<QUuid> affected = affectedBlockIds.isEmpty()
        ? QSet<QUuid>()
        : collectAffected(affectedBlockIds);
    const QSet<QUuid>* affectedPtr = affectedBlockIds.isEmpty() ? nullptr : &affected;

    // Keep the pass-local ignored list alive for the duration of the call.
    QList<QUuid> ignored = ignoredAttachments;
    resolveAllInternal(false, affectedPtr,
                       ignored.isEmpty() ? nullptr : &ignored);
}

//---------------------------------------------------------------------------------------------------------------------
void ParamDocument::resolveAllInternal(bool emitDocChanged,
                                       const QSet<QUuid>* affectedOnly,
                                       const QList<QUuid>* ignoredAttachments)
{
    GCAD_PERF_SCOPE("resolve");
    // Conservative fallback: callers that did not narrow the scope via
    // invalidateLayer()/invalidateAllLayers() re-resolve everything.
    if (!m_dirtyAnnotated) {
        m_auxDirty = true;
        m_workingDirty = true;
    }
    m_dirtyAnnotated = false;

    constexpr int kAuxLayer = 0;  // invariant: the aux layer is always index 0.

    // Attachments excluded from this pass (drag-time cross-selection links
    // pending removal). They stay in the document; the pass simply skips them.
    std::vector<Attachment> filteredAttachments;
    const std::vector<Attachment>* passAttachments = &m_attachments;
    if (ignoredAttachments && !ignoredAttachments->isEmpty()) {
        filteredAttachments.reserve(m_attachments.size());
        for (const auto& a : m_attachments)
            if (!ignoredAttachments->contains(a.id))
                filteredAttachments.push_back(a);
        passAttachments = &filteredAttachments;
    }

    // Dirty-subgraph narrowing: starts as the caller-provided subset; upgraded
    // to null (full resolve) the moment a measurement changes, because a
    // measured value can feed formulas in blocks OUTSIDE the subset.
    const QSet<QUuid>* effAffected = affectedOnly;

    // ── Phase 1: auxiliary calculation layer (only when dirty) ──
    // Aux geometry is a pure function of the variables; during working-layer
    // manipulation it stays frozen and its cached transforms remain valid.
    bool auxRan = false, workingRan = false;
    if (m_auxDirty) {
        GCAD_PERF_SCOPE("resolve.aux");
        std::vector<ResolveDiagnostic> auxDiag;  // discarded (phase 2 owns m_diagnostics)
        measureLinkedVars();   // feed aux formulas before resolving (old semantics)
        measureMeasureVars();
        measureAngleMeasureVars();
        Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                             &auxDiag, Resolver::Scope::AuxOnly, kAuxLayer,
                             effAffected);
        for (int i = 0; i < 4 && (measureLinkedVars() || measureMeasureVars()
                                  || measureAngleMeasureVars()); ++i) {
            if (effAffected) effAffected = nullptr;  // measurement changed → full
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters,
                                 m_conditioned, &auxDiag,
                                 Resolver::Scope::AuxOnly, kAuxLayer,
                                 effAffected);
        }
        m_auxDirty = false;
        auxRan = true;
        // Published measurement values sourced from the aux layer may have
        // changed → working layers must re-measure and re-resolve.
        m_workingDirty = true;
    }

    // ── Phase 2: working layers ──
    // Extracted into a closure so the Phase 3 cross-layer fixpoint can
    // re-run it when settling aux followers perturbs published measurements.
    auto runWorkingPhase = [&]() {
        if (!m_workingDirty) return;
        GCAD_PERF_SCOPE("resolve.work");
        // Measurements sourced entirely from the (now clean) aux layer keep
        // their cached values — only working-geometry measurements re-run.
        // (Cross-layer-linked aux blocks are exempt from the cache — see
        // collectMobileAuxBlocks: their geometry tracks the working layers.)
        measureLinkedVars(/*skipAuxSource=*/true);
        measureMeasureVars(/*skipAuxSource=*/true);
        measureAngleMeasureVars(/*skipAuxSource=*/true);
        Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                             &m_diagnostics, Resolver::Scope::WorkingOnly, kAuxLayer,
                             effAffected);
        // Linked measurements are taken BEFORE the pass; if the pass moved any
        // measured geometry (e.g. the source segment of a length-linked copy was
        // just edited), propagate to consumers until stable (bounded: linked
        // chains are shallow — copies reference originals directly).
        for (int i = 0; i < 4 &&
             (measureLinkedVars(/*skipAuxSource=*/true) ||
              measureMeasureVars(/*skipAuxSource=*/true) ||
              measureAngleMeasureVars(/*skipAuxSource=*/true)); ++i) {
            if (effAffected) effAffected = nullptr;  // measurement changed → full
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters,
                                 m_conditioned, &m_diagnostics,
                                 Resolver::Scope::WorkingOnly, kAuxLayer,
                                 effAffected);
        }
        m_workingDirty = false;
        workingRan = true;
    };
    runWorkingPhase();

    // ── Phase 3: cross-layer settle (跨层沉降) ──
    // Only when cross-layer attachments EXIST (counter-maintained; documents
    // without them pay exactly one integer test). Aux followers are settled
    // onto the now-final working leaders via an AuxOnly pass (out-of-scope
    // working leaders act as static roots). Settling can move measurement
    // SOURCES (aux geometry) → re-measure WITHOUT the aux cache; a changed
    // value feeds working-layer formulas → re-run Phase 2 + Phase 3, bounded
    // by the same ≤4-round fixpoint budget. Non-convergence reports
    // ResolveDiagnostic::NotConverged.
    bool xLayerMoved = false;
    if (m_crossLayerCount > 0 && workingRan) {
        bool settled = false;
        for (int round = 0; round < 4; ++round) {
            xLayerMoved = true;
            GCAD_PERF_SCOPE("resolve.xlayer");
            std::vector<ResolveDiagnostic> xDiag;  // discarded (phase 2 owns m_diagnostics)
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                                 &xDiag, Resolver::Scope::AuxOnly, kAuxLayer,
                                 effAffected);
            // No skipAuxSource here: cross-layer-linked aux geometry may have
            // moved, and its published values must be re-measured fully.
            if (!(measureLinkedVars() || measureMeasureVars()
                  || measureAngleMeasureVars())) {
                settled = true;
                break;
            }
            // Measurement changed → working layers must re-solve, then the
            // aux followers re-settle (next loop round).
            if (effAffected) effAffected = nullptr;  // measurement changed → full
            m_workingDirty = true;
            runWorkingPhase();
        }
        if (!settled) {
            // Budget exhausted: the last round re-solved the working layers,
            // but the aux followers never got their FINAL settle against the
            // fresh leader poses — geometry would otherwise lag one round
            // behind. One pure AuxOnly settle (no re-measurement, no
            // working re-solve), then report the non-convergence.
            GCAD_PERF_SCOPE("resolve.xlayer.final");
            std::vector<ResolveDiagnostic> finalDiag;  // discarded (NotConverged reported below)
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                                 &finalDiag, Resolver::Scope::AuxOnly, kAuxLayer,
                                 effAffected);
            m_diagnostics.push_back({ResolveDiagnostic::Kind::NotConverged, QUuid()});
        }
    }

    // --- Curve-anchor follow post-pass ---
    // CurveAnchor points with a follow connection track their target point:
    // recompute chord-relative params so the anchor stays at target + offset.
    // Only blocks in layer groups that actually re-resolved are touched.
    for (auto& blk : m_blocks) {
        const bool blkAux = isAuxLayer(blk.layer);
        if (!(blkAux ? (auxRan || xLayerMoved) : workingRan)) continue;
        for (auto& pt : blk.points) {
            if (pt.constraint != PointConstraint::CurveAnchor)
                continue;
            if (pt.followPointId.isNull())
                continue;
            // Find the target point (may be in another block or the same one).
            const Block* targetBlk = (pt.followBlockId == blk.id)
                ? &blk : findBlock(pt.followBlockId);
            if (!targetBlk) continue;
            const ParamPoint* target = targetBlk->findPoint(pt.followPointId);
            if (!target || !target->resolved) continue;

            // Desired world position = target world pos + follow offset.
            const geo::Vec2 targetWorld = targetBlk->transform.toWorld(target->resolvedPos);
            const geo::Vec2 desiredWorld = targetWorld + pt.followOffset;
            const geo::Vec2 desiredLocal = blk.transform.toLocal(desiredWorld);

            // Convert to chord-relative (percent, offset).
            const Segment* hostSeg = blk.findSegment(pt.hostSegmentId);
            if (!hostSeg) continue;
            const ParamPoint* sp = blk.findPoint(hostSeg->startPointId);
            const ParamPoint* ep = blk.findPoint(hostSeg->endPointId);
            if (!sp || !ep || !sp->resolved || !ep->resolved) continue;
            const geo::Vec2 chord = ep->resolvedPos - sp->resolvedPos;
            const double len = chord.length();
            if (len < 1e-9) continue;
            const geo::Vec2 unitDir = chord / len;
            const geo::Vec2 normal{-unitDir.y, unitDir.x};
            const geo::Vec2 rel = desiredLocal - sp->resolvedPos;
            pt.interpPercent = rel.dot(unitDir) / len;
            pt.interpOffsetDist = rel.dot(normal);

            // Re-resolve this point's position from the new params.
            pt.resolvedPos = sp->resolvedPos
                           + unitDir * (len * pt.interpPercent)
                           + normal * pt.interpOffsetDist;
            pt.resolved = true;
        }
    }

    emit resolved();
    if (emitDocChanged)
        emit documentChanged();
    cad::perf::Probe::get().frameTick();  // perf probe: one logical frame done
}

// --- Serialization support ---

void ParamDocument::clear()
{
    m_parameters.clear();
    m_formulaParamNames.clear();
    m_conditioned.clear();
    m_freePoints.clear();
    m_blocks.clear();
    m_blockIndex.clear();
    m_attachments.clear();
    m_followersOf.clear();
    m_followersDirty = true;
    m_crossLayerCount = 0;
    m_diagnostics.clear();
    m_variables.clear();
    m_formulas.clear();
    m_formulaGroups.clear();
    m_linkedVars.clear();
    m_measureVars.clear();
    m_angleMeasures.clear();
    m_groups.clear();
    m_blockGroup.clear();
    m_groupMembers.clear();
    m_layers.clear();
    m_layers.push_back(Layer{QStringLiteral("辅助层"), true, LayerType::Auxiliary});
    m_layers.push_back(Layer{QStringLiteral("图层 1"), true, LayerType::Working});
    m_activeLayer = 1;
    m_nextPointSeq = 1;
    m_nextLineSeq  = 1;
    m_nextGroupSeq = 1;

    // Notify the UI so the canvas and all panels drop their stale content.
    // documentReset is consumed by CanvasScene + GroupPanel; the variable
    // panels need their own structural signals (measure cards would
    // otherwise survive a File→New), and LayerPanel rebuilds on
    // layersChanged (its layer registry was just regenerated).
    emit documentReset();
    emit variablesChanged();
    emit formulasChanged();
    emit formulaGroupsChanged();
    emit linkedVarsChanged();
    emit measureVarsChanged();
    emit angleMeasureVarsChanged();
    emit layersChanged();
    emit groupsChanged();
}

void ParamDocument::setSerialCounters(int pointSeq, int lineSeq, int groupSeq)
{
    m_nextPointSeq = pointSeq;
    m_nextLineSeq  = lineSeq;
    m_nextGroupSeq = groupSeq;
}

void ParamDocument::restoreGroups(std::vector<Group> groups, QHash<QUuid, QUuid> blockGroup)
{
    m_groups = std::move(groups);
    m_blockGroup = std::move(blockGroup);
    m_groupMembers.clear();
    for (auto it = m_blockGroup.cbegin(); it != m_blockGroup.cend(); ++it)
        m_groupMembers[it.value()].push_back(it.key());
}

QUuid ParamDocument::addBlockRaw(Block block)
{
    QUuid id = block.id;
    m_blockIndex.insert(id, static_cast<int>(m_blocks.size()));
    m_blocks.push_back(std::move(block));
    m_followersDirty = true;  // batch restore: edge table rebuilt on demand
    return id;
}

void ParamDocument::addAttachmentRaw(Attachment att)
{
    m_attachments.push_back(std::move(att));
    recountCrossLayerAttachments();
    m_followersDirty = true;  // batch restore: edge table rebuilt on demand
}

void ParamDocument::addFreePointRaw(ParamPoint pt)
{
    m_freePoints.push_back(std::move(pt));
}

void ParamDocument::finishRestore()
{
    // Recreate a canvas item for every restored block (geometry is already
    // resolved by the preceding recomputeFormulas() call).
    for (const auto& b : m_blocks)
        emit blockAdded(b.id);

    // Rebuild the variable / formula / linked / group panels with the restored data.
    emit variablesChanged();
    emit formulasChanged();
    emit linkedVarsChanged();
    emit structureChanged();
    emit groupsChanged();
}

// --- Variables ---

void ParamDocument::addVariable(Variable var)
{
    m_variables.push_back(std::move(var));
    emit variablesChanged();
    recomputeFormulas();
}

void ParamDocument::removeVariable(const QUuid& id)
{
    auto it = std::find_if(m_variables.begin(), m_variables.end(),
        [&id](const Variable& v) { return v.id == id; });
    if (it != m_variables.end()) {
        m_variables.erase(it);
        emit variablesChanged();
        recomputeFormulas();
    }
}

void ParamDocument::updateVariable(const Variable& var)
{
    for (auto& v : m_variables) {
        if (v.id == var.id) {
            v = var;
            break;
        }
    }
    emit variablesChanged();
    recomputeFormulas();
}

Variable* ParamDocument::findVariable(const QUuid& id)
{
    auto it = std::find_if(m_variables.begin(), m_variables.end(),
        [&id](const Variable& v) { return v.id == id; });
    return (it != m_variables.end()) ? &(*it) : nullptr;
}

// --- Formula variables ---

void ParamDocument::addFormula(FormulaVariable formula)
{
    m_formulas.push_back(std::move(formula));
    m_formulaDepsDirty = true;  // new formula may be referenced by others
    emit formulasChanged();
    recomputeFormulas();
}

void ParamDocument::removeFormula(const QUuid& id)
{
    auto it = std::find_if(m_formulas.begin(), m_formulas.end(),
        [&id](const FormulaVariable& f) { return f.id == id; });
    if (it != m_formulas.end()) {
        m_formulas.erase(it);
        m_formulaDepsDirty = true;  // references to the removed name now dangle
        emit formulasChanged();
        recomputeFormulas();
    }
}

void ParamDocument::updateFormula(const FormulaVariable& formula)
{
    for (auto& f : m_formulas) {
        if (f.id == formula.id) {
            f.name = formula.name;
            f.expression = formula.expression;
            f.actualValueCm = formula.actualValueCm;
            f.comment = formula.comment;
            f.conditions = formula.conditions;
            f.conditionsEnabled = formula.conditionsEnabled;
            m_formulaDepsDirty = true;  // name / expression may have changed
            break;
        }
    }
    emit formulasChanged();
    recomputeFormulas();
}

FormulaVariable* ParamDocument::findFormula(const QUuid& id)
{
    auto it = std::find_if(m_formulas.begin(), m_formulas.end(),
        [&id](const FormulaVariable& f) { return f.id == id; });
    return (it != m_formulas.end()) ? &(*it) : nullptr;
}

// ============================================================
// Formula groups (panel folders)
// ============================================================

void ParamDocument::addFormulaGroup(FormulaGroup group)
{
    m_formulaGroups.push_back(std::move(group));
    emit formulaGroupsChanged();
}

void ParamDocument::removeFormulaGroup(const QUuid& groupId)
{
    auto it = std::find_if(m_formulaGroups.begin(), m_formulaGroups.end(),
        [&groupId](const FormulaGroup& g) { return g.id == groupId; });
    if (it == m_formulaGroups.end())
        return;

    // Dissolve: members fall back to the ungrouped section.
    bool membersChanged = false;
    for (auto& f : m_formulas) {
        if (f.groupId == groupId) {
            f.groupId = QUuid();
            membersChanged = true;
        }
    }
    m_formulaGroups.erase(it);

    emit formulaGroupsChanged();
    if (membersChanged)
        emit formulasChanged();
}

void ParamDocument::renameFormulaGroup(const QUuid& groupId, const QString& name)
{
    if (auto* g = findFormulaGroup(groupId); g && g->name != name) {
        g->name = name;
        emit formulaGroupsChanged();
    }
}

void ParamDocument::setFormulaGroupCollapsed(const QUuid& groupId, bool collapsed)
{
    if (auto* g = findFormulaGroup(groupId); g && g->collapsed != collapsed) {
        g->collapsed = collapsed;
        emit formulaGroupsChanged();
    }
}

void ParamDocument::moveFormulaGroup(int fromIndex, int toIndex)
{
    const int n = static_cast<int>(m_formulaGroups.size());
    if (fromIndex < 0 || fromIndex >= n || toIndex < 0 || toIndex >= n
        || fromIndex == toIndex)
        return;

    FormulaGroup g = std::move(m_formulaGroups[fromIndex]);
    m_formulaGroups.erase(m_formulaGroups.begin() + fromIndex);
    m_formulaGroups.insert(m_formulaGroups.begin() + toIndex, std::move(g));
    emit formulaGroupsChanged();
}

void ParamDocument::moveFormula(const QUuid& formulaId, const QUuid& targetGroupId,
                                int targetLocalIndex)
{
    auto it = std::find_if(m_formulas.begin(), m_formulas.end(),
        [&formulaId](const FormulaVariable& f) { return f.id == formulaId; });
    if (it == m_formulas.end())
        return;

    FormulaVariable moved = std::move(*it);
    m_formulas.erase(it);
    moved.groupId = targetGroupId;

    // Global insert position = slot of the targetLocalIndex-th member of the
    // target group (after removal). Only the relative order within a group
    // matters for display, so falling back to "after the last member" (or
    // vector end for an empty group) is sufficient.
    auto insertPos = m_formulas.size();
    bool placed = false;
    int local = 0;
    for (std::size_t i = 0; i < m_formulas.size(); ++i) {
        if (m_formulas[i].groupId == targetGroupId) {
            if (local == targetLocalIndex) { insertPos = i; placed = true; break; }
            ++local;
        }
    }
    if (!placed) {
        for (std::size_t i = m_formulas.size(); i > 0; --i) {
            if (m_formulas[i - 1].groupId == targetGroupId) { insertPos = i; break; }
        }
    }

    m_formulas.insert(m_formulas.begin() + insertPos, std::move(moved));
    emit formulasChanged();
}

FormulaGroup* ParamDocument::findFormulaGroup(const QUuid& groupId)
{
    auto it = std::find_if(m_formulaGroups.begin(), m_formulaGroups.end(),
        [&groupId](const FormulaGroup& g) { return g.id == groupId; });
    return (it != m_formulaGroups.end()) ? &(*it) : nullptr;
}

// ============================================================
// Canvas layers (pure selection/visibility filter)
// ============================================================

int ParamDocument::addLayer(const QString& name)
{
    m_layers.push_back(Layer{name, true});
    emit layersChanged();
    return static_cast<int>(m_layers.size()) - 1;
}

void ParamDocument::removeLayer(int index)
{
    const int n = layerCount();
    if (index < 0 || index >= n || n <= 2)
        return;  // Need at least aux + one working layer.
    if (isAuxLayer(index))
        return;  // The auxiliary calculation layer cannot be removed.

    // Blocks in the removed layer fall to the layer below, but never into the
    // auxiliary layer (clamp to 1); blocks in higher layers shift down by one
    // to keep indices contiguous.
    for (auto& b : m_blocks) {
        if (b.layer == index)
            b.layer = std::max(1, index - 1);
        else if (b.layer > index)
            --b.layer;
    }
    m_layers.erase(m_layers.begin() + index);

    if (m_activeLayer >= layerCount())
        m_activeLayer = layerCount() - 1;
    else if (m_activeLayer > index)
        --m_activeLayer;

    emit layersChanged();
}

void ParamDocument::renameLayer(int index, const QString& name)
{
    if (index < 0 || index >= layerCount())
        return;
    if (m_layers[index].name != name) {
        m_layers[index].name = name;
        emit layersChanged();
    }
}

void ParamDocument::setLayerVisible(int index, bool visible)
{
    if (index < 0 || index >= layerCount())
        return;
    if (m_layers[index].visible == visible)
        return;
    m_layers[index].visible = visible;
    emit layersChanged();

    // Hiding the active layer: switch to the nearest visible layer so the
    // user always has an editable layer.
    if (!visible && index == m_activeLayer) {
        for (int d = 1; d < layerCount(); ++d) {
            if (index - d >= 0 && m_layers[index - d].visible) {
                setActiveLayer(index - d);
                return;
            }
            if (index + d < layerCount() && m_layers[index + d].visible) {
                setActiveLayer(index + d);
                return;
            }
        }
    }
}

bool ParamDocument::layerVisible(int index) const
{
    if (index < 0 || index >= layerCount())
        return true;  // Out-of-range treated as visible (defensive).
    return m_layers[index].visible;
}

void ParamDocument::setActiveLayer(int index)
{
    if (index < 0 || index >= layerCount() || index == m_activeLayer)
        return;
    m_activeLayer = index;
    emit activeLayerChanged(index);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Formula dependency graph (拓扑序缓存)
// ═══════════════════════════════════════════════════════════════════════════════

void ParamDocument::rebuildFormulaOrder() const
{
    m_formulaDepsDirty = false;
    const int n = static_cast<int>(m_formulas.size());
    m_formulaOrder.clear();
    m_formulaOrder.reserve(n);

    // Dependencies = identifiers matched case-insensitively against formula
    // names (same folding the evaluator's PushVar fallback uses, so the
    // graph and the evaluator agree on what resolves).
    QHash<QString, int> nameIndex;  // case-folded formula name -> index
    for (int i = 0; i < n; ++i)
        if (!m_formulas[i].name.isEmpty())
            nameIndex.insert(m_formulas[i].name.toLower(), i);

    std::vector<std::vector<int>> dependents(n);
    std::vector<int> inDegree(n, 0);
    for (int i = 0; i < n; ++i) {
        const auto& f = m_formulas[i];
        if (f.actualValueCm.has_value() || f.expression.isEmpty()) continue;
        const QStringList refs = ExpressionEvaluator::referencedNames(f.expression);
        QSet<QString> seen;
        for (const QString& ref : std::as_const(refs)) {
            const QString lower = ref.toLower();
            if (seen.contains(lower)) continue;
            seen.insert(lower);
            const auto it = nameIndex.constFind(lower);
            if (it == nameIndex.constEnd()) continue;  // variable / typo
            dependents[it.value()].push_back(i);
            ++inDegree[i];
        }
    }

    // Kahn's algorithm; document order preserved among ready formulas.
    std::vector<int> queue;
    queue.reserve(n);
    for (int i = 0; i < n; ++i)
        if (inDegree[i] == 0) queue.push_back(i);
    for (size_t head = 0; head < queue.size(); ++head) {
        const int cur = queue[head];
        m_formulaOrder.push_back(cur);
        for (const int d : dependents[cur])
            if (--inDegree[d] == 0) queue.push_back(d);
    }
    m_formulaAcyclic = static_cast<int>(m_formulaOrder.size()) == n;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Formulas
// ═══════════════════════════════════════════════════════════════════════════════

void ParamDocument::recomputeFormulas()
{
    // Sync plain variable values into the parameter map (cm).
    for (const auto& v : m_variables) {
        const double cm = geo::Units::mmToCm(v.value);
        if (!v.name.isEmpty())
            m_parameters.insert(v.name, cm);
        if (!v.refName.isEmpty())
            m_parameters.insert(v.refName, cm);
    }

    // Base value map (cm): variables under display name + reference name.
    QHash<QString, double> baseMap;
    for (const auto& v : m_variables) {
        const double cm = geo::Units::mmToCm(v.value);
        if (!v.name.isEmpty())
            baseMap.insert(v.name, cm);
        if (!v.refName.isEmpty())
            baseMap.insert(v.refName, cm);
    }

    // Condition table: formulaName -> conditions (enabled & non-empty only).
    QHash<QString, QList<Condition>> condByName;
    for (const auto& f : m_formulas) {
        if (f.conditionsEnabled && !f.conditions.isEmpty() && !f.name.isEmpty())
            condByName.insert(f.name, f.conditions);
    }

    // Topological single-pass evaluation (optimisation): formulas may
    // reference other formulas by name. The dependency order is CACHED and
    // rebuilt only when the formula set changes — variable edits reuse it.
    // For the (overwhelmingly common) acyclic case, evaluating in dependency
    // order makes every formula converge in ONE pass — the legacy bounded
    // fixpoint re-evaluated ALL formulas per pass, costing O(depth x count)
    // evaluations on deep reference chains (each early pass mostly failing
    // with "unknown variable" until its dependencies are ready). Cycles (an
    // authoring error) fall back to the original fixpoint, bit-for-bit
    // unchanged.
    if (m_formulaDepsDirty)
        rebuildFormulaOrder();

    if (m_formulaAcyclic) {
        // Acyclic: every formula evaluates exactly once, dependencies first.
        for (const int i : m_formulaOrder) {
            auto& f = m_formulas[i];
            if (f.actualValueCm.has_value()) {
                // User-provided actual value overrides the expression.
                f.valid = true;
                f.error.clear();
                f.baseValue = geo::Units::cmToMm(*f.actualValueCm);
                if (!f.name.isEmpty())
                    baseMap.insert(f.name, *f.actualValueCm);
                continue;
            }
            const auto r = ConditionEngine::evaluate(
                f.expression, baseMap, condByName);
            if (r.ok) {
                f.valid = true;
                f.error.clear();
                f.baseValue = geo::Units::cmToMm(r.value);
                if (!f.name.isEmpty())
                    baseMap.insert(f.name, r.value);
            } else {
                f.valid = false;
                f.error = r.error;
            }
        }
    } else {
        // Cycle detected (a formula references itself or a cycle): fall back
        // to the legacy bounded fixpoint so values stay bit-for-bit identical.
        const int passes = qMax(1, static_cast<int>(m_formulas.size()));
        for (int pass = 0; pass < passes; ++pass) {
            bool progressed = false;
            for (auto& f : m_formulas) {
                // User-provided actual value overrides the expression entirely.
                if (f.actualValueCm.has_value()) {
                    f.valid = true;
                    f.error.clear();
                    f.baseValue = geo::Units::cmToMm(*f.actualValueCm);
                    if (!f.name.isEmpty()) {
                        auto it = baseMap.find(f.name);
                        if (it == baseMap.end()) {
                            baseMap.insert(f.name, *f.actualValueCm);
                            progressed = true;
                        } else if (qAbs(it.value() - *f.actualValueCm) > 1e-9) {
                            it.value() = *f.actualValueCm;
                            progressed = true;
                        }
                    }
                    continue;
                }
                const auto r = ConditionEngine::evaluate(
                    f.expression, baseMap, condByName);
                if (r.ok) {
                    f.valid = true;
                    f.error.clear();
                    f.baseValue = geo::Units::cmToMm(r.value);
                    if (!f.name.isEmpty()) {
                        auto it = baseMap.find(f.name);
                        if (it == baseMap.end()) {
                            baseMap.insert(f.name, r.value);
                            progressed = true;
                        } else if (qAbs(it.value() - r.value) > 1e-9) {
                            it.value() = r.value;
                            progressed = true;
                        }
                    }
                } else {
                    f.valid = false;
                    f.error = r.error;
                }
            }
            if (!progressed)
                break;
        }
    }

    // Final adjusted values (conditions applied) for display + standalone use.
    for (auto& f : m_formulas) {
        if (!f.valid) continue;
        // Actual value is a direct override: no condition adjustment.
        if (f.actualValueCm.has_value()) {
            f.value = f.baseValue;
            continue;
        }
        const double baseCm = geo::Units::mmToCm(f.baseValue);
        const double adjCm = condByName.contains(f.name)
            ? ConditionEngine::applyConditions(baseCm, f.conditions, baseMap)
            : baseCm;
        f.value = geo::Units::cmToMm(adjCm);
    }

    // Sync formula BASE values (conditions NOT applied) into parameters.
    QHash<QString, double> baseCm;
    for (const auto& f : m_formulas) {
        if (f.valid && !f.name.isEmpty())
            baseCm.insert(f.name, geo::Units::mmToCm(f.baseValue));
    }
    syncFormulaConditions(condByName);
    syncFormulaParameters(baseCm);  // triggers resolveAll()

    emit formulasChanged();
}

// --- Linked variables ---

void ParamDocument::addLinked(LinkedVariable lv)
{
    m_linkedVars.push_back(std::move(lv));
    emit linkedVarsChanged();
    resolveAll();
}

void ParamDocument::removeLinked(const QUuid& id)
{
    auto it = std::find_if(m_linkedVars.begin(), m_linkedVars.end(),
        [&id](const LinkedVariable& lv) { return lv.id == id; });
    if (it != m_linkedVars.end()) {
        // Remove the published parameter key.
        if (!it->refName.isEmpty())
            m_parameters.remove(it->refName);
        m_linkedVars.erase(it);
        emit linkedVarsChanged();
        resolveAll();
    }
}

void ParamDocument::updateLinked(const LinkedVariable& lv)
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
                m_parameters.remove(oldRef);
                existing.refName = lv.refName;
            }
            break;
        }
    }
    emit linkedVarsChanged();
    resolveAll();
}

LinkedVariable* ParamDocument::findLinked(const QUuid& id)
{
    auto it = std::find_if(m_linkedVars.begin(), m_linkedVars.end(),
        [&id](const LinkedVariable& lv) { return lv.id == id; });
    return (it != m_linkedVars.end()) ? &(*it) : nullptr;
}

LinkedVariable* ParamDocument::findLinkedBySource(const QUuid& blockId,
                                                  const QUuid& segmentId)
{
    for (auto& lv : m_linkedVars) {
        if (lv.sourceBlockId == blockId && lv.sourceSegmentId == segmentId)
            return &lv;
    }
    return nullptr;
}

QList<QUuid> ParamDocument::linkedConsumerBlocks(const QUuid& sourceBlockId) const
{
    QList<QUuid> result;
    for (const auto& lv : m_linkedVars) {
        if (lv.sourceBlockId != sourceBlockId || lv.refName.isEmpty()) continue;
        for (const auto& b : m_blocks) {
            if (result.contains(b.id)) continue;
            bool consumes = false;
            for (const auto& s : b.segments)
                if (s.lengthFormula == lv.refName) { consumes = true; break; }
            if (!consumes)
                for (const auto& p : b.points)
                    if (p.distanceFormula == lv.refName) { consumes = true; break; }
            if (consumes) result.push_back(b.id);
        }
    }
    return result;
}

bool ParamDocument::measureLinkedVars(bool skipAuxSource)
{
    GCAD_PERF_SCOPE("meas.linked");
    if (m_linkedVars.empty()) return false;

    // Purge variables whose measurement target no longer exists (block or
    // segment deleted). Without a target the variable is destroyed
    // immediately (没有测量对象时立刻销毁).
    bool purged = false;
    auto pit = std::remove_if(m_linkedVars.begin(), m_linkedVars.end(),
        [&](const LinkedVariable& lv) {
            const Block* blk = blockById(lv.sourceBlockId);
            if (blk && blk->findSegment(lv.sourceSegmentId)) return false;
            if (!lv.refName.isEmpty())
                m_parameters.remove(lv.refName);
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
    const QSet<QUuid> mobileAux =
        (skipAuxSource && m_crossLayerCount > 0) ? collectMobileAuxBlocks() : QSet<QUuid>();
    for (auto& lv : m_linkedVars) {
        const Block* blk = blockById(lv.sourceBlockId);
        if (!blk) { lv.dangling = true; continue; }
        // Layered cache: the aux layer was not re-resolved, so measurements
        // sourced from it cannot have changed — keep the cached value.
        if (skipAuxSource && isAuxBlock(*blk) && !mobileAux.contains(lv.sourceBlockId))
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
            m_parameters[lv.refName] = geo::Units::mmToCm(len);
    }
    if (dirty)
        emit linkedVarsChanged();
    return dirty;
}

// --- Measure variables ---

void ParamDocument::addMeasure(MeasureVariable mv)
{
    m_measureVars.push_back(std::move(mv));
    emit measureVarsChanged();
    resolveAll();
}

void ParamDocument::removeMeasure(const QUuid& id)
{
    auto it = std::find_if(m_measureVars.begin(), m_measureVars.end(),
        [&id](const MeasureVariable& mv) { return mv.id == id; });
    if (it == m_measureVars.end()) return;

    if (!it->refName.isEmpty())
        m_parameters.remove(it->refName);
    m_measureVars.erase(it);
    emit measureVarsChanged();
    resolveAll();
}

void ParamDocument::updateMeasure(const MeasureVariable& mv)
{
    for (auto& existing : m_measureVars) {
        if (existing.id == mv.id) {
            const QString oldRef = existing.refName;
            existing.name = mv.name;
            existing.comment = mv.comment;
            if (existing.refName != mv.refName && !mv.refName.isEmpty()) {
                m_parameters.remove(oldRef);
                existing.refName = mv.refName;
            }
            // Keep the owned measure line's segment name in sync (测量变量名称
            // → 测量对象名称). Bump geometryEpoch so the canvas rebuilds the
            // cached name label on the next resolve (a rename moves no geometry,
            // so the resolve pass would not invalidate the cache by itself).
            if (!existing.ownerBlockId.isNull()) {
                if (Block* owner = blockById(existing.ownerBlockId)) {
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
    resolveAll();
}

void ParamDocument::setOwnerMeasureName(const QUuid& ownerBlockId, const QString& name)
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

MeasureVariable* ParamDocument::findMeasure(const QUuid& id)
{
    for (auto& mv : m_measureVars)
        if (mv.id == id) return &mv;
    return nullptr;
}

MeasureVariable* ParamDocument::findMeasureByOwner(const QUuid& ownerBlockId)
{
    if (ownerBlockId.isNull()) return nullptr;
    for (auto& mv : m_measureVars)
        if (mv.ownerBlockId == ownerBlockId) return &mv;
    return nullptr;
}

const MeasureVariable* ParamDocument::findMeasureByOwner(const QUuid& ownerBlockId) const
{
    if (ownerBlockId.isNull()) return nullptr;
    for (const auto& mv : m_measureVars)
        if (mv.ownerBlockId == ownerBlockId) return &mv;
    return nullptr;
}

// --- Angle measure variables ---

void ParamDocument::addAngleMeasure(AngleMeasureVariable am)
{
    m_angleMeasures.push_back(std::move(am));
    emit angleMeasureVarsChanged();
    resolveAll();
}

void ParamDocument::removeAngleMeasure(const QUuid& id)
{
    auto it = std::find_if(m_angleMeasures.begin(), m_angleMeasures.end(),
        [&id](const AngleMeasureVariable& am) { return am.id == id; });
    if (it == m_angleMeasures.end()) return;

    if (!it->refName.isEmpty())
        m_parameters.remove(it->refName);
    m_angleMeasures.erase(it);
    emit angleMeasureVarsChanged();
    resolveAll();
}

void ParamDocument::updateAngleMeasure(const AngleMeasureVariable& am)
{
    for (auto& existing : m_angleMeasures) {
        if (existing.id == am.id) {
            existing.name = am.name;
            existing.comment = am.comment;
            break;
        }
    }
    emit angleMeasureVarsChanged();
    resolveAll();
}

AngleMeasureVariable* ParamDocument::findAngleMeasure(const QUuid& id)
{
    for (auto& am : m_angleMeasures)
        if (am.id == id) return &am;
    return nullptr;
}

bool ParamDocument::measureAngleMeasureVars(bool skipAuxSource)
{
    GCAD_PERF_SCOPE("meas.angle");
    if (m_angleMeasures.empty()) return false;

    // Purge variables whose measurement target no longer exists (block or
    // segment deleted) — destroyed immediately (没有测量对象时立刻销毁).
    bool purged = false;
    auto pit = std::remove_if(m_angleMeasures.begin(), m_angleMeasures.end(),
        [&](const AngleMeasureVariable& am) {
            const Block* blkA = blockById(am.blockA);
            const Block* blkB = blockById(am.blockB);
            if (blkA && blkB &&
                blkA->findSegment(am.segmentA) && blkB->findSegment(am.segmentB))
                return false;
            if (!am.refName.isEmpty())
                m_parameters.remove(am.refName);
            purged = true;
            return true;
        });
    m_angleMeasures.erase(pit, m_angleMeasures.end());
    if (purged)
        emit angleMeasureVarsChanged();

    bool dirty = false;
    // Cross-layer correction (see measureLinkedVars): mobile aux blocks track
    // the working layers via Phase 3, so their measurements cannot be cached.
    const QSet<QUuid> mobileAux =
        (skipAuxSource && m_crossLayerCount > 0) ? collectMobileAuxBlocks() : QSet<QUuid>();
    for (auto& am : m_angleMeasures) {
        const Block* blkA = blockById(am.blockA);
        const Block* blkB = blockById(am.blockB);
        if (!blkA || !blkB) { am.dangling = true; continue; }
        // Layered cache: both segments on the (clean) aux layer → the value
        // cannot have changed — keep the cached measurement.
        if (skipAuxSource && isAuxBlock(*blkA) && isAuxBlock(*blkB)
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
            m_parameters[am.refName] = angleDeg;  // degree domain
    }
    if (dirty)
        emit angleMeasureVarsChanged();
    return dirty;
}

bool ParamDocument::measureMeasureVars(bool skipAuxSource)
{
    GCAD_PERF_SCOPE("meas.measure");
    if (m_measureVars.empty()) return false;

    // Purge variables whose measurement target no longer exists (block or
    // point deleted) — destroyed immediately (没有测量对象时立刻销毁).
    bool purged = false;
    auto pit = std::remove_if(m_measureVars.begin(), m_measureVars.end(),
        [&](const MeasureVariable& mv) {
            const Block* blkA = blockById(mv.blockA);
            const Block* blkB = blockById(mv.blockB);
            if (blkA && blkB &&
                blkA->findPoint(mv.pointA) && blkB->findPoint(mv.pointB))
                return false;
            if (!mv.refName.isEmpty())
                m_parameters.remove(mv.refName);
            purged = true;
            return true;
        });
    m_measureVars.erase(pit, m_measureVars.end());
    if (purged)
        emit measureVarsChanged();

    bool dirty = false;
    // Cross-layer correction (see measureLinkedVars): mobile aux blocks track
    // the working layers via Phase 3, so their measurements cannot be cached.
    const QSet<QUuid> mobileAux =
        (skipAuxSource && m_crossLayerCount > 0) ? collectMobileAuxBlocks() : QSet<QUuid>();
    for (auto& mv : m_measureVars) {
        const Block* blkA = blockById(mv.blockA);
        const Block* blkB = blockById(mv.blockB);
        if (!blkA || !blkB) { mv.dangling = true; continue; }
        // Layered cache: both endpoints on the (clean) aux layer → the value
        // cannot have changed — keep the cached measurement.
        if (skipAuxSource && isAuxBlock(*blkA) && isAuxBlock(*blkB)
            && !mobileAux.contains(mv.blockA) && !mobileAux.contains(mv.blockB))
            continue;

        const ParamPoint* pa = blkA->findPoint(mv.pointA);
        const ParamPoint* pb = blkB->findPoint(mv.pointB);
        if (!pa || !pb || !pa->resolved || !pb->resolved) continue;

        // World-space distance (points may be on different blocks).
        const geo::Vec2 wa = blkA->transform.toWorld(pa->resolvedPos);
        const geo::Vec2 wb = blkB->transform.toWorld(pb->resolvedPos);
        const double dist = wa.distanceTo(wb);

        mv.dangling = false;
        if (std::abs(dist - mv.value) > 1e-9) {
            mv.value = dist;
            dirty = true;
        }
        if (!mv.refName.isEmpty())
            m_parameters[mv.refName] = geo::Units::mmToCm(dist);
    }
    if (dirty)
        emit measureVarsChanged();
    return dirty;
}

} // namespace cad::param
