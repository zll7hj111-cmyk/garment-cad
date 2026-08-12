#pragma once

#include <QWidget>
#include <QUuid>
#include <QList>
#include <QSet>

class ElaScrollArea;
class QVBoxLayout;
class ElaText;
class ElaPushButton;
class QUndoStack;

namespace cad::param { class ParamDocument; }

/// Panel page managing user groups (成组), hosted inside SidePanel (组 tab).
///
/// Card-based design (matches the LayerPanel visual language):
///   - Each group card: [batch checkbox] [expand caret] [serial tag]
///     [name] [member count pill]; expanded cards preview member rows.
///   - Click card body → select the WHOLE group on canvas.
///   - Drag a card → reorder the group list (persisted via groups order).
///   - Right-click card → rename / dissolve (解散组).
///   - Batch toolbar: 解散选中 dissolves every checked group (one undo macro).
///   - Keyboard on a focused card: Enter = select group, Delete = dissolve.
///
/// The panel is a viewer/operator only — protection guards live in the
/// tools; undo goes through UngroupCommand.
class GroupPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GroupPanel(cad::param::ParamDocument* doc, QWidget* parent = nullptr);

    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }

signals:
    /// Card body clicked / Enter: the host window should activate the
    /// selection tool and select+confirm exactly these blocks.
    void selectGroupRequested(const QList<QUuid>& blockIds);

public slots:
    /// Rebuild all group cards from the document's group registry.
    void refresh();

protected:
    /// Intercept card clicks / drag-drop on the container / card keys.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUi();
    void showGroupMenu(const QPoint& globalPos, const QUuid& groupId);
    void renameGroup(const QUuid& groupId);
    void dissolveGroup(const QUuid& groupId);
    void dissolveCheckedGroups();
    void updateBatchButton();
    /// The card whose geometry contains @p pos (container coords), or null.
    [[nodiscard]] QWidget* cardAt(const QPoint& pos) const;

    cad::param::ParamDocument* m_doc = nullptr;
    QUndoStack* m_undoStack = nullptr;

    ElaScrollArea* m_scroll      = nullptr;
    QWidget*     m_container   = nullptr;
    QVBoxLayout* m_listLayout  = nullptr;
    ElaText*     m_countLabel  = nullptr;   ///< Header pill: total group count.
    ElaText*     m_emptyHint   = nullptr;   ///< Shown when no groups exist.
    ElaPushButton* m_batchBtn    = nullptr;   ///< 解散选中 (batch dissolve).
    QList<QWidget*> m_cards;                ///< Cards in layout order.
    QSet<QUuid>  m_checked;                 ///< Groups selected for batch ops.
    QWidget*     m_dropTarget  = nullptr;   ///< Card under the drag cursor.
};
