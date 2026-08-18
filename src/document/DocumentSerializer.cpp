#include "DocumentSerializer.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QUuid>
#include <QColor>
#include <QStringList>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Attachment.h"
#include "parametric/Group.h"
#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/FormulaGroup.h"
#include "parametric/Layer.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "parametric/AngleMeasureVariable.h"
#include "parametric/Condition.h"

namespace cad::param {
namespace {

// ─── UUID helpers ───
QString uuidStr(const QUuid& id) { return id.toString(QUuid::WithoutBraces); }
QUuid uuidFrom(const QString& s) { return QUuid::fromString(s); }

// ─── Enum string maps ───
// The two functions below must be extended IN PAIRS when PointConstraint
// gains a value (see the registry next to the enum in ParamPoint.h). The
// if-chain in pointConstraintFrom does NOT fail the build on a missing
// case — it silently degrades to Free and corrupts saved files on reload.
QString pointConstraintStr(PointConstraint c) {
    switch (c) {
    case PointConstraint::Free:         return "Free";
    case PointConstraint::Polar:        return "Polar";
    case PointConstraint::Midpoint:     return "Midpoint";
    case PointConstraint::OnSegment:    return "OnSegment";
    case PointConstraint::Intersection: return "Intersection";
    case PointConstraint::Interpolated: return "Interpolated";
    case PointConstraint::CurveAnchor:  return "CurveAnchor";
    }
    return "Free";
}
PointConstraint pointConstraintFrom(const QString& s, bool* recognized = nullptr) {
    if (s == "Polar")        { if (recognized) *recognized = true;  return PointConstraint::Polar; }
    if (s == "Midpoint")     { if (recognized) *recognized = true;  return PointConstraint::Midpoint; }
    if (s == "OnSegment")    { if (recognized) *recognized = true;  return PointConstraint::OnSegment; }
    if (s == "Intersection") { if (recognized) *recognized = true;  return PointConstraint::Intersection; }
    if (s == "Interpolated") { if (recognized) *recognized = true;  return PointConstraint::Interpolated; }
    if (s == "CurveAnchor")  { if (recognized) *recognized = true;  return PointConstraint::CurveAnchor; }
    // The DEFAULT branch is only a degradation when the string is neither the
    // canonical default nor empty (missing field = normal defaulting).
    if (recognized) *recognized = (s.isEmpty() || s == QLatin1String("Free"));
    return PointConstraint::Free;
}

QString segmentTypeStr(SegmentType t) {
    switch (t) {
    case SegmentType::Line:   return "Line";
    case SegmentType::Arc:    return "Arc";
    case SegmentType::Bezier: return "Bezier";
    }
    return "Line";
}
SegmentType segmentTypeFrom(const QString& s, bool* recognized = nullptr) {
    if (s == "Arc")    { if (recognized) *recognized = true;  return SegmentType::Arc; }
    if (s == "Bezier") { if (recognized) *recognized = true;  return SegmentType::Bezier; }
    if (recognized) *recognized = (s.isEmpty() || s == QLatin1String("Line"));
    return SegmentType::Line;
}

QString segmentRoleStr(SegmentRole r) {
    switch (r) {
    case SegmentRole::Outline:   return "Outline";
    case SegmentRole::Internal:  return "Internal";
    case SegmentRole::Auxiliary: return "Auxiliary";
    }
    return "Outline";
}
SegmentRole segmentRoleFrom(const QString& s, bool* recognized = nullptr) {
    if (s == "Internal")  { if (recognized) *recognized = true;  return SegmentRole::Internal; }
    if (s == "Auxiliary") { if (recognized) *recognized = true;  return SegmentRole::Auxiliary; }
    if (recognized) *recognized = (s.isEmpty() || s == QLatin1String("Outline"));
    return SegmentRole::Outline;
}

QString lineStyleStr(LineStyle s) {
    switch (s) {
    case LineStyle::Solid:  return "Solid";
    case LineStyle::Dashed: return "Dashed";
    case LineStyle::Dotted: return "Dotted";
    }
    return "Solid";
}
LineStyle lineStyleFrom(const QString& s, bool* recognized = nullptr) {
    if (s == "Dashed") { if (recognized) *recognized = true;  return LineStyle::Dashed; }
    if (s == "Dotted") { if (recognized) *recognized = true;  return LineStyle::Dotted; }
    if (recognized) *recognized = (s.isEmpty() || s == QLatin1String("Solid"));
    return LineStyle::Solid;
}

QString adjustModeStr(AdjustMode m) {
    return m == AdjustMode::PerStep ? "PerStep" : "Flat";
}
AdjustMode adjustModeFrom(const QString& s) {
    return s == "PerStep" ? AdjustMode::PerStep : AdjustMode::Flat;
}

// ─── Vec2 ───
QJsonObject vec2Json(const geo::Vec2& v) {
    return {{"x", v.x}, {"y", v.y}};
}
geo::Vec2 vec2From(const QJsonObject& o) {
    return {o["x"].toDouble(), o["y"].toDouble()};
}

// ─── Condition ───
QJsonObject conditionJson(const Condition& c) {
    return {
        {"id", uuidStr(c.id)},
        {"watchVar", c.watchVar},
        {"lowerOn", c.lowerOn},
        {"lower", c.lower},
        {"upperOn", c.upperOn},
        {"upper", c.upper},
        {"mode", adjustModeStr(c.mode)},
        {"step", c.step},
        {"amount", c.amount},
    };
}
Condition conditionFrom(const QJsonObject& o) {
    Condition c;
    c.id = uuidFrom(o["id"].toString());
    c.watchVar = o["watchVar"].toString();
    c.lowerOn = o["lowerOn"].toBool(true);
    c.lower = o["lower"].toDouble();
    c.upperOn = o["upperOn"].toBool(true);
    c.upper = o["upper"].toDouble();
    c.mode = adjustModeFrom(o["mode"].toString());
    c.step = o["step"].toDouble(1.0);
    c.amount = o["amount"].toDouble();
    return c;
}

// ─── ParamPoint ───
QJsonObject pointJson(const ParamPoint& p) {
    QJsonObject o;
    o["id"] = uuidStr(p.id);
    o["serial"] = p.serial;
    o["name"] = p.name;
    o["annotation"] = p.annotation;
    o["constraint"] = pointConstraintStr(p.constraint);
    o["freePos"] = vec2Json(p.freePos);
    o["refPointId"] = uuidStr(p.refPointId);
    o["distance"] = p.distance;
    o["angle"] = p.angle;
    o["refSegmentId"] = uuidStr(p.refSegmentId);
    o["distanceFormula"] = p.distanceFormula;
    o["angleFormula"] = p.angleFormula;
    o["refPointA"] = uuidStr(p.refPointA);
    o["refPointB"] = uuidStr(p.refPointB);
    o["ratio"] = p.ratio;
    o["hostSegmentId"] = uuidStr(p.hostSegmentId);
    o["interpRefPointId"] = uuidStr(p.interpRefPointId);
    o["interAngle"] = p.interAngle;
    o["interAngleFormula"] = p.interAngleFormula;
    o["interBidirectional"] = p.interBidirectional;
    o["interAimPointId"] = uuidStr(p.interAimPointId);
    o["interpPercent"] = p.interpPercent;
    o["interpPercentFormula"] = p.interpPercentFormula;
    o["interpConstant"] = p.interpConstant;
    o["interpConstantFormula"] = p.interpConstantFormula;
    o["interpOffsetAngle"] = p.interpOffsetAngle;
    o["interpOffsetAngleFormula"] = p.interpOffsetAngleFormula;
    o["interpOffsetDist"] = p.interpOffsetDist;
    o["interpOffsetDistFormula"] = p.interpOffsetDistFormula;
    o["interpFromEnd"] = p.interpFromEnd;
    // Curve anchor tangent handles
    o["tangentInX"] = p.tangentIn.x;
    o["tangentInY"] = p.tangentIn.y;
    o["tangentOutX"] = p.tangentOut.x;
    o["tangentOutY"] = p.tangentOut.y;
    o["tangentLocked"] = p.tangentLocked;
    o["autoTangent"] = p.autoTangent;
    o["isAuxiliary"] = p.isAuxiliary;
    o["visible"] = p.visible;
    o["selectable"] = p.selectable;
    o["showName"] = p.showName;
    return o;
}
ParamPoint pointFrom(const QJsonObject& o, QStringList* warnings = nullptr) {
    ParamPoint p;
    p.id = uuidFrom(o["id"].toString());
    p.serial = o["serial"].toString();
    p.name = o["name"].toString();
    p.annotation = o["annotation"].toString();
    bool recognized = true;
    p.constraint = pointConstraintFrom(o["constraint"].toString(), &recognized);
    if (!recognized && warnings) {
        const QString who = p.serial.isEmpty() ? p.id.toString() : p.serial;
        warnings->append(QString::fromUtf8("点 %1 的约束类型 \"%2\" 无法识别，已按自由点加载")
                             .arg(who).arg(o["constraint"].toString()));
    }
    p.freePos = vec2From(o["freePos"].toObject());
    p.refPointId = uuidFrom(o["refPointId"].toString());
    p.distance = o["distance"].toDouble();
    p.angle = o["angle"].toDouble();
    p.refSegmentId = uuidFrom(o["refSegmentId"].toString());
    p.distanceFormula = o["distanceFormula"].toString();
    p.angleFormula = o["angleFormula"].toString();
    p.refPointA = uuidFrom(o["refPointA"].toString());
    p.refPointB = uuidFrom(o["refPointB"].toString());
    p.ratio = o["ratio"].toDouble(0.5);
    p.hostSegmentId = uuidFrom(o["hostSegmentId"].toString());
    p.interpRefPointId = uuidFrom(o["interpRefPointId"].toString());
    p.interAngle = o["interAngle"].toDouble(90.0);
    p.interAngleFormula = o["interAngleFormula"].toString();
    p.interBidirectional = o["interBidirectional"].toBool();
    p.interAimPointId = uuidFrom(o["interAimPointId"].toString());
    p.interpPercent = o["interpPercent"].toDouble(0.5);
    p.interpPercentFormula = o["interpPercentFormula"].toString();
    p.interpConstant = o["interpConstant"].toDouble();
    p.interpConstantFormula = o["interpConstantFormula"].toString();
    p.interpOffsetAngle = o["interpOffsetAngle"].toDouble();
    p.interpOffsetAngleFormula = o["interpOffsetAngleFormula"].toString();
    p.interpOffsetDist = o["interpOffsetDist"].toDouble();
    p.interpOffsetDistFormula = o["interpOffsetDistFormula"].toString();
    p.interpFromEnd = o["interpFromEnd"].toBool();
    // Curve anchor tangent handles
    p.tangentIn = {o["tangentInX"].toDouble(), o["tangentInY"].toDouble()};
    p.tangentOut = {o["tangentOutX"].toDouble(), o["tangentOutY"].toDouble()};
    p.tangentLocked = o.contains("tangentLocked") ? o["tangentLocked"].toBool() : true;
    p.autoTangent = o.contains("autoTangent") ? o["autoTangent"].toBool() : true;
    p.isAuxiliary = o["isAuxiliary"].toBool();
    p.visible = o["visible"].toBool(true);
    p.selectable = o["selectable"].toBool(true);
    p.showName = o["showName"].toBool();
    return p;
}

// ─── Segment ───
QJsonObject segmentJson(const Segment& s) {
    QJsonArray auxIds;
    for (const auto& id : s.auxPointIds)
        auxIds.append(uuidStr(id));
    QJsonArray passIds;
    for (const auto& id : s.passPointIds)
        passIds.append(uuidStr(id));
    return {
        {"id", uuidStr(s.id)},
        {"serial", s.serial},
        {"name", s.name},
        {"type", segmentTypeStr(s.type)},
        {"role", segmentRoleStr(s.role)},
        {"startPointId", uuidStr(s.startPointId)},
        {"endPointId", uuidStr(s.endPointId)},
        {"ctrlPointId", uuidStr(s.ctrlPointId)},
        {"ctrlPoint2Id", uuidStr(s.ctrlPoint2Id)},
        {"tension", s.tension},
        {"constructAngle", s.constructAngle},
        {"isDriven", s.isDriven},
        {"lengthFormula", s.lengthFormula},
        {"lineStyle", lineStyleStr(s.lineStyle)},
        {"color", s.color.name()},
        {"weight", s.weight},
        {"visible", s.visible},
        {"showName", s.showName},
        {"showLength", s.showLength},
        {"auxPointIds", auxIds},
        {"passPointIds", passIds},
    };
}
Segment segmentFrom(const QJsonObject& o, QStringList* warnings = nullptr) {
    Segment s;
    s.id = uuidFrom(o["id"].toString());
    s.serial = o["serial"].toString();
    s.name = o["name"].toString();
    bool recType = true, recRole = true, recStyle = true;
    s.type = segmentTypeFrom(o["type"].toString(), &recType);
    s.role = segmentRoleFrom(o["role"].toString(), &recRole);
    s.startPointId = uuidFrom(o["startPointId"].toString());
    s.endPointId = uuidFrom(o["endPointId"].toString());
    s.ctrlPointId = uuidFrom(o["ctrlPointId"].toString());
    s.ctrlPoint2Id = uuidFrom(o["ctrlPoint2Id"].toString());
    s.tension = o["tension"].toDouble();
    for (const auto& v : o["passPointIds"].toArray())
        s.passPointIds.push_back(uuidFrom(v.toString()));
    s.constructAngle = o["constructAngle"].toDouble();
    s.isDriven = o["isDriven"].toBool();
    s.lengthFormula = o["lengthFormula"].toString();
    s.lineStyle = lineStyleFrom(o["lineStyle"].toString(), &recStyle);
    s.color = QColor(o["color"].toString("#1e1e1e"));
    s.weight = o["weight"].toDouble(1.2);
    s.visible = o["visible"].toBool(true);
    s.showName = o["showName"].toBool();
    s.showLength = o["showLength"].toBool();
    for (const auto& v : o["auxPointIds"].toArray())
        s.auxPointIds.push_back(uuidFrom(v.toString()));
    if (warnings) {
        const QString who = s.serial.isEmpty() ? s.id.toString() : s.serial;
        if (!recType)
            warnings->append(QString::fromUtf8("线段 %1 的类型 \"%2\" 无法识别，已按直线加载")
                                 .arg(who).arg(o["type"].toString()));
        if (!recRole)
            warnings->append(QString::fromUtf8("线段 %1 的角色 \"%2\" 无法识别，已按轮廓线加载")
                                 .arg(who).arg(o["role"].toString()));
        if (!recStyle)
            warnings->append(QString::fromUtf8("线段 %1 的线型 \"%2\" 无法识别，已按实线加载")
                                 .arg(who).arg(o["lineStyle"].toString()));
    }
    return s;
}

// ─── Block ───
QJsonObject blockJson(const Block& b) {
    QJsonArray pts, segs;
    for (const auto& p : b.points) pts.append(pointJson(p));
    for (const auto& s : b.segments) segs.append(segmentJson(s));
    return {
        {"id", uuidStr(b.id)},
        {"name", b.name},
        {"origin", vec2Json(b.transform.origin)},
        {"rotation", b.transform.rotation},
        {"isClosed", b.isClosed},
        {"isBridge", b.isBridge},
        {"layer", uuidStr(b.layer)},
        {"endTargetBlockId", uuidStr(b.endTargetBlockId)},
        {"endTargetPointId", uuidStr(b.endTargetPointId)},
        {"endTargetOffset", b.endTargetOffset},
        {"endTargetOffsetFormula", b.endTargetOffsetFormula},
        {"dartStartBlockId", uuidStr(b.dartStartBlockId)},
        {"dartStartPointId", uuidStr(b.dartStartPointId)},
        {"dartRefBlockId", uuidStr(b.dartRefBlockId)},
        {"dartRefPointId", uuidStr(b.dartRefPointId)},
        {"dartRefSegmentId", uuidStr(b.dartRefSegmentId)},
        {"dartOffsetMm", b.dartOffsetMm},
        {"dartOffsetFormula", b.dartOffsetFormula},
        {"dartAngleDeg", b.dartAngleDeg},
        {"dartAngleFormula", b.dartAngleFormula},
        {"points", pts},
        {"segments", segs},
    };
}
Block blockFrom(const QJsonObject& o, QStringList* warnings = nullptr,
                int* legacyLayerIndex = nullptr) {
    Block b;
    b.id = uuidFrom(o["id"].toString());
    b.name = o["name"].toString();
    b.transform.origin = vec2From(o["origin"].toObject());
    b.transform.rotation = o["rotation"].toDouble();
    b.isClosed = o["isClosed"].toBool();
    b.isBridge = o["isBridge"].toBool();  // Optional since v3 — defaults to false.
    // Layer reference: stable Layer::id string (current format) or a legacy
    // integer index (pre-id files). Legacy indices are reported through
    // @p legacyLayerIndex and remapped to layer ids by the caller once the
    // layer registry is restored.
    const QJsonValue layerVal = o["layer"];
    if (legacyLayerIndex)
        *legacyLayerIndex = -1;
    if (layerVal.isString()) {
        b.layer = uuidFrom(layerVal.toString());   // Missing/empty = null id.
    } else if (legacyLayerIndex) {
        *legacyLayerIndex = layerVal.toInt(0);     // Legacy index (default 0).
    }
    b.endTargetBlockId = uuidFrom(o["endTargetBlockId"].toString());
    b.endTargetPointId = uuidFrom(o["endTargetPointId"].toString());
    b.endTargetOffset = o["endTargetOffset"].toDouble();
    b.endTargetOffsetFormula = o["endTargetOffsetFormula"].toString();
    // Dart-line constraint (省道线, Optional since v7 — absent = plain line).
    b.dartStartBlockId  = uuidFrom(o["dartStartBlockId"].toString());
    b.dartStartPointId  = uuidFrom(o["dartStartPointId"].toString());
    b.dartRefBlockId    = uuidFrom(o["dartRefBlockId"].toString());
    b.dartRefPointId    = uuidFrom(o["dartRefPointId"].toString());
    b.dartRefSegmentId  = uuidFrom(o["dartRefSegmentId"].toString());
    b.dartOffsetMm      = o["dartOffsetMm"].toDouble();
    b.dartOffsetFormula = o["dartOffsetFormula"].toString();
    b.dartAngleDeg      = o["dartAngleDeg"].toDouble(90.0);
    b.dartAngleFormula  = o["dartAngleFormula"].toString();
    for (const auto& v : o["points"].toArray())
        b.addPoint(pointFrom(v.toObject(), warnings));
    for (const auto& v : o["segments"].toArray())
        b.addSegment(segmentFrom(v.toObject(), warnings));
    return b;
}

// ─── Attachment ───
QJsonObject attachmentJson(const Attachment& a) {
    return {
        {"id", uuidStr(a.id)},
        {"fromBlockId", uuidStr(a.fromBlockId)},
        {"fromPointId", uuidStr(a.fromPointId)},
        {"toBlockId", uuidStr(a.toBlockId)},
        {"toPointId", uuidStr(a.toPointId)},
        {"toSegmentId", uuidStr(a.toSegmentId)},
        {"followerAngle", a.followerAngle},
        {"followerAngleFormula", a.followerAngleFormula},
        {"rotationMode", static_cast<int>(a.rotationMode)},
        {"arcLength", a.arcLength},
        {"arcLengthFormula", a.arcLengthFormula},
        {"isPin", a.isPin},
        {"isLocked", a.isLocked},
        {"angleOnly", a.angleOnly},
        {"slideMode", static_cast<int>(a.slideMode)},
        {"slideAlongMm", a.slideAlongMm},
        {"slidePerpMm", a.slidePerpMm},
    };
}
Attachment attachmentFrom(const QJsonObject& o) {
    Attachment a;
    a.id = uuidFrom(o["id"].toString());
    a.fromBlockId = uuidFrom(o["fromBlockId"].toString());
    a.fromPointId = uuidFrom(o["fromPointId"].toString());
    a.toBlockId = uuidFrom(o["toBlockId"].toString());
    a.toPointId = uuidFrom(o["toPointId"].toString());
    // Optional since v2 — legacy documents leave it null (scan fallback).
    a.toSegmentId = uuidFrom(o["toSegmentId"].toString());
    a.followerAngle = o["followerAngle"].toDouble();
    a.followerAngleFormula = o["followerAngleFormula"].toString();
    // Validate the enum range — a corrupted file can carry any int here and
    // would otherwise fall into an unexpected branch (ArcLength checks etc.).
    // Unlike the string-based enums (point/segment/line-style), this one was
    // serialized as a raw int; unknown values degrade to the Angle default.
    const int mode = o["rotationMode"].toInt(0);
    a.rotationMode = (mode >= static_cast<int>(RotationMode::Angle)
                      && mode <= static_cast<int>(RotationMode::ArcLength))
        ? static_cast<RotationMode>(mode) : RotationMode::Angle;
    a.arcLength = o["arcLength"].toDouble();
    a.arcLengthFormula = o["arcLengthFormula"].toString();
    a.isPin = o["isPin"].toBool();  // Optional since v3 — defaults to false.
    a.isLocked = o["isLocked"].toBool();  // Optional since v4 — defaults to false.
    a.angleOnly = o["angleOnly"].toBool();  // Optional since v5 — defaults to false.
    // 滑轨模式 (Optional since v6): raw int with enum-range validation —
    // unknown values degrade to None (same defensive pattern as rotationMode).
    const int sm = o["slideMode"].toInt(0);
    a.slideMode = (sm >= static_cast<int>(cad::param::SlideMode::None)
                   && sm <= static_cast<int>(cad::param::SlideMode::PerpLeader))
        ? static_cast<cad::param::SlideMode>(sm) : cad::param::SlideMode::None;
    a.slideAlongMm = o["slideAlongMm"].toDouble();
    a.slidePerpMm = o["slidePerpMm"].toDouble();
    return a;
}

// ─── Group ───
QJsonObject groupJson(const Group& g) {
    QJsonObject o{
        {"id", uuidStr(g.id)},
        {"serial", g.serial},
        {"name", g.name},
        {"showBoundingBox", g.showBoundingBox},
        {"componentRootBlockId", uuidStr(g.componentRootBlockId)},
        {"hasHinge", g.hasHinge}
    };
    if (g.hasHinge) {
        o["hinge"] = QJsonObject{
            {"memberBlockId", uuidStr(g.hinge.memberBlockId)},
            {"memberPointId", uuidStr(g.hinge.memberPointId)},
            {"leaderBlockId", uuidStr(g.hinge.leaderBlockId)},
            {"leaderPointId", uuidStr(g.hinge.leaderPointId)},
            {"leaderSegmentId", uuidStr(g.hinge.leaderSegmentId)},
            {"followerAngle", g.hinge.followerAngle},
            {"followerAngleFormula", g.hinge.followerAngleFormula}
        };
    }
    return o;
}
Group groupFrom(const QJsonObject& o) {
    Group g;
    g.id = uuidFrom(o["id"].toString());
    g.serial = o["serial"].toString();
    g.name = o["name"].toString();
    g.showBoundingBox = o.contains("showBoundingBox") ? o["showBoundingBox"].toBool(true) : true;
    g.componentRootBlockId = uuidFrom(o["componentRootBlockId"].toString());
    g.hasHinge = o["hasHinge"].toBool(false);
    if (g.hasHinge) {
        const QJsonObject h = o["hinge"].toObject();
        g.hinge.memberBlockId = uuidFrom(h["memberBlockId"].toString());
        g.hinge.memberPointId = uuidFrom(h["memberPointId"].toString());
        g.hinge.leaderBlockId = uuidFrom(h["leaderBlockId"].toString());
        g.hinge.leaderPointId = uuidFrom(h["leaderPointId"].toString());
        g.hinge.leaderSegmentId = uuidFrom(h["leaderSegmentId"].toString());
        g.hinge.followerAngle = h["followerAngle"].toDouble(0.0);
        g.hinge.followerAngleFormula = h["followerAngleFormula"].toString();
    }
    return g;
}

// ─── Variable ───
QJsonObject variableJson(const Variable& v) {
    return {
        {"id", uuidStr(v.id)},
        {"name", v.name},
        {"refName", v.refName},
        {"value", v.value},
        {"comment", v.comment},
    };
}
Variable variableFrom(const QJsonObject& o) {
    Variable v;
    v.id = uuidFrom(o["id"].toString());
    v.name = o["name"].toString();
    v.refName = o["refName"].toString();
    v.value = o["value"].toDouble();
    v.comment = o["comment"].toString();
    return v;
}

// ─── FormulaVariable ───
QJsonObject formulaJson(const FormulaVariable& f) {
    QJsonArray conds;
    for (const auto& c : f.conditions)
        conds.append(conditionJson(c));
    QJsonObject o{
        {"id", uuidStr(f.id)},
        {"name", f.name},
        {"expression", f.expression},
        {"comment", f.comment},
        {"conditions", conds},
        {"conditionsEnabled", f.conditionsEnabled},
    };
    if (f.actualValueCm.has_value())
        o["actualValueCm"] = *f.actualValueCm;
    if (!f.groupId.isNull())
        o["groupId"] = uuidStr(f.groupId);
    return o;
}
FormulaVariable formulaFrom(const QJsonObject& o) {
    FormulaVariable f;
    f.id = uuidFrom(o["id"].toString());
    f.name = o["name"].toString();
    f.expression = o["expression"].toString();
    if (o.contains("actualValueCm"))
        f.actualValueCm = o["actualValueCm"].toDouble();
    f.comment = o["comment"].toString();
    for (const auto& v : o["conditions"].toArray())
        f.conditions.append(conditionFrom(v.toObject()));
    f.conditionsEnabled = o["conditionsEnabled"].toBool(true);
    f.groupId = uuidFrom(o["groupId"].toString());  // absent -> null (ungrouped)
    return f;
}

// ─── FormulaGroup ───
QJsonObject formulaGroupJson(const FormulaGroup& g) {
    return {
        {"id", uuidStr(g.id)},
        {"name", g.name},
        {"collapsed", g.collapsed},
    };
}
FormulaGroup formulaGroupFrom(const QJsonObject& o) {
    FormulaGroup g;
    g.id = uuidFrom(o["id"].toString());
    g.name = o["name"].toString();
    g.collapsed = o["collapsed"].toBool(false);
    return g;
}

// ─── Layer ───
QJsonObject layerJson(const Layer& l) {
    return {
        {"id", uuidStr(l.id)},
        {"name", l.name},
        {"visible", l.visible},
        {"type", l.type == LayerType::Auxiliary ? "auxiliary" : "working"},
    };
}
Layer layerFrom(const QJsonObject& o, QStringList* warnings = nullptr) {
    Layer l;
    // Files without per-layer ids (pre-id format) keep the generated id —
    // block layer references are remapped index→id by the caller.
    const QUuid fileId = uuidFrom(o["id"].toString());
    if (!fileId.isNull())
        l.id = fileId;
    l.name = o["name"].toString();
    l.visible = o["visible"].toBool(true);
    const QString typeStr = o["type"].toString();
    l.type = (typeStr == QLatin1String("auxiliary"))
                 ? LayerType::Auxiliary : LayerType::Working;
    // Unknown layer types silently become WORKING layers (sealed aux semantics
    // are too dangerous to guess) — report the degradation instead.
    if (warnings && !typeStr.isEmpty()
        && typeStr != QLatin1String("auxiliary")
        && typeStr != QLatin1String("working")) {
        warnings->append(QString::fromUtf8("图层 \"%1\" 的类型 \"%2\" 无法识别，已按工作层加载")
                             .arg(l.name).arg(typeStr));
    }
    return l;
}

// ─── LinkedVariable ───
QJsonObject linkedJson(const LinkedVariable& lv) {
    return {
        {"id", uuidStr(lv.id)},
        {"name", lv.name},
        {"refName", lv.refName},
        {"sourceBlockId", uuidStr(lv.sourceBlockId)},
        {"sourceSegmentId", uuidStr(lv.sourceSegmentId)},
        {"comment", lv.comment},
    };
}
LinkedVariable linkedFrom(const QJsonObject& o) {
    LinkedVariable lv;
    lv.id = uuidFrom(o["id"].toString());
    lv.name = o["name"].toString();
    lv.refName = o["refName"].toString();
    lv.sourceBlockId = uuidFrom(o["sourceBlockId"].toString());
    lv.sourceSegmentId = uuidFrom(o["sourceSegmentId"].toString());
    lv.comment = o["comment"].toString();
    return lv;
}

// ─── MeasureVariable ───
namespace {
QString measureKindStr(cad::param::MeasureKind k)
{
    switch (k) {
        case cad::param::MeasureKind::Horizontal: return QStringLiteral("h");
        case cad::param::MeasureKind::Vertical:   return QStringLiteral("v");
        case cad::param::MeasureKind::Distance:   break;
    }
    return QStringLiteral("dist");
}
cad::param::MeasureKind measureKindFrom(const QString& s)
{
    if (s == QStringLiteral("h")) return cad::param::MeasureKind::Horizontal;
    if (s == QStringLiteral("v")) return cad::param::MeasureKind::Vertical;
    return cad::param::MeasureKind::Distance;  // missing / legacy → distance
}
} // namespace

QJsonObject measureJson(const MeasureVariable& mv) {
    return {
        {"id", uuidStr(mv.id)},
        {"name", mv.name},
        {"refName", mv.refName},
        {"kind", measureKindStr(mv.kind)},
        {"blockA", uuidStr(mv.blockA)},
        {"pointA", uuidStr(mv.pointA)},
        {"blockB", uuidStr(mv.blockB)},
        {"pointB", uuidStr(mv.pointB)},
        {"ownerBlockId", uuidStr(mv.ownerBlockId)},
        {"comment", mv.comment},
    };
}
MeasureVariable measureFrom(const QJsonObject& o) {
    MeasureVariable mv;
    mv.id = uuidFrom(o["id"].toString());
    mv.name = o["name"].toString();
    mv.refName = o["refName"].toString();
    mv.kind = measureKindFrom(o["kind"].toString());
    mv.blockA = uuidFrom(o["blockA"].toString());
    mv.pointA = uuidFrom(o["pointA"].toString());
    mv.blockB = uuidFrom(o["blockB"].toString());
    mv.pointB = uuidFrom(o["pointB"].toString());
    mv.ownerBlockId = uuidFrom(o["ownerBlockId"].toString());
    mv.comment = o["comment"].toString();
    return mv;
}

// ─── AngleMeasureVariable ───
QJsonObject angleMeasureJson(const AngleMeasureVariable& am) {
    return {
        {"id", uuidStr(am.id)},
        {"name", am.name},
        {"refName", am.refName},
        {"blockA", uuidStr(am.blockA)},
        {"segmentA", uuidStr(am.segmentA)},
        {"blockB", uuidStr(am.blockB)},
        {"segmentB", uuidStr(am.segmentB)},
        {"comment", am.comment},
    };
}
AngleMeasureVariable angleMeasureFrom(const QJsonObject& o) {
    AngleMeasureVariable am;
    am.id = uuidFrom(o["id"].toString());
    am.name = o["name"].toString();
    am.refName = o["refName"].toString();
    am.blockA = uuidFrom(o["blockA"].toString());
    am.segmentA = uuidFrom(o["segmentA"].toString());
    am.blockB = uuidFrom(o["blockB"].toString());
    am.segmentB = uuidFrom(o["segmentB"].toString());
    am.comment = o["comment"].toString();
    return am;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════

QJsonObject DocumentSerializer::serialize(const ParamDocument& doc)
{
    // --- document.json ---
    QJsonObject docObj;

    // Parameters
    QJsonObject paramsObj;
    for (auto it = doc.parameters().cbegin(); it != doc.parameters().cend(); ++it)
        paramsObj[it.key()] = it.value();
    docObj["parameters"] = paramsObj;

    // Blocks
    QJsonArray blocksArr;
    for (const auto& b : doc.blocks())
        blocksArr.append(blockJson(b));
    docObj["blocks"] = blocksArr;

    // Canvas layers
    QJsonArray layersArr;
    for (const auto& l : doc.layers())
        layersArr.append(layerJson(l));
    docObj["layers"] = layersArr;
    docObj["activeLayer"] = uuidStr(doc.activeLayer());

    // Free points
    QJsonArray fpArr;
    for (const auto& p : doc.freePoints())
        fpArr.append(pointJson(p));
    docObj["freePoints"] = fpArr;

    // Attachments
    QJsonArray attArr;
    for (const auto& a : doc.attachments())
        attArr.append(attachmentJson(a));
    docObj["attachments"] = attArr;

    // Groups
    QJsonArray grpArr;
    for (const auto& g : doc.groups())
        grpArr.append(groupJson(g));
    docObj["groups"] = grpArr;

    // Block-group mapping
    QJsonObject bgObj;
    // Iterate via blocksInGroup for each group
    for (const auto& g : doc.groups()) {
        const auto blockIds = doc.blocksInGroup(g.id);
        for (const auto& bid : blockIds)
            bgObj[uuidStr(bid)] = uuidStr(g.id);
    }
    docObj["blockGroup"] = bgObj;

    // Serial counters
    docObj["pointSeq"] = doc.pointSeq();
    docObj["lineSeq"] = doc.lineSeq();
    docObj["groupSeq"] = doc.groupSeq();

    // --- variables.json ---
    QJsonObject varObj;
    QJsonArray varsArr;
    for (const auto& v : doc.variables())
        varsArr.append(variableJson(v));
    varObj["variables"] = varsArr;

    QJsonArray formulasArr;
    for (const auto& f : doc.formulas())
        formulasArr.append(formulaJson(f));
    varObj["formulas"] = formulasArr;

    QJsonArray formulaGroupsArr;
    for (const auto& g : doc.formulaGroups())
        formulaGroupsArr.append(formulaGroupJson(g));
    varObj["formulaGroups"] = formulaGroupsArr;

    QJsonArray linkedArr;
    for (const auto& lv : doc.linkedVars())
        linkedArr.append(linkedJson(lv));
    varObj["linkedVars"] = linkedArr;

    QJsonArray measureArr;
    for (const auto& mv : doc.measureVars())
        measureArr.append(measureJson(mv));
    varObj["measureVars"] = measureArr;

    QJsonArray angleArr;
    for (const auto& am : doc.angleMeasures())
        angleArr.append(angleMeasureJson(am));
    varObj["angleMeasures"] = angleArr;

    return {{"document", docObj}, {"variables", varObj}};
}

void DocumentSerializer::deserialize(ParamDocument& doc, const QJsonObject& root,
                                     QStringList* warnings)
{
    doc.clear();

    const QJsonObject docObj = root["document"].toObject();
    const QJsonObject varObj = root["variables"].toObject();

    // Serial counters
    doc.setSerialCounters(
        docObj["pointSeq"].toInt(1),
        docObj["lineSeq"].toInt(1),
        docObj["groupSeq"].toInt(1));

    // Canvas layers (restore before blocks: blocks reference layers by id).
    // Legacy migration: files written before the auxiliary calculation layer
    // have no aux layer — one is inserted first and every legacy block layer
    // INDEX shifts up by one (their old layer 0 was a WORKING layer). Files
    // without any "layers" array keep the default pair created by clear()
    // and get the same shift.
    bool legacyShift = false;
    if (docObj.contains(QStringLiteral("layers"))) {
        std::vector<Layer> restored;
        for (const auto& v : docObj["layers"].toArray())
            restored.push_back(layerFrom(v.toObject(), warnings));
        if (restored.empty()) {
            Layer fallback;
            fallback.name = QStringLiteral("图层 1");
            restored.push_back(std::move(fallback));
        }
        bool hasAux = false;
        for (const auto& l : restored)
            if (l.type == LayerType::Auxiliary) { hasAux = true; break; }
        if (!hasAux) {
            Layer aux;
            aux.name = QStringLiteral("辅助层");
            aux.type = LayerType::Auxiliary;
            restored.insert(restored.begin(), std::move(aux));
            legacyShift = true;
        }
        doc.replaceLayersRaw(std::move(restored));
    } else {
        legacyShift = true;  // default pair already contains the aux layer
    }
    // Active layer: stable id string (current format) or a legacy int index.
    // Default (field missing) = first working layer.
    const QJsonValue activeVal = docObj["activeLayer"];
    if (activeVal.isString()) {
        doc.setActiveLayer(uuidFrom(activeVal.toString()));
    } else {
        const int idx = qBound(0, activeVal.toInt(1) + (legacyShift ? 1 : 0),
                               doc.layerCount() - 1);
        doc.setActiveLayer(doc.layers()[static_cast<size_t>(idx)].id);
    }

    // Blocks (raw, no resolve). Legacy integer layer indices are collected
    // and remapped to the restored layers' stable ids below.
    struct LegacyLayerRef { QUuid blockId; int oldIndex; };
    std::vector<LegacyLayerRef> legacyLayerRefs;
    for (const auto& v : docObj["blocks"].toArray()) {
        int legacyIdx = -1;
        Block b = blockFrom(v.toObject(), warnings, &legacyIdx);
        const QUuid blockId = doc.addBlockRaw(std::move(b));
        if (legacyIdx >= 0)
            legacyLayerRefs.push_back({blockId, legacyIdx});
    }

    // Legacy migration: map old layer indices (shifted above the inserted aux
    // layer) onto the restored layers' stable ids.
    for (const auto& ref : legacyLayerRefs) {
        const int idx = qBound(0, ref.oldIndex + (legacyShift ? 1 : 0),
                               doc.layerCount() - 1);
        if (auto* b = doc.blockById(ref.blockId))
            b->layer = doc.layers()[static_cast<size_t>(idx)].id;
    }

    // Validate block layer refs: unknown ids (corrupt file / stale legacy
    // index) fall back to the first working layer, reported one by one
    // (逐条报告, no silent degradation).
    for (const auto& bc : doc.blocks()) {
        if (bc.layer.isNull())
            continue;  // Missing layer field: addBlock-style defaulting below.
        if (doc.layerIndex(bc.layer) >= 0)
            continue;
        if (warnings)
            warnings->append(QString::fromUtf8("线段块 %1 引用的图层 %2 不存在，已移到第一个工作层")
                                 .arg(bc.name, uuidStr(bc.layer)));
        if (auto* b = doc.blockById(bc.id))
            b->layer = doc.firstWorkingLayerId();
    }
    for (const auto& bc : doc.blocks())
        if (bc.layer.isNull())
            if (auto* b = doc.blockById(bc.id))
                b->layer = doc.firstWorkingLayerId();

    // Free points
    for (const auto& v : docObj["freePoints"].toArray())
        doc.addFreePointRaw(pointFrom(v.toObject(), warnings));

    // Attachments (raw, no resolve)
    for (const auto& v : docObj["attachments"].toArray())
        doc.addAttachmentRaw(attachmentFrom(v.toObject()));

    // Groups + block-group mapping (user groups, 成组). Membership entries
    // whose block no longer exists are dropped one by one (逐条报告, no
    // silent degradation); groups that end up with < 2 valid members vanish.
    std::vector<Group> groups;
    for (const auto& v : docObj["groups"].toArray())
        groups.push_back(groupFrom(v.toObject()));
    QHash<QUuid, QUuid> blockGroup;
    const QJsonObject bgObj = docObj["blockGroup"].toObject();
    for (auto it = bgObj.constBegin(); it != bgObj.constEnd(); ++it) {
        const QUuid blockId = uuidFrom(it.key());
        if (!doc.findBlock(blockId)) {
            if (warnings)
                warnings->append(QString::fromUtf8("组成员 %1 不存在，已从组映射中移除")
                                     .arg(it.key()));
            continue;
        }
        blockGroup.insert(blockId, uuidFrom(it.value().toString()));
    }
    std::vector<Group> validGroups;
    for (auto& g : groups) {
        int memberCount = 0;
        for (auto i = blockGroup.cbegin(); i != blockGroup.cend(); ++i)
            if (i.value() == g.id) ++memberCount;
        if (memberCount >= 2) {
            validGroups.push_back(std::move(g));
        } else {
            if (warnings)
                warnings->append(QString::fromUtf8("组 %1 有效成员不足两个，已丢弃")
                                     .arg(g.serial.isEmpty() ? g.id.toString() : g.serial));
            for (auto i = blockGroup.begin(); i != blockGroup.end(); )
                i = (i.value() == g.id) ? blockGroup.erase(i) : std::next(i);
        }
    }
    doc.restoreGroups(std::move(validGroups), std::move(blockGroup));

    // Variables
    for (const auto& v : varObj["variables"].toArray())
        doc.restoreVariableRaw(variableFrom(v.toObject()));

    // Formula groups (restore before formulas so membership can be validated)
    for (const auto& v : varObj["formulaGroups"].toArray())
        doc.restoreFormulaGroupRaw(formulaGroupFrom(v.toObject()));

    // Formulas
    for (const auto& v : varObj["formulas"].toArray()) {
        auto f = formulaFrom(v.toObject());
        // Drop a dangling group reference (group missing from the file).
        if (!f.groupId.isNull() && !doc.findFormulaGroup(f.groupId))
            f.groupId = QUuid();
        doc.restoreFormulaRaw(std::move(f));
    }

    // Linked variables
    for (const auto& v : varObj["linkedVars"].toArray())
        doc.restoreLinkedRaw(linkedFrom(v.toObject()));

    // Measure variables
    for (const auto& v : varObj["measureVars"].toArray())
        doc.restoreMeasureRaw(measureFrom(v.toObject()));

    // Angle measure variables
    for (const auto& v : varObj["angleMeasures"].toArray())
        doc.restoreAngleMeasureRaw(angleMeasureFrom(v.toObject()));

    // Recompute formulas (syncs parameters + resolves all blocks)
    doc.recomputeFormulas();

    // Refresh the UI: create canvas items for all blocks, rebuild panels.
    doc.finishRestore();
}

} // namespace cad::param
