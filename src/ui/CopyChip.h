#pragma once

#include <QWidget>

class ElaText;
class ElaLineEdit;
class QTimer;

namespace cad::ui {

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

    Variant m_variant;
    QString m_text;
    QString m_placeholder;
    bool m_copyEnabled = true;
    bool m_placeholderStyled = false;   ///< label shows placeholder style (avoids per-frame setStyleSheet)

    ElaText*   m_label = nullptr;
    ElaLineEdit* m_edit  = nullptr;   ///< Hidden overlay, shown only during editing.
    QTimer*    m_clickTimer = nullptr;
};

} // namespace cad::ui
