#include "DocumentSerializer.h"

#include <algorithm>
#include <array>
#include <utility>
#include <tuple>
#include <cmath>
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
#include "parametric/Variable.h"
#include "parametric/FormulaVariable.h"
#include "parametric/FormulaGroup.h"
#include "parametric/Layer.h"
#include "parametric/LinkedVariable.h"
#include "parametric/MeasureVariable.h"
#include "parametric/AngleMeasureVariable.h"
#include "parametric/Condition.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::param {
namespace {

// ─── UUID helpers ───
QString uuidStr(const QUuid& id) { return id.toString(QUuid::WithoutBraces); }
QUuid uuidFrom(const QString& s) { return QUuid::fromString(s); }

// ─── Enum string maps ───
// 序列化成对映射表驱动 (2026-08-28 收口 B4)。原四个手写 switch + if 链改为
// 单张 constexpr 表 + 双向查找 —— 新增枚举只改一处表 (str→enum 与 enum→str
// 同表必然同步; 旧的 From 侧 if 链漏分支不报错、静默降级损坏存档, 现为
// 表项缺失走 canonical default, 语义逐字节不变)。
// 登记表规则: 第一行 = canonical default (未知枚举/未知字符串的降级目标),
// 与 ParamPoint.h 枚举旁的 12 处登记同步扩展。
template <typename E, std::size_t N>
QString enumToStr(const std::array<std::pair<E, const char*>, N>& table, E v,
                  const char* fallback)
{
    for (const auto& [e, s] : table)
        if (e == v) return QLatin1String(s);
    return QLatin1String(fallback);
}

/// @p recognized 语义与旧实现逐位一致: 命中表中任一 (含默认行) = true;
/// 未知字符串 = false (s 非空且不是默认名)。空串 = 字段缺失, 正常默认化。
template <typename E, std::size_t N>
E enumFromStr(const std::array<std::pair<E, const char*>, N>& table,
              const QString& s, const char* defaultName, E defaultVal,
              bool* recognized)
{
    for (const auto& [e, name] : table) {
        if (s == QLatin1String(name)) {
            if (recognized) *recognized = true;
            return e;
        }
    }
    if (recognized) *recognized =
        (s.isEmpty() || s == QLatin1String(defaultName));
    return defaultVal;
}

constexpr std::array<std::pair<PointConstraint, const char*>, 7>
    kPointConstraintMap = {{
        {PointConstraint::Free,         "Free"},
        {PointConstraint::Polar,        "Polar"},
        {PointConstraint::Midpoint,     "Midpoint"},
        {PointConstraint::OnSegment,    "OnSegment"},
        {PointConstraint::Intersection, "Intersection"},
        {PointConstraint::Interpolated, "Interpolated"},
        {PointConstraint::CurveAnchor,  "CurveAnchor"},
    }};
QString pointConstraintStr(PointConstraint c) {
    return enumToStr(kPointConstraintMap, c, "Free");
}
PointConstraint pointConstraintFrom(const QString& s, bool* recognized = nullptr) {
    return enumFromStr(kPointConstraintMap, s, "Free",
                       PointConstraint::Free, recognized);
}

constexpr std::array<std::pair<SegmentType, const char*>, 3>
    kSegmentTypeMap = {{
        {SegmentType::Line,   "Line"},
        {SegmentType::Arc,    "Arc"},
        {SegmentType::Bezier, "Bezier"},
    }};
QString segmentTypeStr(SegmentType t) {
    return enumToStr(kSegmentTypeMap, t, "Line");
}
SegmentType segmentTypeFrom(const QString& s, bool* recognized = nullptr) {
    return enumFromStr(kSegmentTypeMap, s, "Line",
                       SegmentType::Line, recognized);
}

constexpr std::array<std::pair<SegmentRole, const char*>, 3>
    kSegmentRoleMap = {{
        {SegmentRole::Outline,   "Outline"},
        {SegmentRole::Internal,  "Internal"},
        {SegmentRole::Auxiliary, "Auxiliary"},
    }};
QString segmentRoleStr(SegmentRole r) {
    return enumToStr(kSegmentRoleMap, r, "Outline");
}
SegmentRole segmentRoleFrom(const QString& s, bool* recognized = nullptr) {
    return enumFromStr(kSegmentRoleMap, s, "Outline",
                       SegmentRole::Outline, recognized);
}

constexpr std::array<std::pair<LineStyle, const char*>, 3>
    kLineStyleMap = {{
        {LineStyle::Solid,  "Solid"},
        {LineStyle::Dashed, "Dashed"},
        {LineStyle::Dotted, "Dotted"},
    }};
QString lineStyleStr(LineStyle s) {
    return enumToStr(kLineStyleMap, s, "Solid");
}
LineStyle lineStyleFrom(const QString& s, bool* recognized = nullptr) {
    return enumFromStr(kLineStyleMap, s, "Solid",
                       LineStyle::Solid, recognized);
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
    o["interUseWorldAngle"] = p.interUseWorldAngle;
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
    p.interUseWorldAngle = o["interUseWorldAngle"].toBool(false);
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
        {"annotation", s.annotation},  // Optional since v12 (便利贴注释, 缺省空串)
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
        {"extendStartMm", s.extendStartMm},
        {"extendStartFormula", s.extendStartFormula},
        {"extendEndMm", s.extendEndMm},
        {"extendEndFormula", s.extendEndFormula},
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
    s.annotation = o["annotation"].toString();  // Optional, 老档缺省空串
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
    s.extendStartMm = std::max(0.0, o["extendStartMm"].toDouble(0.0));  // 只往外(D2)
    s.extendStartFormula = o["extendStartFormula"].toString();
    s.extendEndMm = std::max(0.0, o["extendEndMm"].toDouble(0.0));
    s.extendEndFormula = o["extendEndFormula"].toString();
    s.lineStyle = lineStyleFrom(o["lineStyle"].toString(), &recStyle);
    s.color = QColor(o["color"].toString("#1e1e1e"));  // color-allow: 磁盘旧档缺省线色回退（数据契约，非样式）
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
        {"lengthAuto", b.lengthAuto},
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
Block blockFrom(const QJsonObject& o, QStringList* warnings = nullptr) {
    Block b;
    b.id = uuidFrom(o["id"].toString());
    b.name = o["name"].toString();
    b.transform.origin = vec2From(o["origin"].toObject());
    b.transform.rotation = o["rotation"].toDouble();
    b.isClosed = o["isClosed"].toBool();
    b.isBridge = o["isBridge"].toBool();  // Optional since v3 — defaults to false.
    b.lengthAuto = o["lengthAuto"].toBool();  // Optional since v12 — defaults to false.
    // Layer reference: always a stable Layer::id string. Integer indices are
    // a v0 (pre-id) shape and are rewritten by FormatMigration::migrateV0ToV1
    // before this function ever runs — anything non-string here is corruption
    // and degrades to a null id (caller: first working layer).
    b.layer = uuidFrom(o["layer"].toString());   // Missing/empty = null id.
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
        {"fromComponentId", uuidStr(a.fromComponentId)},  // 组件级连接 (Optional since v9)
        {"fromPointId", uuidStr(a.fromPointId)},
        {"toBlockId", uuidStr(a.toBlockId)},
        {"toPointId", uuidStr(a.toPointId)},
        {"toSegmentId", uuidStr(a.toSegmentId)},
          {"angleRefBlockId", uuidStr(a.angleRefBlockId)},
          {"angleRefSegmentId", uuidStr(a.angleRefSegmentId)},
            {"angleRefPointId", uuidStr(a.angleRefPointId)},
            {"angleRef2BlockId", uuidStr(a.angleRef2BlockId)},
            {"angleRef2PointId", uuidStr(a.angleRef2PointId)},
        {"followerAngle", a.followerAngle},
        {"followerAngleFormula", a.followerAngleFormula},
        {"rotationMode", static_cast<int>(a.rotationMode)},
        {"arcLength", a.arcLength},
        {"arcLengthFormula", a.arcLengthFormula},
        {"isPin", a.isPin},
        {"isLocked", a.isLocked},
        {"angleOnly", a.angleOnly},
        {"slideMode", static_cast<int>(a.slideMode)},
          {"angleIndependent", a.angleIndependent},
        {"slideAlongMm", a.slideAlongMm},
        {"slidePerpMm", a.slidePerpMm},
        {"slideAlongFormula", a.slideAlongFormula},
        {"slidePerpFormula", a.slidePerpFormula},
    };
}
Attachment attachmentFrom(const QJsonObject& o) {
    Attachment a;
    a.id = uuidFrom(o["id"].toString());
    a.fromBlockId = uuidFrom(o["fromBlockId"].toString());
    a.fromComponentId = uuidFrom(o["fromComponentId"].toString());  // Optional since v9
    a.fromPointId = uuidFrom(o["fromPointId"].toString());
    a.toBlockId = uuidFrom(o["toBlockId"].toString());
    a.angleRefBlockId = uuidFrom(o["angleRefBlockId"].toString());  // Optional
    a.angleRefSegmentId = uuidFrom(o["angleRefSegmentId"].toString());  // Optional
    a.angleRefPointId = uuidFrom(o["angleRefPointId"].toString());  // Optional
    a.angleRef2BlockId = uuidFrom(o["angleRef2BlockId"].toString());  // Optional since v12
    a.angleRef2PointId = uuidFrom(o["angleRef2PointId"].toString());  // Optional since v12
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
    a.angleIndependent = o["angleIndependent"].toBool();  // Optional — defaults to false.
    // 滑轨模式 (Optional since v6): raw int with enum-range validation —
    // unknown values degrade to None (same defensive pattern as rotationMode).
    const int sm = o["slideMode"].toInt(0);
    a.slideMode = (sm >= static_cast<int>(cad::param::SlideMode::None)
                   && sm <= static_cast<int>(cad::param::SlideMode::PerpLeader))
        ? static_cast<cad::param::SlideMode>(sm) : cad::param::SlideMode::None;
    a.slideAlongMm = o["slideAlongMm"].toDouble();
    a.slidePerpMm = o["slidePerpMm"].toDouble();
    // 滑轨公式 (Optional since v7): 缺失 = 空 (数值路径兼容旧档).
    a.slideAlongFormula = o["slideAlongFormula"].toString();
    a.slidePerpFormula = o["slidePerpFormula"].toString();
    // 旧影子偏转/冻结字段 (shadowAnchorRotDeg/noFollowRotate/angleRefFrozen*)
    // 已于 2026 删除 —— 旧档多余键读取时天然忽略, 行为零变化。
    return a;
}

// ─── Component (组件) ───
QJsonObject componentJson(const Component& c) {
    QJsonArray members;
    for (const QUuid& mid : c.memberBlockIds)
        members.append(uuidStr(mid));
    return {
        {"id", uuidStr(c.id)},
        {"name", c.name},
        {"members", members},
        {"exposedPointId", uuidStr(c.exposedPointId)},
        {"exposedSegmentId", uuidStr(c.exposedSegmentId)},
        {"showBoundingBox", c.showBoundingBox},
        {"defaultAngleDeg", c.defaultAngleDeg},
        {"defaultAngleFormula", c.defaultAngleFormula},  // Optional since v10
    };
}

Component componentFrom(const QJsonObject& o) {
    Component c;
    c.id = uuidFrom(o["id"].toString());
    c.name = o["name"].toString();
    for (const auto& v : o["members"].toArray())
        c.memberBlockIds.push_back(uuidFrom(v.toString()));
    c.exposedPointId = uuidFrom(o["exposedPointId"].toString());
    c.exposedSegmentId = uuidFrom(o["exposedSegmentId"].toString());
    c.showBoundingBox = o["showBoundingBox"].toBool(true);  // Optional since v8 — defaults true.
    c.defaultAngleDeg = o["defaultAngleDeg"].toDouble(0.0); // Optional since v8.
    c.defaultAngleFormula = o["defaultAngleFormula"].toString(); // Optional since v10.
    return c;
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

    // Components (组件: rigid work groups)
    QJsonArray compArr;
    for (const auto& c : doc.components())
        compArr.append(componentJson(c));
    docObj["components"] = compArr;

    // Serial counters
    docObj["pointSeq"] = doc.pointSeq();
    docObj["lineSeq"] = doc.lineSeq();

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
        docObj["lineSeq"].toInt(1));

