#include "VirtualCardList.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QApplication>
#include <QTimer>
#include <QSet>
#include <QEvent>

#include <utility>

VirtualCardList::VirtualCardList(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void VirtualCardList::init(QScrollArea* area)
{
    m_area = area;
    if (!m_area)
        return;
    connect(m_area->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &VirtualCardList::refreshWindow);
    connect(m_area->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, &VirtualCardList::refreshWindow);
    m_area->viewport()->installEventFilter(this);
}

void VirtualCardList::setProviders(Factory factory, Binder binder)
{
    m_factory = std::move(factory);
    m_binder = std::move(binder);
}

int VirtualCardList::contentWidth() const
{
    return qMax(0, width() - kMarginLeft - kMarginRight);
}

QSize VirtualCardList::sizeHint() const
{
    return QSize(200, m_totalHeight);
}

QSize VirtualCardList::minimumSizeHint() const
{
    return QSize(0, m_totalHeight);
}

QRect VirtualCardList::rowRect(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_keys.size()))
        return {};
    const int h = m_heights.value(m_keys[row], kDefaultRowHeight);
    return QRect(kMarginLeft, m_tops[row], contentWidth(), h);
}

int VirtualCardList::rowOf(const QUuid& key) const
{
    return m_keyIndex.value(key, -1);
}

QWidget* VirtualCardList::widgetFor(const QUuid& key) const
{
    return m_widgets.value(key, nullptr);
}

void VirtualCardList::setRows(const QVector<QUuid>& keys)
{
    if (keys == m_keys) {
        rebindAll();
        return;
    }

    const QSet<QUuid> newKeys(keys.begin(), keys.end());

    // Drop materialized widgets whose row disappeared. Snapshot first:
    // destroying a focused editor commits its edit, which may re-enter
    // setRows() synchronously.
    QList<std::pair<QUuid, QWidget*>> snapshot;
    snapshot.reserve(m_widgets.size());
    for (auto it = m_widgets.begin(); it != m_widgets.end(); ++it)
        snapshot.emplace_back(it.key(), it.value());
    for (const auto& [key, w] : snapshot) {
        if (!newKeys.contains(key))
            destroyWidget(key, w);
    }

    // Drop cached row heights of vanished keys too (aligned with the widget
    // teardown above — the cache only ever grew, so stale keys from deleted
    // entities / document resets leaked forever in long sessions). Heights
    // are re-measured on the next materialization.
    for (auto it = m_heights.begin(); it != m_heights.end();) {
        if (newKeys.contains(it.key()))
            ++it;
        else
            it = m_heights.erase(it);
    }

    m_keys = keys;
    m_keyIndex.clear();
    m_keyIndex.reserve(static_cast<int>(keys.size()));
    for (int i = 0; i < static_cast<int>(keys.size()); ++i)
        m_keyIndex.insert(keys[i], i);

    relayoutHeights();
    repositionRows();
    refreshWindow();
}

void VirtualCardList::rebindAll()
{
    if (!m_binder)
        return;
    // Snapshot: binders may commit edits and mutate the set.
    QList<std::pair<QUuid, QWidget*>> snapshot;
    snapshot.reserve(m_widgets.size());
    for (auto it = m_widgets.begin(); it != m_widgets.end(); ++it)
        snapshot.emplace_back(it.key(), it.value());
    for (const auto& [key, w] : snapshot) {
        const int row = m_keyIndex.value(key, -1);
        if (row >= 0)
            m_binder(row, w);
    }
}

void VirtualCardList::relayoutHeights()
{
    const int n = static_cast<int>(m_keys.size());
    m_tops.resize(n);
    int top = kMarginTop;
    for (int i = 0; i < n; ++i) {
        m_tops[i] = top;
        top += m_heights.value(m_keys[i], kDefaultRowHeight) + kSpacing;
    }
    m_totalHeight = (n > 0 ? top - kSpacing : kMarginTop) + kMarginBottom;
    updateGeometry();
}

void VirtualCardList::repositionRows()
{
    const int w = contentWidth();
    const auto snapshot = m_widgets;  // copy: geometry changes may re-enter
    for (auto it = snapshot.begin(); it != snapshot.end(); ++it) {
        const int row = m_keyIndex.value(it.key(), -1);
        if (row < 0)
            continue;
        const int h = m_heights.value(it.key(), kDefaultRowHeight);
        it.value()->setGeometry(kMarginLeft, m_tops[row], w, h);
    }
}

QWidget* VirtualCardList::createRow(int row)
{
    if (row < 0 || row >= static_cast<int>(m_keys.size()) || !m_factory)
        return nullptr;
    const QUuid key = m_keys[row];
    if (auto* existing = m_widgets.value(key, nullptr))
        return existing;

    QWidget* w = m_factory(row);
    if (!w)
        return nullptr;
    if (w->parent() != this)
        w->setParent(this);
    w->installEventFilter(this);

    const int cached = m_heights.value(key, kDefaultRowHeight);
    w->setGeometry(kMarginLeft, m_tops[row], contentWidth(), cached);
    // Measure with the real width so wrapped content gets its true height.
    const int h = qMax(w->sizeHint().height(), 1);
    if (h != cached) {
        m_heights.insert(key, h);
        relayoutHeights();
    }
    w->setGeometry(kMarginLeft, m_tops[row], contentWidth(),
                   m_heights.value(key, kDefaultRowHeight));

    if (m_binder)
        m_binder(row, w);
    w->show();
    m_widgets.insert(key, w);
    return w;
}

