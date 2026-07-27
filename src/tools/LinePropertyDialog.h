#pragma once

#include <QDialog>
#include <QUuid>

#include <optional>

#include "parametric/Attachment.h"

class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QDoubleSpinBox;
class QTabWidget;
class QDialogButtonBox;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace cad::param {
class ParamDocument;
struct Segment;
struct ParamPoint;
class Block;
}

class CanvasScene;

namespace cad::tools {

/// Modeless-style dialog for editing a segment's properties.
/// Changes apply LIVE as the user edits (no need to press OK for preview).
/// OK closes; Cancel reverts to the state before the dialog opened.
class LinePropertyDialog : public QDialog
{
    Q_OBJECT

public:
    LinePropertyDialog(const QUuid& blockId, const QUuid& segmentId,
                       cad::param::ParamDocument* paramDoc,
                       CanvasScene* scene,
                       QWidget* parent = nullptr);

    [[nodiscard]] bool confirmed() const { return m_confirmed; }

    /// Switch the dialog to edit a different segment (used by the group tree).
    void setTarget(const QUuid& blockId, const QUuid& segmentId);

private slots:
    void onLiveUpdate();   ///< Apply current widget values to model + re-resolve + refresh.
    void onLengthRefresh(); ///< Apply length only + refresh preview.
    void onLengthTextChanged(); ///< Mark refresh button as dirty (orange).
    void onDetach();       ///< Remove this block's follower attachment.
    void onAccepted();
    void onRejected();     ///< Revert to snapshot.

private:
    void buildPage1(QTabWidget* tabs);
    void buildPage2(QTabWidget* tabs);   ///< The "相关" (connections) tab.
    void populateFromModel();
    void applyToModel();
    void refreshScene();
    void connectLiveSignals();

    /// Rebuild the "相关" tab: start/end endpoint headers + left/right connection
    /// bubbles (leader/follower relationships at each endpoint).
    void refreshRelatedTab();

    QUuid m_blockId;
    QUuid m_segmentId;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    CanvasScene* m_scene = nullptr;
    bool m_confirmed = false;

    // Snapshot for cancel-revert
    struct Snapshot {
        QString segName;
        bool showName;
        bool showLength;
        QString lengthFormula;
        double distance;
        QString startName, startAnno;
        bool startShowName;
        QString endName, endAnno;
        bool endShowName;
        int lineStyle;
        double weight;
        std::optional<cad::param::Attachment> followerAtt;  ///< Attachment where this block is the follower.
    };
    Snapshot m_snapshot;

    // Page 1 widgets
    QLabel*       m_lblSegId      = nullptr;
    QLineEdit*    m_editName      = nullptr;
    QCheckBox*    m_chkShowName   = nullptr;
    QLineEdit*    m_editLength    = nullptr;
    QPushButton*  m_btnLenRefresh = nullptr;
    QCheckBox*    m_chkShowLength = nullptr;
    QLabel*       m_lblStartPtId  = nullptr;
    QLineEdit*    m_editStartName = nullptr;
    QCheckBox*    m_chkShowStartName = nullptr;
    QLineEdit*    m_editStartAnno = nullptr;
    QLabel*       m_lblEndPtId    = nullptr;
    QLineEdit*    m_editEndName   = nullptr;
    QCheckBox*    m_chkShowEndName = nullptr;
    QLineEdit*    m_editEndAnno   = nullptr;
    QComboBox*    m_cmbStyle      = nullptr;
    QDoubleSpinBox* m_spinWeight  = nullptr;

    // Page 2 ("相关") widgets
    QLabel*     m_lblStartHeader = nullptr;   ///< "起点" endpoint header (serial + name).
    QLabel*     m_lblEndHeader   = nullptr;   ///< "终点" endpoint header (serial + name).
    QWidget*    m_startBubble    = nullptr;   ///< Left bubble container (start-side connections).
    QWidget*    m_endBubble      = nullptr;   ///< Right bubble container (end-side connections).
    QVBoxLayout* m_startLayout   = nullptr;
    QVBoxLayout* m_endLayout     = nullptr;
    QDoubleSpinBox* m_spinAngle  = nullptr;   ///< Construction angle (editable when follower).
    QPushButton*  m_btnDetach    = nullptr;   ///< Detach from leader.
};

} // namespace cad::tools
