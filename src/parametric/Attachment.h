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

/// 滑轨模式 (抽屉式滑动, 用户拍板 2026-08): 连接姿态保持 —— 旋转照旧由
/// 基准线方向 + followerAngle 驱动 —— 但位置只保留一个自由度，在基准线
/// 局部坐标系 (x = 沿基准线方向, y = 垂直基准线) 下单向滑动，如同抽屉，
/// 用于微调。基准线旋转时滑轨跟着转。
enum class SlideMode {
    None       = 0,  ///< 普通全连接: 位置吸附 + 角度跟随 (默认)。
    AlongLeader,     ///< 沿线滑动 (模式A): 连接点沿基准线方向滑,
                     ///< 垂直偏移锁定 (slidePerpMm, 激活时快照)。
    PerpLeader,      ///< 垂直拉出 (模式B): 连接点垂直基准线拉动,
                     ///< 沿线位置锁定 (slideAlongMm, 激活时快照)。
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
///   拆开 (Detach)        = 解除位置吸附但保留角度跟随（angleOnly=true，
///                          用户拍板 2026-08：拆开默认保留角度）。
///   独立角 (angleIndependent) = 位置保持吸附、角度不跟随（用户 2026）。
///   2026-xx 两维独立 (用户拍板): angleOnly (位置维度) 与 angleIndependent
///                          (角度维度) 不再互斥 —— 双拆开 = 位置自由 + 角度
///                          自管 = 自由线 (Resolver 对 angleOnly 无条件放行
///                          位置、angleIndependent 保持自身旋转)。两维度分别
///                          由属性面板「连接点」「基准点」的拆开/重连 双面
///                          按钮控制。
///   滑轨 (Slide)         = 抽屉式单向滑动（slideMode + slideAlongMm/
///                          slidePerpMm，用户拍板 2026-08）：位置只剩一个
///                          自由度 —— 沿基准线局部系 x（沿线滑动）或 y
///                          （垂直拉出）—— 角度跟随始终保留。滑轨与
///                          拆开 (angleOnly)/独立角 互斥：进滑轨自动清
///                          angleOnly+angleIndependent 并解除拖动保护
///                          （位置必须可滑动）；切回 None 恢复全连接。
///
/// 约束：每个 Block 至多作为一条连接的跟随线（附着图是一棵树/森林）。
/// ────────────────────────────────────────────────────────────────────────────
struct Attachment {
    QUuid id = QUuid::createUuid();

    QUuid fromBlockId;  ///< The Block being attached (follower / 跟随线).
    /// 组件级连接 (用户拍板 2026-09): when non-null the FOLLOWER is a whole
    /// COMPONENT (整组作为单一实体跟随外部线). fromPointId = the exposed
    /// endpoint (暴露端点, a point on one member); the component's overall
    /// pose is driven as ONE rigid transform while member-internal relations
    /// (attachment/变量/公式) stay fully ACTIVE. fromComponentId and
    /// fromBlockId are mutually exclusive (null fromBlockId = 组件级连接).
    /// A component follows at most ONE external line (森林不变式 组件维度).
    QUuid fromComponentId;
    QUuid fromPointId;  ///< Point on the from-Block that snaps.

    QUuid toBlockId;    ///< The target Block (leader / 基准线).
    QUuid toPointId;    ///< Point on the to-Block to snap to.

    QUuid toSegmentId;  ///< Leader SEGMENT on the to-Block whose exit direction
                        ///< is the follower-angle reference. Disambiguates
                        ///< points shared by multiple segments. Null (legacy
                        ///< documents) = fall back to the first segment found
                        ///< at toPointId (old behaviour).

    /// 可选：独立角度基准 (位置锚点与角度基准分离, 用户需求 2026).
    /// 当非空时, 位置仍吸附到 toBlockId/toPointId, 但 followerAngle 改为相对
    /// 这条“角度基准线段”的方向计算。为空 = 沿用旧行为 (角度基准=位置宿主)。
    QUuid angleRefBlockId;
    QUuid angleRefSegmentId;
    /// 可选：角度基准上的具体点。当非空时，角度基准方向使用该点的出口方向
    /// （与位置连接同构），而不是固定用线段 start→end 方向；为空时兼容旧档，
    /// 回退到引用线段起点方向。
    QUuid angleRefPointId;


    double followerAngle = 0.0;  ///< 跟随角度 in degrees (跟随角度), owned by
                               ///< the FOLLOWER. Measured from the leader's exit
                               ///< direction at toPointId (the direction that
                               ///< continues the leader straight past that point).
                               ///< 闭合基准（用户拍板 2026-08）: 0° = 两线折叠
                               ///< 重叠、90° = 垂直、180° = 沿 leader 延伸直行；
                               ///< 起点/终点吸附同基准。

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

