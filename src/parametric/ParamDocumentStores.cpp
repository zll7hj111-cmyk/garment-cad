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
#include "parametric/IntersectDebug.h"
#include "parametric/LayerRegistry.h"
#include "parametric/VariableStore.h"
#include "parametric/MeasurementStore.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::param {

// 子域存储门面转发：变量/公式/公式组/图层/联动/测量 CRUD + *Raw 恢复 (2026-08 拆分)

void ParamDocument::addVariable(Variable var) { m_variableStore->addVariable(std::move(var)); }
void ParamDocument::removeVariable(const QUuid& id) { m_variableStore->removeVariable(id); }
void ParamDocument::updateVariable(const Variable& var) { m_variableStore->updateVariable(var); }
const std::vector<Variable>& ParamDocument::variables() const { return m_variableStore->variables(); }
Variable* ParamDocument::findVariable(const QUuid& id) { return m_variableStore->findVariable(id); }
const Variable* ParamDocument::findVariable(const QUuid& id) const { return m_variableStore->findVariable(id); }

void ParamDocument::addFormula(FormulaVariable formula) { m_variableStore->addFormula(std::move(formula)); }
void ParamDocument::removeFormula(const QUuid& id) { m_variableStore->removeFormula(id); }
void ParamDocument::updateFormula(const FormulaVariable& formula) { m_variableStore->updateFormula(formula); }
const std::vector<FormulaVariable>& ParamDocument::formulas() const { return m_variableStore->formulas(); }
FormulaVariable* ParamDocument::findFormula(const QUuid& id) { return m_variableStore->findFormula(id); }
const FormulaVariable* ParamDocument::findFormula(const QUuid& id) const { return m_variableStore->findFormula(id); }
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
const FormulaGroup* ParamDocument::findFormulaGroup(const QUuid& groupId) const { return m_variableStore->findFormulaGroup(groupId); }

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

// --- Trusted-pipeline channel (P1-2) -------------------------------------
// The *Raw members above are PRIVATE; RawModelAccess is the single friend that
// may call them, so every silent-restore site names the trust explicitly.
QUuid RawModelAccess::addBlockRaw(ParamDocument& doc, Block block)
{ return doc.addBlockRaw(std::move(block)); }
void RawModelAccess::addAttachmentRaw(ParamDocument& doc, Attachment att)
{ doc.addAttachmentRaw(std::move(att)); }
void RawModelAccess::addAttachmentsRaw(ParamDocument& doc,
                                       const std::vector<Attachment>& atts)
{ doc.addAttachmentsRaw(atts); }
void RawModelAccess::addFreePointRaw(ParamDocument& doc, ParamPoint pt)
{ doc.addFreePointRaw(std::move(pt)); }
void RawModelAccess::replaceLayersRaw(ParamDocument& doc, std::vector<Layer> layers)
{ doc.replaceLayersRaw(std::move(layers)); }
void RawModelAccess::restoreVariableRaw(ParamDocument& doc, Variable var)
{ doc.restoreVariableRaw(std::move(var)); }
void RawModelAccess::restoreFormulaRaw(ParamDocument& doc, FormulaVariable formula)
{ doc.restoreFormulaRaw(std::move(formula)); }
void RawModelAccess::restoreFormulaGroupRaw(ParamDocument& doc, FormulaGroup group)
{ doc.restoreFormulaGroupRaw(std::move(group)); }
void RawModelAccess::insertFormulaGroupAt(ParamDocument& doc, int index,
                                          FormulaGroup group)
{ doc.insertFormulaGroupAt(index, std::move(group)); }
void RawModelAccess::restoreLinkedRaw(ParamDocument& doc, LinkedVariable lv)
{ doc.restoreLinkedRaw(std::move(lv)); }
void RawModelAccess::restoreMeasureRaw(ParamDocument& doc, MeasureVariable mv)
{ doc.restoreMeasureRaw(std::move(mv)); }
void RawModelAccess::restoreAngleMeasureRaw(ParamDocument& doc,
                                            AngleMeasureVariable am)
{ doc.restoreAngleMeasureRaw(std::move(am)); }
void RawModelAccess::restoreComponentRaw(ParamDocument& doc, Component comp)
{ doc.restoreComponentRaw(std::move(comp)); }
void RawModelAccess::publishParamsRaw(ParamDocument& doc,
                                      const QHash<QString, double>& cmValues)
{ doc.publishParamsRaw(cmValues); }

} // namespace cad::param