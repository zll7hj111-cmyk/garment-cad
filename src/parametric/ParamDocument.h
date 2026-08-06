#pragma once

#include <QObject>
#include <QUuid>
#include <QHash>
#include <QSet>
#include <QUndoStack>
#include <vector>

#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/ParamPoint.h"
#include "parametric/Group.h"
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

/// The parametric document holds all Blocks, free points, attachments,
/// and global parameters. It manages resolve and undo/redo.
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

        [[nodiscard]] bool hasImpact() const
        {
            return attachmentsRemoved > 0 || bridgesReleased > 0 ||
                   intersectionsFrozen > 0 || intersectionsAimCleared > 0 ||
                   linkedFrozen > 0 ||
                   linkedVarsRemoved > 0 || measureVarsRemoved > 0 ||
                   angleVarsRemoved > 0 || formulasBroken > 0;
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
            return *this;
        }
    };

    /// Delete-impact report for @p id (prediction, no mutation).
    [[nodiscard]] DeleteImpact deleteImpactReport(const QUuid& id) const;
    [[nodiscard]] std::vector<Block>& blocks() { return m_blocks; }
    [[nodiscard]] const std::vector<Block>& blocks() const { return m_blocks; }
    /// O(1) block lookup by id (returns nullptr if not found).
    [[nodiscard]] Block* blockById(const QUuid& id);
    [[nodiscard]] const Block* blockById(const QUuid& id) const;

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
    /// Toggle the LOCKED flag of an existing attachment (锁定连接). Locked
    /// connections are welded: dragging cannot tear them apart, and dragging
    /// either side moves the whole pair. Resolves once.
    void setAttachmentLocked(const QUuid& id, bool locked);
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

    /// Remove every attachment that references the given block (as leader or
    /// follower). Used to "kick a segment out of its group" while keeping the
    /// block geometry. Returns the number of attachments removed.
    int removeAttachmentsOfBlock(const QUuid& blockId);

    /// Bridge blocks (Block::isBridge) that have a pin attachment TO the given
    /// host block. Deleting the host releases these bridges (they lose a pin
    /// and become independent segments). Used by DeleteBlockCommand to
    /// snapshot the affected bridges for undo.
    [[nodiscard]] std::vector<QUuid> bridgesPinnedTo(const QUuid& hostBlockId) const;

    // --- Readable serials ---
    /// Generate a fresh human-readable serial for a point / line / group.
    [[nodiscard]] QString newPointSerial();
    [[nodiscard]] QString newLineSerial();
    [[nodiscard]] QString newGroupSerial();

    // --- Variables (plain value variables) ---
    void addVariable(Variable var);
    void removeVariable(const QUuid& id);
    void updateVariable(const Variable& var);
    [[nodiscard]] const std::vector<Variable>& variables() const { return m_variables; }
    [[nodiscard]] std::vector<Variable>& variables() { return m_variables; }
    [[nodiscard]] Variable* findVariable(const QUuid& id);

    // --- Formula variables ---
    void addFormula(FormulaVariable formula);
    void removeFormula(const QUuid& id);
    void updateFormula(const FormulaVariable& formula);
    [[nodiscard]] const std::vector<FormulaVariable>& formulas() const { return m_formulas; }
    [[nodiscard]] std::vector<FormulaVariable>& formulas() { return m_formulas; }
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
    [[nodiscard]] const std::vector<FormulaGroup>& formulaGroups() const { return m_formulaGroups; }
    [[nodiscard]] std::vector<FormulaGroup>& formulaGroups() { return m_formulaGroups; }
    [[nodiscard]] FormulaGroup* findFormulaGroup(const QUuid& groupId);

    // --- Canvas layers (selection/visibility filter + aux calculation layer) ---
    /// The layer registry. Blocks reference layers by index (Block::layer).
    /// Index 0 is always the single auxiliary calculation layer; the rest are
    /// working layers. Always contains at least two layers.
    [[nodiscard]] const std::vector<Layer>& layers() const { return m_layers; }
    [[nodiscard]] std::vector<Layer>& layers() { return m_layers; }
    [[nodiscard]] int layerCount() const { return static_cast<int>(m_layers.size()); }
    /// Append a WORKING layer and return its index. Emits layersChanged().
    int addLayer(const QString& name);
    /// Remove a layer: its blocks move to the layer below (clamped so they
    /// never fall into the auxiliary layer) and every higher layer's index
    /// shifts down by one. The auxiliary layer and the last working layer
    /// cannot be removed (no-op). Emits layersChanged().
    void removeLayer(int index);
    void renameLayer(int index, const QString& name);   ///< Emits layersChanged().
    /// Toggle layer visibility. Hiding the active layer auto-switches the
    /// active layer to the nearest visible one. Emits layersChanged() (and
    /// activeLayerChanged() if the active layer moved).
    void setLayerVisible(int index, bool visible);
    [[nodiscard]] bool layerVisible(int index) const;
    [[nodiscard]] int activeLayer() const { return m_activeLayer; }
    void setActiveLayer(int index);   ///< Emits activeLayerChanged().

    /// True when layer @p index is the auxiliary calculation layer.
    [[nodiscard]] bool isAuxLayer(int index) const {
        return index >= 0 && index < layerCount()
            && m_layers[static_cast<size_t>(index)].type == LayerType::Auxiliary;
    }
    /// True when the block lives on the auxiliary layer.
    [[nodiscard]] bool isAuxBlock(const Block& b) const { return isAuxLayer(b.layer); }
    /// Effective visibility for RENDERING. Any non-active layer renders
    /// GRAYED (BlockItem::LayerMode::Grayed) — including the auxiliary layer,
    /// so its construction geometry stays visible as a reference draft.
    /// Only layers manually hidden (layerVisible == false) are not rendered.
    [[nodiscard]] bool layerEffectivelyVisible(int index) const {
        return layerVisible(index);
    }

    /// Whether the layer's points/segments may be SNAP targets. Non-active
    /// WORKING layers stay snappable (grayed reference — new geometry can
    /// connect to them). The aux layer is ONLY snappable while it is the
    /// active layer: from a working layer its geometry is a reference draft,
    /// and snapping to it would produce a cross-group attachment that
    /// addAttachment() rejects — snap targets must never be connectable
    /// (capture the cursor, then fail to connect = interaction trap).
    [[nodiscard]] bool layerSnappable(int index) const {
        if (!layerVisible(index)) return false;
        if (index == m_activeLayer) return true;
        return !isAuxLayer(index);
    }

    // --- Layered dirty marking (resolve pipeline optimisation) ---
    /// Mark the layer group (aux vs working) containing layer @p index as
    /// needing re-resolution. Accumulates until consumed by resolveAll().
    /// Hot paths (per-frame drags) call this to narrow the resolve scope;
    /// un-annotated resolveAll() calls fall back to full re-resolution.
    void invalidateLayer(int index) {
        if (isAuxLayer(index)) m_auxDirty = true;
        else m_workingDirty = true;
        m_dirtyAnnotated = true;
    }
    /// Mark every layer group dirty (variable edits, structural changes).
    void invalidateAllLayers() {
        m_auxDirty = m_workingDirty = true;
        m_dirtyAnnotated = true;
    }

    /// True when the document contains at least one cross-layer attachment
    /// (aux follower → working leader). Maintained incrementally so resolve
    /// frames can short-circuit the Phase 3 cross-layer settle at zero cost
    /// for the (overwhelmingly common) no-cross-layer case.
    [[nodiscard]] bool hasCrossLayerAttachments() const { return m_crossLayerCount > 0; }

    // --- Linked variables (geometric measurements) ---
    void addLinked(LinkedVariable lv);
    void removeLinked(const QUuid& id);
    void updateLinked(const LinkedVariable& lv);  ///< Rename / comment edit.
    [[nodiscard]] const std::vector<LinkedVariable>& linkedVars() const { return m_linkedVars; }
    [[nodiscard]] std::vector<LinkedVariable>& linkedVars() { return m_linkedVars; }
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
    void updateMeasure(const MeasureVariable& mv);  ///< Rename / comment edit.
    /// Sync the measure variable's display name from its owning block's segment
    /// name (测量对象名称 → 测量变量名称). No-op when the block owns no measure
    /// variable or the name is unchanged.
    void setOwnerMeasureName(const QUuid& ownerBlockId, const QString& name);

    // --- Angle measure variables (two-segment relative angle) ---
    void addAngleMeasure(AngleMeasureVariable am);
    void removeAngleMeasure(const QUuid& id);
    void updateAngleMeasure(const AngleMeasureVariable& am);  ///< Rename / comment edit.
    [[nodiscard]] const std::vector<AngleMeasureVariable>& angleMeasures() const { return m_angleMeasures; }
    [[nodiscard]] std::vector<AngleMeasureVariable>& angleMeasures() { return m_angleMeasures; }
    [[nodiscard]] AngleMeasureVariable* findAngleMeasure(const QUuid& id);
    [[nodiscard]] const std::vector<MeasureVariable>& measureVars() const { return m_measureVars; }
    [[nodiscard]] std::vector<MeasureVariable>& measureVars() { return m_measureVars; }
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

    // --- User groups (成组: authored protection locks) ---
    /// Groups are USER-authored protection units — NOT derived from the
    /// attachment graph. Members are locked against structural operations
    /// (single-line delete / break / internal detach) until dissolved; the
    /// guard itself lives at the tool layer, the model only keeps membership.
    /// Create a group over the given blocks. Members must exist (>= 2), share
    /// one layer and not already belong to a group (nesting forbidden).
    /// Returns the new group's id, or a null QUuid when rejected.
    /// Emits groupsChanged().
    QUuid createGroup(const QList<QUuid>& memberIds, const QString& name = QString());
    /// Dissolve a group: the record and membership vanish; member geometry
    /// and connections stay untouched. Emits groupsChanged().
    void dissolveGroup(const QUuid& groupId);
    /// Reorder groups within the registry (panel drag-sort). View-level
    /// ordering — not undoable; persisted via the groups array order.
    /// Emits groupsChanged().
    void moveGroup(int fromIndex, int toIndex);
    /// Re-insert a group record + member mapping verbatim (undo replay /
    /// batch restore). A record with the same id is replaced; members not
    /// present in the document are skipped. Emits groupsChanged().
    void restoreGroup(Group group, const QList<QUuid>& memberIds);
    [[nodiscard]] const std::vector<Group>& groups() const { return m_groups; }
    [[nodiscard]] Group* findGroup(const QUuid& groupId);
    /// Group id the block currently belongs to (null if ungrouped).
    [[nodiscard]] QUuid groupOfBlock(const QUuid& blockId) const;
    /// All block ids belonging to a group.
    [[nodiscard]] QList<QUuid> blocksInGroup(const QUuid& groupId) const;
    /// Rename a group (emits groupsChanged()).
    void setGroupName(const QUuid& groupId, const QString& name);

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
    /// parameters, groups) and reset serial counters. Does NOT clear undo stack.
    void clear();
    /// Serial counter access (for save/restore).
    [[nodiscard]] int pointSeq() const { return m_nextPointSeq; }
    [[nodiscard]] int lineSeq() const { return m_nextLineSeq; }
    [[nodiscard]] int groupSeq() const { return m_nextGroupSeq; }
    void setSerialCounters(int pointSeq, int lineSeq, int groupSeq);
    /// Batch-restore groups and block-group mapping (used by deserializer).
    void restoreGroups(std::vector<Group> groups, QHash<QUuid, QUuid> blockGroup);
    /// Add a block without resolving or recomputing groups (for batch restore).
    QUuid addBlockRaw(Block block);
    /// Add an attachment without resolving (for batch restore).
    void addAttachmentRaw(Attachment att);
    /// Add a free point without emitting signals (for batch restore).
    void addFreePointRaw(ParamPoint pt);
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