    bool isLocked = false;  ///< 拖动保护 (焊接语义): 拖动不可拆散, 且拖动任一端时
                            ///< 另一端 (递归) 一并整体移动。**新建连接默认置位**
                            ///< (用户拍板 2026-08 复旧; 2026-10 曾改为可选后按用户
                            ///< 要求回滚): addAttachment/addComponentAttachment 对
                            ///< 新建连接强制 isLocked=true; 字段默认 false 仅供
                            ///< 反序列化/undo 回放与解焊态使用。面板「拖动保护」仍
                            ///< 是可选焊接开关 (✗ = 解焊仍完整连接, 拖跟随线可拆)。
                            ///< 多线整体移动仍可交给组件 (componentClosure)。
                            ///< 旧档案读出的 isLocked=false 保持解焊 (不迁移)。
                            ///< 锁定 时拖动跟随线也不拆 (普通连接拖跟随线拆散、
                            ///< 拖宿主不拆)。

    bool angleOnly = false;  ///< 拆开保留角度 (用户拍板 2026-08): 位置吸附已解除,
                             ///< 但角度跟随保留 —— Resolver 只驱动跟随线的旋转
                             ///< (基准线方向 + followerAngle), 跳过位置约束, 因此
                             ///< 线段位置自由、平移不动角度、基准线旋转时跟着转。
                             ///< 拆开路径 (拖端点快拆 / 拖跟随线拆散 / 属性面板
                             ///< 「连接点」拆开) 统一置位。2026-xx 两维独立:
                             ///< 与 angleIndependent 不再互斥 (双拆开 = 自由线),
                             ///< 不自动清 angleIndependent; 与拖动保护互斥 (置位
                             ///< 自动清 isLocked); 与滑轨 (slideMode) 互斥 (置位
                             ///< 自动清 slideMode)。

    bool angleIndependent = false;  ///< 位置吸附保持、角度独立 (用户新需求 2026):
                                     ///< 连接仍把 from-point 钉在 leader 点上, 但
                                     ///< Resolver 不驱动跟随线旋转 —— 本线角度由
                                     ///< 自己的绝对角度/公式/旋转工具自由控制。
                                     ///< 2026-xx 两维独立 (用户拍板): 与 angleOnly
                                     ///< 不再互斥 (双拆开 = 位置自由+角度自管 = 自由
                                     ///< 线, Resolver angleOnly 分支无条件放行位置);
                                     ///< 仍与 slideMode (角度跟随+一轴滑轨) 互斥。

    /// 滑轨模式 (抽屉式滑动, 用户拍板 2026-08); 与 angleOnly 互斥.
    SlideMode slideMode = SlideMode::None;
    double slideAlongMm = 0.0;  ///< 锁轴坐标快照 (mm, 基准线局部系 x):
                                ///< 沿基准线方向的偏移 —— PerpLeader 锁此轴,
                                ///< AlongLeader 忽略 (该轴自由, 拖动回写).
    double slidePerpMm = 0.0;   ///< 锁轴坐标快照 (mm, 基准线局部系 y):
                                ///< 垂直基准线方向的偏移 —— AlongLeader 锁此轴,
                                ///< PerpLeader 忽略 (该轴自由, 拖动回写).
    QString slideAlongFormula;  ///< 沿线公式 (cm 域, 公式优先于 slideAlongMm 生效;
                                ///< 拖拽沿自由轴时清空 —— 与"公式优先、手调=数值"
                                ///< 的其它数值/公式字段同约定, Serialized since v7).
    QString slidePerpFormula;   ///< 垂直公式 (cm 域, 同理).

    /// 基准影子偏转角 (用户拍板 2026-08-27, ROTATE_REDESIGN_DESIGN.md §2.6):
    /// 有效角度基准方向 = 真基准出方向 + 本值 (度)。平时恒为 0 —— 影子贴着
    /// 真基准转, 行为与无此字段逐位一致 (旧档缺省 0 零迁移)。批量/整组刚体
    /// 旋转 δ 时, 凡"被驱朝向属于旋转集 S、而角度基准方向在 S 外"的活跃连接
    /// 写入 += δ —— 直觉 = "真基准也陪组转了 δ", 但真基准本体与其上存储
    /// 一概不动; followerAngle/followerAngleFormula/baselineOffsetDeg 三者
    /// 相互独立, 公式原样存活。连接删除即字段陪葬 (彻底自由, 不做转接)。
    /// 序列化 Optional since v11; 滑轨局部系 (slideMode) 不吃本值 —— 轨道
    /// 属于位置宿主, 与角度影子无关。
    double baselineOffsetDeg = 0.0;
};

} // namespace cad::param
