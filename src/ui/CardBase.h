#pragma once

#include <QWidget>
#include <QUuid>

#include <functional>

#include "CopyChip.h"  // CopyChip::Variant is a nested enum — full type required
#include "ui/NoteButton.h"

class ElaLineEdit;
class ElaText;
class ElaToolButton;
class QHBoxLayout;

namespace cad::ui {

/// Card accent-bar role (ui-redesign-2026-08 §2.5 方案 A, 用户拍板):
/// the left 4px bar carries the card's TYPE color instead of the old
/// blue/orange row alternation — 变量=碳灰 / 公式=深青 / 测量=陶土 / 关联=钴蓝.
enum class CardAccent
{
    Variable,  ///< piece1 碳灰 (also: component work-group rows)
    Formula,   ///< piece2 深青
    Measure,   ///< piece3 陶土 (MeasureCard / AngleMeasureCard)
    Linked,    ///< piece4 钴蓝
};

} // namespace cad::ui

/// Shared skeleton for the five virtual-list cards
/// (VariableCard / FormulaCard / LinkedCard / MeasureCard / AngleMeasureCard).
///
/// Owns the machinery that used to be copy-pasted across all five:
///   - the type-colored left accent bar (paintEvent: piece 家族, 方案 A),
///   - the hover-revealed delete button with its same-size placeholder slot
///     (enterEvent/leaveEvent — 防布局跳动; 占位槽默认画 24% 透明 ✕ 轮廓),
///   - the row-ordinal label (setIndex; per-card prefix via indexText()),
///   - the name chip and the monospace value label,
///   - the 🔒 read-only marker (setupLockIcon),
///   - the dangling value-label style flip (setValueLabelDangling: danger 字
///     + danger 8% 浅底 + 卡片 ⚠ badge),
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

    /// Row parity is no longer visual (类型色竖线取代蓝橙交替, 方案 A) but the
    /// virtual-list binders still call this on every rebind — kept as a no-op
    /// for API compatibility.
    void setAlternate(bool alternate);

protected:
    explicit CardBase(bool alternate, QWidget* parent = nullptr);

    /// Select the piece type color of the accent bar (paint resolves the
    /// token live, so theme switches restyle without rebinding).
    void setAccentRole(cad::ui::CardAccent role);

    // --- Common widget builders (parent = this; caller adds to its layout) ---
    /// Row ordinal label with the shared "font-size: 11px" style.
    ElaText* createIndexLabel(const QString& objectName, const QString& tooltip);
    /// Name chip (editable, CopyChip) with the given placeholder/text.
    cad::ui::CopyChip* createNameChip(cad::ui::CopyChip::Variant variant,
                                      const QString& placeholder,
                                      const QString& text);
    /// Monospace value label; @p bold=false for the formula result label.
    ElaText* createValueLabel(bool bold = true);
    /// 10px text3 unit caption ("cm" / "°") appended right of the value —
    /// the value is the card's first focus, the unit retires to meta size.
    ElaText* createUnitLabel(const QString& unit);
    /// ⚠ dangling badge inserted right after the value label (hidden until
    /// setValueLabelDangling(true)). VariableCard/FormulaCard headers call
    /// this; buildReadOnlySkeleton calls it automatically.
    void appendDanglingBadge(QHBoxLayout* header);
    /// 🔒 read-only marker appended to the header by the caller.
    void setupLockIcon(const QString& tooltip);
    /// Append [placeholder slot][delete button] to @p header. The slot is a
    /// same-size transparent placeholder that swaps visibility with the
    /// button on hover, so layout space stays constant (VirtualCardList 不重测).
    void appendDeleteButton(QHBoxLayout* header, const QString& tooltip);
    /// 便利贴注释按钮 (NoteButton, 高度/尺寸可配, 默认 22); 子类或骨架添加到布局。
    cad::ui::NoteButton* createNoteButton(QWidget* parent, int size = 22);

    /// Set the comment text without emitting signals, and skip entirely while
    /// the user is editing (m_popup guard in NoteButton).
    void setCommentSilently(const QString& text);

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
        QString unit;            ///< Value unit caption ("cm" / "°", empty = none).
        int     refChipWidth = 72;  ///< Ref-chip fixed width (Linked/Measure 72, Angle 84).
    };

    /// Build the shared skeleton of the three read-only cards: header
    /// [ordinal][name chip][value][🔒][✕] + detail [ref chip][source][note],
    /// and wire the shared delete/edited connections through the callbacks.
    /// Caller then refreshes the value text (refreshValue) and keeps its own
    /// per-card members (m_refChip/m_sourceInfo/m_noteBtn are set here).
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
    bool m_alternate = false;  ///< 行奇偶 (视觉已由类型色取代, 仅保留 bind 契约).
    cad::ui::CardAccent m_accentRole = cad::ui::CardAccent::Variable;
    bool m_hovered = false;    ///< 悬停态: 描边转 borderStrong (§6.2 状态矩阵).
    ElaText*        m_indexLabel = nullptr;
    cad::ui::CopyChip* m_nameChip = nullptr;
    ElaText*        m_valueLabel = nullptr;
    ElaText*        m_unitLabel = nullptr;
    ElaText*        m_danglingBadge = nullptr;  ///< ⚠ 引用失效 badge (dangling 时显).
    ElaText*        m_lockIcon = nullptr;
    QWidget*        m_deleteBtnSlot = nullptr;  ///< 悬停占位: 与删除按钮同尺寸互斥显隐, 防布局跳动.
    ElaToolButton*  m_deleteBtn = nullptr;

    // --- Read-only-card shared members (built by buildReadOnlySkeleton) ---
    QWidget*             m_detail = nullptr;
    cad::ui::CopyChip*   m_refChip = nullptr;
    ElaText*             m_sourceInfo = nullptr;
    cad::ui::NoteButton* m_noteBtn = nullptr;

    // --- Value refresh guard state ---
    bool   m_danglingStyled = false;  ///< current value-label style state (avoids per-frame setStyleSheet)
    bool   m_hasShownValue = false;   ///< guard armed after the first refresh
    double m_lastValue = 0.0;         ///< last shown value (no-op guard for per-frame sync)

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
};
