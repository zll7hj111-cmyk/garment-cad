#pragma once

#include <QUuid>

#include <QPushButton>
#include <QWidget>

class ElaText;

namespace cad::param {
class ParamDocument;
struct Attachment;
class Block;
}

class CanvasScene;

namespace cad::ui {

class PointRefEdit;

/// 「对齐点 + 方向」两段式行 (PANEL_REDESIGN_DESIGN §3/§6.4; 2026-12 文案 v2
/// 用户拍板; 2026-09 设计修正: 对齐点从只读 tag 改为可输入):
///   对齐点【P3】  方向：点1【p2】→点2【p1】  [独立]
/// · 对齐点 = 本线段的哪个端点钉在目标点上 (Attachment::fromPointId) ——
///   只允许本线端点 (P3/P4 互选), 输入即重设吸附端 + 反算角度零跳变。
///   **与换向 (进出身份) 完全无关**: fromPointId 由连接语义决定, 不随
///   start/end 翻转 (旧实现绑 startPointId 的只读 tag, 换向后乱跳)。
/// · 点1/点2 是**任意两个点**, 两点连线即基准方向; 自动态 (跟随所连的线)
///   回显 目标点 P2 与 宿主线段另一端 P1, 留空则退化为单点出口方向。
/// · [独立] 按钮 (checkable): 勾选 = 角度改用世界角度 (输入框清空+禁用,
///   模型保留原 ref 字段作唯一缓存); 再点 = 还原上次内容 (反算零跳变)。
/// 终点指向 (终点连接) 生效时旋转由 Resolver Step 7 驱动, 角度基准无意义
/// → 整行隐藏 (互斥)。纯行组 (无边框), 嵌入属性页「摆放」角度区。
class SegmentRefCard : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentRefCard(cad::param::ParamDocument* doc,
                            CanvasScene* scene, QWidget* parent = nullptr);

    /// 切换编辑目标 (对话框 setTarget 时同步)。
    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    /// 从模型全量刷新 (锚点 tag/点回填/启用态/预填自动落库/终点指向隐藏)。
    void refresh();

signals:
    /// 角度基准已变更 —— 对话框刷新画布 (及角度卡灰态)。
    void changed();

private:
    void onAngleRefPointResolved(const QUuid& blockId, const QUuid& pointId);
    void onAngleRefPoint2Resolved(const QUuid& blockId, const QUuid& pointId);
    /// 对齐点 (2026-09 设计修正): 本线段的哪个端点钉在目标点上 —— 写
    /// Attachment::fromPointId + 反算 followerAngle 零跳变。与换向自动解耦
    /// （fromPointId 由连接语义决定, 不随 start/end 身份翻转）。
    void onAlignPointResolved(const QUuid& blockId, const QUuid& pointId);
    /// 本线自身成员/重复点1 等非法基准输入: toast 明示拒绝理由 + 对齐点
    /// tag 红色闪烁 (不静默刷回 —— 静默刷回 = "点2 填不进"假象, 2026-09)。
    void rejectRefInput(const QString& reason);
    /// [独立] 按钮: 独立角 ↔ 恢复跟随 (还原上次基准, 反算零跳变)。
    void onIndependentToggled(bool checked);

    [[nodiscard]] const cad::param::Attachment* findFollowerAttachment() const;
    void refreshAngleRefRow(const cad::param::Attachment* att);

    cad::param::ParamDocument* m_doc = nullptr;
    CanvasScene* m_scene = nullptr;   ///< 拒绝反馈 toast (点2 非法输入, 2026-09).
    QUuid m_blockId;
    QUuid m_segmentId;

    // 两段式行: 对齐点【PointRefEdit】  方向：点1【PointRefEdit】→点2【PointRefEdit】 [独立]
    PointRefEdit* m_alignPointEdit = nullptr;   ///< 对齐点 (本线端点, 可输入).
    ElaText*      m_lblDirWord = nullptr;       ///< "方向：" 标签 (endTarget 时隐藏).
    PointRefEdit* m_angleRefPoint = nullptr;  ///< 点1.
    PointRefEdit* m_angleRefPoint2 = nullptr; ///< 点2.
    QPushButton* m_btnIndependent = nullptr;  ///< [独立] checkable.
};

} // namespace cad::ui
