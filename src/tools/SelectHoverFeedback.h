#pragma once

#include "geometry/Vec2.h"

#include <QCursor>
#include <QUuid>

namespace cad::param { class ParamDocument; }

namespace cad::tools {

/// 悬停/长按判定与光标形状计算 (ToolSelect 手势提炼, 阶段 3 拆分):
///   · 长按拖动判定: press 线身后进入待定, 移动超阈值由 mouseMove 判定进入
///     beginDrag (锚点 = press 位置); release 未触发 = 单击语义。已选块 / 未
///     选块两套阈值 (M6): press 已选中的块意图就是拖、误拖代价高 → 阈值
///     放宽到 10px。
///   · 无按钮悬停光标形状: 已选块端点 (kConnectGrabRadius 内) = 十字 (可连接),
///     线身 = 抓手 (可拖动), Ctrl 悬停 = 快捷复制提示, 空白 = 箭头。
/// 本控制器只做判定, 不碰选择集 / HUD / reportHoverTarget / 重叠提示 ——
/// 光标 viewport 与状态栏回调仍由 ToolSelect 主循环持有。
class SelectHoverFeedback
{
public:
    SelectHoverFeedback() = default;

    // ── 长按拖动判定 (M6 分档阈值) ──
    /// press 未选中的块。
    static constexpr double kDragThresholdPx = 5.0;
    /// press 已选中的块 (误拖代价更高)。
    static constexpr double kDragThresholdSelectedPx = 10.0;

    void beginPending(const cad::geo::Vec2& pos, const QUuid& blockId,
                      bool wasSelected);
    void cancelPending();

    /// 本次拖动阈值 (用户坐标): 已选中的块用更宽的一档。zoom<=0 按 1.0 处理。
    [[nodiscard]] double thresholdUserUnits(double zoom) const;

    /// 无按钮悬停: 根据命中/选择/修饰键算出光标形状 (同值短路由调用方做)。
    [[nodiscard]] Qt::CursorShape cursorShapeFor(
        cad::param::ParamDocument* doc, const QUuid& blockHit,
        const cad::geo::Vec2& pos, double zoom, bool ctrlHeld) const;

    /// 该块是否在 worldRadius 内有可捕捉端点 (悬停十字判定)。
    [[nodiscard]] bool blockHasEndpointNear(cad::param::ParamDocument* doc,
                                            const QUuid& blockId,
                                            const cad::geo::Vec2& pos,
                                            double worldRadius) const;

    [[nodiscard]] bool pending() const { return m_pending; }
    [[nodiscard]] bool wasSelected() const { return m_wasSelected; }
    [[nodiscard]] const QUuid& blockId() const { return m_blockId; }
    [[nodiscard]] const cad::geo::Vec2& pos() const { return m_pos; }
    [[nodiscard]] Qt::CursorShape cursor() const { return m_cursor; }
    void setCursor(Qt::CursorShape c) { m_cursor = c; }

    /// 重置内部光标状态 (工具切换时不需再设 viewport —— viewport 由 ToolSelect).
    void resetCursor();

private:
    bool            m_pending = false;
    QUuid           m_blockId;
    bool            m_wasSelected = false;
    cad::geo::Vec2  m_pos;
    Qt::CursorShape m_cursor = Qt::ArrowCursor;
};

} // namespace cad::tools
