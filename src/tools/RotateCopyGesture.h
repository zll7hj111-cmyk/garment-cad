#pragma once

#include <QUuid>

#include "geometry/Vec2.h"
#include "parametric/Duplicate.h"

namespace cad::tools {

class ToolRotate;

/// Rotate-copy gesture of the rotation tool (Ctrl+drag 旋转复制): clones the
/// target block, attaches the clone BACK to the original (pivot point), then
/// rotates the clone relative to the original's current direction. The
/// preview clone lives in the document until release (one undo step) or Esc
/// (discarded). Mid-gesture conversion (plain rotation + Ctrl) transfers the
/// angle rotated so far to the clone.
///
/// Implemented as a friend helper so it can read/write the owner's session
/// state directly — the gesture shares the pivot / base snapshot / HUD /
/// gizmo machinery of the owning tool.
class RotateCopyGesture
{
public:
    using Vec2 = cad::geo::Vec2;

    explicit RotateCopyGesture(ToolRotate* owner);

    [[nodiscard]] bool active() const { return m_copyMode; }
    /// Preview clone→original attachment id (live preview only).
    [[nodiscard]] QUuid cloneAttId() const { return m_cloneAttId; }

    /// Ctrl+press on the target: clone + attach back + enter Rotating.
    void begin(const Vec2& pos);
    /// Mid-gesture conversion: a plain rotation becomes a rotate-copy when
    /// Ctrl is pressed while dragging. The angle rotated so far transfers to
    /// the clone; the original snaps back to its pre-rotation pose. No-op for
    /// connected / released-anchor sessions (complex semantics — stays plain).
    void convert(const Vec2& pos);
    /// Apply a relative angle (deg) to the preview clone (live preview).
    /// The angle is the CLONE's pivot-relative rotation (绕锚心角): 0 = the
    /// clone overlaps the original, regardless of which anchor is pivoted or
    /// whether the original is connected — the stored follower angle adds the
    /// copy-base offset (原线世界朝向 − 工具参考方向; 2026-08 定稿:
    /// 自由线 = 线自身方向 + 锚心 180°、连接线 = refWorld + π − α)。
    void applyAngle(double deg);
    /// Apply an evaluated formula value as the same pivot-relative angle
    /// (副本自动去公式: the clone never keeps a formula — 用户拍板).
    void applyFormulaValue(double value);
    /// Current pivot-relative angle (deg) of the preview clone (stored
    /// follower angle minus the copy-base offset; 0 = overlap with original).
    [[nodiscard]] double currentRelativeAngle() const;
    /// World direction (rad) of the clone at a given pivot-relative angle.
    /// Base = 挂接点出口世界方向（原线在锚心的 exitDirection = 克隆 Resolver
    /// 的 refWorld）+ 复制基准偏移 + 相对角 — i.e. the ORIGINAL's direction
    /// + relDeg (2026-08 定稿: 旧基准 m_copyRefWorldRad − π 使自由线差 180°、
    /// 连接线差 α)。
    [[nodiscard]] double relToWorldRad(double relDeg) const;
    /// Inverse of relToWorldRad: pivot-relative angle for a world direction.
    [[nodiscard]] double worldRadToRel(double worldRad) const;
    /// Current world direction (rad) of the preview clone.
    [[nodiscard]] double currentWorldRad() const;
    /// Release / HUD Enter: drop the preview clone, replay ONE undo step with
    /// the final relative angle. Zero-angle releases discard the copy.
    void commit();
    /// Esc / tool switch: drop the preview clone, return to Ready.
    void cancel();

private:
    /// Remove the live preview clone (attachment + block + linked vars).
    void removeCopyPreview();
    /// Reset copy state; owner back to Ready.
    void reset();

    ToolRotate* m_owner = nullptr;

    bool m_copyMode = false;                  ///< Rotating a preview clone.
    cad::param::DuplicateResult m_copyResult; ///< Pristine clone for the command.
    QUuid m_cloneBlockId;                     ///< Preview clone block.
    QUuid m_cloneAttId;                       ///< Preview clone→original attachment.
    QUuid m_pivotPointId;                     ///< Original pivot point (挂接点).
    QUuid m_clonePivotPointId;                ///< Clone-side counterpart of the pivot.
    QUuid m_leaderSegmentId;                  ///< Original exit segment at the pivot
                                              ///< (construction-angle reference).
    double m_baseOffsetDeg = 0.0;             ///< Copy-base offset = 原线世界朝向 −
                                              ///< 挂接点出口世界方向: makes the clone's
                                              ///< relative 0° = overlap with the
                                              ///< original for BOTH anchors and for
                                              ///< free AND connected lines (2026-08).
    double m_attachExitRad = 0.0;             ///< 原线在挂接点的出口世界方向 (rad) =
                                              ///< 克隆 Resolver 的 refWorld。
};

} // namespace cad::tools
