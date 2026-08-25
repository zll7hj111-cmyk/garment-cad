#pragma once

#include <QWidget>
#include <QUuid>

#include <functional>

#include "CopyChip.h"  // CopyChip::Variant is a nested enum — full type required

class ElaLineEdit;
class ElaText;
class ElaToolButton;
class QHBoxLayout;

/// Shared skeleton for the five virtual-list cards
/// (VariableCard / FormulaCard / LinkedCard / MeasureCard / AngleMeasureCard).
///
/// Owns the machinery that used to be copy-pasted across all five:
///   - the alternating left accent bar (paintEvent: 偶数行蓝 / 奇数行橙),
///   - the hover-revealed delete button with its same-size placeholder slot
///     (enterEvent/leaveEvent — 防布局跳动),
///   - the row-ordinal label (setIndex; per-card prefix via indexText()),
///   - the name chip and the monospace value label,
///   - the 🔒 read-only marker (setupLockIcon),
///   - the dangling value-label style flip (setValueLabelDangling),
///   - the per-frame value no-op guard (refreshValueGuard).
///
/// Derived cards build their own header/detail rows with the protected
/// helpers, keep their own data members and signals, and inherit
/// setIndex / setAlternate. No Q_OBJECT here — nothing in the base emits.
class CardBase : public QWidget
{
public:
    ~CardBase() override = default;

    /// Set the view-row ordinal shown in the header. Pure presentation — the
    /// panels re-apply this on every (re)bind of a reused card.
    void setIndex(int n);

    /// Set the alternating row parity (odd = orange bar, even = blue).
    /// Re-applied on every (re)bind — reused cards must not keep a stale
    /// parity from their previous row position.
    void setAlternate(bool alternate);

protected:
    explicit CardBase(bool alternate, QWidget* parent = nullptr);

    // --- Common widget builders (parent = this; caller adds to its layout) ---
    /// Row ordinal label with the shared "font-size: 11px" style.
    ElaText* createIndexLabel(const QString& objectName, const QString& tooltip);
    /// Name chip (editable, CopyChip) with the given placeholder/text.
    cad::ui::CopyChip* createNameChip(cad::ui::CopyChip::Variant variant,
                                      const QString& placeholder,
                                      const QString& text);
    /// Monospace value label; @p bold=false for the formula result label.
    ElaText* createValueLabel(bool bold = true);
    /// 🔒 read-only marker appended to the header by the caller.
    void setupLockIcon(const QString& tooltip);
    /// Append [placeholder slot][delete button] to @p header. The slot is a
    /// same-size transparent placeholder that swaps visibility with the
    /// button on hover, so layout space stays constant (VirtualCardList 不重测).
    void appendDeleteButton(QHBoxLayout* header, const QString& tooltip);
    /// "注释…" line editor (height 22); caller sets text and adds to layout.
    ElaLineEdit* createCommentEdit(QWidget* parent);

    /// Parameter bundle for buildReadOnlySkeleton() — the only per-card
    /// differences among the three read-only cards
    /// (LinkedCard / MeasureCard / AngleMeasureCard).
    struct ReadOnlySkeletonSpec
    {
        QString objectName;      ///< Card objectName (e.g. "LinkedCard").
        QString indexObjectName; ///< Row-ordinal label objectName (test contract: linkedIndex/measureIndex/angleIndex).
        QString indexTooltip;    ///< Row-ordinal tooltip.
        QString namePlaceholder; ///< Name chip placeholder ("名称").
        QString nameText;        ///< Initial name text.
        QString deleteTooltip;   ///< Delete-button tooltip.
        QString lockTooltip;     ///< 🔒 tooltip ("自动测量，不可编辑").
        QString sourceTooltip;   ///< Source-info tooltip (per-card wording).
        QString sourceLabel;     ///< Initial source-info text.
        QString commentText;     ///< Initial comment text.
        QString refName;         ///< Initial refName text.
        int     refChipWidth = 72;  ///< Ref-chip fixed width (Linked/Measure 72, Angle 84).
    };

    /// Build the shared skeleton of the three read-only cards: header
    /// [ordinal][name chip][value][🔒][✕] + detail [ref chip][source][comment],
    /// and wire the shared delete/edited connections through the callbacks.
    /// Caller then refreshes the value text (refreshValue) and keeps its own
    /// per-card members (m_refChip/m_sourceInfo/m_commentEdit are set here).
    void buildReadOnlySkeleton(const ReadOnlySkeletonSpec& spec,
                               std::function<void()> onDelete,
                               std::function<void()> onEdited);

    /// Flip the value label between the dangling style and the monospace
    /// style. Only the stylesheet/tooltip — the label TEXT (e.g. "—") is the
    /// caller's job. setStyleSheet re-polishes the panel, so callers must
    /// only invoke this when the dangling state actually flips.
    void setValueLabelDangling(bool dangling, const QString& tooltip);

    /// Value no-op guard for the read-only cards' per-frame refresh: returns
    /// true when nothing visible changed (same dangling state and |Δvalue| <
    /// 1e-3, well below the 0.01 cm display precision). Cards with extra
    /// guard inputs (MeasureCard's kind) test them around this call.
    bool refreshValueGuard(double value, bool dangling);

    /// Ordinal text for setIndex(). Default "N" (empty when n <= 0);
    /// MeasureCard "测 N", AngleMeasureCard "角 N".
    virtual QString indexText(int n) const;

    /// Left accent-bar X offset (FormulaCard shifts by kGroupIndent when
    /// the card is a group member).
    virtual int accentBarX() const;

    // --- Shared state (derived cards read/write these) ---
    bool m_alternate = false;
    ElaText*        m_indexLabel = nullptr;
    cad::ui::CopyChip* m_nameChip = nullptr;
    ElaText*        m_valueLabel = nullptr;
    ElaText*        m_lockIcon = nullptr;
    QWidget*        m_deleteBtnSlot = nullptr;  ///< 悬停占位: 与删除按钮同尺寸互斥显隐, 防布局跳动.
    ElaToolButton*  m_deleteBtn = nullptr;

    // --- Read-only-card shared members (built by buildReadOnlySkeleton) ---
    QWidget*          m_detail = nullptr;
    cad::ui::CopyChip* m_refChip = nullptr;
    ElaText*          m_sourceInfo = nullptr;
    ElaLineEdit*      m_commentEdit = nullptr;

    // --- Value refresh guard state ---
    bool   m_danglingStyled = false;  ///< current value-label style state (avoids per-frame setStyleSheet)
    bool   m_hasShownValue = false;   ///< guard armed after the first refresh
    double m_lastValue = 0.0;         ///< last shown value (no-op guard for per-frame sync)

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
};
