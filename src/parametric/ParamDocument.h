#pragma once

#include <QObject>
#include <QUuid>
#include <QHash>
#include <QSet>
#include <QUndoStack>
#include <memory>
#include <vector>

#include "parametric/Block.h"
#include "parametric/Component.h"
#include "parametric/Attachment.h"
#include "parametric/ParamPoint.h"
#include "parametric/Resolver.h"
#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/FormulaGroup.h"
#include "parametric/Layer.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "parametric/AngleMeasureVariable.h"
#include "parametric/Condition.h"

namespace cad::param {

class LayerRegistry;
class VariableStore;
class MeasurementStore;

/// The parametric document holds all Blocks, free points, attachments,
/// and global parameters. It manages resolve and undo/redo.
///
/// Facade over the geometry core (blocks / attachments / free points /
/// parameters / resolve pipeline) and four sub-domain registries:
/// LayerRegistry (canvas layers + dirty marking), VariableStore (variables /
/// formulas / formula groups), MeasurementStore (linked / measure / angle
/// measures). Sub-domain signals are
/// re-emitted unchanged so observers keep a single connection point.
class ParamDocument : public QObject
{
    Q_OBJECT

public:
    explicit ParamDocument(QObject* parent = nullptr);
    ~ParamDocument() override;

    // --- Global parameters (formula variables) ---
    /// Parameters are stored in cm — the formula domain unit.
    /// User-facing formulas (e.g. "b/4+0.6") evaluate against these cm values,
    /// and Block::resolve() converts distance formula results back to mm.
    void setParameter(const QString& name, double value);
    /// Batch-set multiple parameters with a single resolveAll() at the end.
    void setParameters(const QHash<QString, double>& nameValues);
    void removeParameter(const QString& name);
    /// Replace all formula-derived parameters with the given map (values in cm).
    /// Stale names from the previous call are removed automatically.
    void syncFormulaParameters(const QHash<QString, double>& cmValues);
    /// Store the per-formula condition table (formulaName -> conditions) used for
    /// standalone-condition semantics. Store-only: the resolve happens via the
    /// accompanying syncFormulaParameters() call.
    void syncFormulaConditions(const QHash<QString, QList<Condition>>& conditioned);
    [[nodiscard]] double parameter(const QString& name, double defaultVal = 0.0) const;
    [[nodiscard]] const QHash<QString, double>& parameters() const { return m_parameters; }
    /// Per-formula condition table (formulaName -> conditions). Read-only access
    /// so commands can evaluate formula texts with the same semantics as the
    /// Resolver (e.g. BreakSegmentCommand re-evaluating a break offset).
    [[nodiscard]] const QHash<QString, QList<Condition>>& conditions() const
    { return m_conditioned; }

    // --- Free-standing points (not belonging to any Block) ---
    void addFreePoint(ParamPoint pt);
    void removeFreePoint(const QUuid& id);
    [[nodiscard]] ParamPoint* findFreePoint(const QUuid& id);
    [[nodiscard]] const std::vector<ParamPoint>& freePoints() const { return m_freePoints; }

    // --- Block management ---
    QUuid addBlock(Block block);
    void removeBlock(const QUuid& id);
    [[nodiscard]] Block* findBlock(const QUuid& id);
    [[nodiscard]] const Block* findBlock(const QUuid& id) const;

    /// Predicted consequences of removing one block — every counter mirrors
    /// one cascade branch of removeBlock() (删除影响报告). Kept in sync with
    /// the cleanup logic; advisory only (the deletion itself is unconditional).
    struct DeleteImpact {
        int attachmentsRemoved   = 0;  ///< 连接数（attachment）。
        int bridgesReleased      = 0;  ///< 释放为独立线段的桥接线（保留几何）。
        int intersectionsFrozen  = 0;  ///< 冻结在原位置的交点（射线原点消失）。
        int intersectionsAimCleared = 0; ///< 交点回退为固定角度的指向点（指向点被删）。
        int linkedFrozen         = 0;  ///< 长度引用固化为数值的段/点（引用对象被删）。
        int linkedVarsRemoved    = 0;  ///< 删除的关联长度变量。
        int measureVarsRemoved   = 0;  ///< 删除的测量变量（不可恢复）。
        int angleVarsRemoved     = 0;  ///< 删除的角度测量变量（不可恢复）。
        int formulasBroken       = 0;  ///< 引用被删测量名的公式（将失效报错）。
        int dartLinesDegraded    = 0;  ///< 失去起点/偏移点而降级为普通线的省道线。

