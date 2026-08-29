#pragma once

#include <QWidget>
#include <QUuid>

#include <functional>
#include <vector>

class ElaTabWidget;
class QListWidget;
class ElaComboBox;
class ElaDoubleSpinBox;
class ElaCheckBox;
class ElaText;
class ElaPushButton;

namespace cad::param {
class ParamDocument;
class Block;
struct Segment;
}

namespace cad::ui {

/// "锚点" tab of LinePropertyDialog: the anchor list (start + curve pass
/// points + end) with manual Bézier tangent editing, auto/C2 tangent mode,
/// smooth/corner lock, and curve-point follow connections.
/// Reads/writes the model directly through ParamDocument; scene refreshes
/// go through the injected callback.
class SegmentAnchorTab : public QWidget
{
    Q_OBJECT

public:
    SegmentAnchorTab(cad::param::ParamDocument* doc,
                     const std::function<void()>& sceneRefresh,
                     QWidget* parent = nullptr);

    /// Build the tab content and register it with @p tabs.
    void build(ElaTabWidget* tabs);
    /// Switch the tab to a different segment (callers follow up with
    /// populateList()/refreshFields()).
    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    /// Rebuild the anchor list from the segment (起点 + 曲线点 + 终点).
    void populateList(const cad::param::Block* block, const cad::param::Segment* seg);
    /// Refresh the edit fields for the given list row.
    void refreshFields(int row);

private:
    cad::param::ParamDocument* m_paramDoc = nullptr;
    std::function<void()> m_sceneRefresh;
    QUuid m_blockId;
    QUuid m_segmentId;

    QListWidget*    m_anchorList = nullptr;
    ElaComboBox*      m_cmbTanMode = nullptr;
    ElaDoubleSpinBox* m_spinTanAngleIn = nullptr;
    ElaDoubleSpinBox* m_spinTanLenIn = nullptr;
    ElaDoubleSpinBox* m_spinTanAngleOut = nullptr;
    ElaDoubleSpinBox* m_spinTanLenOut = nullptr;
    ElaCheckBox*      m_chkTanLocked = nullptr;
    ElaText*        m_lblFollowInfo = nullptr;
    ElaPushButton*    m_btnReleaseFollow = nullptr;
    ElaPushButton*    m_btnResetTan = nullptr;
    ElaText*        m_lblTanInfo = nullptr;
    /// Ordered point IDs matching list rows.
    std::vector<QUuid> m_anchorPointIds;
};

} // namespace cad::ui
