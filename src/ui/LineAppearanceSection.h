#pragma once

#include <QWidget>
#include <QUuid>
#include <QColor>

class QPushButton;
class QButtonGroup;
class ElaDoubleSpinBox;
class ElaText;

namespace cad::param {
class ParamDocument;
class Block;
struct Segment;
}

namespace cad::ui {

/// Appearance section for LinePropertyDialog (line style, weight, color, show toggles).
class LineAppearanceSection : public QWidget
{
    Q_OBJECT

public:
    explicit LineAppearanceSection(cad::param::ParamDocument* paramDoc,
                                   QWidget* parent = nullptr);
    ~LineAppearanceSection() override = default;

    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    void populateFromModel(const cad::param::Block& block, const cad::param::Segment& seg);
    void applyToModel(cad::param::Block* block, cad::param::Segment* seg);

    void applyHoldOverride(bool forceName, bool forceLength);
    void updateWeightControls();

    [[nodiscard]] QPushButton* chkShowName() const { return m_chkShowName; }
    [[nodiscard]] QPushButton* chkShowLength() const { return m_chkShowLength; }
    [[nodiscard]] QPushButton* chkVisible() const { return m_chkVisible; }
    [[nodiscard]] QColor currentColor() const { return m_currentColor; }

signals:
    void liveUpdated();

private slots:
    void onColorPick();

private:
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    QPushButton*      m_styleBtns[3]  = {nullptr, nullptr, nullptr};
    QButtonGroup*     m_styleGroup    = nullptr;
    QPushButton*      m_weightBtns[3] = {nullptr, nullptr, nullptr};
    QButtonGroup*     m_weightGroup   = nullptr;
    ElaDoubleSpinBox* m_spinWeight    = nullptr;
    QPushButton*      m_btnColor      = nullptr;
    ElaText*          m_lblColorHex   = nullptr;
    QPushButton*      m_chkVisible    = nullptr;
    QPushButton*      m_chkShowName   = nullptr;
    QPushButton*      m_chkShowLength = nullptr;

    QColor            m_currentColor;
};

} // namespace cad::ui