        [[nodiscard]] bool hasImpact() const
        {
            return attachmentsRemoved > 0 || bridgesReleased > 0 ||
                   intersectionsFrozen > 0 || intersectionsAimCleared > 0 ||
                   linkedFrozen > 0 ||
                   linkedVarsRemoved > 0 || measureVarsRemoved > 0 ||
                   angleVarsRemoved > 0 || formulasBroken > 0 ||
                   dartLinesDegraded > 0;
        }
        DeleteImpact& operator+=(const DeleteImpact& o)
        {
            attachmentsRemoved  += o.attachmentsRemoved;
            bridgesReleased     += o.bridgesReleased;
            intersectionsFrozen += o.intersectionsFrozen;
            intersectionsAimCleared += o.intersectionsAimCleared;
            linkedFrozen        += o.linkedFrozen;
            linkedVarsRemoved   += o.linkedVarsRemoved;
            measureVarsRemoved  += o.measureVarsRemoved;
            angleVarsRemoved    += o.angleVarsRemoved;
            formulasBroken      += o.formulasBroken;
            dartLinesDegraded   += o.dartLinesDegraded;
            return *this;
        }
    };

    /// Delete-impact report for @p id (prediction, no mutation).
    [[nodiscard]] DeleteImpact deleteImpactReport(const QUuid& id) const;
    [[nodiscard]] const std::vector<Block>& blocks() const { return m_blocks; }
    /// O(1) block lookup by id (returns nullptr if not found).
    [[nodiscard]] Block* blockById(const QUuid& id);
    [[nodiscard]] const Block* blockById(const QUuid& id) const;

    // --- Components (组件: packaged work groups) ---
    /// Add a component. Validates that every member exists and updates the
    /// reverse block→component index. Emits componentsChanged() +
    /// structureChanged() + documentChanged().
    QUuid addComponent(Component comp);
    /// Insert a component VERBATIM without validation or signals (deserialize
    /// / undo replay).
    void restoreComponentRaw(Component comp);
    /// Dissolve: remove the component record only; members become independent
    /// segments again (their internal attachments revive). Emits
    /// componentsChanged().
    void removeComponentRecord(const QUuid& id);
    /// Update metadata (name / bounding box / exposed endpoint / default
    /// angle). Geometry is unchanged, so no resolve. Emits componentsChanged().
    void updateComponent(const Component& comp);
    [[nodiscard]] const std::vector<Component>& components() const { return m_components; }
    [[nodiscard]] Component* findComponent(const QUuid& id);
    [[nodiscard]] const Component* findComponent(const QUuid& id) const;
    /// Reverse lookup: the component @p blockId belongs to (nullptr if none).
    [[nodiscard]] Component* componentOfBlock(const QUuid& blockId);
    [[nodiscard]] const Component* componentOfBlock(const QUuid& blockId) const;
    /// World-space AABB over every member's resolved geometry (points + curve hull).
    [[nodiscard]] BBox boundingBoxOf(const QUuid& componentId) const;
    /// Expand a seed set to the full component closure: every member of every
    /// component that intersects @p seed. Whole-component drag/rotate and dirty
    /// propagation all ride this single expansion.
    [[nodiscard]] QSet<QUuid> componentClosure(const QSet<QUuid>& seed) const;
    /// Member block owning @p pointId inside @p comp (null id when none).
    [[nodiscard]] QUuid memberOwningPoint(const Component& comp, const QUuid& pointId) const;

