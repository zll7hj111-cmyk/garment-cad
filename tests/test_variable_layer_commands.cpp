#include <QtTest>
#include <QUuid>
#include <QUndoStack>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/ExpressionEvaluator.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/VariableCommands.h"
#include "document/commands/DocumentCommands.h"
#include "document/commands/LayerCommands.h"
#include "geometry/Vec2.h"
#include "geometry/CurveMath.h"
#include "geometry/Units.h"

using namespace cad::param;
using cad::geo::Vec2;

namespace {

/// Test convenience: stable id of the display layer at @p row.
QUuid layerIdAt(const cad::param::ParamDocument& doc, int row)
{
    const auto& ls = doc.layers();
    return (row >= 0 && row < static_cast<int>(ls.size()))
        ? ls[static_cast<size_t>(row)].id : QUuid();
}

/// Create a minimal horizontal line block and add it to the document.
struct LineSetup {
    QUuid blockId;
    QUuid startId;
    QUuid endId;
    QUuid segId;
};

LineSetup makeLine(ParamDocument& doc, double lenMm, const Vec2& origin = Vec2::zero())
{
    Block block;
    block.transform.origin = origin;
    block.transform.rotation = 0.0;

    ParamPoint p1;
    p1.constraint = PointConstraint::Free;
    p1.freePos = Vec2::zero();
    QUuid startId = p1.id;

    ParamPoint p2;
    p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distance = lenMm;
    p2.angle = 0.0;
    QUuid endId = p2.id;

    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));

    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    QUuid segId = seg.id;
    block.addSegment(std::move(seg));

    QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    return {blockId, startId, endId, segId};
}

} // namespace

class TestVariableLayerCommands : public QObject
{
    Q_OBJECT

private slots:
    void recomputeFormulas_cycleFallsBackToFixpoint();
    void evaluator_caseInsensitiveFallback();
    void addVariable_undoRedo();
    void removeVariable_undoRedo();
    void setVariableValue_undoRedo();
    void addFormula_undoRedo();
    void measureCommands_undoRedo();
    void angleMeasureCommands_undoRedo();
    void bakeMeasureCopy_undoRedo();
    void bakeMeasureCopy_invalidCases();
    void layerCommands_activeLayerRestored();
};

void TestVariableLayerCommands::recomputeFormulas_cycleFallsBackToFixpoint()
{
    ParamDocument doc;
    Variable v;
    v.name = QStringLiteral("B");
    v.refName = QStringLiteral("b");
    v.value = 840.0;  // mm
    doc.addVariable(v);

    // Mutual cycle: A references F1, F1 references A.
    FormulaVariable a;
    a.name = QStringLiteral("A");
    a.expression = QStringLiteral("F1/2");
    const QUuid aId = a.id;
    doc.addFormula(a);
    FormulaVariable f1;
    f1.name = QStringLiteral("F1");
    f1.expression = QStringLiteral("A/2");
    const QUuid f1Id = f1.id;
    doc.addFormula(f1);

    // Self-reference: S = S+1.
    FormulaVariable s;
    s.name = QStringLiteral("S");
    s.expression = QStringLiteral("S+1");
    const QUuid sId = s.id;
    doc.addFormula(s);

    // A healthy leaf must still evaluate (the cycle fallback runs the full
    // bounded fixpoint, so non-cycle formulas keep working).
    FormulaVariable leaf;
    leaf.name = QStringLiteral("LEAF");
    leaf.expression = QStringLiteral("b/2+6");
    const QUuid leafId = leaf.id;
    doc.addFormula(leaf);

    doc.recomputeFormulas();

    // Cycle members: invalid (their partner never becomes available).
    QVERIFY(doc.findFormula(aId));
    QVERIFY(!doc.findFormula(aId)->valid);
    QVERIFY(doc.findFormula(f1Id));
    QVERIFY(!doc.findFormula(f1Id)->valid);
    QVERIFY(doc.findFormula(sId));
    QVERIFY(!doc.findFormula(sId)->valid);

    // Healthy formulas unaffected by the cycle fallback.
    QVERIFY(doc.findFormula(leafId)->valid);
    const double leafCm = cad::geo::Units::mmToCm(doc.findFormula(leafId)->baseValue);
    QVERIFY(std::abs(leafCm - (84.0 / 2.0 + 6.0)) < 1e-9);
}

