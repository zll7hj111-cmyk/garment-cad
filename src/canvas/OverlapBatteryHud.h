#pragma once

#include <QGraphicsItem>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <vector>
#include <functional>

#include "geometry/Vec2.h"

class QGraphicsView;
class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

namespace cad::canvas {

/// 单个重叠候选数据项
struct BatteryCandidate
{
    enum class Kind { Point, Segment };
    Kind kind = Kind::Point;
    QUuid blockId;
    QUuid pointId;
    QUuid segmentId;
    int serial = 0;
    QString title;       ///< 标号徽章: 如 "P1", "L1"
    QString name;        ///< 实体名称: 如 "前片侧缝顶", "侧缝弧线"
    QString blockName;   ///< 所属图块裁片: 如 "前衣片"
    QString layerName;   ///< 图层: 如 "裁片层"
    QString roleText;    ///< 角色: 如 "轮廓线", "放置点", "端点"
    double metricValue = 0.0; ///< 长度(mm)或其他度量
    bool isPlaced = false;
    bool isAuxiliary = false;
    bool isCurveAnchor = false;
};

/// 屏幕常量重叠消歧「引线电池组/胶囊卡片」HUD (任务 4)
///
/// 具备两种展示模式:
///   1. Badge (常态微标): 在重叠点侧边显示微缩标 [● 2] 或 [━ 3], 紧凑不遮挡视野;
///   2. Expanded (展开电池组): 从重叠原点引出引线, 展开排列整齐的胶囊电池卡片 (Battery Chips).
///
/// 特性:
///   · 随缩放做 1/zoom 补偿, 屏幕像素尺寸恒定;
///   · 悬停电池芯片时, 芯片与引线高亮, 同步联动画布实体高亮;
///   · 拖拽连接模式 (ConnectMode): 电池卡片作为磁吸投靶 (Landing Pad), 端点拖入直接吸附投放.
class OverlapBatteryHud : public QGraphicsItem
{
public:
    enum class DisplayMode {
        Hidden,
        Badge,      ///< 微标态: [● N]
        Expanded    ///< 展开引线电池组态
    };

    explicit OverlapBatteryHud(QGraphicsItem* parent = nullptr);
    ~OverlapBatteryHud() override = default;

    void setCandidates(std::vector<BatteryCandidate> cands);
    [[nodiscard]] const std::vector<BatteryCandidate>& candidates() const { return m_candidates; }
    [[nodiscard]] int candidateCount() const { return static_cast<int>(m_candidates.size()); }

    void setDisplayMode(DisplayMode mode);
    [[nodiscard]] DisplayMode displayMode() const { return m_mode; }

    void setConnectMode(bool connectMode);
    [[nodiscard]] bool isConnectMode() const { return m_isConnectMode; }

    void setHoveredIndex(int index);
    [[nodiscard]] int hoveredIndex() const { return m_hoveredIndex; }

    void setSelectedIndex(int index);
    [[nodiscard]] int selectedIndex() const { return m_selectedIndex; }

    /// 更新锚点位置 (用户/世界坐标) 与视图缩放
    void updatePosition(const cad::geo::Vec2& worldPos, const QGraphicsView* view);

    /// 在世界坐标下检测命中的候选索引 (-1 表示未命中)
    [[nodiscard]] int hitCandidateAtWorld(const cad::geo::Vec2& worldPos, double zoom) const;

    /// 在场景坐标下检测命中的候选索引 (-1 表示未命中)
    [[nodiscard]] int hitCandidateAtScene(const QPointF& scenePos, double zoom) const;

    /// 获取第 index 个候选在场景坐标下的磁吸端口坐标 (供拖拽连接吸附)
    [[nodiscard]] QPointF candidatePortScenePos(int index, double zoom) const;

    /// 检查场景点是否命中微标
    [[nodiscard]] bool hitBadgeAtScene(const QPointF& scenePos, double zoom) const;

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

    // 回调钩子
    std::function<void(int index)> onCandidateHovered;
    std::function<void(int index)> onCandidateSelected;

private:
    [[nodiscard]] QRectF badgeRect() const;
    [[nodiscard]] QRectF chipRect(int index) const;
    [[nodiscard]] QPointF chipPortLocal(int index) const;

    DisplayMode m_mode = DisplayMode::Hidden;
    bool m_isConnectMode = false;
    int m_hoveredIndex = -1;
    int m_selectedIndex = -1;

    std::vector<BatteryCandidate> m_candidates;
    cad::geo::Vec2 m_worldPos;
    double m_currentZoom = 1.0;

    // 布局常量 (屏幕像素)
    static constexpr qreal kChipWidth = 196.0;
    static constexpr qreal kChipHeight = 28.0;
    static constexpr qreal kChipGap = 5.0;
    static constexpr qreal kBadgeW = 42.0;
    static constexpr qreal kBadgeH = 20.0;
    static constexpr qreal kOffsetRight = 36.0;
};

} // namespace cad::canvas
