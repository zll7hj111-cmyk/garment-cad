#pragma once

#include <QWidget>

#include "ElaText.h"

class ElaLineEdit;
class QTimer;

namespace cad::ui {

class CopyChip;

/// 常驻输入框样式的承载控件 (ElaText 子类)。
///
/// 背景 + 1px 圆角描边画在 label 的 paintEvent 里, 与文本同上下文上屏。
/// 为什么不在 chip 本体画: 2026-08 排查确认 chip 本体的 paintEvent 被
/// Qt 合成器以空 clip 调用 (visibleRegion 非空、ev->region() 非空、QPainter
/// device/engine/transform 全部正常, 唯独 painter clip 为 0x0 —— 所有自绘
/// 静默丢弃, grab/render/屏幕全不可见), 而其子控件 (ElaText label) 的绘制
/// 路径正常。故描边由 label 绘制: 文本、背景、边框同一绘制上下文。
class ChipLabel : public ElaText
{
public:
    ChipLabel(const QString& text, int pixelSize, CopyChip* host,
              QWidget* parent = nullptr);

    /// hover 加深底色; 触发本控件重绘 (描边+底色随 hover 变化)。
    void setHovered(bool hovered);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    CopyChip* m_host;
    bool m_hovered = false;
};

/// A click-to-copy / double-click-to-edit text chip.
///
///   • Single click  → copy the text to the clipboard (brief "✓已复制" feedback).
///   • Double click  → overlay an inline editor; Enter / focus-out commits.
///
/// Uses a QLabel for display with a QLineEdit overlay for editing.
/// No QStackedWidget — avoids layout height conflicts on Windows.
class CopyChip : public QWidget
{
    Q_OBJECT

public:
    enum class Variant { Name, Ref, Formula };

    explicit CopyChip(Variant variant, QWidget* parent = nullptr);

    [[nodiscard]] QString text() const { return m_text; }
    void setText(const QString& text);
    void setPlaceholderText(const QString& ph);
    void setCopyEnabled(bool on) { m_copyEnabled = on; }

    /// Enter edit mode and focus the editor (used right after creation).
    void focusEdit();

    [[nodiscard]] bool isHovered() const { return m_hovered; }

signals:
    void edited(const QString& text);   ///< Committed a new value.
    void copied(const QString& text);   ///< Text copied to clipboard.

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    /// QSS attribute-selector key for the chip variant.
    [[nodiscard]] QString variantKey() const;
    void updateDisplay();
    void enterEdit();
    void commitEdit();
    void copyText();
    void setHovered(bool hovered);

    Variant m_variant;
    QString m_text;
    QString m_placeholder;
    bool m_copyEnabled = true;
    bool m_placeholderStyled = false;   ///< label shows placeholder style (avoids per-frame setStyleSheet)
    bool m_hovered = false;             ///< hover 加深底色 (chip 本体绘制).

    ChipLabel* m_label = nullptr;
    ElaLineEdit* m_edit  = nullptr;   ///< Hidden overlay, shown only during editing.
    QTimer*    m_clickTimer = nullptr;
};

} // namespace cad::ui