// ---------------------------------------------------------------------------
// Case-insensitive variable fallback: a formula referencing a variable in a
// different case than its registered key must resolve through the O(1)
// per-pass normLookup (and produce the same value as the exact-key path).
// ---------------------------------------------------------------------------

void TestVariableLayerCommands::evaluator_caseInsensitiveFallback()
{
    ParamDocument doc;
    // Register the parameter ONLY in uppercase — the formula uses lowercase.
    doc.setParameter(QStringLiteral("B"), 84.0);  // cm

    // Block whose segment length is driven by the lowercase reference.
    Block block;
    block.layer = layerIdAt(doc, 1);
    ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2::zero();
    const QUuid startId = p1.id;
    ParamPoint p2; p2.constraint = PointConstraint::Polar;
    p2.refPointId = startId;
    p2.distanceFormula = QStringLiteral("b/2");  // lowercase -> case-folded hit
    const QUuid endId = p2.id;
    block.addPoint(std::move(p1));
    block.addPoint(std::move(p2));
    Segment seg;
    seg.startPointId = startId;
    seg.endPointId = endId;
    block.addSegment(std::move(seg));
    const QUuid blockId = block.id;
    doc.addBlock(std::move(block));
    doc.resolveAll();

    // b/2 = 84/2 = 42 cm = 420 mm; the Polar distance (mm) must reflect it.
    const auto* blk = doc.findBlock(blockId);
    QVERIFY(blk);
    const ParamPoint* ep = blk->findPoint(endId);
    QVERIFY(ep);
    QVERIFY(ep->resolved);
    QVERIFY(std::abs(ep->resolvedPos.x - 420.0) < 1e-9);

    // Same value through the direct evaluator path (no ctx -> linear scan).
    const auto direct = ExpressionEvaluator::evaluate(
        QStringLiteral("b/2"), QHash<QString, double>{{QStringLiteral("B"), 84.0}});
    QVERIFY(direct.ok);
    QVERIFY(std::abs(direct.value - 42.0) < 1e-9);
}

// ---------------------------------------------------------------------------
// AddVariableCommand: redo adds, undo removes, redo re-adds.
// ---------------------------------------------------------------------------

void TestVariableLayerCommands::addVariable_undoRedo()
{
    ParamDocument doc;
    QCOMPARE(static_cast<int>(doc.variables().size()), 0);

    Variable var;
    var.name = QStringLiteral("胸围");
    var.refName = QStringLiteral("b");
    var.value = 88.0;
    QUuid varId = var.id;

    cad::cmd::AddVariableCommand cmd(&doc, var);
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 1);
    QVERIFY(doc.findVariable(varId) != nullptr);
    QCOMPARE(doc.findVariable(varId)->value, 88.0);

    cmd.undo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 0);
    QVERIFY(doc.findVariable(varId) == nullptr);

    cmd.redo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 1);
    QCOMPARE(doc.findVariable(varId)->refName, QStringLiteral("b"));
}

// ---------------------------------------------------------------------------
// RemoveVariableCommand: redo removes, undo restores.
// ---------------------------------------------------------------------------

void TestVariableLayerCommands::removeVariable_undoRedo()
{
    ParamDocument doc;

    Variable var;
    var.name = QStringLiteral("腰围");
    var.refName = QStringLiteral("w");
    var.value = 72.0;
    QUuid varId = var.id;
    doc.addVariable(var);
    QCOMPARE(static_cast<int>(doc.variables().size()), 1);

    cad::cmd::RemoveVariableCommand cmd(&doc, varId);
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 0);

    // Undo restores with all fields.
    cmd.undo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 1);
    const auto* v = doc.findVariable(varId);
    QVERIFY(v);
    QCOMPARE(v->name, QStringLiteral("腰围"));
    QCOMPARE(v->refName, QStringLiteral("w"));
    QCOMPARE(v->value, 72.0);

    // Redo removes again.
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.variables().size()), 0);
}

