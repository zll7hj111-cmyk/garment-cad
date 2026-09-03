#pragma once

#include <QWidget>
#include <QString>

class ElaLineEdit;
class QTimer;

namespace cad::ui {

class CompoundChipLabel;

/// A unified chip presenting both reference identifier (e.g. "B" / "M_01")
/// and natural language name (e.g. "胸围" / "前袖窿弧长") in a two-slot badge:
///
///   ┌───────────────┬────────────────────────┐
///   │   B (代码格)   │  胸围 (名称格)          │
///   └───────────────┴────────────────────────┘
///
/// Dual-slot behavior:
///   • Both slots are permanently visible with clear placeholders ("代码" | "名称")
///   • Click on ref part  → copy refName to clipboard (emits refClicked / brief "✓")
///   • Double-click ref   → inline edit refName (emits refEdited)
///   • Double-click name  → inline edit name (emits nameEdited)
///   • Tab navigation: Tab in ref jumps to name; Tab in name advances to next widget
///   • Smart compound parsing: typing "B 胸围" or "B:胸围" in name edit splits automatically
class CompoundChip : public QWidget
{
    Q_OBJECT

public:
    explicit CompoundChip(QWidget* parent = nullptr);
    ~CompoundChip() override = default;

    [[nodiscard]] QString refName() const { return m_refName; }
    void setRefName(const QString& ref);

    [[nodiscard]] QString name() const { return m_name; }
    void setName(const QString& name);

    void setPlaceholderText(const QString& ph);
    void setRefPlaceholderText(const QString& ph);

    void setRefEditable(bool editable) { m_refEditable = editable; }
    void setRefCopyEnabled(bool enabled) { m_refCopyEnabled = enabled; }

    /// Focus inline editor for name (e.g. immediately after card creation).
    void focusNameEdit();
    /// Focus inline editor for reference name.
    void focusRefEdit();

    /// Attempt to parse compound input (e.g. "B 胸围", "B:胸围", "B=胸围").
    static bool tryParseCompound(const QString& input, QString& outRef, QString& outName);

signals:
    void refEdited(const QString& refName);
    void nameEdited(const QString& name);
    void refClicked(const QString& refName);
    void nameClicked(const QString& name);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void updatePartsGeometry();
    void enterRefEdit();
    void commitRefEdit();
    void enterNameEdit();
    void commitNameEdit();
    void copyRefText();

    QString m_refName;
    QString m_name;
    QString m_refPlaceholder = QStringLiteral("代码");
    QString m_namePlaceholder = QStringLiteral("名称");

    bool m_refEditable = true;
    bool m_refCopyEnabled = true;

    CompoundChipLabel* m_refLabel = nullptr;
    CompoundChipLabel* m_nameLabel = nullptr;

    ElaLineEdit* m_refEdit = nullptr;
    ElaLineEdit* m_nameEdit = nullptr;

    QTimer* m_copyFeedbackTimer = nullptr;
    bool m_showingCopyFeedback = false;
};

} // namespace cad::ui
