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
    [[nodiscard]] std::vector<Block>& blocks() { return m_blocks; }
    [[nodiscard]] const std::vector<Block>& blocks() const { return m_blocks; }
    /// O(1) block lookup by id (returns nullptr if not found).
    [[nodiscard]] Block* blockById(const QUuid& id);
    [[nodiscard]] const Block* blockById(const QUuid& id) const;

    // --- Attachment management ---
    void addAttachment(Attachment att);
    void removeAttachment(const QUuid& id);
    [[nodiscard]] const std::vector<Attachment>& attachments() const { return m_attachments; }

    /// Remove every attachment that references the given block (as leader or
    /// follower). Used to "kick a segment out of its group" while keeping the
    /// block geometry. Returns the number of attachments removed.
    int removeAttachmentsOfBlock(const QUuid& blockId);

    // --- Readable serials ---
    /// Generate a fresh human-readable serial for a point / line / group.
    [[nodiscard]] QString newPointSerial();
    [[nodiscard]] QString newLineSerial();
    [[nodiscard]] QString newGroupSerial();

    // --- Groups ---
    /// Recompute group membership from the attachment graph (connected
    /// components of >= 2 blocks), preserving group identity/name across
    /// topology changes. Emits groupsChanged().
    void recomputeGroups();
    [[nodiscard]] const std::vector<Group>& groups() const { return m_groups; }
    [[nodiscard]] Group* findGroup(const QUuid& groupId);
    /// Group id the block currently belongs to (null if free / singleton).
    [[nodiscard]] QUuid groupOfBlock(const QUuid& blockId) const;
    /// All block ids belonging to a group.
    [[nodiscard]] QList<QUuid> blocksInGroup(const QUuid& groupId) const;
    /// Rename a group (emits groupsChanged()).
    void setGroupName(const QUuid& groupId, const QString& name);

    // --- Resolve ---
    /// Re-resolve all blocks and attachments. Call after any parameter change.
    void resolveAll();

    // --- Undo/Redo ---
    [[nodiscard]] QUndoStack* undoStack() const { return m_undoStack; }

signals:
    void blockAdded(const QUuid& blockId);
    void blockRemoved(const QUuid& blockId);
    void documentChanged();   ///< Emitted after resolveAll() or structural changes.
    void resolved();          ///< Emitted specifically after a resolve pass.
    /// Emitted when the block/attachment topology changes (add/remove block,
    /// add/remove attachment). Used to rebuild group views.
    void structureChanged();
    /// Emitted when the group registry changes (recompute or rename).
    void groupsChanged();

private:
    QHash<QString, double>       m_parameters;
    QSet<QString>                m_formulaParamNames;  ///< Param names contributed by formula variables.
    QHash<QString, QList<Condition>> m_conditioned;    ///< formulaName -> conditions (standalone semantics).
    std::vector<ParamPoint>     m_freePoints;
    std::vector<Block>          m_blocks;
    QHash<QUuid, int>           m_blockIndex;   ///< blockId -> index in m_blocks (O(1) lookup)
    std::vector<Attachment>     m_attachments;
    QUndoStack*                 m_undoStack = nullptr;

    // --- Readable serial counters (monotonic, never reused) ---
    int m_nextPointSeq = 1;
    int m_nextLineSeq  = 1;
    int m_nextGroupSeq = 1;

    // --- Group registry ---
    std::vector<Group>   m_groups;       ///< Active groups (each has >= 2 members).
    QHash<QUuid, QUuid>  m_blockGroup;   ///< blockId -> groupId.
};

} // namespace cad::param
