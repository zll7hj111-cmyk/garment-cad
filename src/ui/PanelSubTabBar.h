#pragma once

#include <QTabBar>
#include <QColor>
#include <QVector>

namespace cad::ui {

/// 变量面板子页签条 (ui-redesign-2026-08 §4.2 / §6.3)。
///
/// 两级页签的信号分离:
///  - 大标签 (面板悬浮窗 变量/图层/组件) = 导航层, 激活下划线 accent 黄;
///  - 子页签 (本条: 变量/公式/关联/测量) = 内容层, 激活下划线 = piece 类型色。
///
/// 只读页签 (关联/测量) 文字恒 text2 且激活不加粗 —— 区分「输入区」与
/// 「结果区」; 其余页签 hover text1、激活 text1 + 600 字重。
/// ElaTabBar 的下划线颜色取自 ElaTheme (单色), 无法逐页签着色, 故自绘。
class PanelSubTabBar : public QTabBar
{
public:
    explicit PanelSubTabBar(QWidget* parent = nullptr);

    /// @p accent = 激活下划线颜色 (piece 类型色); @p readOnly = 只读页签
    /// (文字恒 text2、激活不加粗)。
    void setTabProfile(int index, const QColor& accent, bool readOnly);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void syncHover(const QPoint& pos);

    QVector<QColor> m_accents;
    QVector<bool>   m_readOnly;
    int m_hoverIndex = -1;
};

} // namespace cad::ui
