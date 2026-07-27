#pragma once

#include <QWidget>
#include <QUuid>

namespace cad::param {
class ParamDocument;
struct GroupNode;
}

class CanvasScene;
class QTreeWidget;
class QTreeWidgetItem;

/// Panel page managing attachment groups ("大组"), hosted inside SidePanel.
///
/// Each group is shown as a top-level row (readable G-serial + editable group
/// name + member count); its member segments are nested below as a leader→
/// follower tree (readable L-serial + editable segment name).
///   • Click a segment row  → select it on canvas (turns red).
///   • Double-click ID cell → open the segment property dialog.
///   • Edit the name cell   → renames the group / segment (synced everywhere).
///   • Delete key on a row  → kick the segment out of its group (keeps geometry).
class GroupPanel : public QWidget
{
    Q_OBJECT

public:
    GroupPanel(cad::param::ParamDocument* paramDoc, CanvasScene* scene,
               QWidget* parent = nullptr);

public slots:
    /// Rebuild the tree from the document's group registry.
    void refresh();

protected:
    /// Captures the Delete key to kick a segment out of its group.
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onItemChanged(QTreeWidgetItem* item, int column);

private:
    /// Item data roles (SerialRole lives in SerialDelegate.h at UserRole+100).
    enum Role {
        RoleGroupId   = Qt::UserRole,      ///< Group id (group rows).
        RoleBlockId   = Qt::UserRole + 1,  ///< Block id (member rows).
        RoleSegmentId = Qt::UserRole + 2,  ///< Segment id (member rows).
        RoleIsGroup   = Qt::UserRole + 3,  ///< bool: true for group rows.
    };

    void setupUi();
    void addMemberNode(const cad::param::GroupNode& node, QTreeWidgetItem* parent);
    void kickOutSelected();

    cad::param::ParamDocument* m_paramDoc = nullptr;
    CanvasScene* m_scene = nullptr;
    QTreeWidget* m_tree = nullptr;
    bool m_refreshing = false;  ///< Guards against itemChanged during rebuild.
};
