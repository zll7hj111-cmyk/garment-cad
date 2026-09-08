#pragma once

#include <QtTest>
#include <QApplication>
#include <QKeyEvent>
#include <QFile>
#include <QTextStream>
#include <QLineEdit>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QUndoStack>

#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "canvas/overlay/TransientOverlay.h"
#include "app/ContextStrip.h"
#include "tools/ToolManager.h"
#include "tools/ToolRotate.h"
#include "tools/RotateGizmo.h"
#include "tools/ToolSelect.h"
#include "ui/LinePropertyDialog.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include "parametric/ParamDocument.h"
#include "parametric/Duplicate.h"
#include "parametric/Serial.h"
#include "geometry/Angle.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/ComponentCommands.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::test::makeLine;
using cad::test::LineSetup;
using cad::test::layerIdAt;

class StripBridge
{
public:
    StripBridge(cad::param::ParamDocument* doc, cad::tools::ToolManager& tm)
        : strip(doc)
    {
        strip.setUndoStack(nullptr);
        strip.hide();
        QObject::connect(&tm, &cad::tools::ToolManager::pinnedTargetChanged,
                         &strip, [this](const QUuid& b, const QUuid& s) {
            pinnedBlock = b;
            pinnedSeg = s;
            if (s.isNull()) strip.clearPinned();
            else            strip.setPinnedTarget(b, s, /*grabFocus=*/false);
        });
        QObject::connect(&tm, &cad::tools::ToolManager::hoverTargetChanged,
                         &strip, [this](const QUuid& b, const QUuid& s) {
            if (s.isNull()) strip.clearHover();
            else            strip.setHoverTarget(b, s);   // 80ms 节流传到 strip
        });
        QObject::connect(&tm, &cad::tools::ToolManager::hintOverrideChanged,
                         &strip, [this](const QString& h) { hint = h; });
        QObject::connect(&tm, &cad::tools::ToolManager::rotateAnchorStateChanged,
                         &strip, [this](bool active, bool anchorIsEnd, bool canToggle,
                                        const QString& reason) {
            strip.setRotateAnchorState(active, anchorIsEnd, canToggle, reason);
        });
        QObject::connect(&strip, &cad::app::ContextStrip::reverseRequested,
                         &tm, &cad::tools::ToolManager::forwardReverseRequest);
    }

    cad::app::ContextStrip strip;
    QUuid pinnedBlock;
    QUuid pinnedSeg;
    QString hint;
};

inline void sendConfirm(CanvasView& view)
{
    QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier,
                      view.viewport()->rect().center());
    QTest::qWait(10);
}

inline void sendKeyEsc(CanvasView& view)
{
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &ev);
}

inline void sendKeyX(CanvasView& view)
{
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(&view, &ev);
}

inline double worldAngleDeg(const ParamDocument& doc, const QUuid& blockId)
{
    const Block* b = doc.findBlock(blockId);
    if (!b || b->segments.empty()) return 0.0;
    const Segment& seg = b->segments.front();
    const ParamPoint* sp = b->findPoint(seg.startPointId);
    const ParamPoint* ep = b->findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return 0.0;
    const Vec2 w1 = b->transform.toWorld(sp->resolvedPos);
    const Vec2 w2 = b->transform.toWorld(ep->resolvedPos);
    return (w2 - w1).angle() * 180.0 / M_PI;
}

inline const Attachment* cloneAttachment(const ParamDocument& doc,
                                         const QUuid& cloneId, const QUuid& originalId)
{
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == cloneId && a.toBlockId == originalId && !a.isPin)
            return &a;
    return nullptr;
}

inline const Attachment* followerAttachmentOf(const ParamDocument& doc,
                                              const QUuid& followerId)
{
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == followerId && !a.isPin)
            return &a;
    return nullptr;
}

inline Attachment attachCloneToOriginal(ParamDocument& doc, const Block& clone,
                                        const QUuid& originalId,
                                        const LineSetup& orig, double followerAngle)
{
    Attachment att;
    att.fromBlockId = clone.id;
    att.fromPointId = clone.points.front().id;
    att.toBlockId = originalId;
    att.toPointId = orig.startId;
    att.toSegmentId = orig.segId;
    att.followerAngle = followerAngle;
    att.rotationMode = RotationMode::Angle;
    return att;
}
