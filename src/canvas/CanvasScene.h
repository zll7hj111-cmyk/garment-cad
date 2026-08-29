#pragma once

#include <QGraphicsScene>
#include <QHash>
#include <QSet>
#include <QUuid>

#include "CanvasStyle.h"
#include "CanvasAnimator.h"
#include "parametric/MeasureVariable.h"

class QGraphicsRectItem;
class QTimer;

namespace cad::param { class ParamDocument; }

class BlockItem;

/// Screen-constant HUD label (toast / 重叠提示共用, TOOL_SYSTEM_AUDIT P1).
class HudItem;

/// The scene that holds all entity items and the origin crosshair.
/// Owns the CanvasStyle (design tokens) and CanvasAnimator (transition engine).
class CanvasScene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit CanvasScene(cad::param::ParamDocument* paramDoc, QObject* parent = nullptr);
    ~CanvasScene() override;

    [[nodiscard]] cad::param::ParamDocument* paramDocument() const { return m_paramDoc; }

    /// Access the style token table.
    [[nodiscard]] const CanvasStyle* style() const { return &m_style; }

    /// Access the animation engine.
    [[nodiscard]] CanvasAnimator* animator() { return &m_animator; }

    /// Replace the active theme (triggers full scene repaint).
    void setStyle(const CanvasStyle& s);

    /// Current view zoom (scale factor of the first view's transform).
    /// Returns 1.0 when there are no views — safe for headless tests.
    [[nodiscard]] double currentZoom() const;

    /// Create and add a BlockItem for the given block ID.
    void addBlockItem(const QUuid& blockId);

    /// Remove the BlockItem for the given block ID.
    void removeBlockItem(const QUuid& blockId);

    /// Remove every BlockItem (used on document reset / load).
    void clearAllBlockItems();

    /// Refresh all BlockItems (call after resolve).
    void refreshAllBlockItems();

    /// Lightweight position sync for all BlockItems: items whose block only
    /// translated are moved via setPos() (O(1) each — no cache rebuild, no
    /// string formatting, no allocation). Items whose block rotated fall back
    /// to a full refresh. Call after resolveAll() or direct transform edits.
    void syncBlockPositions();

    /// Targeted sync for a subset of blocks (drag hot-path: only the dragged
    /// blocks are touched — the rest of the scene is not even iterated).
    void syncBlockPositions(const QList<QUuid>& blockIds);

    /// Select exactly the given block on canvas (clears previous selection).
    void selectBlock(const QUuid& blockId);

    /// Find the BlockItem for a block (nullptr if none).
    [[nodiscard]] BlockItem* findBlockItem(const QUuid& blockId) const;

    /// Transient hold-to-show overrides (N/L keys held on canvas). Purely
    /// visual — the model is untouched, no cache rebuild, no epoch bump.
    /// Painters overlay every name/length label while set; release restores
    /// the model's own show flags (snapshot semantics).
    [[nodiscard]] bool forceShowName() const { return m_forceShowName; }
    [[nodiscard]] bool forceShowLength() const { return m_forceShowLength; }
    void setForceShowName(bool on);
    void setForceShowLength(bool on);

    /// Transient toast pill anchored at the top-center of the first view
    /// (工具守卫提示, auto-hides after ~1.4 s). Shared by every tool.
    void showToast(const QString& text);

    /// 常驻模式角标 (W 键模式指示 L2): 钉在视口**左上角**的深色小胶囊,
    /// 只显示非默认模式的短名 ("多选" / "水平" / "省道线")。
    ///
    /// 与 toast 的分工: toast 讲"刚刚变成什么了" (1.4s 后消失), 角标讲
    /// "下一次点击的语义" (常驻, 且在视线里 —— 状态栏还要低头看)。
    /// 传空串 = 撤下 (默认态 / 切工具), 默认态因此零像素成本。
    void setModeBadge(const QString& text);
    /// 当前角标文本 (空 = 未显示)。供同值短路与测试断言。
    [[nodiscard]] QString modeBadgeText() const;
    /// 角标当前所在的**场景坐标**。它是"钉在视口左上角"的, 所以视口一滚动
    /// 这个值就该变 —— 断言它不变就是重定位漏了。
    [[nodiscard]] QPointF modeBadgeScenePos() const;

    /// Flash the two source points of a measurement: amber rings on both
    /// points plus an amber dashed connector (ToolMeasure preview style).
    /// The transient graphics self-destruct after ~1.5 s. Returns false when
    /// either block/point is missing or unresolved (caller falls back to a
    /// whole-block highlight).
    bool flashMeasure(const QUuid& blockA, const QUuid& pointA,
                       const QUuid& blockB, const QUuid& pointB,
                       cad::param::MeasureKind kind = cad::param::MeasureKind::Distance);

    /// Flash an angle measurement: its two source segments plus a half arc at
    /// the line intersection. The transient graphics self-destruct after
    /// ~1.5 s. Returns false when either segment is missing/unresolved.
    bool flashAngleMeasure(const QUuid& blockA, const QUuid& segmentA,
                           const QUuid& blockB, const QUuid& segmentB);
    /// Notify listeners (MainWindow) that a segment was just created by the
    /// smart pen. The host shows the status-bar edit strip (SegmentEditBar)
    /// for immediate naming/length/angle edits — no creation dialog.
    void notifyLineCreated(const QUuid& blockId, const QUuid& segmentId);
    /// Notify listeners of live length/angle readouts while a stroke is being
    /// drawn (creation-in-progress, read-only preview in the status bar).
    void notifyLinePreview(double lenCm, double angleDeg);

