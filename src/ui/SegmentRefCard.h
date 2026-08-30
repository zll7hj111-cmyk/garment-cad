#pragma once

#include <QUuid>

#include <QWidget>

class ElaLineEdit;
class ElaPushButton;
class ElaText;

namespace cad::param {
class ParamDocument;
struct Attachment;
class Block;
}

class CanvasScene;

namespace cad::ui {

class PointRefEdit;

/// 「引用」行组 (2026-12 去卡框化, 原 SegmentConnectionCard 的引用/指向
/// 两行抽出): ①基准线/基准点 —— 角度基准 (双基准, 可与他线线段方向对比);
/// ②指向点/偏移 —— 终点方向约束。自由态引用行保持预填 (连入自动落库为角度
/// 基准, refresh 触发一次); 无连接时按钮禁用。
/// 2026-xx 用户拍板 (两维独立): 「独立角度」复选框与「清除」已删 —— 基准点
/// 按钮 = 角度维度的 拆开/重连 双面开关: 拆开 = 有连接线·无基准线 (角度独立,
/// 位置保持吸附、角度不再跟随); 重连 = 恢复角度跟随 (反算回原基准/位置宿主,
/// 零跳变)。与连接点按钮 (位置维度, SegmentConnectionCard) 独立。
/// 纯行组 (无边框/无标题), 嵌入属性页「连接」分区。
class SegmentRefCard : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentRefCard(cad::param::ParamDocument* doc,
                            CanvasScene* scene, QWidget* parent = nullptr);

    /// 切换编辑目标 (对话框 setTarget 时同步)。
    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    /// 从模型全量刷新 (引用填充 + 指向填充 + 启用态 + 预填自动落库)。
    void refresh();

signals:
    /// 角度基准 / 终点指向 已变更 —— 对话框刷新画布 (及角度卡灰态)。
    void changed();

private:
    void onAngleRefPointResolved(const QUuid& blockId, const QUuid& pointId);
    /// 角度维度 拆开/重连 (独立角 ↔ 恢复跟随)。
    void onAngleBaseToggled();
    void onAngleRefSegEdited();
    void onAimTargetResolved(const QUuid& blockId, const QUuid& pointId);
    void onAimOffsetApply();
    void onClearAim();

    [[nodiscard]] const cad::param::Attachment* findFollowerAttachment() const;
    void refreshAngleRefRow(const cad::param::Attachment* att);
    void refreshAimRow(const cad::param::Block* block);
    [[nodiscard]] QString leaderRefLabel(const cad::param::Attachment& att) const;

    cad::param::ParamDocument* m_doc = nullptr;
    CanvasScene* m_scene = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    // 引用行: [基准线][L#/P#][基准点][P#][拆开/重连].
    ElaLineEdit*  m_lblAngleRefSeg = nullptr;
    PointRefEdit* m_angleRefPoint = nullptr;
    ElaPushButton* m_btnAngleBase = nullptr;   ///< 角度维度 拆开/重连 双面按钮.
    // 指向行: [指向点][P#][偏移(°)][值][清除].
    PointRefEdit*  m_refAimPoint = nullptr;
    ElaLineEdit*   m_editAimOffset = nullptr;
    ElaPushButton* m_btnClearAim = nullptr;
};

} // namespace cad::ui
