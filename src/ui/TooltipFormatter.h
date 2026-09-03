#pragma once

#include <QKeySequence>
#include <QString>

#include "ui/Theme.h"

namespace cad::ui {

/// 全局统一的富文本 Tooltip 格式化工具 (Endfield 2.0 纸黄色工程图纸面规范)。
/// 生成符合 Qt 富文本子集标准的卡片排版，实现标题、键帽、功能说明与禁用原因的高对比度分层。
class TooltipFormatter
{
public:
    /// 生成技术键帽 HTML 微徽标（如 "[V]"、"[Shift+A]"）。
    [[nodiscard]] static QString keycapHtml(const QString& key)
    {
        if (key.isEmpty()) return {};
        const auto& t = Theme::tokens();
        // 在纸黄色背景上：keycap 用工整底色 + 边框 + 统一深色文字
        const QString bg = t.surface2.name();
        const QString bd = t.tooltipBorder.name();
        const QString fg = t.text1.name();
        return QStringLiteral(
            "<span style=\"background:%1; color:%2; border:1px solid %3; "
            "border-radius:3px; padding:1px 5px; font-family:'Consolas','Courier New',monospace; "
            "font-size:10px; font-weight:bold;\">%4</span>")
            .arg(bg, fg, bd, key.toHtmlEscaped());
    }

    /// 为 CAD 工具按钮（如左侧工具坞）生成结构化提示卡。
    /// @param name      工具名称（如 "选择(&V)"，内部自动剔除 & 加速符）。
    /// @param shortcut  快捷键（如 Qt::Key_V）。
    /// @param hintText  状态栏操作说明全文（内部自动分拆主要功能与操作技巧）。
    [[nodiscard]] static QString tool(const QString& name,
                                      const QKeySequence& shortcut,
                                      const QString& hintText)
    {
        const auto& t = Theme::tokens();
        const QString bgCol = t.tooltipBg.name();
        const QString titleCol = t.tooltipFg.name();
        const QString descCol = t.text2.name();
        const QString tipCol = t.text3.name();
        const QString divCol = t.tooltipBorder.name();

        QString cleanName = name;
        cleanName.remove(QLatin1Char('&'));

        const QString sc = shortcut.isEmpty()
            ? QString()
            : shortcut.toString(QKeySequence::NativeText);
        const QString badge = sc.isEmpty() ? QString() : keycapHtml(sc);

        // 尝试从 hintText 中拆分出第一句核心描述与后续操作提示/技巧
        QString desc = hintText.trimmed();
        QString tip;
        const QString sep = QString::fromUtf8("；");
        if (desc.contains(sep)) {
            tip = desc.section(sep, 1).trimmed();
            desc = desc.section(sep, 0, 0).trimmed();
        } else if (desc.contains(QLatin1Char(';'))) {
            tip = desc.section(QLatin1Char(';'), 1).trimmed();
            desc = desc.section(QLatin1Char(';'), 0, 0).trimmed();
        }

        QString tipBlock;
        if (!tip.isEmpty()) {
            tipBlock = QStringLiteral(
                "<div style=\"font-size:10px; color:%1; margin-top:5px; "
                "padding-top:4px; border-top:1px solid %2;\">"
                "💡 %3</div>")
                .arg(tipCol, divCol, tip.toHtmlEscaped());
        }

        return QStringLiteral(
            "<div style=\"min-width:140px; max-width:290px; background-color:%1; color:%2; line-height:1.4;\">"
            "<div style=\"margin-bottom:4px;\"><span style=\"font-size:12px; font-weight:bold; color:%2;\">%3</span> %4</div>"
            "<div style=\"font-size:11px; color:%5;\">%6</div>"
            "%7"
            "</div>")
            .arg(bgCol, titleCol, cleanName.toHtmlEscaped(), badge, descCol, desc.toHtmlEscaped(), tipBlock);
    }

