#pragma once

#include <QWidget>
#include <QUuid>
#include <QVector>
#include <QString>

class ElaScrollArea;
class ElaText;
class QUndoStack;
class VirtualCardList;

namespace cad::param {
class ParamDocument;
struct MeasureVariable;
struct AngleMeasureVariable;
}

namespace cad::ui {

/// "测量" page of the variable panel: length + angle measure cards in one
/// virtualized list (auto-generated measurements — no add button). Syncs
/// from the document signals via sync(); source-clicks are re-emitted for
/// the canvas highlight wiring.
class MeasureTab : public QWidget
{
    Q_OBJECT

public:
    explicit MeasureTab(cad::param::ParamDocument* doc, QWidget* parent = nullptr);

    void setUndoStack(QUndoStack* stack);

    /// Smart sync: rebuild the row structure only when keys/order changed,
    /// otherwise rebind materialized cards in-place (preserves focus & scroll).
    /// Also updates the empty-hint visibility.
    void sync();

    /// Called by the panel when measure/angle/layer data changed: marks the
    /// metadata cache dirty so the next sync() performs a full metadata
    /// comparison instead of the value-only resolved fast path.
    void notifyMeasureDataChanged();

signals:
    /// Flash the measured points precisely (MeasureCard source click/hover).
    void highlightMeasureRequested(const QUuid& measureId);
    /// Flash the angle measurement's two segments and half-arc (angle card
    /// source click/hover).
    void highlightAngleMeasureRequested(const QUuid& angleMeasureId);
    /// Highlight a source block on canvas (linked cards / fallback paths).
    void highlightBlockRequested(const QUuid& blockId);

private:
    void onMeasureDeleted(const QUuid& id);
    void onMeasureEdited(const cad::param::MeasureVariable& mv);
    void onAngleMeasureDeleted(const QUuid& id);
    void onAngleMeasureEdited(const cad::param::AngleMeasureVariable& am);

    cad::param::ParamDocument* m_doc = nullptr;
    QUndoStack* m_undoStack = nullptr;

    ElaScrollArea* m_scroll = nullptr;
    QWidget*     m_container = nullptr;
    VirtualCardList* m_host = nullptr;
    ElaText*     m_emptyHint = nullptr;

    /// Cached metadata signature: lets sync() detect name/comment/ref/source
    /// changes without forcing a full card rebind on every value-only resolve.
    QString m_lastMetaSignature;
    QVector<QUuid> m_lastKeys;
    bool m_metaDirty = true;   ///< Set by notifyMeasureDataChanged; first sync evaluates.
};

} // namespace cad::ui