void VirtualCardList::destroyWidget(const QUuid& key, QWidget* w)
{
    if (!w || m_widgets.value(key) != w)
        return;
    // Commit any in-progress edit before the row leaves the window so typed
    // text is never silently lost.
    if (QWidget* fw = QApplication::focusWidget();
        fw && (fw == w || w->isAncestorOf(fw))) {
        fw->clearFocus();
    }
    w->removeEventFilter(this);
    m_widgets.remove(key);
    w->hide();
    w->deleteLater();
}

void VirtualCardList::refreshWindow()
{
    if (!m_area)
        return;
    const int scrollY = m_area->verticalScrollBar()->value();
    const int viewH = m_area->viewport()->height();

    const int createTop = scrollY - kBufferPx;
    const int createBot = scrollY + viewH + kBufferPx;
    const int destroyTop = scrollY - kDestroyPx;
    const int destroyBot = scrollY + viewH + kDestroyPx;

    // Destroy pass first (snapshot: commits may re-enter).
    QList<std::pair<QUuid, QWidget*>> snapshot;
    snapshot.reserve(m_widgets.size());
    for (auto it = m_widgets.begin(); it != m_widgets.end(); ++it)
        snapshot.emplace_back(it.key(), it.value());
    for (const auto& [key, w] : snapshot) {
        const int row = m_keyIndex.value(key, -1);
        if (row < 0)
            continue;  // row vanished via re-entrant setRows
        const int top = m_tops[row];
        const int h = m_heights.value(key, kDefaultRowHeight);
        if (top + h < destroyTop || top > destroyBot)
            destroyWidget(key, w);
    }

    // Create pass over the current structure.
    for (int row = 0; row < static_cast<int>(m_keys.size()); ++row) {
        const QUuid key = m_keys[row];
        if (m_widgets.contains(key))
            continue;
        const int top = m_tops[row];
        const int h = m_heights.value(key, kDefaultRowHeight);
        if (top + h >= createTop && top <= createBot)
            createRow(row);
    }
    repositionRows();
}

void VirtualCardList::ensureMaterialized(const QUuid& key)
{
    createRow(m_keyIndex.value(key, -1));
    repositionRows();
}

void VirtualCardList::remeasureRow(const QUuid& key)
{
    auto* w = m_widgets.value(key, nullptr);
    if (!w)
        return;
    const int row = m_keyIndex.value(key, -1);
    if (row < 0)
        return;
    w->setGeometry(kMarginLeft, m_tops[row], contentWidth(),
                   m_heights.value(key, kDefaultRowHeight));
    const int h = qMax(w->sizeHint().height(), 1);
    if (h != m_heights.value(key, kDefaultRowHeight)) {
        m_heights.insert(key, h);
        relayoutHeights();
        repositionRows();
    }
}

void VirtualCardList::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // Width changed: refit rows and re-measure (heights may depend on width).
    const int w = contentWidth();
    bool changed = false;
    const auto snapshot = m_widgets;
    for (auto it = snapshot.begin(); it != snapshot.end(); ++it) {
        const int row = m_keyIndex.value(it.key(), -1);
        if (row < 0)
            continue;
        QWidget* widget = it.value();
        widget->setGeometry(kMarginLeft, m_tops[row], w,
                            m_heights.value(it.key(), kDefaultRowHeight));
        const int h = qMax(widget->sizeHint().height(), 1);
        if (h != m_heights.value(it.key(), kDefaultRowHeight)) {
            m_heights.insert(it.key(), h);
            changed = true;
        }
    }
    if (changed) {
        relayoutHeights();
        repositionRows();
    }
    refreshWindow();
}

bool VirtualCardList::eventFilter(QObject* obj, QEvent* event)
{
    if (m_area && obj == m_area->viewport() && event->type() == QEvent::Resize) {
        refreshWindow();
        return false;
    }
    if (event->type() == QEvent::LayoutRequest && obj->isWidgetType()) {
        // A row's internal content changed size: schedule a re-measure.
        for (auto it = m_widgets.begin(); it != m_widgets.end(); ++it) {
            if (it.value() != obj)
                continue;
            m_remeasurePending.insert(it.key());
            if (!m_remeasureQueued) {
                m_remeasureQueued = true;
                QTimer::singleShot(0, this, [this]() {
                    m_remeasureQueued = false;
                    const QSet<QUuid> pending = m_remeasurePending;
                    m_remeasurePending.clear();
                    for (const auto& key : pending)
                        remeasureRow(key);
                });
            }
            break;
        }
    }
    return QWidget::eventFilter(obj, event);
}