    // --- Attachment management ---
    /// Add an attachment (follower 跟随线 snaps to leader 基准线).
    /// Enforces the forest invariant (see Attachment.h glossary): rejected
    /// (returns false, nothing added) when a referenced block is missing,
    /// the from-block already follows another leader, or the link would
    /// close a cycle.
    ///
    /// Cross-layer rule (单向跨层附着, one-way only): an auxiliary-layer
    /// follower may attach to a working-layer leader (aux → working) — the
    /// Resolver settles those followers in Phase 3 after the working layers.
    /// The REVERSE direction (working follower → aux leader) stays rejected:
    /// working geometry must never be driven by the frozen calculation draft.
    /// Also rejected when the new edge would close a VALUE cycle through a
    /// measurement variable (see wouldCreateMeasureValueCycle).
    bool addAttachment(Attachment att);
    void removeAttachment(const QUuid& id);
    /// Toggle the 拖动保护 flag of an existing attachment. Protected
    /// connections are welded: dragging cannot tear them apart, and dragging
    /// either side moves the whole pair. Resolves once.
    void setAttachmentLocked(const QUuid& id, bool locked);
    /// 拆开保留角度 (用户拍板 2026-08): convert an existing attachment to
    /// angle-only mode — the follower keeps following the leader's ANGLE
    /// (rotation driven by leader direction + followerAngle) while its
    /// position constraint is released (the line moves freely). Converting
    /// TO angle-only clears 拖动保护 (isLocked) — welded dragging is
    /// incompatible with a free position; converting BACK re-welds (只要
    /// 建立跟随就保护). Resolves once.
    void setAttachmentAngleOnly(const QUuid& id, bool angleOnly);
    /// 位置吸附保持、角度独立 (用户新需求 2026): 连接仍把 from-point 钉在
    /// leader 点上, 但 Resolver 不再驱动跟随线旋转 —— 本线角度保持自己的
    /// 当前角度/公式。与 angleOnly (位置自由) 和 slideMode (一轴滑轨) 互斥。
    void setAttachmentAngleIndependent(const QUuid& id, bool angleIndependent);
    /// 滑轨模式 (抽屉式滑动, 用户拍板 2026-08): 切换 Attachment 的 slideMode。
    /// 进入滑轨 (AlongLeader / PerpLeader): 先按当前几何快照锁轴坐标
    /// (slideAlongMm/slidePerpMm = 跟随点相对基准线局部系的当前投影), 再清
    /// angleOnly 并解除拖动保护 (位置必须可滑动)。切回 None: 恢复完整连接
    /// 位置锚点与角度基准分离 (用户需求 2026): 设置角度参考线段/点。
    /// refBlockId/refSegmentId/refPointId 为空 = 恢复默认 (角度跟随位置宿主)。
    void setAttachmentAngleRef(const QUuid& id,
                               const QUuid& refBlockId,
                               const QUuid& refSegmentId,
                               const QUuid& refPointId = QUuid());
    /// (angleOnly=false, 重新焊接 isLocked=true)。Resolves once.
    void setAttachmentSlideMode(const QUuid& id, cad::param::SlideMode mode);
    /// 重快照滑轨锁轴坐标 (重定向基准线指向点后调用): 按当前几何把
    /// slideAlongMm/slidePerpMm 重新投影到新基准线局部系。Resolves once.
    void refreshSlideOffsets(const QUuid& id);
    /// 拖动回写 (拖拽工具每帧调用): 把跟随线当前 from-point 的世界位置投影
    /// 到基准线局部系, 只回写**自由轴**坐标 —— AlongLeader 写 slideAlongMm,
    /// PerpLeader 写 slidePerpMm —— 锁轴坐标保持激活时快照。不触发 resolve
    /// (随后的 resolveForDrag / resolveAll 会按新坐标落位)。滑轨未激活时 no-op。
    void updateSlideOffsetsFromCurrent(const QUuid& id);
    /// Expand a seed set of block ids to include every block welded by LOCKED
    /// attachments (递归焊接闭包): dragging any member moves the whole closure
    /// (A锁B、B锁C → 拖A时B、C一起走). Also the basis of the drag-time
    /// auto-disconnect decision (both sides inside the closure = never torn).
    [[nodiscard]] QSet<QUuid> lockedClosure(const QSet<QUuid>& seed) const;
    /// Batch variant of removeAttachment(): sever every listed attachment in
    /// ONE pass (one resolve + one structureChanged), with the same bridge
    /// release cascade. Used by MakeGroupCommand to cut K links cheaply
    /// instead of K full re-solves.
    void removeAttachments(const QList<QUuid>& ids);
    /// Insert attachments VERBATIM without revalidation, resolving once.
    /// Undo-replay only — the snapshot is the ground truth (快照完整性铁律);
    /// revalidating restored state would let later edits win the undo race.
    void addAttachmentsRaw(const std::vector<Attachment>& atts);
    [[nodiscard]] const std::vector<Attachment>& attachments() const { return m_attachments; }
    /// Find an attachment by id. The mutable overload is the ONLY authorized
    /// in-place attachment edit channel (e.g. welding a follower angle during
    /// an aim-release back-solve); structural add/remove still goes through
    /// addAttachment()/removeAttachment().
    [[nodiscard]] Attachment* findAttachment(const QUuid& id);
    [[nodiscard]] const Attachment* findAttachment(const QUuid& id) const;

    /// Remove every attachment that references the given block (as leader or
    /// follower) while keeping the block geometry. Returns the number of
    /// attachments removed.
    int removeAttachmentsOfBlock(const QUuid& blockId);

    /// 撤销全部 (dialog reject): drop every non-pin follower attachment of
    /// @p fromBlockId and restore @p followerAtt VERBATIM if set (keeps the
    /// snapshot's isLocked — 快照完整性). Resolves once; the caller drives
    /// the UI refresh.
    void restoreFollowerAttachment(const QUuid& fromBlockId,
                                   const std::optional<Attachment>& followerAtt);

    /// Bridge blocks (Block::isBridge) that have a pin attachment TO the given
    /// host block. Deleting the host releases these bridges (they lose a pin
    /// and become independent segments). Used by DeleteBlockCommand to
    /// snapshot the affected bridges for undo.
    [[nodiscard]] std::vector<QUuid> bridgesPinnedTo(const QUuid& hostBlockId) const;

    // --- Readable serials ---
    /// Generate a fresh human-readable serial for a point / line.
    [[nodiscard]] QString newPointSerial();
    [[nodiscard]] QString newLineSerial();

