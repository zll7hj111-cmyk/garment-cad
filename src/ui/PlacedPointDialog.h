#pragma once

#include "ElaDialog.h"
#include <QUuid>
#include <QTimer>
#include "parametric/ParamPoint.h"

class QLineEdit;
class ElaLineEdit;
class ElaText;
class ElaPushButton;
class CanvasScene;

namespace cad::param {
class ParamDocument;
}

namespace cad::ui {

/// Standalone property panel for points placed via ToolPlacePoint.
/// Allows editing offset distance, offset angle (with quick-angle buttons),
/// along-segment distance, and mathematical formulas with live canvas updates.
class PlacedPointDialog : public ElaDialog
{
    Q_OBJECT

public:
    PlacedPointDialog(const QUuid& blockId,
                      const QUuid& pointId,
                      cad::param::ParamDocument* doc,
                      CanvasScene* scene,
                      QWidget* parent = nullptr);
    ~PlacedPointDialog() override;

private slots:
    void onLiveUpdate();
    void onAccepted();
    void onRejected();
    void onDeletePoint();

private:
    void populateFromModel();
    void applyToPoint(cad::param::ParamPoint& pt) const;

    QUuid m_blockId;
    QUuid m_pointId;
    cad::param::ParamDocument* m_doc = nullptr;
    CanvasScene* m_scene = nullptr;
    cad::param::ParamPoint m_initialPoint;

    // Controls
    ElaText* m_lblSerial = nullptr;
    ElaText* m_lblHost = nullptr;
    ElaLineEdit* m_editName = nullptr;

    // Offset distance
    ElaLineEdit* m_editOffsetDist = nullptr;
    ElaLineEdit* m_editOffsetDistFormula = nullptr;

    // Offset angle
    ElaLineEdit* m_editOffsetAngle = nullptr;
    ElaLineEdit* m_editOffsetAngleFormula = nullptr;

    QTimer* m_debounce = nullptr;
};

} // namespace cad::ui
