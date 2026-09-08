#pragma once

#include <QtTest>
#include <QApplication>
#include <QElapsedTimer>
#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>

#include <cmath>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Serial.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "ui/LinePropertyDialog.h"
#include "ui/SegmentAngleCard.h"
#include "ui/SegmentRefCard.h"
#include "ui/PointRefEdit.h"
#include "ui/SegmentAuxTab.h"
#include "ui/SegmentConnectionCard.h"
#include "ElaComboBox.h"
#include "ElaPushButton.h"
#include "ElaLineEdit.h"
#include "ui/AuxPointForm.h"
#include "ElaScrollPageArea.h"
#include "ElaText.h"
#include "geometry/Angle.h"
#include "TestHelpers.h"

using namespace cad::param;
using namespace cad::test;

namespace {

inline QUuid addAuxPoint(ParamDocument& doc, const QUuid& blockId, const QUuid& segId)
{
    Block* blk = doc.findBlock(blockId);
    ParamPoint pt;
    pt.constraint = PointConstraint::Interpolated;
    pt.hostSegmentId = segId;
    pt.isAuxiliary = true;
    pt.visible = true;
    pt.interpPercent = 0.5;
    pt.interpConstant = 0.0;
    pt.interpOffsetAngle = 0.0;
    pt.interpOffsetDist = 0.0;
    pt.serial = doc.newPointSerial();
    const QUuid id = blk->addPoint(pt);
    blk->findSegment(segId)->auxPointIds.push_back(id);
    return id;
}

inline QUuid addAnchorPoint(ParamDocument& doc, const QUuid& blockId, const QUuid& segId)
{
    Block* blk = doc.findBlock(blockId);
    auto* seg = blk->findSegment(segId);
    seg->type = SegmentType::Bezier;
    ParamPoint pp;
    pp.constraint = PointConstraint::CurveAnchor;
    pp.hostSegmentId = segId;
    pp.interpPercent = 0.5;
    pp.interpOffsetDist = 20.0;
    pp.autoTangent = true;
    pp.serial = doc.newPointSerial();
    const QUuid id = blk->addPoint(pp);
    seg->passPointIds.push_back(id);
    return id;
}

inline void setup(ParamDocument& doc, CanvasScene& scene, LineSetup& line)
{
    doc.setActiveLayer(layerIdAt(doc, 1));
    makeLine(doc, 100.0, Vec2(200.0, 0.0));
    line = makeLine(doc, 60.0);
}

inline QLineEdit* percentEditOf(cad::ui::AuxPointForm* form)
{
    for (auto* e : form->findChildren<QLineEdit*>())
        if (e->placeholderText().contains(QLatin1String("0.5")))
            return e;
    return nullptr;
}

} // namespace