    // --- Sub-domain registry access (read-only escape hatch) ---
    /// Read-only views of the sub-domain registries. Mutations MUST go
    /// through the facade methods (checked paths) or the explicit *Raw
    /// restore APIs (trusted batch pipelines) — no writable container
    /// reference leaves the facade.
    [[nodiscard]] const LayerRegistry& layerRegistry() const;
    [[nodiscard]] const VariableStore& variableStore() const;
    [[nodiscard]] const MeasurementStore& measurementStore() const;

    // --- Variables (plain value variables) ---
    void addVariable(Variable var);
    void removeVariable(const QUuid& id);
    void updateVariable(const Variable& var);
    [[nodiscard]] const std::vector<Variable>& variables() const;
    [[nodiscard]] Variable* findVariable(const QUuid& id);

    // --- Formula variables ---
    void addFormula(FormulaVariable formula);
    void removeFormula(const QUuid& id);
    void updateFormula(const FormulaVariable& formula);
    [[nodiscard]] const std::vector<FormulaVariable>& formulas() const;
    [[nodiscard]] FormulaVariable* findFormula(const QUuid& id);

    /// Re-evaluate all formulas against current variables, update cached values,
    /// and sync results into the parameter map + resolve.
    void recomputeFormulas();

    // --- Formula groups (panel folders for formula variables) ---
    void addFormulaGroup(FormulaGroup group);
    /// Dissolve a group: members become ungrouped, the group is removed.
    void removeFormulaGroup(const QUuid& groupId);
    void renameFormulaGroup(const QUuid& groupId, const QString& name);
    /// Collapse/expand toggle (view state, persisted; not undoable).
    void setFormulaGroupCollapsed(const QUuid& groupId, bool collapsed);
    /// Reorder groups within the registry.
    void moveFormulaGroup(int fromIndex, int toIndex);
    /// Move a formula to targetGroupId (may be null = ungrouped) at the given
    /// local index within that group's display sequence.
    void moveFormula(const QUuid& formulaId, const QUuid& targetGroupId,
                     int targetLocalIndex);
    [[nodiscard]] const std::vector<FormulaGroup>& formulaGroups() const;
    [[nodiscard]] FormulaGroup* findFormulaGroup(const QUuid& groupId);

    // --- Canvas layers (selection/visibility filter + aux calculation layer) ---
    /// The layer registry in DISPLAY ORDER. Element 0 is always the single
    /// auxiliary calculation layer; the rest are working layers. Always
    /// contains at least two layers. Blocks reference layers by STABLE id
    /// (Block::layer = Layer::id), never by display row.
    [[nodiscard]] const std::vector<Layer>& layers() const;
    [[nodiscard]] int layerCount() const;
    /// Display row of @p layerId (-1 when absent).
    [[nodiscard]] int layerIndex(const QUuid& layerId) const;
    /// Layer with @p layerId (nullptr when absent).
    [[nodiscard]] const Layer* layerById(const QUuid& layerId) const;
    /// Id of the (single) auxiliary calculation layer.
    [[nodiscard]] QUuid auxLayerId() const;
    /// Id of the first working layer (always present).
    [[nodiscard]] QUuid firstWorkingLayerId() const;
    /// Append a WORKING layer and return its id. Emits layersChanged().
    QUuid addLayer(const QString& name);
    /// Remove a layer: its blocks move to the layer below (never into the
    /// auxiliary layer) and the layer record is dropped. Other blocks keep
    /// their stable layer ids untouched. The auxiliary layer and the last
    /// working layer cannot be removed (no-op).
    void removeLayer(const QUuid& layerId);
    /// Re-insert a layer record at display row @p index (undo support; the
    /// record keeps its original id).
    void insertLayerAt(int index, Layer layer);
    void renameLayer(const QUuid& layerId, const QString& name);
    /// Toggle layer visibility. Hiding the active layer auto-switches the
    /// active layer to the nearest visible one. Emits layersChanged() (and
    /// activeLayerChanged() if the active layer moved).
    void setLayerVisible(const QUuid& layerId, bool visible);
    [[nodiscard]] bool layerVisible(const QUuid& layerId) const;
    [[nodiscard]] QUuid activeLayer() const;
    void setActiveLayer(const QUuid& layerId);

    /// True when @p layerId is the auxiliary calculation layer.
    [[nodiscard]] bool isAuxLayer(const QUuid& layerId) const;
    /// True when the block lives on the auxiliary layer.
    [[nodiscard]] bool isAuxBlock(const Block& b) const { return isAuxLayer(b.layer); }
    /// Effective visibility for RENDERING. Any non-active layer renders
    /// GRAYED (BlockItem::LayerMode::Grayed) — including the auxiliary layer,
    /// so its construction geometry stays visible as a reference draft.
    /// Only layers manually hidden (layerVisible == false) are not rendered.
    [[nodiscard]] bool layerEffectivelyVisible(const QUuid& layerId) const;

