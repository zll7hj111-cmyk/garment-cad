#pragma once

#include <QWidget>
#include <QUuid>
#include <QString>

class ElaLineEdit;
class ElaText;
class QPushButton;
class QButtonGroup;
class QComboBox;
class QTimer;

namespace cad::param {
class ParamDocument;
class Block;
struct Segment;
struct MeasureVariable;
}

class CanvasScene;

namespace cad::ui {

/// Geometry section for LinePropertyDialog:
/// - Length mode (Auto / Specified)
/// - Length value & formula input, debounce, paste button
/// - Publish length variable button
/// - Actual resolved length display
/// - Slide row (drawer-style sliding along/perp to base line)
/// - Curve rows (arc length, tension, convert to line)
class LineGeometrySection : public QWidget
{
    Q_OBJECT

public:
    explicit LineGeometrySection(cad::param::ParamDocument* paramDoc,
                                CanvasScene* scene,
                                QWidget* parent = nullptr);
    ~LineGeometrySection() override = default;

    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    void populateFromModel(const cad::param::Block& block, const cad::param::Segment& seg);
    void applyToModel(cad::param::Block* block, cad::param::Segment* seg);

    void refreshActualLengthLabel();
    void refreshLengthMode();
    void refreshSlideRow();
    void applyBridgeReadOnly();

    [[nodiscard]] const cad::param::MeasureVariable* findBridgeMeasure() const;

    [[nodiscard]] ElaLineEdit* editLength() const { return m_editLength; }
    [[nodiscard]] QPushButton* btnLengthAuto() const { return m_btnLenAuto; }
    [[nodiscard]] QPushButton* btnLengthSpec() const { return m_btnLenSpec; }
    [[nodiscard]] ElaText* lblActualLength() const { return m_lblActualLength; }

signals:
    void liveUpdated();
    void lengthApplied();
    void sceneRefreshRequested();

private slots:
    void onLengthDirty();
    void onLengthApply();
    void onLengthModeChanged(bool autoMode);
    void onSlideModeChanged(int index);
    void onSlideOffsetEdited();
    void onPublishLength();
    void onConvertToLine();
    void onDebounceTimeout();

private:
    cad::param::ParamDocument* m_paramDoc = nullptr;
    CanvasScene* m_scene = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    ElaLineEdit*   m_editLength      = nullptr;
    ElaText*       m_lblFx           = nullptr;
    QPushButton*   m_btnLenAuto      = nullptr;
    QPushButton*   m_btnLenSpec      = nullptr;
    QButtonGroup*  m_lenGroup        = nullptr;
    ElaText*       m_lblActualLength = nullptr;
    QPushButton*   m_btnPublishLen   = nullptr;

    // Slide row
    QWidget*       m_slideRow        = nullptr;
    ElaText*       m_lblSlideBadge   = nullptr;
    QComboBox*     m_cmbSlideMode    = nullptr;
    ElaLineEdit*   m_editSlideAlong  = nullptr;
    ElaLineEdit*   m_editSlidePerp   = nullptr;

    // Curve demand rows
    QWidget*       m_arcRow          = nullptr;
    ElaText*       m_lblArcLength    = nullptr;
    QWidget*       m_tensionRow      = nullptr;
    ElaLineEdit*   m_editTension     = nullptr;
    QPushButton*   m_btnConvert      = nullptr;

    QTimer*        m_debounce        = nullptr;
};

} // namespace cad::ui
