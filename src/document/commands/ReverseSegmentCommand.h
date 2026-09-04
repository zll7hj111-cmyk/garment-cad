#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <QString>
#include <vector>
#include <utility>

#include "parametric/ParamPoint.h"
#include "geometry/Vec2.h"

namespace cad::param { class ParamDocument; class Block; struct Attachment; }

namespace cad::cmd {

/// 线段换向 (角度基准视角切换, 用户拍板 2026-08): 交换线段 start/end 身份,
/// 并把"驱动端"Polar 约束搬到另一物理端 (角度 +180 补偿), 换向后几何零跳变、
/// 修改长度/角度变为驱动另一端。物理延长尾巴不动 (extendStart/End 随端点
/// 角色互换); 宿主辅助点仅翻转 interpFromEnd (求解坐标系等价, 位置不变)。
/// v2 放开 + 自动补偿 (世界姿态/位置零跳变):
///   · 基准段消费者 (Polar refSegmentId=本段) —— 角度 +180 / 公式包裹;
///   · 相对交点宿主 (射线角相对段方向) —— interAngle +180 / 公式包裹;
///   · 跟随连接 —— followerAngle +180·k 补偿 (k = 自身 localDir 与角度基准
///     方向的翻转次数, 两次翻转相互抵消; angleIndependent 不驱动旋转不补偿);
///   · 被连接 + 旧档空角度基准点 —— 回填 angleRefPointId = 旧终点 (其出方向
///     = 原 start→end 基准, 精确等价);
///   · 曲线 —— 过点反序 + 切线互换取反 + 弦上锚点 percent→1−p / offset 取反。
/// v1 资格仍拒绝: 桥/省道/终点指向、角度测量引用 (start→end 是测量基准)、
/// 端点被块内其他线段共享、非"锚 Free + 驱动 Polar(ref=另一端)"标准结构、
/// 滑轨模式连接 (局部系快照会镜像)、需补偿的弧长模式连接 (πr 不可参数化表达)。
class ReverseSegmentCommand : public QUndoCommand
{
public:
    /// 资格检查: 可换向返回 true; 否则 reason 带中文原因 (UI 置灰提示)。
    static bool canReverse(cad::param::ParamDocument* doc,
                           const QUuid& blockId, const QUuid& segmentId,
                           QString* reason = nullptr);

    ReverseSegmentCommand(cad::param::ParamDocument* doc,
                          const QUuid& blockId, const QUuid& segmentId,
                          QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    /// 端点约束快照 (换向只动这两个点的驱动结构; 曲线时含切线)。
    struct PointSnapshot {
        cad::param::PointConstraint constraint{};
        cad::geo::Vec2  freePos;
        QUuid  refPointId;
        double distance = 0.0;
        double angle    = 0.0;
        QUuid  refSegmentId;
        QString distanceFormula;
        QString angleFormula;
        bool   interpFromEnd = false;
        cad::geo::Vec2 tangentIn;    ///< 原存储切线 (undo 恢复; 直线恒零向量, 无害)。
        cad::geo::Vec2 tangentOut;
        bool   autoTangent = true;  ///< 原自动切线标志 (冻结后恢复)。
    };

    /// v2: 方向基准消费者快照 (Polar refSegmentId / 相对交点, +180 补偿)。
    struct ConsumerSnapshot {
        QUuid pointId;
        double angle = 0.0;
        QString angleFormula;
        double interAngle = 0.0;
        QString interAngleFormula;
    };

    /// v2: 曲线过点快照。Hobby 自动切线求解不保证换序对称 (实测弧长漂移),
    /// 换向 = 从曲线缓存捕获"同解"有效切线 → 镜像后冻结为手动 (与
    /// BreakSegmentCommand 打断冻结同范式), undo 恢复原 autoTangent/切线。
    struct CurveAnchorSnapshot {
        QUuid pointId;
        bool autoTangent = true;      ///< 原标志 (undo 恢复)。
        cad::geo::Vec2 tangentIn;          ///< 原存储切线 (undo 恢复)。
        cad::geo::Vec2 tangentOut;
        cad::geo::Vec2 effTangentIn;       ///< 换向前解算有效切线 (冻结源)。
        cad::geo::Vec2 effTangentOut;
        double interpPercent = 0.0;
        double interpOffsetDist = 0.0;
    };

    /// v2: 连接补偿快照 (跟随角 +180·k / 旧档角度基准点回填 oldEnd)。
    struct AttachmentSnapshot {
        QUuid attId;
        bool compensateAngle = false;  ///< k 为奇数且角度被驱动 → followerAngle +180
        bool backfill = false;         ///< 旧档空角度基准点 → 回填旧终点 id
        double followerAngle = 0.0;
        QString followerAngleFormula;
        QUuid angleRefPointId;         ///< 原值 (可能为空, undo 恢复空 = 旧档语义)
    };

    void applyState(bool reversed);

    cad::param::ParamDocument* m_doc;
    QUuid m_blockId;
    QUuid m_segmentId;
    QUuid m_oldStartId;
    QUuid m_oldEndId;
    PointSnapshot m_oldStart;
    PointSnapshot m_oldEnd;
    std::vector<std::pair<QUuid, bool>> m_auxFromEnd;  ///< 宿主辅助点 (id, 原 interpFromEnd).
    std::vector<QUuid> m_passPoints;                   ///< 曲线过点原序 (undo 恢复).
    std::vector<ConsumerSnapshot> m_consumers;
    std::vector<CurveAnchorSnapshot> m_curveAnchors;
    bool m_curveCacheValid = false;   ///< 曲线缓存捕获成功 (span 数 = 过点数+1)。
    cad::geo::Vec2 m_effStartTangentOut;   ///< 曲线端点同解有效切线 (缓存捕获, 冻结源)。
    cad::geo::Vec2 m_effEndTangentIn;
    std::vector<cad::geo::Vec2> m_innerEffIn;   ///< 内部锚点同解有效切线 (过点序)。
    std::vector<cad::geo::Vec2> m_innerEffOut;
    std::vector<AttachmentSnapshot> m_attComp;
    double m_extendStartMm = 0.0;
    QString m_extendStartFormula;
    double m_extendEndMm = 0.0;
    QString m_extendEndFormula;
};

} // namespace cad::cmd
