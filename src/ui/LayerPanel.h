#pragma once

#include <QWidget>
#include <QUuid>
#include <QSet>

class ElaScrollArea;
class QVBoxLayout;
class ElaText;
class QUndoStack;
class QFrame;
class QToolButton;

namespace cad::param { class ParamDocument; }

/// Panel page managing canvas layers, hosted inside SidePanel (图层 tab).
///
/// Card-based design (matches the VariablePanel visual language):
///   - Each layer is a rounded card; the ACTIVE layer has a blue accent bar
///     and light-blue header tint.
///   - Card header: [collapse ▾] [eye toggle] [name] [count pill].
///     Click header → make active. Double-click name → inline rename.
///   - Segment rows nested inside: [eye toggle] [name / —] [serial tag badge].
///   - Right-click layer card → delete layer.
///   - Right-click segment row → move to layer / delete segment.
///
/// Layers are a pure selection/visibility filter: hiding a layer never breaks
/// its variables, measurements, or attachments — they keep solving normally.
class LayerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayerPanel(cad::param::ParamDocument* doc, QWidget* parent = nullptr);

    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }

    /// Rebuild theme-token driven styles after a theme change (light/dark).
    void applyTheme();

public slots:
    /// Rebuild all layer cards from the document's layer registry.
    /// Structural changes only (layers added/removed, blocks added/removed,
    /// active layer switched).
    void refresh();
    /// In-place sync for plain document changes (renames, visibility, value
    /// edits): updates card/row texts without rebuilding widgets. Falls back
    /// to refresh() when the structure actually changed.
    void syncFromDoc();

protected:
    /// Intercept clicks on layer name labels (activate / rename).
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onAddLayerClicked();

private:
    void setupUi();
    void deleteLayer(int index);
    void deleteBlock(const QUuid& blockId);
    void setSegmentVisible(const QUuid& blockId, bool visible);
    void moveBlockToLayer(const QUuid& blockId, int targetLayer);

    /// Context menus (position in global screen coordinates).
    void showLayerMenu(const QPoint& globalPos, int layerIndex);
    void showSegmentMenu(const QPoint& globalPos, const QUuid& blockId);

    /// Begin inline rename of a layer name label.
    void startRename(int layerIndex, ElaText* nameLabel);

    cad::param::ParamDocument* m_doc = nullptr;
    QUndoStack* m_undoStack = nullptr;

    ElaScrollArea* m_scroll    = nullptr;
    QWidget*     m_container = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    ElaText*     m_countLabel = nullptr;   ///< Header pill: total layer count.
    ElaText*     m_emptyHint  = nullptr;   ///< Shown when no blocks exist at all.
    QWidget*     m_header     = nullptr;   ///< Header bar (styled from tokens).
    QFrame*      m_sep        = nullptr;   ///< Header separator line.
    QToolButton* m_addBtn     = nullptr;   ///< "new layer" header button.
    QSet<QUuid>  m_collapsed;              ///< Layer ids whose card is collapsed
                                           ///< (persists across refreshes).
    /// Cards in layout order (index = layer index). The concrete type
    /// LayerCard lives in LayerPanel.cpp's anonymous namespace — stored as
    /// QWidget* here and cast at the use site.
    QList<QWidget*> m_cards;
};
