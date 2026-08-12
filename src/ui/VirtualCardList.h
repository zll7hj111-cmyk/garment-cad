#pragma once

#include <QWidget>
#include <QHash>
#include <QVector>
#include <QUuid>
#include <functional>

class ElaScrollArea;

/// Virtualized vertical host for rich card widgets.
///
/// Rows are identified by stable keys (QUuid). Only rows intersecting the
/// viewport (plus a buffer band) are materialized as real widgets; rows far
/// outside are destroyed and rebuilt on demand. Per-key height caching keeps
/// the scrollbar geometry stable and scroll-back cheap, so panel cost stays
/// O(visible rows) instead of O(total rows).
///
/// The host lives inside a QScrollArea (widgetResizable = true) and lays its
/// rows out manually — no QLayout — mirroring the old container margins.
class VirtualCardList : public QWidget
{
    Q_OBJECT

public:
    using Factory = std::function<QWidget*(int row)>;        ///< Create row widget.
    using Binder  = std::function<void(int row, QWidget* w)>; ///< Sync widget with model.

    explicit VirtualCardList(QWidget* parent = nullptr);

    /// Hook the owning scroll area: its scrollbar + viewport drive the window.
    void init(ElaScrollArea* area);
    void setProviders(Factory factory, Binder binder);

    /// Replace the row structure. Rows whose key survives keep their cached
    /// height. If the key list is unchanged, only materialized rows rebind.
    void setRows(const QVector<QUuid>& keys);

    /// Rebind every materialized row (data changed, structure did not).
    void rebindAll();

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
    void relayoutHeights();   ///< Recompute prefix tops + total height.
    void repositionRows();    ///< Fit widths + move materialized rows.
    void remeasureRow(const QUuid& key);
    int  contentWidth() const;

    ElaScrollArea* m_area = nullptr;

    Factory m_factory;
    Binder  m_binder;

    QVector<QUuid> m_keys;
    QHash<QUuid, int> m_keyIndex;       ///< key → row position.
    QHash<QUuid, int> m_heights;        ///< Cached row heights.
    QHash<QUuid, QWidget*> m_widgets;   ///< Materialized rows.
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
};
