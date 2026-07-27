#pragma once

#include <QGraphicsView>
#include <QPoint>

class CanvasScene;

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

private:
    void emitZoomChanged();
    void ensureSceneRect();

    bool   m_panning = false;
    QPoint m_lastMousePos;

    cad::tools::ToolManager* m_toolManager = nullptr;

    static constexpr double ZOOM_FACTOR_STEP = 1.12;
    static constexpr double ZOOM_MIN = 0.2;    // 20% — 总览整件衣服
    static constexpr double ZOOM_MAX = 10.0;   // 1000% — 细节足够

    static constexpr double SCENE_BOUND = 10000.0; // max scene extent ±10m from origin
};
