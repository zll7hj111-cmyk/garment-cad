#pragma once

#include <QUuid>
#include <QWidget>

namespace cad::param {
class ParamDocument;
}

class CanvasScene;

namespace cad::ui {

class SegmentAlignPointCard;
class SegmentShadowBasisCard;
class SegmentAngleRefCard;

/// 「对齐点 + 方向」两段式行 (PANEL_REDESIGN_DESIGN §3/§6.4; 2026-12 文案 v2
/// 用户拍板; 2026-09 设计修正: 对齐点从只读 tag 改为可输入):
///   对齐点【P3】  方向：点1【p2】→点2【p1】  [独立]
/// 聚合 SegmentAlignPointCard、SegmentShadowBasisCard、SegmentAngleRefCard 三个子卡
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

private slots:
    void rejectRefInput(const QString& reason);

private:
    cad::param::ParamDocument* m_doc = nullptr;
    CanvasScene* m_scene = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    SegmentAlignPointCard*  m_alignCard = nullptr;
    SegmentShadowBasisCard* m_shadowCard = nullptr;
    SegmentAngleRefCard*    m_angleCard = nullptr;
};

} // namespace cad::ui
