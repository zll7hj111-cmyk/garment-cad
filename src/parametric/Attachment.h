#pragma once

#include <QUuid>
#include <QString>

namespace cad::param {

/// How the follower's rotation is driven relative to the leader.
enum class RotationMode {
    Angle,      ///< Driven by follower angle (followerAngle, degrees).
    ArcLength,  ///< Driven by arc length (arcLength, mm internal / cm formula).
                ///< angleRad = arcLength / segmentLength; CCW positive,
                ///< follows the same follower-angle direction convention as Angle.
};

/// Defines a snapping/attachment relationship between two Blocks.
/// The "from" Block's point is constrained to coincide with the "to" Block's point,
/// and the from Block's rotation is driven so that its attached segment makes the
/// follower angle (followerAngle) relative to the leader segment's direction.
///
/// ────────────────────────────────────────────────────────────────────────────
/// 术语表 (Glossary) — 全项目统一使用以下名词，避免同义混用：
///
///   连接 (Connection)   = 本结构 Attachment。两条线段端点接合的关系。
///                         UI 上统一称“连接”；代码中保留 Attachment 一名。
///   基准线 (Leader)     = to Block 的线段。被附着的一方，提供参照方向。
///                         旧称“基准”“leader”“to block”。
///   跟随线 (Follower)   = from Block 的线段。附着上去的一方，位置/旋转被驱动。
///                         旧称“跟随”“follower”“from block”。
///   跟随角度 (followerAngle) = 跟随线相对基准线的夹角（度）。**归属于跟随线**。
///                         参照方向 = 基准线在吸附点处的“延长方向”
///                         (Block::exitDirectionAtPoint)，因此：
///                         0° = 沿基准线继续直行（无论吸附在基准线的哪一端）；
///                         逆时针为正。
///                         旧称“构造角”。
///   吸附 (Snap)          = 创建连接的动作（SmartPen 起点落在已有点上）。
///
/// 约束：每个 Block 至多作为一条连接的跟随线（附着图是一棵树/森林）。
/// ────────────────────────────────────────────────────────────────────────────
struct Attachment {
    QUuid id = QUuid::createUuid();

    QUuid fromBlockId;  ///< The Block being attached (follower / 跟随线).
    QUuid fromPointId;  ///< Point on the from-Block that snaps.

    QUuid toBlockId;    ///< The target Block (leader / 基准线).
    QUuid toPointId;    ///< Point on the to-Block to snap to.

    QUuid toSegmentId;  ///< Leader SEGMENT on the to-Block whose exit direction
                        ///< is the follower-angle reference. Disambiguates
                        ///< points shared by multiple segments. Null (legacy
                        ///< documents) = fall back to the first segment found
                        ///< at toPointId (old behaviour).

    double followerAngle = 0.0;  ///< 跟随角度 in degrees (跟随角度), owned by
                               ///< the FOLLOWER. Measured from the leader's exit
                               ///< direction at toPointId (the direction that
                               ///< continues the leader straight past that point).
                               ///< 0 = continue straight along the leader, whether
                               ///< snapped at the leader's start or end; CCW positive.

    QString followerAngleFormula;  ///< Optional formula overriding followerAngle when
                                 ///< non-empty. Evaluates to degrees (no unit
                                 ///< conversion). Example: "shoulder_slope+5".

    // --- Arc-length rotation mode ---
    RotationMode rotationMode = RotationMode::Angle;

    double arcLength = 0.0;      ///< Arc length in mm (internal). The endpoint
                                 ///< sweeps this distance along the circle whose
                                 ///< radius = full segment length. CCW positive.
    QString arcLengthFormula;    ///< Optional formula overriding arcLength.
                                 ///< cm domain (auto-converted to mm).
                                 ///< Example: "sleeve_cap/2".

    bool isPin = false;  ///< Pure position pin (no rotation drive). A bridge line
                         ///< (Block::isBridge) is the follower of exactly TWO pin
                         ///< attachments; followerAngle / toSegmentId are unused for
                         ///< pins. The resolver places both pinned points on their
                         ///< hosts and lets the bridge length/direction be passive.

    bool isLocked = false;  ///< 锁定连接（强制连接）：拖动不可拆散，且拖动任一端时
                            ///< 另一端（递归）一并整体移动 —— 焊接语义。辅助层线段
                            ///< 建立的连接由 addAttachment 自动置位；工作层可由
                            ///< 属性面板手动开关。锁定时拖动跟随线也不拆（普通连接
                            ///< 拖跟随线拆散、拖宿主不拆）。
};

} // namespace cad::param
