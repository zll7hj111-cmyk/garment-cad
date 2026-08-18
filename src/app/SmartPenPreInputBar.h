#pragma once

#include <QString>
#include <QWidget>

class ElaLineEdit;

namespace cad::app {

/// Status-bar one-shot pre-input strip for the smart pen (智能笔预输入条).
///
/// Visible while the smart pen is active. Name / length (cm) / angle (deg)
/// typed here are consumed by the NEXT line the tool commits, then cleared —
/// the strip returns to its empty pre-input state for the following line.
/// Length and angle accept numbers or formulas (same semantics as the
/// SegmentEditBar).
///
/// Implements input containment (输入包含): all key presses and shortcut
/// overrides inside the line edits are contained within the input fields,
/// preventing global tool shortcuts (V, L, C, R, B, I, A, H, etc.) from kicking
/// the user out of the tool while typing.
class SmartPenPreInputBar : public QWidget
{
    Q_OBJECT

public:
    explicit SmartPenPreInputBar(QWidget* parent = nullptr);

    /// Clear all three fields after the values have been used.
    void clearAll();

    [[nodiscard]] QString nameText() const;
    [[nodiscard]] QString lengthText() const;
    [[nodiscard]] QString angleText() const;

    /// Set reference to the canvas view so focus returns smoothly to the canvas
    /// when Enter is pressed on the last field or Esc is pressed.
    void setCanvasView(QWidget* canvasView);

    void focusFirstNameField();
    void focusLengthField();
    void focusAngleField();

    [[nodiscard]] ElaLineEdit* nameEdit() const { return m_nameEdit; }
    [[nodiscard]] ElaLineEdit* lengthEdit() const { return m_lenEdit; }
    [[nodiscard]] ElaLineEdit* angleEdit() const { return m_angleEdit; }

signals:
    /// Fired whenever any field is edited (or after clearAll()).
    void valuesChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    ElaLineEdit* m_nameEdit   = nullptr;
    ElaLineEdit* m_lenEdit    = nullptr;
    ElaLineEdit* m_angleEdit  = nullptr;
    QWidget*     m_canvasView = nullptr;
};

} // namespace cad::app

