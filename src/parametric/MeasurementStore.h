#pragma once

#include <QObject>
#include <QUuid>
#include <QList>
#include <vector>

#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "parametric/AngleMeasureVariable.h"

namespace cad::param {

class ParamDocument;

/// Measurement sub-domain of the document: linked variables (segment-length
/// publications), measure variables (two-point distances) and angle measure
/// variables (two-segment relative angles). Re-measurement reads resolved
/// geometry through ParamDocument; published values are pushed into its
/// parameter map via the internal hooks.
class MeasurementStore : public QObject
{
    Q_OBJECT

public:
    explicit MeasurementStore(ParamDocument* doc, QObject* parent = nullptr);

    // --- Linked variables (geometric measurements) ---
    void addLinked(LinkedVariable lv);
    void removeLinked(const QUuid& id);
    void updateLinked(const LinkedVariable& lv);  ///< Rename / comment edit.
    [[nodiscard]] const std::vector<LinkedVariable>& linkedVars() const { return m_linkedVars; }
    [[nodiscard]] LinkedVariable* findLinked(const QUuid& id);
    /// Find a linked variable by its source segment (nullptr if not published).
    [[nodiscard]] LinkedVariable* findLinkedBySource(const QUuid& blockId,
                                                     const QUuid& segmentId);
    /// Blocks whose length formulas reference (exact match) a linked variable
    /// sourced from the given block. When the source block is deleted those
    /// lengths are baked back to plain numbers (长度固化为数值) — used by
    /// RemoveBlockCommand to snapshot the consumers for undo.
    [[nodiscard]] QList<QUuid> linkedConsumerBlocks(const QUuid& sourceBlockId) const;
    /// Re-measure all linked variables from their source geometry, refresh
    /// cached values, and sync into the parameter map (cm domain).
    /// @param skipAuxSource When true, variables whose source block sits on
    ///        the (clean) auxiliary layer keep their cached value.
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
    [[nodiscard]] const std::vector<MeasureVariable>& measureVars() const { return m_measureVars; }
    [[nodiscard]] MeasureVariable* findMeasure(const QUuid& id);
    /// Reverse lookup: the measure variable OWNED by @p ownerBlockId (the
    /// measurement line created together with it), nullptr when the block
    /// owns none.
    [[nodiscard]] MeasureVariable* findMeasureByOwner(const QUuid& ownerBlockId);
    [[nodiscard]] const MeasureVariable* findMeasureByOwner(const QUuid& ownerBlockId) const;
    /// Re-measure all measure variables (two-point distances), refresh cached
    /// values, and sync into the parameter map (cm domain).
    /// @param skipAuxSource When true, measurements whose endpoints BOTH sit on
    ///        the (clean) auxiliary layer keep their cached value.
    /// Returns true if any measured value changed.
    bool measureMeasureVars(bool skipAuxSource = false);

    // --- Angle measure variables (two-segment relative angle) ---
    void addAngleMeasure(AngleMeasureVariable am);
    void removeAngleMeasure(const QUuid& id);
    void updateAngleMeasure(const AngleMeasureVariable& am);  ///< Rename / comment edit.
    [[nodiscard]] const std::vector<AngleMeasureVariable>& angleMeasures() const { return m_angleMeasures; }
    [[nodiscard]] AngleMeasureVariable* findAngleMeasure(const QUuid& id);
    /// Re-measure all angle measure variables (two-segment relative angles),
    /// refresh cached values, and sync into the parameter map (degree domain).
    /// @param skipAuxSource When true, measurements whose segments BOTH sit on
    ///        the (clean) auxiliary layer keep their cached value.
    /// Returns true if any measured value changed.
    bool measureAngleMeasureVars(bool skipAuxSource = false);

    /// Cascade for removeBlock(): drop every variable whose measurement
    /// target lives in the removed block (length-freeze of consumer formulas
    /// is orchestrated by ParamDocument BEFORE this call). Emits the
    /// per-family signals when anything was removed.
    void purgeBlockReferences(const QUuid& blockId);

    /// Clear all registries (document reset). No signals emitted.
    void clear();

    // --- Silent raw mutations (trusted callers only: deserializer / undo
    // replay). No signals, no resolve — the caller's pipeline owns the
    // eventual refresh (快照完整性 / 反序列化批量恢复).
    void addLinkedRaw(LinkedVariable lv);
    void addMeasureRaw(MeasureVariable mv);
    void addAngleMeasureRaw(AngleMeasureVariable am);

signals:
    void linkedVarsChanged();
    void measureVarsChanged();
    void angleMeasureVarsChanged();

private:
    ParamDocument* m_doc = nullptr;

    std::vector<LinkedVariable>        m_linkedVars;
    std::vector<MeasureVariable>       m_measureVars;
    std::vector<AngleMeasureVariable>  m_angleMeasures;
};

} // namespace cad::param
