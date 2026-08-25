#pragma once

#include <QWidget>
#include <QHash>
#include <QList>
#include <QVector>
#include <QUuid>
#include <functional>

class ElaScrollArea;

/// Virtualized vertical host for rich card widgets.
///
/// Rows are identified by stable keys (QUuid). Only rows intersecting the
/// viewport (plus a buffer band) are materialized as real widgets; rows far
/// outside are parked in a bounded reuse pool (same-key rebind on return,
/// no reconstruction) and evicted beyond the cap. Per-key height caching
/// keeps the scrollbar geometry stable and scroll-back cheap, so panel cost
/// stays O(visible rows) instead of O(total rows).
///
/// The host lives inside a QScrollArea (widgetResizable = true) and lays its
/// rows out manually — no QLayout — mirroring the old container margins.
class VirtualCardList : public QWidget
{
    Q_OBJECT

public:
    using Factory = std::function<QWidget*(int row)>;        ///< Create row widget.
    using Binder  = std::function<void(int row, QWidget* w)>; ///< Sync widget with model.
    /// Value-only sync (每帧 resolved 热路径): 与 Binder 同形, 但只刷新数值
    /// 标签, 不整卡 rebind (2026-09 性能专项)。
    using ValueBinder = std::function<void(int row, QWidget* w)>;

    explicit VirtualCardList(QWidget* parent = nullptr);

    /// Hook the owning scroll area: its scrollbar + viewport drive the window.
    void init(ElaScrollArea* area);
    void setProviders(Factory factory, Binder binder);
    /// Light per-frame value refresh path — called by setRows() when the key
    /// list is unchanged and a value binder is installed (falls back to a full
    /// rebindAll() when absent).
    void setValueBinder(ValueBinder f) { m_valueBinder = std::move(f); }

    /// Replace the row structure. Rows whose key survives keep their cached
    /// height. If the key list is unchanged, only materialized rows rebind.
    void setRows(const QVector<QUuid>& keys);

    /// Rebind every materialized row (data changed, structure did not).
    void rebindAll();

    /// 值级刷新所有已物化行 (结构未变 + value binder 已装): 只跑轻量值更新,
    /// 不重排/不整卡 rebind。无 value binder 时回退 rebindAll()。
    void refreshValues();

    /// Row geometry in host coordinates (valid for non-materialized rows too).
    [[nodiscard]] QRect rowRect(int row) const;
    [[nodiscard]] int rowCount() const { return static_cast<int>(m_keys.size()); }
    [[nodiscard]] int rowOf(const QUuid& key) const;

    /// Materialized widget for a key, or nullptr when outside the window.
    [[nodiscard]] QWidget* widgetFor(const QUuid& key) const;

    /// Force-materialize a row even outside the window (focus a new card).
    void ensureMaterialized(const QUuid& key);

    /// Update the materialization window after external scroll changes.
    void refreshWindow();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QWidget* createRow(int row);
    void destroyWidget(const QUuid& key, QWidget* w);
    QWidget* takeCached(const QUuid& key);    ///< Pop a parked widget (or nullptr).
    void parkWidget(const QUuid& key, QWidget* w);  ///< Park + evict beyond cap.
    void relayoutHeights();   ///< Recompute prefix tops + total height.
    void repositionRows();    ///< Fit widths + move materialized rows.
    void remeasureRow(const QUuid& key);
    int  contentWidth() const;

    ElaScrollArea* m_area = nullptr;

    Factory m_factory;
    Binder  m_binder;
    ValueBinder m_valueBinder;

    QVector<QUuid> m_keys;
    QHash<QUuid, int> m_keyIndex;       ///< key → row position.
    QHash<QUuid, int> m_heights;        ///< Cached row heights.
    QHash<QUuid, QWidget*> m_widgets;   ///< Materialized rows.
    QHash<QUuid, QWidget*> m_cache;     ///< Parked (hidden) widgets, same-key reuse.
    QList<QUuid> m_cacheOrder;          ///< FIFO order of parked keys (eviction).
    QVector<int> m_tops;                ///< Prefix offsets (incl. top margin).
    int m_totalHeight = 0;
    bool m_remeasureQueued = false;
    QSet<QUuid> m_remeasurePending;

    // Same metrics as the previous QVBoxLayout-based container.
    static constexpr int kMarginLeft   = 10;
    static constexpr int kMarginRight  = 16;
    static constexpr int kMarginTop    = 8;
    static constexpr int kMarginBottom = 8;
    static constexpr int kSpacing      = 6;
    static constexpr int kBufferPx     = 400;  ///< Materialize band.
    static constexpr int kDestroyPx    = 1000; ///< Destroy band.
    static constexpr int kDefaultRowHeight = 64;
    static constexpr int kCacheCap     = 64;  ///< Parked-widget pool bound.
};
