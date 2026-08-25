#pragma once

#include <QUuid>
#include <QString>
#include <QHash>
#include <vector>
#include <algorithm>

#include "geometry/Vec2.h"
#include "parametric/Block.h"

namespace cad::param {

// ────────────────────────────────────────────────────────────────────────────
// 组件 (Component, 用户拍板 2026-09): a PACKAGED set of Blocks.
// ────────────────────────────────────────────────────────────────────────────
// A component is a packaging/managing container over its member segments —
// it does NOT freeze or override their internal parametric relations. Member
// variables / expressions / line-level attachments keep resolving exactly as
// before (参数化活性铁律: 组件只是打包, 不失去内部关系).
//
// The component has an OVERALL pose driven by the (optional) single
// component-level attachment (Attachment::fromComponentId): when another line
// connects to the component's exposed endpoint, the WHOLE component follows
// that line as one rigid unit (借用端点连接 + 借用端点线段形成夹角), while the
// members' internal relations stay untouched.
//
// 暴露端点 (exposed endpoint): a member endpoint the component "lends" to an
// external connection. Recorded automatically on the FIRST component-level
// connect (可为空 before that). The direction basis for the external follower
// angle is the exposed endpoint's own segment direction (线段借来做夹角).
// ────────────────────────────────────────────────────────────────────────────
struct Component
{
    QUuid id = QUuid::createUuid();
    QString name;

    std::vector<QUuid> memberBlockIds;       ///< packaged members (segment blocks).

    QUuid exposedPointId;                    ///< exposed endpoint (a point on a member);
                                             ///< recorded on the FIRST component-level connect.
    QUuid exposedSegmentId;                  ///< segment owning the exposed endpoint
                                             ///< (direction basis for external connections).

    bool   showBoundingBox = true;           ///< bounding box visibility (管理页开关).
    double defaultAngleDeg = 0.0;            ///< creation-time overall orientation
                                             ///< (回到默认角度 target, degrees).
    QString defaultAngleFormula;             ///< Optional formula overriding
                                             ///< defaultAngleDeg (deg domain, 无换算).

    [[nodiscard]] bool isMember(const QUuid& blockId) const
    {
        for (const QUuid& id : memberBlockIds)
            if (id == blockId) return true;
        return false;
    }

    [[nodiscard]] int memberIndex(const QUuid& blockId) const
    {
        for (int i = 0; i < static_cast<int>(memberBlockIds.size()); ++i)
            if (memberBlockIds[i] == blockId) return i;
        return -1;
    }
};

/// Axis-aligned bounding box (world coordinates, mm).
struct BBox
{
    geo::Vec2 min;
    geo::Vec2 max;
    bool valid = false;

    void expand(const geo::Vec2& p)
    {
        if (!valid) { min = p; max = p; valid = true; return; }
        if (p.x < min.x) min.x = p.x;
        if (p.y < min.y) min.y = p.y;
        if (p.x > max.x) max.x = p.x;
        if (p.y > max.y) max.y = p.y;
    }

    [[nodiscard]] geo::Vec2 center() const { return (min + max) * 0.5; }
    [[nodiscard]] double width()  const { return max.x - min.x; }
    [[nodiscard]] double height() const { return max.y - min.y; }
};

} // namespace cad::param
