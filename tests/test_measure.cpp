/// @file test_measure.cpp
/// Measure tool regression:
///   - default mode measures the Euclidean two-point distance;
///   - W cycles 距离 → 水平 → 垂直 → 距离, and each committed
///     MeasureVariable carries the right kind and value (|dx| / |dy|);
///   - horizontal/vertical modes REFUSE to commit when the two points
///     coincide on the measured axis (dx≈0 / dy≈0) — no measure is
///     created, the tool stays mid-selection, and a later valid B commits.

#include <QtTest>
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QTimer>

#include <cmath>

#include "canvas/CanvasScene.h"
#include "tools/ToolMeasure.h"
#include "tools/MeasureResultDialog.h"
#include "parametric/ParamDocument.h"
#include "parametric/MeasureVariable.h"
#include "geometry/Vec2.h"

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

/// Block with two free points at @p a and @p b on working layer @p layerRow.
void makePointBlock(ParamDocument& doc, int layerRow, const Vec2& a, const Vec2& b)
{
    Block block;
    block.layer = layerIdAt(doc, layerRow);
    ParamPoint pa;
    pa.constraint = PointConstraint::Free;
    pa.freePos = a;
    ParamPoint pb;
    pb.constraint = PointConstraint::Free;
    pb.freePos = b;
    block.addPoint(std::move(pa));
    block.addPoint(std::move(pb));
    doc.addBlock(std::move(block));
}

/// Schedule an auto-reject of the modal MeasureResultDialog that
/// commitMeasure() opens — the timer fires inside the dialog's nested event
/// loop during exec(), so the commit finishes without a human.
void armDialogAutoDismiss()
{
    QTimer::singleShot(0, [] {
        const auto tops = QApplication::topLevelWidgets();
        for (QWidget* w : tops) {
            if (auto* dlg = qobject_cast<cad::tools::MeasureResultDialog*>(w)) {
                dlg->reject();
                return;
            }
        }
    });
}

/// Synthesize a left-press at @p p (mm) and deliver it to the tool.
void clickAt(cad::tools::ToolMeasure& tool, const QPointF& p)
{
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos(p);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    tool.mousePress(&press);
}

/// Synthesize the W key press (mode cycle).
void pressW(cad::tools::ToolMeasure& tool)
{
    QKeyEvent key(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
    tool.keyPress(&key);
}

} // namespace

class TestMeasure : public QObject
{
    Q_OBJECT

private slots:
    void distanceModeIsEuclidean();
    void wCyclesModesAndValues();
    void axisCoincidenceRejected();
};

void TestMeasure::distanceModeIsEuclidean()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    makePointBlock(doc, 1, Vec2(0.0, 0.0), Vec2(30.0, 40.0));
    doc.resolveAll();
    QGraphicsView view(&scene);

    cad::tools::ToolMeasure tool;
    tool.activate(scene, &doc);

    clickAt(tool, QPointF(0.0, 0.0));    // A
    armDialogAutoDismiss();
    clickAt(tool, QPointF(30.0, 40.0));  // B: |A-B| = 50 mm

    QCOMPARE(doc.measureVars().size(), size_t(1));
    const MeasureVariable& mv = doc.measureVars().front();
    QCOMPARE(mv.kind, MeasureKind::Distance);
    QVERIFY(std::abs(mv.value - 50.0) < 1e-9);
    tool.deactivate();
}

void TestMeasure::wCyclesModesAndValues()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    makePointBlock(doc, 1, Vec2(0.0, 0.0), Vec2(30.0, 40.0));
    doc.resolveAll();
    QGraphicsView view(&scene);

    cad::tools::ToolMeasure tool;
    tool.activate(scene, &doc);

    // Each iteration: W → A(0,0) → B(30,40). With points (0,0)/(30,40):
    //   水平 = |dx| = 30, 垂直 = |dy| = 40, 距离 = 50 — and the cycle wraps
    //   back to 水平 on the fourth W.
    const MeasureKind expectedKind[] = {
        MeasureKind::Horizontal, MeasureKind::Vertical,
        MeasureKind::Distance,   MeasureKind::Horizontal,
    };
    const double expectedValue[] = { 30.0, 40.0, 50.0, 30.0 };

    for (int i = 0; i < 4; ++i) {
        pressW(tool);
        clickAt(tool, QPointF(0.0, 0.0));
        armDialogAutoDismiss();
        clickAt(tool, QPointF(30.0, 40.0));
    }

    QCOMPARE(doc.measureVars().size(), size_t(4));
    for (int i = 0; i < 4; ++i) {
        const MeasureVariable& mv = doc.measureVars()[static_cast<size_t>(i)];
        QCOMPARE(mv.kind, expectedKind[i]);
        QVERIFY2(std::abs(mv.value - expectedValue[i]) < 1e-9,
                 qPrintable(QStringLiteral("measure %1 value").arg(i)));
    }
    tool.deactivate();
}

void TestMeasure::axisCoincidenceRejected()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    // First block: two points with the SAME x — a horizontal measure between
    // them must be refused. Second block: a recovery target at (30,40).
    makePointBlock(doc, 1, Vec2(0.0, 0.0), Vec2(0.0, 40.0));
    makePointBlock(doc, 1, Vec2(30.0, 40.0), Vec2(60.0, 40.0));
    doc.resolveAll();
    QGraphicsView view(&scene);

    cad::tools::ToolMeasure tool;
    tool.activate(scene, &doc);
    pressW(tool);  // → 水平

    clickAt(tool, QPointF(0.0, 0.0));   // A
    // B has the same x → the horizontal measure must be REFUSED: no measure
    // is created, no dialog opens, and the tool STAYS mid-selection.
    clickAt(tool, QPointF(0.0, 40.0));
    QCOMPARE(doc.measureVars().size(), size_t(0));

    // Still in SelectB: a different B commits normally as 水平.
    armDialogAutoDismiss();
    clickAt(tool, QPointF(30.0, 40.0));
    QCOMPARE(doc.measureVars().size(), size_t(1));
    const MeasureVariable& mv = doc.measureVars().front();
    QCOMPARE(mv.kind, MeasureKind::Horizontal);
    QVERIFY(std::abs(mv.value - 30.0) < 1e-9);
    tool.deactivate();
}

QTEST_MAIN(TestMeasure)
#include "test_measure.moc"
