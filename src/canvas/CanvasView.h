#pragma once

#include <QGraphicsView>
#include <QPoint>

class CanvasScene;

namespace cad::param { class ParamDocument; }
namespace cad::tools { class ToolManager; }

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

    void setToolManager(cad::tools::ToolManager* tm) { m_toolManager = tm; }
    void setParamDoc(cad::param::ParamDocument* doc) { m_paramDoc = doc; }
    [[nodiscard]] double zoomFactor() const;

signals:
    void mouseScenePosChanged(qreal x, qreal y);
    void zoomFactorChanged(double factor);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;
    /// Keep Tab as a tool key (leader-candidate cycling) instead of letting
    /// the focus system consume it before keyPressEvent is reached.
    bool focusNextPrevChild(bool next) override;

private:
    void emitZoomChanged();
    void ensureSceneRect();
    /// Publish the length of the segment under @p scenePos as a linked variable.
    void publishLengthAt(const QPointF& scenePos);
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

    cad::tools::ToolManager* m_toolManager = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;

    static constexpr double ZOOM_FACTOR_STEP = 1.12;
    static constexpr double ZOOM_MIN = 0.2;    // 20% — 总览整件衣服
    static constexpr double ZOOM_MAX = 10.0;   // 1000% — 细节足够

    static constexpr double SCENE_BOUND = 10000.0; // max scene extent ±10m from origin
};