// ---------------------------------------------------------------------------
// SetVariableValueCommand: redo sets new value, undo restores old value.
// ---------------------------------------------------------------------------

void TestVariableLayerCommands::setVariableValue_undoRedo()
{
    ParamDocument doc;

    Variable var;
    var.name = QStringLiteral("肩宽");
    var.refName = QStringLiteral("s");
    var.value = 40.0;
    QUuid varId = var.id;
    doc.addVariable(var);

    cad::cmd::SetVariableValueCommand cmd(&doc, varId, 45.0);
    cmd.redo();
    QCOMPARE(doc.findVariable(varId)->value, 45.0);

    cmd.undo();
    QCOMPARE(doc.findVariable(varId)->value, 40.0);

    cmd.redo();
    QCOMPARE(doc.findVariable(varId)->value, 45.0);
}

// ---------------------------------------------------------------------------
// AddFormulaCommand: redo adds formula, undo removes, redo re-adds.
// ---------------------------------------------------------------------------

void TestVariableLayerCommands::addFormula_undoRedo()
{
    ParamDocument doc;
    QCOMPARE(static_cast<int>(doc.formulas().size()), 0);

    FormulaVariable formula;
    formula.name = QStringLiteral("胸宽");
    formula.expression = QStringLiteral("b/2+6");
    QUuid formulaId = formula.id;

    cad::cmd::AddFormulaCommand cmd(&doc, formula);
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.formulas().size()), 1);
    QVERIFY(doc.findFormula(formulaId) != nullptr);
    QCOMPARE(doc.findFormula(formulaId)->expression, QStringLiteral("b/2+6"));

    cmd.undo();
    QCOMPARE(static_cast<int>(doc.formulas().size()), 0);
    QVERIFY(doc.findFormula(formulaId) == nullptr);

    cmd.redo();
    QCOMPARE(static_cast<int>(doc.formulas().size()), 1);
    QCOMPARE(doc.findFormula(formulaId)->name, QStringLiteral("胸宽"));
}

// ---------------------------------------------------------------------------
// Measure commands: remove/set round-trips keep all fields restorable.
// ---------------------------------------------------------------------------

void TestVariableLayerCommands::measureCommands_undoRedo()
{
    ParamDocument doc;
    const auto lineA = makeLine(doc, 100.0);
    const auto lineB = makeLine(doc, 80.0, Vec2{0.0, 50.0});

    MeasureVariable mv;
    mv.name = QStringLiteral("桥接距离");
    mv.refName = QStringLiteral("M_test1");
    mv.blockA = lineA.blockId;
    mv.pointA = lineA.endId;
    mv.blockB = lineB.blockId;
    mv.pointB = lineB.startId;
    mv.comment = QStringLiteral("备注");
    const QUuid mvId = mv.id;
    doc.addMeasure(mv);
    QCOMPARE(static_cast<int>(doc.measureVars().size()), 1);

    // Set: rename + comment round-trip.
    MeasureVariable edited = mv;
    edited.name = QStringLiteral("新名称");
    edited.comment = QStringLiteral("新备注");
    cad::cmd::SetMeasureCommand setCmd(&doc, edited);
    setCmd.redo();
    QCOMPARE(doc.findMeasure(mvId)->name, QStringLiteral("新名称"));
    QCOMPARE(doc.findMeasure(mvId)->comment, QStringLiteral("新备注"));
    setCmd.undo();
    QCOMPARE(doc.findMeasure(mvId)->name, QStringLiteral("桥接距离"));
    QCOMPARE(doc.findMeasure(mvId)->comment, QStringLiteral("备注"));

    // Remove: undo restores every field.
    cad::cmd::RemoveMeasureCommand rmCmd(&doc, mvId);
    rmCmd.redo();
    QCOMPARE(static_cast<int>(doc.measureVars().size()), 0);
    QVERIFY(doc.findMeasure(mvId) == nullptr);

    rmCmd.undo();
    QCOMPARE(static_cast<int>(doc.measureVars().size()), 1);
    const auto* restored = doc.findMeasure(mvId);
    QVERIFY(restored);
    QCOMPARE(restored->name, QStringLiteral("桥接距离"));
    QCOMPARE(restored->refName, QStringLiteral("M_test1"));
    QCOMPARE(restored->blockA, lineA.blockId);
    QCOMPARE(restored->pointA, lineA.endId);
    QCOMPARE(restored->blockB, lineB.blockId);
    QCOMPARE(restored->pointB, lineB.startId);

    rmCmd.redo();
    QCOMPARE(static_cast<int>(doc.measureVars().size()), 0);
}


