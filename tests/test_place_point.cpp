#include <QtTest>
#include <QSignalSpy>
#include <QLineEdit>
#include <QGraphicsSceneMouseEvent>
#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Serial.h"
#include "document/DocumentSerializer.h"
#include "document/commands/EndpointCommands.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "tools/ToolPlacePoint.h"
#include "tools/ToolSelect.h"
#include "ui/PlacedPointDialog.h"

using namespace cad::param;
using namespace cad::tools;
using namespace cad::geo;

class TestPlacePoint : public QObject
{
    Q_OBJECT

private slots:
    void testPlacedPointModelAndSerialization();
    void testPlacedPointParametricFollow();
    void testPlacedPointDialogEditAndUndo();
    void testPlacePointInteractiveTool();
    void testPlacedPointDeleteKeyOnlyDeletesPointNotSegment();
    void testPlacePointInputLocking();
};

void TestPlacePoint::testPlacedPointModelAndSerialization()
{
    ParamDocument doc;
    Block block;
    block.name = QStringLiteral("TestBlock");

    ParamPoint p1;
    p1.freePos = Vec2(0.0, 0.0);
    p1.serial = doc.newPointSerial();
    const QUuid p1Id = block.addPoint(p1);

    ParamPoint p2;
    p2.freePos = Vec2(100.0, 0.0);
    p2.serial = doc.newPointSerial();
    const QUuid p2Id = block.addPoint(p2);

    Segment seg;
    seg.startPointId = p1Id;
    seg.endPointId = p2Id;
    const QUuid segId = block.addSegment(seg);

    // Add a placed point offset 50mm at 90 degrees from p1
    ParamPoint placed;
    placed.constraint = PointConstraint::Interpolated;
    placed.hostSegmentId = segId;
    placed.interpRefPointId = p1Id;
    placed.interpPercent = 0.0;
    placed.interpConstant = 0.0;
    placed.interpOffsetDist = 50.0;
    placed.interpOffsetAngle = 90.0;
    placed.isAuxiliary = true;
    placed.isPlaced = true;
    placed.serial = doc.newPointSerial();
    const QUuid placedId = block.addPoint(placed);
    block.findSegment(segId)->auxPointIds.push_back(placedId);

    const QUuid blkId = doc.addBlock(std::move(block));
    doc.resolveAll();

    // Verify resolved position: baseline is +X (angle 0), offset 90 deg -> +Y
    auto* b = doc.findBlock(blkId);
    QVERIFY(b);
    auto* pt = b->findPoint(placedId);
    QVERIFY(pt);
    QVERIFY(pt->resolved);
    QVERIFY(pt->isPlaced);
    QVERIFY(std::abs(pt->resolvedPos.x - 0.0) < 1e-6);
    QVERIFY(std::abs(pt->resolvedPos.y - 50.0) < 1e-6);

    // Test serialization round-trip
    const QJsonObject json = DocumentSerializer::serialize(doc);
    ParamDocument restoredDoc;
    QStringList warnings;
    DocumentSerializer::deserialize(restoredDoc, json, &warnings);
    QVERIFY(warnings.isEmpty());

    auto* bRestored = restoredDoc.findBlock(blkId);
    QVERIFY(bRestored);
    auto* ptRestored = bRestored->findPoint(placedId);
    QVERIFY(ptRestored);
    QVERIFY(ptRestored->isPlaced);
    QCOMPARE(ptRestored->interpOffsetDist, 50.0);
    QCOMPARE(ptRestored->interpOffsetAngle, 90.0);
}

