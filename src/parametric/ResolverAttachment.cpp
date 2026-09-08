#include "Resolver.h"

#include <cmath>

#include "Block.h"
#include "Attachment.h"
#include "ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/Epsilon.h"
#include "parametric/ResolveDiagnostics.h"

namespace cad::param {

bool Resolver::applyAttachment(Block& from, const Attachment& att,
                               const Block& to,
                               const Block* angleRef,
                               const Block* angleRef2,
                               const QHash<QString, double>& params,
                               const QHash<QString, QList<Condition>>& conditioned,
                               std::vector<ResolveDiagnostic>* diagnostics,
                               EvalContext* ctx,
                               bool preserveEndTargetRotation)
{
    // The leader's snapped point must exist and be resolved.
    const ParamPoint* toPt = to.findPoint(att.toPointId);
    if (!toPt || !toPt->resolved) {
        appendDiagnostic(diagnostics, ResolveDiagnostic::Kind::DanglingPoint, att.id);
        return false;
    }

    // Get the target point's world position on the "to" block.
    geo::Vec2 targetWorldPos = to.worldPos(att.toPointId);

    // Reference direction for the POSITION leader (used by slide rails).
    const double leaderRefWorld = to.transform.rotation
                    + to.exitDirectionAtPoint(att.toPointId, att.toSegmentId);
    double refWorld = leaderRefWorld;

    // 母线基准方向（用户拍板 2026-09）: 直接自动取母线端点1到端点2的世界直线向量
    // 无视曲线控制点/弯曲切线，无视吸附端点进出方向
    const Segment* toSeg = to.findSegment(att.toSegmentId);
    if (toSeg) {
        const ParamPoint* sp = to.findPoint(toSeg->startPointId);
        const ParamPoint* ep = to.findPoint(toSeg->endPointId);
        if (sp && ep && sp->resolved && ep->resolved) {
            const geo::Vec2 w1 = to.transform.toWorld(sp->resolvedPos);
            const geo::Vec2 w2 = to.transform.toWorld(ep->resolvedPos);
            if (w1.distanceTo(w2) > cad::geo::kGeomEpsLoose) {
                refWorld = std::atan2(w2.y - w1.y, w2.x - w1.x);
            }
        }
    }

    if (!att.angleRefBlockId.isNull() && angleRef) {
        if (!att.angleRef2BlockId.isNull() && !att.angleRef2PointId.isNull()) {
            const Block* ref2 = angleRef2;
            const ParamPoint* p1 = angleRef->findPoint(att.angleRefPointId);
            const ParamPoint* p2 = ref2 ? ref2->findPoint(att.angleRef2PointId) : nullptr;
            if (p1 && p2 && p1->resolved && p2->resolved) {
                const geo::Vec2 w1 = angleRef->transform.toWorld(p1->resolvedPos);
                const geo::Vec2 w2 = ref2->transform.toWorld(p2->resolvedPos);
                if (w1.distanceTo(w2) > cad::geo::kGeomEpsLoose) refWorld = std::atan2(w2.y - w1.y, w2.x - w1.x);
            }
        } else {
            const Segment* refSeg = angleRef->findSegment(att.angleRefSegmentId);
            if (refSeg) {
                const ParamPoint* rsp = angleRef->findPoint(refSeg->startPointId);
                const ParamPoint* rep = angleRef->findPoint(refSeg->endPointId);
                if (rsp && rep && rsp->resolved && rep->resolved) {
                    const geo::Vec2 w1 = angleRef->transform.toWorld(rsp->resolvedPos);
                    const geo::Vec2 w2 = angleRef->transform.toWorld(rep->resolvedPos);
                    if (w1.distanceTo(w2) > cad::geo::kGeomEpsLoose) refWorld = std::atan2(w2.y - w1.y, w2.x - w1.x);
                }
            }
        }
    }

    // The follower's attached point must exist and be resolved (checked before
    // any direction lookup so dangling points short-circuit cleanly).
    const ParamPoint* fromPt = from.findPoint(att.fromPointId);
    if (!fromPt || !fromPt->resolved) {
        appendDiagnostic(diagnostics, ResolveDiagnostic::Kind::DanglingPoint, att.id);
        return false;
    }

    // Local direction of the follower's attached segment (the one anchored at
    // fromPointId). The follower's own orientation is its start->end direction.
    // Its world segment direction equals from.transform.rotation + localDir, so
    // to achieve the desired world direction (refWorld + π − angle, 闭合基准
    // 2026-08: 0° = 折叠重叠、180° = 直行延续) we set:
    //     rotation = refWorld + π − angle − localDir
    double localDir = from.directionAtPoint(att.fromPointId);

    // Evaluate follower angle: formula overrides numeric value.
    double angleRad;
    if (att.rotationMode == RotationMode::ArcLength) {
        // Arc-length mode: convert arc length to angle via radius = segment length.
        // The arc starts at the CLOSED position (弧长 0 = 角度 0° = 两线折叠
        // 重叠), sweeping so that πr = 180° = straight continuation (闭合基准,
        // 用户拍板 2026-08 定稿, 与角度模式同基准): 弧长 0 = 0°, 弧长 πr = 180°.
        double arcMm = att.arcLength;
        ConditionEngine::evaluateLengthMm(att.arcLengthFormula, params, conditioned, arcMm, ctx);
        const double radius = from.segmentLengthAtPoint(att.fromPointId);
        angleRad = cad::geo::degToRad(cad::geo::arcMmToDeg(arcMm, radius));
    } else if (att.rotationMode == RotationMode::ChordLength) {
        // Chord-length / opening distance mode (直线弦长/开度模式).
        // Opening distance C = 2 * r * sin(theta / 2).
        // Closed at C = 0 (0° = 两线折叠重叠), opening outwards.
        double chordMm = att.chordLength;
        ConditionEngine::evaluateLengthMm(att.chordLengthFormula, params, conditioned, chordMm, ctx);
        const double radius = from.segmentLengthAtPoint(att.fromPointId);
        angleRad = cad::geo::degToRad(cad::geo::chordMmToDeg(chordMm, radius));
    } else {
        // Angle mode (default).
        double angleDeg = att.followerAngle;
        if (!att.followerAngleFormula.isEmpty()) {
            auto r = ConditionEngine::evaluate(att.followerAngleFormula, params, conditioned, ctx);
            if (r.ok) angleDeg = r.value;  // result is in degrees, no conversion
        }
        angleRad = cad::geo::degToRad(angleDeg);
    }

    // Closed-base convention (闭合基准, 用户拍板 2026-08 定稿): followerAngle
    // 0° = the follower folds back onto the leader (两线重叠), 90° = vertical,
    // 180° = straight continuation along the leader's exit direction. The
    // world direction is therefore refWorld + π − angleRad (mirror about the
    // perpendicular), NOT refWorld + angleRad. Both rotation modes share it.
    double newRotation = refWorld + cad::geo::kPi - angleRad - localDir;

    // A block whose rotation is driven by an endpoint-aim constraint (endTarget,
    // applied in Step 7) must not have its rotation overwritten by the attachment
    // during the post-aim re-settle: the aim rotation is authoritative. Only the
    // position constraint is enforced (origin re-snapped about the kept rotation).
    if (preserveEndTargetRotation && !from.endTargetBlockId.isNull())
        newRotation = from.transform.rotation;

    // 位置吸附保持、角度独立 (用户新需求 2026): the point is still pinned to
    // the leader, but the follower keeps its OWN rotation. This is the inverse
    // of angleOnly: position follows, angle does not.
    if (att.angleIndependent)
        newRotation = from.transform.rotation;

    // 拆开保留角度 (angleOnly, 用户拍板 2026-08): the follower keeps following
    // the leader's ANGLE — rotation is still driven by leader direction +
    // followerAngle — but the position constraint is released: the from-point
    // no longer has to land on the leader's point, so the line translates
    // freely while its orientation keeps the relative angle.
    // 2026-xx 两维独立 (用户拍板): angleOnly 与 angleIndependent 不再互斥 ——
    // 双拆开 (angleOnly + angleIndependent) = 位置自由 + 角度自管 = 自由线
    // (rotation 已被上面 angleIndependent 分支保持为自身值, 这里写入同值
    // 并提前 return, 跳过位置钉点)。
    if (att.angleOnly) {
        const bool moved = std::abs(newRotation - from.transform.rotation) > cad::geo::kGeomEps;
        from.transform.rotation = newRotation;
        return moved;
    }

    // ── 滑轨模式 (slideMode, 抽屉式滑动, 用户拍板 2026-08) ──
    // 连接姿态保持 (rotation 照旧由基准线方向 + followerAngle 驱动), 位置
    // 只保留一个自由度: 在基准线局部系 (x = 沿基准线延长方向, y = 垂直,
    // 基准线旋转时滑轨跟着转) 下 —— AlongLeader 沿 x 滑动 (y 锁
    // slidePerpMm), PerpLeader 沿 y 拉出 (x 锁 slideAlongMm)。
    //
    // 位置**只从存储坐标 (slideAlongMm/slidePerpMm) 解算**, 不做现场投影:
    // 基准线刚体移动 (平移/旋转) 时滑轨局部坐标不变 → 跟随线随滑轨刚性
    // 携带; 拖动跟随线时由拖拽工具每帧调用
    // ParamDocument::updateSlideOffsetsFromCurrent() 回写**自由轴**坐标,
    // 锁轴坐标保持激活时快照不变。
    if (att.slideMode != SlideMode::None && !att.angleIndependent) {
        // Leader-local rail frame at the anchor point.
        const double railAngle = leaderRefWorld;  // leader's world exit direction
        const geo::Vec2 alongDir(std::cos(railAngle), std::sin(railAngle));
        const geo::Vec2 perpDir(-alongDir.y, alongDir.x);

        // 数值或公式 (cm 域, 2026-12): 公式优先于存储值; 公式无效时回退存储值
        // (与弧长/跟随角公式同约定)。面板输入的 .00 只是数值回显, 变量/表达式
        // 同样可用。
        double alongMm = att.slideAlongMm;
        double perpMm  = att.slidePerpMm;
        ConditionEngine::evaluateLengthMm(att.slideAlongFormula, params, conditioned, alongMm, ctx);
        ConditionEngine::evaluateLengthMm(att.slidePerpFormula, params, conditioned, perpMm, ctx);

        // from-point world position on the rail, pinned from the stored pair.
        const geo::Vec2 localOffset = fromPt->resolvedPos;
        const geo::Vec2 fromPointWorld =
            targetWorldPos + alongDir * alongMm + perpDir * perpMm;

        // origin = from-point world minus the (new-rotation) rotated local offset.
        const double c = std::cos(newRotation);
        const double sn = std::sin(newRotation);
        const geo::Vec2 rotatedOffset{
            localOffset.x * c - localOffset.y * sn,
            localOffset.x * sn + localOffset.y * c
        };
        const geo::Vec2 newOrigin = fromPointWorld - rotatedOffset;

        const bool moved =
            std::abs(newRotation - from.transform.rotation) > cad::geo::kGeomEps ||
            std::abs(newOrigin.x - from.transform.origin.x) > cad::geo::kGeomEpsLoose ||
            std::abs(newOrigin.y - from.transform.origin.y) > cad::geo::kGeomEpsLoose;
        from.transform.rotation = newRotation;
        from.transform.origin = newOrigin;
        return moved;
    }

    // Now position the from-block so that its from-point lands on targetWorldPos.
    // from-point in local coords:
    geo::Vec2 localOffset = fromPt->resolvedPos;

    // Rotate localOffset by the new rotation
    double c = std::cos(newRotation);
    double s = std::sin(newRotation);
    geo::Vec2 rotatedOffset{
        localOffset.x * c - localOffset.y * s,
        localOffset.x * s + localOffset.y * c
    };

    // origin = targetWorldPos - rotatedOffset
    const geo::Vec2 newOrigin = targetWorldPos - rotatedOffset;

    // Only report "moved" when the transform actually changed, so the outer
    // loop can detect convergence of a healthy forest.
    const bool moved =
        std::abs(newRotation - from.transform.rotation) > cad::geo::kGeomEps ||
        std::abs(newOrigin.x - from.transform.origin.x) > cad::geo::kGeomEpsLoose ||
        std::abs(newOrigin.y - from.transform.origin.y) > cad::geo::kGeomEpsLoose;

    from.transform.rotation = newRotation;
    from.transform.origin = newOrigin;
    return moved;
}

} // namespace cad::param
