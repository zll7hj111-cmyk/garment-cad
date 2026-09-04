#pragma once

#include <QUuid>
#include <QString>
#include <optional>
#include <vector>

#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Duplicate.h"

class CanvasScene;
class QUndoStack;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

class RotateCopyGesture;

/// Anchor point state (start vs end).
struct RotateAnchorState
{
    bool isEnd = false;
    QUuid pointId;
    QUuid releaseAttId;
    cad::param::Attachment releaseAttBackup;
    bool releaseAttHeld = false;
};

/// Shadow angle channel state (R6/R8, detach shadow datum).
struct RotateShadowChannel
{
    QUuid shadowId;              ///< 影子块 id (空 = 非影子通道).
    bool isMounted = false;      ///< true = 挂载态 (Att1 存在, 写 Δ).
    QUuid att1Id;                ///< 挂载态: Att1 (影子→宿主) id.
    double shadowRot0 = 0.0;     ///< 拆开态: 影子 rotation 基线 (rad).
    double shadowDelta0 = 0.0;   ///< 挂载态: Att1 followerAngle 基线 (deg).
    cad::param::Transform2D shadowTf0;      ///< 拆开态: 影子完整基线 transform.
    cad::param::Transform2D followerTf0;    ///< 拆开态: 跟随线基线 transform (R8 p3 轴心回写).

    [[nodiscard]] bool active() const { return !shadowId.isNull(); }
    void reset() {
        shadowId = QUuid();
        isMounted = false;
        att1Id = QUuid();
        shadowRot0 = 0.0;
        shadowDelta0 = 0.0;
        shadowTf0 = cad::param::Transform2D();
        followerTf0 = cad::param::Transform2D();
    }
};

/// Base state snapshot captured when target is selected.
struct RotateBaseSnapshot
{
    double  baseAngle = 0.0;
    QString baseFormula;
    cad::param::RotationMode rotationMode = cad::param::RotationMode::Angle;
    double  baseArcLength = 0.0;
    QString baseArcFormula;
    cad::param::Transform2D baseTf;
    QUuid baseEndTargetBlock;
    QUuid baseEndTargetPoint;
};

/// Single-line rotation session managing the rotation target, anchor point,
/// follower attachment, shadow channel, and base state snapshot.
class RotateSession
{
public:
    RotateSession() = default;

    void setupTarget(cad::param::ParamDocument* doc,
                     const QUuid& blockId,
                     const std::optional<cad::geo::Vec2>& clickWorld = std::nullopt);
    void clear();

    void toggleAnchor(cad::param::ParamDocument* doc);
    void rebuildAnchorState(cad::param::ParamDocument* doc);
    [[nodiscard]] QUuid anchorPointAt(const cad::param::ParamDocument* doc,
                                      const cad::geo::Vec2& worldPos,
                                      double zoom) const;
    void releaseFollowerIfAnchorMoved(cad::param::ParamDocument* doc, CanvasScene* scene);

    void applyAngleDeg(cad::param::ParamDocument* doc,
                       CanvasScene* scene,
                       double deg,
                       RotateCopyGesture* copyGesture,
                       double dragAngle0 = 0.0);
    void applyShadowAngleDeg(cad::param::ParamDocument* doc,
                             double deg,
                             double dragAngle0);
    void applyModeValue(cad::param::ParamDocument* doc,
                        CanvasScene* scene,
                        double value,
                        RotateCopyGesture* copyGesture);
    void restoreBase(cad::param::ParamDocument* doc, CanvasScene* scene);
    bool commit(cad::param::ParamDocument* doc, QUndoStack* undoStack);

    [[nodiscard]] double currentAngleDeg(const cad::param::ParamDocument* doc,
                                         RotateCopyGesture* copyGesture) const;
    [[nodiscard]] double currentModeValue(const cad::param::ParamDocument* doc,
                                          RotateCopyGesture* copyGesture) const;
    [[nodiscard]] double segmentRadius(const cad::param::ParamDocument* doc) const;
    [[nodiscard]] bool isAngleLocked(RotateCopyGesture* copyGesture) const;
    [[nodiscard]] double originalWorldRotDeg(const cad::param::ParamDocument* doc) const;
    [[nodiscard]] QString anchorTag(const cad::param::ParamDocument* doc) const;

    struct GizmoAngles { double dashRad; double arcStart; double arcEnd; };
    [[nodiscard]] GizmoAngles calculateGizmoAngles(double deg, bool isRotating, double dragAngle0) const;

    [[nodiscard]] const cad::param::Attachment* editableAttachment(
        const cad::param::ParamDocument* doc) const;
    [[nodiscard]] cad::param::Attachment* editableAttachment(
        cad::param::ParamDocument* doc);
    [[nodiscard]] cad::param::Attachment* attachmentAtPoint(
        cad::param::ParamDocument* doc, const QUuid& pointId);
    [[nodiscard]] cad::param::Attachment* followerAttachment(
        cad::param::ParamDocument* doc);

    // Getters & inspection
    [[nodiscard]] const QUuid& blockId() const { return m_blockId; }
    [[nodiscard]] bool isConnected() const { return m_connected; }
    [[nodiscard]] const QUuid& attachmentId() const { return m_attId; }
    [[nodiscard]] const cad::geo::Vec2& pivot() const { return m_pivot; }
    void setPivot(const cad::geo::Vec2& p) { m_pivot = p; }
    [[nodiscard]] double refWorldRad() const { return m_refWorldRad; }

    [[nodiscard]] const RotateAnchorState& anchor() const { return m_anchor; }
    [[nodiscard]] RotateAnchorState& anchor() { return m_anchor; }
    [[nodiscard]] const RotateShadowChannel& shadow() const { return m_shadow; }
    [[nodiscard]] RotateShadowChannel& shadow() { return m_shadow; }
    [[nodiscard]] const RotateBaseSnapshot& base() const { return m_base; }
    [[nodiscard]] RotateBaseSnapshot& base() { return m_base; }

    [[nodiscard]] const cad::geo::Vec2& anchorLocal() const { return m_anchorLocal; }
    [[nodiscard]] double localDir() const { return m_localDir; }

private:
    QUuid m_blockId;
    bool  m_connected = false;
    QUuid m_attId;
    cad::geo::Vec2 m_pivot;
    double m_refWorldRad = 0.0;

    RotateAnchorState m_anchor;
    RotateShadowChannel m_shadow;
    RotateBaseSnapshot m_base;

    cad::geo::Vec2 m_anchorLocal;
    double m_localDir = 0.0;
};

} // namespace cad::tools