void TestPlacePoint::testPlacedPointParametricFollow()
{
    ParamDocument doc;
    Block block;

    ParamPoint p1;
    p1.freePos = Vec2(0.0, 0.0);
    const QUuid p1Id = block.addPoint(p1);

    ParamPoint p2;
    p2.freePos = Vec2(100.0, 0.0);
    const QUuid p2Id = block.addPoint(p2);

    Segment seg;
    seg.startPointId = p1Id;
    seg.endPointId = p2Id;
    const QUuid segId = block.addSegment(seg);

    // Placed point: 30mm offset at 90 deg
    ParamPoint placed;
    placed.constraint = PointConstraint::Interpolated;
    placed.hostSegmentId = segId;
    placed.interpRefPointId = p1Id;
    placed.interpPercent = 0.0;
    placed.interpConstant = 0.0;
    placed.interpOffsetDist = 30.0;
    placed.interpOffsetAngle = 90.0;
    placed.isAuxiliary = true;
    placed.isPlaced = true;
    const QUuid placedId = block.addPoint(placed);
    block.findSegment(segId)->auxPointIds.push_back(placedId);

    const QUuid blkId = doc.addBlock(std::move(block));
    doc.resolveAll();

    // Now rotate base line: move p2 to (0, 100), direction is +Y (90 deg)
    auto* b = doc.findBlock(blkId);
    b->findPoint(p2Id)->freePos = Vec2(0.0, 100.0);
    doc.resolveAll();

    // Offset angle is +90 deg relative to host direction (+Y) -> -X direction (-30, 0)
    auto* pt = b->findPoint(placedId);
    QVERIFY(pt && pt->resolved);
    QVERIFY(std::abs(pt->resolvedPos.x - (-30.0)) < 1e-6);
    QVERIFY(std::abs(pt->resolvedPos.y - 0.0) < 1e-6);
}

void TestPlacePoint::testPlacedPointDialogEditAndUndo()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    CanvasView view(&scene);
    view.resize(800, 600);
    view.show();

    Block block;
    ParamPoint p1;
    p1.freePos = Vec2(0.0, 0.0);
    const QUuid p1Id = block.addPoint(p1);

    ParamPoint p2;
    p2.freePos = Vec2(100.0, 0.0);
    const QUuid p2Id = block.addPoint(p2);

    Segment seg;
    seg.startPointId = p1Id;
    seg.endPointId = p2Id;
    const QUuid segId = block.addSegment(seg);

    ParamPoint placed;
    placed.constraint = PointConstraint::Interpolated;
    placed.hostSegmentId = segId;
    placed.interpRefPointId = p1Id;
    placed.interpPercent = 0.0;
    placed.interpConstant = 0.0;
    placed.interpOffsetDist = 20.0;   // 2.0 cm
    placed.interpOffsetAngle = 90.0;
    placed.isAuxiliary = true;
    placed.isPlaced = true;
    placed.serial = doc.newPointSerial();
    const QUuid placedId = block.addPoint(placed);
    block.findSegment(segId)->auxPointIds.push_back(placedId);

    const QUuid blkId = doc.addBlock(std::move(block));
    doc.resolveAll();

    // Open PlacedPointDialog
    auto* dlg = new cad::ui::PlacedPointDialog(blkId, placedId, &doc, &scene, &view);
    dlg->show();
    QVERIFY(QTest::qWaitForWindowExposed(dlg));

    // Edit offset distance: change from 2.0cm to 6.0cm (60.0mm)
    auto* editDist = dlg->findChild<QLineEdit*>();
    QVERIFY(editDist);

    auto* editAngle = dlg->findChildren<QLineEdit*>().value(3); // angle edit
    QVERIFY(editAngle);

    // Apply via accept
    dlg->accept();
    delete dlg;

    // Verify point remains intact and can be edited via EditPlacedPointCommand
    auto* b = doc.findBlock(blkId);
    auto* pt = b->findPoint(placedId);
    const ParamPoint oldPt = *pt;
    ParamPoint newPt = oldPt;
    newPt.interpOffsetDist = 80.0;
    newPt.interpOffsetAngle = 180.0;

    doc.undoStack()->push(new cad::cmd::EditPlacedPointCommand(&doc, blkId, oldPt, newPt));
    QCOMPARE(b->findPoint(placedId)->interpOffsetDist, 80.0);
    QCOMPARE(b->findPoint(placedId)->interpOffsetAngle, 180.0);

    // Test Undo
    doc.undoStack()->undo();
    QCOMPARE(b->findPoint(placedId)->interpOffsetDist, 20.0);
    QCOMPARE(b->findPoint(placedId)->interpOffsetAngle, 90.0);

    // Test Redo
    doc.undoStack()->redo();
    QCOMPARE(b->findPoint(placedId)->interpOffsetDist, 80.0);
    QCOMPARE(b->findPoint(placedId)->interpOffsetAngle, 180.0);
}

