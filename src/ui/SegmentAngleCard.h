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
struct Segment;
}

class CanvasScene;

namespace cad::ui {

/// 「角度」行组 (2026-12 面板重设计): 原 SegmentConnectionCard 的角度/弧长
/// 编辑器抽为独立行组, 嵌入属性页「几何」分区 —— 角度是线的方向 (长度+方向
/// 同属几何), 连接卡只留拓扑。caption 按语义切换 (短词, 单位见占位符):
///   跟随角/弧长 —— 连接线 (含 =绝对角度 提示);
///   独立角      —— 位置吸附但角度自由 (世界方向提示);
///   角度        —— 自由线 (世界角度, 0~360° 逆时针为正)。
/// 行内: [标签64][fx][输入150][=值][=绝对角度][∠/⌒ 30x35], 全部 35px 高。
class SegmentAngleCard : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentAngleCard(cad::param::ParamDocument* doc,
                              CanvasScene* scene, QWidget* parent = nullptr);

    /// 切换编辑目标 (对话框 setTarget 时同步)。
    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    /// 全量刷新: caption/提示/输入框 (不覆盖正在输入) —— populateAngleField 由
    /// applyAngle/setTarget 内部调用, refresh 只刷标签/行状态。
    void refresh();
    /// 应用角度编辑 (debounce / Enter / 失焦): 写跟随角/弧长 或 自由端点 Polar 角。
    void applyAngle();
    /// 桥接线: 显示测出的世界角 (只读), 禁用编辑器。
    void setBridgeReadOnly(bool bridge);

signals:
    /// 值已写入模型 —— 对话框刷新画布。
    void changed();
    /// 输入文本变化 —— 对话框重启 debounce。
    void angleEdited();
    /// 线段已换向 —— 对话框全量重填 (端点微卡 P1/P2 标签互换) + 刷画布。
    void reversed();

private:
    void onAngleDirty();
    void onModeToggle();
    void onReverseClicked();
    void refreshBasisRow();
    void onDocResolved();
    void populateAngleField();
    void updateWorldAngleLabel(const cad::param::Attachment& att);
    /// 目标线的非 pin 跟随 attachment (自由线时 nullptr)。
    [[nodiscard]] const cad::param::Attachment* findFollowerAttachment() const;

    cad::param::ParamDocument* m_doc = nullptr;
    CanvasScene* m_scene = nullptr;   // unused now (保留构造签名一致性).
    QUuid m_blockId;
    QUuid m_segmentId;

    ElaText*      m_lblCaption = nullptr;   ///< caption: 跟随角度(°)/弧长(cm)/独立角度(°)/角度(°).
    ElaText*      m_lblFxAngle = nullptr;   ///< fx 指示 (公式).
    ElaLineEdit*  m_editAngle  = nullptr;
    ElaText*      m_lblFollowValue = nullptr;  ///< 公式当前计算值 (objectName followValueLabel).
    ElaText*      m_lblWorldAngle = nullptr;   ///< "= 绝对角度 xx°" / "= 世界角度 xx°".
    ElaPushButton* m_btnAngleMode = nullptr;   ///< ∠/⌒ 切换 (几何保持).
    ElaText*      m_lblBasisValue = nullptr;  ///< 角度基准 "P1 → P2" (起点→终点).
    ElaPushButton* m_btnReverse = nullptr;    ///< 换向 (交换角度基准视角).
};

} // namespace cad::ui