    /// Whether the layer's points/segments may be SNAP targets. Non-active
    /// WORKING layers stay snappable (grayed reference — new geometry can
    /// connect to them). The aux layer is ONLY snappable while it is the
    /// active layer: from a working layer its geometry is a reference draft,
    /// and snapping to it would produce a cross-group attachment that
    /// addAttachment() rejects — snap targets must never be connectable
    /// (capture the cursor, then fail to connect = interaction trap).
    [[nodiscard]] bool layerSnappable(const QUuid& layerId) const;

    // --- Layered dirty marking (resolve pipeline optimisation) ---
    /// Mark the layer group (aux vs working) containing layer @p index as
    /// needing re-resolution. Accumulates until consumed by resolveAll().
    /// Hot paths (per-frame drags) call this to narrow the resolve scope;
    /// un-annotated resolveAll() calls fall back to full re-resolution.
    void invalidateLayer(const QUuid& layerId);
    /// Mark every layer group dirty (variable edits, structural changes).
    void invalidateAllLayers();

    /// True when the document contains at least one cross-layer attachment
    /// (aux follower → working leader). Maintained incrementally so resolve
    /// frames can short-circuit the Phase 3 cross-layer settle at zero cost
    /// for the (overwhelmingly common) no-cross-layer case.
    [[nodiscard]] bool hasCrossLayerAttachments() const { return m_crossLayerCount > 0; }

    // --- Linked variables (geometric measurements) ---
    void addLinked(LinkedVariable lv);
    void removeLinked(const QUuid& id);
    void updateLinked(const LinkedVariable& lv);
    [[nodiscard]] const std::vector<LinkedVariable>& linkedVars() const;
    [[nodiscard]] LinkedVariable* findLinked(const QUuid& id);
    /// Find a linked variable by its source segment (nullptr if not published).
    [[nodiscard]] LinkedVariable* findLinkedBySource(const QUuid& blockId,
                                                     const QUuid& segmentId);

    /// Re-measure all linked variables from their source geometry, refresh
    /// cached values, and sync into the parameter map (cm domain).
    /// Called internally by resolveAll() around the Resolver pass.
    /// @param skipAuxSource When true, variables whose source block sits on the
    ///        (clean) auxiliary layer keep their cached value — the aux layer
    ///        was not re-resolved, so its geometry cannot have changed.
    /// Returns true if any measured value changed (consumers need re-resolve).
    bool measureLinkedVars(bool skipAuxSource = false);

    // --- Measure variables (two-point distance measurements) ---
    void addMeasure(MeasureVariable mv);
    void removeMeasure(const QUuid& id);
    void updateMeasure(const MeasureVariable& mv);
    /// Sync the measure variable's display name from its owning block's segment
    /// name (测量对象名称 → 测量变量名称). No-op when the block owns no measure
    /// variable or the name is unchanged.
    void setOwnerMeasureName(const QUuid& ownerBlockId, const QString& name);

    // --- Angle measure variables (two-segment relative angle) ---
    void addAngleMeasure(AngleMeasureVariable am);
    void removeAngleMeasure(const QUuid& id);
    void updateAngleMeasure(const AngleMeasureVariable& am);
    [[nodiscard]] const std::vector<AngleMeasureVariable>& angleMeasures() const;
    [[nodiscard]] AngleMeasureVariable* findAngleMeasure(const QUuid& id);
    [[nodiscard]] const std::vector<MeasureVariable>& measureVars() const;
    [[nodiscard]] MeasureVariable* findMeasure(const QUuid& id);
    /// Reverse lookup: the measure variable OWNED by @p ownerBlockId (the
    /// measurement line created together with it), nullptr when the block
    /// owns none. Used by the canvas right-click menu to detect measure lines
    /// (烘焙到操作层入口).
    [[nodiscard]] MeasureVariable* findMeasureByOwner(const QUuid& ownerBlockId);
    [[nodiscard]] const MeasureVariable* findMeasureByOwner(const QUuid& ownerBlockId) const;

    /// Re-measure all measure variables (two-point distances), refresh cached
    /// values, and sync into the parameter map (cm domain).
    /// @param skipAuxSource When true, measurements whose endpoints BOTH sit on
    ///        the (clean) auxiliary layer keep their cached value.
    /// Returns true if any measured value changed.
    bool measureMeasureVars(bool skipAuxSource = false);

    /// Re-measure all angle measure variables (two-segment relative angles),
    /// refresh cached values, and sync into the parameter map (degree domain).
    /// @param skipAuxSource When true, measurements whose segments BOTH sit on
    ///        the (clean) auxiliary layer keep their cached value.
    /// Returns true if any measured value changed.
    bool measureAngleMeasureVars(bool skipAuxSource = false);

