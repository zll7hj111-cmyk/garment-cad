#pragma once

#include "CanvasStyle.h"

#include <QObject>
#include <QHash>
#include <QUuid>
#include <QTimer>

class QGraphicsItem;

/// Drives smooth state-transition animations for canvas entities.
///
/// Each entity (identified by owner item + entity UUID) has an independent
/// animation progress. A single shared QTimer ticks at ~60 fps while any
/// animation is active; it stops when all entities reach their target state.
class CanvasAnimator : public QObject
{
    Q_OBJECT

public:
    explicit CanvasAnimator(const CanvasStyle* style, QObject* parent = nullptr);

    /// Set the target state for an entity (starts a transition animation).
    void setState(QGraphicsItem* owner, const QUuid& entityId, EntityState target);

    /// Query the current-frame line paint params (interpolated during animation).
    [[nodiscard]] EntityPaintParams lineParams(QGraphicsItem* owner, const QUuid& entityId,
                                               const QColor& baseColor, double baseWidth) const;

    /// Query the current-frame point paint params (interpolated during animation).
    [[nodiscard]] EntityPaintParams pointParams(QGraphicsItem* owner, const QUuid& entityId,
                                                bool auxiliary) const;

    /// Whether an entity is currently mid-animation.
    [[nodiscard]] bool isAnimating(QGraphicsItem* owner, const QUuid& entityId) const;

    /// Remove all animation state for an owner (call when BlockItem is destroyed).
    void removeOwner(QGraphicsItem* owner);

signals:
    /// Emitted each tick for owners that need a repaint.
    void invalidationRequested(QGraphicsItem* owner);

private:
    void tick();
    void ensureTimerRunning();
    void stopTimerIfIdle();

    struct Entry {
        EntityState from     = EntityState::Normal;
        EntityState to       = EntityState::Normal;
        double      progress = 1.0;  // 1.0 = at rest (reached target)
    };

    // owner → (entityId → Entry)
    QHash<QGraphicsItem*, QHash<QUuid, Entry>> m_entries;
    const CanvasStyle* m_style = nullptr;
    QTimer* m_timer = nullptr;
};
