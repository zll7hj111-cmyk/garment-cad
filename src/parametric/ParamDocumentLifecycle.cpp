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

// 文档生命周期：clear / 序列计数 / *Raw 追加 / finishRestore (2026-08 拆分)

void ParamDocument::clear()
{
    m_parameters.clear();
    m_formulaParamNames.clear();
    m_conditioned.clear();
    m_freePoints.clear();
    m_blocks.clear();
    bumpStructureEpoch();   // P1-3: every previously handed-out Block* is gone
    m_blockIndex.clear();
    m_attachments.clear();
    m_followersOf.clear();
    m_followersDirty = true;
    m_referenceIndex.clear();
    m_referenceIndexDirty = true;
    m_components.clear();
    m_blockToComponent.clear();
    m_crossLayerCount = 0;
    m_diagnostics.clear();
    // P1-5: the document owns its expression bytecode — release it with the
    // rest of the model (no evaluation is in flight at a lifecycle boundary).
    m_exprCache.clear();
    m_variableStore->clear();
    m_measureStore->clear();
    m_layerRegistry->reset();
    m_nextPointSeq = 1;
    m_nextLineSeq  = 1;

    // P0-1 (ARCHITECTURE_REVIEW): undo 栈与文档生命周期同步 —— 清空模型必须
    // 同步清空 undo 历史。此前 clear() 保留栈: 打开/新建文档时旧文档的命令
    // 快照留在栈里, 在新文档里 undo 会"复活"旧命令（跨文档污染）。先删命令
    // 再发重置信号, 避免信号槽在新旧文档状态交替时被触发。
    if (m_undoStack) {
        m_undoStack->clear();
        m_undoStack->setClean();
    }

    // Notify the UI so the canvas and all panels drop their stale content.
    // documentReset is consumed by CanvasScene; the variable panels need
    // their own structural signals (measure cards would otherwise survive a
    // File→New), and LayerPanel rebuilds on layersChanged (its layer
    // registry was just regenerated).
    emit documentReset();
    emit variablesChanged();
    emit formulasChanged();
    emit formulaGroupsChanged();
    emit linkedVarsChanged();
    emit measureVarsChanged();
    emit angleMeasureVarsChanged();
    emit layersChanged();
    emit componentsChanged();
}

void ParamDocument::setSerialCounters(int pointSeq, int lineSeq)
{
    m_nextPointSeq = pointSeq;
    m_nextLineSeq  = lineSeq;
}

QUuid ParamDocument::addBlockRaw(Block block)
{
    QUuid id = block.id;
    m_blockIndex.insert(id, static_cast<int>(m_blocks.size()));
    m_blocks.push_back(std::move(block));
    bumpStructureEpoch();  // P1-3
    m_followersDirty = true;  // batch restore: edge table rebuilt on demand
    m_referenceIndexDirty = true;  // batch restore: reference index rebuilt on demand
    return id;
}

void ParamDocument::addAttachmentRaw(Attachment att)
{
    m_attachments.push_back(std::move(att));
    recountCrossLayerAttachments();
    m_followersDirty = true;  // batch restore: edge table rebuilt on demand
    // 组件级连接经 undo/反序列化 verbatim 恢复 → 暴露端点同步 (自动暴露).
    if (!att.fromComponentId.isNull()) {
        if (Component* c = findComponent(att.fromComponentId))
            recordExposedEndpoint(*c, att);
    }
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

    // Rebuild the variable / formula / linked panels with the restored data.
    emit variablesChanged();
    emit formulasChanged();
    emit linkedVarsChanged();
    emit componentsChanged();
    emit structureChanged();
}

} // namespace cad::param