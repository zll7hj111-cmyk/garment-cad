#pragma once

#include "Tool.h"
#include "SnapEngine.h"
#include "geometry/Vec2.h"

#include <QGraphicsItem>
#include <QPainter>
#include <optional>

class QGraphicsLineItem;
class QGraphicsEllipseItem;
class QGraphicsRectItem;
class QGraphicsView;

namespace cad::param { class ParamDocument; }

namespace cad::tools {

/// Screen-space HUD label that follows the cursor during line drawing.
class HudItem : public QGraphicsItem
{
public:
    explicit HudItem(QGraphicsItem* parent = nullptr);

    void setText(const QString& text);
    void moveToPoint(const cad::geo::Vec2& userPos, const QGraphicsView* view);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

private:
    QString m_text;
    QRectF  m_rect;
};

/// Smart Pen tool — parametric line creation with snapping and property dialog.
///
/// Flow B interaction:
///   Click → set start (auto-snap) → move → preview + HUD → click → set end (auto-snap)
///   → property dialog pops up → confirm → line created → back to Idle.
///
/// Right-click / Esc cancels.  Hold Shift for 45° angle constraint.
class ToolSmartPen : public Tool
{
public:
    void activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void deactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void keyPress(QKeyEvent* event) override;
    void keyRelease(QKeyEvent* event) override;

    [[nodiscard]] const char* name() const override { return "\xe6\x99\xba\xe8\x83\xbd\xe7\xac\x94"; }

private:
    enum class State { Idle, Drawing };

    void commitLine(const cad::geo::Vec2& end);
    void cancelLine();
    void clearPreview();

    [[nodiscard]] cad::geo::Vec2 applyAngleSnap(const cad::geo::Vec2& raw) const;
    void updatePreview(const cad::geo::Vec2& effectiveEnd);
    void updateSnapIndicator(const cad::geo::Vec2& worldPos);

    /// Create a parametric Block for a free line (no snap).
    void createFreeLine(const cad::geo::Vec2& start, const cad::geo::Vec2& end);

    /// Create a parametric Block attached to an existing block (snapped start).
    void createAttachedLine(const SnapResult& snapStart, const cad::geo::Vec2& end);

    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    State m_state = State::Idle;

    cad::geo::Vec2 m_startPoint;
    bool m_angleSnap = false;
    mutable double m_snapAngleDeg = 0.0;  ///< Construction angle (relative to leader) for HUD.
    double m_refDirDeg = 0.0;             ///< Leader segment world direction (deg) at snapped start; 0 when free.

    // Snap state
    SnapEngine m_snapEngine;
    std::optional<SnapResult> m_startSnap;  ///< If start was snapped to existing point.
    std::optional<SnapResult> m_currentSnap; ///< Current hover snap (for indicator).

    // Preview graphics
    QGraphicsLineItem*    m_previewLine  = nullptr;
    QGraphicsEllipseItem* m_startMarker  = nullptr;
    QGraphicsRectItem*    m_snapIndicator = nullptr;
    HudItem*              m_hud          = nullptr;
};

} // namespace cad::tools