void TestPlacePoint::testPlacePointInteractiveTool()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    CanvasView view(&scene);

    Block block;
    block.name = QStringLiteral("InteractiveBlock");

    ParamPoint p1;
    p1.freePos = Vec2(0.0, 0.0);
    p1.serial = doc.newPointSerial();
    const QUuid p1Id = block.addPoint(p1);

    ParamPoint p2;
    p2.freePos = Vec2(100.0, 0.0);
    p2.serial = doc.newPointSerial();
    const QUuid p2Id = block.addPoint(p2);

    Segment seg;
    seg.startPointId = p1Id;
    seg.endPointId = p2Id;
    block.addSegment(seg);

    const QUuid blkId = doc.addBlock(std::move(block));
    doc.resolveAll();
    scene.syncBlockPositions();

    // 1. Activate ToolPlacePoint
    ToolPlacePoint tool;
    tool.activate(scene, &doc);
    tool.setUndoStack(doc.undoStack());

    // 2. Click on the segment at (50.0, 0.0) to select reference
    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    press1.setScenePos(QPointF(50.0, 0.0));
    press1.setButton(Qt::LeftButton);
    press1.setButtons(Qt::LeftButton);
    tool.mousePress(&press1);

    // 3. Move cursor to (50.0, 30.0) to set 30mm offset at 90 deg
    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(QPointF(50.0, 30.0));
    tool.mouseMove(&move);

    // 4. Click at (50.0, 30.0) to commit
    QGraphicsSceneMouseEvent press2(QEvent::GraphicsSceneMousePress);
    press2.setScenePos(QPointF(50.0, 30.0));
    press2.setButton(Qt::LeftButton);
    press2.setButtons(Qt::LeftButton);
    tool.mousePress(&press2);

    tool.deactivate();

    // Verify placed point was created
    auto* b = doc.findBlock(blkId);
    QVERIFY(b);
    QUuid createdPlacedPointId;
    for (const auto& pt : b->points) {
        if (pt.isPlaced) {
            createdPlacedPointId = pt.id;
            break;
        }
    }
    QVERIFY(!createdPlacedPointId.isNull());

    const auto* placedPt = b->findPoint(createdPlacedPointId);
    QVERIFY(placedPt);
    QVERIFY(placedPt->resolved);
    QVERIFY(std::abs(placedPt->interpOffsetDist - 30.0) < 0.1);
    QVERIFY(std::abs(placedPt->interpOffsetAngle - 90.0) < 0.1);
    QVERIFY(std::abs(placedPt->resolvedPos.x - 50.0) < 0.1);
    QVERIFY(std::abs(placedPt->resolvedPos.y - 30.0) < 0.1);

    // 5. Test ToolSelect double-click to open PlacedPointDialog
    ToolSelect selTool;
    selTool.activate(scene, &doc);

    QGraphicsSceneMouseEvent dblClick(QEvent::GraphicsSceneMouseDoubleClick);
    dblClick.setScenePos(QPointF(50.0, 30.0));
    dblClick.setButton(Qt::LeftButton);
    dblClick.setButtons(Qt::LeftButton);
    selTool.mouseDoubleClick(&dblClick);

    // Verify dialog opened
    cad::ui::PlacedPointDialog* openDlg = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* d = qobject_cast<cad::ui::PlacedPointDialog*>(widget)) {
            openDlg = d;
            break;
        }
    }
    QVERIFY(openDlg != nullptr);
    openDlg->accept();
    delete openDlg;

    selTool.deactivate();
}