    // Canvas layers (restore before blocks: blocks reference layers by id).
    //
    // P2-1: HISTORY is no longer handled here. A file loaded through
    // DocumentFile::load has already been walked forward to kFormatVersion by
    // the registered migration chain — the "v0 predates the auxiliary layer,
    // so integer block layer indices shift up by one" knowledge now lives in
    // cad::doc::FormatMigration::migrateV0ToV1, where it is named, versioned
    // and unit-testable on its own.
    //
    // What remains is CORRUPTION tolerance for hand-built JSON (tests and
    // direct API callers that never went through DocumentFile): missing or
    // nonsensical structure degrades to the documented safe default instead
    // of being silently reinterpreted as history.
    if (docObj.contains(QStringLiteral("layers"))) {
        std::vector<Layer> restored;
        for (const auto& v : docObj["layers"].toArray())
            restored.push_back(layerFrom(v.toObject(), warnings));
        if (restored.empty()) {
            Layer fallback;
            fallback.name = QStringLiteral("图层 1");
            restored.push_back(std::move(fallback));
        }
        // Model invariant (NOT history): exactly one auxiliary layer, at index
        // 0. A file whose layers array has none (e.g. an unrecognised "type"
        // was degraded to Working above) still has to end up with one — the
        // layer registry cannot represent a document without it.
        //
        // What is deliberately NOT done here anymore is the v0 INDEX REMAP
        // ("every block's integer layer index shifts up by one because v0 had
        // no aux layer"): that is version-specific history and now lives in
        // cad::doc::FormatMigration::migrateV0ToV1 (P2-1). By the time a file
        // reaches this function through DocumentFile::load, every layer
        // reference is already a stable id.
        bool hasAux = false;
        for (const auto& l : restored)
            if (l.type == LayerType::Auxiliary) { hasAux = true; break; }
        if (!hasAux) {
            Layer aux;
            aux.name = QStringLiteral("辅助层");
            aux.type = LayerType::Auxiliary;
            restored.insert(restored.begin(), std::move(aux));
        }
        cad::param::RawModelAccess::replaceLayersRaw(doc, std::move(restored));
    }
    // No "layers" array at all -> keep the default pair created by clear().