    /// 为带快捷键的操作按钮（如 ContextStrip 属性条按钮）生成提示卡。
    /// @param title           操作标题（如 "线段换向"、"切换锚心"）。
    /// @param shortcut        快捷键（如 "X"）。
    /// @param description     功能描述。
    /// @param disabledReason  若非空，标示为不可用状态并以 danger 色警示。
    [[nodiscard]] static QString actionWithShortcut(const QString& title,
                                                    const QString& shortcut,
                                                    const QString& description,
                                                    const QString& disabledReason = QString())
    {
        const auto& t = Theme::tokens();
        const QString bgCol = t.tooltipBg.name();
        const QString titleCol = t.tooltipFg.name();
        const QString descCol = t.text2.name();
        const QString divCol = t.tooltipBorder.name();
        const QString dangerCol = t.danger.name();
        const QString badge = shortcut.isEmpty() ? QString() : keycapHtml(shortcut);

        if (!disabledReason.isEmpty()) {
            return QStringLiteral(
                "<div style=\"min-width:130px; max-width:270px; background-color:%1; color:%2; line-height:1.4;\">"
                "<div style=\"margin-bottom:3px;\"><span style=\"font-size:12px; font-weight:bold; color:%2;\">%3</span> "
                "<span style=\"color:%4; font-size:11px; font-weight:bold;\">(不可用)</span> %5</div>"
                "<div style=\"font-size:11px; color:%6;\">%7</div>"
                "<div style=\"color:%4; font-size:11px; margin-top:4px; padding-top:3px; "
                "border-top:1px solid %8; font-weight:500;\">⚠ %9</div>"
                "</div>")
                .arg(bgCol, titleCol, title.toHtmlEscaped(), dangerCol, badge,
                     descCol, description.toHtmlEscaped(), divCol, disabledReason.toHtmlEscaped());
        }

        return QStringLiteral(
            "<div style=\"min-width:130px; max-width:270px; background-color:%1; color:%2; line-height:1.4;\">"
            "<div style=\"margin-bottom:3px;\"><span style=\"font-size:12px; font-weight:bold; color:%2;\">%3</span> %4</div>"
            "<div style=\"font-size:11px; color:%5;\">%6</div>"
            "</div>")
            .arg(bgCol, titleCol, title.toHtmlEscaped(), badge, descCol, description.toHtmlEscaped());
    }

    /// 为无快捷键的操作按钮生成提示卡。
    /// @param title           操作标题。
    /// @param description     功能描述。
    /// @param disabledReason  若非空，标示为不可用状态并以 danger 色警示。
    [[nodiscard]] static QString action(const QString& title,
                                        const QString& description,
                                        const QString& disabledReason = QString())
    {
        return actionWithShortcut(title, QString(), description, disabledReason);
    }

    /// 为状态徽标或诊断信息生成提示卡。
    /// @param title      状态标题。
    /// @param detail     明细描述。
    /// @param isWarning  是否为警示/错误状态。
    [[nodiscard]] static QString status(const QString& title,
                                        const QString& detail,
                                        bool isWarning = false)
    {
        const auto& t = Theme::tokens();
        const QString bgCol = t.tooltipBg.name();
        const QString titleCol = t.tooltipFg.name();
        const QString bodyCol = t.text2.name();
        const QString dangerCol = t.danger.name();

        if (isWarning) {
            return QStringLiteral(
                "<div style=\"min-width:130px; max-width:260px; background-color:%1; color:%2; line-height:1.4;\">"
                "<div style=\"margin-bottom:3px;\"><span style=\"font-size:12px; font-weight:bold; color:%3;\">⚠ %4</span></div>"
                "<div style=\"font-size:11px; color:%2;\">%5</div>"
                "</div>")
                .arg(bgCol, bodyCol, dangerCol, title.toHtmlEscaped(), detail.toHtmlEscaped());
        }

        return QStringLiteral(
            "<div style=\"min-width:130px; max-width:260px; background-color:%1; color:%2; line-height:1.4;\">"
            "<div style=\"margin-bottom:3px;\"><span style=\"font-size:12px; font-weight:bold; color:%3;\">%4</span></div>"
            "<div style=\"font-size:11px; color:%2;\">%5</div>"
            "</div>")
            .arg(bgCol, bodyCol, titleCol, title.toHtmlEscaped(), detail.toHtmlEscaped());
    }

    /// 为简单的说明文本生成纸黄色技术提示（保证富文本包装，杜绝 Windows DWM 黑底）。
    [[nodiscard]] static QString plain(const QString& text)
    {
        if (text.isEmpty()) return {};
        if (text.startsWith(QLatin1String("<div"))) return text;
        const auto& t = Theme::tokens();
        return QStringLiteral(
            "<div style=\"background-color:%1; color:%2; font-size:11px; line-height:1.4;\">%3</div>")
            .arg(t.tooltipBg.name(), t.tooltipFg.name(), text.toHtmlEscaped());
    }
};


} // namespace cad::ui