signals:
    void blockAdded(const QUuid& blockId);
    void blockRemoved(const QUuid& blockId);
    /// Emitted when the whole document is cleared/reset (new document or load).
    /// The canvas uses this to discard every BlockItem before a rebuild.
    void documentReset();
    void documentChanged();   ///< Emitted after resolveAll() or structural changes.
    void resolved();          ///< Emitted specifically after a resolve pass.
    /// Emitted when the block/attachment topology changes (add/remove block,
    /// add/remove attachment). Used to rebuild group views.
    void structureChanged();
    /// Emitted when the group registry changes (recompute or rename).
    void groupsChanged();
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
    void activeLayerChanged(int index);

private:
    QHash<QString, double>       m_parameters;
    QSet<QString>                m_formulaParamNames;  ///< Param names contributed by formula variables.
    QHash<QString, QList<Condition>> m_conditioned;    ///< formulaName -> conditions (standalone semantics).
    std::vector<ParamPoint>     m_freePoints;
    std::vector<Block>          m_blocks;
    QHash<QUuid, int>           m_blockIndex;   ///< blockId -> index in m_blocks (O(1) lookup)
    std::vector<Attachment>     m_attachments;
    std::vector<ResolveDiagnostic> m_diagnostics;  ///< Issues from the last resolve pass.
    QUndoStack*                 m_undoStack = nullptr;

    // --- Readable serial counters (monotonic, never reused) ---
    int m_nextPointSeq = 1;
    int m_nextLineSeq  = 1;
    int m_nextGroupSeq = 1;

    // --- Variables & Formulas ---
    std::vector<Variable>        m_variables;
    std::vector<FormulaVariable> m_formulas;
    std::vector<FormulaGroup>    m_formulaGroups;  ///< Panel folders for formulas.
    std::vector<LinkedVariable>  m_linkedVars;
    std::vector<MeasureVariable> m_measureVars;
    std::vector<AngleMeasureVariable> m_angleMeasures;

    // --- User group registry (authored via createGroup; membership lock) ---
    std::vector<Group>   m_groups;       ///< Active groups (each has >= 2 members).
    QHash<QUuid, QUuid>  m_blockGroup;   ///< blockId -> groupId.
    /// Reverse membership index (groupId -> member block ids, insertion
    /// order). Kept in sync with m_blockGroup so blocksInGroup() is
    /// O(members) instead of a full-table scan.
    QHash<QUuid, QList<QUuid>> m_groupMembers;

    // --- Canvas layers ---
    std::vector<Layer>   m_layers;       ///< Layer registry (index = layer id).
    int                  m_activeLayer = 0;

    // --- Layered dirty marking ---
    bool m_auxDirty = true;        ///< Aux layer needs re-resolution.
    bool m_workingDirty = true;    ///< Working layers need re-resolution.
    bool m_dirtyAnnotated = false; ///< True when the caller narrowed the scope
                                   ///< via invalidateLayer()/invalidateAllLayers();
                                   ///< false = conservative full re-resolve.

    /// Number of cross-layer attachments (aux follower → working leader,
    /// the only permitted direction). 0 = the aux/working boundary is sealed
    /// and Phase 3 (跨层沉降) is skipped entirely — zero per-frame overhead.
    int m_crossLayerCount = 0;

    // --- Dirty-subgraph edge table (阶段2) ---
    /// leader block id → follower block ids (from ALL attachments, pins
    /// included — bridges depend on their pin hosts). Lazy-rebuilt whenever
    /// the attachment list changes (m_followersDirty).
    mutable QHash<QUuid, QList<QUuid>> m_followersOf;
    mutable bool m_followersDirty = true;

    // ── Formula dependency graph cache (公式拓扑序) ──
    /// Topological evaluation order over m_formulas, rebuilt ONLY when the
    /// formula set/expressions change (variable edits reuse it — building
    /// the graph costs more than evaluating the formulas it orders).
    /// m_formulaAcyclic == false means a cycle was found: recomputeFormulas
    /// falls back to the legacy bounded fixpoint (bit-for-bit unchanged).
    mutable bool m_formulaDepsDirty = true;
    mutable std::vector<int> m_formulaOrder;
    mutable bool m_formulaAcyclic = true;

    /// Rebuild m_formulaOrder via Kahn's algorithm over formula-name
    /// references (case-insensitive, matching the evaluator's fallback).
    void rebuildFormulaOrder() const;

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

    /// Aux-layer blocks whose transform can move together with the working
    /// layers — the followers (transitively) of cross-layer attachments.
    /// Empty when m_crossLayerCount == 0 (fast path). Used to defeat the
    /// skipAuxSource measurement cache: geometry of these blocks changes in
    /// Phase 3 even though they live on the aux layer.
    [[nodiscard]] QSet<QUuid> collectMobileAuxBlocks() const;

    /// Release any bridge block that no longer has both of its position pins
    /// (a pin was detached or its host block was deleted). Instead of deleting
    /// the bridge, it becomes an independent segment: the current (stretched)
    /// geometry is frozen into its own length + world angle, and a surviving
    /// pin is converted into a normal follower attachment whose construction
    /// angle preserves the current direction. Does NOT resolve or recompute
    /// groups — the caller does that once afterwards.
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
};

} // namespace cad::param