signals:
    void sceneMouseMoved(qreal x, qreal y);
    /// A segment was just created (smart pen commit). blockId/segmentId
    /// identify the NEW line; the host shows the status-bar edit strip.
    void lineCreated(const QUuid& blockId, const QUuid& segmentId);
    /// Live readout while a stroke is in progress (creation preview).
    void linePreviewChanged(double lenCm, double angleDeg);
    /// Hold-to-show overrides changed (N/L keys). Host forwards to open
    /// LinePropertyDialogs so their display toggles track the held state.
    void forceShowChanged(bool showNames, bool showLengths);

protected:
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;

private:
    /// Rebuild the component bounding-box overlays (add/remove/reposition).
    void refreshComponentBoxes();

    cad::param::ParamDocument* m_paramDoc = nullptr;
    QHash<QUuid, BlockItem*> m_blockItems;
    QHash<QUuid, QGraphicsRectItem*> m_componentBoxes;  ///< componentId -> bbox overlay.
    /// Component bounding-box geometry caching (2026-09 perf): resolved triggers
    /// refreshComponentBoxes; the old implementation recomputed every visible
    /// component's boundingBoxOf(O(member points)). This signature (member epoch +
    /// origin + rotation + zoom) hashes unchanged results and skips rebuild.
    QHash<QUuid, quint64> m_componentBoxSig;

    CanvasStyle m_style = CanvasStyle::lightTheme();
    CanvasAnimator m_animator;

    // Toast overlay (owned by the scene, lazily created).
    HudItem* m_toastItem = nullptr;
    QTimer* m_toastTimer = nullptr;

    /// 常驻模式角标 (owned by the scene, lazily created) —— 见 setModeBadge。
    HudItem* m_modeBadge = nullptr;
    /// 角标重定位 + 视口信号接线 (滚动/缩放会让"视口左上角"对应的场景坐标
    /// 变化, 常驻图元必须跟着走)。
    void repositionModeBadge();
    void connectModeBadgeViewSignals();

    // Hold-to-show overrides (N/L keys): transient view state, never written
    // to the model.
    bool m_forceShowName = false;
    bool m_forceShowLength = false;
};