    // Active layer: a stable id string. Anything unresolvable (missing field,
    // stale id) falls back to the first working layer — never the auxiliary
    // one, which is not a drafting target.
    const QUuid activeId = uuidFrom(docObj["activeLayer"].toString());
    doc.setActiveLayer(doc.layersView().layerIndex(activeId) >= 0 ? activeId
                                                     : doc.layersView().firstWorkingLayerId());

    // Blocks (raw, no resolve). Layer references are stable id strings; the
    // "unknown or missing" validation right below owns every other case.
    for (const auto& v : docObj["blocks"].toArray())
        cad::param::RawModelAccess::addBlockRaw(doc, blockFrom(v.toObject(), warnings));

    // Validate block layer refs: unknown ids (corrupt file / stale legacy
    // index) fall back to the first working layer, reported one by one
    // (逐条报告, no silent degradation).
    for (const auto& bc : doc.blocks()) {
        if (bc.layer.isNull())
            continue;  // Missing layer field: addBlock-style defaulting below.
        if (doc.layersView().layerIndex(bc.layer) >= 0)
            continue;
        if (warnings)
            warnings->append(QString::fromUtf8("线段块 %1 引用的图层 %2 不存在，已移到第一个工作层")
                                 .arg(bc.name, uuidStr(bc.layer)));
        if (auto* b = doc.blockById(bc.id))
            b->layer = doc.layersView().firstWorkingLayerId();
    }
    for (const auto& bc : doc.blocks())
        if (bc.layer.isNull())
            if (auto* b = doc.blockById(bc.id))
                b->layer = doc.layersView().firstWorkingLayerId();

