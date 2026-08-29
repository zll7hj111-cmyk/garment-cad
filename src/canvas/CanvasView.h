#pragma once

#include <QGraphicsView>
#include <QPoint>
#include <QPointF>
#include <QUuid>

#include <functional>

#include "canvas/InputDispatcher.h"

class CanvasScene;

namespace cad::param { class ParamDocument; }

/// Custom QGraphicsView implementing:
/// - Mouse-wheel zoom anchored at cursor position
/// - Middle-mouse-button drag to pan
/// - Tool event dispatch (coordinates converted to user space: +Y up)
///
/// The scene uses Qt-native coordinates (+Y down).  User coordinates (+Y up)
/// are obtained by negating Y.  This view performs that conversion at every
/// outbound boundary (signals and tool dispatch) so that downstream code never
/// sees scene coordinates.
class CanvasView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit CanvasView(CanvasScene* scene, QWidget* parent = nullptr);
    ~CanvasView() override;

    /// Input sink for tool dispatch. Takes the canvas-layer INTERFACE (not
    /// cad::tools::ToolManager) so the canvas never depends on the tools layer
    /// — see InputDispatcher.h (P1-6). ToolManager implements the interface, so
    /// existing `setInputDispatcher(toolManager)` calls keep working.
    void setInputDispatcher(cad::canvas::InputDispatcher* d) { m_inputDispatcher = d; }
    void setParamDoc(cad::param::ParamDocument* doc) { m_paramDoc = doc; }

    /// Optional predicate installed by the APP layer: return true to suppress
    /// the canvas context menu entirely (e.g. the rotate tool owns a pending
    /// target, where right-click means "confirm/back out" instead of a menu).
    /// The canvas does not know about tools, so the app supplies the policy.
    using ContextMenuGuard = std::function<bool()>;
    void setContextMenuGuard(ContextMenuGuard guard)
    { m_contextMenuGuard = std::move(guard); }

    [[nodiscard]] double zoomFactor() const;
    /// Apply the pattern-paper ground color. Called by CanvasScene::setStyle
    /// (the single authoritative theme-switch path) and the constructor.
    void applyCanvasBackground(const QColor& c);

signals:
    void mouseScenePosChanged(qreal x, qreal y);
    void zoomFactorChanged(double factor);
    /// Right-click on a segment: the app layer owns the menu, the dialogs and
    /// the commands (P1-6 — the canvas reports the hit, it does not act on it).
    void segmentContextMenuRequested(const cad::canvas::SegmentHit& hit);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    /// Release hold-to-show overrides (N/L) when focus leaves the canvas —
    /// the key-up would otherwise be lost while the user interacts elsewhere.
    void focusOutEvent(QFocusEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;
    /// Keep Tab as a tool key (leader-candidate cycling) instead of letting
    /// the focus system consume it before keyPressEvent is reached.
    bool focusNextPrevChild(bool next) override;

private:
    void emitZoomChanged();
    void ensureSceneRect();
    /// Probe the OpenGL viewport after it is exposed: software GL (VMs, RDP,
    /// driverless machines) makes every repaint cost tens of milliseconds —
    /// far slower than Qt's native rasterizer. Detect it and fall back to a
    /// plain QWidget viewport (GCAD_NO_GL=1 forces the fallback).
    void probeGlAndMaybeFallback();
    void fallbackToSoftware();

    bool   m_panning = false;
    bool   m_glProbed = false;  ///< GL probe ran (once, after first show).
    bool   m_useGl = false;     ///< OpenGL viewport requested (GCAD_ENABLE_GL=1).
    QPoint m_lastMousePos;

    CanvasScene* m_scene = nullptr;
    cad::canvas::InputDispatcher* m_inputDispatcher = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    ContextMenuGuard m_contextMenuGuard;

    static constexpr double ZOOM_FACTOR_STEP = 1.12;
    static constexpr double ZOOM_MIN = 0.2;    // 20% — 总览整件衣服
    static constexpr double ZOOM_MAX = 10.0;   // 1000% — 细节足够

    static constexpr double SCENE_BOUND = 10000.0; // max scene extent ±10m from origin
};
