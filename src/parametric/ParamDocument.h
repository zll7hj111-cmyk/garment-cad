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
#include "parametric/ExpressionEvaluator.h"
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
/// Trusted-pipeline accessor for the silent-restore (*Raw) API — the only
/// friend allowed to call it. See parametric/ParamDocumentRaw.h (P1-2).
struct RawModelAccess;
/// 按域分组的只读窄接口视图 (B1 门面分组试点) — 完整定义见 parametric/BlockView.h。
class BlocksView;
/// 按域分组的只读窄接口视图 — 附着域 (B2)，见 parametric/AttachmentsView.h。
class AttachmentsView;
/// 按域分组的只读窄接口视图 — 组件域 (B2)，见 parametric/ComponentsView.h。
class ComponentsView;
/// 按域分组的只读窄接口视图 — 测量域 (B2)，见 parametric/MeasurementsView.h。
class MeasurementsView;
/// 按域分组的只读窄接口视图 — 图层域 (B3)，见 parametric/LayersView.h。
class LayersView;
/// 按域分组的只读窄接口视图 — 变量域 (B3)，见 parametric/VariablesView.h。
class VariablesView;

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
    /// Mutable accessors return a raw pointer into the blocks vector: it dies
    /// on the next structural mutation (vector growth / erase / clear). P1-3
    /// added the guard below — re-fetch per use, never hold across a mutation.
    [[nodiscard]] Block* findBlock(const QUuid& id);
    [[nodiscard]] const Block* findBlock(const QUuid& id) const;

    /// Bumped on every structural change of the block vector (add / remove /
    /// clear). Handy to invalidate caches keyed on "the set of blocks", and the
    /// observable counterpart of the pointer-lifetime caveat above (P1-3).
    ///
    /// Mid-term the fix is a handle (id + generation) instead of a raw pointer;
    /// until then treat any `Block*` as valid only until the next mutation.
    [[nodiscard]] quint64 structureEpoch() const noexcept
    { return m_structureEpoch; }

    /// Debug-only sanity check: does @p p still point into the CURRENT block
    /// storage? Catches the classic "held a Block* across addBlock() and the
    /// vector reallocated" bug — at least when the new buffer lands elsewhere.
    /// Compiles to `true` in release (no cost, no behaviour change).
    bool blockPointerInRange(const Block* p) const noexcept;

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

    /// 按域分组的只读窄接口视图 (B1 门面分组试点): blocks 域读取走
    /// doc.blocksView().byId()/all()/epoch()/impactOf(), 写路径仍在本门面
    /// (校验+信号不绕过)。视图无状态 — 契约见 parametric/BlockView.h。
    [[nodiscard]] BlocksView blocksView() const noexcept;

    // --- Components (组件: packaged work groups) ---
    /// Add a component. Validates that every member exists and updates the
    /// reverse block→component index. Emits componentsChanged() +
    /// structureChanged() + documentChanged().
    QUuid addComponent(Component comp);
    /// (restoreComponentRaw — insert a component VERBATIM without validation or
    /// signals — is a trusted-pipeline API: see RawModelAccess in
    /// parametric/ParamDocumentRaw.h, P1-2.)
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
    /// World-space AABB over an arbitrary block set (旋转选集高亮, 2026-08-27):
    /// same per-block geometry as boundingBoxOf, unioned across @p blockIds.
    [[nodiscard]] BBox boundingBoxOfBlocks(const QList<QUuid>& blockIds) const;
    /// Expand a seed set to the full component closure: every member of every
    /// component that intersects @p seed. Whole-component drag/rotate and dirty
    /// propagation all ride this single expansion.
    [[nodiscard]] QSet<QUuid> componentClosure(const QSet<QUuid>& seed) const;
    /// Member block owning @p pointId inside @p comp (null id when none).
    [[nodiscard]] QUuid memberOwningPoint(const Component& comp, const QUuid& pointId) const;

    /// 组件域只读窄接口视图 (B2) — 契约见 parametric/ComponentsView.h。
    [[nodiscard]] ComponentsView componentsView() const noexcept;

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
    /// 当前角度/公式。2026-xx 两维独立 (用户拍板): 与 angleOnly (位置维度)
    /// 不再互斥, 仅与 slideMode (一轴滑轨) 互斥。
    void setAttachmentAngleIndependent(const QUuid& id, bool angleIndependent);
    /// 滑轨模式 (抽屉式滑动, 用户拍板 2026-08): 切换 Attachment 的 slideMode。
    /// 进入滑轨 (AlongLeader / PerpLeader): 先按当前几何快照锁轴坐标
    /// (slideAlongMm/slidePerpMm = 跟随点相对基准线局部系的当前投影), 再清
    /// angleOnly 并解除拖动保护 (位置必须可滑动)。切回 None: 恢复完整连接
    /// 位置锚点与角度基准分离 (用户需求 2026): 设置角度参考线段/点。
    /// refBlockId/refSegmentId/refPointId 为空 = 恢复默认 (角度跟随位置宿主)。
    /// ref2BlockId/ref2PointId 非空 = 两点连线方向 (点1→点2) 为角度基准
    /// (PANEL_REDESIGN §6.4; 引擎两点分支要求 refBlockId 非空, 调用方保证);
    /// ref2 为空时清空既有 ref2 字段 (与 SetAttachmentAngleRefCommand 同步)。
    void setAttachmentAngleRef(const QUuid& id,
                               const QUuid& refBlockId,
                               const QUuid& refSegmentId,
                               const QUuid& refPointId = QUuid(),
                               const QUuid& ref2BlockId = QUuid(),
                               const QUuid& ref2PointId = QUuid());
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
    /// (addAttachmentsRaw — insert attachments VERBATIM without revalidation —
    /// is a trusted-pipeline API: see RawModelAccess in
    /// parametric/ParamDocumentRaw.h, P1-2. Snapshot integrity rule: the
    /// restored snapshot is the ground truth, so it is never revalidated.)
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

    // ── 影子基准 (拆开影子线段, DETACH_SHADOW_DESIGN.md; 用户拍板 2026-xx) ──
    // 拆开连接 = 复制本体 exit 线段为隐藏冻结克隆 (影子 Block, isShadow) 作
    // 角度基准 + Att2 原地换代指向影子 (angleOnly)。挂新宿主 = 影子作为
    // follower 挂上去 (Att1, Δ 反算保向) 形成 L3→影子→L2 标准双连接链 (R3)。
    // 挂回本体 = 删影子恢复活引用 (⑤); 本体删 → 影子删 (⑥); 宿主删 → 影子
    // 弹回拆开态 (⑦)。零新增 Resolver 逻辑 (影子=普通块, 链式附着现有已支持)。
    /// 拆开 + 影子基准: 克隆 att 的 to-block exit 段为影子块并把 Att2 原地
    /// 换代 (toBlock/toPoint/toSeg→影子, angleOnly=true, offset 原样 R2)。
    /// 基准已是影子 (再拆开④) → 释放挂载、影子冻结当前方向。降级场景
    /// (本体为桥线/省道/组件成员/多段块/曲线段) 返回空 id, 调用方走旧
    /// angleOnly 行为。undo 经 SetAttachmentAngleOnlyCommand 一步回退。
    QUuid detachWithShadow(const QUuid& attId);
    /// 挂载影子到新宿主 (③): Att1 = 影子→宿主 (Δ=反算保向, 挂载瞬间影子/
    /// 跟随线世界方向不变) + Att2 恢复位置钉点并重新焊接。@p toSegmentId 空
    /// = 自动取宿主出口段。森林/跨层/桥线规则与 addAttachment 同门校验。
    bool mountShadowTo(const QUuid& shadowId, const QUuid& toBlockId,
                       const QUuid& toPointId, const QUuid& toSegmentId = QUuid());
    /// 影子弹回拆开态 (④/⑦): 删除 Att1 (挂载关系), Att2 回 angleOnly。
    /// 影子保持当前解算姿态 (冻结当前方向, 不跳变)。未挂载 = 幂等返回 true。
    bool releaseShadowToDetached(const QUuid& shadowId);
    /// 挂回本体 (⑤): Att2 基准若是影子 → 删影子 (含 Att1) 并把 Att2 还原到
    /// 本体 (活引用恢复 + 重新焊接)。@p explicitToPoint 非空 = 挂载路由显式
    /// 落点 (拖回本体时钉在用户拖到的点); 空 = 复原拆开前锚点 (面板重连)。
    /// 拆开态 (②) 与挂载态 (③) 都可用 (不依赖 angleOnly 旗标)。非影子基准
    /// 返回 false。
    bool reattachShadowToMaster(const QUuid& attId,
                                const QUuid& explicitToPoint = QUuid(),
                                const QUuid& explicitToSegment = QUuid());
    /// 清除影子 (面板「清除影子」): 删除 Att2 (跟随线转纯自由线) 与 Att1
    /// (若挂载) 及影子块本身。
    bool removeShadow(const QUuid& shadowId);
    /// 本体 → 影子块查找 (一个本体可有多个影子 —— 多条跟随线各自拆开)。
    /// 返回第一个匹配; 无影子返回 nullptr。
    [[nodiscard]] Block* findShadowOfMaster(const QUuid& masterBlockId);
    [[nodiscard]] const Block* findShadowOfMaster(const QUuid& masterBlockId) const;
    /// 纯构建器 (无副作用, 命令层快照与降级判定共用): 拆开换代产物。
    /// 失败 (降级/已是影子基准) 返回 false。成功时 outShadow 为待添加的影子
    /// 块、outNewAtt 为 Att2 的 verbatim 换代态。
    [[nodiscard]] bool buildShadowDetach(const QUuid& attId, Block& outShadow,
                                         Attachment& outNewAtt) const;
    /// 纯构建器: 挂回本体 (⑤) 的 Att2 还原态。@p explicitToPoint 非空 = 挂载
    /// 路由显式落点 (拖回本体时用户钉的点); 否则按影子锚角色 1:1 复原拆开前
    /// 锚点 (面板重连语义)。
    [[nodiscard]] bool buildShadowReconnect(const QUuid& attId, Attachment& outRestored,
                                            const QUuid& explicitToPoint = QUuid(),
                                            const QUuid& explicitToSegment = QUuid()) const;
    /// 纯构建器: 挂载 Att1 (影子→宿主, Δ 反算保向)。已挂载/目标非法返回 false。
    [[nodiscard]] bool buildShadowMount(const QUuid& shadowId, const QUuid& toBlockId,
                                        const QUuid& toPointId, const QUuid& toSegmentId,
                                        Attachment& outAtt1) const;

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

    /// 附着域只读窄接口视图 (B2) — 契约见 parametric/AttachmentsView.h。
    [[nodiscard]] AttachmentsView attachmentsView() const noexcept;

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
    [[nodiscard]] const Variable* findVariable(const QUuid& id) const;

    /// 变量域只读窄接口视图 (B3) — 契约见 parametric/VariablesView.h。
    [[nodiscard]] VariablesView variablesView() const noexcept;

    // --- Formula variables ---
    void addFormula(FormulaVariable formula);
    void removeFormula(const QUuid& id);
    void updateFormula(const FormulaVariable& formula);
    [[nodiscard]] const std::vector<FormulaVariable>& formulas() const;
    [[nodiscard]] FormulaVariable* findFormula(const QUuid& id);
    [[nodiscard]] const FormulaVariable* findFormula(const QUuid& id) const;

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
    [[nodiscard]] const FormulaGroup* findFormulaGroup(const QUuid& groupId) const;

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

    /// 图层域只读窄接口视图 (B3) — 契约见 parametric/LayersView.h。
    [[nodiscard]] LayersView layersView() const noexcept;

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

    /// 测量域只读窄接口视图 (B2) — 契约见 parametric/MeasurementsView.h。
    [[nodiscard]] MeasurementsView measurementsView() const noexcept;

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
    /// parameters) and reset serial counters. P0-1: also clears the undo
    /// stack, so opening/creating a document can never revive commands whose
    /// snapshots belong to a previous document.
    void clear();
    /// Serial counter access (for save/restore).
    [[nodiscard]] int pointSeq() const { return m_nextPointSeq; }
    [[nodiscard]] int lineSeq() const { return m_nextLineSeq; }
    void setSerialCounters(int pointSeq, int lineSeq);
    /// Emit all UI-refresh signals after a batch restore (deserialization).
    /// Creates canvas items for every restored block and rebuilds the
    /// variable / formula / group panels. Call once, after recomputeFormulas().
    void finishRestore();
    /// Issues detected during the last resolve pass (dangling attachments,
    /// non-converging graph). Empty when the document is healthy.
    [[nodiscard]] const std::vector<ResolveDiagnostic>& diagnostics() const
    { return m_diagnostics; }

    /// The document's own expression compile cache (2026-12 P1-5). Every
    /// resolve pass compiles formulas through it, so bytecode is partitioned
    /// per document instead of living in a process-wide static — and it is
    /// released on clear() (new/open). Pass it as EvalContext::cache for
    /// out-of-band evaluations (commands, previews) on this document.
    [[nodiscard]] ExpressionCache& expressionCache() { return m_exprCache; }

    // --- Undo/Redo ---
    /// Maximum number of undo steps kept (P2-5). Every command snapshots the
    /// model — the delete commands even snapshot their whole cascade subgraph
    /// — so the stack used to grow without bound across a long session. QUndo
    /// drops the oldest command once this limit is reached.
    /// Tuning: 150 ≈ several minutes of continuous editing; raise it only
    /// together with incremental snapshots (the real fix for big documents).
    static constexpr int kUndoStackLimit = 150;

    [[nodiscard]] QUndoStack* undoStack() const { return m_undoStack; }

    // --- Internal sub-domain hooks (VariableStore / MeasurementStore) ---
    /// Publish a single parameter entry (cm domain) without resolving. The
    /// caller's pipeline is responsible for the eventual resolveAll().
    void publishParameter(const QString& name, double cmValue);
    /// Remove a parameter entry without resolving.
    void removeParameterEntry(const QString& name);
    /// (publishParamsRaw — batch-publish entries without resolving — is a
    /// trusted-pipeline API: see RawModelAccess in
    /// parametric/ParamDocumentRaw.h, P1-2.)
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
    // ── Trusted-pipeline channel (P1-2) ────────────────────────────────────
    // Silent restore operations: they mutate the model WITHOUT validation,
    // signals or a resolve pass. They are NOT part of the public facade — the
    // only way to call them is cad::param::RawModelAccess (see
    // parametric/ParamDocumentRaw.h), which is the single friend below. The
    // sanctioned callers are the deserializer, QUndoCommand undo/redo replay
    // and the drag-cancel snapshot restores in the tools layer.
    friend struct RawModelAccess;

    QUuid addBlockRaw(Block block);
    void addAttachmentRaw(Attachment att);
    void addAttachmentsRaw(const std::vector<Attachment>& atts);
    void addFreePointRaw(ParamPoint pt);
    void replaceLayersRaw(std::vector<Layer> layers);
    void restoreVariableRaw(Variable var);
    void restoreFormulaRaw(FormulaVariable formula);
    void restoreFormulaGroupRaw(FormulaGroup group);
    void insertFormulaGroupAt(int index, FormulaGroup group);
    void restoreLinkedRaw(LinkedVariable lv);
    void restoreMeasureRaw(MeasureVariable mv);
    void restoreAngleMeasureRaw(AngleMeasureVariable am);
    void restoreComponentRaw(Component comp);
    void publishParamsRaw(const QHash<QString, double>& cmValues);

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

    /// Expression bytecode cache owned by this document (P1-5): threaded into
    /// every Resolver pass via EvalContext::cache, cleared on clear()/reload.
    ExpressionCache              m_exprCache;

    /// Structural generation of m_blocks (P1-3) — see structureEpoch().
    quint64                      m_structureEpoch = 0;
    /// Bump m_structureEpoch (every add/remove/clear of a block).
    void bumpStructureEpoch() noexcept { ++m_structureEpoch; }

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

// B1 门面分组: blocks 域窄接口视图。放在类定义之后, 内联成员 blocksView()
// 的定义需要完整的 ParamDocument (定义体在 BlockView.h 中)。
#include "parametric/BlockView.h"
#include "parametric/AttachmentsView.h"
#include "parametric/ComponentsView.h"
#include "parametric/MeasurementsView.h"
#include "parametric/LayersView.h"
#include "parametric/VariablesView.h"
