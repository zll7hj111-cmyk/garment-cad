#pragma once

#include <QUuid>

#include <vector>

#include "geometry/Vec2.h"
#include "tools/SnapEngine.h"

class QUndoStack;
class CanvasScene;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

/// A leader-segment candidate at the snapped start: any segment incident to
/// any point coincident with the snap position (within snap radius, across
/// blocks — attached points from several blocks stack on one spot).
struct LeaderCandidate {
    QUuid blockId;
    QUuid pointId;    ///< Attachment target point on that block.
    QUuid segmentId;  ///< Segment providing the reference direction.
};

/// One-shot construction values from the smart-pen pre-input strip (预输入):
/// the next line is born with these values instead of being edited afterwards.
/// Geometry fields carry the evaluated numbers used to place the endpoint;
/// formula fields (when non-empty) are stored on the created entities so the
/// Resolver keeps the line live-linked.
struct LineBuildOptions {
    QString name;                 ///< Segment name.
    bool   hasLength = false;     ///< A length was pre-typed.
    double lengthMm  = 0.0;       ///< Evaluated length in mm (geometry).
    QString lengthFormula;        ///< Original text when it was a formula.
    bool   hasAngle = false;      ///< An angle was pre-typed.
    double displayAngleDeg = 0.0; ///< Input-space angle: follower fold angle
                                  ///< (attached start, 闭合基准) or absolute
                                  ///< world angle (free start).
    QString angleFormula;         ///< Original text when it was a formula.
};

/// Creates parametric lines for the smart pen: free lines, lines attached to
/// an existing leader, and bridge lines (桥接线, both endpoints pinned,
/// length driven by a measure variable). Pure construction — no interaction
/// state; the caller passes the leader-candidate selection for attached /
/// bridge lines. All creations go through the undo stack (single command).
class LineFactory
{
public:
    using Vec2 = cad::geo::Vec2;

    LineFactory(cad::param::ParamDocument* doc, QUndoStack* undoStack,
                CanvasScene* scene);

    /// Create a free line (no snap): block origin at start, end Polar.
    /// @p opts carries the one-shot pre-input values (empty by default).
    void createFreeLine(const Vec2& start, const Vec2& end,
                        const LineBuildOptions& opts = {});

    /// Create a line attached to an existing block (snapped start).
    /// @p leaderIndex / @p candidates: the user-selected leader candidate
    /// (click / W during rubber band); -1 / empty = raw snap result +
    /// auto-picked exit segment.
    void createAttachedLine(const SnapResult& snapStart, const Vec2& end,
                            int leaderIndex,
                            const std::vector<LeaderCandidate>& candidates,
                            const LineBuildOptions& opts = {});

    /// Create a bridge line (桥接线): both endpoints pinned to existing
    /// points (snapped start AND end). Length/angle are passive — fully
    /// determined by the two host points; the Resolver re-derives them every
    /// pass. A measure variable publishes the distance as a formula param.
    /// Only @p opts.name applies to a bridge (length/angle stay passive).
    void createBridgeLine(const SnapResult& snapStart, const SnapResult& snapEnd,
                          int leaderIndex,
                          const std::vector<LeaderCandidate>& candidates,
                          const LineBuildOptions& opts = {});

private:
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;
    CanvasScene* m_scene = nullptr;
};

} // namespace cad::tools
