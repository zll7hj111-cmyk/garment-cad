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

namespace cad::param {

ParamDocument::ParamDocument(QObject* parent)
    : QObject(parent)
    , m_undoStack(new QUndoStack(this))
    , m_layerRegistry(std::make_unique<LayerRegistry>(this))
    , m_variableStore(std::make_unique<VariableStore>(this, this))
    , m_measureStore(std::make_unique<MeasurementStore>(this, this))
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

    // P2-5: bound the history. Commands carry FULL model snapshots (and a
    // delete command snapshots its whole cascade subgraph), so an unbounded
    // stack grows without limit in a long editing session. 150 steps is far
    // beyond any realistic "I want that back" reach, and QUndoStack drops the
    // oldest command on its own once the limit is hit.
    m_undoStack->setUndoLimit(kUndoStackLimit);
}

ParamDocument::~ParamDocument() = default;

// --- Sub-domain registry access (read-only escape hatch) ---

const LayerRegistry& ParamDocument::layerRegistry() const { return *m_layerRegistry; }
const VariableStore& ParamDocument::variableStore() const { return *m_variableStore; }
const MeasurementStore& ParamDocument::measurementStore() const { return *m_measureStore; }

} // namespace cad::param