void TestVariableLayerCommands::angleMeasureCommands_undoRedo()
{
    ParamDocument doc;
    const auto lineA = makeLine(doc, 100.0);
    const auto lineB = makeLine(doc, 80.0, Vec2{0.0, 50.0});

    AngleMeasureVariable am;
    am.name = QStringLiteral("夹角");
    am.refName = QStringLiteral("MA_test1");
    am.blockA = lineA.blockId;
    am.segmentA = lineA.segId;
    am.blockB = lineB.blockId;
    am.segmentB = lineB.segId;
    am.comment = QStringLiteral("备注");
    const QUuid amId = am.id;
    doc.addAngleMeasure(am);
    QCOMPARE(static_cast<int>(doc.angleMeasures().size()), 1);

    // Set: rename round-trip.
    AngleMeasureVariable edited = am;
    edited.name = QStringLiteral("新夹角");
    cad::cmd::SetAngleMeasureCommand setCmd(&doc, edited);
    setCmd.redo();
    QCOMPARE(doc.findAngleMeasure(amId)->name, QStringLiteral("新夹角"));
    setCmd.undo();
    QCOMPARE(doc.findAngleMeasure(amId)->name, QStringLiteral("夹角"));

    // Remove: undo restores every field.
    cad::cmd::RemoveAngleMeasureCommand rmCmd(&doc, amId);
    rmCmd.redo();
    QCOMPARE(static_cast<int>(doc.angleMeasures().size()), 0);

    rmCmd.undo();
    QCOMPARE(static_cast<int>(doc.angleMeasures().size()), 1);
    const auto* restored = doc.findAngleMeasure(amId);
    QVERIFY(restored);
    QCOMPARE(restored->refName, QStringLiteral("MA_test1"));
    QCOMPARE(restored->segmentA, lineA.segId);
    QCOMPARE(restored->segmentB, lineB.segId);

    rmCmd.redo();
    QCOMPARE(static_cast<int>(doc.angleMeasures().size()), 0);
}

// ---------------------------------------------------------------------------
// BakeMeasureCopyCommand (烘焙到操作层): baking = COPY, not move — the source
// measurement line stays on its layer as the measurement owner while a NEW
// free line appears on the target working layer at the source's world pose,
// its length live-linked to the measurement variable (M_xxx).
// ---------------------------------------------------------------------------

