#pragma once

#include <QWidget>
#include <QUuid>

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

signals:
    /// Flash the measured points precisely (MeasureCard source click).
    void highlightMeasureRequested(const QUuid& measureId);
    /// Highlight the source block on canvas (angle card source click).
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
};

} // namespace cad::ui
