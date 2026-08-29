#pragma once

/// Trusted-pipeline channel for ParamDocument (P1-2, ARCHITECTURE_REVIEW).
///
/// These are the *Raw (silent restore) operations: they mutate the model
/// WITHOUT validation, signals or a resolve pass. They exist for exactly two
/// pipelines that already own a consistent snapshot:
///
///   * DocumentSerializer  — replaying a file into a freshly cleared document.
///   * QUndoCommand::undo/redo — replaying a snapshot the command itself took.
///   * (plus drag-cancel restore paths in the tools layer, which replay a
///     snapshot taken at drag start)
///
/// They used to be 14 public methods on the ParamDocument facade, where any UI
/// code could call them and bypass validation. They are now PRIVATE members;
/// this struct is the single friend that can reach them, so a caller must name
/// `RawModelAccess` explicitly — the trust is visible at the call site and
/// greppable (`RawModelAccess`).
///
/// Include this header ONLY from those pipelines. Everything else must go
/// through the validating facade API.
///
/// Usage:  cad::param::RawModelAccess::addBlockRaw(doc, std::move(block));

#include <QHash>
#include <QString>
#include <vector>

#include "parametric/Attachment.h"
#include "parametric/Block.h"
#include "parametric/Component.h"
#include "parametric/FormulaGroup.h"
#include "parametric/FormulaVariable.h"
#include "parametric/Layer.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "parametric/AngleMeasureVariable.h"
#include "parametric/ParamPoint.h"
#include "parametric/Variable.h"

namespace cad::param {

class ParamDocument;

struct RawModelAccess
{
    RawModelAccess() = delete;

    /// Add a block without resolving or recomputing groups (batch restore).
    static QUuid addBlockRaw(ParamDocument& doc, Block block);
    /// Add an attachment without resolving (batch restore / undo replay).
    static void addAttachmentRaw(ParamDocument& doc, Attachment att);
    static void addAttachmentsRaw(ParamDocument& doc,
                                  const std::vector<Attachment>& atts);
    /// Add a free point without emitting signals (batch restore).
    static void addFreePointRaw(ParamDocument& doc, ParamPoint pt);

    // --- Silent batch restore for the variable/measurement sub-domains ---
    /// No signals, no recompute/resolve: finishRestore() + the caller's
    /// resolveAll() own the eventual refresh.
    static void replaceLayersRaw(ParamDocument& doc, std::vector<Layer> layers);
    static void restoreVariableRaw(ParamDocument& doc, Variable var);
    static void restoreFormulaRaw(ParamDocument& doc, FormulaVariable formula);
    static void restoreFormulaGroupRaw(ParamDocument& doc, FormulaGroup group);
    /// Re-insert a formula group at registry position @p index (undo replay).
    static void insertFormulaGroupAt(ParamDocument& doc, int index,
                                     FormulaGroup group);
    static void restoreLinkedRaw(ParamDocument& doc, LinkedVariable lv);
    static void restoreMeasureRaw(ParamDocument& doc, MeasureVariable mv);
    static void restoreAngleMeasureRaw(ParamDocument& doc,
                                       AngleMeasureVariable am);
    static void restoreComponentRaw(ParamDocument& doc, Component comp);
    /// Batch-publish parameter entries without resolving (caller resolves once).
    static void publishParamsRaw(ParamDocument& doc,
                                 const QHash<QString, double>& cmValues);
};

} // namespace cad::param