    // Free points
    for (const auto& v : docObj["freePoints"].toArray())
        cad::param::RawModelAccess::addFreePointRaw(doc, pointFrom(v.toObject(), warnings));

    // Attachments (raw, no resolve)
    for (const auto& v : docObj["attachments"].toArray())
        cad::param::RawModelAccess::addAttachmentRaw(doc, attachmentFrom(v.toObject()));

    // Components (raw restore — offsets trusted verbatim; blocks already added)
    for (const auto& v : docObj["components"].toArray())
        cad::param::RawModelAccess::restoreComponentRaw(doc, componentFrom(v.toObject()));

    // Variables
    for (const auto& v : varObj["variables"].toArray())
        cad::param::RawModelAccess::restoreVariableRaw(doc, variableFrom(v.toObject()));

    // Formula groups (restore before formulas so membership can be validated)
    for (const auto& v : varObj["formulaGroups"].toArray())
        cad::param::RawModelAccess::restoreFormulaGroupRaw(doc, formulaGroupFrom(v.toObject()));

    // Formulas
    for (const auto& v : varObj["formulas"].toArray()) {
        auto f = formulaFrom(v.toObject());
        // Drop a dangling group reference (group missing from the file).
        if (!f.groupId.isNull() && !doc.variablesView().groupById(f.groupId))
            f.groupId = QUuid();
        cad::param::RawModelAccess::restoreFormulaRaw(doc, std::move(f));
    }

    // Linked variables
    for (const auto& v : varObj["linkedVars"].toArray())
        cad::param::RawModelAccess::restoreLinkedRaw(doc, linkedFrom(v.toObject()));

    // Measure variables
    for (const auto& v : varObj["measureVars"].toArray())
        cad::param::RawModelAccess::restoreMeasureRaw(doc, measureFrom(v.toObject()));

    // Angle measure variables
    for (const auto& v : varObj["angleMeasures"].toArray())
        cad::param::RawModelAccess::restoreAngleMeasureRaw(doc, angleMeasureFrom(v.toObject()));

    // Recompute formulas (syncs parameters + resolves all blocks)
    doc.recomputeFormulas();

    // Refresh the UI: create canvas items for all blocks, rebuild panels.
    doc.finishRestore();
}

} // namespace cad::param
