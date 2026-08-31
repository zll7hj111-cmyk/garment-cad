#pragma once

#include <QDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "ElaPushButton.h"
#include "Theme.h"

namespace cad::ui {

/// 共享输入占位符 (2026-08-28 收口 B3): 「数值或公式」输入框的通用文案。
/// 原散落于 SegmentAngleCard/ComponentTab/LinePropertyDialog 的
/// QStringLiteral/QString::fromUtf8 副本, 统一为 QStringLiteral 常量。
inline const QString kPlaceholderAngleOrFormula = QStringLiteral("数值(°)或公式");
inline const QString kPlaceholderCmOrFormula   = QStringLiteral("数值(cm)或公式");

/// 扁平 chip 按钮样式 (原外观分区同款, 2026-xx 起全线条属性对话框按钮统一):
/// 透明底 + 1px 细边 + 2px 圆角 + 11px 紧凑字 (与状态栏紧凑化对齐)。
/// **只适用于原生 QPushButton** —— ElaPushButton 自绘 (drawRoundedRect +
/// 阴影 + 底边线), 不吃 QSS 背景/边框。
inline QString chipButtonStyle()
{
    const auto& t = cad::ui::Theme::tokens();
    return QStringLiteral(
        "QPushButton { border:1px solid %1; border-radius:2px;"
        " background:transparent; color:%2; padding:2px 8px; font-size:11px; }"
        "QPushButton:hover { background:%3; }"
        "QPushButton:checked { background:%4; border-color:%5; color:%6; }"
        "QPushButton:disabled { color:%7; }")
        .arg(t.border.name(), t.text2.name(), t.surface2.name(),
             t.accentTint.name(), t.borderStrong.name(), t.text1.name(),
             t.text3.name());
}

/// 工具表单群统一骨架 (ui-redesign-2026-08 §4.5/§5.5, P2 落地)。
///
/// 覆盖「锚点/辅助点/交点/测量结果」等独立工具表单的公共结构：
///   - 代号标题栏 makeFormTitleBar: ink 反转面实心底 + 等宽大写英文代号
///     (accent 黄) + 中文标题 —— 图纸编号感 (§4.5 标题栏)。
///   - 分组标题 makeFormGroupHeader: 等宽 10px 大写代号 + 中文 + 底部发丝线,
///     替代裸粗体分组 (§4.5 分组标题强化)。
///   - 表单栅格 applyFormGrid: 标签列固定 88px 右对齐、行距 10px,
///     与线条属性对话框同一栅格语言 (§4.5 表单栅格)。
///   - 按钮条 makeFormButtonBar: surface3 底 + 顶部分隔线, 按钮右对齐,
///     主操作最右 = 实心 accent 黄; 次级 = borderStrong 描边 (§5.5 按钮条 +
///     §5.1 按钮四级)。
///
/// 颜色在构造时从 ThemeTokens 取值烘焙 —— 表单对话框生命周期短
/// (exec/reject 即销毁), 与面板卡片同一烘焙纪律; 非模态长驻的
/// QuickAuxDialog 在主题切换后重开即恢复。
///
/// 注意: 助手只是「装配」不改变语义 —— 调用方保留原有字段/信号/objectName
/// 测试契约, 仅重排布局与样式。

/// 代号标题栏 (§4.5): 反转面底色取 tooltip 反转对 (tooltipBg/tooltipFg),
/// 英文代号亮色模式用 accent 黄、暗色模式用 onAccent 墨 (对比度保证)。
inline QWidget* makeFormTitleBar(const QString& code, const QString& title,
                                 QWidget* parent)
{
    const auto& tk = Theme::tokens();
    const bool dark = Theme::mode() == ThemeMode::Dark;

    auto* bar = new QWidget(parent);
    bar->setAttribute(Qt::WA_StyledBackground, true);
    bar->setStyleSheet(
        QStringLiteral("background-color: %1;").arg(tk.tooltipBg.name()));

    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(14, 9, 14, 9);
    lay->setSpacing(8);

    auto* codeLbl = new QLabel(code, bar);
    codeLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-family: 'Consolas','Courier New',monospace;"
        " font-size: 10px; letter-spacing: 1.5px; background: transparent; }")
        .arg(dark ? tk.onAccent.name() : tk.accent.name()));
    lay->addWidget(codeLbl);

    auto* titleLbl = new QLabel(title, bar);
    titleLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: 600;"
        " background: transparent; }").arg(tk.tooltipFg.name()));
    lay->addWidget(titleLbl);
    lay->addStretch(1);
    return bar;
}

