#pragma once

#include <QColor>
#include <QWidget>
#include <functional>

#include "parametric/Attachment.h"

class ElaLineEdit;
class ElaText;
class ElaPushButton;
class CanvasStyle;

namespace cad::ui {

/// Small frameless floating angle/arc-length input shown near an interaction
/// point (viewport overlay). Accepts a plain number or a formula;
/// Enter commits, Esc cancels. A toggle button switches between construction
/// angle (degrees) and arc length (cm). Shared by the selection tool
/// (post-connect angle entry) and the rotate tool.
class AngleHud : public QWidget
{
    Q_OBJECT

public:
    /// @p style 画布样式 (L7): 颜色直接取自 CanvasStyle, 不再沿父链反查
    /// (旧实现 viewport->parentWidget()->parentWidget() 一断链就静默回退
    /// 硬编码色)。null = 用内置默认色 (单测直构)。
    explicit AngleHud(QWidget* viewport, const CanvasStyle* style = nullptr);

    std::function<void(const QString&)> onTextChanged;  ///< Live preview.
    std::function<void()> onCommit;                     ///< Enter.
    std::function<void()> onCancel;                     ///< Esc.
    std::function<void(cad::param::RotationMode)> onModeChanged;  ///< Mode toggled.

    [[nodiscard]] ElaLineEdit* edit() const { return m_edit; }

    /// Green border when the current input is valid, red when not.
    void setValid(bool ok);

    /// 无效原因短文 (M8): 非空时显示在单位标签位置 (⚠ 前缀); 空串恢复
    /// 单位显示。公式解析失败时由宿主把 ExpressionEvaluator 的 error 传进来,
    /// 用户不再只看到"输入变红"而不知原因。
    void setError(const QString& msg);

    /// Set rotation mode (updates label, unit, placeholder).
    void setMode(cad::param::RotationMode mode);
    [[nodiscard]] cad::param::RotationMode mode() const { return m_mode; }

    /// Override the caption label (e.g. “绝对角度” for free lines,
    /// “相对角度” for rotate-copy). Empty restores the mode default
    /// (“跟随角度” / “弧长”).
    void setCaption(const QString& text);

    /// 当前显示的 caption 文本 (测试/诊断用; TOOL_SYSTEM_AUDIT H2 确认提示
    /// 后缀的断言依赖它)。
    [[nodiscard]] QString captionText() const;

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void applyModeVisuals();
    /// 单位标签按 错误短文 / 单位 切换 (M8: 错误优先)。
    void applyErrorVisual();
    /// 12% alpha wash of a CanvasStyle hue (danger-family error background).
    [[nodiscard]] static QString errorWash(const QColor& fg);

    const CanvasStyle* m_style = nullptr;
    ElaLineEdit* m_edit = nullptr;
    ElaText* m_lblCaption = nullptr;
    ElaText* m_lblUnit = nullptr;
    ElaPushButton* m_btnToggle = nullptr;
    cad::param::RotationMode m_mode = cad::param::RotationMode::Angle;
    QString m_captionOverride;   ///< Non-empty = user-set caption (setCaption).
    QString m_errorText;         ///< 非空 = 无效原因短文 (M8).
};

} // namespace cad::ui
