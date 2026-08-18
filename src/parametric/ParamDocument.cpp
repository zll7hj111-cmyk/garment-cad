#include "ParamDocument.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <QDebug>

#include "parametric/Resolver.h"
#include "parametric/Serial.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ExpressionEvaluator.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "parametric/FollowerAngle.h"
#include "parametric/PerfProbe.h"
#include "parametric/LayerRegistry.h"
#include "parametric/VariableStore.h"
#include "parametric/MeasurementStore.h"
#include "parametric/GroupRegistry.h"

namespace cad::param {

ParamDocument::ParamDocument(QObject* parent)
    : QObject(parent)
    , m_undoStack(new QUndoStack(this))
    , m_layerRegistry(std::make_unique<LayerRegistry>(this))
    , m_variableStore(std::make_unique<VariableStore>(this, this))
    , m_measureStore(std::make_unique<MeasurementStore>(this, this))
    , m_groupRegistry(std::make_unique<GroupRegistry>(this, this))
{
    // Re-emit sub-domain signals so observers keep a single connection point
    // (ParamDocument remains the facade over the sub-domain registries).
    connect(m_layerRegistry.get(), &LayerRegistry::layersChanged,
            this, &ParamDocument::layersChanged);
    connect(m_layerRegistry.get(), &LayerRegistry::activeLayerChanged,
            this, &ParamDocument::activeLayerChanged);
    connect(m_variableStore.get(), &VariableStore::variablesChanged,
            this, &ParamDocument::variablesChanged);
    connect(m_variableStore.get(), &VariableStore::formulasChanged,
            this, &ParamDocument::formulasChanged);
    connect(m_variableStore.get(), &VariableStore::formulaGroupsChanged,
            this, &ParamDocument::formulaGroupsChanged);
    connect(m_measureStore.get(), &MeasurementStore::linkedVarsChanged,
            this, &ParamDocument::linkedVarsChanged);
    connect(m_measureStore.get(), &MeasurementStore::measureVarsChanged,
            this, &ParamDocument::measureVarsChanged);
    connect(m_measureStore.get(), &MeasurementStore::angleMeasureVarsChanged,
            this, &ParamDocument::angleMeasureVarsChanged);
    connect(m_groupRegistry.get(), &GroupRegistry::groupsChanged,
            this, &ParamDocument::groupsChanged);
}

ParamDocument::~ParamDocument() = default;

// --- Sub-domain registry access (read-only escape hatch) ---

const LayerRegistry& ParamDocument::layerRegistry() const { return *m_layerRegistry; }
const VariableStore& ParamDocument::variableStore() const { return *m_variableStore; }
const MeasurementStore& ParamDocument::measurementStore() const { return *m_measureStore; }
const GroupRegistry& ParamDocument::groupRegistry() const { return *m_groupRegistry; }

// --- Facade forwarding: variables / formulas / formula groups ---

void ParamDocument::addVariable(Variable var) { m_variableStore->addVariable(std::move(var)); }
void ParamDocument::removeVariable(const QUuid& id) { m_variableStore->removeVariable(id); }
void ParamDocument::updateVariable(const Variable& var) { m_variableStore->updateVariable(var); }
const std::vector<Variable>& ParamDocument::variables() const { return m_variableStore->variables(); }
Variable* ParamDocument::findVariable(const QUuid& id) { return m_variableStore->findVariable(id); }

void ParamDocument::addFormula(FormulaVariable formula) { m_variableStore->addFormula(std::move(formula)); }
void ParamDocument::removeFormula(const QUuid& id) { m_variableStore->removeFormula(id); }
void ParamDocument::updateFormula(const FormulaVariable& formula) { m_variableStore->updateFormula(formula); }
const std::vector<FormulaVariable>& ParamDocument::formulas() const { return m_variableStore->formulas(); }
FormulaVariable* ParamDocument::findFormula(const QUuid& id) { return m_variableStore->findFormula(id); }
void ParamDocument::recomputeFormulas() { m_variableStore->recomputeFormulas(); }

void ParamDocument::addFormulaGroup(FormulaGroup group) { m_variableStore->addFormulaGroup(std::move(group)); }
void ParamDocument::removeFormulaGroup(const QUuid& groupId) { m_variableStore->removeFormulaGroup(groupId); }
void ParamDocument::renameFormulaGroup(const QUuid& groupId, const QString& name) { m_variableStore->renameFormulaGroup(groupId, name); }
void ParamDocument::setFormulaGroupCollapsed(const QUuid& groupId, bool collapsed) { m_variableStore->setFormulaGroupCollapsed(groupId, collapsed); }
void ParamDocument::moveFormulaGroup(int fromIndex, int toIndex) { m_variableStore->moveFormulaGroup(fromIndex, toIndex); }
void ParamDocument::moveFormula(const QUuid& formulaId, const QUuid& targetGroupId, int targetLocalIndex)
{ m_variableStore->moveFormula(formulaId, targetGroupId, targetLocalIndex); }
const std::vector<FormulaGroup>& ParamDocument::formulaGroups() const { return m_variableStore->formulaGroups(); }
FormulaGroup* ParamDocument::findFormulaGroup(const QUuid& groupId) { return m_variableStore->findFormulaGroup(groupId); }

// --- Facade forwarding: canvas layers ---

const std::vector<Layer>& ParamDocument::layers() const { return m_layerRegistry->layers(); }
int ParamDocument::layerCount() const { return m_layerRegistry->layerCount(); }
int ParamDocument::layerIndex(const QUuid& layerId) const { return m_layerRegistry->indexOf(layerId); }
const Layer* ParamDocument::layerById(const QUuid& layerId) const { return m_layerRegistry->layerById(layerId); }
QUuid ParamDocument::auxLayerId() const { return m_layerRegistry->auxLayerId(); }
QUuid ParamDocument::firstWorkingLayerId() const { return m_layerRegistry->firstWorkingLayerId(); }
QUuid ParamDocument::addLayer(const QString& name) { return m_layerRegistry->addLayer(name); }
void ParamDocument::insertLayerAt(int index, Layer layer) { m_layerRegistry->insertLayerAt(index, std::move(layer)); }
void ParamDocument::renameLayer(const QUuid& layerId, const QString& name) { m_layerRegistry->renameLayer(layerId, name); }
void ParamDocument::setLayerVisible(const QUuid& layerId, bool visible) { m_layerRegistry->setLayerVisible(layerId, visible); }
bool ParamDocument::layerVisible(const QUuid& layerId) const { return m_layerRegistry->layerVisible(layerId); }
QUuid ParamDocument::activeLayer() const { return m_layerRegistry->activeLayer(); }
void ParamDocument::setActiveLayer(const QUuid& layerId) { m_layerRegistry->setActiveLayer(layerId); }
bool ParamDocument::isAuxLayer(const QUuid& layerId) const { return m_layerRegistry->isAuxLayer(layerId); }
bool ParamDocument::layerEffectivelyVisible(const QUuid& layerId) const { return layerVisible(layerId); }
bool ParamDocument::layerSnappable(const QUuid& layerId) const
{
    if (!layerVisible(layerId)) return false;
    if (layerId == activeLayer()) return true;
    return !isAuxLayer(layerId);
}
void ParamDocument::invalidateLayer(const QUuid& layerId) { m_layerRegistry->invalidateLayer(layerId); }
void ParamDocument::invalidateAllLayers() { m_layerRegistry->invalidateAllLayers(); }

// --- Facade forwarding: linked / measure / angle-measure variables ---

void ParamDocument::addLinked(LinkedVariable lv) { m_measureStore->addLinked(std::move(lv)); }
void ParamDocument::removeLinked(const QUuid& id) { m_measureStore->removeLinked(id); }
void ParamDocument::updateLinked(const LinkedVariable& lv) { m_measureStore->updateLinked(lv); }
const std::vector<LinkedVariable>& ParamDocument::linkedVars() const { return m_measureStore->linkedVars(); }
LinkedVariable* ParamDocument::findLinked(const QUuid& id) { return m_measureStore->findLinked(id); }
LinkedVariable* ParamDocument::findLinkedBySource(const QUuid& blockId, const QUuid& segmentId)
{ return m_measureStore->findLinkedBySource(blockId, segmentId); }
bool ParamDocument::measureLinkedVars(bool skipAuxSource) { return m_measureStore->measureLinkedVars(skipAuxSource); }

void ParamDocument::addMeasure(MeasureVariable mv) { m_measureStore->addMeasure(std::move(mv)); }
void ParamDocument::removeMeasure(const QUuid& id) { m_measureStore->removeMeasure(id); }
void ParamDocument::updateMeasure(const MeasureVariable& mv) { m_measureStore->updateMeasure(mv); }
void ParamDocument::setOwnerMeasureName(const QUuid& ownerBlockId, const QString& name)
{ m_measureStore->setOwnerMeasureName(ownerBlockId, name); }

void ParamDocument::addAngleMeasure(AngleMeasureVariable am) { m_measureStore->addAngleMeasure(std::move(am)); }
void ParamDocument::removeAngleMeasure(const QUuid& id) { m_measureStore->removeAngleMeasure(id); }
void ParamDocument::updateAngleMeasure(const AngleMeasureVariable& am) { m_measureStore->updateAngleMeasure(am); }
const std::vector<AngleMeasureVariable>& ParamDocument::angleMeasures() const { return m_measureStore->angleMeasures(); }
AngleMeasureVariable* ParamDocument::findAngleMeasure(const QUuid& id) { return m_measureStore->findAngleMeasure(id); }
const std::vector<MeasureVariable>& ParamDocument::measureVars() const { return m_measureStore->measureVars(); }
MeasureVariable* ParamDocument::findMeasure(const QUuid& id) { return m_measureStore->findMeasure(id); }
MeasureVariable* ParamDocument::findMeasureByOwner(const QUuid& ownerBlockId)
{ return m_measureStore->findMeasureByOwner(ownerBlockId); }
const MeasureVariable* ParamDocument::findMeasureByOwner(const QUuid& ownerBlockId) const
{ return m_measureStore->findMeasureByOwner(ownerBlockId); }
bool ParamDocument::measureMeasureVars(bool skipAuxSource) { return m_measureStore->measureMeasureVars(skipAuxSource); }
bool ParamDocument::measureAngleMeasureVars(bool skipAuxSource) { return m_measureStore->measureAngleMeasureVars(skipAuxSource); }
QList<QUuid> ParamDocument::linkedConsumerBlocks(const QUuid& sourceBlockId) const
{ return m_measureStore->linkedConsumerBlocks(sourceBlockId); }

// --- Silent batch restore (deserializer / trusted pipelines) ---

void ParamDocument::replaceLayersRaw(std::vector<Layer> layers)
{ m_layerRegistry->replaceLayersRaw(std::move(layers)); }
void ParamDocument::restoreVariableRaw(Variable var)
{ m_variableStore->addVariableRaw(std::move(var)); }
void ParamDocument::restoreFormulaRaw(FormulaVariable formula)
{ m_variableStore->addFormulaRaw(std::move(formula)); }
void ParamDocument::restoreFormulaGroupRaw(FormulaGroup group)
{ m_variableStore->addFormulaGroupRaw(std::move(group)); }
void ParamDocument::insertFormulaGroupAt(int index, FormulaGroup group)
{ m_variableStore->insertFormulaGroupAt(index, std::move(group)); }
void ParamDocument::restoreLinkedRaw(LinkedVariable lv)
{ m_measureStore->addLinkedRaw(std::move(lv)); }
void ParamDocument::restoreMeasureRaw(MeasureVariable mv)
{ m_measureStore->addMeasureRaw(std::move(mv)); }
void ParamDocument::restoreAngleMeasureRaw(AngleMeasureVariable am)
{ m_measureStore->addAngleMeasureRaw(std::move(am)); }

// --- Attachment lookup (the ONLY authorized in-place attachment edit channel)

Attachment* ParamDocument::findAttachment(const QUuid& id)
{
    for (auto& a : m_attachments)
        if (a.id == id)
            return &a;
    return nullptr;
}

const Attachment* ParamDocument::findAttachment(const QUuid& id) const
{
    for (const auto& a : m_attachments)
        if (a.id == id)
            return &a;
    return nullptr;
}

// --- Facade forwarding: user groups ---

QUuid ParamDocument::createGroup(const QList<QUuid>& memberIds, const QString& name)
{ return m_groupRegistry->createGroup(memberIds, name); }
void ParamDocument::dissolveGroup(const QUuid& groupId) { m_groupRegistry->dissolveGroup(groupId); }
void ParamDocument::moveGroup(int fromIndex, int toIndex) { m_groupRegistry->moveGroup(fromIndex, toIndex); }
void ParamDocument::restoreGroup(Group group, const QList<QUuid>& memberIds)
{ m_groupRegistry->restoreGroup(std::move(group), memberIds); }
const std::vector<Group>& ParamDocument::groups() const { return m_groupRegistry->groups(); }
Group* ParamDocument::findGroup(const QUuid& groupId) { return m_groupRegistry->findGroup(groupId); }
QUuid ParamDocument::groupOfBlock(const QUuid& blockId) const { return m_groupRegistry->groupOfBlock(blockId); }
QList<QUuid> ParamDocument::blocksInGroup(const QUuid& groupId) const { return m_groupRegistry->blocksInGroup(groupId); }
bool ParamDocument::addGroupMember(const QUuid& groupId, const QUuid& blockId) { return m_groupRegistry->addGroupMember(groupId, blockId); }
bool ParamDocument::removeGroupMember(const QUuid& groupId, const QUuid& blockId) { return m_groupRegistry->removeGroupMember(groupId, blockId); }
void ParamDocument::setGroupName(const QUuid& groupId, const QString& name) { m_groupRegistry->setGroupName(groupId, name); }
void ParamDocument::setGroupBoundingBoxVisible(const QUuid& groupId, bool visible) { m_groupRegistry->setGroupBoundingBoxVisible(groupId, visible); }
bool ParamDocument::isGroupBoundingBoxVisible(const QUuid& groupId) const { return m_groupRegistry->isGroupBoundingBoxVisible(groupId); }
void ParamDocument::restoreGroups(std::vector<Group> groups, QHash<QUuid, QUuid> blockGroup)
{ m_groupRegistry->restoreGroups(std::move(groups), std::move(blockGroup)); }
void ParamDocument::ensureGroupComponentRoot(const QUuid& groupId) { m_groupRegistry->ensureComponentRoot(groupId); }
QUuid ParamDocument::groupComponentRootBlockId(const QUuid& groupId) const { return m_groupRegistry->componentRootBlockId(groupId); }
bool ParamDocument::hasComponentHinge(const QUuid& groupId) const { return m_groupRegistry->hasComponentHinge(groupId); }
bool ParamDocument::setComponentHinge(const QUuid& groupId, const ComponentHinge& hinge) { return m_groupRegistry->setComponentHinge(groupId, hinge); }
bool ParamDocument::updateComponentHinge(const QUuid& groupId, const ComponentHinge& hinge) { return m_groupRegistry->updateComponentHinge(groupId, hinge); }
void ParamDocument::clearComponentHinge(const QUuid& groupId) { m_groupRegistry->clearComponentHinge(groupId); }
const ComponentHinge* ParamDocument::componentHinge(const QUuid& groupId) const { return m_groupRegistry->componentHinge(groupId); }

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

// --- Internal sub-domain hooks ---

void ParamDocument::publishParameter(const QString& name, double cmValue)
{
    m_parameters[name] = cmValue;
}

void ParamDocument::removeParameterEntry(const QString& name)
{
    m_parameters.remove(name);
}

void ParamDocument::publishParamsRaw(const QHash<QString, double>& cmValues)
{
    for (auto it = cmValues.cbegin(); it != cmValues.cend(); ++it)
        m_parameters[it.key()] = it.value();
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
    // A block without a layer assignment lands on the first WORKING layer —
    // never on the auxiliary calculation layer (no implicit aux drafts).
    if (block.layer.isNull())
        block.layer = m_layerRegistry->firstWorkingLayerId();
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
    m_groupRegistry->purgeBlock(id);

    // Auto-delete linked variables whose source is this block. Exact-match
    // consumers (length-linked copies, 复制的线段) first bake the frozen
    // measurement back to a plain number — the reference object is gone
    // (引用对象被删, 长度恢复为数值).
    for (const auto& lv : m_measureStore->linkedVars()) {
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
    m_measureStore->purgeBlockReferences(id);

    // Dart lines (省道线) that referenced the removed block (as start pin A
    // or offset point B) lose their constraint and degrade to plain lines —
    // their current geometry stays frozen in place (降级普通线).
    for (auto& b : m_blocks) {
        if (b.dartStartBlockId == id || b.dartRefBlockId == id) {
            b.dartStartBlockId = {};
            b.dartStartPointId = {};
            b.dartRefBlockId   = {};
            b.dartRefPointId   = {};
            b.dartRefSegmentId = {};
            b.dartOffsetFormula.clear();
            b.dartAngleFormula.clear();
        }
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
    for (const auto& lv : m_measureStore->linkedVars()) {
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
    for (const auto& mv : m_measureStore->measureVars())
        if (mv.blockA == id || mv.blockB == id || mv.ownerBlockId == id)
            ++r.measureVarsRemoved;

    // 7. Angle measures referencing the victim (either segment's host).
    for (const auto& am : m_measureStore->angleMeasures())
        if (am.blockA == id || am.blockB == id)
            ++r.angleVarsRemoved;

    // 8. Formulas referencing any measurement name removed above lose their
    //    operand and will report an evaluation error on the next resolve.
    QSet<QString> removedNames;
    for (const auto& lv : m_measureStore->linkedVars())
        if (lv.sourceBlockId == id && !lv.refName.isEmpty())
            removedNames.insert(lv.refName);
    for (const auto& mv : m_measureStore->measureVars())
        if ((mv.blockA == id || mv.blockB == id || mv.ownerBlockId == id)
            && !mv.refName.isEmpty())
            removedNames.insert(mv.refName);
    for (const auto& am : m_measureStore->angleMeasures())
        if ((am.blockA == id || am.blockB == id) && !am.refName.isEmpty())
            removedNames.insert(am.refName);
    if (!removedNames.isEmpty()) {
        for (const auto& f : m_variableStore->formulas()) {
            const QStringList names = ExpressionEvaluator::referencedNames(f.expression);
            for (const QString& n : names) {
                if (removedNames.contains(n)) { ++r.formulasBroken; break; }
            }
        }
    }

    // 9. Dart lines (省道线) that referenced the victim (start pin A or offset
    //    point B) degrade to plain lines, keeping their current geometry.
    for (const auto& b : m_blocks) {
        if (b.id == id) continue;
        if (b.dartStartBlockId == id || b.dartRefBlockId == id)
            ++r.dartLinesDegraded;
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

    // 拖动保护默认开启 (2026-08 用户拍板): 只要建立跟随就保护他。
    // 所有新建连接统一锁定 (焊接语义) —— 拖动任一端时整个对一起移动,
    // 不会被拖拆; 用户可在属性面板手动取消拖动保护。注意 undo/redo 的
    // 快照还原走 addAttachmentRaw/addAttachmentsRaw (verbatim), 不经过
    // 这里, 因此用户手动解锁的状态在撤销后不会被悄悄重新锁定。
    att.isLocked = true;
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
    // NOTE: 组对连接零限制 (模型层组零限制, 2026-08-04 设计定稿) ——
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

void ParamDocument::setAttachmentAngleOnly(const QUuid& id, bool angleOnly)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end() || it->angleOnly == angleOnly)
        return;
    it->angleOnly = angleOnly;
    // 位置自由 ↔ 焊接互斥: 拆开自动解锁; 恢复完整连接重新焊接 (默认保护).
    it->isLocked = !angleOnly;
    // 拆开 (位置全自由) 与滑轨 (一轴自由) 互斥: 拆开时清除滑轨模式.
    it->slideMode = SlideMode::None;
    resolveAll();
}

void ParamDocument::setAttachmentSlideMode(const QUuid& id, SlideMode mode)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end() || it->slideMode == mode)
        return;
    if (mode != SlideMode::None) {
        // Snapshot the locked-axis coordinate from the CURRENT settled
        // geometry BEFORE switching (the follower's from-point projected onto
        // the leader-local rail frame). For a plain full connection this is
        // (0, 0) — the from-point sits exactly on the anchor.
        const Block* from = blockById(it->fromBlockId);
        const Block* to = blockById(it->toBlockId);
        if (from && to) {
            const auto [s, t] = computeSlideOffsets(*from, *it, *to);
            it->slideAlongMm = s;
            it->slidePerpMm = t;
        }
        // 滑轨与拆开/拖动保护互斥: 位置只留一轴自由度 — 必须解锁 (可滑动).
        it->angleOnly = false;
        it->isLocked = false;
        it->slideMode = mode;
    } else {
        // 切回普通全连接: 位置吸附 + 角度跟随恢复, 重新焊接 (只要建立跟随
        // 就保护). 锁轴快照保留 (切回滑轨时按当时几何重快照, 不依赖旧值).
        it->slideMode = SlideMode::None;
        it->angleOnly = false;
        it->isLocked = true;
    }
    resolveAll();
}

void ParamDocument::refreshSlideOffsets(const QUuid& id)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end() || it->slideMode == SlideMode::None)
        return;
    const Block* from = blockById(it->fromBlockId);
    const Block* to = blockById(it->toBlockId);
    if (!from || !to) return;
    const auto [s, t] = computeSlideOffsets(*from, *it, *to);
    it->slideAlongMm = s;
    it->slidePerpMm = t;
    resolveAll();
}