    /// Blocks whose length formulas reference (exact match) a linked variable
    /// sourced from the given block. When the source block is deleted those
    /// lengths are baked back to plain numbers (长度固化为数值) — used by
    /// RemoveBlockCommand to snapshot the consumers for undo.
    [[nodiscard]] QList<QUuid> linkedConsumerBlocks(const QUuid& sourceBlockId) const;

    // --- Resolve ---
    /// Re-resolve all blocks and attachments. Call after any parameter change.
    void resolveAll();
    /// Re-resolve after a per-frame drag transform update (live-follow mode).
    /// Emits resolved() so the canvas syncs positions, but NOT
    /// documentChanged() — panels refresh once when the gesture commits
    /// through its undo command's resolveAll().
    /// @param affectedBlockIds  Seeds of the dirty subgraph: these blocks (and
    ///        everything depending on them via attachments / cross-block
    ///        point references) are re-solved; all other blocks stay frozen.
    ///        Empty = full resolve (same cost as resolveAll, minus panels).
    /// @param ignoredAttachments Attachments excluded from this pass (e.g.
    ///        cross-selection links pending removal at drag end). They remain
    ///        in the document; only the pass skips them.
    /// @note The dirty layer group must still be annotated via
    ///        invalidateLayer()/invalidateAllLayers() before calling.
    void resolveForDrag(const QList<QUuid>& affectedBlockIds = {},
                        const QList<QUuid>& ignoredAttachments = {});

    // --- Serialization support ---
    /// Clear all document data (blocks, attachments, variables, formulas,
    /// parameters) and reset serial counters. Does NOT clear undo stack.
    void clear();
    /// Serial counter access (for save/restore).
    [[nodiscard]] int pointSeq() const { return m_nextPointSeq; }
    [[nodiscard]] int lineSeq() const { return m_nextLineSeq; }
    void setSerialCounters(int pointSeq, int lineSeq);
    /// Add a block without resolving or recomputing groups (for batch restore).
    QUuid addBlockRaw(Block block);
    /// Add an attachment without resolving (for batch restore).
    void addAttachmentRaw(Attachment att);
    /// Add a free point without emitting signals (for batch restore).
    void addFreePointRaw(ParamPoint pt);
    // --- Silent batch restore for the variable/measurement sub-domains ---
    /// (deserializer only — no signals, no recompute/resolve; finishRestore()
    /// + the caller's resolveAll() own the eventual refresh).
    void replaceLayersRaw(std::vector<Layer> layers);
    void restoreVariableRaw(Variable var);
    void restoreFormulaRaw(FormulaVariable formula);
    void restoreFormulaGroupRaw(FormulaGroup group);
    /// Re-insert a formula group at registry position @p index (undo replay).
    void insertFormulaGroupAt(int index, FormulaGroup group);
    void restoreLinkedRaw(LinkedVariable lv);
    void restoreMeasureRaw(MeasureVariable mv);
    void restoreAngleMeasureRaw(AngleMeasureVariable am);
    /// Emit all UI-refresh signals after a batch restore (deserialization).
    /// Creates canvas items for every restored block and rebuilds the
    /// variable / formula / group panels. Call once, after recomputeFormulas().
    void finishRestore();
    /// Issues detected during the last resolve pass (dangling attachments,
    /// non-converging graph). Empty when the document is healthy.
    [[nodiscard]] const std::vector<ResolveDiagnostic>& diagnostics() const
    { return m_diagnostics; }

    // --- Undo/Redo ---
    [[nodiscard]] QUndoStack* undoStack() const { return m_undoStack; }

