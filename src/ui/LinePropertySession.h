#pragma once

#include <optional>
#include <QUuid>
#include <QString>
#include <QColor>
#include "parametric/ParamPoint.h"
#include "parametric/Attachment.h"

namespace cad::param {
class ParamDocument;
}

namespace cad::ui {

class SegmentAuxTab;

struct EndpointSnapshot
{
    QString name;
    QString annotation;
    bool showName = false;
};

/// Snapshot of line properties when LinePropertyDialog is opened.
struct LinePropertySnapshot
{
    QString segName;
    QString segAnnotation;
    bool showName = false;
    bool showLength = false;
    bool visible = true;
    int role = 0;
    QString lengthFormula;
    QColor color;
    double tension = 0.0;
    double distance = 0.0;
    QString distanceFormula;
    double angle = 0.0;
    QString angleFormula;
    cad::param::PointConstraint constraint = cad::param::PointConstraint::Free;
    QUuid refPointId;
    int lineStyle = 0;
    double weight = 1.0;
    bool lengthAuto = false;

    // Follower attachment state
    std::optional<cad::param::Attachment> followerAtt;

    // Aim target
    QUuid endTargetBlockId;
    QUuid endTargetPointId;
    double endTargetOffset = 0.0;
    QString endTargetOffsetFormula;

    // Endpoint states
    EndpointSnapshot startPoint;
    EndpointSnapshot endPoint;
};

/// Session manager for LinePropertyDialog snapshot capture, commit, and rollback.
class LinePropertySession
{
public:
    void takeSnapshot(cad::param::ParamDocument* doc,
                      const QUuid& blockId,
                      const QUuid& segmentId);

    bool commit(cad::param::ParamDocument* doc,
                const QUuid& blockId,
                const QUuid& segmentId,
                bool isCreation);

    void rollback(cad::param::ParamDocument* doc,
                  const QUuid& blockId,
                  const QUuid& segmentId,
                  bool isCreation,
                  SegmentAuxTab* auxTab);

    [[nodiscard]] const LinePropertySnapshot& snapshot() const { return m_snapshot; }

private:
    LinePropertySnapshot m_snapshot;
};

} // namespace cad::ui
