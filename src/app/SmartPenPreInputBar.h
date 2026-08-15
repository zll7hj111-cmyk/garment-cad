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

signals:
    /// Fired whenever any field is edited (or after clearAll()).
    void valuesChanged();

private:
    ElaLineEdit* m_nameEdit  = nullptr;
    ElaLineEdit* m_lenEdit   = nullptr;
    ElaLineEdit* m_angleEdit = nullptr;
};

} // namespace cad::app