    // --- Internal sub-domain hooks (VariableStore / MeasurementStore) ---
    /// Publish a single parameter entry (cm domain) without resolving. The
    /// caller's pipeline is responsible for the eventual resolveAll().
    void publishParameter(const QString& name, double cmValue);
    /// Remove a parameter entry without resolving.
    void removeParameterEntry(const QString& name);
    /// Batch-publish parameter entries without resolving (caller resolves once).
    void publishParamsRaw(const QHash<QString, double>& cmValues);
    /// Aux-layer blocks whose transform can move together with the working
    /// layers — the followers (transitively) of cross-layer attachments.
    /// Empty when there are no cross-layer attachments (fast path). Used by
    /// MeasurementStore to defeat the skipAuxSource measurement cache.
    [[nodiscard]] QSet<QUuid> collectMobileAuxBlocks() const;

signals:
    void blockAdded(const QUuid& blockId);
    void blockRemoved(const QUuid& blockId);
    /// Emitted when the whole document is cleared/reset (new document or load).
    /// The canvas uses this to discard every BlockItem before a rebuild.
    void documentReset();
    void documentChanged();   ///< Emitted after resolveAll() or structural changes.
    void resolved();          ///< Emitted specifically after a resolve pass.
    /// Emitted when the block/attachment topology changes (add/remove block,
    /// add/remove attachment).
    void structureChanged();
    /// Emitted when the variable list changes (add/remove/update).
    void variablesChanged();
    /// Emitted when the formula list changes (add/remove/update/recompute).
    void formulasChanged();
    /// Emitted when the formula group registry changes
    /// (add/remove/rename/collapse/reorder).
    void formulaGroupsChanged();
    /// Emitted when the linked variable list changes (add/remove/update).
    void linkedVarsChanged();
    /// Emitted when the measure variable list changes (add/remove/update).
    void measureVarsChanged();
    /// Emitted when the angle measure variable list changes (add/remove/update).
    void angleMeasureVarsChanged();
    /// Emitted when the canvas layer registry changes
    /// (add/remove/rename/visibility, or a block's layer assignment).
    void layersChanged();
    /// Emitted when the active canvas layer switches.
    void activeLayerChanged(const QUuid& layerId);
    /// Emitted when the component registry changes (add/remove/dissolve/update).
    void componentsChanged();

private:
    QHash<QString, double>       m_parameters;
    QSet<QString>                m_formulaParamNames;  ///< Param names contributed by formula variables.
    QHash<QString, QList<Condition>> m_conditioned;    ///< formulaName -> conditions (standalone semantics).
    std::vector<ParamPoint>      m_freePoints;
    std::vector<Block>           m_blocks;
    QHash<QUuid, int>            m_blockIndex;   ///< blockId -> index in m_blocks (O(1) lookup)
    std::vector<Component>       m_components;
    QHash<QUuid, QUuid>          m_blockToComponent;  ///< blockId -> componentId (reverse index)
    std::vector<Attachment>      m_attachments;
    std::vector<ResolveDiagnostic> m_diagnostics;  ///< Issues from the last resolve pass.
    QUndoStack*                  m_undoStack = nullptr;

    // --- Readable serial counters (monotonic, never reused) ---
    int m_nextPointSeq = 1;
    int m_nextLineSeq  = 1;

    // --- Sub-domain registries (facade; PIMPL-style: forward-declared and
    // heap-owned so ParamDocument.h does not drag in the registry headers) ---
    std::unique_ptr<LayerRegistry>    m_layerRegistry;
    std::unique_ptr<VariableStore>    m_variableStore;
    std::unique_ptr<MeasurementStore> m_measureStore;

    /// Number of cross-layer attachments (aux follower → working leader,
    /// the only permitted direction). 0 = the aux/working boundary is sealed
    /// and Phase 3 (跨层沉降) is skipped entirely — zero per-frame overhead.
    int m_crossLayerCount = 0;

    /// Phase 2.5 (跨层交点重解) trigger flag: whether ANY aux-layer
    /// Intersection point references a working-layer point. The old code
    /// re-scanned ALL working × aux blocks × points on EVERY resolve — an
    /// O(work×aux) triple loop per drag frame. The flag is a structural
    /// property of the doc (does not change during a drag), so it is
    /// recomputed only on FULL resolves (every structural mutation funnels
    /// through resolveAll()); narrowed drag frames reuse the cached value.
    bool m_auxIntersectToWorking = false;

    /// Phase 4 (工作侧跨层交点重解) trigger flag: whether ANY working-layer
    /// Intersection point references an aux-layer point. Such an intersection is
    /// solved in Phase 2 against the aux pose of that moment; the Phase 3 settle
    /// may move the aux origin afterwards → the intersection drifts off the ray
    /// (用户回归: 辅助层射线起点 + 工作层交点, 变量修改后偏离 1.3mm).
    /// Structural property — recomputed on FULL resolves only, cached for drags.
    bool m_workingIntersectToAux = false;

    /// Scan the whole doc for the Phase 2.5 cross-ref (aux intersection
    /// referencing a working-layer point). O(work×aux×points) — full resolves
    /// only, never per-frame.
    [[nodiscard]] bool scanAuxIntersectionCrossRefs() const;

    /// Scan for working-layer intersections referencing aux-layer points
    /// (mirror of scanAuxIntersectionCrossRefs). Full resolves only.
    [[nodiscard]] bool scanWorkingIntersectionAuxRefs() const;

    // --- Dirty-subgraph edge table (阶段2) ---
    /// leader block id → follower block ids (from ALL attachments, pins
    /// included — bridges depend on their pin hosts). Lazy-rebuilt whenever
    /// the attachment list changes (m_followersDirty).
    mutable QHash<QUuid, QList<QUuid>> m_followersOf;
    mutable bool m_followersDirty = true;

    // --- Cross-block REFERENCE index (2026-09 性能专项) ---
    /// target block id → referencing block ids (终点指向 / 省道 / 曲线点 follow /
    /// polar/midpoint/on-segment/intersection/interp refs — the same relation
    /// blockReferences() used to scan for, per BFS node). Lazy-rebuilt when
    /// dirty; the OLD collectAffected re-scanned ALL blocks per dequeued node
    /// (O(N²)/drag frame). Dirty set by every full resolve (all reference
    /// mutations funnel through resolveAll()) plus structural ops.
    mutable QHash<QUuid, QHash<QUuid, char>> m_referenceIndex;
    mutable bool m_referenceIndexDirty = true;
    void ensureReferencesIndex() const;