/// 分组标题 (§4.5): 等宽 10px 大写代号 + 中文短词 + 底部 1px 发丝线。
inline QWidget* makeFormGroupHeader(const QString& code, const QString& title,
                                    QWidget* parent)
{
    auto* box = new QWidget(parent);
    auto* lay = new QVBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    auto* row = new QWidget(box);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    const auto& tk = Theme::tokens();

    auto* codeLbl = new QLabel(code, row);
    codeLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-family: 'Consolas','Courier New',monospace;"
        " font-size: 10px; letter-spacing: 1.5px; background: transparent; }")
        .arg(tk.text3.name()));
    h->addWidget(codeLbl);

    auto* titleLbl = new QLabel(title, row);
    // 中文标题 = text1 墨字 12px 600 —— 2026-08-28 用户反馈按设计稿原样
    // (text2 11px) 有「掉色感」, 加深一档; 代号保持 text3 标注感。
    titleLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; font-weight: 600;"
        " background: transparent; }").arg(tk.text1.name()));
    h->addWidget(titleLbl);
    h->addStretch(1);
    lay->addWidget(row);

    auto* line = new QFrame(box);
    line->setFrameShape(QFrame::HLine);
    line->setObjectName(QStringLiteral("divider"));  // 全局 QSS: 1px 发丝线
    line->setFixedHeight(1);
    lay->addWidget(line);
    return box;
}

/// 表单栅格 (§4.5): 标签列固定 88px 右对齐、行距 10px —— 统一对话栅格。
/// 对既有 QFormLayout 原地应用, 不改变行结构/字段/objectName。
inline void applyFormGrid(QFormLayout* form)
{
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(10);
    for (int r = 0; r < form->rowCount(); ++r) {
        if (QLayoutItem* it = form->itemAt(r, QFormLayout::LabelRole)) {
            if (QWidget* w = it->widget())
                w->setFixedWidth(88);
        }
    }
}

/// 按钮条 (§5.5/§5.1): surface3 底 + 顶部分隔线, 按钮右对齐,
/// 次级 [取消] 在左、主操作 [确定] 在右 = 实心 accent 黄 + onAccent 字。
struct FormButtonBar
{
    ElaPushButton* ok = nullptr;
    ElaPushButton* cancel = nullptr;
    QWidget* row = nullptr;
};

inline FormButtonBar makeFormButtonBar(
    QDialog* dlg, const QString& okText = QString::fromUtf8("确定"),
    const QString& cancelText = QString::fromUtf8("取消"))
{
    const auto& tk = Theme::tokens();

    auto* row = new QWidget(dlg);
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setStyleSheet(QStringLiteral(
        "background-color: %1; border-top: 1px solid %2;")
        .arg(tk.surface3.name(), tk.border.name()));
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(14, 10, 14, 10);
    lay->setSpacing(8);
    lay->addStretch(1);

    FormButtonBar out;
    out.cancel = new ElaPushButton(cancelText, row);
    out.cancel->setMinimumWidth(84);
    out.cancel->setStyleSheet(QStringLiteral(
        "ElaPushButton { background: %1; border: 1px solid %2;"
        " border-radius: 2px; padding: 4px 14px; color: %3; }")
        .arg(tk.surface.name(), tk.borderStrong.name(), tk.text1.name()));
    lay->addWidget(out.cancel);

    // 主操作 = 全屏唯一实心黄 (亮暗同底, onAccent 墨字保证可读)。
    out.ok = new ElaPushButton(okText, row);
    out.ok->setMinimumWidth(84);
    out.ok->setStyleSheet(QStringLiteral(
        "ElaPushButton { background: %1; border: 1px solid %2;"
        " border-radius: 2px; padding: 4px 14px; color: %3; font-weight: 600; }")
        .arg(tk.accent.name(), tk.accentStrong.name(), tk.onAccent.name()));
    lay->addWidget(out.ok);

    QObject::connect(out.ok, &ElaPushButton::clicked, dlg, &QDialog::accept);
    QObject::connect(out.cancel, &ElaPushButton::clicked, dlg, &QDialog::reject);
    out.row = row;
    return out;
}

} // namespace cad::ui