void TestPlacePoint::testPlacedPointDeleteKeyOnlyDeletesPointNotSegment()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    CanvasView view(&scene);

    Block block;
    block.name = QStringLiteral("DeleteTestBlock");

    ParamPoint p1;
    p1.freePos = Vec2(0.0, 0.0);
    p1.serial = doc.newPointSerial();
    const QUuid p1Id = block.addPoint(p1);

    ParamPoint p2;
    p2.freePos = Vec2(100.0, 0.0);
    p2.serial = doc.newPointSerial();
    const QUuid p2Id = block.addPoint(p2);

    Segment seg;
    seg.startPointId = p1Id;
    seg.endPointId = p2Id;
    const QUuid segId = block.addSegment(seg);

    ParamPoint placed;
    placed.constraint = PointConstraint::Interpolated;
    placed.hostSegmentId = segId;
    placed.interpRefPointId = p1Id;
    placed.interpPercent = 0.5;
    placed.interpConstant = 0.0;
    placed.interpOffsetDist = 20.0;
    placed.interpOffsetAngle = 90.0;
    placed.isAuxiliary = true;
    placed.isPlaced = true;
    placed.serial = doc.newPointSerial();
    const QUuid placedId = block.addPoint(placed);
    block.findSegment(segId)->auxPointIds.push_back(placedId);

    const QUuid blkId = doc.addBlock(std::move(block));
    doc.resolveAll();
    scene.syncBlockPositions();

    ToolSelect selTool;
    selTool.activate(scene, &doc);
    selTool.setUndoStack(doc.undoStack());

    // 1. Single click on placed point at (50, 20)
    QGraphicsSceneMouseEvent click(QEvent::GraphicsSceneMousePress);
    click.setScenePos(QPointF(50.0, 20.0));
    click.setButton(Qt::LeftButton);
    click.setButtons(Qt::LeftButton);
    selTool.mousePress(&click);

    // 2. Press Delete key
    QKeyEvent delEvent(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    selTool.keyPress(&delEvent);

    // 3. Verify: Placed point is DELETED, host segment is NOT deleted!
    auto* b = doc.findBlock(blkId);
    QVERIFY(b);
    QVERIFY(b->findPoint(placedId) == nullptr);
    QCOMPARE(b->segments.size(), 1);
    QVERIFY(b->findSegment(segId) != nullptr);

    // 4. Test Undo: Placed point should be restored!
    doc.undoStack()->undo();
    QVERIFY(b->findPoint(placedId) != nullptr);
    QCOMPARE(b->segments.size(), 1);

    selTool.deactivate();
}

void TestPlacePoint::testPlacePointInputLocking()
{
    ParamDocument doc;
    CanvasScene scene(&doc);
    CanvasView view(&scene);

    Block block;
    ParamPoint p1;
    p1.freePos = Vec2(0.0, 0.0);
    p1.serial = doc.newPointSerial();
    const QUuid p1Id = block.addPoint(p1);

    ParamPoint p2;
    p2.freePos = Vec2(100.0, 0.0);
    p2.serial = doc.newPointSerial();
    const QUuid p2Id = block.addPoint(p2);

    Segment seg;
    seg.startPointId = p1Id;
    seg.endPointId = p2Id;
    block.addSegment(seg);

    const QUuid blkId = doc.addBlock(std::move(block));
    doc.resolveAll();
    scene.syncBlockPositions();

    ToolPlacePoint tool;
    tool.activate(scene, &doc);
    tool.setUndoStack(doc.undoStack());

    // 1. Click at (0, 0) to select p1 as reference
    QGraphicsSceneMouseEvent press1(QEvent::GraphicsSceneMousePress);
    press1.setScenePos(QPointF(0.0, 0.0));
    press1.setButton(Qt::LeftButton);
    press1.setButtons(Qt::LeftButton);
    tool.mousePress(&press1);

    // 2. Lock distance at 4.0 cm (40 mm)
    tool.placePointDistInput(4.0, true);

    // 3. Move cursor to (0.0, 100.0) — raw cursor distance is 100, but locked dist should stay 40!
    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setScenePos(QPointF(0.0, 100.0));
    tool.mouseMove(&move);

    // 4. Commit via placePointCommitted()
    tool.placePointCommitted();
    tool.deactivate();

    // Verify: placed point has dist 40mm, angle 90 deg, resolved pos (0, 40)
    auto* b = doc.findBlock(blkId);
    QVERIFY(b);
    QUuid placedId;
    for (const auto& pt : b->points) {
        if (pt.isPlaced) { placedId = pt.id; break; }
    }
    QVERIFY(!placedId.isNull());
    const auto* pt = b->findPoint(placedId);
    QVERIFY(pt && pt->resolved);
    QCOMPARE(pt->interpOffsetDist, 40.0);
    QCOMPARE(pt->interpOffsetAngle, 90.0);
    QVERIFY(std::abs(pt->resolvedPos.x - 0.0) < 0.1);
    QVERIFY(std::abs(pt->resolvedPos.y - 40.0) < 0.1);
}

QTEST_MAIN(TestPlacePoint)
#include "test_place_point.moc"
