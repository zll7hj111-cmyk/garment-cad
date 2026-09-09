#pragma once
/// Enum <-> string codecs shared by DocumentSerializer.cpp.
///
/// Extracted 2026-12 (CIRCLE_TOOL_DESIGN.md M1): adding the optional `fitKind`
/// key pushed DocumentSerializer.cpp past its declared redline baseline
/// (redline_exceptions.json: 987 lines, limit 900), and that list may only
/// shrink. Moving this cohesive generic block out keeps the serializer file
/// under its limit instead of expanding the exception.
///
/// Table rule (unchanged from the original site): the FIRST row is the
/// canonical default — the degradation target for an unknown enum value or an
/// unrecognized string. Keep it in sync with the registration table next to the
/// ParamPoint.h enum (str→enum and enum→str share one table, so they cannot
/// drift; a missing row degrades silently to the canonical default, which is
/// why every row must be added here).
#include <array>
#include <cstddef>
#include <utility>

#include <QString>

#include "parametric/Condition.h"    // AdjustMode
#include "parametric/ParamPoint.h"   // PointConstraint
#include "parametric/Segment.h"      // SegmentType / SegmentRole / LineStyle / FitKind

namespace cad::param {

template <typename E, std::size_t N>
inline QString enumToStr(const std::array<std::pair<E, const char*>, N>& table, E v,
                         const char* fallback)
{
    for (const auto& [e, s] : table)
        if (e == v) return QLatin1String(s);
    return QLatin1String(fallback);
}

/// @p recognized 语义与旧实现逐位一致: 命中表中任一 (含默认行) = true;
/// 未知字符串 = false (s 非空且不是默认名)。空串 = 字段缺失, 正常默认化。
template <typename E, std::size_t N>
inline E enumFromStr(const std::array<std::pair<E, const char*>, N>& table,
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

inline constexpr std::array<std::pair<PointConstraint, const char*>, 8>
    kPointConstraintMap = {{
        {PointConstraint::Free,         "Free"},
        {PointConstraint::Polar,        "Polar"},
        {PointConstraint::Midpoint,     "Midpoint"},
        {PointConstraint::OnSegment,    "OnSegment"},
        {PointConstraint::Intersection, "Intersection"},
        {PointConstraint::Interpolated, "Interpolated"},
        {PointConstraint::CurveAnchor,  "CurveAnchor"},
        {PointConstraint::OrthoOffset,  "OrthoOffset"},
    }};
inline QString pointConstraintStr(PointConstraint c) {
    return enumToStr(kPointConstraintMap, c, "Free");
}
inline PointConstraint pointConstraintFrom(const QString& s, bool* recognized = nullptr) {
    return enumFromStr(kPointConstraintMap, s, "Free",
                       PointConstraint::Free, recognized);
}

inline constexpr std::array<std::pair<SegmentType, const char*>, 3>
    kSegmentTypeMap = {{
        {SegmentType::Line,   "Line"},
        {SegmentType::Arc,    "Arc"},
        {SegmentType::Bezier, "Bezier"},
    }};
inline QString segmentTypeStr(SegmentType t) {
    return enumToStr(kSegmentTypeMap, t, "Line");
}
inline SegmentType segmentTypeFrom(const QString& s, bool* recognized = nullptr) {
    return enumFromStr(kSegmentTypeMap, s, "Line",
                       SegmentType::Line, recognized);
}

inline constexpr std::array<std::pair<SegmentRole, const char*>, 3>
    kSegmentRoleMap = {{
        {SegmentRole::Outline,   "Outline"},
        {SegmentRole::Internal,  "Internal"},
        {SegmentRole::Auxiliary, "Auxiliary"},
    }};
inline QString segmentRoleStr(SegmentRole r) {
    return enumToStr(kSegmentRoleMap, r, "Outline");
}
inline SegmentRole segmentRoleFrom(const QString& s, bool* recognized = nullptr) {
    return enumFromStr(kSegmentRoleMap, s, "Outline",
                       SegmentRole::Outline, recognized);
}

inline constexpr std::array<std::pair<LineStyle, const char*>, 3>
    kLineStyleMap = {{
        {LineStyle::Solid,  "Solid"},
        {LineStyle::Dashed, "Dashed"},
        {LineStyle::Dotted, "Dotted"},
    }};
inline QString lineStyleStr(LineStyle s) {
    return enumToStr(kLineStyleMap, s, "Solid");
}
inline LineStyle lineStyleFrom(const QString& s, bool* recognized = nullptr) {
    return enumFromStr(kLineStyleMap, s, "Solid",
                       LineStyle::Solid, recognized);
}

// Analytic curve fit (CIRCLE_TOOL_DESIGN.md D3/D21). Optional additive key:
// absent in older documents -> None (zero migration, no format-version bump —
// same contract as Segment::annotation).
inline constexpr std::array<std::pair<FitKind, const char*>, 2>
    kFitKindMap = {{
        {FitKind::None,   "None"},
        {FitKind::Circle, "Circle"},
    }};
inline QString fitKindStr(FitKind f) {
    return enumToStr(kFitKindMap, f, "None");
}
inline FitKind fitKindFrom(const QString& s, bool* recognized = nullptr) {
    return enumFromStr(kFitKindMap, s, "None", FitKind::None, recognized);
}

inline QString adjustModeStr(AdjustMode m) {
    return m == AdjustMode::PerStep ? "PerStep" : "Flat";
}
inline AdjustMode adjustModeFrom(const QString& s) {
    return s == "PerStep" ? AdjustMode::PerStep : AdjustMode::Flat;
}

} // namespace cad::param