void TestVariableLayerCommands::bakeMeasureCopy_undoRedo()
{
    ParamDocument doc;
    QVERIFY(doc.layerCount() >= 2);
    QVERIFY(doc.isAuxLayer(layerIdAt(doc, 0)));
    const QUuid targetLayer = layerIdAt(doc, 1);

    // Source measure line on the AUX layer: 120 mm, world-rotated 30°.
    Block mline;
    mline.layer = layerIdAt(doc, 0);
    mline.transform.origin = Vec2{50.0, -40.0};
    mline.transform.rotation = 30.0 * M_PI / 180.0;
    ParamPoint p1; p1.constraint = PointConstraint::Free; p1.freePos = Vec2::zero();
    ParamPoint p2; p2.constraint = PointConstraint::Polar;
    p2.refPointId = p1.id; p2.distance = 120.0; p2.angle = 0.0;
    const QUuid mStart = p1.id, mEnd = p2.id;
    mline.addPoint(std::move(p1));
    mline.addPoint(std::move(p2));
    Segment mseg; mseg.startPointId = mStart; mseg.endPointId = mEnd;
    mline.addSegment(std::move(mseg));
    const QUuid mlId = mline.id;
    doc.addBlock(std::move(mline));

    MeasureVariable mv;
    mv.refName = QStringLiteral("M_bake1");
    mv.blockA = mlId; mv.pointA = mStart;
    mv.blockB = mlId; mv.pointB = mEnd;
    mv.ownerBlockId = mlId;
    const QUuid mvId = mv.id;
    doc.addMeasure(mv);
    doc.resolveAll();

    // findMeasureByOwner: reverse lookup by owner (and miss for strangers).
    QVERIFY(doc.findMeasureByOwner(mlId) != nullptr);
    QCOMPARE(doc.findMeasureByOwner(mlId)->id, mvId);
    QVERIFY(doc.findMeasureByOwner(QUuid::createUuid()) == nullptr);
    QVERIFY(doc.findMeasureByOwner(QUuid()) == nullptr);

    // Source world endpoints — the copy must land at exactly this pose.
    const auto* src = doc.findBlock(mlId);
    QVERIFY(src);
    const Vec2 srcStart = src->transform.toWorld(src->findPoint(mStart)->resolvedPos);
    const Vec2 srcEnd   = src->transform.toWorld(src->findPoint(mEnd)->resolvedPos);
    const double srcRotation = src->transform.rotation;

    cad::cmd::BakeMeasureCopyCommand cmd(&doc, mlId, targetLayer);
    QVERIFY(cmd.isValid());
    const QUuid newId = cmd.newBlockId();

    // --- redo: the copy appears on the target working layer ---
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 2);
    const auto* baked = doc.findBlock(newId);
    QVERIFY(baked);
    QCOMPARE(baked->layer, targetLayer);
    QCOMPARE(static_cast<int>(baked->segments.size()), 1);
    // Length = live link to the measurement variable (NOT the owner of it).
    QCOMPARE(baked->segments.front().lengthFormula, QStringLiteral("M_bake1"));
    QCOMPARE(doc.findMeasure(mvId)->ownerBlockId, mlId);
    // Free line: no attachments, no end aim.
    QVERIFY(doc.attachments().empty());
    QVERIFY(baked->endTargetBlockId.isNull());
    QVERIFY(!baked->isBridge);
    // World pose mirrors the source measure line; the M_bake1 formula
    // resolves to the measured length (120 mm).
    QVERIFY(std::abs(baked->transform.rotation - srcRotation) < 1e-9);
    const auto& bSeg = baked->segments.front();
    const Vec2 bStart = baked->transform.toWorld(
        baked->findPoint(bSeg.startPointId)->resolvedPos);
    const Vec2 bEnd = baked->transform.toWorld(
        baked->findPoint(bSeg.endPointId)->resolvedPos);
    QVERIFY((bStart - srcStart).length() < 1e-6);
    QVERIFY((bEnd - srcEnd).length() < 1e-6);
    QVERIFY(std::abs((bEnd - bStart).length() - 120.0) < 1e-6);

    // --- undo: the copy vanishes; source line + variable untouched ---
    cmd.undo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 1);
    QVERIFY(doc.findBlock(newId) == nullptr);
    QVERIFY(doc.findBlock(mlId) != nullptr);
    QCOMPARE(doc.findBlock(mlId)->layer, layerIdAt(doc, 0));
    QCOMPARE(static_cast<int>(doc.measureVars().size()), 1);
    QCOMPARE(doc.findMeasure(mvId)->ownerBlockId, mlId);
    QCOMPARE(doc.findMeasure(mvId)->refName, QStringLiteral("M_bake1"));

    // --- redo again restores the copy (stable id) ---
    cmd.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 2);
    baked = doc.findBlock(newId);
    QVERIFY(baked);
    QCOMPARE(baked->layer, targetLayer);
    QCOMPARE(baked->segments.front().lengthFormula, QStringLiteral("M_bake1"));
}

