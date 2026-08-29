#pragma once

#include <QProxyStyle>
#include <QPainter>
#include <QStyleOption>

#include "ui/Theme.h"

namespace cad::app {

/// 工具坞按钮样式 (ui-redesign-2026-08 §6.1 状态矩阵):
/// 激活 = 实心 accent 黄底 + onAccent 墨色图标 —— 全屏唯一实心黄 (设计原则①);
/// 按压 = accentStrong; 悬停 = surface2 底 + text1 图标; 默认 = text2 图标。
/// 替代 ElaToolButtonStyle 的 BasicSelectedAlpha 灰蓝选中态, 其余绘制路径
/// (ElaIcon 字体图标) 与 Ela 同构。
///
/// 禁用态已删除 (M3 复核): 工具坞 8 枚按钮全项目永不禁用 (全仓 setEnabled
/// 只在最近文件/图层菜单), 原 text3 disabled 分支是"以为有、实际没有"的
/// 伪状态。若未来出现"禁用某工具"需求, 需重新引入 enabled 分支。
///
/// 仅用于工具坞 8 枚按钮 (ElaIconType 字形 + 无菜单); 图层 chip 等带 QIcon/
/// 文字的按钮不走本样式 (走 Ela 默认样式, 判据 = 无 ElaIconType 属性)。
class ToolDockStyle : public QProxyStyle
{
public:
    explicit ToolDockStyle(QObject* parent = nullptr)
        : QProxyStyle()
    {
        if (parent)
            setParent(parent);
    }

    void drawComplexControl(ComplexControl cc, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget) const override
    {
        if (cc != QStyle::CC_ToolButton) {
            QProxyStyle::drawComplexControl(cc, option, painter, widget);
            return;
        }
        const auto* bopt = qstyleoption_cast<const QStyleOptionToolButton*>(option);
        // 只接管 ElaIcon 字形按钮; QIcon/文字按钮 (图层 chip) 交回默认样式。
        if (!bopt || bopt->arrowType != Qt::NoArrow || !widget
            || widget->property("ElaIconType").toString().isEmpty()) {
            QProxyStyle::drawComplexControl(cc, option, painter, widget);
            return;
        }

        const auto& tk = cad::ui::Theme::tokens();
        const bool pressed = bopt->state.testFlag(QStyle::State_Sunken);
        // checkable QAction 的选中态 (State_On) 即「当前工具」; 兼容
        // State_Selected / ElaToolButton::setIsSelected 注入的内部标志。
        const bool selected = bopt->state.testFlag(QStyle::State_On)
                              || bopt->state.testFlag(QStyle::State_Selected);
        const bool hover = bopt->state.testFlag(QStyle::State_MouseOver);

        QRect r = bopt->rect.adjusted(1, 1, -1, -1);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        QColor bg;
        if (pressed)
            bg = tk.accentStrong;          // 按压
        else if (selected)
            bg = tk.accent;                // 激活 = 全屏唯一实心黄
        else if (hover)
            bg = tk.surface2;              // 悬停
        if (bg.isValid()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(bg);
            painter->drawRoundedRect(r, 3, 3);
        }

        // ElaIcon 字形: 激活/按压 = onAccent 墨字, 悬停 = text1, 默认 = text2。
        painter->setPen((selected || pressed) ? tk.onAccent
                                              : (hover ? tk.text1 : tk.text2));
        QFont iconFont(QStringLiteral("ElaAwesome"));
        const int px = int(0.75 * std::min(bopt->iconSize.width(),
                                           bopt->iconSize.height()));
        iconFont.setPixelSize(qMax(px, 8));
        painter->setFont(iconFont);
        painter->drawText(r, Qt::AlignCenter,
                          widget->property("ElaIconType").toString());
        painter->restore();
    }
};

} // namespace cad::app
