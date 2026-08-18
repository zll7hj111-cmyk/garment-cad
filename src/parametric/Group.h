#pragma once

#include <QUuid>
#include <QString>
#include <QList>

namespace cad::param {

/// A USER-authored group (成组), upgraded to a first-class COMPONENT (组件).
/// The model layer stores explicit membership (ParamDocument's block-group map)
/// and — when a main hinge exists — participates in geometry solving through
/// Resolver's component pass. A component is a rigid body assembled from its
/// member blocks; the ROOT block (Group::componentRootBlockId) owns the
/// component pose. Any member endpoint may be the component connection point.
/// Only ONE main hinge is supported (单一主连接铰链, 用户拍板 2026-08).
/// Without a hinge the component is still an operation unit for selection,
/// drag/rotate and structural-edit guards; delete is NOT protected; deleting
/// members shrinks the group and auto-dissolves below 2 members.
struct ComponentHinge {
    QUuid memberBlockId;   ///< Member whose endpoint acts as the component connection point.
    QUuid memberPointId;   ///< Endpoint on that member (any member endpoint is allowed).
    QUuid leaderBlockId;   ///< External leader block (基准线宿主).
    QUuid leaderPointId;   ///< Point on the leader to snap to.
    QUuid leaderSegmentId; ///< Leader segment used as the follower-angle reference (optional).
    double followerAngle = 0.0;       ///< 组件角度 = 组件连接点所在线段相对基准线的夹角（度）。
    QString followerAngleFormula;     ///< Optional formula overriding followerAngle.
    [[nodiscard]] bool isValid() const
    {
        return !memberBlockId.isNull() && !memberPointId.isNull()
            && !leaderBlockId.isNull() && !leaderPointId.isNull();
    }
};

/// Resolver-facing snapshot of a component. Built by GroupRegistry so the
/// Resolver stays decoupled from the document/group-registry internals.
struct Component {
    QUuid groupId;             ///< Backing Group::id.
    QUuid rootBlockId;         ///< Component pose owner (must be a member).
    QList<QUuid> memberIds;    ///< Assembly member block ids.
    bool hasHinge = false;     ///< True = active component driven by the hinge.
    ComponentHinge hinge;      ///< Single main hinge (valid when hasHinge).
};

struct Group {
    QUuid   id = QUuid::createUuid();  ///< Internal stable identifier (not shown).
    QString serial;  ///< Human-readable ID, e.g. "m3p7qG1" (assigned by ParamDocument).
    QString name;    ///< User-editable group name (default empty).
    bool    showBoundingBox = true; ///< Canvas dashed bounding box visibility.

    /// Root block whose Transform2D is the component pose. Null until the
    /// registry pins it to the first surviving member. Only meaningful when
    /// the group acts as a component (all groups are components; a root is
    /// required once a hinge is set).
    QUuid componentRootBlockId;
    /// True when the component has a main hinge and Resolver must drive the
    /// whole component from that hinge (组件模式). False = ordinary group.
    bool hasHinge = false;
    ComponentHinge hinge;
};

} // namespace cad::param