    /// Recount m_crossLayerCount from scratch over the current attachments.
    /// Structural mutation paths only (never per-frame); used wherever the
    /// deleted blocks may already be gone so per-edge layer lookups are
    /// unreliable (removeBlock erases the block BEFORE its attachments).
    void recountCrossLayerAttachments();

    /// Conservative static VALUE-CYCLE pre-check for addAttachment(): true
    /// when the candidate edge would hang a measurement variable's SOURCE
    /// block (blockA/blockB, or a linked source) beneath one of that
    /// variable's CONSUMER blocks — i.e. a consumer whose length/angle
    /// formula reads M_xxx while the measured geometry rides on the
    /// consumer's own attachment subtree (值循环). Runs ONLY for candidate
    /// edges crossing the aux/working boundary (same-layer behaviour stays
    /// bit-for-bit unchanged); lightweight: subtree BFS + leader-chain walk.
    [[nodiscard]] bool wouldCreateMeasureValueCycle(const Attachment& candidate) const;

    /// Release any bridge block that no longer has both of its position pins
    /// (a pin was detached or its host block was deleted). Instead of deleting
    /// the bridge, it becomes an independent segment: the current (stretched)
    /// geometry is frozen into its own length + world angle, and a surviving
    /// pin is converted into a normal follower attachment whose construction
    /// angle preserves the current direction. Does NOT resolve — the caller
    /// does that once afterwards.
    /// Returns the ids of the bridges released.
    std::vector<QUuid> releaseOrphanedBridges();

    /// Degrade Intersection points whose ray origin (refPointA) or target
    /// segment (hostSegmentId) no longer exists. The point is frozen at its
    /// last resolved position as a Free point (or OnSegment if the target
    /// segment still exists and t can be computed).
    void degradeOrphanedIntersections();

    /// Convert a single bridge that lost at least one pin into a normal block
    /// (see releaseOrphanedBridges for the semantics).
    void releaseBridge(Block& b);

    /// Shared resolve pipeline. @p emitDocChanged controls whether the
    /// documentChanged() panel-refresh signal is emitted after the pass.
    /// @p affectedOnly / @p ignoredAttachments are forwarded to the Resolver
    /// (see resolveForDrag for semantics; null = full / none).
    void resolveAllInternal(bool emitDocChanged,
                            const QSet<QUuid>* affectedOnly = nullptr,
                            const QList<QUuid>* ignoredAttachments = nullptr);

    // ── Dirty-subgraph machinery (阶段2: 依赖边表 + 脏传播) ──
    /// Rebuild the leader→followers edge table from m_attachments when stale.
    void ensureFollowersIndex() const;
    /// BFS from @p seeds along the attachment edge table, then collect every
    /// block holding a cross-block reference into the growing set
    /// (endpoint-aim targets, point references, curve-anchor follows).
    [[nodiscard]] QSet<QUuid> collectAffected(const QList<QUuid>& seeds) const;
    /// True when @p b references anything owned by @p targetBlockId:
    /// an aim target, a point id of that block, or a curve-anchor follow.
    bool blockReferences(const Block& b, const QUuid& targetBlockId) const;

    // ── Component (组件) internals ──
    /// Component-level attachment settlement: drive the component's OVERALL
    /// pose as ONE rigid transform (借用暴露端点连接 + 借用端点线段方向), leaving
    /// member-internal relations untouched. Returns true when any member moved.
    bool applyComponentTransform(Component& comp, const Attachment& att, const Block& toBlk);
    /// Validate + insert a component-level attachment (组件级连接): one external
    /// line per component, exposed endpoint must be a member endpoint, no cycle
    /// through the component.
    bool addComponentAttachment(Attachment att);
    /// 自动暴露: record the borrowed endpoint as the component's exposed endpoint
    /// on its FIRST component-level connect (no-op once set).
    void recordExposedEndpoint(Component& comp, const Attachment& att);
    /// Settle every component-level attachment (Attachment::fromComponentId).
    /// Returns true iff ANY component's transform actually changed this pass.
    /// @p affectedOnly (drag-narrowed passes): components whose leader AND all
    /// members are untouched are skipped entirely (frozen poses can't need a
    /// re-settle) — the 2026-09 fix for the old "always 4 rounds" loop.
    bool settleComponents(const std::vector<Attachment>& atts,
                          const QSet<QUuid>* affectedOnly);
    /// Drop @p blockId from any component it belongs to; dissolve components
    /// that fall below two members. Returns true when a component record
    /// changed (caller emits componentsChanged()).
    bool pruneComponentsForBlock(const QUuid& blockId);
};

} // namespace cad::param
