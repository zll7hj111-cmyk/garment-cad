#pragma once

#include <QUuid>

#include <vector>

#include "geometry/Vec2.h"
#include "tools/SnapEngine.h"
#include "tools/LineFactory.h"   // LeaderCandidate

class CanvasScene;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

/// Leader-segment candidate selection (基准线点选) for the smart pen: at a
/// snapped start, every segment incident to any point coincident with the
/// snap position is a valid angle reference. Collects + ranks candidates,
/// highlights the selected one on canvas, and tracks the construction-angle
/// reference direction (the HUD / Shift snap / final followerAngle work in
/// this space: 闭合基准 2026-08, 0° = 折叠重叠、180° = 直行延续).
class LeaderCandidatePicker
{
public:
    using Vec2 = cad::geo::Vec2;

    LeaderCandidatePicker(CanvasScene* scene, cad::param::ParamDocument* doc);

    /// Collect + rank candidates around the snapped start point
    /// (auto-pick order: endpoint segments before host segments, then
    /// Outline < Internal < Auxiliary, then creation order).
    void collect(const SnapResult& snap);
    /// Choose candidate by index: teal-highlight it and update refDirDeg().
    void setIndex(int index);
    /// Clear highlight and candidate list (refDirDeg is NOT reset — it stays
    /// valid until the next snapped start replaces it).
    void clear();
    /// Reset the reference direction to 0 (free-line fallback).
    void setRefDirDeg(double deg) { m_refDirDeg = deg; }

    [[nodiscard]] int index() const { return m_leaderIndex; }
    [[nodiscard]] const std::vector<LeaderCandidate>& candidates() const
    { return m_leaderCandidates; }
    /// Construction-angle reference direction (deg), updated by setIndex().
    [[nodiscard]] double refDirDeg() const { return m_refDirDeg; }

    /// Candidate whose segment body is within pick tolerance of worldPos
    /// (nearest wins), or -1. Used for click-to-switch during rubber band.
    [[nodiscard]] int candidateAt(const Vec2& worldPos, double zoom) const;

private:
    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;

    std::vector<LeaderCandidate> m_leaderCandidates;
    int   m_leaderIndex = -1;        ///< Index into m_leaderCandidates (-1 = none).
    QUuid m_highlightBlockId;        ///< Block item currently showing the highlight.
    double m_refDirDeg = 0.0;        ///< Leader segment world direction (deg) at snapped start.
    SnapEngine m_snapEngine;         ///< Snap reach source (same radius as the tool's).
};

} // namespace cad::tools