// ---------------------------------------------------------------------------
// BakeMeasureCopyCommand rejection paths: unknown source or a non-working
// target layer must produce an inert (invalid) command.
// ---------------------------------------------------------------------------

void TestVariableLayerCommands::bakeMeasureCopy_invalidCases()
{
    ParamDocument doc;

    // Unknown source block → invalid no-op.
    cad::cmd::BakeMeasureCopyCommand noSrc(&doc, QUuid::createUuid(), layerIdAt(doc, 1));
    QVERIFY(!noSrc.isValid());
    noSrc.redo();
    QCOMPARE(static_cast<int>(doc.blocks().size()), 0);
    noSrc.undo();   // must not crash / mutate
    QCOMPARE(static_cast<int>(doc.blocks().size()), 0);

    // A real measure line, but...
    const auto line = makeLine(doc, 100.0);
    MeasureVariable mv;
    mv.refName = QStringLiteral("M_bake2");
    mv.blockA = line.blockId; mv.pointA = line.startId;
    mv.blockB = line.blockId; mv.pointB = line.endId;
    mv.ownerBlockId = line.blockId;
    doc.addMeasure(mv);
    doc.resolveAll();

    // ...the AUX layer is not a valid bake target.
    cad::cmd::BakeMeasureCopyCommand auxTarget(&doc, line.blockId, layerIdAt(doc, 0));
    QVERIFY(!auxTarget.isValid());

    // ...an unknown layer id is rejected too.
    cad::cmd::BakeMeasureCopyCommand oobTarget(&doc, line.blockId, QUuid::createUuid());
    QVERIFY(!oobTarget.isValid());

    // A block WITHOUT an owned measure variable is not bakeable.
    const auto plain = makeLine(doc, 60.0, Vec2{0.0, 80.0});
    cad::cmd::BakeMeasureCopyCommand notMeasure(&doc, plain.blockId, layerIdAt(doc, 1));
    QVERIFY(!notMeasure.isValid());

    // A measure line whose segment END POINT never resolves is rejected
    // (Polar referencing a nonexistent point stays unresolved after resolveAll).
    Block dangling;
    dangling.layer = layerIdAt(doc, 0);
    ParamPoint dp1; dp1.constraint = PointConstraint::Free; dp1.freePos = Vec2::zero();
    ParamPoint dp2; dp2.constraint = PointConstraint::Polar;
    dp2.refPointId = QUuid::createUuid();   // dangling reference
    dp2.distance = 50.0; dp2.angle = 0.0;
    const QUuid dStart = dp1.id;
    dangling.addPoint(std::move(dp1));
    dangling.addPoint(std::move(dp2));
    Segment dseg; dseg.startPointId = dStart;
    dseg.endPointId = QUuid::createUuid();  // endpoint id matches no point
    dangling.addSegment(std::move(dseg));
    const QUuid dId = dangling.id;
    doc.addBlock(std::move(dangling));
    MeasureVariable dmv;
    dmv.refName = QStringLiteral("M_bake3");
    dmv.ownerBlockId = dId;
    doc.addMeasure(dmv);
    doc.resolveAll();
    cad::cmd::BakeMeasureCopyCommand unresolved(&doc, dId, layerIdAt(doc, 1));
    QVERIFY(!unresolved.isValid());

    // A ZERO-LENGTH measure line is rejected (degenerate direction).
    Block zero;
    zero.layer = layerIdAt(doc, 0);
    ParamPoint zp1; zp1.constraint = PointConstraint::Free; zp1.freePos = Vec2::zero();
    ParamPoint zp2; zp2.constraint = PointConstraint::Polar;
    zp2.refPointId = zp1.id; zp2.distance = 0.0; zp2.angle = 0.0;
    const QUuid zStart = zp1.id, zEnd = zp2.id;
    zero.addPoint(std::move(zp1));
    zero.addPoint(std::move(zp2));
    Segment zseg; zseg.startPointId = zStart; zseg.endPointId = zEnd;
    zero.addSegment(std::move(zseg));
    const QUuid zId = zero.id;
    doc.addBlock(std::move(zero));
    MeasureVariable zmv;
    zmv.refName = QStringLiteral("M_bake4");
    zmv.blockA = zId; zmv.pointA = zStart;
    zmv.blockB = zId; zmv.pointB = zEnd;
    zmv.ownerBlockId = zId;
    doc.addMeasure(zmv);
    doc.resolveAll();
    cad::cmd::BakeMeasureCopyCommand zeroLen(&doc, zId, layerIdAt(doc, 1));
    QVERIFY(!zeroLen.isValid());
}

