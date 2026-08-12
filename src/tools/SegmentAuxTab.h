#pragma once

#include <QWidget>
#include <QUuid>

#include <functional>
#include <vector>

class ElaTabWidget;
class QListWidget;
class QVBoxLayout;

namespace cad::param {
class ParamDocument;
class Block;
struct Segment;
}

class CanvasScene;

namespace cad::tools {

class AuxPointForm;
class IntersectionForm;

/// "辅助点" + "点连接" tabs of LinePropertyDialog: the auxiliary point list
/// with its edit forms (Interpolated / Intersection), the per-point connection
/// cards, and the 撤销全部 snapshots for aux edits made during the session.
/// Reads/writes the model directly through ParamDocument; scene refreshes and
/// the global input debounce go through the injected callbacks.
class SegmentAuxTab : public QWidget
{
    Q_OBJECT

public:
    SegmentAuxTab(cad::param::ParamDocument* doc,
                  CanvasScene* scene,
                  const std::function<void()>& sceneRefresh,
                  const std::function<void()>& debounceRestart,
                  QWidget* parent = nullptr);

    /// Build both tabs and register them with @p tabs.
    void build(ElaTabWidget* tabs);
    /// Switch the tab to a different segment.
    void setTarget(const QUuid& blockId, const QUuid& segmentId);

    /// Rebuild the "点连接" tab cards.
    void refreshConnections();
    /// Rebuild the "辅助点" list.
    void refreshList();
    /// Fill the correct edit form (aux or intersection) for the selection.
    void populateFields();
    /// Refresh the endpoint labels shown in the aux form.
    void refreshDirLabels();
    /// Snapshot the current aux points for 撤销全部 revert (dialog-open time).
    void saveSnapshots(const cad::param::Segment* seg);
    /// Revert aux points: remove session-added ones, restore snapshots.
    void restoreSnapshots();

public slots:
    void onSelectionChanged();
    void onAdd();
    void onRemove();
    void onLiveUpdate();

signals:
    /// Double-click on a connection card: jump the dialog to that segment.
    void jumpRequested(const QUuid& blockId, const QUuid& segmentId);

private:
    /// Snapshot for aux points (revert support).
    struct AuxSnapshot {
        QUuid pointId;
        double percent;
        QString percentFormula;
        double constant;
        QString constantFormula;
        bool fromEnd;
        bool showName;
        QString name;
    };

    cad::param::ParamDocument* m_paramDoc = nullptr;
    CanvasScene* m_scene = nullptr;
    std::function<void()> m_sceneRefresh;
    std::function<void()> m_debounceRestart;
    QUuid m_blockId;
    QUuid m_segmentId;

    QListWidget*       m_auxList = nullptr;      ///< List of aux + intersection points.
    AuxPointForm*      m_auxForm = nullptr;      ///< Form for Interpolated points.
    IntersectionForm*  m_ixForm = nullptr;       ///< Form for Intersection points.
    QUuid              m_currentAuxId;           ///< Currently selected point (stable across refreshes).
    QVBoxLayout*       m_auxConnLayout = nullptr; ///< Container for aux connection cards.

    std::vector<AuxSnapshot> m_auxSnapshots;     ///< Initial state of aux points for revert.
    std::vector<QUuid> m_auxAddedIds;            ///< Aux points added during this session (remove on revert).
};

} // namespace cad::tools
