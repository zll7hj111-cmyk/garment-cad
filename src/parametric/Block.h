#pragma once

#include <QUuid>
#include <QString>
#include <QHash>
#include <QList>
#include <vector>

#include "geometry/Vec2.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Condition.h"

namespace cad::param {

/// Rigid-body 2D transform: translation + rotation.
struct Transform2D {
    geo::Vec2 origin;         ///< World-coordinate position of local origin.
    double    rotation = 0.0; ///< Rotation in radians (CCW positive).

    /// Transform a local-coordinate point to world coordinates.
    [[nodiscard]] geo::Vec2 toWorld(const geo::Vec2& local) const;

    /// Transform a world-coordinate point to local coordinates.
    [[nodiscard]] geo::Vec2 toLocal(const geo::Vec2& world) const;
};

/// A Block is a rigid body containing parametric points and segments.
/// Dragging moves the whole Block (changes Transform);
/// editing shape means modifying internal parameters.
class Block
{
public:
    QUuid   id   = QUuid::createUuid();
    QString name;

    Transform2D transform;  ///< Rigid-body placement (drag changes this).

    std::vector<ParamPoint> points;    ///< Internal points (local coordinates).
    std::vector<Segment>    segments;  ///< Internal segments referencing points.

    bool isClosed = false;  ///< Whether this block forms a closed contour.

    /// Resolve all internal point positions based on constraint chain.
    /// @param params       Variable name→value map (cm) for formula evaluation.
    /// @param conditioned  formulaName→conditions for standalone-condition
    ///                     semantics (see ConditionEngine). May be empty.
    void resolve(const QHash<QString, double>& params = {},
                 const QHash<QString, QList<Condition>>& conditioned = {});

    /// Get the world-coordinate position of a point by ID.
    [[nodiscard]] geo::Vec2 worldPos(const QUuid& pointId) const;

    /// Find a mutable point by ID (nullptr if not found). O(1) via index.
    [[nodiscard]] ParamPoint* findPoint(const QUuid& pointId);

    /// Find a const point by ID (nullptr if not found). O(1) via index.
    [[nodiscard]] const ParamPoint* findPoint(const QUuid& pointId) const;

    /// Find a segment by ID (nullptr if not found).
    [[nodiscard]] Segment* findSegment(const QUuid& segmentId);

    /// Direction (radians, local coordinates, start->end) of the segment that
    /// has the given point as one of its endpoints. Returns 0 if no such
    /// segment exists or its endpoints are unresolved.
    [[nodiscard]] double directionAtPoint(const QUuid& pointId) const;

    /// Add a point and return its ID.
    QUuid addPoint(ParamPoint pt);

    /// Add a segment and return its ID.
    QUuid addSegment(Segment seg);

    /// Rebuild the internal point index (call after modifying points vector directly).
    void rebuildPointIndex();

private:
    QHash<QUuid, int> m_pointIndex;  ///< pointId -> index in points vector
};

} // namespace cad::param