// ---------------------------------------------------------------------------
// LayerCommands: add/remove-layer redo mutates m_activeLayer (the model layer
// clamps/shifts it) — undo must restore the PRE-COMMAND active layer, not the
// model-adjusted leftover.
// ---------------------------------------------------------------------------

void TestVariableLayerCommands::layerCommands_activeLayerRestored()
{
    ParamDocument doc;   // [aux(row 0), 图层 1(row 1)], active = 图层 1
    QCOMPARE(doc.layerCount(), 2);
    QCOMPARE(doc.activeLayer(), layerIdAt(doc, 1));

    doc.addLayer(QStringLiteral("w2"));   // row 2
    doc.addLayer(QStringLiteral("w3"));   // row 3
    QCOMPARE(doc.layerCount(), 4);

    // --- AddLayerCommand: undo restores the pre-command active layer ---
    doc.setActiveLayer(layerIdAt(doc, 1));
    cad::cmd::AddLayerCommand addCmd(&doc, QStringLiteral("w4"));
    addCmd.redo();
    QCOMPARE(doc.layerCount(), 5);
    QCOMPARE(doc.activeLayer(), layerIdAt(doc, 4));  // the new layer becomes active
    addCmd.undo();
    QCOMPARE(doc.layerCount(), 4);
    QCOMPARE(doc.activeLayer(), layerIdAt(doc, 1));  // snapshot restored

    // --- RemoveLayerCommand: undo restores the removed layer + the active
    //     snapshot. Removing the ACTIVE layer retargets the model to the
    //     first working layer until undo restores the snapshot.
    doc.setActiveLayer(layerIdAt(doc, 2));
    QCOMPARE(doc.layers()[2].name, QStringLiteral("w2"));
    cad::cmd::RemoveLayerCommand rmCmd(&doc, 2);
    rmCmd.redo();
    QCOMPARE(doc.layerCount(), 3);
    // Model fallback: first working layer (图层 1), NOT the removed w2.
    QCOMPARE(doc.activeLayer(), layerIdAt(doc, 1));
    rmCmd.undo();
    QCOMPARE(doc.layerCount(), 4);
    QCOMPARE(doc.activeLayer(), layerIdAt(doc, 2));
    QCOMPARE(doc.layerById(doc.activeLayer())->name, QStringLiteral("w2"));  // restored

    // Redo/undo round-trip keeps the restoration stable.
    rmCmd.redo();
    rmCmd.undo();
    QCOMPARE(doc.activeLayer(), layerIdAt(doc, 2));
    QCOMPARE(doc.layerById(doc.activeLayer())->name, QStringLiteral("w2"));
}

// ---------------------------------------------------------------------------
// 拆开影子基准 (用户拍板 2026-xx, DETACH_SHADOW_DESIGN.md §3 R1/R2 —— 翻案
// 旧「拆开保留角度 = 活引用」语义): SetAttachmentAngleOnlyCommand 现在复制
// 隐藏影子块作为角度基准 —— 拆开后旋转本体 A 不再影响 B (R1 去耦合, 影子 =
// 快照不是活引用); B 与原 A 的夹角偏移量 (90°) 原样保留, 基准 = 影子 (R2)。
// Undo restores the full connection AND removes the shadow (影子随之删除);
// Redo verbatim 重放 (影子 id 与首次一致)。
// ---------------------------------------------------------------------------

QTEST_MAIN(TestVariableLayerCommands)
#include "test_variable_layer_commands.moc"
