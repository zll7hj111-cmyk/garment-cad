#pragma once

#include <QWidget>
#include <functional>

#include "parametric/Attachment.h"

class QLineEdit;
class QLabel;
class QPushButton;

namespace cad::tools {

/// Small frameless floating angle/arc-length input shown near an interaction
/// point (viewport overlay). Accepts a plain number or a formula;
/// Enter commits, Esc cancels. A toggle button switches between construction
/// angle (degrees) and arc length (cm). Shared by the selection tool
/// (post-connect angle entry) and the rotate tool.
class AngleHud : public QWidget
{
    Q_OBJECT

public:
    explicit AngleHud(QWidget* viewport);

    std::function<void(const QString&)> onTextChanged;  ///< Live preview.
    std::function<void()> onCommit;                     ///< Enter.
    std::function<void()> onCancel;                     ///< Esc.
    std::function<void(cad::param::RotationMode)> onModeChanged;  ///< Mode toggled.

    [[nodiscard]] QLineEdit* edit() const { return m_edit; }

    /// Green border when the current input is valid, red when not.
    void setValid(bool ok);

    /// Set rotation mode (updates label, unit, placeholder).
    void setMode(cad::param::RotationMode mode);
    [[nodiscard]] cad::param::RotationMode mode() const { return m_mode; }

    /// Override the caption label (e.g. “绝对角度” for free lines,
    /// “相对角度” for rotate-copy). Empty restores the mode default
    /// (“跟随角度” / “弧长”).
    void setCaption(const QString& text);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void applyModeVisuals();

    QLineEdit* m_edit = nullptr;
    QLabel* m_lblCaption = nullptr;
    QLabel* m_lblUnit = nullptr;
    QPushButton* m_btnToggle = nullptr;
    cad::param::RotationMode m_mode = cad::param::RotationMode::Angle;
    QString m_captionOverride;   ///< Non-empty = user-set caption (setCaption).
};

} // namespace cad::tools