void ParamDocument::updateSlideOffsetsFromCurrent(const QUuid& id)
{
    auto it = std::find_if(m_attachments.begin(), m_attachments.end(),
        [&id](const Attachment& a) { return a.id == id; });
    if (it == m_attachments.end()
        || it->slideMode == SlideMode::None)
        return;
    const Block* from = blockById(it->fromBlockId);
    const Block* to = blockById(it->toBlockId);
    if (!from || !to) return;
    const auto [s, t] = computeSlideOffsets(*from, *it, *to);
    // 只回写自由轴; 锁轴坐标保持激活时快照 (拖动只改变自由轴).
    if (it->slideMode == SlideMode::AlongLeader)
        it->slideAlongMm = s;
    else
        it->slidePerpMm = t;
}

QSet<QUuid> ParamDocument::lockedClosure(const QSet<QUuid>& seed) const
{
    QSet<QUuid> result = seed;
    bool expanded = true;
    while (expanded) {
        expanded = false;
        for (const auto& att : m_attachments) {
            // angleOnly (拆开保留角度) / 滑轨 (slideMode) attachments are
            // position-constrained picks (free / one-axis-free): they must
            // never weld the pair back together for drags.
            if (!att.isLocked || att.angleOnly
                || att.slideMode != SlideMode::None) continue;
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

void ParamDocument::restoreFollowerAttachment(
    const QUuid& fromBlockId, const std::optional<Attachment>& followerAtt)
{
    std::erase_if(m_attachments, [&fromBlockId](const Attachment& a) {
        return !a.isPin && a.fromBlockId == fromBlockId;
    });
    if (followerAtt)
        addAttachmentRaw(*followerAtt);  // verbatim (keeps the snapshot's isLocked)
    resolveAll();
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

// --- Canvas layers (facade: block re-layering + registry removal) ---

void ParamDocument::removeLayer(const QUuid& layerId)
{
    const int n = m_layerRegistry->layerCount();
    if (n <= 2)
        return;  // Need at least aux + one working layer.
    const int row = m_layerRegistry->indexOf(layerId);
    if (row < 0 || m_layerRegistry->isAuxLayer(layerId))
        return;  // Unknown or the auxiliary calculation layer: cannot be removed.

    // Blocks in the removed layer fall to the layer below, but never into the
    // auxiliary layer. Blocks in other layers keep their stable ids untouched
    // (removal no longer shifts any references).
    QUuid targetId = row > 0 ? m_layerRegistry->layers()[static_cast<size_t>(row - 1)].id
                             : QUuid();
    if (targetId.isNull() || m_layerRegistry->isAuxLayer(targetId))
        targetId = m_layerRegistry->firstWorkingLayerId();
    for (auto& b : m_blocks) {
        if (b.layer == layerId)
            b.layer = targetId;
    }
    m_layerRegistry->removeLayerRaw(layerId);
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
    for (const auto& mv : m_measureStore->measureVars())
        if (checkVar(mv.refName, mv.ownerBlockId, {mv.blockA, mv.blockB}))
            return true;
    for (const auto& lv : m_measureStore->linkedVars())
        if (checkVar(lv.refName, QUuid(), {lv.sourceBlockId}))
            return true;
    for (const auto& am : m_measureStore->angleMeasures())
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
    // Dart-line references (省道线): the start pin A and the offset point B
    // must both re-solve (the dart block re-computes its transform) whenever
    // their host blocks move.
    if (b.dartStartBlockId == targetBlockId || b.dartRefBlockId == targetBlockId)
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
    if (m_crossLayerCount > 0)
        m_layerRegistry->invalidateAllLayers();

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
    if (!m_layerRegistry->dirtyAnnotated())
        m_layerRegistry->invalidateAllLayers();
    m_layerRegistry->clearDirtyAnnotation();

    // Invariant: the aux layer id comes from the registry (element 0).
    const QUuid kAuxLayer = m_layerRegistry->auxLayerId();

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

    // Component (组件) snapshots: assembled once per resolve so active
    // components are driven by their single hinge.
    const std::vector<Component> components = m_groupRegistry->components();

    // Dirty-subgraph narrowing: starts as the caller-provided subset; upgraded
    // to null (full resolve) the moment a measurement changes, because a
    // measured value can feed formulas in blocks OUTSIDE the subset.
    const QSet<QUuid>* effAffected = affectedOnly;

    // ── Phase 1: auxiliary calculation layer (only when dirty) ──
    // Aux geometry is a pure function of the variables; during working-layer
    // manipulation it stays frozen and its cached transforms remain valid.
    bool auxRan = false, workingRan = false;
    if (m_layerRegistry->auxDirty()) {
        GCAD_PERF_SCOPE("resolve.aux");
        std::vector<ResolveDiagnostic> auxDiag;  // discarded (phase 2 owns m_diagnostics)
        measureLinkedVars();   // feed aux formulas before resolving (old semantics)
        measureMeasureVars();
        measureAngleMeasureVars();
        Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                             &auxDiag, Resolver::Scope::AuxOnly, kAuxLayer,
                             effAffected, components);
        for (int i = 0; i < 4 && (measureLinkedVars() || measureMeasureVars()
                                  || measureAngleMeasureVars()); ++i) {
            if (effAffected) effAffected = nullptr;  // measurement changed → full
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters,
                                 m_conditioned, &auxDiag,
                                 Resolver::Scope::AuxOnly, kAuxLayer,
                                 effAffected, components);
        }
        m_layerRegistry->setAuxDirty(false);
        auxRan = true;
        // Published measurement values sourced from the aux layer may have
        // changed → working layers must re-measure and re-resolve.
        m_layerRegistry->setWorkingDirty(true);
    }

    // ── Phase 2: working layers ──
    // Extracted into a closure so the Phase 3 cross-layer fixpoint can
    // re-run it when settling aux followers perturbs published measurements.
    auto runWorkingPhase = [&]() {
        if (!m_layerRegistry->workingDirty()) return;
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
                             effAffected, components);
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
                                 effAffected, components);
        }
        m_layerRegistry->setWorkingDirty(false);
        workingRan = true;
    };
    runWorkingPhase();

    // ── Phase 2.5: cross-layer intersection re-solve (跨层交点重解) ──
    // An aux-layer intersection whose ray origin / borrow point lives on a
    // WORKING layer was solved in Phase 1 against the STALE working position
    // (the working layers only move in Phase 2). Without a re-solve the
    // intersection drifts off the origin→borrow ray (用户回归: P612 在
    // 肩褶高 15/20 时不共线). Detect the dependency (cheap: aux intersections
    // only) and re-run the aux pass with the fresh working values; changed
    // aux measurements re-trigger the working pass, bounded like Phase 3.
    if (auxRan && workingRan) {
        bool auxRefsWorking = false;
        for (const auto& blk : m_blocks) {
            if (isAuxLayer(blk.layer)) continue;
            for (const auto& ob : m_blocks) {
                if (!isAuxLayer(ob.layer)) continue;
                for (const auto& pt : ob.points) {
                    if (pt.constraint != PointConstraint::Intersection) continue;
                    for (const QUuid& ref : {pt.refPointA, pt.interAimPointId}) {
                        if (ref.isNull()) continue;
                        if (blk.findPoint(ref)) { auxRefsWorking = true; break; }
                    }
                    if (auxRefsWorking) break;
                }
                if (auxRefsWorking) break;
            }
            if (auxRefsWorking) break;
        }
        if (auxRefsWorking) {
            for (int round = 0; round < 4; ++round) {
                std::vector<ResolveDiagnostic> auxDiag2;  // discarded
                Resolver::resolveAll(m_blocks, *passAttachments, m_parameters,
                                     m_conditioned, &auxDiag2,
                                     Resolver::Scope::AuxOnly, kAuxLayer,
                                     nullptr, components);
                if (!(measureLinkedVars() || measureMeasureVars()
                      || measureAngleMeasureVars()))
                    break;
                m_layerRegistry->setWorkingDirty(true);
                runWorkingPhase();
            }
        }
    }

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
                                 effAffected, components);
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
            m_layerRegistry->setWorkingDirty(true);
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
                                 effAffected, components);
            m_diagnostics.push_back({ResolveDiagnostic::Kind::NotConverged, QUuid()});
        }
    }

    // --- Curve-anchor follow post-pass ---
    // CurveAnchor points with a follow connection track their target point:
    // recompute chord-relative params so the anchor stays at target + offset.
    // Only blocks in layer groups that actually re-resolved are touched.
    // NOTE: this pass runs AFTER every Resolver pass (including the attachment
    // settle), so an anchor moved here leaves attached followers on the OLD
    // anchor position for the rest of this frame — the final drag frame would
    // show the follower one frame behind (曲线点连接不跟随, 用户报告 2026-08).
    // followMoved tracks that case so the re-settle below closes the gap.
    bool followMoved = false;
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
            const geo::Vec2 newPos = sp->resolvedPos
                                   + unitDir * (len * pt.interpPercent)
                                   + normal * pt.interpOffsetDist;
            // The anchor moved — bump the epoch so the canvas rebuilds the
            // curve cache this frame (Block::resolve's own epoch bump already
            // ran BEFORE this post-pass, so without this the curve would keep
            // passing through the OLD anchor until the next resolve).
            if (pt.resolvedPos.distanceSquaredTo(newPos) > 1e-6) {
                ++blk.geometryEpoch;
                followMoved = true;
            }
            pt.resolvedPos = newPos;
            pt.resolved = true;
        }
    }

    // ── Curve-anchor follow re-settle (曲线点连接同帧跟随, 2026-08) ──
    // The post-pass moved anchor(s) AFTER the attachment settle. Re-run the
    // same scoped resolve(s) (affected-narrowed) so followers attached to the
    // moved anchors land on the fresh positions in THIS frame — and the curve
    // span cache is rebuilt against the moved anchor (exit-direction lookups
    // stay consistent). Bounded: the re-settle is a plain Resolver pass and
    // never re-enters this post-pass (it lives below), so no recursion.
    if (followMoved) {
        if (workingRan) {
            GCAD_PERF_SCOPE("resolve.followResettle");
            std::vector<ResolveDiagnostic> reDiag;  // discarded (phase 2 owns m_diagnostics)
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                                 &reDiag, Resolver::Scope::WorkingOnly, kAuxLayer,
                                 effAffected, components);
        }
        if (auxRan || xLayerMoved) {
            GCAD_PERF_SCOPE("resolve.followResettleAux");
            std::vector<ResolveDiagnostic> reDiag;  // discarded
            Resolver::resolveAll(m_blocks, *passAttachments, m_parameters, m_conditioned,
                                 &reDiag, Resolver::Scope::AuxOnly, kAuxLayer,
                                 effAffected, components);
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
    m_variableStore->clear();
    m_measureStore->clear();
    m_groupRegistry->clear();
    m_layerRegistry->reset();
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

} // namespace cad::param
